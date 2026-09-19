#include "scenes/integrators_scene.h"
#include "core/physics_core.h"

#include "raygui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace physlab {

namespace {

constexpr int   kGraphCapacity = 600;     // 약 5 초 @ 1/120 dt
constexpr float kPxPerUnit     = 200.0f;  // 시뮬 단위 → 화면 픽셀

const char* kIntegratorNames[(int)IntegratorKind::COUNT] = {
    "Explicit Euler",
    "Semi-impl. Euler",
    "Velocity Verlet",
    "RK4",
};

const Color kIntegratorColors[(int)IntegratorKind::COUNT] = {
    {255, 100, 100, 255},   // Explicit — 빨강 (가장 빨리 발산)
    {120, 200, 255, 255},   // Semi-impl — 파랑
    {120, 230, 130, 255},   // Verlet — 초록
    {255, 200, 100, 255},   // RK4 — 노랑
};

const float kDtOptions[4] = {
    1.0f / 240.0f,
    1.0f / 120.0f,
    1.0f /  60.0f,
    1.0f /  30.0f,
};

const char* kDtLabels[4] = {
    "1/240",
    "1/120",
    "1/60",
    "1/30",
};

}  // namespace

IntegratorsScene::IntegratorsScene() {
    for (int i = 0; i < (int)IntegratorKind::COUNT; ++i) {
        osc_[i].color = kIntegratorColors[i];
        osc_[i].energyHist.Reset(kGraphCapacity);
        osc_[i].posHist.Reset(kGraphCapacity);
    }
    analyticalEnergy_.Reset(kGraphCapacity);
    analyticalPos_.Reset(kGraphCapacity);
}

void IntegratorsScene::OnEnter() {
    ResetAll();
}

void IntegratorsScene::ResetAll() {
    for (auto& o : osc_) {
        o.x = amplitude_;
        o.v = 0.0f;
        o.energyHist.Clear();
        o.posHist.Clear();
    }
    analyticalEnergy_.Clear();
    analyticalPos_.Clear();
    timeElapsed_   = 0.0f;
    internalAccum_ = 0.0f;
    E0_            = 0.5f * omega_ * omega_ * amplitude_ * amplitude_;
}

float IntegratorsScene::Energy(const Oscillator& o) const {
    return 0.5f * o.v * o.v + 0.5f * omega_ * omega_ * o.x * o.x;
}

void IntegratorsScene::StepOne(IntegratorKind kind,
                                  Oscillator& o,
                                  float dt) const {
    core::StepHarmonic(
        static_cast<core::HarmonicIntegrator>((int)kind),
        omega_, dt, o.x, o.v);
}

void IntegratorsScene::HandleInput() {
    if (IsKeyPressed(KEY_R)) ResetAll();
}

void IntegratorsScene::Step(float dt) {
    internalAccum_ += dt;
    int  guard = 0;
    while (internalAccum_ >= integDt_ && guard < 32) {
        for (int i = 0; i < (int)IntegratorKind::COUNT; ++i) {
            if (!osc_[i].enabled) continue;
            StepOne((IntegratorKind)i, osc_[i], integDt_);
            osc_[i].energyHist.Push(Energy(osc_[i]) / E0_);
            osc_[i].posHist.Push(osc_[i].x);
        }
        timeElapsed_ += integDt_;
        analyticalPos_.Push(amplitude_ * std::cos(omega_ * timeElapsed_));
        analyticalEnergy_.Push(1.0f);  // 정확히 보존되면 E / E0 = 1
        internalAccum_ -= integDt_;
        ++guard;
    }
}

void IntegratorsScene::DrawWorld(int cx, int cy, int cw, int ch) {
    DrawRectangle(cx, cy, cw, ch, Color{20, 22, 28, 255});
    DrawRectangleLines(cx, cy, cw, ch, Color{60, 62, 70, 255});

    // === 진동자 시각화 ===
    // 4 개 적분기 + 분석해 = 5 트랙. 각 트랙은 수평선 + 그 위를 x 값 따라 오가는 점.
    int   trackTop    = cy + 12;
    int   trackHeight = 36;
    int   trackInner  = 28;   // 마커 그리는 폭의 반(수직)
    int   trackXStart = cx + 60;
    int   trackXEnd   = cx + cw - 20;
    int   trackXMid   = (trackXStart + trackXEnd) / 2;
    int   trackXSpan  = (trackXEnd - trackXStart) / 2;  // 진폭이 화면 폭에 맞도록

    auto drawTrack = [&](int idx, const char* name, Color c, float xVal) {
        int ty = trackTop + idx * trackHeight + trackHeight / 2;
        DrawText(name, cx + 6, ty - 6, 12, Color{180, 184, 192, 255});
        DrawLine(trackXStart, ty, trackXEnd, ty, Color{60, 64, 72, 255});
        DrawCircle(trackXMid, ty, 2.0f, Color{100, 100, 110, 255});

        // 발산한 Explicit Euler 의 x 가 NaN 이면 clamp 가 NaN 을 그대로 돌려주고
        // 아래 (int) 변환이 UB — 유한하지 않으면 원점에 그린다.
        float t  = std::isfinite(xVal) ? std::clamp(xVal / amplitude_, -1.5f, 1.5f) : 0.0f;
        int   px = trackXMid + (int)(t * (float)trackXSpan);
        DrawLineEx(Vector2{(float)trackXMid, (float)ty},
                    Vector2{(float)px, (float)ty}, 2.0f, c);
        DrawCircle(px, ty, 7.0f, c);
        (void)trackInner;
    };

    drawTrack(0, kIntegratorNames[0], osc_[0].color, osc_[0].x);
    drawTrack(1, kIntegratorNames[1], osc_[1].color, osc_[1].x);
    drawTrack(2, kIntegratorNames[2], osc_[2].color, osc_[2].x);
    drawTrack(3, kIntegratorNames[3], osc_[3].color, osc_[3].x);
    drawTrack(4, "Analytical",
                Color{200, 200, 200, 200},
                amplitude_ * std::cos(omega_ * timeElapsed_));

    // === 에너지 그래프 ===
    int graphTop = trackTop + 5 * trackHeight + 16;
    Rectangle energyRect = {
        (float)(cx + 10),
        (float)graphTop,
        (float)(cw - 20),
        (float)(cy + ch - graphTop - 12),
    };
    GraphInput inputs[5] = {
        {&osc_[0].energyHist, osc_[0].color, kIntegratorNames[0]},
        {&osc_[1].energyHist, osc_[1].color, kIntegratorNames[1]},
        {&osc_[2].energyHist, osc_[2].color, kIntegratorNames[2]},
        {&osc_[3].energyHist, osc_[3].color, kIntegratorNames[3]},
        {&analyticalEnergy_,  Color{180,180,180,180}, "E_0 (ideal)"},
    };
    DrawLineGraph(energyRect, inputs, 5,
                   "Energy / E_0 (1.0 = ideal conservation)",
                   0.5f, 1.5f, true);
}

void IntegratorsScene::DrawPanel(int panelX, int panelY, int panelW, int panelH) {
    (void)panelW; (void)panelH;
    int  y = panelY + 8;
    char buf[160];

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "Integrators — SHO comparison");
    y += 24;

    // 적분기 4 개 토글.
    for (int i = 0; i < (int)IntegratorKind::COUNT; ++i) {
        bool b = osc_[i].enabled;
        DrawRectangle(panelX + 8, y + 3, 12, 12, kIntegratorColors[i]);
        GuiCheckBox(Rectangle{(float)(panelX + 26), (float)y, 16, 16},
                     kIntegratorNames[i], &b);
        osc_[i].enabled = b;
        y += 22;
    }
    y += 8;

    // dt 토글 (4 옵션). 클릭으로 선택.
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "Integration dt:");
    y += 20;
    for (int i = 0; i < 4; ++i) {
        Rectangle r = {(float)(panelX + 8 + i * 60), (float)y, 56, 22};
        bool selected = (dtSliderIdx_ == i);
        if (selected) {
            DrawRectangleRec(r, Color{60, 100, 160, 255});
        }
        if (GuiButton(r, kDtLabels[i])) {
            dtSliderIdx_ = i;
            integDt_     = kDtOptions[i];
            ResetAll();
        }
    }
    y += 28;

    // omega, amplitude 슬라이더.
    std::snprintf(buf, sizeof(buf), "omega: %.2f rad/s", omega_);
    float omegaPrev = omega_;
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &omega_, 1.0f, 20.0f);
    y += 22;

    std::snprintf(buf, sizeof(buf), "amplitude: %.2f", amplitude_);
    float ampPrev = amplitude_;
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &amplitude_, 0.1f, 3.0f);
    y += 22;

    if (omegaPrev != omega_ || ampPrev != amplitude_) {
        ResetAll();
    }
    y += 4;

    if (GuiButton(Rectangle{(float)(panelX + 8), (float)y, 100, 22}, "Reset (R)")) {
        ResetAll();
    }
    y += 28;

    // 현재 상태 라벨.
    std::snprintf(buf, sizeof(buf), "time: %.2f s", timeElapsed_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf);
    y += 18;

    for (int i = 0; i < (int)IntegratorKind::COUNT; ++i) {
        float E   = Energy(osc_[i]);
        float r   = E / E0_;
        std::snprintf(buf, sizeof(buf), "%s  E/E0=%.3f",
                       kIntegratorNames[i], r);
        DrawRectangle(panelX + 8, y + 3, 8, 12, kIntegratorColors[i]);
        GuiLabel(Rectangle{(float)(panelX + 22), (float)y, 240, 18}, buf);
        y += 18;
    }
    y += 8;

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "Try dt=1/30 with Explicit:");
    y += 16;
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "the red curve diverges.");
    y += 16;
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "Verlet / RK4 stay near 1.0.");
}

}  // namespace physlab
