// shape — 2D 충돌 형상: Circle, Box(축 정렬 반쪽 길이).
//
// 강체 시뮬의 기본 형상이다. 원은 회전 불변이라 narrow-phase 가 가장 싸고,
// 박스는 면/모서리 접촉을 만든다. 형상은 질량과 함께 관성 모멘트를 정해 회전
// 동역학을 결정한다(physics 시리즈 질량 특성). OBB+SAT 와 볼록 다각형은
// narrow_phase 모듈에서 확장한다.

#pragma once

#include "math/spatial/aabb.hpp"
#include "math/vec.hpp"

namespace mega {
namespace physics {

enum class ShapeType { Circle, Box };

struct Shape {
    ShapeType   type   = ShapeType::Circle;
    float       radius = 0.5f;             // Circle
    math::Vec2  half{0.5f, 0.5f};          // Box 반쪽 길이

    static Shape Circle(float r) { Shape s; s.type = ShapeType::Circle; s.radius = r; return s; }
    static Shape Box(const math::Vec2& h) { Shape s; s.type = ShapeType::Box; s.half = h; return s; }

    // 질량 m 에 대한 (z축) 관성 모멘트. 회전 동역학에 쓴다.
    float Inertia(float m) const {
        if (type == ShapeType::Circle) return 0.5f * m * radius * radius;
        float w = 2 * half.x, h = 2 * half.y;
        return m * (w * w + h * h) / 12.0f;       // 직사각형
    }

    // 위치 기준 월드 AABB(회전 미고려 — 보수적 경계).
    math::Aabb2 ComputeAabb(const math::Vec2& pos) const {
        if (type == ShapeType::Circle)
            return {{pos.x - radius, pos.y - radius}, {pos.x + radius, pos.y + radius}};
        return {{pos.x - half.x, pos.y - half.y}, {pos.x + half.x, pos.y + half.y}};
    }
};

}  // namespace physics
}  // namespace mega
