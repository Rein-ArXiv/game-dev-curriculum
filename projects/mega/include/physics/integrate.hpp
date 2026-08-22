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

#include <cstdint>

namespace mega {
namespace physics {

// 어느 시점의 속도로 위치를 옮기는가. 이 하나가 두 적분기의 전부다.
enum class Integrator : uint8_t { Symplectic, Explicit };

inline void IntegrateForces(RigidBody2D& b, const math::Vec2& gravity, float dt) {
    if (b.IsStatic()) { b.force = {0, 0}; b.torque = 0; return; }
    b.prevVelocity   = b.velocity;                 // Explicit 팔이 읽을 "갱신 전"
    b.prevAngularVel = b.angularVel;
    math::Vec2 accel = gravity + b.force * b.invMass;
    b.velocity   += accel * dt;                    // 속도 먼저
    b.angularVel += b.torque * b.invInertia * dt;
}

// snippet-begin: integrate-velocity
inline void IntegrateVelocity(RigidBody2D& b, float dt,
                              Integrator kind = Integrator::Symplectic) {
    if (b.IsStatic()) return;
    // Symplectic 은 *지금* 속도로 옮긴다 — 이번 스텝의 중력과 접촉 임펄스가 이미
    // 반영된 속도다. Explicit 은 힘을 더하기 **전** 속도로 옮기므로, 이번 스텝에
    // 솔버가 밀어낸 결과가 위치에 들어가지 않는다. 적층이 부푸는 원인이 그것이다.
    const math::Vec2 v = kind == Integrator::Explicit ? b.prevVelocity : b.velocity;
    const float      w = kind == Integrator::Explicit ? b.prevAngularVel : b.angularVel;
    b.position += v * dt;
    b.angle    += w * dt;
    b.force = {0, 0};
    b.torque = 0;
}
// snippet-end: integrate-velocity

// 힘→속도→위치 한 번에(솔버 없이 단독 적분할 때).
inline void Integrate(RigidBody2D& b, const math::Vec2& gravity, float dt) {
    IntegrateForces(b, gravity, dt);
    IntegrateVelocity(b, dt);
}

}  // namespace physics
}  // namespace mega
