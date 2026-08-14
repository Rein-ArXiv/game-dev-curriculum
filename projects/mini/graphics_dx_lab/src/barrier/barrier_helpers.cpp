#include "barrier/barrier_helpers.h"

namespace dxlab {

const char* BarrierPolicyName(BarrierPolicy p) {
    switch (p) {
        case BarrierPolicy::Immediate: return "Immediate";
        case BarrierPolicy::Batched:   return "Batched";
        case BarrierPolicy::Split:     return "Split";
    }
    return "?";
}

void BarrierBatch::Transition(ID3D12Resource* res,
                               D3D12_RESOURCE_STATES before,
                               D3D12_RESOURCE_STATES after) {
    pending_.push_back(MakeTransitionBarrier(res, before, after));
}

size_t BarrierBatch::FlushImmediate(ID3D12GraphicsCommandList* cmd) {
    size_t issued = 0;
    for (const auto& b : pending_) {
        cmd->ResourceBarrier(1, &b);
        ++issued;
    }
    pending_.clear();
    return issued;
}

size_t BarrierBatch::FlushBatched(ID3D12GraphicsCommandList* cmd) {
    if (pending_.empty()) return 0;
    cmd->ResourceBarrier((UINT)pending_.size(), pending_.data());
    pending_.clear();
    return 1;
}

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

}  // namespace dxlab
