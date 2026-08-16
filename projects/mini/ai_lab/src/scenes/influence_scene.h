#pragma once

#include "grid/grid.h"
#include "scenes/iscene.h"

#include "raylib.h"

#include <vector>

namespace ailab {

// Influence Map Scene — decay -> stamp -> spread 루프와 히트맵.
// 좌클릭=아군 유닛, 우클릭=적 유닛. control(아군-적) 을 색으로 본다 (ai/part09).
class InfluenceScene : public IScene {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void DrawWorld(int cx, int cy, int cw, int ch) override;
    void DrawPanel(int px, int py, int pw, int ph) override;
    std::string_view Name() const override { return "Influence"; }

private:
    void Step();   // decay -> stamp -> spread 한 번

    Grid grid_;
    std::vector<Cell>  friendly_;
    std::vector<Cell>  enemy_;
    std::vector<float> fField_;   // 아군 영향력
    std::vector<float> eField_;   // 적 영향력

    int   gridDim_   = 40;
    float decay_     = 0.92f;   // momentum (1=영구 기억)
    float spread_    = 0.62f;   // 확산 감쇠율
    int   iters_     = 2;       // influence tick당 spread 반복
    int   viewMode_  = 0;       // 0 control, 1 tension, 2 vulnerability
    double stepAccumulator_ = 0.0;
};

}  // namespace ailab
