// Step 8: Camera Projection + Numerical Stability Scene.
//   Mode A — Depth precision: View Z 를 [-near, -far] 로 sweep 하며
//             Standard Z 와 Reverse-Z 의 NDC z 분포를 그래프로 비교.
//             near / far 슬라이더로 분포 변화 즉시 갱신.
//   Mode B — Kahan summation: N 개 작은 수를 누적하면서 naive 합 vs Kahan 의
//             오차 누적을 그래프로 비교.

#pragma once

#include "scenes/iscene.h"

namespace mathlab {

class ProjectionScene : public IScene {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) override;
    void DrawPanel(int panelX, int panelY, int panelW, int panelH) override;
    std::string_view Name() const override { return "Projection / Stability"; }

private:
    enum class Mode { DepthPrecision, KahanSum };

    Mode  mode_ = Mode::DepthPrecision;

    // Depth precision params
    float near_   = 0.1f;
    float far_    = 1000.0f;
    bool  showStandard_ = true;
    bool  showReverse_  = true;
    bool  logScale_     = false;

    // Kahan params
    int   kahanN_ = 100000;
    float kahanSmall_ = 0.0001f;
    float kahanBig_   = 1.0f;
};

}  // namespace mathlab
