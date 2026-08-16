## 들어가며

Step 3 은 Vulkan 의 핵심인 Pipeline Barrier 를 다룬다. Vulkan barrier 는 “기다린다” 하나가 아니라 *stage mask*, *access mask*, *image layout* 세 축을 동시에 명시한다.

이 lab 의 barrier 정책은 두 가지다.

1. Over-sync — 같은 layout 전이에 넓은 stage/access 범위를 쓰는 비교용 barrier.
2. Minimal — 실제 전이에 필요한 stage/access 만 지정.

---

## 목차

1. [Barrier의 세 축](#1-barrier의-세-축)
2. [Over-sync](#2-over-sync)
3. [Minimal Barrier](#3-minimal-barrier)
4. [측정 기준](#4-측정-기준)
5. [빌드와 실행 결과](#5-빌드와-실행-결과)
6. [실패 주입](#6-실패-주입)

---

## 1. Barrier의 세 축

`VkImageMemoryBarrier2` 의 핵심 필드는 다음이다.

```cpp
VkImageMemoryBarrier2 b{};
b.oldLayout = oldLayout;
b.newLayout = newLayout;
b.srcStageMask = srcStage;
b.dstStageMask = dstStage;
b.srcAccessMask = srcAccess;
b.dstAccessMask = dstAccess;
```

layout 은 이미지의 사용 형태, stage 는 파이프라인 위치, access 는 메모리 접근 종류다. 셋 중 하나가 틀리면 validation 이 잡거나, 더 나쁘게는 특정 GPU 에서만 깨진다.

## 2. Over-sync

Over-sync 모드는 layout과 queue family 계약은 그대로 지키면서, 실제
producer/consumer보다 넓은 stage/access 범위를 지정한다. 넓은 mask는 누락된
layout 전이나 잘못된 queue ownership transfer를 복구하지 않으므로 “가장
안전한 barrier”라는 뜻은 아니다.

<!-- snippet: src/barrier/barrier_helpers.cpp#BuildImageBarrier -->
```cpp
b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
// ... (subresourceRange, 정책 분기)
b.srcStageMask = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
    ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
    : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
b.dstStageMask = newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    ? VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT
    : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
b.srcAccessMask = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
    ? 0
    : VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
b.dstAccessMask = newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    ? 0
    : VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
```

queue family 는 정책과 무관하게 두 인덱스 모두 `VK_QUEUE_FAMILY_IGNORED` 다. 이 lab 은 제출을 graphics queue 하나로만 하기 때문이다. ownership transfer 가 필요해지는 순간에는 mask 보다 이 두 줄이 먼저 바뀌어야 한다.

UNDEFINED 소스와 PRESENT_SRC 대상만 예외로 두는 이유는 그 둘이 각각 “읽을 이전 내용이 없음”과 “presentation engine 이 access mask 로 표현되지 않는 소비자”라는 계약이기 때문이다. 그 밖에는 넓은 mask 를 그대로 쓴다.

장점은 dependency 범위를 좁히기 전의 보수적인 출발점을 표현하기 쉽다는
점이다. 단점은 실제로 color attachment write만 연결하면 되는 상황에서도
관련 없는 stage를 dependency 범위에 포함할 수 있다는 점이다. 다만 이 lab의
단일 color pass처럼 겹칠 독립 작업이 거의 없으면 측정 차이가 0에 가까울 수
있다.

## 3. Minimal Barrier

swapchain image 를 color attachment 로 쓰는 전이는 다음처럼 좁힐 수 있다.

<!-- snippet: src/barrier/barrier_helpers.cpp#BuildImageBarrier -->
```cpp
// undefined -> color attachment
b.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
b.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
b.srcAccessMask = 0;
b.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
```

present 로 넘길 때는 color output write 만 source 로 잡는다.

<!-- snippet: src/barrier/barrier_helpers.cpp#BuildImageBarrier -->
```cpp
// color attachment -> present
b.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
b.dstStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
b.dstAccessMask = 0;
```

depth image 의 초기화 전이도 같은 표에 들어 있다. 이쪽은 소비자가 depth 테스트이므로 early/late fragment test 두 stage 를 함께 잡는다.

<!-- snippet: src/barrier/barrier_helpers.cpp#BuildImageBarrier -->
```cpp
// undefined -> depth attachment
b.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
b.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
b.srcAccessMask = 0;
b.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
```

이 차이가 Vulkan 학습의 핵심이다. barrier 는 “많이 넣으면 안전”이 아니라 “정확히 넣어야 빠르고 안전”하다.

다만 이 lab 의 Minimal 이 정확한 mask 를 아는 전이는 위 세 가지뿐이다. `BuildImageBarrier` 의 Minimal 분기는 3-엔트리 lookup table 이고, 그 밖의 layout 조합은 `ALL_COMMANDS` + `MEMORY_READ | MEMORY_WRITE` 라는 보수적 fallback 으로 떨어져 Over-sync 와 비트 단위로 같은 값을 낸다. 새 전이를 추가하면서 이 테이블을 갱신하지 않으면 Minimal 을 골라도 조용히 over-sync 가 된다.

## 4. 측정 기준

작은 큐브 장면에서는 over-sync 와 minimal 의 GPU ms 차이가 작을 수 있다. 그렇다고 학습 가치가 없는 것이 아니다.

| 장면 | 차이 |
|---|---|
| 단일 color pass | 작음 |
| shadow + G-buffer + post | 독립 작업의 overlap을 제한할 가능성 증가 |
| async compute 포함 | queue 사이 의존 범위를 넓히면 overlap 감소 가능 |
| transfer upload 병행 | ownership/access 오류 가능성 증가 |

이 lab 의 목적은 큰 수치를 만드는 것이 아니라 *정확한 barrier 작성 습관* 을 만드는 것이다.

## 5. 빌드와 실행 결과

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release -R vk_step03_barrier --output-on-failure
.\build\Release\graphics_vk_contract_tests.exe step03
build\Release\graphics_vk_lab.exe
```

```text
    Start 4: vk_step03_barrier
1/1 Test #4: vk_step03_barrier ................   Passed    0.03 sec

100% tests passed out of 1

[step03] barrier_policy_names ... ok (2 checks)
[step03] minimal_barriers ... ok (49 checks)
[step03] oversync_barriers ... ok (42 checks)
[step03] barrier_record_path ... ok (24 checks)
[step03] barrier_injection ... ok (20 checks)
step03: 5 test(s), 137 checks, 0 failed
```

다섯 이름이 각각 고정하는 것은 다음과 같다.

- `minimal_barriers` / `oversync_barriers` — 세 전이(UNDEFINED→COLOR_ATTACHMENT_OPTIMAL, COLOR_ATTACHMENT_OPTIMAL→PRESENT_SRC_KHR, UNDEFINED→DEPTH_ATTACHMENT_OPTIMAL) 각각의 `image` / `oldLayout` / `newLayout` / `subresourceRange` / 네 mask 를 양쪽 정책에서 단언한다. Minimal 이 정확한 mask 를 아는 layout 쌍은 그 셋뿐이고 그 밖의 조합에서는 Over-sync 와 비트 단위로 같은 값이 나온다는 fallback 계약도 `oversync_barriers` 쪽에 있다.
- `barrier_record_path` — `ImageLayoutTransition` 이 실제로 채워 넘기는 `VkDependencyInfo` 를 함수 포인터 seam 으로 가로채, barrier 개수가 1 이고 그 내용이 `BuildImageBarrier` 결과와 완전히 같은지 확인한다.
- `barrier_injection` — 아래 실패 주입 토글의 headless 계약. 자세한 것은 이어지는 절에 있다.
- `barrier_policy_names` — 패널 콤보에 뜨는 두 정책 이름.

관측 항목.

- 실행 화면의 패널 카운터에서 `barriers` 가 매 프레임 `2` 다 — color 전이 두 번(UNDEFINED→COLOR_ATTACHMENT_OPTIMAL, COLOR_ATTACHMENT_OPTIMAL→PRESENT_SRC_KHR)이 프레임당 한 번씩 기록된다. depth 전이는 swapchain 생성 시 한 번뿐이라 이 per-frame 카운터에는 잡히지 않지만, 같은 `BuildImageBarrier` 를 통과하므로 `Barrier policy` 토글의 영향은 받는다.
- `Barrier policy` 콤보를 Over-sync ↔ Minimal 로 바꿔도 `barriers` 카운터는 2로 유지되고 바뀌는 것은 mask 뿐이다. 단일 color pass 라 GPU ms 차이는 측정 잡음 수준일 수 있다 — 위 표의 “단일 color pass = 작음”이 그대로 관측된다.
- Debug 빌드로 두 정책을 모두 켜 봐도 `Validation log` 는 조용해야 한다. 메시지가 뜨면 mask 가 실제 producer/consumer 를 덮지 못한다는 뜻이다.

## 6. 실패 주입

“validation 이 조용하다”가 의미를 가지려면, 틀렸을 때 시끄러워진다는 것도 같이 보여야 한다. 패널의 `Barrier policy` 아래에 의도적 오류 토글 두 개가 있다.

| 토글 | 코드 동작 | 기대되는 validation 오류 유형 |
|---|---|---|
| `Inject: skip barriers` | 프레임당 color 전이 두 개를 아예 기록하지 않는다 | wrong image layout — attachment 가 `COLOR_ATTACHMENT_OPTIMAL` 이 아니고 present 시점에 `PRESENT_SRC_KHR` 도 아니다 |
| `Inject: wrong srcAccessMask` | `BuildImageBarrier` 결과의 `srcAccessMask` 만 0 으로 덮어쓴다 | wrong access mask — 이전 쓰기를 덮지 못하는 dependency |

두 토글의 계약 중 headless 로 잡을 수 있는 부분은 `barrier_injection` 이 검사한다. `ShouldRecordBarrier` / `ApplyInjection` / `ExpectedBarrierCalls` 는 순수 함수로 분리되어 있고, 테스트는 `App::RecordFrame` 과 같은 순서로 이 셋을 호출한다. 기록 훅이 실제로 몇 번 불렸는지까지 세므로 "계산은 0 이라고 하는데 기록은 되는" 어긋남이 남지 않는다.

<!-- snippet: tests/graphics_vk_contract_tests.cpp@barrier-injection-record -->
```cpp
for (const vklab::BarrierInjection& inj : {none, skip, wrong}) {
    g_captureCalls = 0;
    int recorded = 0;
    for (int t = 0; t < 2; ++t) {   // 프레임당 color 전이 2회.
        if (!vklab::ShouldRecordBarrier(inj)) continue;
        const auto b = vklab::ApplyInjection(clean, inj);
        vklab::ImageLayoutTransitions(fakeCmd, &b, 1);
        ++recorded;
    }
    CHECK(recorded == vklab::ExpectedBarrierCalls(inj, 2));
    CHECK(g_captureCalls == vklab::ExpectedBarrierCalls(inj, 2));
}
```

wrong 토글이 `srcAccessMask` 만 0 으로 만들고 layout·image·`dstAccessMask` 는 건드리지 않는다는 것도 같은 실행이 확인한다.

반면 “그래서 validation layer 가 실제로 무엇을 말하는가”는 자동 검증 대상이 아니다. Debug 빌드로 실행해 토글을 켜고 `Validation log` 를 눈으로 읽는 **수동 관측** 절차다.

```powershell
cmake --build build --config Debug
build\Debug\graphics_vk_lab.exe
```

---

## 결론

Vulkan barrier 는 성능 최적화 이전에 correctness 장치다. 넓은 dependency
mask와 정확한 dependency는 모두 올바른 layout 및 queue ownership 계약을
전제로 한다. producer가 실제로 실행되는 stage/access와 consumer가 실제로
기다려야 하는 stage/access를 식별한 뒤 그 범위만 연결하는 것이 목표다.

## 더 읽을거리

- [Vulkan Synchronization Examples (Khronos Wiki)](https://github.com/KhronosGroup/Vulkan-Docs/wiki/Synchronization-Examples) — 패턴별 정답 barrier 모음.
- Hans-Kristian Arntzen, ["Yet another blog explaining Vulkan synchronization"](https://themaister.net/blog/2019/08/14/yet-another-blog-explaining-vulkan-synchronization/) — stage/access/layout 세 축의 최고 해설.
- Khronos, [Understanding Vulkan Synchronization](https://www.khronos.org/blog/understanding-vulkan-synchronization) — 공식 블로그 정리.
