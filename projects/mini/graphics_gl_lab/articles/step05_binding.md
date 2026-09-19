## 들어가며

광원 64 개를 매 프레임 셰이더에 전달하는 *세 방식* — *per-call Uniform*, *UBO*, *SSBO*. 같은 데이터 흐름 → 다른 CPU/GPU 비용 분포. *프레임 상수 / 광원 / 머티리얼 데이터* 의 *전달 비용 분석* 이 그래픽스 측정의 핵심 토픽.

여기까지의 코드 상태 — Step 1~4 의 골격 / 측정 / 드로우콜 / 셰이더 perm 토글. 이 글에서 도달할 결과물 — *광원 토글 활성화 시 셰이더가 64 개 광원 합산* + *세 binding 모드 즉시 전환* + *CPU/GPU ms 비교*. 학습자가 *"UBO 가 per-call uniform 대비 N 배 빠르다"* 의 실제 수치를 측정.

다섯 단계.

1. 세 binding 모드의 의미.
2. per-call Uniform — 광원당 호출 N × 2.
3. UBO (std140) — 한 버퍼 갱신.
4. SSBO (std430) — UBO + 가변 길이.
5. 측정 결과 + 게임 엔진 일반 패턴.

---

## 목차
1. [세 binding 모드](#1-세-binding-모드)
2. [per-call Uniform](#2-per-call-uniform)
3. [UBO (std140)](#3-ubo-std140)
4. [SSBO (std430)](#4-ssbo-std430)
5. [측정 결과](#5-측정-결과)
6. [실행과 관측](#6-실행과-관측)

---

## 1. 세 binding 모드

| 모드 | 셰이더 측 | CPU 호출 / 프레임 | GPU 측 접근 |
|---|---|---|---|
| per-call Uniform | `uniform vec3 uLightPos[64]` | N × 2 = 128 | uniform memory |
| UBO | `layout(std140) uniform LightBlock { ... }` | 1 (buffer subdata) | UBO cached read |
| SSBO | `layout(std430) readonly buffer LightStorage { ... }` | 1 | global memory read |

핵심.

- *per-call Uniform* — 가장 단순하지만 *광원 N × 2 회 함수 호출* → CPU 비용 큼.
- *UBO* — *한 번의 buffer subdata + 셰이더 measuring*. GPU 측 *작은 cached read* 라 빠름. *크기 제한 (대부분 64KB)*.
- *SSBO* — UBO 와 비슷한 CPU 비용. *가변 길이 + 더 큼 (1GB 이상)*. GPU 측 *cache 가 UBO 보다 약함*.

### Light 구조체

<!-- snippet: src/render/lighting_bindings.h#Light -->
```cpp
struct Light {
    glm::vec4 posRadius;       // xyz = position, w = radius.
    glm::vec4 colorIntensity;  // rgb = color, w = intensity.
};
```

`vec4 × 2 = 32 바이트` 로 패킹. *std140 의 vec3 16바이트 정렬* 함정을 회피.

## 2. per-call Uniform

uniform 이름을 만드는 부분은 *GL 호출이 없는 순수 함수* 로 떼어 둔다. 컨텍스트 없이 테스트할 수 있고, 아래 `Apply` 가 이 목록을 그대로 순회하므로 *비용 모델과 실행 경로가 한 곳에서 나온다*.

<!-- snippet: src/render/lighting_bindings.cpp#BuildPerCallUniformNames -->
```cpp
names.reserve((size_t)activeCount * 2);
for (int i = 0; i < activeCount; ++i) {
    names.push_back("uLightPos[" + std::to_string(i) + "]");
    names.push_back("uLightColor[" + std::to_string(i) + "]");
}
```

<!-- snippet: src/render/lighting_bindings.cpp#LightingBindings::Apply -->
```cpp
const std::vector<std::string> names = BuildPerCallUniformNames(activeCount_);
for (size_t k = 0; k + 1 < names.size(); k += 2) {
    const size_t i = k / 2;
    if (shader.SetVec3(names[k].c_str(),
                       glm::vec3(lights_[i].posRadius)))
        counters.uniformUpdates++;
    if (shader.SetVec3(names[k + 1].c_str(), LightRadiance(lights_[i])))
        counters.uniformUpdates++;
}
```

64 광원 = *128 호출 + 128 문자열 빌드 + 128 glUniform3fv*. Uniform location cache 활용해도 *호출 자체 비용* 무시 못 함.

<!-- snippet: shaders/lit_per_call.frag -->
```glsl
const int MAX_LIGHTS = 64;
// ...
uniform vec3 uLightPos[MAX_LIGHTS];
uniform vec3 uLightColor[MAX_LIGHTS];
```

GPU 측 — uniform 은 *드라이버가 const memory / register 에 배치* — 작은 데이터에 최적. 64 광원 정도면 효율적.

## 3. UBO (std140)

### 생성 + 바인딩

<!-- snippet: src/render/lighting_bindings.cpp#LightingBindings::Init -->
```cpp
glGenBuffers(1, &ubo_);
glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
glBufferData(GL_UNIFORM_BUFFER,
             (GLsizeiptr)(sizeof(Light) * (size_t)maxLights_),
             nullptr, GL_DYNAMIC_DRAW);
glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_);
```

`binding = 0` 슬롯에 묶임. 셰이더 측의 `layout(std140, binding = 0)` 와 매칭.

### 매 프레임 갱신

<!-- snippet: src/render/lighting_bindings.cpp#LightingBindings::UploadUBO -->
```cpp
glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
glBufferSubData(GL_UNIFORM_BUFFER, 0,
                (GLsizeiptr)(sizeof(Light) * (size_t)activeCount_),
                lights_.data());
```

*한 호출* 로 *N 광원 × 32 B 갱신*. 64 광원 = 2 KB. 네트워크 비용 무시 가능.

### 셰이더 측

<!-- snippet: shaders/lit_ubo.frag -->
```glsl
const int MAX_LIGHTS = 64;
struct Light {
    vec4 posRadius;
    vec4 colorIntensity;
};
layout(std140, binding = 0) uniform LightBlock {
    Light lights[MAX_LIGHTS];
};
```

`std140` layout 의 함정 — *vec3 가 16바이트 정렬*. C++ 측 `glm::vec3` 가 *12 바이트* 라 layout 어긋남. *vec4 두 개* 로 패킹해 회피.

<!-- snippet: shaders/lit_ubo.frag#main -->
```glsl
vec3 L = lights[i].posRadius.xyz - vWorldPos;
// ...
vec3 col = lights[i].colorIntensity.rgb * lights[i].colorIntensity.w;
```

`.xyz` / `.rgb` 로 *vec3 부분만* 사용.

## 4. SSBO (std430)

UBO 와 거의 같은 패턴 + *크기 제한 거의 없음 + 가변 길이*.

### 생성

<!-- snippet: src/render/lighting_bindings.cpp#LightingBindings::Init -->
```cpp
glGenBuffers(1, &ssbo_);
glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_);
glBufferData(GL_SHADER_STORAGE_BUFFER,
             (GLsizeiptr)(sizeof(Light) * (size_t)maxLights_),
             nullptr, GL_DYNAMIC_DRAW);
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo_);
```

OpenGL 4.3+. 4.5 Core 에 포함.

### 셰이더 측 — 가변 길이

<!-- snippet: shaders/lit_ssbo.frag -->
```glsl
struct Light {
    vec4 posRadius;
    vec4 colorIntensity;
};
layout(std430, binding = 0) readonly buffer LightStorage {
    Light lights[];     // 길이 명시 없음 — runtime 결정.
};
```

UBO 는 *컴파일 시점 길이* 필요. SSBO 는 *runtime 가변 길이*. *대규모 인스턴스 데이터 (10000 매트릭스 등)* 에 적합.

`readonly` 한정자 — 셰이더 가 *쓰기 안 함* — 드라이버 최적화 힌트.

### std430 의 vec3 처리

std430 도 *vec3 base alignment 16바이트* 는 동일. `vec3 + float` 같은 조합은 두 레이아웃 모두 *float 가 offset 12* 로 남는 4 바이트를 채운다. 다른 것은 *배열 stride 와 구조체 정렬* 뿐 — std140 은 vec4 배수로 올림하고 std430 은 요소 정렬을 그대로 쓴다. 안전성을 위해 *vec4 패킹* 이 권장.

## 5. 측정 결과

전형적 결과 (RTX 3060, 1만 오브젝트, Instanced 모드, 64 광원).

| Binding | CPU ms | GPU ms |
|---|---|---|
| per-call Uniform | 1.2 | 5.8 |
| UBO | 0.4 | 5.5 |
| SSBO | 0.4 | 5.7 |

CPU 비용.

- per-call — *문자열 빌드 + uniform 호출* 비용으로 *3 배*. *광원 수에 정비례*.
- UBO/SSBO — *1 회 호출*. 광원 수와 무관.

GPU 비용.

- *세 모드 거의 동일* — 같은 데이터를 같은 셰이더가 합산. UBO 가 *살짝 빠름* (cache 우대).
- *광원 수 ↑ → GPU 시간 정비례* (모든 픽셀이 광원당 합산 — fragment 비용 증가).

### 광원 수 변화

| 광원 수 | per-call CPU | UBO CPU | UBO GPU |
|---|---|---|---|
| 8 | 0.2 | 0.4 | 1.5 |
| 32 | 0.6 | 0.4 | 3.0 |
| 64 | 1.2 | 0.4 | 5.5 |

per-call 의 CPU 가 *광원에 정비례* 늘어남이 분명. UBO 는 *일정* (한 회 호출). GPU 는 *광원 수에 정비례* — fragment shader 의 loop.

### 게임 엔진 패턴

| 데이터 종류 | 일반 binding |
|---|---|
| 카메라 / 뷰 / 프레임 상수 | UBO (작고 자주 갱신) |
| 광원 배열 (수 ~ 수십) | UBO |
| Per-material 텍스처 / 상수 | per-draw root constant 또는 UBO |
| 인스턴스 데이터 (수만 +) | SSBO (가변 / 큼) |
| 컴퓨트 쓰기 결과 | SSBO (write 가능) |

규칙 — *작고 자주 사용* → UBO. *크고 가변 / 쓰기* → SSBO. *그리고 per-call Uniform 은 *극히 작은 데이터 (1~2 값)* 에만*. 광원 배열 같은 *반복 데이터* 에 per-call 은 *안티패턴*.

## 6. 실행과 관측

```bash
cmake --build build --config Release
build\Release\graphics_gl_lab.exe      # Linux/macOS: ./build/graphics_gl_lab
```

패널의 *Lighting binding (Step 5)* 에서 `use multi-light shader` 를 켜고 `binding` 을 바꾼다. 광원 셰이더 (`lit_*.frag`) 에는 `uLightDir` / `uCameraPos` / `uShadingModel` 이 없으므로 프레임 공통 uniform 은 `uView` / `uProj` / `uUseInstancing` 3 회만 계수된다. Instanced 모드 · 광원 32 개 기준 관측값.

| binding | uniform updates | 비고 |
|---|---|---|
| per-call Uniform | 68 = 3 + 1 (`uLightCount`) + 32 × 2 | 광원 수에 정비례 |
| UBO | 4 = 3 + 1 | `glBufferSubData` 1 회는 `buffer updates` 로 계수 |
| SSBO | 4 = 3 + 1 | 위와 같음 |

`glBufferSubData` 를 `state changes` 가 아니라 별도의 `buffer updates` 로 세는 이유는 두 가지가 성질이 다르기 때문이다. `state changes` 는 바인딩 교체이고 `buffer updates` 는 실제 데이터 전송이다. 한 칸에 섞으면 per-call 과 UBO 의 차이가 "uniform 호출 64 회 vs 바인딩 1 회" 처럼 보여 *무엇이 대체되었는지* 가 흐려진다.

`light count` 슬라이더를 움직이면 per-call 에서만 `uniform updates` 가 따라 움직인다.

### 이 step 의 검증

이 절의 수치를 잠그는 항목 두 개가 이 step 소속이다.

```powershell
ctest --test-dir build -C Release -R gl_step05 --output-on-failure
```

```text
    Start  6: gl_step05_binding_contract
1/2 Test  #6: gl_step05_binding_contract .......   Passed    0.03 sec
    Start 10: gl_step05_binding_render
2/2 Test #10: gl_step05_binding_render .........   Passed    0.25 sec

100% tests passed out of 2
```

계약 쪽은 GL 컨텍스트 없이 돌면서 `CountBindingCost(mode, n)` 의 결과가 `ExpectedBindingCost(mode, n)` 와 같은지, 그리고 per-call 이 `1 + 2N`, UBO/SSBO 가 `{uniform 1, buffer 1}` 인지 절대값으로 검사한다.

```powershell
build\Release\graphics_gl_contract_tests.exe step05   # Linux/macOS: ./build/graphics_gl_contract_tests step05
```

```text
[step05] binding_cost_model ... ok
[step05] counted_binding_cost ... ok
[step05] per_call_uniform_names ... ok
graphics_gl_contract_tests: 3/3 passed (filter=step05)
```

<!-- snippet: tests/graphics_gl_contract_tests.cpp#TestCountedBindingCostMatchesModel -->
```cpp
// 절대 기준 — per-call 은 광원당 2 회 + uLightCount 1 회.
CHECK(gllab::CountBindingCost(gllab::BindingMode::PerCallUniform, 32)
          .uniformUpdates == 65);
CHECK(gllab::CountBindingCost(gllab::BindingMode::PerCallUniform, 64)
          .uniformUpdates == 129);
CHECK(gllab::CountBindingCost(gllab::BindingMode::UBO, 64).uniformUpdates == 1);
CHECK(gllab::CountBindingCost(gllab::BindingMode::UBO, 64).bufferUpdates == 1);
// 광원 수가 늘어도 UBO 경로는 상수 — 이 lab 의 핵심 주장.
CHECK(gllab::CountBindingCost(gllab::BindingMode::UBO, 8).uniformUpdates ==
      gllab::CountBindingCost(gllab::BindingMode::UBO, 64).uniformUpdates);
```

`CountBindingCost` 는 `Apply` 의 per-call 루프가 순회하는 uniform 이름 목록 (`BuildPerCallUniformNames`) 을 그대로 세므로, 그 목록을 만드는 루프를 무력화하면 예측값이 함께 무너져 여기서 죽는다. `Apply` 쪽 루프만 지우면 예측값은 남고 아래 렌더 테스트가 갈린다.

렌더 쪽은 실제 컨텍스트에서 `Apply` 를 호출해 카운터 증가분이 그 예측과 정확히 일치하는지 확인한다 — 셰이더에 없는 uniform 은 `glUniform` 호출 자체가 없으므로, 계수 규칙이 실제 GL 호출 수와 어긋나면 여기서 갈라진다.

```powershell
build\Release\graphics_gl_render_tests.exe step05   # Linux/macOS: ./build/graphics_gl_render_tests step05
```

```text
[gl] vendor=Intel renderer=Intel(R) Iris(R) Xe Graphics version=4.5.0 - Build 32.0.101.7088
[step05] binding_counters ... ok
graphics_gl_render_tests: 1/1 passed (filter=step05)
```

<!-- snippet: tests/graphics_gl_render_tests.cpp#TestBindingCountersMatchModel -->
```cpp
lighting.Apply(mode, prog, counters);

const auto model = gllab::CountBindingCost(mode, n);
// ...
CHECK(counters.uniformUpdates == model.uniformUpdates);
CHECK(counters.bufferUpdates == model.bufferUpdates);
```

세 모드 × 광원 8 / 32 / 64 의 아홉 조합을 모두 돈다. 어긋나면 `[binding] mode=… runtime(u=…,b=…) model(u=…,b=…)` 한 줄이 함께 출력되어 어느 조합이 갈렸는지 남는다.

---

## 결론

여기까지의 코드는 *광원 배열의 binding 비용* 을 *세 모드 즉시 전환 + 측정* 가능하게 만든다. per-call Uniform 의 *CPU 측 호출 비용*, UBO 의 *작은 데이터 우대*, SSBO 의 *가변 길이 + 큰 데이터* — 세 모드의 *위치* 가 *측정 그래프로 명확*.

`std140` / `std430` 의 *vec3 정렬 함정*, *binding slot 명시*, *gen → bind → bufferData → bufferSubData* 의 표준 lifecycle 패턴 — 그래픽스 *데이터 흐름 코드의 표준*.

## 더 읽을거리

- [OpenGL Wiki: Interface Block (GLSL)](https://www.khronos.org/opengl/wiki/Interface_Block_(GLSL)) — std140/std430 정렬 규칙의 명세.
- [OpenGL Wiki: Shader Storage Buffer Object](https://www.khronos.org/opengl/wiki/Shader_Storage_Buffer_Object) — SSBO 의 가변 길이·일관성 규칙.
- Cass Everitt 외, "Approaching Zero Driver Overhead" (GDC 2014) — persistent mapping 등 바인딩 비용 회피의 다음 단계.
