// integrate — Symplectic Euler 적분기.
//
// Explicit Euler(x += v·dt; v += a·dt)는 에너지를 발산시켜 진자·스택이 부푼다.
// Symplectic Euler 는 *속도를 먼저 갱신하고 그 새 속도로 위치를 옮긴다* — 한 줄
// 차이지만 에너지가 유계로 보존돼 60Hz 스텝에서 안정적이다(physics 시리즈 적분기
// 비교의 결론). 비용은 Explicit Euler 와 동일하다.
//
// gravity 는 가속도(질량 무관)로 더하고, 누적 외력은 invMass 로 가속도화한다.

#pragma once

#include "math/vec.hpp"
#include "physics/rigid_body.hpp"

namespace mega {
namespace physics {

inline void IntegrateForces(RigidBody2D& b, const math::Vec2& gravity, float dt) {
    if (b.IsStatic()) { b.force = {0, 0}; b.torque = 0; return; }
    math::Vec2 accel = gravity + b.force * b.invMass;
    b.velocity   += accel * dt;                    // 속도 먼저
    b.angularVel += b.torque * b.invInertia * dt;
}

inline void IntegrateVelocity(RigidBody2D& b, float dt) {
    if (b.IsStatic()) return;
    b.position += b.velocity * dt;                 // 새 속도로 위치 이동
    b.angle    += b.angularVel * dt;
    b.force = {0, 0};
    b.torque = 0;
}

// 힘→속도→위치 한 번에(솔버 없이 단독 적분할 때).
inline void Integrate(RigidBody2D& b, const math::Vec2& gravity, float dt) {
    IntegrateForces(b, gravity, dt);
    IntegrateVelocity(b, dt);
}

}  // namespace physics
}  // namespace mega
