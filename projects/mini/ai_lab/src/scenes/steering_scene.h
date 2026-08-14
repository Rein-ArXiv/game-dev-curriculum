#pragma once

#include "scenes/iscene.h"

#include "raylib.h"

#include <deque>

namespace ailab {

// Steering Scene — Seek / Arrive / Wander / Pursue 를 한 에이전트로 시연.
// 목표는 마우스 위치. 최근 궤적을 fade-out trail 로 그린다 (ai/part11).
class SteeringScene : public IScene {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void DrawWorld(int cx, int cy, int cw, int ch) override;
    void DrawPanel(int px, int py, int pw, int ph) override;
    std::string_view Name() const override { return "Steering"; }

private:
    Vector2 pos_{0, 0};
    Vector2 vel_{0, 0};
    float   wanderAngle_ = 0.0f;

    // pursue 용 가상 타깃 (원운동).
    Vector2 target_{0, 0};
    Vector2 targetVel_{0, 0};
    float   targetT_ = 0.0f;

    int   behaviorIdx_ = 0;   // 0 Seek, 1 Arrive, 2 Wander, 3 Pursue
    float maxSpeed_    = 260.0f;
    float maxForce_    = 520.0f;
    float slowRadius_  = 120.0f;
    bool  showVectors_ = true;

    Rectangle canvas_{0, 0, 0, 0};
    std::deque<Vector2> trail_;
};

}  // namespace ailab
