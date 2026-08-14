#include "ui/panel.h"

#include "measure/frame_timer.h"
#include "measure/timestamp_query.h"
#include "measure/validation_log.h"

#include <imgui.h>
#include <implot.h>

namespace vklab {

void Panel::Draw(const PanelIO& io) {
    ImGui::Begin("graphics_vk_lab");

    // ── 지표 ──────────────────────────────────────────────────────────
    ImGui::Text("FPS %.0f   CPU %.2f ms   GPU %.2f ms",
                io.cpuTimer->Fps(), io.cpuTimer->EmaFrameMs(), io.gpuTimer->EmaMs());

    if (ImPlot::BeginPlot("##frametime", ImVec2(-1, 120))) {
        ImPlot::SetupAxes(nullptr, "ms",
                          ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, FrameTimer::kHistorySize, ImPlotCond_Always);
        const auto& ch = io.cpuTimer->History();
        ImPlot::PlotLine("CPU ms", ch.data(), (int)ch.size(), 1.0, 0.0,
                         0, io.cpuTimer->HistoryHead());
        const auto& gh = io.gpuTimer->History();
        ImPlot::PlotLine("GPU ms", gh.data(), (int)gh.size(), 1.0, 0.0,
                         0, io.gpuTimer->HistoryHead());
        ImPlot::EndPlot();
    }

    ImGui::Separator();

    // ── 토글 ──────────────────────────────────────────────────────────
    int dm = (*io.drawMode == DrawMode::Instanced) ? 1 : 0;
    if (ImGui::Combo("Draw mode", &dm, "Per-Draw\0Instanced\0"))
        *io.drawMode = dm ? DrawMode::Instanced : DrawMode::PerDraw;
    ImGui::TextDisabled("%s", DrawModeName(*io.drawMode));

    ImGui::Checkbox("Blinn-Phong (off = Lambert)", io.useBlinnPhong);

    int bp = (*io.barrierPolicy == BarrierPolicy::Minimal) ? 1 : 0;
    if (ImGui::Combo("Barrier policy", &bp, "Over-sync\0Minimal\0"))
        *io.barrierPolicy = bp ? BarrierPolicy::Minimal : BarrierPolicy::OverSync;
    ImGui::TextDisabled("%s", BarrierPolicyName(*io.barrierPolicy));

    // 실패 주입 — 켜면 렌더 결과가 깨지고 Debug 빌드의 Validation log 에
    // 오류가 쌓인다. 정상 상태를 보려면 둘 다 꺼 둔다.
    if (io.injection) {
        ImGui::Checkbox("Inject: skip barriers", &io.injection->skipBarriers);
        ImGui::Checkbox("Inject: wrong srcAccessMask", &io.injection->wrongSrcAccess);
        if (io.injection->skipBarriers || io.injection->wrongSrcAccess) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "failure injection ON — validation errors expected");
        }
    }

    int sm = (*io.syncMode == SyncMode::Timeline) ? 1 : 0;
    if (ImGui::Combo("Frame sync", &sm, "Binary fence\0Timeline\0"))
        *io.syncMode = sm ? SyncMode::Timeline : SyncMode::Binary;
    if (*io.syncMode == SyncMode::Timeline) {
        ImGui::TextDisabled("%s   (value = %llu)",
                            SyncModeName(*io.syncMode),
                            (unsigned long long)io.timelineValue);
    } else {
        ImGui::TextDisabled("%s", SyncModeName(*io.syncMode));
    }

    ImGui::Checkbox("VSync (FIFO / off = Mailbox)", io.vsync);

    const int counts[] = {1000, 10000, 100000};
    int ci = 1;
    for (int i = 0; i < 3; ++i) if (counts[i] == io.objectCount) ci = i;
    if (ImGui::Combo("Object count", &ci, "1k\0" "10k\0" "100k\0"))
        *io.pendingCount = counts[ci];
    ImGui::Text("Active objects: %d", io.objectCount);

    ImGui::Separator();

    // ── 카운터 ────────────────────────────────────────────────────────
    const GlCounters& c = *io.counters;
    ImGui::Text("draws %d   instanced %d", c.drawCalls, c.instancedDraws);
    ImGui::Text("pipeline binds %d   barriers %d   push %d",
                c.pipelineBinds, c.barrierCalls, c.pushConstants);

    ImGui::Separator();

    // ── Validation log ────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Validation log")) {
        auto& log = ValidationLog::Instance();
        int n = log.Count();
        int head = log.RingHead();
        ImGui::BeginChild("##vallog", ImVec2(0, 120), true);
        for (int i = 0; i < n; ++i) {
            int idx = (head - 1 - i + ValidationLog::kRingSize * 2) % ValidationLog::kRingSize;
            const ValMsg& m = log.Get(idx);
            ImVec4 col = (m.sev == ValMsg::Sev::Error)   ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                       : (m.sev == ValMsg::Sev::Warning) ? ImVec4(1.0f, 0.85f, 0.4f, 1.0f)
                       :                                   ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
            ImGui::TextColored(col, "%s", m.text.c_str());
        }
        ImGui::EndChild();
    }

    ImGui::End();
}

}  // namespace vklab
