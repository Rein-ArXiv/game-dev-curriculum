## 들어가며

PBR의 전제는 렌더링 방정식, Cook-Torrance 미세면 BRDF, $D$(GGX)·$G$(Smith)·$F$(Fresnel-Schlick), metallic/roughness 워크플로, IBL의 split-sum과 Kulla-Conty 다중 산란 보정이다. 이 글은 그 수식을 *DX11에서 어떻게 구현하느냐* 를 다룬다. 같은 수식을 OpenGL·Vulkan으로 옮기면 셰이더 언어와 자원 바인딩만 달라진다.

필요한 식은 다음과 같다. 정반사 $f_{\text{spec}} = \dfrac{D\,G\,F}{4(\mathbf{n}\cdot\mathbf{l})(\mathbf{n}\cdot\mathbf{v})}$, 확산 $f_{\text{diff}} = (1-F)(1-\text{metallic})\dfrac{c_{\text{albedo}}}{\pi}$, GGX는 $\alpha=\text{roughness}^2$, 정반사 IBL은 프리필터 맵 × BRDF LUT(split-sum)로 근사한다. 이 값들을 셰이더에 전달하고 전처리를 굽는 것이 DX11의 일이다.

여기까지의 코드는 외부 모델을 Blinn-Phong 수준 셰이딩으로 그릴 수 있다고 가정한다 — 정점에 위치·법선·UV·접선이 있고, 픽셀 셰이더가 텍스처를 샘플한다. 도달점은 metallic/비금속이 구별되는 PBR 셰이딩에 IBL 환경광이 입혀진 모델이며, IBL 전처리는 DX11 Compute Shader로, 런타임 셰이딩은 픽셀 셰이더의 텍스처 페치로 정리된다.


---

## 목차
1. [BRDF 세 항의 HLSL 구현](#1-brdf-세-항의-hlsl-구현)
2. [metallic/roughness 분기와 전체 BRDF](#2-metallicroughness-분기와-전체-brdf)
3. [셰이더 입출력과 텍스처 바인딩](#3-셰이더-입출력과-텍스처-바인딩)
4. [IBL 전처리: Compute Shader](#4-ibl-전처리-compute-shader)
5. [IBL 런타임 합성](#5-ibl-런타임-합성)


---

## 1. BRDF 세 항의 HLSL 구현

$D$·$G$·$F$를 각각 함수로 두고 곱해 분모로 나눈다. 셰이더 구현의 핵심은 수식 자체가 아니라 *부동소수점 방어* 다.

```hlsl
float D_GGX(float NoH, float roughness) {
    float a    = roughness * roughness;   // alpha = roughness^2
    float a2   = a * a;
    float NoH2 = NoH * NoH;
    float denom = NoH2 * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * denom * denom);   // 분모는 반드시 제곱
}

float G_SchlickGGX(float NoX, float k) {
    return NoX / (NoX * (1.0 - k) + k);
}
float G_Smith(float NoV, float NoL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;              // 직접광용 k = (rough+1)^2 / 8
    return G_SchlickGGX(NoV, k) * G_SchlickGGX(NoL, k);
}

float3 F_Schlick(float VoH, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
}
```

`D_GGX`의 분모를 `denom * denom`으로 두 번 곱하는 것이 핵심 — 식의 분모 전체가 제곱이라 한 번만 곱하면 결과가 어긋난다. `G_Smith`의 $k$는 직접광용 $(\text{rough}+1)^2/8$이고, IBL용 $k=\text{roughness}^2/2$는 4절의 BRDF LUT에서 따로 쓴다. 두 $k$를 혼동하면 직접광과 환경광의 거칠기가 어긋나 보인다.

`F_Schlick`의 `pow(x, 5.0)`은 `x2=x*x; x2*x2*x`로 직접 풀면 일부 GPU에서 빠르고, 모바일에서는 Spherical Gaussian 근사 `exp2((-5.55473*VoH - 6.98316)*VoH)`를 쓰기도 한다.

세 항을 묶은 Cook-Torrance 함수:

```hlsl
float3 CookTorranceSpec(float3 N, float3 V, float3 L, float roughness, float3 F0) {
    float3 H  = normalize(V + L);
    float NoV = saturate(dot(N, V));
    float NoL = saturate(dot(N, L));
    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));

    float  D = D_GGX(NoH, roughness);
    float  G = G_Smith(NoV, NoL, roughness);
    float3 F = F_Schlick(VoH, F0);
    return (D * G * F) / (4.0 * NoV * NoL + 0.0001);   // 0으로 나누기 방어
}
```

분모에 더하는 `0.0001`은 grazing 각도에서 $NoV$·$NoL$이 0에 수렴할 때의 0 나누기를 막는다. 한 픽셀의 NaN은 블렌딩과 후처리를 거치며 화면 전체로 번지므로, `saturate`로 내적을 $[0,1]$에 묶는 방어와 함께 PBR 셰이더 전반에서 반복되는 패턴이다.


## 2. metallic/roughness 분기와 전체 BRDF

metallic 한 스칼라가 $F_0$와 확산색을 동시에 분기시킨다.

```hlsl
float3 PBR(float3 N, float3 V, float3 L, float3 lightColor,
           float3 albedo, float metallic, float roughness)
{
    float NoL = saturate(dot(N, L));
    if (NoL <= 0.0) return float3(0, 0, 0);     // 표면 뒤 광원은 기여 없음

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float3 spec = CookTorranceSpec(N, V, L, roughness, F0);

    float3 F  = F_Schlick(saturate(dot(normalize(V + L), V)), F0);
    float3 kD = (1.0 - F) * (1.0 - metallic);   // 에너지 보존 + 금속 차감
    float3 diff = kD * albedo / 3.14159265;

    return (diff + spec) * lightColor * NoL;
}
```

`F0 = lerp(0.04, albedo, metallic)` 한 줄이 워크플로의 핵심이다. metallic 0이면 $F_0=0.04$·확산색=albedo, metallic 1이면 $F_0$=albedo·확산=0. 같은 base color 텍스처가 metallic에 따라 정반대 의미(확산색 ↔ 정반사색)로 해석된다. `kD = (1-F)(1-metallic)`이 $k_S+k_D\le 1$을 보장한다.

roughness는 0에 너무 가까우면 specular 하이라이트가 한 픽셀보다 작아져 깜빡인다(specular aliasing). 최소값 클램프가 표준 방어다.

```hlsl
roughness = max(roughness, 0.04);   // 0.04~0.08 클램프가 표준
```

근본 해법은 법선맵 밉 레벨에 따라 roughness를 키우는 specular AA(Toksvig, LEAN)이지만, 단순 클램프만으로 깜빡임의 대부분이 사라진다.


## 3. 셰이더 입출력과 텍스처 바인딩

DX11은 텍스처·샘플러를 레지스터에 명시적으로 바인딩한다. glTF 표준은 metallic·roughness를 한 텍스처의 B·G 채널에 묶는다.

```hlsl
struct PSInput {
    float4 pos      : SV_Position;
    float3 worldPos : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float3x3 TBN    : TEXCOORD1;
};

Texture2D    baseColorMap  : register(t0);
Texture2D    normalMap     : register(t1);
Texture2D    metalRoughMap : register(t2);   // G = Roughness, B = Metallic (glTF)
SamplerState linearWrap    : register(s0);

float4 PSMain(PSInput input) : SV_Target {
    float3 baseColor = baseColorMap.Sample(linearWrap, input.uv).rgb;
    float3 normalT   = normalMap.Sample(linearWrap, input.uv).xyz * 2.0 - 1.0;
    float2 mr        = metalRoughMap.Sample(linearWrap, input.uv).gb;
    float  roughness = max(mr.x, 0.04);
    float  metallic  = mr.y;

    float3 N = normalize(mul(normalT, input.TBN));
    float3 V = normalize(cameraPos - input.worldPos);

    float3 color = 0;
    for (int i = 0; i < numLights; ++i) {
        float3 L = normalize(lights[i].pos - input.worldPos);
        color += PBR(N, V, L, lights[i].color, baseColor, metallic, roughness);
    }
    return float4(color, 1.0);
}
```

DX11 고유의 함정은 *텍스처 포맷의 색 공간* 이다. base color는 sRGB 포맷(`DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`)으로 만들어 샘플 시 자동 선형화되게 하고, normal·metalRough는 데이터값이므로 반드시 선형 포맷(`_UNORM`)으로 둔다. 이 둘을 바꾸면 법선이 휘거나 거칠기가 어긋나며, metalRough에 밉맵을 생성할 때 색 공간 변환이 끼면 안 되므로 선형 포맷이 맞다. 법선 텍셀의 `* 2.0 - 1.0`은 $[0,1]$ 텍셀을 $[-1,1]$ 벡터로 펴는 표준 디코딩이다.


## 4. IBL 전처리: Compute Shader

IBL의 세 전처리(조도 맵·프리필터 맵·BRDF LUT)를 DX11 Compute Shader로 굽는다. 큐브맵 6면은 `RWTexture2DArray`의 배열 인덱스(0~5)로 동시에 채운다. Compute Shader에는 픽셀 미분이 없어 `SampleLevel`로 밉을 명시해야 한다.

조도 맵(확산): 법선 방향 반구를 구좌표로 적분한다.

```hlsl
TextureCube  environmentMap : register(t0);
RWTexture2DArray<float4> irradianceMap : register(u0);   // 6면 배열
SamplerState linearSampler : register(s0);

[numthreads(8, 8, 1)]
void IrradianceCS(uint3 tid : SV_DispatchThreadID) {
    float2 uv = (tid.xy + 0.5) / float2(irradianceSize, irradianceSize) * 2.0 - 1.0;
    float3 N  = GetCubeDirection(tid.z, uv);     // tid.z = 면(0~5)

    float3 irradiance = 0; int samples = 0; float delta = 0.025;
    for (float phi = 0; phi < 2*3.14159; phi += delta)
    for (float theta = 0; theta < 0.5*3.14159; theta += delta) {
        float3 tan = float3(sin(theta)*cos(phi), sin(theta)*sin(phi), cos(theta));
        float3 world = TangentToWorld(tan, N);
        irradiance += environmentMap.SampleLevel(linearSampler, world, 0).rgb
                      * cos(theta) * sin(theta);   // 램버트 cos × 야코비안 sin
        samples++;
    }
    irradianceMap[tid] = float4(3.14159 * irradiance / samples, 1.0);
}
```

적분의 $\cos\theta$는 램버트 코사인, $\sin\theta$는 구좌표 야코비안이다 — $\sin\theta$를 빠뜨리면 천정이 과하게 밝아진다. 디퓨즈는 본래 흐릿해 32×32 큐브맵이면 충분하고, 정적 환경이면 로딩 시 한 번만 굽는다.

프리필터 맵(정반사): roughness별로 밉 체인에 GGX 중요도 표집 결과를 담는다.

```hlsl
[numthreads(8, 8, 1)]
void SpecularCS(uint3 tid : SV_DispatchThreadID, uint mip : MIPLEVEL) {
    float  roughness = mip / float(maxMipLevels - 1);
    float3 R = GetCubeDirection(tid.z, /* uv */);
    float3 N = R, V = R;                  // N=V=R 가정으로 적분 1차원 축소

    float3 prefiltered = 0; float totalWeight = 0;
    for (uint i = 0; i < 1024; ++i) {
        float2 xi = Hammersley(i, 1024);          // low-discrepancy
        float3 H  = ImportanceSampleGGX(xi, N, roughness);
        float3 L  = reflect(-V, H);
        float NoL = saturate(dot(N, L));
        if (NoL > 0) {
            prefiltered += environmentMap.SampleLevel(linearSampler, L, 0).rgb * NoL;
            totalWeight += NoL;
        }
    }
    specularMap[uint3(tid.xy, tid.z)] = float4(prefiltered / totalWeight, 1.0);
}
```

$N=V=R$ 가정은 시선 정보를 버려 적분을 한 차원 줄이는 실시간 IBL의 표준 절충이다(비스듬한 각의 늘어진 반사는 표현 못 함). 균등 표집은 GGX의 좁은 분포에 낭비되므로 Hammersley + GGX 중요도 표집으로 분포가 큰 방향에 집중시킨다.

```hlsl
float2 Hammersley(uint i, uint N) {
    float vdc = float(reversebits(i)) * 2.3283064365386963e-10;   // Van der Corput
    return float2(float(i) / float(N), vdc);
}
float3 ImportanceSampleGGX(float2 xi, float3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * 3.14159 * xi.x;
    float cosT = sqrt((1.0 - xi.y) / (1.0 + (a*a - 1.0) * xi.y));
    float sinT = sqrt(1.0 - cosT * cosT);
    float3 H = float3(sinT*cos(phi), sinT*sin(phi), cosT);
    float3 up = abs(N.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 T  = normalize(cross(up, N));
    return normalize(T*H.x + cross(N, T)*H.y + N*H.z);
}
```

`up`을 법선과 거의 평행할 때 갈아 끼우는 것은 외적이 0 벡터가 되어 접선 기저가 무너지는 것을 막는 방어다.

BRDF LUT: 환경과 무관한 split-sum의 둘째 인수를 $(NoV, roughness)$ 2D 텍스처로 굽는다. 여기서는 IBL용 $k=\text{roughness}^2/2$를 써야 한다.

```hlsl
[numthreads(8, 8, 1)]
void BRDFLutCS(uint3 tid : SV_DispatchThreadID) {
    float NoV = (tid.x + 0.5) / lutSize;
    float roughness = (tid.y + 0.5) / lutSize;
    float3 V = float3(sqrt(1.0 - NoV*NoV), 0, NoV);

    float scale = 0, bias = 0;
    for (uint i = 0; i < 1024; ++i) {
        float2 xi = Hammersley(i, 1024);
        float3 H  = ImportanceSampleGGX(xi, float3(0,0,1), roughness);
        float3 L  = reflect(-V, H);
        float NoL = saturate(L.z), NoH = saturate(H.z), VoH = saturate(dot(V, H));
        if (NoL > 0) {
            float k = (roughness*roughness) / 2.0;          // IBL용 k
            float G = G_SchlickGGX(NoV, k) * G_SchlickGGX(NoL, k);
            float G_Vis = (G * VoH) / (NoH * NoV);
            float Fc = pow(1.0 - VoH, 5.0);
            scale += (1.0 - Fc) * G_Vis;
            bias  += Fc * G_Vis;
        }
    }
    brdfLut[tid.xy] = float2(scale, bias) / 1024.0;
}
```

LUT는 256×256 두 채널(`DXGI_FORMAT_R16G16_FLOAT`)로 굽고 *클램프 샘플러* 로 읽는다 — 가장자리 wrap이 섞이면 grazing 각에서 어긋난다. 직접광용 $k$로 구우면 환경광 스페큘러가 전반적으로 틀어진다.


## 5. IBL 런타임 합성

전처리된 세 텍스처를 픽셀 셰이더에서 합친다. 무거운 적분은 전부 전처리로 옮겨졌으므로 런타임은 텍스처 페치 세 번이다.

```hlsl
TextureCube irradianceMap : register(t10);
TextureCube specularMap   : register(t11);
Texture2D   brdfLut       : register(t12);

float3 IBL(float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0) {
    float  NoV = saturate(dot(N, V));
    float3 R   = reflect(-V, N);

    // 거칠기 반영 Fresnel (Lagarde 변형) — 직접광과 같은 kD 규약을 쓴다
    float3 F  = F0 + (max(1.0 - roughness, F0) - F0) * pow(1.0 - NoV, 5.0);
    float3 kD = (1.0 - F) * (1.0 - metallic);

    float3 diffuseIBL = irradianceMap.Sample(linearSampler, N).rgb
                        * albedo * kD;

    float3 prefilter = specularMap.SampleLevel(linearSampler, R,
                                               roughness * (maxMip - 1)).rgb;
    float2 envBRDF   = brdfLut.Sample(linearSampler, float2(NoV, roughness)).rg;
    float3 specularIBL = prefilter * (F0 * envBRDF.x + envBRDF.y);   // split-sum

    return diffuseIBL + specularIBL;
}
```

`prefilter * (F0 * envBRDF.x + envBRDF.y)`가 split-sum의 두 조각(환경맵 적분 × BRDF LUT)을 곱하는 부분이다. roughness에 `(maxMip - 1)`을 곱해 밉을 고르면 하드웨어 삼선형 필터가 인접 밉을 보간해 거칠기가 연속적으로 변해도 끊김이 없다. 디퓨즈에 $k_D=(1-F)(1-\text{metallic})$을 곱하는 것은 직접광과 동일하다. (거친 금속의 어두워짐을 막는 Kulla-Conty 보정은 BRDF LUT에 보정 채널을 함께 굽는 방식으로 더할 수 있다 — 이론은 전역 조명 문서 참고.)


---

## 결론

DX11에서 PBR은 두 갈래로 나뉜다 — 직접광은 픽셀 셰이더가 광원마다 Cook-Torrance를 합산하고, 환경광은 Compute Shader로 미리 구운 세 텍스처(조도·프리필터·BRDF LUT)를 페치한다. 둘은 같은 BRDF를 공유한다.

이 글의 DX11 고유 사항은 이론이 아니라 구현 디테일이었다 — `RWTexture2DArray`로 큐브 6면 동시 기록, Compute Shader의 `SampleLevel` 명시(픽셀 미분 부재), sRGB와 선형 텍스처 포맷의 엄격한 구분, BRDF LUT의 클램프 샘플러와 IBL용 $k$, 부동소수점 0 나누기·NaN 방어. 같은 수식을 명시적 모델(DX12·Vulkan)로 옮기면 자원 바인딩이 PSO·디스크립터·배리어로 바뀔 뿐, 셰이더가 계산하는 PBR은 동일하다.

## 더 읽을거리

- Brian Karis, ["Real Shading in Unreal Engine 4"](https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf) (SIGGRAPH 2013) — split-sum·prefilter·BRDF LUT의 원전.
- Microsoft, [Direct3D 11 Compute Shader 개요](https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-compute-shader) — `RWTexture2DArray`·디스패치 모델.
- Romain Guy, Mathias Agopian, [Google Filament 문서](https://google.github.io/filament/Filament.html) — 실시간 PBR 구현의 수식·코드 종합.
- [LearnOpenGL: PBR](https://learnopengl.com/PBR/Theory) — 같은 수식의 GLSL 구현(API만 다름).
