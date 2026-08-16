## 들어가며

Step 5 는 Vulkan 동기화 객체를 다룬다. Binary semaphore는 signal 상태를
wait가 소비한 뒤 다시 signal할 수 있는 GPU 의존성 객체이고, Timeline
semaphore는 단조 증가하는 값을 기다리는 객체다. Timeline 값은 여러
in-flight 작업을 하나의 논리적 완료 순서로 표현할 수 있다.

---

## 목차

1. [Binary Semaphore](#1-binary-semaphore)
2. [Timeline Semaphore](#2-timeline-semaphore)
3. [Fence와의 관계](#3-fence와의-관계)
4. [Frame-in-flight 모델](#4-frame-in-flight-모델)
5. [빌드와 실행 결과](#5-빌드와-실행-결과)

---

## 1. Binary Semaphore

전통적인 swapchain 루프는 binary semaphore 를 쓴다.

이 lab 은 `synchronization2` 를 켜므로 제출도 `VkSubmitInfo2` 이고, semaphore 는 `VkSemaphoreSubmitInfo` 로 stage 까지 함께 지정한다.

<!-- snippet: src/app/app.cpp#App::OnFrame -->
```cpp
VkResult acq = vkAcquireNextImageKHR(dev, swap_.Handle(), UINT64_MAX,
                                     fr.imageAvailable, VK_NULL_HANDLE, &imageIndex);
// ... 완료 대기 · 기록 ...
VkSemaphoreSubmitInfo waitInfo{};
waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
waitInfo.semaphore = fr.imageAvailable;
waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

VkSemaphoreSubmitInfo signalInfos[2]{};
signalInfos[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
signalInfos[0].semaphore = renderFinished_[imageIndex];
signalInfos[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
uint32_t signalCount = 1;
if (syncMode_ == SyncMode::Timeline) {          // timeline signal 이 하나 더 붙는다
    signalInfos[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfos[1].semaphore = timeline_.Handle();
    signalInfos[1].value = signalValue;
    signalInfos[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signalCount = 2;
}

VkSubmitInfo2 submit{};
submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
submit.waitSemaphoreInfoCount = 1;
submit.pWaitSemaphoreInfos = &waitInfo;
submit.commandBufferInfoCount = 1;
submit.pCommandBufferInfos = &cmdInfo;
submit.signalSemaphoreInfoCount = signalCount;
submit.pSignalSemaphoreInfos = signalInfos;

VkFence submitFence = (syncMode_ == SyncMode::Binary) ? fr.inFlight : VK_NULL_HANDLE;
if (vkQueueSubmit2(ctx_.GraphicsQueue(), 1, &submit, submitFence) != VK_SUCCESS)
    throw std::runtime_error("vkQueueSubmit2");
```

`submitFence` 는 Binary 모드에서만 frame slot 의 fence 이고, Timeline 모드에서는 `VK_NULL_HANDLE` 이다. 완료 추적을 fence 또는 timeline 값 중 하나로만 하기 때문이다.

이 lab은 frame slot마다 `imageAvailable`과 fence를 두고, swapchain image마다
`renderFinished`를 둔다. acquire semaphore를 재사용하려면 해당 signal을
기다리는 submit이 완료 가능한 상태여야 하고, present wait semaphore는
그 image가 다시 acquire되기 전에 임의로 재사용해서는 안 된다.

## 2. Timeline Semaphore

Timeline semaphore 는 값 기반이다.

<!-- snippet: src/sync/timeline_sync.cpp#TimelineSemaphore::Init -->
```cpp
VkSemaphoreTypeCreateInfo type{};
type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
type.initialValue = 0;

VkSemaphoreCreateInfo ci{};
ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
ci.pNext = &type;
```

signal 은 “값을 N 으로 만든다”, wait 은 “값이 N 이상이 될 때까지 기다린다”가 된다. 여러 frame-in-flight 와 queue 간 의존성을 하나의 semaphore 값으로 표현할 수 있다.

## 3. Fence와의 관계

Fence 는 CPU 가 GPU 완료를 기다릴 때 쓰기 쉽다. Timeline semaphore 도 CPU wait 이 가능하지만, swapchain acquire/present 는 여전히 binary semaphore 와 함께 쓰는 경우가 많다.

| 객체 | 주 사용처 |
|---|---|
| Binary semaphore | acquire/present 연결 |
| Fence | CPU frame resource 재사용 대기 |
| Timeline semaphore | queue 간 값 기반 의존성 |

이 lab의 토글은 같은 frame/image 자원 재사용 조건을 두 방식으로 표현한다.
Fence 모드는 frame slot 및 image의 마지막 제출 fence를 기다리고, Timeline
모드는 각 slot/image에 마지막 signal 값을 기록해 그 값까지 기다린다.
Acquire와 present 연결은 두 모드 모두 binary semaphore를 사용한다.

“무엇을 기다려야 하는가”를 판정하는 상태는 Vulkan 객체와 무관하다. 그래서
`FrameCompletionTracker`(`src/sync/frame_completion.h`)가 slot별·image별 마지막
값과 “이 image 를 마지막으로 쓴 slot” 만 들고 있고, 실제 대기(`vkWaitForFences`
/ `vkWaitSemaphores`)는 그 판정 결과를 받아 앱이 수행한다. 이 분리 덕분에 아래
세 계약이 디바이스 없이 검사된다.

<!-- snippet: src/sync/frame_completion.h#FrameCompletionTracker -->
```cpp
void OnSwapchainRecreated(size_t imageCount);
bool OnSyncModeChanged(SyncMode mode);
// ...
void OnSubmit(int slot, uint32_t image, uint64_t value);
bool NeedsTimelineWait(uint32_t image, uint64_t& outValue) const;
bool NeedsSlotWait(int slot, uint64_t& outValue) const;
int  LastSlotForImage(uint32_t image) const;
```

## 4. Frame-in-flight 모델

Timeline 으로 frame completion 을 표현하면 다음과 같다.

```cpp
uint64_t frameValue = ++nextValue;
vkQueueSubmit2(queue, 1, &submit, VK_NULL_HANDLE);
// submit signal: timeline = frameValue

vkWaitSemaphores(device, &waitInfoFor(frameValue), timeout);
```

장점은 “몇 번째 fence 배열인가” 대신 “어떤 작업 값까지 끝났는가”로 사고한다는 점이다.

## 5. 빌드와 실행 결과

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release -R vk_step05_semaphore --output-on-failure
.\build\Release\graphics_vk_contract_tests.exe step05
build\Release\graphics_vk_lab.exe
```

```text
    Start 6: vk_step05_semaphore
1/1 Test #6: vk_step05_semaphore ..............   Passed    0.04 sec

100% tests passed out of 1

[step05] sync_mode_names ... ok (2 checks)
[step05] timeline_counter ... ok (4 checks)
[step05] frame_completion_tracking ... ok (137 checks)
step05: 3 test(s), 143 checks, 0 failed
```

`timeline_counter` 의 4개는 `Current() == 0`, `Next() == 1`, `Next() == 2`, 즉 값이 1씩 단조 증가한다는 것이다. 나머지 137개가 완료 추적이고, 그중 대부분은 12프레임 루프에서 slot·image별로 기억된 값이 그 제출의 signal 값과 같고 재사용마다 커지는지를 프레임마다 확인하는 데 쓰인다.

가장 깨지기 쉬운 지점은 모드 전환이다. 두 체계가 섞이면 없는 값이나 다른 체계의 fence 를 기다리게 되는데, 이것은 화면에서 간헐적 멈춤으로만 나타나 눈으로 잡기 어렵다.

<!-- snippet: tests/graphics_vk_contract_tests.cpp@sync-mode-isolation -->
```cpp
CHECK(t.OnSyncModeChanged(SyncMode::Binary));
for (uint32_t i = 0; i < kImages; ++i) {
    uint64_t w = 0;
    CHECK(!t.NeedsTimelineWait(i, w));             // Binary 에서는 값 대기 없음.
    CHECK(t.ImageValue(i) == 0);
    CHECK(t.LastSlotForImage(i) == FrameCompletionTracker::kNoSlot);
}
for (int s = 0; s < kSlots; ++s) {
    uint64_t w = 0;
    CHECK(!t.NeedsSlotWait(s, w));
    CHECK(t.SlotValue(s) == 0);
}
```

같은 실행이 swapchain 재생성도 확인한다 — image별 추적값은 0 으로 초기화되고 slot 쪽 기록은 유지된다(command buffer 와 fence 는 재생성되지 않기 때문).

관측 항목.

- 패널의 `Frame sync` 콤보를 `Timeline` 으로 바꾸면 그 아래에 `Timeline completion tracking   (value = N)` 이 뜨고 N 이 제출마다 1씩 증가한다. `Binary fence` 로 돌리면 `Fence completion tracking` 만 남고 값 표시가 사라진다.
- 두 모드 사이를 오가도 화면이 멈추거나 validation 메시지가 뜨지 않아야 한다. 모드 전환 지점에서 fence 추적과 timeline 값 추적이 섞이면 여기서 드러난다.
- 두 모드 모두 acquire/present 는 binary semaphore 를 쓰므로, 전환해도 present 경로의 semaphore 개수는 변하지 않는다.

---

## 결론

Timeline semaphore는 여러 완료 지점을 값 기반 상태로 표현한다. 그렇다고
binary semaphore나 fence를 자동으로 대체하는 것은 아니다. 이 구현에서도
WSI acquire/present는 binary semaphore를 사용하고, CPU의 frame/image 자원
재사용 조건만 fence 또는 timeline 값으로 비교한다. 핵심은 각 wait가 어떤
signal과 자원 수명을 보호하는지 추적하는 것이다.

## 더 읽을거리

- Khronos, ["Vulkan Timeline Semaphores"](https://www.khronos.org/blog/vulkan-timeline-semaphores) — 공식 도입 해설(설계 동기 포함).
- [VK_KHR_timeline_semaphore 명세](https://registry.khronos.org/vulkan/specs/latest/man/html/VkSemaphoreTypeCreateInfo.html) — 값 단조 증가·wait-before-signal 규칙.
- Hans-Kristian Arntzen, [Vulkan 동기화 블로그](https://themaister.net/blog/2019/08/14/yet-another-blog-explaining-vulkan-synchronization/) — fence/semaphore/barrier 의 관계 정리.
