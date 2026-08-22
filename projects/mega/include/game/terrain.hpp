// terrain — 정적 기하 색인과 발파.
//
// 두 종류의 질의가 바위 위에서 돈다.
//
//   영역   발파 브러시가 어느 칸을 덮는가.
//   선분   타워와 적 사이에 바위가 끼어 있는가(사격선).
//
// 둘 다 "정적이고, 가끔 통째로 바뀌고, 그 사이에는 아주 자주 물어보는" 부류다 —
// BVH 가 정확히 그 부류를 위한 자료구조다. 전수 순회 팔을 지우지 않고 남겨
// `static_query=naive|bvh` 로 고른다. 두 팔은 **같은 히트 집합**을 내야 하고,
// 그 단언이 없으면 빨라진 쪽이 덜 찾은 것인지 알 수 없다.
//
// 2D 게임에 3D BVH 를 쓰는 이유는 같은 알고리즘의 2D 사본을 만들지 않기 위해서다.
// 칸 하나는 z∈[0,1] 로 매립한 Aabb3 이고, 질의점의 z 는 늘 0.5 다.
//
// 발파는 math/sdf.hpp 의 브러시를 밀도장에서 **감산**한다. 감산만 하는 연산이라
// 열리기만 하고 닫히지 않는다 — 코어가 봉쇄되는 발파가 구조적으로 없다는 것이
// 그 성질에서 바로 나온다.

#pragma once

#include "game/defense_map.hpp"
#include "game/switches.hpp"
#include "math/sdf.hpp"
#include "math/spatial/bvh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mega {
namespace game {

// 선분 대 AABB(3D slab). t∈[0,1] 구간에서 겹치면 참.
// snippet-begin: segment-aabb
inline bool SegmentHitsAabb(const math::Vec3& from, const math::Vec3& to,
                            const math::Aabb3& box) {
    float tMin = 0.0f, tMax = 1.0f;
    const math::Vec3 d = to - from;
    for (int axis = 0; axis < 3; ++axis) {
        const float origin = from[axis], delta = d[axis];
        const float lo = box.min[axis], hi = box.max[axis];
        if (std::fabs(delta) < 1e-9f) {
            if (origin < lo || origin > hi) return false;   // 축에 평행하고 밖이다
            continue;
        }
        float t0 = (lo - origin) / delta;
        float t1 = (hi - origin) / delta;
        if (t0 > t1) { const float swap = t0; t0 = t1; t1 = swap; }
        if (t0 > tMin) tMin = t0;
        if (t1 < tMax) tMax = t1;
        if (tMin > tMax) return false;
    }
    return true;
}
// snippet-end: segment-aabb

// 발파 브러시. a==b 면 구, 아니면 캡슐(선형 폭파).
struct BlastBrush {
    math::Vec3 a{0, 0, 0.5f}, b{0, 0, 0.5f};
    float      radius = 26.0f;

    bool Capsule() const { return !(a.x == b.x && a.y == b.y && a.z == b.z); }
    float Sd(const math::Vec3& p) const {
        return Capsule() ? math::SdCapsule(p, a, b, radius) : math::SdSphere(p - a, radius);
    }
    math::Aabb3 Bounds() const {
        math::Aabb3 box = math::Aabb3::Empty();
        box.Expand(a);
        box.Expand(b);
        box.min = box.min - math::Vec3{radius, radius, radius};
        box.max = box.max + math::Vec3{radius, radius, radius};
        return box;
    }
    static BlastBrush Sphere(const math::Vec2& at, float radius) {
        BlastBrush brush;
        brush.a = brush.b = {at.x, at.y, 0.5f};
        brush.radius = radius;
        return brush;
    }
};

struct BlastResult {
    int opened = 0;      // 실제로 열린 바위 칸
    int candidates = 0;  // 색인이 넘겨준 후보 칸
};

// 바위 칸(과 안착 잔해)의 색인. 지형이 바뀌면 Rebuild 하고 버전이 오른다 —
// 무효화를 잊었는지는 그 버전으로만 밖에서 확인할 수 있다.
class StaticGeometry {
public:
    void Rebuild(const DefenseMap& map) {
        prims_.clear();
        cells_.clear();
        const float cell = static_cast<float>(map.CellSize());
        for (int y = 0; y < map.Rows(); ++y)
            for (int x = 0; x < map.Columns(); ++x) {
                if (!map.Rock(x, y)) continue;
                const float px = static_cast<float>(x) * cell;
                const float py = static_cast<float>(map.OriginY()) + static_cast<float>(y) * cell;
                prims_.push_back(math::Aabb3{{px, py, 0.0f}, {px + cell, py + cell, 1.0f}});
                cells_.push_back({x, y});
            }
        bvh_.Build(prims_, 4);
        ++version_;
    }

    size_t PrimCount() const { return prims_.size(); }
    size_t NodeCount() const { return bvh_.NodeCount(); }
    uint32_t Version() const { return version_; }
    DefenseMap::Cell CellOf(size_t prim) const { return cells_[prim]; }
    const math::Aabb3& Prim(size_t i) const { return prims_[i]; }

    // 영역 질의. 두 팔이 같은 집합을 내야 하므로 결과를 오름차순으로 정규화한다.
    // snippet-begin: query-region
    void QueryRegion(const math::Aabb3& region, StaticQueryKind kind,
                     std::vector<uint32_t>& out) const {
        out.clear();
        if (kind == StaticQueryKind::Bvh) {
            bvh_.Query(region, [&out](int prim) { out.push_back(static_cast<uint32_t>(prim)); });
            std::sort(out.begin(), out.end());
            return;
        }
        for (size_t i = 0; i < prims_.size(); ++i)
            if (prims_[i].Intersects(region)) out.push_back(static_cast<uint32_t>(i));
    }
    // snippet-end: query-region

    // 사격선 판정. 지면 높이 0.5 의 선분이 바위 상자를 지나면 막힌 것이다.
    // snippet-begin: segment-blocked
    bool SegmentBlocked(const math::Vec2& from, const math::Vec2& to,
                        StaticQueryKind kind) const {
        if (prims_.empty()) return false;
        const math::Vec3 a{from.x, from.y, 0.5f};
        const math::Vec3 b{to.x, to.y, 0.5f};
        if (kind == StaticQueryKind::Naive) {
            for (const math::Aabb3& box : prims_)
                if (SegmentHitsAabb(a, b, box)) return true;
            return false;
        }
        // BVH 팔: 선분의 AABB 로 가지를 쳐 낸 뒤 후보에만 slab 테스트를 건다.
        math::Aabb3 span = math::Aabb3::Empty();
        span.Expand(a);
        span.Expand(b);
        bool blocked = false;
        bvh_.Query(span, [&](int prim) {
            if (!blocked && SegmentHitsAabb(a, b, prims_[static_cast<size_t>(prim)]))
                blocked = true;
        });
        return blocked;
    }
    // snippet-end: segment-blocked

private:
    std::vector<math::Aabb3>       prims_;
    std::vector<DefenseMap::Cell>  cells_;
    math::Bvh                      bvh_;
    uint32_t                       version_ = 0;
};

// 발파: 브러시가 덮은 바위 칸을 연다. 색인은 열린 칸이 있을 때만 다시 짓는다.
// snippet-begin: apply-blast
inline BlastResult ApplyBlast(DefenseMap& map, StaticGeometry& geometry,
                              const BlastBrush& brush, StaticQueryKind kind,
                              std::vector<uint32_t>& scratch,
                              std::vector<DefenseMap::Cell>* openedOut = nullptr) {
    BlastResult result;
    geometry.QueryRegion(brush.Bounds(), kind, scratch);
    result.candidates = static_cast<int>(scratch.size());
    for (uint32_t prim : scratch) {
        const DefenseMap::Cell c = geometry.CellOf(prim);
        const math::Vec2 center = map.CellCenter(c.x, c.y);
        const math::Vec3 p{center.x, center.y, 0.5f};
        // 바위 안이라는 사실을 -1 로 적는다(SDF 규약: 안이 음수). 브러시를 그
        // 장에서 감산하면 브러시 안쪽만 부호가 뒤집혀 열린다.
        const float after = math::OpSubtract(brush.Sd(p), -1.0f);
        if (after < 0.0f) continue;
        if (!map.OpenCell(c.x, c.y)) continue;
        ++result.opened;
        if (openedOut) openedOut->push_back(c);
    }
    if (result.opened > 0) geometry.Rebuild(map);
    return result;
}
// snippet-end: apply-blast

}  // namespace game
}  // namespace mega
