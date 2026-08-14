#pragma once

#include "app/frame_cb.h"
#include "device/d3d12_context.h"
#include "device/upload_buffer.h"
#include "rootsig/root_signature.h"
#include "pso/pso_cache.h"
#include "descriptor/heap_manager.h"
#include "barrier/barrier_helpers.h"
#include "scene/scene.h"
#include "measure/frame_timer.h"
#include "measure/timestamp_query.h"
#include "measure/d3d12_debug.h"

#include <Windows.h>
#include <DirectXMath.h>
#include <memory>

namespace dxlab {

class Panel;

class App {
public:
    App(HINSTANCE hInstance, int nCmdShow);
    ~App();

    int Run();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    void CreateMainWindow(HINSTANCE hInstance, int nCmdShow);
    void InitImGui();
    void InitResources();
    void Shutdown();

    void OnFrame();
    void BuildFrameCB();
    void RenderUI();

    HWND        hwnd_   = nullptr;
    int         width_  = 1280;
    int         height_ = 720;

    D3D12Context        ctx_;
    RootSignatureCache  rootSig_;
    PsoCache            psoCache_;
    HeapManager         heap_;
    BarrierBatch        barriers_;
    Scene               scene_;
    FrameTimer          cpuTimer_;
    TimestampQuery      gpuTimer_;
    D3D12Debug          debug_;
    std::unique_ptr<Panel> panel_;

    GlCounters counters_{};

    // Per-frame upload heap for FrameCB.
    ComPtr<ID3D12Resource> frameCB_;
    FrameCB*               frameCBMapped_ = nullptr;

    // ImGui descriptor (SRV heap for ImGui font).
    ComPtr<ID3D12DescriptorHeap> imguiHeap_;

    // Initial upload resources (mesh staging) — keep alive until first frame.
    std::vector<ComPtr<ID3D12Resource>> uploadKeepAlive_;

    bool         vsync_       = false;
    bool         prevVsync_   = false;
    DrawMode     drawMode_    = DrawMode::PerDraw;
    PsoSwitchPolicy psoPolicy_  = PsoSwitchPolicy::PerPass;
    RootSigPolicy   rsPolicy_   = RootSigPolicy::Global;
    HeapPolicy      heapPolicy_ = HeapPolicy::RebuildEachFrame;
    BarrierPolicy   barrierPolicy_ = BarrierPolicy::Immediate;
    bool         useBlinnPhong_ = false;
    int          objectCount_ = 10000;
    int          pendingCount_ = 10000;
};

}  // namespace dxlab
