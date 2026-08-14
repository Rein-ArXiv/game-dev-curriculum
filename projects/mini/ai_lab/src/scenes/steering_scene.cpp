#include "scenes/steering_scene.h"

#include "model/steering.h"
#include "raygui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ailab {

namespace {
constexpr size_t kTrailMax = 90;

Vector2 Sub(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
Vector2 Add(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
Vector2 Mul(Vector2 a, float s)   { return {a.x * s, a.y * s}; }
float   Len(Vector2 v)            { return std::sqrt(v.x * v.x + v.y * v.y); }
Vector2 Norm(Vector2 v) { float l = Len(v); return (l > 1e-5f) ? Mul(v, 1.0f / l) : Vector2{0, 0}; }

ModelVector2 ToModel(Vector2 v) { return {v.x, v.y}; }
Vector2      ToRay(ModelVector2 v) { return {v.x, v.y}; }
}  // namespace

void SteeringScene::OnEnter() {
    trail_.clear();
    vel_ = {0, 0};
}

void SteeringScene::Update(float dt) {
    if (dt <= 0.0f) return;
    if (canvas_.width <= 0) return;   // 첫 DrawWorld 전

    Vector2 center{canvas_.x + canvas_.width * 0.5f, canvas_.y + canvas_.height * 0.5f};
    if (pos_.x == 0 && pos_.y == 0) pos_ = center;

    // 가상 타깃 (Pursue 용): 캔버스 중앙을 도는 원운동.
    targetT_ += dt;
    float r = std::min(canvas_.width, canvas_.height) * 0.30f;
    const float angularSpeed = 0.8f;
    target_ = {center.x + std::cos(targetT_ * 0.8f) * r,
               center.y + std::sin(targetT_ * 0.8f) * r};
    targetVel_ = {-std::sin(targetT_ * angularSpeed) * r * angularSpeed,
                   std::cos(targetT_ * angularSpeed) * r * angularSpeed};

    // 행동별 목표: Pursue 만 가상 타깃, 나머지는 마우스.
    const auto behavior = static_cast<SteerBehavior>(std::clamp(behaviorIdx_, 0, 3));
    const Vector2 goal =
        (behavior == SteerBehavior::Pursue) ? target_ : GetMousePosition();
    const ModelVector2 targetVelocity = (behavior == SteerBehavior::Pursue)
                                            ? ToModel(targetVel_)
                                            : ModelVector2{0.0f, 0.0f};
    const float wanderDelta = (behavior == SteerBehavior::Wander)
                                  ? (float)GetRandomValue(-30, 30) * 0.01f
                                  : 0.0f;

    SteerState state{ToModel(pos_), ToModel(vel_), wanderAngle_};
    const SteerParams params{maxSpeed_, maxForce_, slowRadius_};
    const SteerBounds bounds{canvas_.x, canvas_.y, canvas_.x + canvas_.width,
                             canvas_.y + canvas_.height};
    state = StepSteering(state, behavior, ToModel(goal), targetVelocity, params, dt,
                         bounds, wanderDelta);
    pos_ = ToRay(state.pos);
    vel_ = ToRay(state.vel);
    wanderAngle_ = state.wanderAngle;

    trail_.push_back(pos_);
    while (trail_.size() > kTrailMax) trail_.pop_front();
}

void SteeringScene::DrawWorld(int cx, int cy, int cw, int ch) {
    canvas_ = {(float)cx, (float)cy, (float)cw, (float)ch};

    // 궤적 (오래된 것일수록 흐리게).
    int n = (int)trail_.size();
    for (int i = 1; i < n; ++i) {
        float t = (float)i / (float)n;
        Color c{240, 200, 80, (unsigned char)(180 * t)};
        DrawLineEx(trail_[(size_t)(i - 1)], trail_[(size_t)i], 2.0f, c);
    }

    bool usesMouse = (behaviorIdx_ != 3);
    Vector2 goal = usesMouse ? GetMousePosition() : target_;

    // 목표 표시.
    if (behaviorIdx_ == 1)
        DrawCircleLines((int)goal.x, (int)goal.y, slowRadius_, Color{90, 100, 120, 160});
    DrawCircleV(goal, 6.0f, Color{220, 90, 90, 230});

    // 에이전트 (속도 방향 삼각형).
    Vector2 fwd = Norm(vel_);
    if (Len(fwd) < 1e-4f) fwd = {1, 0};
    Vector2 right{-fwd.y, fwd.x};
    Vector2 a = Add(pos_, Mul(fwd, 12.0f));
    Vector2 b = Add(Sub(pos_, Mul(fwd, 8.0f)), Mul(right, 7.0f));
    Vector2 c = Sub(Sub(pos_, Mul(fwd, 8.0f)), Mul(right, 7.0f));
    DrawTriangle(a, b, c, Color{90, 200, 230, 255});

    if (showVectors_) {
        DrawLineEx(pos_, Add(pos_, Mul(vel_, 0.25f)), 2.0f, Color{120, 230, 140, 230});
    }
}

void SteeringScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;
    const float W = 240.0f;
    auto row = [&](int h) { float r = (float)y; y += h; return r; };

    GuiLabel(Rectangle{(float)(px + 8), row(22), W, 18}, "Steering Behaviors");
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "Behavior");
    GuiComboBox(Rectangle{(float)(px + 8), row(26), W, 22},
                "Seek;Arrive;Wander;Pursue", &behaviorIdx_);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "max speed = %.0f", maxSpeed_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &maxSpeed_, 40.0f, 600.0f);

    std::snprintf(buf, sizeof(buf), "max force = %.0f", maxForce_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &maxForce_, 40.0f, 1200.0f);

    std::snprintf(buf, sizeof(buf), "slow radius = %.0f", slowRadius_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &slowRadius_, 20.0f, 300.0f);

    GuiCheckBox(Rectangle{(float)(px + 8), row(24), 16, 16}, " show velocity", &showVectors_);

    y += 8;
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16},
             behaviorIdx_ == 3 ? "Pursue: 원운동 타깃의 미래 위치 추적"
                               : "목표 = 마우스 위치");
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16},
             "force = desired_vel - current_vel");
}

}  // namespace ailab
