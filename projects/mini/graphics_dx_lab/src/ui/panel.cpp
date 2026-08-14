#include "ui/panel.h"

#include "measure/frame_timer.h"
#include "measure/timestamp_query.h"
#include "measure/d3d12_debug.h"

#include <imgui.h>
#include <implot.h>

#include <array>

namespace dxlab {

namespace {

template<int N>
std::array<float, N> Linearize(const std::array<float, N>& src, int head) {
    std::array<float, N> out{};
    for (int i = 0; i < N; ++i) out[(size_t)i] = src[(size_t)((head + i) % N)];
    return out;
}

const char* SevLabel(DebugMessage::Severity s) {
    switch (s) {
        case DebugMessage::Severity::Corruption: return "CORRUPT";
        case DebugMessage::Severity::High:       return "HIGH";
        case DebugMessage::Severity::Medium:     return "MEDIUM";
        case DebugMessage::Severity::Low:        return "LOW";
        case DebugMessage::Severity::Notification: return "NOTE";
    }
    return "?";
}

ImU32 SevColor(DebugMessage::Severity s) {
    switch (s) {
        case DebugMessage::Severity::Corruption: return IM_COL32(255, 50, 200, 255);
        case DebugMessage::Severity::High:       return IM_COL32(255, 80, 80, 255);
        case DebugMessage::Severity::Medium:     return IM_COL32(240, 180, 80, 255);
        case DebugMessage::Severity::Low:        return IM_COL32(200, 200, 80, 255);
        case DebugMessage::Severity::Notification: return IM_COL32(160, 160, 160, 255);
    }
    return IM_COL32(255, 255, 255, 255);
}

}  // namespace

void Panel::Draw(const FrameTimer& cpuTimer,
                  const TimestampQuery& gpuTimer,
                  const D3D12Debug& debug,
                  const Scene& scene,
                  const GlCounters& counters,
                  DrawMode& drawMode,
                  PsoSwitchPolicy& psoPolicy,
                  RootSigPolicy& rsPolicy,
                  HeapPolicy& heapPolicy,
                  BarrierPolicy& barrierPolicy,
                  bool& useBlinnPhong,
                  int& pendingObjectCount,
                  bool& vsync) {
    ImGui::Begin("graphics_dx_lab");

    ImGui::SeparatorText("Frame");
    ImGui::Text("CPU frame:  %.3f ms (ema %.3f)",
                cpuTimer.LastFrameMs(), cpuTimer.EmaFrameMs());
    ImGui::Text("GPU pass:   %.3f ms (ema %.3f)",
                gpuTimer.LastMs(), gpuTimer.EmaMs());
    ImGui::Text("fps:        %.1f", cpuTimer.Fps());

    auto cpuHist = Linearize<FrameTimer::kHistorySize>(
        cpuTimer.History(), cpuTimer.HistoryHead());
    auto gpuHist = Linearize<TimestampQuery::kHistorySize>(
        gpuTimer.History(), gpuTimer.HistoryHead());

    if (ImPlot::BeginPlot("ms history", ImVec2(-1, 140),
                           ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("frame", "ms",
                          ImPlotAxisFlags_AutoFit,
                          ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine("CPU", cpuHist.data(), FrameTimer::kHistorySize);
        ImPlot::PlotLine("GPU", gpuHist.data(), TimestampQuery::kHistorySize);
        ImPlot::EndPlot();
    }

    ImGui::SeparatorText("Scene");
    ImGui::Text("objects: %d", scene.ObjectCount());

    ImGui::SeparatorText("Counters (this frame)");
    // 제출 수는 헬퍼가 예측한 값과 반드시 같아야 한다. 어긋나면 기록 루프가
    // 계약을 깬 것이므로 UI 에서 바로 드러나게 한다(계약 테스트와 같은 식).
    {
        const int actualSubmissions = counters.drawCalls + counters.instancedDraws;
        const int expectedSubmissions =
            ExpectedDrawSubmissions(drawMode, scene.ObjectCount());
        ImGui::Text("draw submissions:%d (expected %d)%s",
                    actualSubmissions, expectedSubmissions,
                    actualSubmissions == expectedSubmissions ? "" : "  <-- MISMATCH");
    }
    ImGui::Text("draw calls:      %d", counters.drawCalls);
    ImGui::Text("instanced draws: %d", counters.instancedDraws);
    ImGui::Text("PSO sets:        %d", counters.pipelineSets);
    ImGui::Text("RootSig sets:    %d", counters.rootSigSets);
    ImGui::Text("Root constants:  %d", counters.rootConstantUpdates);
    ImGui::Text("Heap sets:       %d", counters.descriptorHeapSets);
    ImGui::Text("Descriptor writes:%d", counters.descriptorWrites);
    ImGui::Text("Barriers:        %d", counters.barrierCalls);

    ImGui::SeparatorText("Render mode");
    ImGui::Checkbox("vsync (정확 측정 시 OFF)", &vsync);
    ImGui::Checkbox("use Blinn-Phong PSO (default: Lambert)",
                    &useBlinnPhong);

    ImGui::SeparatorText("Draw mode (Step 3 + 7)");
    {
        int m = (int)drawMode;
        const char* names[] = { "Per-Draw (N root constants)", "Instanced (1 draw)" };
        if (ImGui::Combo("draw", &m, names, IM_ARRAYSIZE(names)))
            drawMode = (DrawMode)m;
    }

    ImGui::SeparatorText("PSO policy (Step 3)");
    {
        int p = (int)psoPolicy;
        const char* names[] = { "Per-Pass (1 set)", "Per-Material", "Per-Draw (worst)" };
        if (ImGui::Combo("pso", &p, names, IM_ARRAYSIZE(names)))
            psoPolicy = (PsoSwitchPolicy)p;
    }

    ImGui::SeparatorText("RootSig policy (Step 4)");
    {
        int p = (int)rsPolicy;
        const char* names[] = { "Global", "Per-Draw" };
        if (ImGui::Combo("rootsig", &p, names, IM_ARRAYSIZE(names)))
            rsPolicy = (RootSigPolicy)p;
    }

    ImGui::SeparatorText("Heap policy (Step 5)");
    {
        int p = (int)heapPolicy;
        const char* names[] = { "Transient rebuild", "Persistent table" };
        if (ImGui::Combo("heap", &p, names, IM_ARRAYSIZE(names)))
            heapPolicy = (HeapPolicy)p;
    }

    ImGui::SeparatorText("Barrier policy (Step 6)");
    {
        int p = (int)barrierPolicy;
        const char* names[] = { "Immediate", "Batched", "Split" };
        if (ImGui::Combo("barrier", &p, names, IM_ARRAYSIZE(names)))
            barrierPolicy = (BarrierPolicy)p;
    }

    ImGui::SeparatorText("Scale (Step 7)");
    {
        const char* countNames[] = { "1,000", "10,000", "100,000" };
        const int   counts[]     = { 1000, 10000, 100000 };
        int idx = 1;
        for (int i = 0; i < 3; ++i) if (counts[i] == pendingObjectCount) idx = i;
        if (ImGui::Combo("object count", &idx, countNames, IM_ARRAYSIZE(countNames)))
            pendingObjectCount = counts[idx];
        ImGui::TextDisabled("(다음 프레임 시작 시 Flush 로 GPU 완료를 보장한 뒤 Scene 재빌드)");
    }

    ImGui::SeparatorText("Step 진행");
    ImGui::TextWrapped("Step 1~7 완료. PSO/RootSig/Heap/Barrier 정책은 명령 호출 패턴과 카운터에 반영.");

    ImGui::End();

    // 디버그 로그 창.
    ImGui::Begin("D3D12 debug log");
    int n = debug.Count();
    int head = debug.RingHead();
    if (n == 0) {
        ImGui::TextDisabled("(no messages)");
    } else {
        for (int i = 0; i < n; ++i) {
            int idx = ((head - n + i) + D3D12Debug::kRingSize) % D3D12Debug::kRingSize;
            const auto& m = debug.Get(idx);
            ImGui::PushStyleColor(ImGuiCol_Text, SevColor(m.severity));
            ImGui::Text("[%s] id=%llu %s", SevLabel(m.severity),
                        (unsigned long long)m.id, m.text.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::End();
}

}  // namespace dxlab
