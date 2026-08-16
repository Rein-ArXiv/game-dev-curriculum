#include "app/app.h"
#include "ui/panel.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>
#include <implot.h>

#include <algorithm>
#include <stdexcept>
#include <cstring>

using namespace DirectX;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace dxlab {

namespace {
App* g_app = nullptr;
}

App::App(HINSTANCE hInstance, int nCmdShow)
    : panel_(std::make_unique<Panel>()) {
    g_app = this;
    CreateMainWindow(hInstance, nCmdShow);
    ctx_.Init(hwnd_, width_, height_);
    InitImGui();
    InitResources();
}

App::~App() {
    Shutdown();
    g_app = nullptr;
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_app && wp != SIZE_MINIMIZED) {
            int w = LOWORD(lp), h = HIWORD(lp);
            g_app->width_  = (w > 0) ? w : g_app->width_;
            g_app->height_ = (h > 0) ? h : g_app->height_;
            if (g_app->ctx_.Device()) g_app->ctx_.Resize(g_app->width_, g_app->height_);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void App::CreateMainWindow(HINSTANCE hInstance, int nCmdShow) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &App::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"graphics_dx_labCls";
    RegisterClassExW(&wc);

    RECT r{0, 0, width_, height_};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"graphics_dx_lab",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top,
                             nullptr, nullptr, hInstance, nullptr);
    if (!hwnd_) throw std::runtime_error("CreateWindowExW failed");
    ShowWindow(hwnd_, nCmdShow);
}

void App::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::StyleColorsDark();

    // ImGui DX12 백엔드는 *SRV heap 한 슬롯* 필요 (폰트 텍스처).
    D3D12_DESCRIPTOR_HEAP_DESC d{};
    d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    d.NumDescriptors = 1;
    d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ctx_.Device()->CreateDescriptorHeap(&d, IID_PPV_ARGS(&imguiHeap_));

    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX12_Init(ctx_.Device(), kFramesInFlight,
                         ctx_.RtvFormat(), imguiHeap_.Get(),
                         imguiHeap_->GetCPUDescriptorHandleForHeapStart(),
                         imguiHeap_->GetGPUDescriptorHandleForHeapStart());
}

void App::InitResources() {
    rootSig_.Init(ctx_.Device());
    psoCache_.Init(ctx_.Device(), rootSig_.Get(),
                    ctx_.RtvFormat(), ctx_.DepthFormat());
    heap_.Init(ctx_.Device(), 100000);
    gpuTimer_.Init(ctx_.Device(), ctx_.DirectQueue());
    debug_.Install(ctx_.Device());

    // FrameCB — Upload heap 영구 map.
    void* mapped = nullptr;
    frameCB_ = CreateBufferUpload(ctx_.Device(), sizeof(FrameCB) * kFramesInFlight,
                                    &mapped);
    frameCBMapped_ = (FrameCB*)mapped;

    // Scene mesh + objects 업로드 — 컨텍스트의 공용 command list 로 single-shot 제출.
    ctx_.BeginFrame();    // index 0 사용.
    scene_.Init(ctx_, ctx_.CmdList(), uploadKeepAlive_);
    ctx_.EndFrame(false);
}

void App::BuildFrameCB() {
    const FrameCB cb = MakeFrameCB(width_, height_, CameraSetup{},
                                    DefaultLightDir(),
                                    cpuTimer_.LastDeltaSeconds());
    UINT f = ctx_.CurrentFrame();
    std::memcpy(frameCBMapped_ + f, &cb, sizeof(FrameCB));
}

void App::Shutdown() {
    // Wait for GPU before destroying resources.
    // 마지막 프레임의 EndFrame 은 ExecuteCommandLists + Present + Signal 만 하고
    // 대기하지 않는다. Flush() 가 direct queue 에 새 fence 값을 Signal 하고 그
    // 값까지 기다리므로, 그 이후로는 아래에서 놓는 자원(ImGui 폰트 텍스처·프레임별
    // 정점/인덱스 버퍼·PSO·루트시그니처, query heap, readback 버퍼)을 GPU 가
    // 더 참조하지 않는다.
    ctx_.Flush();

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    // 디버그 콜백은 `this` 를 컨텍스트로 잡고 있으므로 디바이스 파괴 전에 해제한다.
    debug_.Uninstall();

    gpuTimer_.Shutdown();
    ctx_.Shutdown();
}

int App::Run() {
    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }
        OnFrame();
    }
    return (int)msg.wParam;
}

void App::OnFrame() {
    cpuTimer_.BeginFrame();

    // snippet-begin: scene-rebuild
    if (pendingCount_ != objectCount_) {
        objectCount_ = pendingCount_;
        // Instance upload buffer 를 교체할 수 있으므로 이전 frame-in-flight 사용이
        // 끝났음을 보장한 뒤 재빌드한다.
        ctx_.Flush();
        scene_.Rebuild(objectCount_, ctx_, ctx_.CmdList(), uploadKeepAlive_);
    }
    // snippet-end: scene-rebuild

    UINT f = ctx_.BeginFrame();
    BuildFrameCB();

    auto* cmd = ctx_.CmdList();

    // snippet-begin: barrier-entry
    // Barrier: BackBuffer Present -> RenderTarget.
    const size_t entryBarriers = RecordBackBufferBarriers(
        barriers_, cmd, ctx_.BackBuffer(f), barrierPolicy_, true);
    counters_.Reset();
    counters_.barrierCalls += (int)entryBarriers;
    // snippet-end: barrier-entry

    auto rtv = ctx_.BackBufferRtv(f);
    auto dsv = ctx_.DepthDsv();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    const float clearColor[] = {0.05f, 0.05f, 0.07f, 1.0f};
    cmd->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT vp{0, 0, (float)width_, (float)std::max(height_, 1), 0.0f, 1.0f};
    D3D12_RECT     sc{0, 0, (LONG)width_, (LONG)std::max(height_, 1)};
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    // snippet-begin: descriptor-workload
    // Descriptor lifetime workload. 최대 32K개의 null SRV를 frame-safe slice에
    // 다시 쓰거나 persistent table로 재사용한다. 셰이더는 이 표를 읽지
    // 않으므로 측정값은 CPU descriptor 생성 비용만 뜻한다.
    const UINT descriptorWork =
        static_cast<UINT>(std::min(objectCount_, 32768));
    counters_.descriptorWrites += static_cast<int>(
        heap_.PrepareNullSrvWorkload(ctx_.Device(), heapPolicy_, f,
                                     kFramesInFlight, descriptorWork));

    ID3D12DescriptorHeap* heaps[] = { heap_.SrvHeap() };
    cmd->SetDescriptorHeaps(1, heaps);
    counters_.descriptorHeapSets++;
    // snippet-end: descriptor-workload

    // snippet-begin: rootsig-bind
    // Root signature. 루트 인자는 반드시 *바인딩된 루트 시그니처 뒤* 에 와야 하고
    // CmdList::Reset 이 바인딩을 초기화하므로, 정책과 무관하게 프레임 시작 시
    // 한 번 설정한다. Per-Draw 정책은 Scene::Draw 가 드로우마다 같은 RS 를 다시
    // 설정하며, 동일 RS 재설정은 이미 기록한 루트 인자를 무효화하지 않는다.
    cmd->SetGraphicsRootSignature(rootSig_.Get());
    counters_.rootSigSets++;

    // Frame CBV.
    D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
        frameCB_->GetGPUVirtualAddress() + f * sizeof(FrameCB);
    cmd->SetGraphicsRootConstantBufferView(1, cbAddr);
    // snippet-end: rootsig-bind

    // snippet-begin: pso-per-pass
    // PSO. PerMaterial/PerDraw 정책은 Scene::Draw 안에서 의도적으로 재설정.
    auto* primaryPso =
        useBlinnPhong_ ? psoCache_.BlinnPhong() : psoCache_.Lambert();
    auto* secondaryPso =
        useBlinnPhong_ ? psoCache_.Lambert() : psoCache_.BlinnPhong();
    if (psoPolicy_ == PsoSwitchPolicy::PerPass) {
        cmd->SetPipelineState(primaryPso);
        counters_.pipelineSets++;
    }
    // snippet-end: pso-per-pass

    // snippet-begin: gpu-timer-record
    // GPU timer.
    gpuTimer_.Begin(cmd);

    // Scene draw.
    scene_.Draw(cmd, ctx_, drawMode_, rsPolicy_, psoPolicy_,
                rootSig_.Get(), primaryPso, secondaryPso,
                cpuTimer_.LastDeltaSeconds(), counters_);

    gpuTimer_.End(cmd);
    // snippet-end: gpu-timer-record

    // ImGui.
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    RenderUI();

    ID3D12DescriptorHeap* imguiHeaps[] = { imguiHeap_.Get() };
    cmd->SetDescriptorHeaps(1, imguiHeaps);
    counters_.descriptorHeapSets++;
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);

    // snippet-begin: barrier-exit
    // Barrier: RT -> Present.
    counters_.barrierCalls += (int)RecordBackBufferBarriers(
        barriers_, cmd, ctx_.BackBuffer(f), barrierPolicy_, false);
    // snippet-end: barrier-exit

    // Update scene physics (delta).
    scene_.Update(cpuTimer_.LastDeltaSeconds());

    if (vsync_ != prevVsync_) {
        prevVsync_ = vsync_;
    }

    // snippet-begin: frame-submit
    ctx_.EndFrame(vsync_);
    gpuTimer_.SignalSubmitted();
    gpuTimer_.Poll();
    cpuTimer_.EndFrame();
    // snippet-end: frame-submit
}

void App::RenderUI() {
    panel_->Draw(cpuTimer_, gpuTimer_, debug_, scene_, counters_,
                  drawMode_, psoPolicy_, rsPolicy_, heapPolicy_,
                  barrierPolicy_, useBlinnPhong_, pendingCount_,
                  vsync_);
}

}  // namespace dxlab
