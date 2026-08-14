## 들어가며

DX12 의 *Resource Barrier 패턴* — *Immediate / Batched / Split-style* 세 모드 비교.

OpenGL 은 *드라이버가 자동 동기화*. DX12 는 *모든 자원 state 전이를 프로그래머가 명시*. *Barrier 호출 빈도 + 모드* 가 *GPU stall 비용* 을 결정.

---

## 목차
1. [Resource Barrier 의 비용](#1-resource-barrier-의-비용)
2. [Immediate vs Batched](#2-immediate-vs-batched)
3. [Split Barrier (Begin/End)](#3-split-barrier-beginend)
4. [측정 결과](#4-측정-결과)

---

## 1. Resource Barrier 의 비용

```cpp
D3D12_RESOURCE_BARRIER b{};
b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
b.Transition.pResource   = res;
b.Transition.StateBefore = STATE_RT;
b.Transition.StateAfter  = STATE_PIXEL_SR;
cmd->ResourceBarrier(1, &b);
```

*GPU 측 stall* — *이전 상태의 모든 작업이 끝나야 다음 작업 가능*. CPU 측 *함수 호출* 도 비용 (드라이버가 GPU 명령으로 변환).

## 2. Immediate vs Batched

Immediate — *각 전환마다 한 호출*.

```cpp
for (auto& b : transitions) cmd->ResourceBarrier(1, &b);
```

Batched — *같은 패스의 전환 모아 한 호출*.

```cpp
cmd->ResourceBarrier((UINT)transitions.size(), transitions.data());
```

장점은 API 호출과 명령 기록 단위를 줄일 수 있다는 점이다. 다만 런타임이
임의로 잘못된 상태 전이를 제거하거나 의존성을 약화한다고 가정하면 안 된다.
애플리케이션이 올바른 before/after 상태와 필요한 전이만 제출해야 한다.

## 3. Split Barrier (Begin/End)

```cpp
b.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
cmd->ResourceBarrier(1, &b);    // Begin — 시작

// ... 다른 작업 ...

b.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
cmd->ResourceBarrier(1, &b);    // End — 완료
```

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
직접 돌려주고, 프레임 루프는 그 반환값을 그대로 누적한다.

```cpp
size_t BarrierBatch::FlushBatched(ID3D12GraphicsCommandList* cmd) {
    if (pending_.empty()) return 0;
    cmd->ResourceBarrier((UINT)pending_.size(), pending_.data());
    pending_.clear();
    return 1;
}
```

계약 테스트는 command list 대역을 넘겨 반환값과 실제 기록 수를 대조한다.
계류 배리어 K개에 대해 Immediate는 K, Batched는 `K ? 1 : 0`, Split은 2K이고,
Split 기록은 `BEGIN_ONLY` 다음 `END_ONLY` 순서여야 한다. flush 전 `PendingCount()`가
K, flush 후 0인 것도 함께 본다.

프레임 전이 사슬도 같은 방식으로 못박았다. 프레임 시작과 끝의 배리어 발급은
`RecordBackBufferBarriers(batch, cmd, backBuffer, policy, toRenderTarget)`
하나로 모았고, 테스트는 한 프레임을 기록해 첫 전이가 `PRESENT → RENDER_TARGET`,
두 번째가 `RENDER_TARGET → PRESENT`이며 *앞 전이의 도착 상태가 뒤 전이의 출발
상태와 같음*을 확인한다. 상태 사슬이 끊기면 Debug Layer를 켜기 전에 CTest가
먼저 잡는다.

```powershell
ctest --test-dir build -C Release --output-on-failure
```

---

## 결론

DX12에서는 자원 상태와 전이 시점을 애플리케이션이 소유한다. 이 장면의 토글은
세 기록 형태를 확인하는 도구이며, `Immediate < Batched < Split` 같은 성능
순위를 측정하는 벤치마크가 아니다.

## 더 읽을거리

- Microsoft Learn, [Using Resource Barriers](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12) — transition/aliasing/UAV 배리어 공식 문서.
- [DirectX-Specs: Enhanced Barriers](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html) — 상태 모델을 대체하는 새 배리어 명세.
- MJP, ["Breaking Down Barriers" 시리즈](https://therealmjp.github.io/posts/breaking-down-barriers-part-1-whats-a-barrier/) — GPU 동기화의 가장 깊은 해설.
