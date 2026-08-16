## 들어가며

Step 4 는 Vulkan 의 queue 모델을 다룬다. Vulkan 은 graphics, compute, transfer queue 를 분리할 수 있지만, 분리한다고 자동으로 빨라지지 않는다. queue family 가 달라지면 ownership transfer 와 semaphore 의존성을 직접 관리해야 한다.

이 단계의 코드는 queue family 를 선택하는 구조를 갖추고, 문서는 언제 multi-queue 가 의미 있는지 정리한다.

---

## 목차

1. [Queue Family](#1-queue-family)
2. [단일 Queue가 기본값인 이유](#2-단일-queue가-기본값인-이유)
3. [Ownership Transfer](#3-ownership-transfer)
4. [언제 분리하는가](#4-언제-분리하는가)
5. [빌드와 실행 결과](#5-빌드와-실행-결과)

---

## 1. Queue Family

Vulkan physical device 는 queue family 목록을 제공한다.

<!-- snippet: src/device/vk_context.cpp#VkContext::PickPhysicalDevice -->
```cpp
uint32_t qfc = 0;
vkGetPhysicalDeviceQueueFamilyProperties(d, &qfc, nullptr);
std::vector<VkQueueFamilyProperties> qfs(qfc);
vkGetPhysicalDeviceQueueFamilyProperties(d, &qfc, qfs.data());
```

family 는 capability 를 가진다.

| flag | 의미 |
|---|---|
| `VK_QUEUE_GRAPHICS_BIT` | draw 가능 |
| `VK_QUEUE_COMPUTE_BIT` | dispatch 가능 |
| `VK_QUEUE_TRANSFER_BIT` | copy/blit 가능 |

중요한 점은 dedicated compute/transfer queue 가 없을 수 있다는 것이다. 이 경우 graphics queue 하나가 모든 일을 한다. 이 lab 은 “graphics 비트가 없는 compute family”를 dedicated 로 보고, 못 찾으면 graphics family 로 되돌린다.

이 판정에는 Vulkan 핸들이 필요 없다. family 목록과 family 별 present 지원 여부만 있으면 결론이 나오므로, 규칙은 `SelectQueueFamilies`(`src/device/queue_select.h`)라는 순수 함수로 나가 있고 `VkContext::PickPhysicalDevice` 는 장치에서 그 두 입력을 뽑아 넘기기만 한다.

<!-- snippet: src/device/queue_select.cpp#SelectQueueFamilies -->
```cpp
if ((flags & VK_QUEUE_COMPUTE_BIT) &&
    !(flags & VK_QUEUE_GRAPHICS_BIT) && c == kNoQueueFamily) {
    c = i;
}
// ... transfer 도 같은 방식으로 고른다
if (g == kNoQueueFamily) return false;
if (c == kNoQueueFamily) c = g;
if (t == kNoQueueFamily) t = g;
```

present 가능한 graphics family 가 아예 없으면 함수가 `false` 를 돌려주고 호출부가 그 장치를 후보에서 뺀다. 반면 compute/transfer 는 없어도 탈락 사유가 아니다 — graphics family 로 대체된다.

## 2. 단일 Queue가 기본값인 이유

처음에는 graphics queue 하나가 맞다.

- ownership transfer 가 없다.
- submit 순서가 단순하다.
- validation 오류 지점이 적다.
- 작은 장면에서는 multi-queue 이득이 거의 없다.

Vulkan 을 처음 배우는 단계에서 multi-queue 를 먼저 넣으면 barrier 와 semaphore 오류가 학습을 방해한다.

## 3. Ownership Transfer

queue family 가 다른 queue 사이에서 같은 resource 를 쓰려면 ownership transfer 가 필요하다.

```cpp
barrier.srcQueueFamilyIndex = transferFamily;
barrier.dstQueueFamilyIndex = graphicsFamily;
```

이 필드를 빼먹으면 “copy 는 끝났는데 graphics queue 가 소유하지 않은 image 를 읽는” 상태가 된다. 같은 family 안의 queue 라면 ownership transfer 는 필요 없다.

## 4. 언제 분리하는가

multi-queue 는 작업이 실제로 겹칠 때만 의미가 있다.

| 분리 | 의미 있는 경우 |
|---|---|
| transfer queue | 대량 texture/mesh streaming |
| compute queue | graphics 와 독립적인 culling, simulation, post |
| graphics queue only | 대부분의 작은 샘플과 초기 엔진 |

queue overlap 을 측정하려면 각 queue 에 timestamp 를 따로 기록하고 semaphore wait 구간을 함께 봐야 한다. 단순히 queue 를 둘로 나누는 것은 최적화가 아니다.

## 5. 빌드와 실행 결과

family 선택 규칙이 순수 함수로 나가 있으므로, “어떤 장치를 만나면 무엇을 고르는가”는 GPU 없이 확인할 수 있다. 장치를 기다릴 필요 없이 가상의 family 조합을 직접 만들어 넣는다.

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release -R vk_step04_multi_queue --output-on-failure
.\build\Release\graphics_vk_contract_tests.exe step04
```

```text
    Start 5: vk_step04_multi_queue
1/1 Test #5: vk_step04_multi_queue ............   Passed    0.04 sec

100% tests passed out of 1

[step04] queue_family_selection ... ok (26 checks)
step04: 1 test(s), 26 checks, 0 failed
```

26개 CHECK 가 덮는 조합은 일곱이다. dedicated 이 다 있는 장치, 하나도 없는 장치, graphics 비트를 함께 가진 “가짜 dedicated”, compute 비트가 있어 transfer 후보가 못 되는 family, present 를 지원하지 않는 graphics family, present 가능한 graphics 가 아예 없는 장치, present 정보가 없는 호출. 앞의 둘이 이 절의 주제인 fallback 이다.

<!-- snippet: tests/graphics_vk_contract_tests.cpp@queue-family-fallback -->
```cpp
// (a) dedicated 가 다 있는 장치 — 셋이 서로 다른 family 로 갈린다.
vklab::QueueFamilies q;
CHECK(vklab::SelectQueueFamilies(MakeFamilies({kG, kC, kT}), {}, q));
CHECK(q.graphics == 0);
CHECK(q.compute == 1);
CHECK(q.transfer == 2);

// (b) dedicated 가 하나도 없으면 셋 다 graphics family 로 접힌다.
vklab::QueueFamilies only;
CHECK(vklab::SelectQueueFamilies(MakeFamilies({kG}), {}, only));
CHECK(only.graphics == 0);
CHECK(only.compute == 0);          // c == kNoQueueFamily → c = g.
CHECK(only.transfer == 0);         // t == kNoQueueFamily → t = g.
```

`!(flags & VK_QUEUE_GRAPHICS_BIT)` 조건을 지우면 (c) 가, present 검사를 지우면 (e)·(f) 가 깨진다. 반대로 이 실행이 확인하지 못하는 것은 “실제 장치의 family 목록이 무엇인가”다. 그것은 드라이버가 답하는 값이라 실행으로 본다.

```powershell
cmake --build build --config Debug
build\Debug\graphics_vk_lab.exe
```

관측 항목.

- 이 step 은 설계 토픽이므로 패널에 토글이 생기지 않는다. 관측 대상은 화면이 아니라 device 생성 결과다. dedicated compute/transfer family 가 없는 장치에서도 실행이 성공하면 `c = g` fallback 이 실제 장치에서도 동작한 것이다.
- 실제 제출 경로는 `ctx_.GraphicsQueue()` 하나뿐이다. `computeQueue_` / `transferQueue_` 는 family 선택 구조를 보여 주기 위해 생성만 하고 이 lab 의 워크로드에서는 사용하지 않는다 — 그래서 ownership transfer 도 발생하지 않고, `Validation log` 에 queue ownership 관련 메시지가 뜨지 않는 것이 정상이다.
- 정리하면 이 step 의 종료 상태는 “single graphics queue 로 모든 제출이 이루어지고, 분리 조건만 코드 구조에 남아 있다”이다.

---

## 결론

Vulkan multi-queue 는 고급 최적화다. 이 lab 에서는 구조와 조건을 먼저 고정한다. 단일 graphics queue 에서 barrier 가 정확히 동작한 뒤에야 compute/transfer queue 분리가 의미를 가진다.

## 더 읽을거리

- AMD GPUOpen, ["Concurrent Execution: Asynchronous Queues"](https://gpuopen.com/learn/concurrent-execution-asynchronous-queues/) — 비동기 큐 이득의 조건과 측정.
- [Vulkan Specification: Queue Family Ownership Transfer](https://registry.khronos.org/vulkan/specs/latest/html/chap7.html#synchronization-queue-transfers) — 큐 간 자원 소유권 이전 명세.
- [Sascha Willems: Compute N-body 예제](https://github.com/SaschaWillems/Vulkan/tree/master/examples/computenbody) — 그래픽스+컴퓨트 큐 병행의 실물.
