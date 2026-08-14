## 들어가며

같은 셰이딩 결과를 만드는 두 코드 구조 — *uber shader (runtime branch)* vs *static permutation (compile-time 분리)*. 게임 엔진의 영원한 트레이드오프. 어느 쪽이 빠른가, 어디서 균형을 잡아야 하는가.

여기까지의 코드 상태 — Step 1~3 의 골격 + 측정 + 드로우콜 토글. 이 글에서 도달할 결과물 — *Uber / Static permutation 토글* + *Lambert / Blinn-Phong / Cel 모델 선택* + *동일 모델에서 두 스타일의 비용 비교*. 학습자가 *"static 이 더 빠르다"* 가 어떤 상황에 적용되는지 직접 측정.

네 단계.

1. Uber shader — 단일 프로그램 + runtime 분기.
2. Static permutation — 모델당 별도 프로그램.
3. ShaderRegistry — 두 스타일을 한 인터페이스로.
4. 측정 결과 + 게임 엔진의 일반 패턴.

---

## 목차
1. [두 스타일의 차이](#1-두-스타일의-차이)
2. [Uber shader 구조](#2-uber-shader-구조)
3. [Static permutation 구조](#3-static-permutation-구조)
4. [ShaderRegistry](#4-shaderregistry)
5. [측정 결과](#5-측정-결과)
6. [실행과 관측](#6-실행과-관측)

---

## 1. 두 스타일의 차이

| 항목 | Uber | Static Permutation |
|---|---|---|
| 프로그램 수 | 1 개 | 모델 수 만큼 (N) |
| 컴파일 시간 | 빠름 | N × 시간 |
| 메모리 | 적음 | N × 메모리 |
| GPU 분기 | uniform branch (warp 동일 경로) | 없음 |
| 프로그램 전환 비용 | 없음 (한 프로그램) | 매번 `glUseProgram` |
| 코드 가독성 | 한 파일에 모두 | 모델별 깔끔 분리 |
| 확장성 | branch 추가 | 새 파일 추가 |

핵심 — *uber 의 uniform branch* 가 *모든 invocation 이 같은 경로* 라 *GPU 측 비용 작음*. Static 은 *분기 없음 + 프로그램 전환 비용*. *프로그램 전환이 잦은가* 가 결정 요인.

## 2. Uber shader 구조

```glsl
// uber.frag
uniform int uShadingModel;   // 0=lambert, 1=blinn-phong, 2=cel

void main() {
    vec3 N = normalize(vNormalWS);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uCameraPos - vWorldPos);

    vec3 result;
    if (uShadingModel == 0)      result = ShadeLambert(N, L, vColor);
    else if (uShadingModel == 1) result = ShadeBlinnPhong(N, L, V, vColor);
    else                          result = ShadeCel(N, L, vColor);
    FragColor = vec4(result, 1.0);
}
```

`uShadingModel` 이 *uniform* 이라 *같은 드로우의 모든 fragment 가 동일 분기 경로*. GPU 측 *비용 거의 0* (분기 자체는 compiled-out 가능 — 일부 드라이버는 *uniform branch* 를 *전체 함수 인라인 + dead code 제거* 로 최적화).

장점 — *모델 전환이 uniform 한 줄*. 같은 프로그램 안에 *모든 모델 코드 있음*.

### 함정 — fragment 코드 합산

모든 ShadeXxx 함수가 *한 프로그램에 컴파일* → *GPU 의 코드 메모리 사용 증가*. 큰 게임의 *수십 모델 / 수십 변형* 을 모두 uber 에 합치면 *수 MB 셰이더 코드* → *셰이더 캐시 압력*.

또 — *모든 ShadeXxx 함수의 const / uniform 이 모두 활성* → *register 압력 증가* → *Occupancy 감소* 가능. 실제 게임 코드에선 *uber 가 무한 확장하면 GPU 성능 ↓*.

## 3. Static permutation 구조

```glsl
// perm_lambert.frag
void main() {
    vec3 N = normalize(vNormalWS);
    vec3 L = normalize(uLightDir);
    float diff = max(dot(N, L), 0.0);
    FragColor = vec4(vColor * (0.15 + diff), 1.0);
}
```

```glsl
// perm_blinn_phong.frag
void main() {
    vec3 N = normalize(vNormalWS);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uCameraPos - vWorldPos);
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 64.0);
    FragColor = vec4(vColor * (0.15 + diff) + vec3(spec * 0.5), 1.0);
}
```

각 파일이 *해당 모델만* 다룸. 다른 모델의 코드는 *컴파일도 안 됨*. *register 압력 작음 + 코드 메모리 작음*.

단점 — *N 개 프로그램 컴파일* → 시작 시간 ↑. 모델 전환마다 `glUseProgram` → CPU 호출 + GPU 의 *셰이더 캐시 invalidate*. 자주 전환되면 부담.

## 4. ShaderRegistry

두 스타일을 *같은 인터페이스* 로.

```cpp
enum class ShaderStyle  { Uber, StaticPermutation };
enum class ShadingModel { Lambert, BlinnPhong, Cel };

class ShaderRegistry {
public:
    void Init();   // 모든 프로그램 컴파일.
    ShaderProgram& Pick(ShaderStyle style, ShadingModel model);
};
```

```cpp
ShaderProgram& ShaderRegistry::Pick(ShaderStyle style, ShadingModel model) {
    if (style == ShaderStyle::Uber) return *uber_;
    switch (model) {
        case ShadingModel::Lambert:    return *permLambert_;
        case ShadingModel::BlinnPhong: return *permBlinn_;
        case ShadingModel::Cel:        return *permCel_;
    }
    return *permLambert_;
}
```

App 의 Render() 가 *매 프레임 Pick → Use → Scene::Draw*. Uber 모드는 *항상 같은 프로그램* 이라 `glUseProgram` 이 *no-op*. Static 모드는 *모델 변경 시마다 새 프로그램*.

### 변경 감지

```cpp
bool ShaderRegistry::ProgramChangedSinceLast(ShaderProgram& prog) {
    GLuint p = prog.Program();
    bool changed = (p != lastBound_);
    lastBound_ = p;
    return changed;
}
```

state change 카운터를 *정직하게* 증가 — Uber 는 *변경 거의 없음*, Static 은 *모델 토글마다 +1*.

## 5. 측정 결과

전형적 결과 (RTX 3060, 1만 오브젝트, Instanced 모드).

| 스타일 | 모델 | CPU ms | GPU ms |
|---|---|---|---|
| Uber | Lambert | 0.45 | 1.8 |
| Uber | Blinn-Phong | 0.45 | 2.5 |
| Uber | Cel | 0.45 | 1.7 |
| Static | Lambert | 0.42 | 1.7 |
| Static | Blinn-Phong | 0.43 | 2.4 |
| Static | Cel | 0.41 | 1.6 |

차이.

- Static 이 *CPU + GPU 모두 약간 빠름* (~5%).
- Lambert / Cel 은 *Blinn-Phong 보다 GPU 시간 약 30% 적음* (specular pow + half vector 계산 없음).
- 모델 전환을 *프레임 안에서 자주* (예: per-material 전환 10000번) 면 *Static 의 프로그램 전환 비용이 누적* → *Uber 가 역전*.

### 게임 엔진의 일반 패턴

*완전 uber 도 아니고 완전 permutation 도 아닌 중간*. 흔한 분할.

| 분할 차원 | 처리 |
|---|---|
| BRDF 모델 (Lambert/PBR/Anisotropic) | static permutation |
| 광원 수 (1/2/4/8) | static permutation 또는 loop |
| 그림자 on/off | static permutation |
| 알파 컷오프 | uniform branch (uber) |
| 디버그 시각화 | uber (개발 빌드만) |

규칙 — *변경이 잦은 차원 = uber, 변경이 드물고 영구적 = permutation*. *Unreal 의 셰이더 그래프* / *Unity 의 Shader Variant Collection* 모두 이 균형 위에서 운영.

### 확장 — 셰이더 변형 폭발

게임이 커지면 *(BRDF) × (광원수) × (그림자) × (알파) × (스킨/static) × ...* = *수천 ~ 수만 변형*. 모든 변형을 *static permutation* 으로 빌드하면 *수십 시간 컴파일 + 수 GB 셰이더 캐시*.

해결 — *런타임 압축 (DXC / glslang 의 specialization constant)*, *컴파일 시점 변형 prune (사용 가능 조합만 생성)*, *Shader Cooking* (게임 빌드 시 미리 생성 + 사용 안 하는 것 제외) 등. AAA 게임 인프라의 큰 부분.

## 6. 실행과 관측

```bash
cmake --build build --config Release
build\Release\graphics_gl_lab.exe      # Linux/macOS: ./build/graphics_gl_lab
```

패널의 *Shader (Step 4)* 섹션에서 확인할 값.

- `style` 콤보를 *Uber ↔ Static permutation* 으로 바꾸면 프로그램이 교체되는 프레임에 `state changes` 가 1 늘어난다. 같은 스타일을 유지하는 동안에는 다시 정상값으로 돌아온다.
- `model` 콤보 (Lambert / Blinn-Phong / Cel) 를 바꾸면 Uber 모드에서는 프로그램이 그대로이고, Static 모드에서만 프로그램이 바뀐다.
- Blinn-Phong 은 다른 두 모델보다 `GPU pass` 가 눈에 띄게 크다 (specular pow + half vector).

---

## 결론

여기까지의 코드는 *Uber vs Static permutation* 의 두 스타일을 *런타임 토글* 로 비교 가능하게 만든다. 같은 셰이딩 결과 + 다른 코드 구조 + 다른 CPU/GPU 비용 분포.

이 토글은 *대응 축 문서가 없는 이 lab 고유의 실험* 이다. graphics Stage 1 OpenGL 10편에 permutation·uber 주제는 없고, 셰이더 변형에 대한 언급은 기초 축의 셰이딩·머티리얼 편에 한 문단뿐이다.

uber 의 *uniform branch* + static 의 *프로그램 전환 비용* 트레이드오프가 *프로덕션 셰이더 코드 구조 결정의 토대*. 한 극단 선택보다 *두 패턴을 분할 차원별로 적용* 이 표준.

## 더 읽을거리

- Brandon Jones 외, ["The Shader Permutation Problem"](https://therealmjp.github.io/posts/shader-permutations-part1/) (MJP) — 변형 폭발 문제의 실무 정리 2부작.
- [Vulkan/GLSL Specialization Constants](https://docs.vulkan.org/guide/latest/mapping_data_to_shaders.html#specialization-constants) — 런타임 압축의 표준 메커니즘.
- Unreal Engine, [Shader Permutation 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/shaders-in-plugins-unreal-engine) — 프로덕션 쿠킹 파이프라인의 실물.
