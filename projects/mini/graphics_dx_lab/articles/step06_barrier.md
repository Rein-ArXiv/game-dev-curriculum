## 들어가며

DX12 의 *Resource Barrier 패턴* — *Immediate / Batched / Split-style* 세 모드 비교.

OpenGL 은 *드라이버가 자동 동기화*. DX12 는 *모든 자원 state 전이를 프로그래머가 명시*. *Barrier 호출 빈도 + 모드* 가 *GPU stall 비용* 을 결정.

시작 상태 — 백버퍼 전이가 프레임 루프에 직접 박혀 있다. 종료 상태 — 전이 기록이 `BarrierBatch` 한 곳으로 모이고, 패널의 `barrier` 콤보가 같은 전이를 세 가지 형태로 기록하며, 발급 수가 카운터에 그대로 반영된다.

## 이 step 이 추가·변경하는 파일

| 파일 | 상태 | 역할 |
|---|---|---|
| `src/barrier/barrier_helpers.h` | 변경 | `BarrierPolicy`, `MakeTransitionBarrier`, `BarrierBatch`, `RecordBackBufferBarriers` 선언 |
| `src/barrier/barrier_helpers.cpp` | 변경 | 세 Flush + 백버퍼 전이 사슬 |
| `src/app/app.cpp` | 변경 | 프레임 시작·끝의 전이를 헬퍼 호출로 교체 |
| `src/ui/panel.cpp` | 변경 | `barrier` 콤보 |
| `tests/graphics_dx_contract_tests.cpp` | 변경 | 발급 수와 실제 기록 수 대조, 전이 사슬 검사 |
| `CMakeLists.txt` | 변경 | 계약 테스트 소스에 `src/barrier/barrier_helpers.cpp` 추가 |

---

## 목차
1. [Resource Barrier 의 비용](#1-resource-barrier-의-비용)
2. [Immediate vs Batched](#2-immediate-vs-batched)
3. [Split Barrier (Begin/End)](#3-split-barrier-beginend)
4. [측정 결과](#4-측정-결과)
5. [자동 검증](#5-자동-검증)

---

## 1. Resource Barrier 의 비용

전이 배리어 하나를 만드는 코드는 짧다. 이 lab 은 그 구조체 채우기를 한 함수로 모아 두고 세 모드가 모두 같은 값을 쓰게 한다.

<!-- snippet: src/barrier/barrier_helpers.h#MakeTransitionBarrier -->
```cpp
inline D3D12_RESOURCE_BARRIER MakeTransitionBarrier(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}
```

*GPU 측 stall* — *이전 상태의 모든 작업이 끝나야 다음 작업 가능*. CPU 측 *함수 호출* 도 비용 (드라이버가 GPU 명령으로 변환).

전이는 모아 두었다가 발급한다. 모으는 쪽은 `Transition`, 내보내는 쪽은 세 `Flush` 다.

<!-- snippet: src/barrier/barrier_helpers.h#BarrierBatch -->
```cpp
class BarrierBatch {
public:
    void Reset() { pending_.clear(); }

    void Transition(ID3D12Resource* res,
                    D3D12_RESOURCE_STATES before,
                    D3D12_RESOURCE_STATES after);

    size_t FlushImmediate(ID3D12GraphicsCommandList* cmd);
    size_t FlushBatched(ID3D12GraphicsCommandList* cmd);
    size_t FlushSplit(ID3D12GraphicsCommandList* cmd);

    size_t PendingCount() const { return pending_.size(); }

private:
    std::vector<D3D12_RESOURCE_BARRIER> pending_;
};
```

## 2. Immediate vs Batched

Immediate — *각 전환마다 한 호출*.

<!-- snippet: src/barrier/barrier_helpers.cpp#BarrierBatch::FlushImmediate -->
```cpp
size_t BarrierBatch::FlushImmediate(ID3D12GraphicsCommandList* cmd) {
    size_t issued = 0;
    for (const auto& b : pending_) {
        cmd->ResourceBarrier(1, &b);
        ++issued;
    }
    pending_.clear();
    return issued;
}
```

Batched — *같은 패스의 전환 모아 한 호출*.

<!-- snippet: src/barrier/barrier_helpers.cpp#BarrierBatch::FlushBatched -->
```cpp
size_t BarrierBatch::FlushBatched(ID3D12GraphicsCommandList* cmd) {
    if (pending_.empty()) return 0;
    cmd->ResourceBarrier((UINT)pending_.size(), pending_.data());
    pending_.clear();
    return 1;
}
```

장점은 API 호출과 명령 기록 단위를 줄일 수 있다는 점이다. 다만 런타임이
임의로 잘못된 상태 전이를 제거하거나 의존성을 약화한다고 가정하면 안 된다.
애플리케이션이 올바른 before/after 상태와 필요한 전이만 제출해야 한다.

## 3. Split Barrier (Begin/End)

<!-- snippet: src/barrier/barrier_helpers.cpp#BarrierBatch::FlushSplit -->
```cpp
size_t BarrierBatch::FlushSplit(ID3D12GraphicsCommandList* cmd) {
    size_t issued = 0;
    for (auto b : pending_) {
        b.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
        cmd->ResourceBarrier(1, &b);
        b.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
        cmd->ResourceBarrier(1, &b);
        issued += 2;
    }
    pending_.clear();
    return issued;
}
```

루프 변수가 참조가 아니라 *값 복사* 인 것에 주의한다. `Flags` 를 두 번 바꿔 쓰므로 계류 목록의 원본을 건드리면 안 된다.

Begin 과 End 사이에 *다른 GPU 작업이 진행* → barrier stall *은닉*. 큰 텍스처 transition / shadow map → main pass 같은 경우에 효과.

이 lab 은 BackBuffer 의 *PRESENT → RT → PRESENT* 만 처리한다. Split-style 모드는 begin/end barrier 호출 형태를 보여주지만, 두 호출 사이에 은닉할 작업이 거의 없어 성능 이득은 작다. 대형 씬에서 *디퍼드 G-buffer / shadow* 전환에 의미가 커진다.

## 4. 측정 결과

이 lab 시나리오는 패스 경계마다 back buffer 전환이 한 개뿐이다. 따라서
Immediate와 Batched는 모두 `ResourceBarrier` 한 번에 transition 하나를 기록해
동일하다. Split-style은 begin/end 두 호출의 의미를 보여 주지만 두 호출 사이에
독립 작업이 없으므로 stall 은닉 실험이 아니다.

여러 자원이 같은 경계에서 전이되는 렌더러라면 배열 한 번으로 기록하는 batched
형태가 CPU 호출 수를 줄인다. Split barrier는 begin과 end 사이에 실제 독립 GPU
작업이 있고, 두 구간의 resource 사용 계약이 명확할 때만 의미가 있다.

graphics 시리즈의 *"Split Barrier 가 GPU stall 을 줄일 수 있다"* 는 주장의 전제는 큰 transition 사이에 독립 작업을 배치할 수 있을 때다. 작은 lab 에서는 호출 패턴 이해가 목표다.

## 5. 자동 검증

“모드마다 `ResourceBarrier`를 몇 번 부르는가”는 카운터에 정책별 상수를 더해
짐작할 문제가 아니다. 그렇게 하면 실제 발급 수와 표시 값이 조용히 갈라진다.
그래서 세 Flush 함수가 *이번 호출에서 발급한 `ResourceBarrier` 호출 수*를
직접 돌려주고(§2·§3 의 `return` 값), 프레임 루프는 그 반환값을 그대로 누적한다.

프레임 전이 사슬도 한 함수로 못박았다. 프레임 시작과 끝이 같은 규약을 쓰도록 방향 하나만 인자로 받는다.

<!-- snippet: src/barrier/barrier_helpers.cpp#RecordBackBufferBarriers -->
```cpp
size_t RecordBackBufferBarriers(BarrierBatch& batch,
                                ID3D12GraphicsCommandList* cmd,
                                ID3D12Resource* backBuffer,
                                BarrierPolicy policy,
                                bool toRenderTarget) {
    batch.Reset();
    batch.Transition(backBuffer,
                     toRenderTarget ? D3D12_RESOURCE_STATE_PRESENT
                                    : D3D12_RESOURCE_STATE_RENDER_TARGET,
                     toRenderTarget ? D3D12_RESOURCE_STATE_RENDER_TARGET
                                    : D3D12_RESOURCE_STATE_PRESENT);
    switch (policy) {
        case BarrierPolicy::Immediate: return batch.FlushImmediate(cmd);
        case BarrierPolicy::Batched:   return batch.FlushBatched(cmd);
        case BarrierPolicy::Split:     return batch.FlushSplit(cmd);
    }
    return 0;
}
```

프레임 루프의 두 지점은 이 함수 호출과 카운터 누적이 전부다.

<!-- snippet: src/app/app.cpp@barrier-entry -->
```cpp
const size_t entryBarriers = RecordBackBufferBarriers(
    barriers_, cmd, ctx_.BackBuffer(f), barrierPolicy_, true);
counters_.Reset();
counters_.barrierCalls += (int)entryBarriers;
```

<!-- snippet: src/app/app.cpp@barrier-exit -->
```cpp
counters_.barrierCalls += (int)RecordBackBufferBarriers(
    barriers_, cmd, ctx_.BackBuffer(f), barrierPolicy_, false);
```

`counters_.Reset()` 이 진입 배리어 *다음* 에 오는 것이 의도다. 그래야 리셋 직후 더해지는 값부터 이번 프레임의 수가 된다.

계약 테스트는 command list 대역을 넘겨 반환값과 실제 기록 수를 대조한다.
계류 배리어 K개에 대해 Immediate는 K, Batched는 `K ? 1 : 0`, Split은 2K이고,
Split 기록은 `BEGIN_ONLY` 다음 `END_ONLY` 순서여야 한다. flush 전 `PendingCount()`가
K, flush 후 0인 것도 함께 본다.

전이 사슬은 한 프레임을 기록해 첫 전이가 `PRESENT → RENDER_TARGET`,
두 번째가 `RENDER_TARGET → PRESENT`이며 *앞 전이의 도착 상태가 뒤 전이의 출발
상태와 같음*을 확인한다. 상태 사슬이 끊기면 Debug Layer를 켜기 전에 CTest가
먼저 잡는다.

<!-- snippet: tests/graphics_dx_contract_tests.cpp#TestBackBufferBarrierSequence -->
```cpp
void TestBackBufferBarrierSequence() {
    dxlab::BarrierBatch batch;
    FakeCmdList cmd;
    const size_t entry = dxlab::RecordBackBufferBarriers(
        batch, &cmd, FakeResource(), dxlab::BarrierPolicy::Immediate, true);
    const size_t exit = dxlab::RecordBackBufferBarriers(
        batch, &cmd, FakeResource(), dxlab::BarrierPolicy::Immediate, false);
    CHECK(entry == 1);
    CHECK(exit == 1);
    CHECK(cmd.barriers.size() == 2);

    const auto& first = cmd.barriers[0].list[0].Transition;
    const auto& second = cmd.barriers[1].list[0].Transition;
    CHECK(first.StateBefore == D3D12_RESOURCE_STATE_PRESENT);
    CHECK(first.StateAfter == D3D12_RESOURCE_STATE_RENDER_TARGET);
    CHECK(second.StateBefore == D3D12_RESOURCE_STATE_RENDER_TARGET);
    CHECK(second.StateAfter == D3D12_RESOURCE_STATE_PRESENT);
    // 앞 전이의 도착 상태 == 뒤 전이의 출발 상태 (상태 사슬이 끊기지 않는다).
    CHECK(first.StateAfter == second.StateBefore);
    CHECK(first.pResource == second.pResource);
    // ... Split 정책이면 같은 시퀀스를 begin/end 로 나눠 4 회 발급한다.
}
```

`entry` 와 `exit` 를 따로 받아 두는 것이 요점이다. 반환값이 실제 기록 수와 갈라지면 카운터가 조용히 거짓말을 하게 되는데, 여기서는 두 값과 `cmd.barriers.size()` 를 나란히 놓아 그 가능성을 닫는다.

배리어 몫만 도는 등록은 이 한 줄이다.

<!-- snippet: CMakeLists.txt@step06-target -->
```cmake
add_test(NAME graphics_dx_step06_barrier      COMMAND graphics_dx_contract_tests step06)
```

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release -R graphics_dx_step06 --output-on-failure
build\Release\graphics_dx_contract_tests.exe step06
```

```text
1/1 Test #7: graphics_dx_step06_barrier .......   Passed    0.03 sec

100% tests passed out of 1

  [step06] barrier_description                         5 checks
  [step06] barrier_flush_counts                        44 checks
  [step06] back_buffer_barrier_sequence                12 checks
step06: 3 groups, 61 checks, 0 failures
DirectX contract tests passed
```

`barrier_flush_counts` 의 44 판정은 계류 K 를 0 · 1 · 3 으로 바꿔 가며 세 정책을 모두 돌린 결과다. K = 0 이 섞여 있는 것이 의도다 — 계류가 없을 때 Batched 가 빈 `ResourceBarrier` 를 발급하지 않는다는 것도 계약이기 때문이다.

---

## 결론

DX12에서는 자원 상태와 전이 시점을 애플리케이션이 소유한다. 이 장면의 토글은
세 기록 형태를 확인하는 도구이며, `Immediate < Batched < Split` 같은 성능
순위를 측정하는 벤치마크가 아니다.

이 step 의 한계 — 전이 대상이 백버퍼 하나뿐이라 `PendingCount()` 가 항상 1 이다. 배칭이 실제로 줄이는 호출 수도, split 이 숨길 수 있는 stall 도 이 장면에는 존재하지 않는다.

## 더 읽을거리

- Microsoft Learn, [Using Resource Barriers](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12) — transition/aliasing/UAV 배리어 공식 문서.
- [DirectX-Specs: Enhanced Barriers](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html) — 상태 모델을 대체하는 새 배리어 명세.
- MJP, ["Breaking Down Barriers" 시리즈](https://therealmjp.github.io/posts/breaking-down-barriers-part-1-whats-a-barrier/) — GPU 동기화의 가장 깊은 해설.
