#include "measure/d3d12_debug.h"

namespace dxlab {

namespace {

DebugMessage::Severity ToSeverity(D3D12_MESSAGE_SEVERITY s) {
    switch (s) {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION: return DebugMessage::Severity::Corruption;
        case D3D12_MESSAGE_SEVERITY_ERROR:      return DebugMessage::Severity::High;
        case D3D12_MESSAGE_SEVERITY_WARNING:    return DebugMessage::Severity::Medium;
        case D3D12_MESSAGE_SEVERITY_INFO:       return DebugMessage::Severity::Low;
        case D3D12_MESSAGE_SEVERITY_MESSAGE:
        default: return DebugMessage::Severity::Notification;
    }
}

}  // namespace

D3D12Debug::~D3D12Debug() { Uninstall(); }

void D3D12Debug::Install(ID3D12Device* device) {
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&infoQueue_)))) {
        // InfoQueue1 미지원 (디버그 레이어 미활성). 무시.
        return;
    }
    if (FAILED(infoQueue_->RegisterMessageCallback(
            &Callback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, this, &cookie_))) {
        cookie_ = 0;
        infoQueue_.Reset();
    }
}

void D3D12Debug::Uninstall() {
    if (infoQueue_ && cookie_ != 0) {
        infoQueue_->UnregisterMessageCallback(cookie_);
        cookie_ = 0;
    }
    infoQueue_.Reset();
}

void D3D12Debug::Clear() { head_ = 0; count_ = 0; }

void D3D12Debug::Push(const DebugMessage& m) {
    ring_[(size_t)head_] = m;
    head_ = (head_ + 1) % kRingSize;
    if (count_ < kRingSize) ++count_;
}

void __stdcall D3D12Debug::Callback(D3D12_MESSAGE_CATEGORY /*category*/,
                                     D3D12_MESSAGE_SEVERITY severity,
                                     D3D12_MESSAGE_ID id,
                                     LPCSTR description,
                                     void* context) {
    auto* self = (D3D12Debug*)context;
    DebugMessage m;
    m.id = (UINT64)id;
    m.severity = ToSeverity(severity);
    m.text = description ? description : "";
    self->Push(m);
}

}  // namespace dxlab
