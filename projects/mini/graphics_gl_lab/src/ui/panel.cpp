#include "ui/panel.h"

#include "measure/frame_timer.h"
#include "measure/gpu_timer.h"
#include "measure/khr_debug.h"
#include "measure/gl_counters.h"
#include "render/texture_modes.h"
#include "render/msaa_target.h"
#include "scene/scene.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <array>

namespace gllab {

namespace {

template<int N>
std::array<float, N> Linearize(const std::array<float, N>& src, int head) {
    std::array<float, N> out{};
    for (int i = 0; i < N; ++i) {
        out[(size_t)i] = src[(size_t)((head + i) % N)];
    }
    return out;
}

const char* SeverityLabel(DebugMessage::Severity s) {
    switch (s) {
        case DebugMessage::Severity::High:         return "HIGH";
        case DebugMessage::Severity::Medium:       return "MEDIUM";
        case DebugMessage::Severity::Low:          return "LOW";
        case DebugMessage::Severity::Notification: return "NOTE";
    }
    return "?";
}

ImU32 SeverityColor(DebugMessage::Severity s) {
    switch (s) {
        case DebugMessage::Severity::High:         return IM_COL32(255, 80, 80, 255);
        case DebugMessage::Severity::Medium:       return IM_COL32(240, 180, 80, 255);
        case DebugMessage::Severity::Low:          return IM_COL32(200, 200, 80, 255);
        case DebugMessage::Severity::Notification: return IM_COL32(160, 160, 160, 255);
    }
    return IM_COL32(255, 255, 255, 255);
}

}  // namespace

void Panel::Draw(const FrameTimer& cpuTimer,
                  const GpuTimer&   gpuTimer,
                  const KHRDebug&   khrDebug,
                  Scene&            scene,
                  const GlCounters& counters,
                  ShaderStyle&      style,
                  ShadingModel&     model,
                  bool&             useLighting,
                  BindingMode&      bindingMode,
                  int&              lightCount,
                  TextureMode&      textureMode,
                  const TextureRegistry& textures,
                  int&              pendingObjectCount,
                  MsaaSamples&      msaa,
                  bool& wireframe,
                  bool& vsyncOn) {
    ImGui::Begin("graphics_gl_lab");

    ImGui::SeparatorText("Frame");
    ImGui::Text("CPU frame:  %.3f ms (ema %.3f)",
                cpuTimer.LastFrameMs(), cpuTimer.EmaFrameMs());
    ImGui::Text("GPU pass:   %.3f ms (ema %.3f)",
                gpuTimer.LastMs(), gpuTimer.EmaMs());
    ImGui::Text("fps:        %.1f", cpuTimer.Fps());

    auto cpuHist = Linearize<FrameTimer::kHistorySize>(
        cpuTimer.History(), cpuTimer.HistoryHead());
    auto gpuHist = Linearize<GpuTimer::kHistorySize>(
        gpuTimer.History(), gpuTimer.HistoryHead());

    if (ImPlot::BeginPlot("ms history", ImVec2(-1, 140),
                           ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("frame", "ms",
                          ImPlotAxisFlags_AutoFit,
                          ImPlotAxisFlags_AutoFit);
        // snippet-begin: ms-plot
        ImPlot::PlotLine("CPU", cpuHist.data(), FrameTimer::kHistorySize);
        ImPlot::PlotLine("GPU", gpuHist.data(), GpuTimer::kHistorySize);
        // snippet-end: ms-plot
        ImPlot::EndPlot();
    }

    ImGui::SeparatorText("Scene");
    ImGui::Text("objects: %d", scene.ObjectCount());

    ImGui::SeparatorText("GL counters (this frame)");
    ImGui::Text("draw calls:       %d", counters.drawCalls);
    ImGui::Text("instanced draws:  %d", counters.instancedDraws);
    ImGui::Text("indirect draws:   %d", counters.indirectDraws);
    ImGui::Text("state changes:    %d", counters.stateChanges);
    ImGui::Text("uniform updates:  %d", counters.uniformUpdates);
    ImGui::Text("buffer updates:   %d", counters.bufferUpdates);

    ImGui::SeparatorText("Render mode");
    ImGui::Checkbox("wireframe", &wireframe);
    ImGui::Checkbox("vsync (정확 측정 시 OFF)", &vsyncOn);

    ImGui::SeparatorText("Draw mode (Step 3)");
    {
        int modeInt = (int)scene.GetDrawMode();
        const char* names[] = {
            "Individual (N draws)",
            "Instanced (1 draw)",
            "Multi-Draw Indirect (1 draw)",
        };
        if (ImGui::Combo("##drawmode", &modeInt, names, IM_ARRAYSIZE(names))) {
            scene.SetDrawMode((DrawMode)modeInt);
        }
    }

    ImGui::SeparatorText("Shader (Step 4)");
    {
        int styleInt = (int)style;
        const char* sNames[] = {
            "Uber (runtime branch)",
            "Static permutation (per model)",
        };
        if (ImGui::Combo("style", &styleInt, sNames, IM_ARRAYSIZE(sNames))) {
            style = (ShaderStyle)styleInt;
        }

        int modelInt = (int)model;
        const char* mNames[] = { "Lambert", "Blinn-Phong", "Cel" };
        if (ImGui::Combo("model", &modelInt, mNames, IM_ARRAYSIZE(mNames))) {
            model = (ShadingModel)modelInt;
        }
        ImGui::TextWrapped("Uber 는 runtime branch와 프로그램 수를 교환한다. "
                           "실제 우열은 branch coherence, 셰이더 크기, 프로그램 "
                           "전환 패턴을 함께 측정해 판단한다.");
    }

    ImGui::SeparatorText("Lighting binding (Step 5)");
    ImGui::Checkbox("use multi-light shader", &useLighting);
    if (useLighting) {
        int bmInt = (int)bindingMode;
        const char* bmNames[] = {
            "per-call Uniform (N glUniform)",
            "UBO (std140)",
            "SSBO (std430)",
        };
        if (ImGui::Combo("binding", &bmInt, bmNames, IM_ARRAYSIZE(bmNames))) {
            bindingMode = (BindingMode)bmInt;
        }
        ImGui::SliderInt("light count", &lightCount, 1, 64);
        ImGui::TextWrapped("per-call 은 광원 × 2 uniform 호출 매 프레임. "
                           "UBO/SSBO 는 한 번 buffer 갱신.");
    } else {
        ImGui::TextDisabled("(Step 4 단일 광원 셰이더 사용 중)");
    }

    ImGui::SeparatorText("Texture compression (Step 6)");
    {
        int tmInt = (int)textureMode;
        const char* tmNames[] = {
            "None",
            "RGBA8 (uncompressed)",
            "DXT1 / BC1",
            "DXT5 / BC3",
            "BC7",
        };
        // snippet-begin: texture-combo
        if (ImGui::Combo("texture", &tmInt, tmNames, IM_ARRAYSIZE(tmNames))) {
            const auto requested = (TextureMode)tmInt;
            if (textures.IsSupported(requested))
                textureMode = requested;
        }
        // snippet-end: texture-combo
        ImGui::Text("RGBA8:  %d bytes", textures.BytesFor(TextureMode::Uncompressed));
        auto showCompressed = [&](const char* name, TextureMode mode) {
            if (!textures.IsSupported(mode)) {
                ImGui::Text("%s: unsupported", name);
                return;
            }
            const int bytes = textures.BytesFor(mode);
            ImGui::Text("%s: %d bytes (%.1fx)", name, bytes,
                        (float)textures.BytesFor(TextureMode::Uncompressed) /
                        (float)bytes);
        };
        showCompressed("DXT1", TextureMode::DXT1);
        showCompressed("DXT5", TextureMode::DXT5);
        showCompressed("BC7 ", TextureMode::BC7);
        ImGui::TextWrapped("압축은 드라이버의 온라인 인코더를 사용한다. "
                           "크기는 비교할 수 있지만 품질 벤치마크는 아니다.");
    }

    ImGui::SeparatorText("Scale + MSAA (Step 7)");
    {
        const char* countNames[] = { "100", "1,000", "10,000", "100,000" };
        const int   counts[]     = { 100,   1000,    10000,    100000   };
        int idx = 2;
        for (int i = 0; i < 4; ++i) if (counts[i] == pendingObjectCount) idx = i;
        if (ImGui::Combo("object count", &idx, countNames, IM_ARRAYSIZE(countNames))) {
            pendingObjectCount = counts[idx];
        }
        ImGui::TextDisabled("(다음 프레임 시작 시 Scene 재빌드)");

        int msaaInt = (int)msaa;
        const char* msaaNames[] = { "Off", "2x", "4x", "8x" };
        const int   msaaVals[]  = { 0, 2, 4, 8 };
        int msaaIdx = 0;
        for (int i = 0; i < 4; ++i) if (msaaVals[i] == msaaInt) msaaIdx = i;
        if (ImGui::Combo("MSAA", &msaaIdx, msaaNames, IM_ARRAYSIZE(msaaNames))) {
            msaa = (MsaaSamples)msaaVals[msaaIdx];
        }
        ImGui::TextWrapped("MSAA on 시 multisample FBO와 resolve가 추가된다. "
                           "비용은 sample 수뿐 아니라 coverage, shading 방식, "
                           "대역폭, GPU 구조에 좌우된다.");
    }

    ImGui::SeparatorText("Step 진행");
    ImGui::TextWrapped("Step 1~7 완료. graphics_gl_lab 전체 토글 갖춤.");

    ImGui::End();

    // 별도 창: GL 디버그 로그.
    ImGui::Begin("GL debug log");
    int n = khrDebug.Count();
    int head = khrDebug.RingHead();
    if (n == 0) {
        ImGui::TextDisabled("(no messages)");
    } else {
        for (int i = 0; i < n; ++i) {
            int idx = ((head - n + i) + KHRDebug::kRingSize) % KHRDebug::kRingSize;
            const auto& m = khrDebug.Get(idx);
            ImGui::PushStyleColor(ImGuiCol_Text, SeverityColor(m.severity));
            ImGui::Text("[%s] id=%u %s", SeverityLabel(m.severity), m.id,
                        m.text.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::End();
}

}  // namespace gllab
