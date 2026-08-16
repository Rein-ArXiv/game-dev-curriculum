## 들어가며

`graphics_vk_lab` 의 첫 단계는 렌더링 결과보다 *Vulkan 의 계약 표면* 을 세우는 것이다. OpenGL lab 은 드라이버가 숨겨 주는 비용을 측정했고, DX12 lab 은 PSO/RootSig/Barrier 호출 패턴을 드러냈다. Vulkan 은 여기서 한 단계 더 나아가 *image layout, stage/access mask, queue family ownership, semaphore 값* 까지 앱이 명시해야 한다.

이 단계의 목표는 화면에 큐브를 띄우는 것 자체가 아니라, barrier·timestamp·sync 검증이 올라설 수 있는 *정확한 계약 구조* 를 만드는 것이다. 이 구조 위에서 lab 은 실제로 동작하는 렌더러로 완성된다.

이 lab 은 Vulkan SDK 와 shader compiler 인 `glslc` 를 전제로 한다. Windows 에서는 `VULKAN_SDK` 환경변수와 `glslc --version` 이 모두 확인되어야 CMake configure 가 통과한다. 둘 중 하나가 비어 있으면 코드 문제가 아니라 SDK 설치·터미널 환경 문제다.

1. GLFW window + Vulkan instance.
2. validation layer + debug utils messenger.
3. surface present 를 지원하는 physical device + graphics/compute/transfer queue family 선택.
4. Vulkan 1.3 feature chain: `synchronization2`, `dynamicRendering`, `timelineSemaphore`.
5. swapchain + image view.
6. shader module + graphics pipeline skeleton.

---

## 목차

1. [Vulkan lab의 범위](#1-vulkan-lab의-범위)
2. [Instance와 Validation](#2-instance와-validation)
3. [Device와 Feature Chain](#3-device와-feature-chain)
4. [Swapchain Skeleton](#4-swapchain-skeleton)
5. [Pipeline Skeleton](#5-pipeline-skeleton)
6. [빌드와 실행 결과](#6-빌드와-실행-결과)

---

## 1. Vulkan lab의 범위

이 lab 은 `graphics_gl_lab` 과 같은 큐브 격자를 dynamic rendering 으로 그리는 동작 렌더러다. 다만 Vulkan 은 최소 렌더 루프 자체가 길어서, 한 번에 모든 고급 기능을 넣으면 학습 목표가 흐려진다. 그래서 범위를 둘로 나눈다. 골격·측정·barrier·timeline semaphore 는 *실제 동작·측정* 으로 구현하고, multi-queue / bindless / mesh shader 는 *언제 필요한가* 를 판단하는 설계 토픽으로 분리한다.

잘못된 방향은 다음이다.

- Mesh Shader, Bindless, Multi-Queue 를 모두 구현했다고 주장.
- RenderDoc 통합이나 queue overlap 수치를 실제 측정값처럼 서술.
- `vkCmdPipelineBarrier2` 의 stage/access mask 없이 “barrier 비용”만 이야기.

올바른 방향은 다음이다.

- Vulkan 의 기본 계약을 정확히 만들고 실제로 그린다.
- timeline semaphore 를 프레임 동기화 경로에 실제로 넣는다.
- multi-queue / bindless / mesh shader 는 *언제 필요한가* 관점으로 분리하고, 실행 코드가 없는 토픽은 “설계 지점”이라고 명시한다.

## 2. Instance와 Validation

Instance 생성은 GLFW 가 요구하는 surface extension 을 켜고, validation 을 쓰는 빌드에서만 debug utils extension 을 덧붙인다.

<!-- snippet: src/device/vk_context.cpp#VkContext::CreateInstance -->
```cpp
uint32_t glfwExtCount = 0;
const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
std::vector<const char*> exts(glfwExts, glfwExts + glfwExtCount);
if (kEnableValidation) {
    exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
}
```

`kEnableValidation` 은 `NDEBUG` 로 갈리므로 Release 빌드에서는 messenger extension 도 layer 도 올라가지 않는다. 측정용 빌드에 validation 비용이 섞이지 않게 하는 것이 목적이다.

Validation layer 는 Vulkan 학습에서 선택이 아니다. OpenGL 에서는 틀린 상태도 드라이버가 보정하는 경우가 많지만, Vulkan 은 잘못된 layout/access/queue ownership 이 곧 undefined behavior 로 이어진다.

## 3. Device와 Feature Chain

이 lab 의 기준은 Vulkan 1.3 이다.

<!-- snippet: src/device/vk_context.cpp#VkContext::CreateLogicalDevice -->
```cpp
VkPhysicalDeviceVulkan13Features v13{};
v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
v13.synchronization2 = VK_TRUE;
v13.dynamicRendering = VK_TRUE;

VkPhysicalDeviceVulkan12Features v12{};
v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
v12.timelineSemaphore = VK_TRUE;
v12.pNext = &v13;
```

핵심은 feature 를 “있으면 좋다”가 아니라 *이 lab 의 설명 단위* 로 고정하는 것이다. `synchronization2` 없이는 이 lab 의 barrier 가 낡은 API 로 되돌아간다. timeline semaphore 없이는 값 기반 프레임 동기화가 성립하지 않는다.

`descriptorIndexing` 과 `runtimeDescriptorArray` 는 bindless 설계 조건으로만 다룬다. 실행 코드가 bindless texture path 를 만들지 않는데 이 optional feature 를 필수로 켜면, 단순 렌더러가 돌아갈 수 있는 장치에서도 device 생성이 실패할 수 있다.

Device 선택은 feature 와 surface 조건을 함께 봐야 한다. 이 lab 은 Vulkan 1.3, `synchronization2`, `dynamicRendering`, `timelineSemaphore` 가 모두 가능한 장치만 선택한다. 또한 graphics queue 라고 해서 항상 해당 window surface 에 present 할 수 있는 것은 아니다.

<!-- snippet: src/device/vk_context.cpp#VkContext::PickPhysicalDevice -->
```cpp
VkBool32 presentSupported = VK_TRUE;
if (surface != VK_NULL_HANDLE) {
    vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &presentSupported);
}
```

이 lab 은 임시 probe surface 로 physical device 를 고른 뒤 swapchain 단계에서 실제 surface 를 다시 만든다. 핵심은 *present 가능한 graphics queue family 를 고른다* 는 점이다. 초기값이 `VK_TRUE` 인 것은 surface 가 없는 호출(창 없이 device 만 고를 때)에서 present 조건을 검사하지 않겠다는 뜻이고, surface 가 있으면 곧바로 질의 결과로 덮인다.

## 4. Swapchain Skeleton

Swapchain 은 format/present mode 를 반드시 질의해야 한다. `MAILBOX` 를 요청한다고 드라이버가 자동으로 `FIFO` 로 폴백하지 않는다. 지원하지 않는 present mode 를 넣으면 `vkCreateSwapchainKHR` 가 실패할 수 있다.

따라서 코드는 다음 순서가 맞다.

1. `vkGetPhysicalDeviceSurfaceFormatsKHR`.
2. 선호 format 선택, 없으면 첫 format.
3. `vkGetPhysicalDeviceSurfacePresentModesKHR`.
4. `MAILBOX` 가 있으면 선택, 없으면 `FIFO`.

이 선택 과정 자체가 Vulkan 의 명시성이다.

## 5. Pipeline Skeleton

Pipeline 은 shader module 만으로 완성되지 않는다. vertex shader 의 `layout(location = 0) in vec3 aPos` 와 pipeline 의 vertex input description 이 정확히 맞아야 한다.

<!-- snippet: src/pipeline/pipeline_cache.cpp#PipelineCache::Build -->
```cpp
VkVertexInputAttributeDescription attrs[2]{};
attrs[0].location = 0;
attrs[0].binding = 0;
attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
attrs[0].offset = offsetof(VertexForLayout, pos);
attrs[1].location = 1;
attrs[1].binding = 0;
attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
attrs[1].offset = offsetof(VertexForLayout, normal);
```

`VertexForLayout` 은 `pipeline_cache.cpp` 안에 둔 레이아웃 전용 구조체다. 정점 데이터 자체는 `scene.cpp` 의 `Vertex` 가 갖고 있고, 파이프라인은 그 메모리 레이아웃만 알면 되므로 두 파일이 같은 타입을 공유하지 않는다 — 대신 `stride` 와 `offset` 이 어긋나면 화면이 깨지는 계약이 된다.

데이터는 갱신 빈도에 따라 세 곳으로 나눈다. 프레임마다 한 번 바뀌는 `viewProj`·조명·카메라는 set 0, binding 0 의 UBO 로, 오브젝트별 변환은 Instanced 모드에서 binding 1 의 SSBO 로, Per-Draw 모드의 오브젝트별 `model`·`color` 는 push constant 로 보낸다.

push constant 범위는 shader 와 정확히 맞아야 하고, 사양이 보장하는 최소 `maxPushConstantsSize` 가 128 bytes 라는 점도 지켜야 한다. 그래서 PC block 은 `mat4 model + vec4 color + uint mode` (84 bytes) 로 줄였다. 프레임 데이터를 push constant 에 넣어 160 bytes 를 쓰면 AMD 등 128 bytes 한도 장치에서 깨진다 — 자주 안 바뀌는 데이터를 UBO 로 분리해야 하는 실질적 이유다.

또한 메모리 타입 선택과 suballocation 은 VMA(`vmaCreateBuffer` / `vmaCreateImage`) 에 위임했고 이 lab 의 학습 대상이 아니다. staging→device-local 복사와 그 뒤의 buffer barrier 는 직접 작성한다.

## 6. 빌드와 실행 결과

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -R vk_step01_skeleton --output-on-failure
.\build\Release\graphics_vk_contract_tests.exe step01
build\Release\graphics_vk_lab.exe
```

`vk_step01_skeleton` 은 골격이 세운 계약 둘만 돈다. 계약 이름과 CHECK 개수까지 함께 출력되므로, 통과 표시가 아니라 *무엇이 몇 개 확인됐는지* 가 보인다.

```text
    Start 2: vk_step01_skeleton
1/1 Test #2: vk_step01_skeleton ...............   Passed    0.04 sec

100% tests passed out of 1

[step01] swapchain_negotiation ... ok (12 checks)
[step01] push_constant_budget ... ok (9 checks)
step01: 2 test(s), 21 checks, 0 failed
```

첫째는 swapchain 협상이다. vsync 를 켜면 무조건 `FIFO`, 끄면 `MAILBOX > IMMEDIATE > FIFO` 순으로 고른다. 후보 목록이 비어도 `FIFO` 로 떨어진다 — 이 규칙이 §4 의 네 단계 순서를 코드로 고정한 것이다.

<!-- snippet: tests/graphics_vk_contract_tests.cpp#TestSwapchainSelection -->
```cpp
// vsync 켜짐 → 무조건 FIFO.
CHECK(vklab::ChoosePresentMode(all, true) == VK_PRESENT_MODE_FIFO_KHR);
CHECK(vklab::ChoosePresentMode(noMailbox, true) == VK_PRESENT_MODE_FIFO_KHR);
// vsync 꺼짐 → MAILBOX > IMMEDIATE > FIFO.
CHECK(vklab::ChoosePresentMode(all, false) == VK_PRESENT_MODE_MAILBOX_KHR);
CHECK(vklab::ChoosePresentMode(noMailbox, false) == VK_PRESENT_MODE_IMMEDIATE_KHR);
CHECK(vklab::ChoosePresentMode(fifoOnly, false) == VK_PRESENT_MODE_FIFO_KHR);
CHECK(vklab::ChoosePresentMode({}, false) == VK_PRESENT_MODE_FIFO_KHR);
// ...
bool threw = false;
try {
    (void)vklab::ChooseSurfaceFormat({});
} catch (const std::runtime_error&) {
    threw = true;
}
CHECK(threw);
```

둘째는 push constant 예산이다. 앞에서 84 bytes 라고 적은 값이 실제 구조체 크기와 같은지, 그리고 프레임 데이터까지 합치면 보장 한도 128 bytes 를 넘는지를 같은 실행이 확인한다. shader 의 push_constant 블록과 offset 이 어긋나는 변이도 여기서 잡힌다.

<!-- snippet: tests/graphics_vk_contract_tests.cpp#TestPushConstantBudget -->
```cpp
CHECK(sizeof(vklab::PushConstants) == 84);      // mat4(64) + vec4(16) + uint(4).
CHECK(sizeof(vklab::PushConstants) <= 128);     // 사양이 보장하는 최소 한도.
CHECK(offsetof(vklab::PushConstants, model) == 0);
CHECK(offsetof(vklab::PushConstants, color) == 64);
CHECK(offsetof(vklab::PushConstants, mode) == 80);
// ...
CHECK(sizeof(vklab::FrameUBO) == 96);
CHECK(sizeof(vklab::PushConstants) + sizeof(vklab::FrameUBO) > 128);
```

instance/device/messenger 생성은 `VkInstance` 가 있어야 성립하므로 이 실행 단위 밖이다. 그쪽은 아래 화면 관측이 담당한다.

관측 항목.

- configure·빌드가 통과하면 `build/shaders/` 에 `cube.vert.spv`, `lambert.frag.spv`, `blinn.frag.spv` 세 SPIR-V 가 생기고 실행 파일 옆 `shaders/` 로 복사된다. `VULKAN_SDK` 나 `glslc` 가 없으면 configure 단계에서 멈춘다 — 코드 문제가 아니다.
- 실행하면 창이 뜨고 회전하는 큐브 격자와 `graphics_vk_lab` 패널이 함께 보인다. 패널 첫 줄의 `FPS / CPU ms / GPU ms` 가 매 프레임 갱신되면 acquire→record→submit→present 루프가 실제로 돌고 있다는 뜻이다.
- 창 크기를 바꾸면 swapchain 이 재생성되어 그림이 늘어나지 않고 새 해상도로 다시 그려진다.

---

## 결론

이 단계의 핵심은 “Vulkan으로 큐브를 그렸다”가 아니다. *Vulkan은 명시하지 않은 것은 존재하지 않는다* 는 규칙을 코드 구조에 반영하는 것이다. Instance, device feature chain, swapchain 선택, pipeline layout, vertex input, push constant 크기가 모두 shader와 렌더 루프의 계약에 맞아야 한다. 이 계약이 어긋나면 동기화와 성능 측정 결과도 신뢰할 수 없다.

## 더 읽을거리

- [Vulkan Tutorial](https://vulkan-tutorial.com/) — instance→swapchain→파이프라인의 표준 입문.
- [vkguide.dev](https://vkguide.dev/) — 동적 렌더링·VMA 기반의 현대적 구성.
- [Vulkan Specification: Limits](https://registry.khronos.org/vulkan/specs/latest/html/chap33.html) — maxPushConstantsSize 128B 등 보장 한도의 원문.
