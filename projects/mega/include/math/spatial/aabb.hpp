// aabb — 축 정렬 경계 상자(Axis-Aligned Bounding Box). 공간 분할의 공용 통화.
//
// 모든 공간 구조(Quadtree/Grid/BVH)가 AABB 로 영역을 표현하고 겹침을 판정한다.
// 축 정렬이라 교차 판정이 비교 몇 번으로 끝나, 광역 탐색(broad-phase)의 1차
// 필터로 쓰기 좋다.

#pragma once

#include "math/vec.hpp"

namespace mega {
namespace math {

struct Aabb2 {
    Vec2 min{0, 0}, max{0, 0};

    static Aabb2 FromCenterHalf(const Vec2& c, const Vec2& h) {
        return {{c.x - h.x, c.y - h.y}, {c.x + h.x, c.y + h.y}};
    }
    Vec2 Center() const { return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f}; }
    Vec2 Half()   const { return {(max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f}; }

    bool Contains(const Vec2& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
    }
    bool Intersects(const Aabb2& o) const {
        return !(o.min.x > max.x || o.max.x < min.x || o.min.y > max.y || o.max.y < min.y);
    }
};

struct Aabb3 {
    Vec3 min{0, 0, 0}, max{0, 0, 0};

    static Aabb3 Empty() { return {Vec3{1e30f}, Vec3{-1e30f}}; }
    static Aabb3 FromCenterHalf(const Vec3& c, const Vec3& h) {
        return {{c.x - h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y + h.y, c.z + h.z}};
    }

    Vec3 Center() const { return (min + max) * 0.5f; }
    Vec3 Extent() const { return max - min; }

    bool Contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }
    bool Intersects(const Aabb3& o) const {
        return !(o.min.x > max.x || o.max.x < min.x ||
                 o.min.y > max.y || o.max.y < min.y ||
                 o.min.z > max.z || o.max.z < min.z);
    }

    void Expand(const Vec3& p) { min = Min(min, p); max = Max(max, p); }
    void Expand(const Aabb3& o) { min = Min(min, o.min); max = Max(max, o.max); }

    int LongestAxis() const {
        Vec3 e = Extent();
        if (e.x >= e.y && e.x >= e.z) return 0;
        return (e.y >= e.z) ? 1 : 2;
    }
    float SurfaceArea() const {
        Vec3 e = Extent();
        if (e.x < 0 || e.y < 0 || e.z < 0) return 0.0f;   // 빈 상자
        return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
    }
};

}  // namespace math
}  // namespace mega
