// collide — narrow-phase 충돌 검출. 형상 쌍 → 접촉(법선·관통·점).
//
// broad-phase 가 추린 후보 쌍에 대해 실제 겹침과 분리 방향을 구한다. 법선은
// 일관되게 *A 에서 B 로* 향한다(B 를 +법선으로 밀면 분리). 관통(penetration)은
// 겹친 깊이로, 솔버가 이를 0 으로 만들도록 임펄스를 푼다.
//
// 원-원/원-박스/박스-박스(AABB)를 형상별로 분기한다(physics 시리즈 narrow-phase
// 분기 원칙). OBB 의 SAT 와 임의 볼록의 GJK+EPA 는 sat.hpp / gjk_epa.hpp 로 확장.

#pragma once

#include "math/scalar.hpp"
#include "math/vec.hpp"
#include "physics/rigid_body.hpp"

#include <cmath>

namespace mega {
namespace physics {

struct Contact {
    bool       hit         = false;
    math::Vec2 normal{0, 0};     // A → B (단위)
    float      penetration = 0;  // 겹친 깊이 ≥ 0
    math::Vec2 point{0, 0};      // 대표 접촉점(월드)
};

inline float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline Contact CollideCircleCircle(const math::Vec2& pa, float ra, const math::Vec2& pb, float rb) {
    Contact c;
    math::Vec2 d = pb - pa;
    float rsum = ra + rb;
    float dist2 = math::LengthSq(d);
    if (dist2 >= rsum * rsum) return c;
    float dist = std::sqrt(dist2);
    c.hit = true;
    c.normal = dist > 1e-6f ? d * (1.0f / dist) : math::Vec2{0, 1};
    c.penetration = rsum - dist;
    c.point = pa + c.normal * ra;
    return c;
}

// 원 A vs 박스 B(중심 pb, 반쪽 hb). 법선 A→B.
inline Contact CollideCircleBox(const math::Vec2& pa, float ra, const math::Vec2& pb, const math::Vec2& hb) {
    Contact c;
    math::Vec2 d = pa - pb;                          // 박스 중심 → 원 중심
    math::Vec2 closest{ClampF(d.x, -hb.x, hb.x), ClampF(d.y, -hb.y, hb.y)};
    bool inside = (d.x == closest.x && d.y == closest.y);

    if (inside) {
        // 원 중심이 박스 안 → 가장 가까운 면으로 밀어낸다.
        float ox = hb.x - std::fabs(d.x);
        float oy = hb.y - std::fabs(d.y);
        math::Vec2 outward;
        float faceDepth;
        if (ox < oy) { outward = {math::Sign(d.x), 0}; faceDepth = ox; }
        else         { outward = {0, math::Sign(d.y)}; faceDepth = oy; }
        c.hit = true;
        c.normal = outward * -1.0f;                  // A→B(박스 중심 쪽)
        c.penetration = ra + faceDepth;
        c.point = pa + outward * (-ra);
        return c;
    }

    math::Vec2 toCircle = d - closest;               // 면 위 점 → 원 중심
    float dist2 = math::LengthSq(toCircle);
    if (dist2 >= ra * ra) return c;
    float dist = std::sqrt(dist2);
    math::Vec2 outward = dist > 1e-6f ? toCircle * (1.0f / dist) : math::Vec2{0, 1};
    c.hit = true;
    c.normal = outward * -1.0f;                      // A(원)→B(박스)
    c.penetration = ra - dist;
    c.point = pb + closest;
    return c;
}

// 박스 A vs 박스 B(둘 다 AABB). 최소 관통 축으로 분리.
inline Contact CollideBoxBox(const math::Vec2& pa, const math::Vec2& ha, const math::Vec2& pb, const math::Vec2& hb) {
    Contact c;
    math::Vec2 d = pb - pa;
    float ox = (ha.x + hb.x) - std::fabs(d.x);
    if (ox <= 0) return c;
    float oy = (ha.y + hb.y) - std::fabs(d.y);
    if (oy <= 0) return c;
    c.hit = true;
    if (ox < oy) { c.penetration = ox; c.normal = {math::Sign(d.x), 0}; }
    else         { c.penetration = oy; c.normal = {0, math::Sign(d.y)}; }
    c.point = pa + d * 0.5f;
    return c;
}

// 형상 쌍 디스패치. 법선은 항상 a→b.
inline Contact Collide(const RigidBody2D& a, const RigidBody2D& b) {
    if (a.shape.type == ShapeType::Circle && b.shape.type == ShapeType::Circle)
        return CollideCircleCircle(a.position, a.shape.radius, b.position, b.shape.radius);
    if (a.shape.type == ShapeType::Circle && b.shape.type == ShapeType::Box)
        return CollideCircleBox(a.position, a.shape.radius, b.position, b.shape.half);
    if (a.shape.type == ShapeType::Box && b.shape.type == ShapeType::Circle) {
        Contact c = CollideCircleBox(b.position, b.shape.radius, a.position, a.shape.half);
        c.normal = c.normal * -1.0f;                 // b→a 를 a→b 로 뒤집기
        return c;
    }
    return CollideBoxBox(a.position, a.shape.half, b.position, b.shape.half);
}

}  // namespace physics
}  // namespace mega
