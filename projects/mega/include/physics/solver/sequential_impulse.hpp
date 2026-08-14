// sequential_impulse — 충돌 응답 솔버.
//
// 접촉마다 "법선 방향 상대 속도를 0 이상으로(파고들지 않게), 반발 계수만큼 튕기게,
// 마찰은 법선 임펄스에 비례해 제한" 하는 임펄스를 푼다. 여러 접촉을 한 번에 푸는
// 대신 *순차적으로 반복* 하면(Sequential Impulse) 연립 제약이 수렴한다 — 게임
// 엔진의 표준 선택이다(physics 시리즈 솔버 비교).
//
// 위치 오차(관통)는 Baumgarte 안정화로 속도 바이어스에 섞어 천천히 밀어낸다.
// slop 이하의 관통은 무시해 미세 진동을 막는다.

#pragma once

#include "math/vec.hpp"
#include "physics/narrow_phase/collide.hpp"
#include "physics/rigid_body.hpp"

namespace mega {
namespace physics {

// 스칼라 각속도 × 상대 벡터 → 접선 속도(2D 외적).
inline math::Vec2 CrossSV(float s, const math::Vec2& v) { return {-s * v.y, s * v.x}; }

struct SolverParams {
    float baumgarte = 0.2f;     // 위치 보정 비율
    float slop      = 0.005f;   // 허용 관통(이하는 무시)
};

struct ContactConstraint {
    RigidBody2D* a = nullptr;
    RigidBody2D* b = nullptr;
    math::Vec2   normal{0, 0};
    math::Vec2   ra{0, 0}, rb{0, 0};
    float        normalMass  = 0;
    float        tangentMass = 0;
    float        restitutionBias = 0;   // 반발에 의한 목표 분리 속도
    float        biasPos     = 0;       // Baumgarte 위치 보정
    float        friction    = 0;
    float        normalImpulse  = 0;    // 누적(클램프용)
    float        tangentImpulse = 0;
};

inline ContactConstraint MakeConstraint(RigidBody2D& a, RigidBody2D& b, const Contact& c,
                                        float dt, const SolverParams& p = {}) {
    ContactConstraint k;
    k.a = &a; k.b = &b;
    k.normal = c.normal;
    k.ra = c.point - a.position;
    k.rb = c.point - b.position;
    k.friction = std::sqrt(a.friction * b.friction);
    float e = a.restitution < b.restitution ? a.restitution : b.restitution;

    math::Vec2 t = {-k.normal.y, k.normal.x};
    float rnA = math::Cross(k.ra, k.normal), rnB = math::Cross(k.rb, k.normal);
    float kn = a.invMass + b.invMass + a.invInertia * rnA * rnA + b.invInertia * rnB * rnB;
    k.normalMass = kn > 0 ? 1.0f / kn : 0.0f;
    float rtA = math::Cross(k.ra, t), rtB = math::Cross(k.rb, t);
    float kt = a.invMass + b.invMass + a.invInertia * rtA * rtA + b.invInertia * rtB * rtB;
    k.tangentMass = kt > 0 ? 1.0f / kt : 0.0f;

    // 접근 속도가 충분히 크면 반발 목표 속도 설정.
    math::Vec2 rv = (b.velocity + CrossSV(b.angularVel, k.rb)) - (a.velocity + CrossSV(a.angularVel, k.ra));
    float vn = math::Dot(rv, k.normal);
    k.restitutionBias = (vn < -1.0f) ? -e * vn : 0.0f;

    float pen = c.penetration - p.slop;
    k.biasPos = pen > 0.0f ? p.baumgarte * (1.0f / dt) * pen : 0.0f;
    return k;
}

// 한 번의 속도 반복: 법선 임펄스(비관통+반발) → 마찰 임펄스(쿨롱 한계).
inline void SolveVelocity(ContactConstraint& k) {
    RigidBody2D& a = *k.a;
    RigidBody2D& b = *k.b;
    math::Vec2 t = {-k.normal.y, k.normal.x};

    // 법선.
    math::Vec2 rv = (b.velocity + CrossSV(b.angularVel, k.rb)) - (a.velocity + CrossSV(a.angularVel, k.ra));
    float vn = math::Dot(rv, k.normal);
    float lambda = -k.normalMass * (vn - k.restitutionBias - k.biasPos);
    float newI = k.normalImpulse + lambda;
    if (newI < 0) newI = 0;                       // 임펄스는 당기지 않는다(밀기만)
    lambda = newI - k.normalImpulse;
    k.normalImpulse = newI;
    math::Vec2 P = k.normal * lambda;
    a.ApplyImpulse(P * -1.0f, k.ra);
    b.ApplyImpulse(P, k.rb);

    // 마찰(접선): |impulse| ≤ μ·normalImpulse.
    rv = (b.velocity + CrossSV(b.angularVel, k.rb)) - (a.velocity + CrossSV(a.angularVel, k.ra));
    float vt = math::Dot(rv, t);
    float lt = -k.tangentMass * vt;
    float maxF = k.friction * k.normalImpulse;
    float newT = k.tangentImpulse + lt;
    if (newT > maxF) newT = maxF;
    else if (newT < -maxF) newT = -maxF;
    lt = newT - k.tangentImpulse;
    k.tangentImpulse = newT;
    math::Vec2 Pt = t * lt;
    a.ApplyImpulse(Pt * -1.0f, k.ra);
    b.ApplyImpulse(Pt, k.rb);
}

}  // namespace physics
}  // namespace mega
