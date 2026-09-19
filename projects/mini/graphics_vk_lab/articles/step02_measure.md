## 들어가며

Step 2 는 Vulkan 측정 인프라다. Vulkan 에서 측정은 두 축이다. 하나는 GPU timestamp query 로 *명령 구간의 GPU 시간* 을 보는 것, 다른 하나는 validation layer 로 *명시 계약 위반* 을 잡는 것이다.

이 단계에서 중요한 점은 timestamp query 도 동기화 대상이라는 것이다. query 결과를 같은 프레임에서 바로 읽으면 `VK_NOT_READY` 이거나 CPU wait 이 필요하다. 따라서 ring buffer 로 몇 프레임 늦게 읽는다.

---

## 목차

1. [Timestamp Query](#1-timestamp-query)
2. [Query Ring](#2-query-ring)
3. [Validation Log](#3-validation-log)
4. [측정 해석](#4-측정-해석)
5. [빌드와 실행 결과](#5-빌드와-실행-결과)

---

## 1. Timestamp Query

GPU timestamp 는 query pool 에 기록한다.

```cpp
vkCmdResetQueryPool(cmd, pool, slot * 2, 2);
vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, slot * 2);

// draw / dispatch / barrier 구간

vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool, slot * 2 + 1);
```

두 timestamp 의 차이에 physical device property 의 `timestampPeriod` 를 곱하면 ns 단위가 된다.

```cpp
double ms = double(ts1 - ts0) * timestampPeriodNs / 1.0e6;
```

`timestampPeriod` 는 `float` 이므로 정수로 자르면 안 된다. 일부 GPU 에서는 1ns 미만 해상도를 가질 수 있다.

## 2. Query Ring

query 결과는 GPU 가 해당 명령을 실행한 뒤에만 유효하다. 따라서 최소 2~3 frame ring 을 둔다. ring 이 꽉 찬 상태에서 가장 오래된 query 가 아직 준비되지 않았다면, 같은 slot 을 reset 하면 안 된다. 이때는 CPU 를 기다리게 하지 말고 새 timestamp 기록을 한 프레임 건너뛴다.

인덱싱 규칙 자체에는 `VkDevice` 가 필요 없다. 그래서 `writeIdx_` / `inFlight_` / `recording_` 세 상태와 슬롯 계산은 `QueryRing`(`src/measure/query_ring.h`)이라는 순수 클래스로 분리되어 있고 `TimestampQuery` 가 그것을 갖고 있다.

<!-- snippet: src/measure/query_ring.cpp -->
```cpp
int QueryRing::Begin() {
    if (inFlight_ >= kRingSize) {   // 아직 읽지 않은 슬롯을 덮어쓰지 않는다
        recording_ = false;
        return -1;
    }
    recording_ = true;
    return writeIdx_;
}
// ...
int QueryRing::ReadIndex() const {   // 수확 대상은 언제나 가장 오래된 슬롯
    if (inFlight_ == 0) return -1;
    return (writeIdx_ - inFlight_ + kRingSize) % kRingSize;
}
```

수확은 그 인덱스를 non-blocking 으로 읽는다. 결과가 아직 없으면 `vkGetQueryPoolResults` 가 `VK_NOT_READY` 를 돌려주고, 그대로 돌아가 다음 프레임에 다시 시도한다 — `VK_QUERY_RESULT_WAIT_BIT` 를 넣지 않는 것이 핵심이다.

<!-- snippet: src/measure/timestamp_query.cpp#TimestampQuery::Poll -->
```cpp
void TimestampQuery::Poll(VkContext& ctx) {
    const int readIdx = ring_.ReadIndex();
    if (readIdx < 0) return;
    uint64_t ts[2] = {0, 0};
    VkResult r = vkGetQueryPoolResults(ctx.Device(), pool_,
                                        readIdx * 2, 2,
                                        sizeof(ts), ts, sizeof(uint64_t),
                                        VK_QUERY_RESULT_64_BIT);
    if (r != VK_SUCCESS) return;   // VK_NOT_READY 포함 — 기다리지 않는다
    ring_.Retire();
    // ...
}
```

이 패턴의 의미는 “측정 때문에 GPU/CPU 동기화를 만들지 않는다”이다. 최신 프레임 값이 아니라 몇 프레임 전 값이지만 성능 그래프에는 충분하다. 덕분에 다음 계약이 헤드리스 테스트로 고정된다.

- 수확 없이 `kRingSize`(3)개까지 슬롯을 열 수 있고, 그 뒤 `Begin()` 은 -1 을 돌려주며 `recording_` 은 false 로 남는다.
- 수확 대상은 항상 가장 오래된 슬롯이다 — `(writeIdx_ - inFlight_ + kRingSize) % kRingSize`.
- 100 프레임을 돌려도 write/read 인덱스가 `[0, kRingSize)` 를 벗어나지 않고 `inFlight_` 가 `kRingSize` 를 넘지 않는다.

full ring 가드를 지우거나 read 인덱스 계산을 `writeIdx_` 로 바꾸면 이 세 검사가 곧바로 깨진다.

## 3. Validation Log

Vulkan 의 validation 은 OpenGL 의 debug output 보다 더 중요하다. 특히 다음 오류를 잡는다.

| 오류 | 의미 |
|---|---|
| wrong image layout | barrier 누락 또는 layout 전이 오류 |
| wrong access mask | stage/access 조합 불일치 |
| descriptor lifetime | descriptor set/update 시점 오류 |
| queue ownership | multi-queue ownership transfer 누락 |
| object lifetime | GPU 사용 중인 객체 파괴 |

Validation log 를 ImGui 에 연결하면 “화면이 깨졌다”보다 먼저 원인을 볼 수 있다.

## 4. 측정 해석

timestamp 는 GPU 구간 시간을 말한다. CPU submit 비용이나 validation layer 비용과는 다르다.

| 측정 | 도구 |
|---|---|
| GPU pass time | timestamp query |
| CPU frame time | `std::chrono` |
| submit overhead | `vkQueueSubmit` 전후 CPU timer |
| 잘못된 동기화 | validation / sync validation |

Vulkan 에서 성능 그래프가 의미 있으려면 validation 이 먼저 조용해야 한다. 잘못된 layout 위에서 얻은 GPU 시간은 최적화 근거가 될 수 없다.

## 5. 빌드와 실행 결과

인덱싱 규칙은 `VkDevice` 없이 성립하므로 먼저 헤드리스로 돌린다.

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release -R vk_step02_measure --output-on-failure
.\build\Release\graphics_vk_contract_tests.exe step02
```

```text
    Start 3: vk_step02_measure
1/1 Test #3: vk_step02_measure ................   Passed    0.03 sec

100% tests passed out of 1

[step02] timestamp_ring ... ok (536 checks)
step02: 1 test(s), 536 checks, 0 failed
```

536개 CHECK 중 대부분은 100 프레임 루프의 불변식이다 — 매 프레임 write/read 인덱스가 `[0, kRingSize)` 안에 있고 `inFlight_` 가 `kRingSize` 를 넘지 않는지 확인하므로 개수가 프레임 수만큼 늘어난다. 나머지가 full ring 가드와 수확 인덱스 계산이다.

<!-- snippet: tests/graphics_vk_contract_tests.cpp@ring-full-guard -->
```cpp
// ring full — 새 기록을 열지 않는다.
CHECK(ring.Begin() == -1);
CHECK(!ring.Recording());
CHECK(ring.End() == -1);
CHECK(ring.InFlight() == 3);

// 수확하면 다시 열린다. readIdx = (writeIdx - inFlight + kRingSize) % kRingSize.
ring.Retire();
CHECK(ring.InFlight() == 2);
CHECK(ring.ReadIndex() == 1);
```

`inFlight_ >= kRingSize` 가드를 지우면 첫 두 줄이, `ReadIndex()` 를 `writeIdx_` 로 바꾸면 마지막 줄이 곧바로 깨진다.

GPU 시간과 validation 메시지 자체는 실제 디바이스가 있어야 나오므로 그쪽은 실행으로 본다.

```powershell
cmake --build build --config Debug
build\Debug\graphics_vk_lab.exe
```

관측 항목.

- 패널 첫 줄에 `FPS / CPU ms / GPU ms` 가 뜨고, 그 아래 ImPlot 그래프에 `CPU ms` 와 `GPU ms` 두 선이 함께 그려진다. GPU 선은 3-slot ring 을 거쳐 오므로 CPU 선보다 몇 프레임 늦게 반응한다.
- `Validation log` 헤더를 펼치면 Debug 빌드의 messenger 메시지가 최신순으로 쌓인다. 정상 상태에서는 비어 있거나 정보성 메시지만 보인다 — 이 상태가 이후 측정의 전제다.
- Release(`--config Release`) 로 빌드하면 `kEnableValidation` 이 꺼져 같은 로그가 비어 있다. 측정용 빌드와 검증용 빌드를 구분하는 이유다.
- validation 메시지는 패널뿐 아니라 stderr 에도 `[vk] ...` 접두로 출력되므로 콘솔에서도 확인할 수 있다.

---

## 결론

Step 2 의 결과는 “빠른 렌더러”가 아니라 *측정 가능한 렌더러의 조건* 이다. timestamp query 는 GPU 시간을, validation log 는 명시 계약의 정확성을 확인한다. 다만 지금 측정되는 것은 단일 렌더 패스 하나뿐이라, 동기화 방식을 바꿨을 때 그 수치가 어떻게 움직이는지는 이 상태에서 관측할 수 없다.

## 더 읽을거리

- Khronos, [Vulkan Validation Layers 문서](https://github.com/KhronosGroup/Vulkan-ValidationLayers) — 동기화 검증(sync validation) 활성화 포함.
- [Vulkan Specification: Queries](https://docs.vulkan.org/spec/latest/chapters/queries.html) — timestamp query·쿼리 풀 명세.
- [Tracy Profiler](https://github.com/wolfpld/tracy) — CPU+GPU 통합 프레임 프로파일링의 다음 단계.
