## 들어가며

렌더링 경로의 확장성은 오브젝트 수만 올려서는 설명할 수 없다. 같은 10만 개 오브젝트라도 CPU가 10만 번 draw command를 기록하는 경로와, 8MB의 인스턴스 데이터를 올리고 draw 한 번을 기록하는 경로는 병목이 다르다. 이 단계는 1k·10k·100k 오브젝트를 Per-Draw와 Instanced 모드로 실행해 CPU 제출 비용, GPU 작업량, 업로드 대역폭을 분리한다.

Mesh Shader는 이 측정 결과를 해석할 때 등장하는 선택적 수단이다. 단순히 최신 API 기능이라는 이유로 채택하지 않고, 전통 파이프라인의 어느 비용을 줄일 수 있는지와 현재 장면에 제거할 geometry가 있는지를 기준으로 판단한다.

---

## 목차

1. [비교가 성립하는 조건](#1-비교가-성립하는-조건)
2. [Per-Draw와 Instanced 경로](#2-per-draw와-instanced-경로)
3. [인스턴스 업로드 비용](#3-인스턴스-업로드-비용)
4. [스케일 실험과 병목 판정](#4-스케일-실험과-병목-판정)
5. [Mesh Shader 판단 기준](#5-mesh-shader-판단-기준)
6. [검증 기준과 한계](#6-검증-기준과-한계)
7. [빌드와 실행 결과](#7-빌드와-실행-결과)

---

## 1. 비교가 성립하는 조건

두 모드는 같은 mesh, 같은 shader 계산, 같은 camera, 같은 오브젝트 변환을 사용해야 한다. 시각 결과가 다르면 시간 차이를 제출 방식의 차이라고 해석할 수 없다.

고정해야 하는 항목은 다음과 같다.

- 동일한 swapchain extent와 VSync 설정
- 동일한 vertex/index buffer와 index count
- 동일한 view-projection, 조명, fragment shader
- 동일한 오브젝트 수와 배치
- 동일한 프레임 구간의 CPU·GPU 측정

오브젝트 수를 바꿀 때 `vkDeviceWaitIdle`로 기존 프레임의 사용을 끝낸 뒤 CPU 객체 배열을 재구축한다. 이 대기 시간은 설정 변경 비용이며 정상 상태의 프레임 비용이 아니다. 토글 직후의 spike를 버리고 충분히 안정된 구간을 비교해야 한다.

<!-- snippet: src/app/app.cpp#App::OnFrame -->
```cpp
if (pendingCount_ != objectCount_) {
    vkDeviceWaitIdle(dev);
    objectCount_ = pendingCount_;
    scene_.Build(objectCount_);
}
```

1k·10k·100k는 절대 성능 등급이 아니라 비용 증가 형태를 보기 위한 세 점이다. $N$이 10배가 되었을 때 CPU ms, GPU ms, draw 수, push constant 수가 각각 어떻게 변하는지가 중요하다.

## 2. Per-Draw와 Instanced 경로

기록 규칙은 `Scene::RecordDraw` 하나에 모여 있다. 이 함수는 `VkCommandBuffer` 대신 `IDrawRecorder` 를 받으므로 디바이스 없이 돌릴 수 있고, Vulkan 구현이 그대로 `vkCmdPushConstants` / `vkCmdDrawIndexed` 로 내려보낸다(§6).

Per-Draw 모드는 오브젝트마다 model matrix와 color를 push constant로 기록하고 draw를 한 번씩 낸다.

<!-- snippet: src/scene/scene_core.cpp#Scene::RecordDraw -->
```cpp
for (size_t i = 0; i < objects_.size(); ++i) {
    const auto& o = objects_[i];
    PushConstants pc{};
    pc.model = ModelMatrix(o);
    pc.color = glm::vec4(o.color, 1.0f);
    pc.mode  = 0u;
    recorder.PushConstants(pc);
    counters.pushConstants++;

    recorder.DrawIndexed(indexCount_, 1);
    counters.drawCalls++;
}
```

오브젝트 수가 $N$이면 draw command와 push constant 갱신도 각각 $N$번이다.

$$
C_{\text{submit, per-draw}}(N)
\approx N(C_{\text{push}}+C_{\text{draw}})
$$

Instanced 모드는 한 번의 push constant로 모드만 알리고 instance count가 $N$인 draw를 한 번 기록한다.

<!-- snippet: src/scene/scene_core.cpp#Scene::RecordDraw -->
```cpp
PushConstants pc{};
pc.model = glm::mat4(1.0f);
pc.color = glm::vec4(1.0f);
pc.mode  = 1u;
recorder.PushConstants(pc);
counters.pushConstants++;

recorder.DrawIndexed(indexCount_, (uint32_t)objects_.size());
counters.instancedDraws++;
```

이때 CPU command 제출 횟수는 $O(1)$이 되지만 GPU가 처리하는 인스턴스와 정점 수는 줄지 않는다. 큐브 한 개의 index 수를 $I$라 하면 두 모드 모두 대략 $N I$개의 인덱스를 소비한다.

| 모드 | draw 수 | push 수 | 인스턴스 수 | 정점·픽셀 작업 |
|---|---:|---:|---:|---:|
| Per-Draw | $N$ | $N$ | $N$ | 거의 동일 |
| Instanced | 1 | 1 | $N$ | 거의 동일 |

따라서 Instanced 모드에서 CPU ms만 크게 줄고 GPU ms가 비슷한 결과는 정상이다. GPU ms까지 크게 줄었다면 command 처리 외에 cache, state 전달, 측정 구간 차이가 개입했는지 확인해야 한다.

## 3. 인스턴스 업로드 비용

Instanced 모드는 model matrix와 color를 SSBO에 연속으로 쓴다. vertex shader는 `gl_InstanceIndex`로 자기 데이터를 읽는다.

<!-- snippet: shaders/cube.vert -->
```glsl
struct Instance {
    mat4 model;
    vec4 color;
};
layout(set = 0, binding = 1) readonly buffer Instances {
    Instance instances[];
};
// ...
if (pc.mode == 1u) {
    model = instances[gl_InstanceIndex].model;
    col   = instances[gl_InstanceIndex].color.rgb;
} else {
    model = pc.model;
    col   = pc.color.rgb;
}
```

한 인스턴스는 `mat4` 64바이트와 `vec4` 16바이트로 80바이트다. 매 프레임 모든 변환을 다시 쓰므로 업로드 양은 다음과 같다.

$$
B_{\text{frame}}=80N
$$

100k에서는 프레임당 약 8MB이고, 60fps라면 payload만 약 480MB/s다. 메모리 할당 메타데이터, cache writeback, flush 범위는 이 계산에 포함되지 않는다.

<!-- snippet: src/app/app.cpp#App::UpdateFrameData -->
```cpp
if (drawMode_ == DrawMode::Instanced) {
    int n = scene_.WriteInstances(fr.ssboMapped, kMaxObjects);
    vmaFlushAllocation(alloc_.Allocator(), fr.ssboAlloc, 0,
                       (VkDeviceSize)n * sizeof(InstanceData));
}
```

이 설계는 CPU 제출 병목을 보여 주기에는 적합하지만 정적 인스턴스까지 매 프레임 업로드한다. 오브젝트 대부분이 정적이라면 dirty range만 갱신하거나 device-local 버퍼에 유지하는 구조와 결과가 달라진다.

## 4. 스케일 실험과 병목 판정

각 모드와 오브젝트 수 조합을 충분히 워밍업한 뒤 같은 길이의 측정 창을 기록한다.

| 오브젝트 | Per-Draw | Instanced |
|---:|---:|---:|
| 1k | CPU ms, GPU ms, draws | CPU ms, GPU ms, upload bytes |
| 10k | CPU ms, GPU ms, draws | CPU ms, GPU ms, upload bytes |
| 100k | CPU ms, GPU ms, draws | CPU ms, GPU ms, upload bytes |

결과는 다음 순서로 해석한다.

1. **Per-Draw CPU ms만 선형 증가**: command 기록과 제출이 지배적이다.
2. **두 모드 GPU ms가 함께 증가**: vertex, rasterization, fragment 작업이 지배적이다.
3. **Instanced CPU ms도 크게 증가**: transform 계산과 SSBO 쓰기·flush가 지배적일 수 있다.
4. **오브젝트 수보다 해상도에 민감**: pixel fill 또는 fragment shader가 지배적이다.
5. **VSync에서 CPU/GPU 수치가 평평함**: present pacing이 처리량 차이를 가리고 있을 수 있다.

CPU 프레임 시간과 GPU timestamp는 같은 의미가 아니다. CPU timer는 command 기록, UI, 동기화 대기를 포함할 수 있고 GPU timestamp는 command buffer 안의 지정 구간만 측정한다. 둘 중 큰 값 하나만 보고 병목을 단정하지 않는다.

## 5. Mesh Shader 판단 기준

Mesh Shader는 Input Assembly와 전통적인 vertex shader 입력 모델 일부를 task/mesh workgroup 모델로 바꾼다.

```text
Task Shader(optional) -> Mesh Shader -> Rasterizer -> Fragment Shader
```

가치가 생기는 조건은 다음과 같다.

- meshlet 단위 frustum 또는 occlusion culling으로 많은 geometry를 버릴 수 있음
- LOD 선택을 GPU에서 수행함
- CPU가 개별 draw를 만들지 않는 GPU-driven dispatch 구조가 있음
- 장면의 GPU 병목이 vertex·primitive 처리에 있음

현재 큐브 장면은 모든 오브젝트를 그리고, meshlet culling과 LOD가 없다. 이 조건에서는 Mesh Shader로 entry point만 바꾸어도 처리해야 할 geometry가 줄지 않는다. Per-Draw CPU 병목은 Instanced 경로만으로 이미 크게 줄일 수 있다.

따라서 “100k가 느리다”는 사실만으로 Mesh Shader가 해답이 되지 않는다. GPU timestamp와 파이프라인 통계에서 vertex·primitive 작업이 지배적이고, 실제로 제거 가능한 geometry가 충분하다는 근거가 함께 필요하다.

## 6. 검증 기준과 한계

자동 테스트(`tests/graphics_vk_contract_tests.cpp`)가 고정하는 계약은 넷이다.

1. `ExpectedDrawSubmissions(PerDraw, N) == N`
2. `ExpectedDrawSubmissions(Instanced, N) == 1`
3. object count가 1k·10k·100k에서 정확히 반영됨 — `Scene::Build(N)` 후 `ObjectCount() == N`.
4. Instanced 모드의 SSBO 쓰기가 capacity를 넘지 않음 — 오브젝트 100개짜리 장면에 `WriteInstances(dst, 10)` 을 호출하면 반환값이 10 이고 11번째 슬롯의 바이트가 그대로 남아 있다. 호출부는 `kMaxObjects`(100000)를 capacity 로 넘긴다.

1·2 는 공식 자체를 확인하는 데서 그치지 않는다. `Scene::Draw` 의 기록 규칙은 `IDrawRecorder` 를 받는 `Scene::RecordDraw` 로 분리되어 있고, 이 함수는 `VkBuffer`/VMA 를 만지지 않아 별도 번역 단위(`src/scene/scene_core.cpp`)로 테스트 바이너리에 링크된다. 테스트는 fake recorder 로 두 모드를 돌려 `drawCalls + instancedDraws == ExpectedDrawSubmissions(mode, N)` 과 `pushConstants == (PerDraw ? N : 1)` 을 대조한다. Instanced 분기를 N회 루프 draw 로 바꾸면 이 대조가 깨진다.

다음 둘은 여전히 실행 화면에서 사람이 관측하는 항목이고 자동 검증 대상이 아니다.

5. 두 모드가 같은 camera에서 같은 배치와 색을 그림 — 모드를 번갈아 전환해 눈으로 확인한다.
6. validation error가 발생하지 않음 — Debug 빌드의 `Validation log` 로 확인한다.

남은 한계도 분명하다. `Scene::Draw` 중 `vkCmdBindVertexBuffers` / `vkCmdBindIndexBuffer` 와 recorder 의 Vulkan 구현(`vkCmdPushConstants`, `vkCmdDrawIndexed`)은 디바이스가 필요해 자동 검증 밖이다. 검사되는 것은 “무엇을 몇 번 제출하기로 결정했는가”이지 “그 명령이 실제로 GPU 에 도달했는가”가 아니다.

이 실험은 큐브 하나를 반복하는 합성 워크로드다. 실제 게임의 material 전환, 서로 다른 mesh, 투명도, depth prepass, culling, LOD는 포함하지 않는다. 또한 매 프레임 전체 transform을 CPU에서 계산하므로 GPU-driven 렌더러의 상한을 측정하지 않는다.

## 7. 빌드와 실행 결과

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release -R vk_step07_mesh_scale --output-on-failure
.\build\Release\graphics_vk_contract_tests.exe step07
build\Release\graphics_vk_lab.exe
```

```text
    Start 8: vk_step07_mesh_scale
1/1 Test #8: vk_step07_mesh_scale .............   Passed    0.03 sec

100% tests passed out of 1

[step07] draw_mode_names ... ok (5 checks)
[step07] draw_submission_counts ... ok (55 checks)
[step07] scene_scale_contract ... ok (13 checks)
step07: 3 test(s), 73 checks, 0 failed
```

`draw_submission_counts` 의 55개가 §6 의 1·2 다. 1·1000·10000 세 규모 × 두 모드마다 fake recorder 가 센 수를 공식과 대조하므로 개수가 그렇게 늘어난다.

<!-- snippet: tests/graphics_vk_contract_tests.cpp@draw-submission-match -->
```cpp
const int expected = vklab::ExpectedDrawSubmissions(mode, n);
CHECK(counters.drawCalls + counters.instancedDraws == expected);
CHECK(rec.draws == expected);
CHECK(counters.pushConstants ==
      (mode == vklab::DrawMode::PerDraw ? n : 1));
CHECK(rec.pushes == counters.pushConstants);
// Instanced 는 1회 draw 에 N 인스턴스, PerDraw 는 N회 draw 에 1씩.
CHECK(rec.totalInstances == (uint64_t)n);
CHECK(rec.maxInstances ==
      (mode == vklab::DrawMode::PerDraw ? 1u : (uint32_t)n));
```

`counters` 는 `RecordDraw` 가 올린 값이고 `rec` 는 recorder 가 받은 실제 호출 수다. 둘을 함께 보므로 카운터만 맞고 기록은 다른 어긋남이 남지 않는다. `scene_scale_contract` 의 13개가 §6 의 3·4이고, `draw_mode_names` 는 패널 콤보에 뜨는 두 모드 이름이다.

관측 항목.

- 패널에서 `Object count` 를 10k 로 두고 `Draw mode` 를 `Per-Draw` 로 하면 카운터가 `draws 10000  instanced 0`, `push 10000` 이 된다. `Instanced` 로 바꾸면 같은 화면에서 `draws 0  instanced 1`, `push 1` 로 떨어진다. 이 한 줄이 이 step 의 조작 변수와 결과다.
- `Active objects` 는 선택한 1k / 10k / 100k 값과 같아야 한다. 값 변경은 다음 프레임의 scene rebuild 후 반영된다.
- 두 모드의 `CPU ms` / `GPU ms` 를 오브젝트 수마다 적어 두면 §4 의 다섯 가지 병목 패턴 중 어디에 해당하는지 판정할 수 있다. VSync 를 켠 채 비교하면 present pacing 이 차이를 가리므로 끄고 본다.

---

## 결론

1k·10k·100k 오브젝트를 Per-Draw와 Instanced 경로로 비교해 CPU command 제출, GPU geometry 처리, 인스턴스 업로드를 서로 다른 비용으로 관찰할 수 있게 했다. Instancing은 draw 수를 $N$에서 1로 줄이지만 정점·픽셀 작업과 프레임당 SSBO 업로드까지 없애지는 않는다.

Mesh Shader는 Vulkan 학습의 완료 표시가 아니라 특정 병목에 대한 선택적 해법이다. 현재 측정에서 vertex·primitive 작업이 지배적이고 meshlet culling으로 버릴 geometry가 충분할 때만 채택 근거가 생긴다.

## 더 읽을거리

- Khronos, [Mesh Shading for Vulkan](https://www.khronos.org/blog/mesh-shading-for-vulkan) — `VK_EXT_mesh_shader`의 실행 모델
- Khronos, [Vulkan Guide: Push Constants](https://docs.vulkan.org/guide/latest/push_constants.html) — push constant 범위와 pipeline layout 계약
- Vulkan Specification, [Drawing Commands](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#drawing) — indexed·instanced draw의 명세
- NVIDIA, [Introduction to Turing Mesh Shaders](https://developer.nvidia.com/blog/introduction-turing-mesh-shaders/) — meshlet과 GPU culling 모델
- [meshoptimizer](https://github.com/zeux/meshoptimizer) — meshlet 구축과 geometry 최적화 도구
