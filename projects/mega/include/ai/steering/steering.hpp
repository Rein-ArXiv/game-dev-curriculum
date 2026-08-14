// steering — 개체별 가속을 부드럽게 합성하는 조향 행동(Reynolds).
//
// 각 행동은 "목표 속도(desired) - 현재 속도" 를 maxForce 로 자른 *조향력* 을
// 낸다. 여러 힘을 합쳐 매 프레임 가속을 만들면 경로가 부드러워진다. 단순
// 가중합은 평균치에 끌려가므로(회피가 군집에 희석됨), 우선순위 blender 로
// 높은 우선 행동(회피)이 가속 예산을 먼저 쓰게 한다(ai 시리즈 Steering 합성).

#pragma once

#include "math/vec.hpp"

#include <cmath>
#include <vector>

namespace mega {
namespace ai {
namespace steer {

struct Agent {
    math::Vec2 position{0, 0};
    math::Vec2 velocity{0, 0};
    float      maxSpeed = 50.0f;
    float      maxForce = 120.0f;
};

inline math::Vec2 Truncate(const math::Vec2& v, float maxLen) {
    float l2 = math::LengthSq(v);
    if (l2 > maxLen * maxLen && l2 > 1e-12f) return v * (maxLen / std::sqrt(l2));
    return v;
}

// 목표로 향하는 조향.
inline math::Vec2 Seek(const Agent& a, const math::Vec2& target) {
    math::Vec2 d = target - a.position;
    float l = math::Length(d);
    if (l < 1e-5f) return {0, 0};
    math::Vec2 desired = d * (a.maxSpeed / l);
    return Truncate(desired - a.velocity, a.maxForce);
}

// 목표에서 멀어지는 조향.
inline math::Vec2 Flee(const Agent& a, const math::Vec2& target) {
    return Seek(a, a.position + (a.position - target)) ;
}

// 목표에 도착하며 감속(slowRadius 안에서 속도↓).
inline math::Vec2 Arrive(const Agent& a, const math::Vec2& target, float slowRadius) {
    math::Vec2 d = target - a.position;
    float dist = math::Length(d);
    if (dist < 1e-4f) return a.velocity * -1.0f;          // 도착: 정지하려 감속
    float speed = (dist < slowRadius) ? a.maxSpeed * (dist / slowRadius) : a.maxSpeed;
    math::Vec2 desired = d * (speed / dist);
    return Truncate(desired - a.velocity, a.maxForce);
}

// 분리: 가까운 이웃에서 멀어진다(거리에 반비례해 강하게).
inline math::Vec2 Separation(const Agent& a, const std::vector<Agent>& neighbors, float radius) {
    math::Vec2 force{0, 0};
    float r2 = radius * radius;
    for (const Agent& n : neighbors) {
        math::Vec2 d = a.position - n.position;
        float d2 = math::LengthSq(d);
        if (d2 > 1e-6f && d2 < r2) force += d * (1.0f / d2);   // 가까울수록 큼
    }
    if (math::LengthSq(force) < 1e-10f) return {0, 0};
    math::Vec2 desired = math::Normalize(force) * a.maxSpeed;
    return Truncate(desired - a.velocity, a.maxForce);
}

// 정렬: 이웃 평균 방향에 속도를 맞춘다.
inline math::Vec2 Alignment(const Agent& a, const std::vector<Agent>& neighbors) {
    if (neighbors.empty()) return {0, 0};
    math::Vec2 avg{0, 0};
    for (const Agent& n : neighbors) avg += n.velocity;
    avg = avg * (1.0f / neighbors.size());
    if (math::LengthSq(avg) < 1e-10f) return {0, 0};
    math::Vec2 desired = math::Normalize(avg) * a.maxSpeed;
    return Truncate(desired - a.velocity, a.maxForce);
}

// 응집: 이웃 무게중심으로 향한다.
inline math::Vec2 Cohesion(const Agent& a, const std::vector<Agent>& neighbors) {
    if (neighbors.empty()) return {0, 0};
    math::Vec2 center{0, 0};
    for (const Agent& n : neighbors) center += n.position;
    center = center * (1.0f / neighbors.size());
    return Seek(a, center);
}

// 우선순위 blender: 높은 우선 행동부터 가속 예산을 쓰고, 소진되면 나머지 무시.
class PriorityBlender {
public:
    explicit PriorityBlender(float maxForce) : remaining_(maxForce) {}

    // 힘을 우선순위 순으로 추가. 예산이 남아 더 받을 수 있으면 true.
    bool Add(const math::Vec2& force) {
        float mag = math::Length(force);
        if (mag < 1e-5f) return remaining_ > 1e-5f;
        if (mag <= remaining_) { accum_ += force; remaining_ -= mag; return remaining_ > 1e-5f; }
        accum_ += math::Normalize(force) * remaining_;        // 예산만큼만
        remaining_ = 0;
        return false;
    }
    math::Vec2 Result() const { return accum_; }

private:
    math::Vec2 accum_{0, 0};
    float      remaining_;
};

}  // namespace steer
}  // namespace ai
}  // namespace mega
