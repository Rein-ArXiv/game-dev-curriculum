## 들어가며

graphics 시리즈 OpenGL 단계가 자주 반복하는 주장 — *"Instancing 이 개별 드로우콜 대비 N 배 빠르다"* / *"Multi-Draw Indirect 가 더 큰 씬에서 추가 이득을 만든다"*. 이 글은 *같은 1만 오브젝트 씬* 을 *세 모드* 로 그려 *주장을 측정으로 검증* 한다.

세 모드 — *Individual (개별 `glDrawElements` × 10,000)*, *Instanced (`glDrawElementsInstanced` × 1)*, *Multi-Draw Indirect (`glMultiDrawElementsIndirect` × 1, 10000 cmd)*. 시각 결과는 *비트 동일*. CPU / GPU 비용 분포만 다름.

여기까지의 코드 상태 — Step 1~2 의 골격 + 측정 인프라. 이 글에서 도달할 결과물 — *드로우 모드 콤보 박스에서 3 모드 즉시 전환* + *CPU ms / GPU ms / 드로우콜 카운터 가 그 자리에서 갱신*. 학습자가 *주장의 진위 + 정량* 을 *그래프 한 장* 에서 확인.

다섯 단계로 쌓는다.

1. Individual — Uniform N 회 + 드로우콜 N 회.
2. Instanced — per-instance attribute + `glVertexAttribDivisor`.
3. Multi-Draw Indirect — `DrawElementsIndirectCommand` 버퍼.
4. 셰이더 한 벌로 세 모드 처리.
5. 측정 결과의 일반 패턴.

---

## 목차
1. [Individual — N 호출 모델](#1-individual--n-호출-모델)
2. [Instanced — per-instance attribute](#2-instanced--per-instance-attribute)
3. [Multi-Draw Indirect](#3-multi-draw-indirect)
4. [셰이더 한 벌로 세 모드 처리](#4-셰이더-한-벌로-세-모드-처리)
5. [측정 결과의 일반 패턴](#5-측정-결과의-일반-패턴)
6. [실행과 관측](#6-실행과-관측)

---

## 1. Individual — N 호출 모델

```cpp
glBindVertexArray(vao_);
for (const auto& o : objects_) {
    shader.SetMat4("uModel", model);
    shader.SetVec3("uColor", o.color);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
}
```

오브젝트당 *Uniform 2 회 + DrawCall 1 회*. 1 만 오브젝트 = *2 만 Uniform 호출 + 1 만 DrawCall*.

CPU 비용 모델 — *Uniform 호출 ≈ 1 μs, DrawCall ≈ 5~50 μs* (드라이버 / GPU 의존). 합 *50~500ms*. 16ms 예산 *압도적 초과*.

GPU 측은 *일이 적음* — 1만 큐브 = *수십만 삼각형* — 현대 GPU 가 *수 ms* 에 처리. 따라서 *CPU 바운드* 가 된다.

### 카운터

```text
draw calls:       10000
instanced draws:  0
indirect draws:   0
uniform updates:  20006
```

이 수치가 *Individual 모드의 식별표*. 20006 = 오브젝트당 2 회 (× 10000) + 프레임 공통 6 회.

## 2. Instanced — per-instance attribute

같은 메시 + 다른 *per-instance 데이터 (model matrix, color)*. 한 번의 `glDrawElementsInstanced` 호출이 *N 인스턴스* 그림.

### per-instance attribute 셋업

```cpp
glGenBuffers(1, &vboInstanceModel_);
glBindBuffer(GL_ARRAY_BUFFER, vboInstanceModel_);
glBufferData(GL_ARRAY_BUFFER, sizeof(mat4) * N, nullptr, GL_DYNAMIC_DRAW);

// mat4 = 4 vec4 → location 2~5.
for (int i = 0; i < 4; ++i) {
    GLuint loc = 2 + i;
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, sizeof(mat4),
                          (void*)(i * sizeof(vec4)));
    glVertexAttribDivisor(loc, 1);     // ← 핵심: per-instance.
}
```

`glVertexAttribDivisor(loc, 1)` — *1 인스턴스마다 한 번 attribute 진행*. divisor=0 (기본) 은 *per-vertex*.

`mat4` 가 *attribute 4 슬롯* 차지 — OpenGL 스펙의 *vec4 단위 슬롯*. `layout(location=2) in mat4 aInstanceModel` 가 *내부적으로 2,3,4,5 사용*.

### 매 프레임 데이터 갱신

```cpp
glBindBuffer(GL_ARRAY_BUFFER, vboInstanceModel_);
glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(mat4) * N, instanceModels.data());
```

`glBufferSubData` 가 *전체 N × 64 B = 640 KB 업로드*. *PCIe 4.0 × 16 = 32 GB/s* 에서 *수십 μs 미만*. 무시 가능.

### 드로우 호출

```cpp
glDrawElementsInstanced(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT,
                         nullptr, N);
```

*CPU 호출 1 회*. GPU 가 *N 번 정점 셰이더 실행 (인스턴스마다)*. GPU 측 작업량은 Individual 과 같지만, *CPU 측 오버헤드가 거의 0*.

### 카운터

```text
draw calls:       0
instanced draws:  1
indirect draws:   0
uniform updates:  6
```

Uniform 업데이트 *20,006 → 6* — 오브젝트당 호출이 통째로 사라지고 프레임 공통 6 회만 남는다.

## 3. Multi-Draw Indirect

`MDI` 는 *N 개 드로우콜의 인자를 GPU 버퍼에 두고* 한 번에 발행. *드로우 인자가 GPU 측에서 결정* 되거나 *드로우마다 다른 메시 / 머티리얼* 인 경우.

### Command 구조체

```cpp
struct DrawElementsIndirectCommand {
    uint32_t count;          // 인덱스 수
    uint32_t instanceCount;
    uint32_t firstIndex;
    uint32_t baseVertex;
    uint32_t baseInstance;
};
```

OpenGL 4.3+ 표준. *spec 정의 그대로* 의 메모리 layout.

### 버퍼 채움 + 바인딩

```cpp
std::vector<DrawElementsIndirectCommand> cmds(N);
for (int i = 0; i < N; ++i) {
    cmds[i].count         = indexCount;
    cmds[i].instanceCount = 1;
    cmds[i].firstIndex    = 0;
    cmds[i].baseVertex    = 0;
    cmds[i].baseInstance  = i;       // per-instance attribute 의 시작.
}

glGenBuffers(1, &indirectBuffer_);
glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer_);
glBufferData(GL_DRAW_INDIRECT_BUFFER,
             sizeof(DrawElementsIndirectCommand) * N,
             cmds.data(), GL_STATIC_DRAW);
```

### 드로우 호출

```cpp
glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer_);
glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                             nullptr,
                             N,
                             sizeof(DrawElementsIndirectCommand));
```

GPU 가 *indirect buffer 를 읽어 N 개 드로우* 발행. *CPU 측 호출 1 회*.

### Instanced 와의 비교

| 항목 | Instanced | MDI |
|---|---|---|
| 메시 | 같은 메시만 | *N 종 메시 가능* (baseVertex / firstIndex) |
| 인스턴스 | 균등 | 명령마다 다른 instanceCount |
| 드로우 인자 | CPU 결정 | GPU 결정 가능 (`glMemoryBarrier` + compute) |
| CPU 비용 | 1 호출 | 1 호출 + buffer 바인딩 |

같은 메시 *균등 인스턴스* 면 *Instanced 가 약간 빠름*. *다양한 메시 / GPU 측 culling 후 그리기* 면 *MDI 의 가치*. 이 글은 *비교 위한 학습용* 이라 같은 메시 cube 만 사용.

### 카운터

```text
draw calls:       0
instanced draws:  0
indirect draws:   1
uniform updates:  6
```

MDI 는 indirect buffer 바인딩이 한 번 더 있어 *state changes 가 Instanced 보다 1 많다*.

## 4. 셰이더 한 벌로 세 모드 처리

세 모드가 *같은 셰이더 프로그램* 을 공유. *Individual 은 Uniform 으로 model / color 받음*, *Instanced / MDI 는 attribute 로 받음*. 분기 — `uUseInstancing` uniform.

```glsl
#version 450 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in mat4 aInstanceModel;
layout(location = 6) in vec4 aInstanceColor;

uniform mat4 uModel;
uniform vec3 uColor;
uniform int  uUseInstancing;

void main() {
    mat4 model = (uUseInstancing == 1) ? aInstanceModel : uModel;
    /* ... */
    vColor = (uUseInstancing == 1) ? aInstanceColor.rgb : uColor;
}
```

분기가 *uniform 기반* 이라 *모든 invocation 이 같은 경로* — Warp divergence 없음. GPU 비용 동일.

이 단계는 *드로우콜 비용* 에만 집중한다 — 셰이더는 세 모드가 공유하는 단순 분기 한 줄이고, 셰이딩 모델은 하나뿐이다.

## 5. 측정 결과의 일반 패턴

전형적인 결과 (RTX 3060 / 1만 오브젝트).

| 모드 | CPU ms | GPU ms | 드로우콜 |
|---|---|---|---|
| Individual | 25~50 | 2~4 | 10,000 |
| Instanced | 0.3~0.8 | 2~4 | 1 |
| MDI | 0.4~1.0 | 2~4 | 1 |

CPU ms 가 *Individual 30 → Instanced 0.5 = 60× 감소*. GPU ms 는 *모두 동일* — *같은 양의 정점 / 픽셀 처리*.

### 메시지

- *드로우콜 비용은 CPU 측 드라이버 비용*. GPU 자체는 *호출 수가 아닌 정점 / 픽셀 수에 비례*.
- *그래픽스 CPU 최적화 = 드로우콜 감소*. 같은 시각 결과를 더 적은 호출로.
- *Instancing 이 가장 단순한 해결책*. 같은 메시 × 다중 인스턴스가 *모든 게임 씬의 80%+*. MDI 는 *나머지 다양한 메시*.

### Bottleneck 식별

GPU ms 가 *일정하게 2~4* — *Vsync OFF 60fps 기준 16.67ms 예산의 25%*. CPU ms 가 *모드에 따라 0.5 ~ 50* 으로 *변동* — 이 씬 규모에서는 *CPU 가 그래픽스 성능의 결정 요인* 임이 명백하다.

## 6. 실행과 관측

```bash
cmake --build build --config Release
build\Release\graphics_gl_lab.exe      # Linux/macOS: ./build/graphics_gl_lab
```

패널의 *Draw mode (Step 3)* 콤보에서 세 모드를 전환하면 *GL counters* 섹션이 위 세 카운터 블록 (`draw calls 10000` / `instanced draws 1` / `indirect draws 1`) 대로 바뀌고 `uniform updates` 가 20006 ↔ 6 사이를 오간다.

```bash
ctest --test-dir build -C Release --output-on-failure
```

두 개의 테스트 타깃이 돈다. `graphics_gl_contract_tests` 는 GL 컨텍스트 없이 돌면서 `ExpectedDrawSubmissions(mode, count)` 와, GPU 에 올라가는 MDI 커맨드 배열을 만드는 `BuildIndirectCommands(objectCount, indexCount)` 를 검사한다 — 커맨드 수가 오브젝트 수와 같은지, `baseInstance` 가 인스턴스 버퍼 인덱스 `i` 인지, `count` 가 큐브 인덱스 36 인지까지 본다. 커맨드를 한 개로 줄이거나 `baseInstance` 를 상수로 고정하면 여기서 죽는다.

`graphics_gl_render_tests` 는 히든 윈도우로 만든 4.5 코어 컨텍스트에서 256×256 오프스크린 FBO 에 같은 64 오브젝트 씬을 세 모드로 그리고 `glReadPixels` 결과의 FNV-1a 해시를 비교한다. 위에서 말한 *비트 동일* 이 이 해시 세 개의 일치로 확인된다. 아무것도 그리지 않아도 셋이 같아지는 함정을 막기 위해 '클리어만 한 프레임' 의 해시와도 대조한다. 컨텍스트를 만들 수 없는 환경에서는 이 타깃이 77 로 종료해 CTest 가 skip 으로 표시한다.

```text
[capture] blank=de62d2f6be770383 individual=1fefa0cb97edbf78 instanced=1fefa0cb97edbf78 mdi=1fefa0cb97edbf78
```

---

## 결론

여기까지의 코드는 *같은 1만 오브젝트 씬* 을 *세 드로우콜 모드로 즉시 전환* 가능하게 만든다. 드라이버 호출 횟수가 *10000 → 1* 로 줄면서 *CPU 시간이 50× 감소* 가 그래프에 직접 나타난다. 인스턴싱이 없애는 비용이 *GPU 작업량이 아니라 CPU 제출량* 이라는 축 문서의 주장이 카운터 두 개로 확인된다.

`glVertexAttribDivisor` 의 per-instance attribute 패턴, MDI 의 *GPU 측 인자 버퍼*, 두 모드가 *같은 셰이더 한 벌* 로 동작하는 *uniform 분기 패턴* — 셋 모두 *프로덕션 엔진의 기본 토대*. *Unreal / Unity / Frostbite* 의 *static mesh batch* 가 모두 이 위에 올라간다.

이 단계의 종료 상태에서 세 모드의 *시각 결과가 같은지* 는 눈으로 비교하는 것 말고 확인 수단이 없다 — 오프스크린 렌더 후 픽셀을 되읽어 비교하는 회귀는 이 lab 에 없다.

## 더 읽을거리

- [LearnOpenGL: Instancing](https://learnopengl.com/Advanced-OpenGL/Instancing) — glVertexAttribDivisor 패턴 실습.
- [OpenGL Wiki: glMultiDrawElementsIndirect](https://www.khronos.org/opengl/wiki/GLAPI/glMultiDrawElementsIndirect) — MDI 인자 버퍼 명세.
- Cass Everitt 외, "Approaching Zero Driver Overhead" (GDC 2014) — 드로우콜 오버헤드 제거 기법의 종합(AZDO).
