#pragma once

// Resource Barrier 헬퍼 — Step 6 의 Barrier 모드 토글용.
//
// 세 모드:
//   - Immediate : 매 전환마다 즉시 발급.
//   - Batched   : 같은 패스의 전환을 모아 한 번에 발급.
//   - Split     : Begin/End barrier 로 transition 을 시간 분할.
//
// 현재 장면은 패스 경계마다 전환이 한 개뿐이다. 따라서 이 헬퍼는 호출
// 형태와 split semantics를 보여 주며, 모드별 GPU 성능 비교를 보장하지 않는다.

#include <d3d12.h>

#include <vector>

namespace dxlab {

enum class BarrierPolicy {
    Immediate,
    Batched,
    Split,
};

const char* BarrierPolicyName(BarrierPolicy p);

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

class BarrierBatch {
public:
    void Reset() { pending_.clear(); }

    void Transition(ID3D12Resource* res,
                    D3D12_RESOURCE_STATES before,
                    D3D12_RESOURCE_STATES after);

    // 세 Flush 는 *이번 호출에서 발급한 ResourceBarrier 호출 수* 를 돌려준다.
    // 계류 배리어가 K 개일 때 Immediate=K, Batched=(K?1:0), Split=2K.
    // 호출자는 이 값을 그대로 카운터에 누적하면 되고, 계수를 정책별 상수로
    // 짐작할 필요가 없다.
    size_t FlushImmediate(ID3D12GraphicsCommandList* cmd);
    size_t FlushBatched(ID3D12GraphicsCommandList* cmd);
    size_t FlushSplit(ID3D12GraphicsCommandList* cmd);

    size_t PendingCount() const { return pending_.size(); }

private:
    std::vector<D3D12_RESOURCE_BARRIER> pending_;
};

// 백버퍼 상태 전이 기록 — 프레임 시작(PRESENT→RENDER_TARGET)과
// 끝(RENDER_TARGET→PRESENT) 두 지점이 같은 규약을 쓰도록 한 곳에 모은다.
// 반환값은 발급된 ResourceBarrier 호출 수.
size_t RecordBackBufferBarriers(BarrierBatch& batch,
                                ID3D12GraphicsCommandList* cmd,
                                ID3D12Resource* backBuffer,
                                BarrierPolicy policy,
                                bool toRenderTarget);

}  // namespace dxlab
