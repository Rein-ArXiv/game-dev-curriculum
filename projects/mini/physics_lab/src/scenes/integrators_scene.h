// Step 2 — 적분기 4 종 비교. 같은 단진동 시스템을 Explicit Euler / Semi-implicit
// Euler / Verlet (velocity) / RK4 가 동시에 시뮬레이션. 분석해 (sine) 와의 차이
// + 에너지 보존 그래프로 *어떤 적분기가 어떤 dt 에서 발산 / 수렴* 하는지 본다.
//
// 시스템: 단순 조화 진동자 (Simple Harmonic Oscillator)
//   m * x'' = -k * x      (단위 mass = 1, 따라서 a = -k * x = -ω² * x)
//   분석해: x(t) = A cos(ωt + φ)
//   총 에너지: E = (1/2) v² + (1/2) ω² x²   = const (분석해)
// 적분기 비교에서 가장 깨끗한 벤치마크 — 분석해와 에너지 보존 모두 알려져 있음.

#pragma once

#include "scenes/iscene.h"
#include "ui/line_graph.h"

#include "raylib.h"

namespace physlab {

enum class IntegratorKind {
    ExplicitEuler  = 0,
    SemiImplicit   = 1,
    Verlet         = 2,
    RK4            = 3,
    COUNT          = 4,
};

struct Oscillator {
    float        x       = 1.0f;       // 위치
    float        v       = 0.0f;       // 속도
    bool         enabled = true;
    Color        color   = WHITE;
    GraphSeries  energyHist;
    GraphSeries  posHist;
};

class IntegratorsScene : public IScene {
public:
    IntegratorsScene();

    void OnEnter() override;
    void HandleInput() override;
    void Step(float dt) override;
    void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) override;
    void DrawPanel(int panelX, int panelY, int panelW, int panelH) override;
    std::string_view Name() const override { return "Integrators"; }

private:
    void ResetAll();
    void StepOne(IntegratorKind kind, Oscillator& o, float dt) const;
    float Energy(const Oscillator& o) const;

    Oscillator    osc_[(int)IntegratorKind::COUNT];
    GraphSeries   analyticalEnergy_;
    GraphSeries   analyticalPos_;

    // 시스템 파라미터.
    float amplitude_ = 1.0f;             // 초기 진폭 (단위 없음, m 으로 생각)
    float omega_     = 6.2831853f;       // 2π → 1 Hz
    float integDt_   = 1.0f / 120.0f;    // 적분기에 들어가는 dt

    // 누적기 — main 의 fixed dt 와 integDt_ 가 다를 수 있다.
    float internalAccum_ = 0.0f;
    float timeElapsed_   = 0.0f;
    float E0_            = 0.5f;         // 초기 에너지

    int dtSliderIdx_ = 1;   // 0: 1/240, 1: 1/120, 2: 1/60, 3: 1/30
};

}  // namespace physlab
