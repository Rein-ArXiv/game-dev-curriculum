// Step 3: Interpolation Scene.
//   곡선 종류 토글 (Bezier / Catmull-Rom / Hermite)
//   + 4 control point 를 마우스로 드래그
//   + arc-length parameterization 토글 (등속 vs 매개변수 등속)
//   + dot 이 곡선 위를 t ∈ [0, 1] 로 이동.

#pragma once

#include "scenes/iscene.h"
#include "ui/handle.h"

#include "raylib.h"

namespace mathlab {

class InterpolationScene : public IScene {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) override;
    void DrawPanel(int panelX, int panelY, int panelW, int panelH) override;
    std::string_view Name() const override { return "Interpolation"; }

private:
    enum class Curve { Bezier, CatmullRom, Hermite };

    Curve      curve_     = Curve::Bezier;
    DragHandle p0_, p1_, p2_, p3_;
    bool       initialized_ = false;
    float      tParam_   = 0.0f;
    bool       autoPlay_ = true;
    bool       useArcLen_ = false;
    int        sampleCount_ = 64;

    // Hermite 모드의 접선 표시는 P1 - P0, P3 - P2 로 자동.
    Vector2 SampleCurve(float t) const;
};

}  // namespace mathlab
