## 들어가며

PBR과 IBL의 전제는 Cook-Torrance·GGX·Smith·Fresnel-Schlick, split-sum 근사, Kulla-Conty 다중 산란 보정이다. 이 글은 그 수식과 전처리를 *Vulkan에서 구현* 하는 데 집중한다. 같은 수식을 OpenGL·DX로 옮기면 자원 바인딩만 달라진다.

필요한 식은 다음과 같다. 정반사 $f_{\text{spec}}=\dfrac{DGF}{4(\mathbf n\cdot\mathbf l)(\mathbf n\cdot\mathbf v)}$, GGX는 $\alpha=\text{roughness}^2$, 정반사 IBL은 프리필터 맵 × BRDF LUT, 다중 산란 보정은 LUT 두 채널의 합 $E_{ss}=A+B$로 손실분을 되돌린다. Vulkan의 일은 이 값들을 디스크립터·push constant로 전달하고, 전처리를 off-screen 렌더 패스와 컴퓨트로 굽는 것이다.

여기까지의 코드는 외부 메시를 깊이·MSAA로 그리되 셰이딩은 알베도 출력 수준이라 가정한다. 도달점은 임의의 HDR 환경맵 아래 metallic/비금속이 자연스러운 IBL 라이팅을 받는 장면이다.


---

## 목차
1. [디스크립터 세트와 Push Constant](#1-디스크립터-세트와-push-constant)
2. [BRDF의 GLSL 구현](#2-brdf의-glsl-구현)
3. [HDR 환경맵 로딩](#3-hdr-환경맵-로딩)
4. [Equirectangular에서 Cubemap으로](#4-equirectangular에서-cubemap으로)
5. [IBL 전처리 셰이더](#5-ibl-전처리-셰이더)
6. [IBL 합성과 워크플로](#6-ibl-합성과-워크플로)


---

## 1. 디스크립터 세트와 Push Constant

OpenGL과 달리 Vulkan은 셰이더가 읽는 모든 자원을 디스크립터 세트로 묶어 명시적으로 바인딩한다. 핵심 설계 원칙은 *변경 빈도가 같은 자원을 한 세트에 모으는 것* 이다. `vkCmdBindDescriptorSets`로 세트 N을 바인딩하면 pipeline layout이 그 세트들에 대해 호환되지 않는 한 N보다 높은 번호의 세트가 무효화될 수 있으므로(pipeline layout compatibility 규칙), 자주 바뀌는 세트를 높은 번호에 두면 드물게 바뀌는 낮은 번호 세트의 재바인딩을 피한다.

| Set | 변경 빈도 | 자원 |
|---|---|---|
| 0 | global | irradianceMap, prefilterMap, brdfLUT (cubemap×2 + 2D) |
| 1 | per-frame | viewProj, cameraPos, light data |
| 2 | per-material | albedo, normal, metallic-roughness, AO, emissive (5종) |
| 3 | per-object | model matrix (push constant 권장) |

IBL 세 텍스처는 환경이 바뀌지 않는 한 불변이라 가장 낮은 번호인 global 세트(0)가 적합하다. per-material 세트는 fragment 단계에 텍스처 5종을 노출한다.

```cpp
VkDescriptorSetLayoutBinding set2Bindings[] = {
    { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
};
```

매 드로우마다 바뀌는 작은 데이터(64바이트 모델 행렬)는 디스크립터 세트보다 push constant가 빠르다 — 디스크립터를 거치지 않고 명령 버퍼에 값이 직접 기록된다.

```cpp
VkPushConstantRange pcRange{};
pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
pcRange.offset = 0;
pcRange.size = sizeof(glm::mat4);    // 64 바이트
```

```glsl
layout(push_constant) uniform Push { mat4 model; } pc;   // 정점 셰이더
```

스펙이 보장하는 push constant 최소 크기는 128바이트($4\times4$ 행렬 2개)다. `VkPhysicalDeviceLimits::maxPushConstantsSize`로 확인하되(데스크톱은 256이 흔함), 이식성을 위해 128을 가정하고 그 이상은 UBO로 넘긴다.


## 2. BRDF의 GLSL 구현

PBR 수식을 GLSL로 옮긴다. Disney 관례로 `roughness*roughness`를 $\alpha$로 쓰고, 직접광 $k=(\text{rough}+1)^2/8$ / IBL $k=\alpha/2\;(=\text{roughness}^2/2)$로 나눈다.

```glsl
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness*roughness, a2 = a*a;
    float NoH = max(dot(N, H), 0.0);
    float denom = NoH*NoH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * denom * denom);
}
float GeometrySchlickGGX(float NoX, float k) { return NoX / (NoX*(1.0-k) + k); }
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float r = roughness + 1.0, k = (r*r) / 8.0;          // 직접광용 k
    return GeometrySchlickGGX(max(dot(N,V),0.0), k)
         * GeometrySchlickGGX(max(dot(N,L),0.0), k);
}
vec3 FresnelSchlick(float VoH, vec3 F0) { return F0 + (1.0-F0)*pow(1.0-VoH, 5.0); }
```

$F_0$는 `mix(vec3(0.04), albedo, metallic)`로 합성한다. 거친 금속이 단일 산란에서 어두워지는 손실은, 이미 굽는 BRDF LUT를 재활용하는 Fdez-Agüera 보정으로 추가 텍스처 없이 되돌린다.

```glsl
vec3 MultiscatterBRDF(vec3 F0, float roughness, float NoV) {
    vec2 brdf = texture(brdfLUT, vec2(NoV, roughness)).rg;
    vec3 Favg = F0 + (1.0 - F0) / 21.0;     // 평균 Fresnel 근사
    vec3 FssEss = F0 * brdf.x + brdf.y;     // 단일 산란 기여
    float Ess = brdf.x + brdf.y;            // 단일 산란이 보존한 에너지
    float Ems = 1.0 - Ess;                  // 손실분
    vec3 Fms = FssEss * Favg / (1.0 - Favg * Ems);
    return FssEss + Fms * Ems;
}
```


## 3. HDR 환경맵 로딩

IBL 입력은 8비트가 아니라 HDR이다. 하늘·그늘의 휘도 차는 수천 배라 256단계로는 못 담는다. Radiance `.hdr`(RGBE)을 stb_image의 `stbi_loadf`로 float 픽셀로 디코딩한다. Vulkan 메모리 레이아웃에 맞춰 RGBA를 강제하는 것이 핵심이다.

```cpp
int w, h, ch;
float* px = stbi_loadf("environment.hdr", &w, &h, &ch, 4);   // RGBA 강제
VkDeviceSize size = (VkDeviceSize)w * h * 4 * sizeof(float);  // RGBA32F
```

`desired_channels=4`로 RGBA float을 강제하는 이유는 `VK_FORMAT_R32G32B32A32_SFLOAT`와 정확히 맞추기 위해서다. RGB 3채널(12바이트)을 16바이트 포맷 이미지에 그대로 복사하면 채널이 어긋나거나 staging 끝을 넘어 읽는다. `R32G32B32_SFLOAT`(3채널)는 많은 GPU에서 샘플링 미지원이라 RGBA 패딩이 이식성에 안전하다.

대부분 HDR은 등직사각형(equirectangular, 2:1) 투영이다. 이를 2D 이미지로 올린다.

```cpp
VkImageCreateInfo info{};
info.imageType = VK_IMAGE_TYPE_2D;
info.format = VK_FORMAT_R32G32B32A32_SFLOAT;
info.extent = { (uint32_t)w, (uint32_t)h, 1 };
info.mipLevels = 1; info.arrayLayers = 1;
info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
info.tiling = VK_IMAGE_TILING_OPTIMAL;
info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
info.samples = VK_SAMPLE_COUNT_1_BIT;
// host-visible staging → vkCmdCopyBufferToImage → 레이아웃 전이
```

복사 전후로 `UNDEFINED → TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL` 배리어가 필요하다. 4K 환경맵(4096×2048 RGBA32F)은 128MB이므로, 로딩 단계만 32비트를 쓰고 큐브맵부터는 16비트로 내리는 편이 낫다. 등직사각형은 샘플마다 `atan`/`asin`이 들고 극점이 늘어나므로, 런타임 IBL은 큐브맵으로 변환해 둔다.


## 4. Equirectangular에서 Cubemap으로

큐브맵은 방향 벡터로 곧장 샘플되어 삼각함수가 없다. Vulkan에서는 6 array layer 이미지에 `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`를 붙여 만든다.

```cpp
VkImageCreateInfo cubeInfo{};
cubeInfo.imageType = VK_IMAGE_TYPE_2D;
cubeInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;    // half float — 메모리 절반
cubeInfo.extent = { 512, 512, 1 };
cubeInfo.mipLevels = 10; cubeInfo.arrayLayers = 6;  // floor(log2(512))+1 = 10, 6 면
cubeInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
cubeInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
               | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
```

16비트 half float로 낮추면 512×512×6이 24MB→12MB로 절반이고(밉 체인까지 포함하면 약 16MB), HDR 휘도에 정밀도가 충분하다. 변환은 *화면이 아닌 큐브맵 면에 직접 렌더링* 한다 — 원점에 단위 큐브를 두고 카메라를 6방향으로 돌리며 90도 화각으로 각 면을 그린다. 면을 다 그린 뒤에는 `vkCmdBlitImage`로 단계별 축소해 밉 체인을 채운다 — 5절의 Filtered Importance Sampling이 `textureLod`로 상위 밉을 고르려면 이 밉 체인이 반드시 있어야 한다.

```cpp
glm::mat4 captureProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
glm::mat4 captureViews[6] = {
    glm::lookAt(glm::vec3(0), glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)),
    glm::lookAt(glm::vec3(0), glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
    glm::lookAt(glm::vec3(0), glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)),
    glm::lookAt(glm::vec3(0), glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
    glm::lookAt(glm::vec3(0), glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)),
    glm::lookAt(glm::vec3(0), glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0)),
};
```

up 벡터가 $(0,1,0)$이 아니라 $(0,-1,0)$ 등인 점에 주의 — 큐브맵 면 좌표계가 위→아래로 증가하는 관례라, 부호를 틀리면 환경이 반전된다. 각 면을 single-layer image view로 framebuffer를 구성해 6회 그리거나, `VK_KHR_multiview`로 한 드로우에 6 layer를 채운다. fragment 셰이더는 큐브 정점 방향을 등직사각형 UV로 변환해 원본을 페치한다.

```glsl
layout(set = 0, binding = 0) uniform sampler2D equirect;
const vec2 invAtan = vec2(0.1591, 0.3183);   // (1/2π, 1/π)
vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y)) * invAtan + 0.5;
    return uv;
}
void main() { fragColor = vec4(texture(equirect, SampleSphericalMap(normalize(vPosition))).rgb, 1.0); }
```


## 5. IBL 전처리 셰이더

세 전처리(조도·프리필터·LUT)를 굽는다. 적분 자체는 조도 맵, 프리필터 맵, BRDF LUT라는 세 산출물로 나뉘며, 여기서는 이를 Vulkan 셰이더와 오프스크린 패스로 만드는 방법을 본다.

조도 맵(확산): 법선 반구를 적분한다. 디퓨즈는 매우 부드러워 32×32×6(RGBA16F 48KB)이면 충분하다. 분산을 줄이려면 Hammersley 준난수로 반구를 고르게 덮는다.

```glsl
layout(set = 0, binding = 0) uniform samplerCube envMap;
void main() {
    vec3 N = normalize(vWorldPos);
    vec3 right = normalize(cross(vec3(0,1,0), N));
    vec3 up    = normalize(cross(N, right));
    vec3 irradiance = vec3(0.0); int samples = 0;
    for (float phi = 0.0; phi < 6.2831; phi += 0.025)
    for (float theta = 0.0; theta < 1.5708; theta += 0.025) {
        vec3 tan = vec3(sin(theta)*cos(phi), sin(theta)*sin(phi), cos(theta));
        vec3 w = tan.x*right + tan.y*up + tan.z*N;
        irradiance += texture(envMap, w).rgb * cos(theta) * sin(theta);  // cos가중 × 야코비안
        samples++;
    }
    fragColor = vec4(3.14159 * irradiance / float(samples), 1.0);
}
```

`cos(theta)`는 램버트 가중, `sin(theta)`는 구좌표 야코비안 — 빠뜨리면 천정이 과하게 밝아진다.

프리필터 맵(정반사): 거칠기별 결과를 mip 체인에 담는다. Vulkan 구현의 요점은 *Filtered Importance Sampling* — 샘플 입체각과 텍셀 입체각을 비교해 mip을 골라 거친 표면의 반딧불이 노이즈를 억제한다(`textureLod`).

```glsl
layout(push_constant) uniform Push { float roughness; } pc;
void main() {
    vec3 N = normalize(vWorldPos), R = N, V = R;   // N=V=R 가정 (split-sum 근사)
    vec3 prefiltered = vec3(0.0); float totalWeight = 0.0;
    for (uint i = 0u; i < 1024u; ++i) {
        vec2 Xi = Hammersley(i, 1024u);
        vec3 H  = ImportanceSampleGGX(Xi, N, pc.roughness);
        vec3 L  = normalize(2.0*dot(V,H)*H - V);
        float NoL = max(dot(N, L), 0.0);
        if (NoL > 0.0) {
            float D = DistributionGGX(N, H, pc.roughness);
            float pdf = D * max(dot(N,H),0.0) / (4.0*max(dot(H,V),0.0)) + 0.0001;
            float saTexel  = 4.0*3.14159 / (6.0*512.0*512.0);
            float saSample = 1.0 / (1024.0*pdf + 0.0001);
            float mip = pc.roughness == 0.0 ? 0.0 : 0.5*log2(saSample/saTexel);
            prefiltered += textureLod(envMap, L, mip).rgb * NoL;
            totalWeight += NoL;
        }
    }
    fragColor = vec4(prefiltered / totalWeight, 1.0);
}
```

BRDF LUT: 환경과 무관한 split-sum의 둘째 인수를 storage image(`rg16f`)에 컴퓨트로 굽는다. IBL용 $k=\alpha/2=\text{roughness}^2/2$를 써야 한다.

```glsl
layout(local_size_x = 16, local_size_y = 16) in;
layout(set = 0, binding = 0, rg16f) uniform writeonly image2D lutOutput;
void main() {
    vec2 uv = (vec2(gl_GlobalInvocationID.xy) + 0.5) / 512.0;
    float NoV = uv.x, roughness = uv.y;
    vec3 V = vec3(sqrt(1.0 - NoV*NoV), 0.0, NoV), N = vec3(0,0,1);
    float scale = 0.0, bias = 0.0;
    for (uint i = 0u; i < 1024u; ++i) {
        vec2 Xi = Hammersley(i, 1024u);
        vec3 H = ImportanceSampleGGX(Xi, N, roughness);
        vec3 L = normalize(2.0*dot(V,H)*H - V);
        float NoL = max(L.z,0.0), NoH = max(H.z,0.0), VoH = max(dot(V,H),0.0);
        if (NoL > 0.0) {
            float k = (roughness*roughness)/2.0;            // IBL용 k
            float G = GeometrySchlickGGX(NoV,k)*GeometrySchlickGGX(NoL,k);
            float G_Vis = (G * VoH) / (NoH * NoV);
            float Fc = pow(1.0 - VoH, 5.0);
            scale += (1.0 - Fc) * G_Vis; bias += Fc * G_Vis;
        }
    }
    imageStore(lutOutput, ivec2(gl_GlobalInvocationID.xy), vec4(scale/1024.0, bias/1024.0, 0, 1));
}
```

프레넬을 $F = F_0\cdot\text{scale} + \text{bias}$로 분해했으므로 LUT가 $F_0$에 독립이다 — 512×512 `VK_FORMAT_R16G16_SFLOAT`(1MB) 한 장을 모든 장면이 공유한다. 환경맵이 바뀌어도 LUT는 그대로 두고 irradiance·prefilter만 다시 굽는다.


## 6. IBL 합성과 워크플로

전처리한 세 텍스처를 fragment 셰이더에서 합친다. roughness 하한 0.04는 GGX 분모 0(NaN)을 막는다.

```glsl
vec3 N = normalize(vWorldNormal);
vec3 V = normalize(uCameraPos - vWorldPos);
vec3 R = reflect(-V, N);
float NoV = max(dot(N, V), 0.0);

vec3  albedo    = texture(albedoMap, vUv).rgb;
float metallic  = texture(metallicRoughnessMap, vUv).b;   // glTF: B=metallic
float roughness = max(texture(metallicRoughnessMap, vUv).g, 0.04);  // G=roughness
float ao        = texture(aoMap, vUv).r;
vec3  F0 = mix(vec3(0.04), albedo, metallic);

// 거칠기 반영 Fresnel (Lagarde 변형)
vec3 F  = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NoV, 5.0);
vec3 kD = (1.0 - F) * (1.0 - metallic);

vec3 irradiance = texture(irradianceMap, N).rgb;
vec3 diffuse    = irradiance * albedo;

const float MAX_MIP = 4.0;
vec3 prefiltered = textureLod(prefilterMap, R, roughness * MAX_MIP).rgb;
vec3 specular = prefiltered * MultiscatterBRDF(F0, roughness, NoV);  // split-sum BRDF 항 + 다중 산란 보정

vec3 ambient = (kD * diffuse + specular) * ao;
```

`F0 * brdf.x + brdf.y`가 split-sum의 BRDF 항이다. LUT가 $F=F_0 A+B$로 분해해 구운 것이므로 여기에 다시 각도 의존 $F$를 곱하면 프레넬이 이중 적용된다. 위의 거칠기 반영 프레넬 $F$는 $k_D$ 계산에만 쓴다. glTF 2.0은 metallic/roughness를 한 텍스처의 B/G에 패킹하므로 페치 한 번에 둘을 읽고, R에 AO까지 묶으면 occlusion-metallic-roughness를 한 텍스처로 처리한다. 직접광은 split-sum 없이 Cook-Torrance를 그대로 합산해 ambient에 더한다.

마지막은 톤 매핑과 감마다. 여기서 *Vulkan 고유의 함정* 이 있다.

```glsl
vec3 color = ambient + directLighting;
color = color / (color + vec3(1.0));        // Reinhard (또는 ACES filmic)
// 스왑체인이 sRGB 포맷이면 하드웨어가 인코딩하므로 아래 pow는 생략
// color = pow(color, vec3(1.0/2.2));
outColor = vec4(color, 1.0);
```

스왑체인 포맷이 `*_SRGB`면 하드웨어가 자동으로 sRGB 인코딩을 하므로 셰이더의 `pow(1/2.2)`를 *생략* 해야 한다 — 둘 다 적용하면 감마 인코딩이 두 번 걸려 색이 씻긴 듯 밝아진다. DX·GL과 결과는 같되, "어디서 sRGB 인코딩이 일어나는가"를 스왑체인 포맷으로 명시 제어하는 것이 Vulkan 방식이다.


---

## 결론

Vulkan에서 PBR/IBL의 본체는 수식 자체보다 *명시적 자원 관리* 다. 변경 빈도별 디스크립터 세트와 push constant, HDR의 RGBA 패딩과 staging·레이아웃 전이, `CUBE_COMPATIBLE` 6-layer 이미지와 off-screen 렌더 패스(up=−y 주의), 프리필터의 Filtered Importance Sampling과 `textureLod`, storage image로 굽는 BRDF LUT, 그리고 스왑체인 sRGB 포맷에 따른 감마 처리 분기가 핵심이다. 셰이더가 계산하는 PBR 수식은 API와 무관하고, Vulkan에서 어려운 부분은 이 바인딩·전처리 파이프라인에 있다.

무거운 환경맵 적분은 전처리로 옮겨져 런타임은 텍스처 페치 세 번으로 압축된다. 이 문서의 범위는 PBR/IBL 전처리와 런타임 바인딩까지이며, 그림자처럼 별도 깊이 패스를 요구하는 효과는 같은 렌더 패스·프레임버퍼 의존성 원리 위에 추가된다.

## 더 읽을거리

- Brian Karis, ["Real Shading in Unreal Engine 4"](https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf) — split-sum 근사의 원전.
- Christopher Kulla, Alejandro Conty, "Revisiting Physically Based Shading at Imageworks" (SIGGRAPH 2017) — 다중 산란 에너지 보정.
- [Sascha Willems, Vulkan glTF-PBR](https://github.com/SaschaWillems/Vulkan-glTF-PBR) — 본문과 같은 IBL 전처리의 완성 구현.
- [Khronos glTF Sample Renderer](https://github.com/KhronosGroup/glTF-Sample-Renderer) — 표준 PBR 셰이더 레퍼런스.
