#pragma once

// D3D12Context — Device + DXGI Factory + Direct Queue + Swap Chain.
//
// 소유 자원: Direct Queue 1개(Compute/Copy 큐는 이 lab 범위 밖) + RGBA8 RTV
// + D24S8 DSV. 프레임 in flight 3개. Allocator 도 그만큼이지만 CmdList 는 하나를
// 프레임마다 Reset 해 재사용한다.

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

namespace dxlab {

template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

constexpr UINT kFramesInFlight = 3;

class D3D12Context {
public:
    void Init(HWND hwnd, int width, int height);
    void Shutdown();

    void Resize(int width, int height);

    // Frame lifecycle.
    UINT  BeginFrame();   // returns frame index.
    void  EndFrame(bool vsync);
    void  Flush();

    ID3D12Device8*           Device()       { return device_.Get(); }
    ID3D12GraphicsCommandList6* CmdList()   { return cmdList_.Get(); }
    ID3D12CommandQueue*      DirectQueue()  { return directQueue_.Get(); }
    IDXGISwapChain4*         SwapChain()    { return swap_.Get(); }
    ID3D12Resource*          BackBuffer(UINT i) { return backBuffers_[i].Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE BackBufferRtv(UINT i) const;
    D3D12_CPU_DESCRIPTOR_HANDLE DepthDsv() const;
    DXGI_FORMAT              RtvFormat() const { return rtvFormat_; }
    DXGI_FORMAT              DepthFormat() const { return depthFormat_; }
    UINT                     CurrentFrame() const { return frameIndex_; }
    UINT                     Width()  const { return width_; }
    UINT                     Height() const { return height_; }

private:
    void CreateDevice();
    void CreateQueues();
    void CreateSwapChain(HWND hwnd);
    void CreateRtvHeap();
    void CreateBackBufferRtvs();
    void CreateDepthBuffer();
    void CreateFrameResources();
    void WaitForFrame(UINT frame);

    ComPtr<IDXGIFactory7>            factory_;
    ComPtr<IDXGIAdapter4>            adapter_;
    ComPtr<ID3D12Device8>            device_;
    ComPtr<ID3D12CommandQueue>       directQueue_;
    ComPtr<IDXGISwapChain4>          swap_;
    ComPtr<ID3D12DescriptorHeap>     rtvHeap_;
    ComPtr<ID3D12DescriptorHeap>     dsvHeap_;
    UINT                              rtvIncrement_ = 0;

    std::array<ComPtr<ID3D12Resource>,        kFramesInFlight> backBuffers_{};
    std::array<ComPtr<ID3D12CommandAllocator>, kFramesInFlight> allocators_{};
    ComPtr<ID3D12GraphicsCommandList6> cmdList_;

    ComPtr<ID3D12Resource>            depthBuffer_;

    ComPtr<ID3D12Fence>               fence_;
    HANDLE                             fenceEvent_ = nullptr;
    std::array<UINT64, kFramesInFlight> frameFences_{};
    UINT64                             nextFenceValue_ = 0;

    UINT  width_  = 0;
    UINT  height_ = 0;
    UINT  frameIndex_  = 0;
    DXGI_FORMAT rtvFormat_   = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT depthFormat_ = DXGI_FORMAT_D24_UNORM_S8_UINT;
};

}  // namespace dxlab
