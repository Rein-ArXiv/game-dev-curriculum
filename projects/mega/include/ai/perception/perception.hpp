// perception — 인지: 시야 콘 + 가림(occlusion) + last-seen 메모리 + 청각.
//
// 시야만으로는 "방금 봤다가 모서리 뒤로 사라진 적"을 즉시 잊어 부자연스럽다.
// vision(원뿔+가림) + memory(마지막 목격 위치와 감쇠) + hearing(소리 이벤트)를
// 합쳐, 적이 시야에서 사라져도 잠시 마지막 위치로 향하게 한다(ai 시리즈 인지).

#pragma once

#include "ai/pathfinding/grid_map.hpp"
#include "math/vec.hpp"

#include <cmath>

namespace mega {
namespace ai {
namespace perceive {

// 시야 원뿔: 범위 + 반각(코사인으로 보관).
struct VisionCone {
    float range        = 20.0f;
    float halfAngleCos = 0.5f;       // cos(60°) = 0.5 → 전체 시야각 120°

    static VisionCone FromDegrees(float range, float fullAngleDeg) {
        VisionCone c;
        c.range = range;
        c.halfAngleCos = std::cos(fullAngleDeg * 0.5f * 3.14159265f / 180.0f);
        return c;
    }
};

// 가림 무시 시야 판정: 범위 + 각도.
inline bool InCone(const math::Vec2& eye, const math::Vec2& facing,
                   const VisionCone& cone, const math::Vec2& target) {
    math::Vec2 to = target - eye;
    float d = math::Length(to);
    if (d > cone.range) return false;
    if (d < 1e-4f) return true;
    math::Vec2 fwd = math::Normalize(facing);
    float cosA = math::Dot(fwd, to * (1.0f / d));
    return cosA >= cone.halfAngleCos;
}

// 가림 포함 시야: 원뿔 안 + 벽이 가로막지 않음(격자 레이캐스트).
inline bool CanSee(const path::GridMap& map, const math::Vec2& eye, const math::Vec2& facing,
                   const VisionCone& cone, const math::Vec2& target) {
    if (!InCone(eye, facing, cone, target)) return false;
    math::Vec2 d = target - eye;
    float dist = math::Length(d);
    int steps = (int)std::ceil(dist / 0.25f);
    for (int i = 1; i < steps; ++i) {                  // 끝점 제외하고 경로 샘플
        float t = (float)i / steps;
        math::Vec2 p = eye + d * t;
        if (!map.Walkable((int)std::floor(p.x), (int)std::floor(p.y))) return false;  // 벽에 가림
    }
    return true;
}

// 청각: 소리 이벤트(위치+크기) 안에 있으면 들린다.
inline bool CanHear(const math::Vec2& ear, const math::Vec2& soundPos, float loudnessRadius) {
    return math::LengthSq(ear - soundPos) <= loudnessRadius * loudnessRadius;
}

// 마지막 목격 기억: 보면 갱신, 못 보면 나이를 먹어 일정 시간 후 잊는다.
struct Memory {
    bool       hasTarget = false;
    math::Vec2 lastSeen{0, 0};
    float      age = 0;              // 마지막 목격 후 경과(초)

    void Observe(const math::Vec2& pos) { hasTarget = true; lastSeen = pos; age = 0; }
    void Update(float dt, float forgetAfter) {
        if (!hasTarget) return;
        age += dt;
        if (age >= forgetAfter) { hasTarget = false; age = 0; }   // 망각
    }
};

}  // namespace perceive
}  // namespace ai
}  // namespace mega
