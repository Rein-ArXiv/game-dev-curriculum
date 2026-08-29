# graphics_vk_lab - Vulkan 명시적 동기화 측정기

## 컨셉

이 lab은 `graphics_gl_lab`/`graphics_dx_lab`과 같은 큐브 격자 장면을 Vulkan 1.3으로 그리며, 명시적 동기화와 리소스 비용을 측정한다. Vulkan의 난점은 “그리는 코드”보다 swapchain image의 acquire/present 수명, layout transition, stage/access mask, queue submit, semaphore 값이 서로 정확히 맞아야 한다는 점이다.

렌더 루프는 `vkAcquireNextImageKHR → command record → vkQueueSubmit2 → vkQueuePresentKHR` 전체를 구현하고, dynamic rendering(`vkCmdBeginRendering`)과 ImGui Vulkan backend를 연결한다. 화면에는 회전하는 큐브 격자와 측정 패널이 함께 뜬다.

범위는 의도적으로 둘로 나뉜다.

- **실제 동작·측정**: instance/device/swapchain/pipeline, timestamp query + validation log, `synchronization2` barrier policy(over-sync vs minimal), timeline semaphore 기반 프레임 동기화, Per-Draw vs Instanced draw.
- **설계 토픽**: multi-queue overlap, descriptor indexing(bindless), mesh shader는 “언제 필요한가” 관점으로 문서화한다. 단순 큐브 장면에 넣으면 학습 목표가 흐려지므로 실행 토글로 만들지 않는다.
- **범위 밖**: compute 는 이 lab 에 없다. `.comp` 셰이더도, `vkCmdDispatch` 호출도 없고, dedicated compute/transfer queue family 는 선택만 하고 워크로드에서 쓰지 않는다. 그래서 이 lab 의 barrier 실험 대상은 swapchain color 전이 두 개뿐이고, compute→graphics 같은 producer/consumer 는 관측 대상이 아니다.

## 토글 항목

| 항목 | 상태 | 효과 |
|---|---|---|
| Draw mode | 동작 | Per-Draw(push constant × N) vs Instanced(SSBO + 1 draw) |
| Shading | 동작 | Lambert vs Blinn-Phong 파이프라인 |
| Barrier policy | 동작 | 같은 layout 전이의 broad vs minimal stage/access mask |
| Frame completion | 동작 | frame/image 재사용을 fence 또는 timeline 값으로 추적 |
| VSync | 동작 | FIFO vs Mailbox/Immediate/FIFO 가용 순 fallback |
| Object count | 동작 | 1k / 10k / 100k scene rebuild |
| Inject: skip barriers | 동작(실패 주입) | 프레임당 color layout 전이 2개를 기록하지 않는다 |
| Inject: wrong srcAccessMask | 동작(실패 주입) | 배리어의 `srcAccessMask` 만 0 으로 덮어쓴다 |
| Multi-queue | 설계 문서 | queue family / ownership 판단 |
| Descriptor indexing | 설계 문서 | bindless의 Vulkan식 조건 |
| Mesh shader | 설계 문서 | 선택적 extension으로 분리 |

## 데이터 바인딩

갱신 빈도에 따라 세 곳으로 나눈다.

- **set 0, binding 0 (UBO)** — `viewProj`, lightDir, cameraPos. 프레임당 1회 갱신.
- **set 0, binding 1 (SSBO)** — per-instance `model` + `color`. Instanced 모드에서 `gl_InstanceIndex`로 읽는다.
- **push constant (84 bytes)** — Per-Draw 모드의 `model` + `color` + `mode`. 사양 보장 한도 128 bytes 내.

## 출력

- 메인 뷰포트 — 회전하는 큐브 격자.
- CPU/GPU ms 그래프 — `std::chrono` + `VkQueryPool` timestamp, ImPlot.
- 호출 카운터 — draw, instanced draw, pipeline bind, barrier, push constant.
- Validation log — Debug 빌드에서 `VK_EXT_debug_utils` messenger 메시지를 링 버퍼로 표시.
- Timeline 값 — Timeline 모드의 submit마다 증가하는 완료 카운터. Acquire/present 연결은 두 모드 모두 binary semaphore를 사용한다.

## 기술 스택

- Vulkan 1.3 (`synchronization2`, `dynamicRendering`, `timelineSemaphore`).
- GLFW surface.
- VMA(Vulkan Memory Allocator).
- Dear ImGui(dynamic rendering backend) + ImPlot.
- glm (`GLM_FORCE_DEPTH_ZERO_TO_ONE`).
- Vulkan SDK `glslc`.
- CMake 3.20+.

## 빌드

```powershell
cmake -S . -B build
cmake --build build --config Release
build\Release\graphics_vk_lab.exe
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/graphics_vk_lab
```

Vulkan SDK가 필요하다. `VULKAN_SDK` 환경변수가 잡혀 있고 `glslc`가 포함되어야 한다. SDK가 없으면 CMake configure 단계에서 `Vulkan_LIBRARY / Vulkan_INCLUDE_DIR / glslc` 누락으로 실패하는 것이 정상이다.

Windows PowerShell 에서는 먼저 다음을 확인한다.

```powershell
$env:VULKAN_SDK
glslc --version
```

두 값 중 하나라도 비어 있으면 LunarG Vulkan SDK 를 설치한 뒤 새 터미널에서 다시 configure 한다.

Release 빌드는 성능 측정 왜곡을 줄이기 위해 validation layer 를 끈다. Validation log 를 확인하려면 Debug 빌드로 실행한다.

## 자동 검증

테스트 바이너리는 `graphics_vk_contract_tests` 하나다. GPU·창·surface 없이 도는 헤드리스 계약 테스트다. **실행 단위는 step** 이라 인자 없이 부르면 전부 돌고, `step04` 같은 step 이름이나 `queue_family_selection` 같은 개별 이름을 주면 그것만 돈다. CTest 에는 전체 실행 하나와 step 별 실행 일곱 개가 등록된다.

```powershell
cmake -S . -B build
cmake --build build --config Release --target graphics_vk_contract_tests
ctest --test-dir build -C Release --output-on-failure
ctest --test-dir build -C Release -R vk_step04_multi_queue --output-on-failure
.\build\Release\graphics_vk_contract_tests.exe step04
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j --target graphics_vk_contract_tests
ctest --test-dir build --output-on-failure
./build/graphics_vk_contract_tests step04
```

전체 실행은 각 계약의 이름과 CHECK 개수를 함께 찍는다. 어느 step 을 골라도 같은 줄이 나오는 일이 없으므로, 자기가 방금 만든 것이 확인됐는지 출력만 보고 판정할 수 있다.

```text
[step01] swapchain_negotiation ... ok (12 checks)
[step01] push_constant_budget ... ok (9 checks)
[step02] timestamp_ring ... ok (536 checks)
[step03] barrier_policy_names ... ok (2 checks)
[step03] minimal_barriers ... ok (49 checks)
[step03] oversync_barriers ... ok (42 checks)
[step03] barrier_record_path ... ok (24 checks)
[step03] barrier_injection ... ok (20 checks)
[step04] queue_family_selection ... ok (26 checks)
[step05] sync_mode_names ... ok (2 checks)
[step05] timeline_counter ... ok (4 checks)
[step05] frame_completion_tracking ... ok (137 checks)
[step06] descriptor_set_shape ... ok (22 checks)
[step07] draw_mode_names ... ok (5 checks)
[step07] draw_submission_counts ... ok (55 checks)
[step07] scene_scale_contract ... ok (13 checks)
all: 16 test(s), 958 checks, 0 failed
```

CTest 쪽 등록은 다음과 같다. 전체 실행 타깃은 그대로 두고 step 별 타깃을 더한 형태이므로, step 하나만 돌려도 전체를 돌려도 검사가 줄지 않는다.

<!-- snippet: CMakeLists.txt@per-step-tests -->
```cmake
add_test(NAME vk_step01_skeleton    COMMAND graphics_vk_contract_tests step01)
add_test(NAME vk_step02_measure     COMMAND graphics_vk_contract_tests step02)
add_test(NAME vk_step03_barrier     COMMAND graphics_vk_contract_tests step03)
add_test(NAME vk_step04_multi_queue COMMAND graphics_vk_contract_tests step04)
add_test(NAME vk_step05_semaphore   COMMAND graphics_vk_contract_tests step05)
add_test(NAME vk_step06_bindless    COMMAND graphics_vk_contract_tests step06)
add_test(NAME vk_step07_mesh_scale  COMMAND graphics_vk_contract_tests step07)
```

어느 계약이 어느 step 소속인지는 테스트 파일의 표 하나가 정한다. 소속 기준은 “그 step 이 무엇을 추가했는가”다.

<!-- snippet: tests/graphics_vk_contract_tests.cpp@step-test-table -->
```cpp
struct StepTest {
    const char* step;
    const char* name;
    void (*fn)();
};

const StepTest kStepTests[] = {
    {"step01", "swapchain_negotiation",     &TestSwapchainSelection},
    {"step01", "push_constant_budget",      &TestPushConstantBudget},
    {"step02", "timestamp_ring",            &TestQueryRing},
    {"step03", "barrier_policy_names",      &TestBarrierPolicyNames},
    {"step03", "minimal_barriers",          &TestMinimalBarriers},
    {"step03", "oversync_barriers",         &TestOverSyncBarriers},
    {"step03", "barrier_record_path",       &TestImageLayoutTransitionRecords},
    {"step03", "barrier_injection",         &TestBarrierInjection},
    {"step04", "queue_family_selection",    &TestQueueFamilySelection},
    {"step05", "sync_mode_names",           &TestSyncModeNames},
    {"step05", "timeline_counter",          &TestTimelineCounter},
    {"step05", "frame_completion_tracking", &TestFrameCompletionTracker},
    {"step06", "descriptor_set_shape",      &TestDescriptorSetShape},
    {"step07", "draw_mode_names",           &TestDrawModeNames},
    {"step07", "draw_submission_counts",    &TestRecordDrawMatchesExpectedSubmissions},
    {"step07", "scene_scale_contract",      &TestSceneScaleContract},
};
```

이 타깃은 디바이스 호출이 없는 제품 번역 단위를 실제로 링크한다. 순수 로직을 새로 분리하면 CMake 의 이 목록에 추가해야 자동 검증 범위에 들어온다 — 여기 없는 TU 는 자동 검증 밖이라는 뜻이다.

<!-- snippet: CMakeLists.txt@contract-test-sources -->
```cmake
add_executable(graphics_vk_contract_tests
    tests/graphics_vk_contract_tests.cpp
    src/barrier/barrier_helpers.cpp
    src/device/descriptor_layout.cpp
    src/device/queue_select.cpp
    src/device/swapchain_select.cpp
    src/measure/query_ring.cpp
    src/scene/scene_core.cpp
    src/sync/frame_completion.cpp
    src/sync/timeline_sync.cpp)
```

검사 대상은 다음과 같다.

| 축 | step | 내용 |
|---|---|---|
| Swapchain 협상 | 1 | present mode fallback(MAILBOX → IMMEDIATE → FIFO)과 surface format 선호 규칙 |
| Push constant 예산 | 1 | PC 블록이 84 bytes 이고 보장 한도 128 bytes 안에 있으며, 프레임 데이터를 합치면 넘는다 |
| Timestamp ring | 2 | full ring 가드와 가장 오래된 슬롯 수확 인덱스 |
| Layout 전이 | 3 | 세 전이(color 진입 / present / depth 초기화)의 `image`·`oldLayout`·`newLayout`·`subresourceRange`·네 mask 를 단언 — 두 color 전이는 Minimal·Over-sync 양쪽에서, depth 초기화 전이는 Minimal 에서 |
| 기록 경로 | 3 | `vkCmdPipelineBarrier2` 를 함수 포인터 seam 으로 가로채 `VkDependencyInfo` 의 barrier 개수와 내용이 빌더 결과와 일치하는지 확인 |
| 정책 fallback | 3 | Minimal 이 모르는 layout 쌍에서 Over-sync 와 비트 단위로 같은 값이 나온다는 계약 |
| 실패 주입 | 3 | skip 토글이 켜지면 기록된 배리어 수가 0, wrong 토글은 `srcAccessMask` 만 0 으로 만든다 |
| Queue family 선택 | 4 | dedicated compute/transfer 판정, 없을 때 graphics family 로의 fallback, present 불가 장치의 탈락 |
| 완료 추적 | 5 | slot/image별 값의 단조 증가, 모드 전환 시 두 체계 격리, swapchain 재생성 시 image 값 초기화 |
| Descriptor set 모양 | 6 | binding 이 둘이고 전부 원소 하나짜리이며 update-after-bind flag 가 없다는 것, pool 이 layout 에서 파생된다는 것 |
| Draw 제출 수 | 7 | fake recorder 로 두 모드를 돌려 `drawCalls + instancedDraws == ExpectedDrawSubmissions(mode, N)` 대조 |
| Scene 규모 | 7 | `Build(N)` 후 `ObjectCount() == N`, `WriteInstances` 의 capacity clamp, 고정 seed 배치의 결정성 |

자동 검증 밖에 남는 것도 분명하다. 실제 GPU 제출·present, 렌더 결과 픽셀, validation layer 가 실패 주입에서 내놓는 메시지 문구는 Debug 빌드 실행 중 사람이 관측한다.

## 진행 상태

- [x] **Step 1 골격** - instance/device/swapchain/pipeline + 동작 렌더 루프.
- [x] **Step 2 측정 인프라** - timestamp query, validation log, 호출 카운터.
- [x] **Step 3 Barrier** - `vkCmdPipelineBarrier2` over-sync/minimal 토글.
- [x] **Step 4 Queue 모델** - graphics/compute/transfer family 선택 + ownership 설계.
- [x] **Step 5 Timeline semaphore** - 값 기반 프레임 동기화 토글.
- [x] **Step 6 Descriptor indexing** - bindless 설계 조건 정리.
- [x] **Step 7 Mesh/Scale** - mesh shader 확장 분리 + 스케일링 판단 기준.

## 학습 기준

이 lab은 Vulkan을 “DX12와 비슷한 저수준 API”로만 보지 않게 만드는 데 초점을 둔다. barrier의 stage/access mask, image layout, queue ownership, semaphore 값은 실제 producer/consumer와 자원 수명에 대응해야 한다. Validation이 조용한 것은 필요한 검사이지만 정확성의 충분조건은 아니다. 명시한 의존성이 실제 데이터 흐름을 빠짐없이 표현하는지 코드와 GPU 캡처로 함께 확인해야 한다.
