#include "measure/timestamp_query.h"

#include <stdexcept>

namespace dxlab {

void TimestampQuery::Init(ID3D12Device* device, ID3D12CommandQueue* queue) {
    queue->GetTimestampFrequency(&frequency_);
    queue_ = queue;

    D3D12_QUERY_HEAP_DESC desc{};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = kRingSize * 2;     // begin/end 쌍.
    if (FAILED(device->CreateQueryHeap(&desc, IID_PPV_ARGS(&heap_)))) {
        throw std::runtime_error("CreateQueryHeap failed");
    }

    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width  = kRingSize * 2 * sizeof(UINT64);
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&readback_)))) {
        throw std::runtime_error("CreateCommittedResource(readback) failed");
    }
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                    IID_PPV_ARGS(&fence_)))) {
        throw std::runtime_error("CreateFence(timestamp) failed");
    }
    slotFences_.fill(0);
    history_.fill(0.0f);
}

void TimestampQuery::InitHeadless(ID3D12QueryHeap* heap, ID3D12Resource* readback,
                                  ID3D12CommandQueue* queue, ID3D12Fence* fence,
                                  UINT64 frequency) {
    heap_      = heap;
    readback_  = readback;
    queue_     = queue;
    fence_     = fence;
    frequency_ = frequency;
    slotFences_.fill(0);
    history_.fill(0.0f);
}

void TimestampQuery::Shutdown() {
    fence_.Reset();
    queue_.Reset();
    heap_.Reset();
    readback_.Reset();
}

void TimestampQuery::Begin(ID3D12GraphicsCommandList* cmd) {
    activeSlot_ = -1;
    if (!fence_) return;

    const UINT64 previous = slotFences_[(size_t)writeIdx_];
    if (previous != 0 && !IsSlotReady(fence_->GetCompletedValue(), previous)) {
        return;
    }

    activeSlot_ = writeIdx_;
    cmd->EndQuery(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                  activeSlot_ * 2);
}

void TimestampQuery::End(ID3D12GraphicsCommandList* cmd) {
    if (activeSlot_ < 0) return;

    cmd->EndQuery(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                  activeSlot_ * 2 + 1);
    cmd->ResolveQueryData(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                          activeSlot_ * 2, 2,
                          readback_.Get(),
                          activeSlot_ * 2 * sizeof(UINT64));
    submittedSlot_ = activeSlot_;
    writeIdx_ = (activeSlot_ + 1) % kRingSize;
    activeSlot_ = -1;
}

void TimestampQuery::SignalSubmitted() {
    if (submittedSlot_ < 0 || !queue_ || !fence_) return;
    const UINT64 value = ++nextFenceValue_;
    if (FAILED(queue_->Signal(fence_.Get(), value))) {
        submittedSlot_ = -1;
        return;
    }
    slotFences_[(size_t)submittedSlot_] = value;
    submittedSlot_ = -1;
}

void TimestampQuery::Poll() {
    if (!fence_) return;
    const UINT64 ready = slotFences_[(size_t)readIdx_];
    if (ready == 0) return;
    if (!IsSlotReady(fence_->GetCompletedValue(), ready)) return;

    UINT64* mapped = nullptr;
    D3D12_RANGE rd{(size_t)(readIdx_ * 2 * sizeof(UINT64)),
                    (size_t)((readIdx_ * 2 + 2) * sizeof(UINT64))};
    if (FAILED(readback_->Map(0, &rd, reinterpret_cast<void**>(&mapped)))) return;
    UINT64 ts0 = mapped[readIdx_ * 2 + 0];
    UINT64 ts1 = mapped[readIdx_ * 2 + 1];
    D3D12_RANGE wr{0, 0};
    readback_->Unmap(0, &wr);
    slotFences_[(size_t)readIdx_] = 0;
    readIdx_ = (readIdx_ + 1) % kRingSize;

    if (frequency_ == 0 || ts1 < ts0) return;
    lastMs_ = (double)(ts1 - ts0) * 1000.0 / (double)frequency_;
    if (emaMs_ <= 0.0) emaMs_ = lastMs_;
    else emaMs_ = emaMs_ * (1.0 - kEmaAlpha) + lastMs_ * kEmaAlpha;

    history_[(size_t)historyHead_] = (float)lastMs_;
    historyHead_ = (historyHead_ + 1) % kHistorySize;
}

}  // namespace dxlab
