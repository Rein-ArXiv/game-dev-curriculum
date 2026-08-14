// rigid_body — 2D 강체. 선형 + 각운동, 역질량으로 정적/동적 통일.
//
// invMass = 0 이면 정적(무한 질량, 외력에 안 밀림). 임펄스는 속도를 바로 바꾸는
// 단위라 Sequential Impulse 솔버가 이걸 누적한다. 위치/속도/회전을 한 구조에 담고,
// 적분기와 솔버가 이 위에서 동작한다.

#pragma once

#include "math/vec.hpp"
#include "physics/shape.hpp"

namespace mega {
namespace physics {

struct RigidBody2D {
    math::Vec2 position{0, 0};
    float      angle = 0;
    math::Vec2 velocity{0, 0};
    float      angularVel = 0;

    float invMass    = 0;        // 0 = 정적
    float invInertia = 0;
    float restitution = 0.2f;    // 반발 계수 e
    float friction    = 0.4f;    // 마찰 계수 μ

    math::Vec2 force{0, 0};      // 프레임 누적 외력
    float      torque = 0;

    Shape shape;

    bool IsStatic() const { return invMass == 0.0f; }

    // 질량 설정: m<=0 이면 정적(역질량 0).
    void SetMass(float m) {
        if (m <= 0.0f) { invMass = 0; invInertia = 0; return; }
        invMass = 1.0f / m;
        float I = shape.Inertia(m);
        invInertia = I > 0.0f ? 1.0f / I : 0.0f;
    }
    void SetStatic() { invMass = 0; invInertia = 0; }

    // 접촉점 r(질량중심 기준 상대 벡터)에 임펄스 적용 → 선/각속도 변화.
    void ApplyImpulse(const math::Vec2& imp, const math::Vec2& r) {
        velocity   += imp * invMass;
        angularVel += invInertia * math::Cross(r, imp);   // r × imp (스칼라 z)
    }

    // 외력/토크 누적(중력·바람 등).
    void AddForce(const math::Vec2& f) { force += f; }
};

}  // namespace physics
}  // namespace mega
