// Steering 한 스텝의 순수 계산부 — 렌더·입력과 분리해 헤드리스로 검증한다.
//
// SteeringScene::Update 는 시간에 의존하는 입력(마우스 목표, 원운동 타깃,
// 난수 wander 델타)만 만들고 실제 모델은 전부 이 헤더가 담당한다.
//
// 모델 (ai/part11):
//   desired  = 행동별 목표 속도
//   steer    = Truncate(desired - vel, maxForce)     # 1차 절단: 힘
//   vel      = Truncate(vel + steer * dt, maxSpeed)  # 2차 절단: 속도
//   pos      = pos + vel * dt
//   경계     = 캔버스 안으로 clamp + 해당 축 속도 * -0.5 (감쇠 반사)
//
// 경계 처리는 model/ai_model.h 의 ReflectAxis 와 다른 모델이다. Steering 은
// 벽에 부딪힌 에이전트가 튕겨 나가는 대신 벽을 따라 미끄러지는 편이 관찰에
// 유리해 감쇠 반사를 정본으로 삼는다. Boids 는 ReflectAxis 를 쓴다.

#pragma once

#include "model/ai_model.h"

#include <algorithm>
#include <cmath>

namespace ailab {

enum class SteerBehavior { Seek = 0, Arrive = 1, Wander = 2, Pursue = 3 };

struct SteerParams {
    float maxSpeed   = 260.0f;
    float maxForce   = 520.0f;
    float slowRadius = 120.0f;
};

// 에이전트가 머물러야 하는 캔버스 사각형.
struct SteerBounds {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
};

struct SteerState {
    ModelVector2 pos{};
    ModelVector2 vel{};
    float wanderAngle = 0.0f;
};

inline ModelVector2 VecSub(ModelVector2 a, ModelVector2 b) {
    return {a.x - b.x, a.y - b.y};
}
inline ModelVector2 VecAdd(ModelVector2 a, ModelVector2 b) {
    return {a.x + b.x, a.y + b.y};
}
inline ModelVector2 VecMul(ModelVector2 a, float s) { return {a.x * s, a.y * s}; }
inline float VecLen(ModelVector2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
inline ModelVector2 VecNorm(ModelVector2 v) {
    const float l = VecLen(v);
    return (l > 1e-5f) ? VecMul(v, 1.0f / l) : ModelVector2{0.0f, 0.0f};
}
inline ModelVector2 VecTruncate(ModelVector2 v, float maxLength) {
    const float l = VecLen(v);
    return (l > maxLength && l > 1e-5f) ? VecMul(v, maxLength / l) : v;
}

// Pursue 의 예측 시간. 거리가 멀수록, 그리고 서로의 속도가 느릴수록 길다.
// +1 은 두 속도가 모두 0 일 때의 0 나눗셈 방지항이다.
inline float PursueLookahead(float distance, float maxSpeed, float targetSpeed) {
    return distance / (maxSpeed + targetSpeed + 1.0f);
}

// 행동별 목표 속도. wanderAngle 은 이미 갱신된 값을 그대로 읽는다.
inline ModelVector2 DesiredVelocity(SteerBehavior behavior,
                                    const SteerState& state,
                                    ModelVector2 goal,
                                    ModelVector2 targetVelocity,
                                    const SteerParams& params) {
    switch (behavior) {
    case SteerBehavior::Seek:
        return VecMul(VecNorm(VecSub(goal, state.pos)), params.maxSpeed);

    case SteerBehavior::Arrive: {
        const ModelVector2 to = VecSub(goal, state.pos);
        const float d = VecLen(to);
        const float speed = (d < params.slowRadius)
                                ? params.maxSpeed * (d / params.slowRadius)
                                : params.maxSpeed;
        return (d > 1e-4f) ? VecMul(to, speed / d) : ModelVector2{0.0f, 0.0f};
    }

    case SteerBehavior::Wander: {
        ModelVector2 forward = VecNorm(state.vel);
        if (VecLen(forward) < 1e-4f) forward = {1.0f, 0.0f};
        const ModelVector2 circleCenter = VecAdd(state.pos, VecMul(forward, 80.0f));
        const ModelVector2 offset{std::cos(state.wanderAngle) * 50.0f,
                                  std::sin(state.wanderAngle) * 50.0f};
        return VecMul(VecNorm(VecSub(VecAdd(circleCenter, offset), state.pos)),
                      params.maxSpeed);
    }

    case SteerBehavior::Pursue: {
        const float distance = VecLen(VecSub(goal, state.pos));
        const float lookahead =
            PursueLookahead(distance, params.maxSpeed, VecLen(targetVelocity));
        const ModelVector2 future = VecAdd(goal, VecMul(targetVelocity, lookahead));
        return VecMul(VecNorm(VecSub(future, state.pos)), params.maxSpeed);
    }
    }
    return {0.0f, 0.0f};
}

// 한 스텝. wanderDelta 는 Wander 행동에서만 소비된다 (호출자가 난수를 만든다).
inline SteerState StepSteering(SteerState state,
                               SteerBehavior behavior,
                               ModelVector2 goal,
                               ModelVector2 targetVelocity,
                               const SteerParams& params,
                               float dt,
                               const SteerBounds& bounds,
                               float wanderDelta = 0.0f) {
    if (behavior == SteerBehavior::Wander) state.wanderAngle += wanderDelta;

    const ModelVector2 desired =
        DesiredVelocity(behavior, state, goal, targetVelocity, params);

    const ModelVector2 steer =
        VecTruncate(VecSub(desired, state.vel), params.maxForce);
    state.vel = VecTruncate(VecAdd(state.vel, VecMul(steer, dt)), params.maxSpeed);
    state.pos = VecAdd(state.pos, VecMul(state.vel, dt));

    // 캔버스 경계에서 감쇠 반사.
    if (state.pos.x < bounds.minX) { state.pos.x = bounds.minX; state.vel.x *= -0.5f; }
    if (state.pos.x > bounds.maxX) { state.pos.x = bounds.maxX; state.vel.x *= -0.5f; }
    if (state.pos.y < bounds.minY) { state.pos.y = bounds.minY; state.vel.y *= -0.5f; }
    if (state.pos.y > bounds.maxY) { state.pos.y = bounds.maxY; state.vel.y *= -0.5f; }
    return state;
}

}  // namespace ailab
