#include "device/d3d12_context.h"

#include <stdexcept>
#include <string>

namespace dxlab {

namespace {

void ThrowIfFailed(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s (hr=0x%08lx)", what, (unsigned long)hr);
        throw std::runtime_error(buf);
    }
}

}  // namespace

void D3D12Context::Init(HWND hwnd, int width, int height) {
    width_  = (UINT)width;
    height_ = (UINT)height;

    UINT factoryFlags = 0;
#ifndef NDEBUG
    {
        ComPtr<ID3D12Debug1> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
            dbg->EnableDebugLayer();
            dbg->SetEnableGPUBasedValidation(FALSE);   // 비용 큼 — 필요 시 ON.
        }
        factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
    }
#endif
    ThrowIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_)),
                   "CreateDXGIFactory2");

    CreateDevice();
    CreateQueues();
    CreateSwapChain(hwnd);
    CreateRtvHeap();
    CreateBackBufferRtvs();
    CreateDepthBuffer();
    CreateFrameResources();
}

void D3D12Context::CreateDevice() {
    // 고성능 GPU 선호 — Optimus / Switchable 환경 대응.
    ThrowIfFailed(factory_->EnumAdapterByGpuPreference(
        0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
        IID_PPV_ARGS(&adapter_)), "EnumAdapterByGpuPreference");

    ThrowIfFailed(D3D12CreateDevice(adapter_.Get(),
                                     D3D_FEATURE_LEVEL_12_0,
                                     IID_PPV_ARGS(&device_)),
                   "D3D12CreateDevice");
}

void D3D12Context::CreateQueues() {
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    ThrowIfFailed(device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&directQueue_)),
                   "CreateCommandQueue(Direct)");

    ThrowIfFailed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                        IID_PPV_ARGS(&fence_)),
                   "CreateFence");
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) throw std::runtime_error("CreateEventW failed");
}

void D3D12Context::CreateSwapChain(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width  = width_;
    sd.Height = height_;
    sd.Format = rtvFormat_;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kFramesInFlight;
    sd.SampleDesc.Count = 1;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode  = DXGI_ALPHA_MODE_IGNORE;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    ComPtr<IDXGISwapChain1> sc1;
    ThrowIfFailed(factory_->CreateSwapChainForHwnd(
        directQueue_.Get(), hwnd, &sd, nullptr, nullptr, &sc1),
        "CreateSwapChainForHwnd");
    factory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    sc1.As(&swap_);
    frameIndex_ = swap_->GetCurrentBackBufferIndex();
}

void D3D12Context::CreateRtvHeap() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = kFramesInFlight;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device_->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap_)),
                   "CreateDescriptorHeap(RTV)");
    rtvIncrement_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device_->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap_)),
                   "CreateDescriptorHeap(DSV)");
}

void D3D12Context::CreateBackBufferRtvs() {
    auto cpu = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kFramesInFlight; ++i) {
        ThrowIfFailed(swap_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i])),
                       "SwapChain::GetBuffer");
        device_->CreateRenderTargetView(backBuffers_[i].Get(), nullptr, cpu);
        cpu.ptr += rtvIncrement_;
    }
}

void D3D12Context::CreateDepthBuffer() {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width  = width_;
    desc.Height = height_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = depthFormat_;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear{};
    clear.Format = depthFormat_;
    clear.DepthStencil = { 1.0f, 0 };

    ThrowIfFailed(device_->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear, IID_PPV_ARGS(&depthBuffer_)),
        "CreateCommittedResource(depth)");

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = depthFormat_;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(depthBuffer_.Get(), &dsvDesc,
                                     dsvHeap_->GetCPUDescriptorHandleForHeapStart());
}

void D3D12Context::CreateFrameResources() {
    for (UINT i = 0; i < kFramesInFlight; ++i) {
        ThrowIfFailed(device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&allocators_[i])),
            "CreateCommandAllocator");
    }
    ThrowIfFailed(device_->CreateCommandList(0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocators_[0].Get(), nullptr,
        IID_PPV_ARGS(&cmdList_)),
        "CreateCommandList");
    cmdList_->Close();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::BackBufferRtv(UINT i) const {
    auto h = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    h.ptr += i * rtvIncrement_;
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::DepthDsv() const {
    return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
}

UINT D3D12Context::BeginFrame() {
    frameIndex_ = swap_->GetCurrentBackBufferIndex();
    WaitForFrame(frameIndex_);
    allocators_[frameIndex_]->Reset();
    cmdList_->Reset(allocators_[frameIndex_].Get(), nullptr);
    return frameIndex_;
}

void D3D12Context::EndFrame(bool vsync) {
    cmdList_->Close();
    ID3D12CommandList* lists[] = { cmdList_.Get() };
    directQueue_->ExecuteCommandLists(_countof(lists), lists);

    UINT syncInterval = vsync ? 1 : 0;
    UINT flags = vsync ? 0u : DXGI_PRESENT_ALLOW_TEARING;
    swap_->Present(syncInterval, flags);

    ++nextFenceValue_;
    directQueue_->Signal(fence_.Get(), nextFenceValue_);
    frameFences_[frameIndex_] = nextFenceValue_;
}

void D3D12Context::WaitForFrame(UINT frame) {
    UINT64 v = frameFences_[frame];
    if (fence_->GetCompletedValue() < v) {
        fence_->SetEventOnCompletion(v, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void D3D12Context::Flush() {
    ++nextFenceValue_;
    directQueue_->Signal(fence_.Get(), nextFenceValue_);
    if (fence_->GetCompletedValue() < nextFenceValue_) {
        fence_->SetEventOnCompletion(nextFenceValue_, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void D3D12Context::Resize(int width, int height) {
    if ((UINT)width == width_ && (UINT)height == height_) return;
    if (width <= 0 || height <= 0) return;
    Flush();

    for (auto& b : backBuffers_) b.Reset();
    depthBuffer_.Reset();

    width_  = (UINT)width;
    height_ = (UINT)height;
    ThrowIfFailed(swap_->ResizeBuffers(kFramesInFlight, width_, height_,
                                        rtvFormat_,
                                        DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING),
                   "SwapChain::ResizeBuffers");

    CreateBackBufferRtvs();
    CreateDepthBuffer();
    frameIndex_ = swap_->GetCurrentBackBufferIndex();
}

void D3D12Context::Shutdown() {
    if (fence_ && directQueue_) Flush();
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

}  // namespace dxlab
