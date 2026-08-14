#include "ui/hud.h"

#include "imgui.h"
#include "ctx/ctx_type.h"
#include "io/io_backend.h"
#include "io/io_backend_factory.h"
#include "measure/frame_timer.h"
#include "sched/sched_policy.h"
#include "sync/sync_factory.h"
#include "ui/toggles.h"
#include "workload/file_io_workload.h"

#include <cfloat>
#include <cstdio>

namespace skl {

void Hud::Init(int windowW, int windowH) {
    windowW_ = windowW;
    windowH_ = windowH;
}

namespace {

// 슬라이딩 윈도우 ring buffer를 ImGui::PlotLines가 받는 *연속 배열* 로 풀어낸다.
// PlotLines는 [base + offset .. wrap .. base] 형태를 직접 처리하지 못해
// head 기준으로 *오래된 → 최신* 순서의 임시 배열을 만들어 넘긴다.
void UnwrapRing(const float* src, std::size_t cap,
                std::size_t head, std::size_t filled,
                float* dst) {
    for (std::size_t i = 0; i < filled; ++i) {
        const std::size_t idx = (head + cap - filled + i) % cap;
        dst[i] = src[idx];
    }
}

}  // namespace

void Hud::Draw(Toggles& toggles,
                const FrameTimer& timer,
                const FileIoWorkload& workload) {
    // HUD 윈도우는 화면 하단에 고정 영역으로 배치.
    constexpr float kPanelH = 240.0f;
    ImGui::SetNextWindowPos(ImVec2(0.0f, static_cast<float>(windowH_) - kPanelH));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowW_), kPanelH));
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("hud", nullptr, flags);

    // 좌: 핵심 수치.
    ImGui::BeginChild("stats", ImVec2(340, 0), false);
    {
        const int backendIdx = static_cast<int>(workload.Backend());
        const int syncIdx    = static_cast<int>(workload.Sync());
        const int schedIdx   = static_cast<int>(workload.Sched());
        const int ctxIdx     = static_cast<int>(workload.Ctx());
        const char* backendName =
            (backendIdx >= 0 && backendIdx < kBackendTypeCount)
            ? kBackendTypeLabels[backendIdx] : "?";
        const char* syncName =
            (syncIdx >= 0 && syncIdx < kSyncTypeCount)
            ? kSyncTypeLabels[syncIdx] : "?";
        const char* schedName =
            (schedIdx >= 0 && schedIdx < kSchedTypeCount)
            ? kSchedTypeLabels[schedIdx] : "?";
        const char* ctxName =
            (ctxIdx >= 0 && ctxIdx < kCtxTypeCount)
            ? kCtxTypeLabels[ctxIdx] : "?";

        char buf[1024];
        std::snprintf(buf, sizeof(buf),
                      "backend     : %s\n"
                      "sync        : %s\n"
                      "sched req   : %s\n"
                      "sched apply : %s\n"
                      "ctx         : %s\n"
                      "workers     : %zu\n"
                      "files       : %zu  x  %.2f MB\n"
                      "cycle ms    : %7.3f\n"
                      "throughput  : %7.1f MB/s\n"
                      "ctx-sw/cyc  : %7.0f\n"
                      "faults/cyc  : %7.0f\n"
                      "total cycles: %llu\n"
                      "total MB    : %.1f\n"
                      "fixture dir : %s",
                      backendName,
                      syncName,
                      schedName,
                      workload.SchedApplied() ? "yes" : "NO (see stderr / N/A)",
                      ctxName,
                      workload.WorkerCount(),
                      workload.FileCount(),
                      static_cast<double>(workload.FileBytes()) / 1.0e6,
                      timer.LastCycleMs(),
                      timer.LastThroughputMBs(),
                      timer.LastCtxSwitches(),
                      timer.LastPageFaults(),
                      static_cast<unsigned long long>(timer.TotalCycles()),
                      static_cast<double>(timer.TotalBytes()) / 1.0e6,
                      workload.FixtureDir().c_str());
        ImGui::TextUnformatted(buf);
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // 중: 토글 패널.
    ImGui::BeginChild("toggles", ImVec2(260, 0), false);
    {
        ImGui::TextUnformatted("toggles");
        ImGui::Separator();

        // backend 콤보.
        const int curBackendIdx = static_cast<int>(toggles.backend);
        const char* backendPrev =
            (curBackendIdx >= 0 && curBackendIdx < kBackendTypeCount)
            ? kBackendTypeLabels[curBackendIdx] : "(none)";
        if (ImGui::BeginCombo("backend", backendPrev)) {
            for (int n = 0; n < kBackendTypeCount; ++n) {
                const auto t = static_cast<BackendType>(n);
                const bool compiled = BackendCompiledIn(t);
                ImGui::BeginDisabled(!compiled);
                const bool selected = (curBackendIdx == n);
                if (ImGui::Selectable(kBackendTypeLabels[n], selected)) {
                    toggles.backend = t;
                }
                if (selected) ImGui::SetItemDefaultFocus();
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }

        // sync 콤보.
        const int curSyncIdx = static_cast<int>(toggles.sync);
        const char* syncPrev =
            (curSyncIdx >= 0 && curSyncIdx < kSyncTypeCount)
            ? kSyncTypeLabels[curSyncIdx] : "(none)";
        if (ImGui::BeginCombo("sync", syncPrev)) {
            for (int n = 0; n < kSyncTypeCount; ++n) {
                const bool selected = (curSyncIdx == n);
                if (ImGui::Selectable(kSyncTypeLabels[n], selected)) {
                    toggles.sync = static_cast<SyncType>(n);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // sched 콤보.
        const int curSchedIdx = static_cast<int>(toggles.sched);
        const char* schedPrev =
            (curSchedIdx >= 0 && curSchedIdx < kSchedTypeCount)
            ? kSchedTypeLabels[curSchedIdx] : "(none)";
        if (ImGui::BeginCombo("sched", schedPrev)) {
            for (int n = 0; n < kSchedTypeCount; ++n) {
                const bool selected = (curSchedIdx == n);
                if (ImGui::Selectable(kSchedTypeLabels[n], selected)) {
                    toggles.sched = static_cast<SchedType>(n);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // ctx 콤보.
        const int curCtxIdx = static_cast<int>(toggles.ctx);
        const char* ctxPrev =
            (curCtxIdx >= 0 && curCtxIdx < kCtxTypeCount)
            ? kCtxTypeLabels[curCtxIdx] : "(none)";
        if (ImGui::BeginCombo("ctx", ctxPrev)) {
            for (int n = 0; n < kCtxTypeCount; ++n) {
                const bool selected = (curCtxIdx == n);
                if (ImGui::Selectable(kCtxTypeLabels[n], selected)) {
                    toggles.ctx = static_cast<CtxType>(n);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // workers 콤보.
        const int curWIdx = toggles.workerIdx;
        const char* wPrev =
            (curWIdx >= 0 && curWIdx < kWorkerCountPresetCount)
            ? kWorkerCountPresets[curWIdx].label : "?";
        if (ImGui::BeginCombo("workers", wPrev)) {
            for (int n = 0; n < kWorkerCountPresetCount; ++n) {
                const bool selected = (curWIdx == n);
                if (ImGui::Selectable(kWorkerCountPresets[n].label,
                                        selected)) {
                    toggles.workerIdx = n;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // 우: 네 그래프 (cycle ms, throughput, ctx-sw, page-faults).
    ImGui::BeginChild("graphs", ImVec2(0, 0), false);
    {
        const std::size_t filled = timer.Filled();
        if (filled > 0) {
            constexpr std::size_t cap = FrameTimer::kCapacity;
            const std::size_t head = timer.Head();

            float msSeries[cap];
            float tpSeries[cap];
            float csSeries[cap];
            float pfSeries[cap];
            UnwrapRing(timer.CycleMs().data(),       cap, head, filled, msSeries);
            UnwrapRing(timer.ThroughputMBs().data(), cap, head, filled, tpSeries);
            UnwrapRing(timer.CtxSwitches().data(),   cap, head, filled, csSeries);
            UnwrapRing(timer.PageFaults().data(),    cap, head, filled, pfSeries);

            char overlay[64];
            std::snprintf(overlay, sizeof(overlay),
                          "%.3f ms", timer.LastCycleMs());
            ImGui::PlotLines("cycle ms", msSeries,
                             static_cast<int>(filled), 0, overlay,
                             0.0f, FLT_MAX, ImVec2(0, 38));

            std::snprintf(overlay, sizeof(overlay),
                          "%.1f MB/s", timer.LastThroughputMBs());
            ImGui::PlotLines("throughput", tpSeries,
                             static_cast<int>(filled), 0, overlay,
                             0.0f, FLT_MAX, ImVec2(0, 38));

            std::snprintf(overlay, sizeof(overlay),
                          "%.0f / cycle", timer.LastCtxSwitches());
            ImGui::PlotLines("ctx switches", csSeries,
                             static_cast<int>(filled), 0, overlay,
                             0.0f, FLT_MAX, ImVec2(0, 38));

            std::snprintf(overlay, sizeof(overlay),
                          "%.0f / cycle", timer.LastPageFaults());
            ImGui::PlotLines("page faults", pfSeries,
                             static_cast<int>(filled), 0, overlay,
                             0.0f, FLT_MAX, ImVec2(0, 38));
        } else {
            ImGui::TextUnformatted("(collecting samples...)");
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

}  // namespace skl
