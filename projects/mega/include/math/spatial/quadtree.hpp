// quadtree — 2D 점 영역 쿼드트리. 동적 객체의 이웃 쿼리에.
//
// 한 노드에 점이 maxItems 를 넘으면 4분할해 점을 자식으로 내린다. 영역/반경
// 쿼리는 겹치지 않는 가지를 통째로 가지치기해, 균등 분포에서 평균 O(log n + k)
// 로 이웃을 찾는다(k = 결과 수). 갱신이 잦은 군집 시뮬의 근접 탐색에 맞는다.
//
// 노드를 벡터 풀에 두고 자식을 인덱스로 가리킨다(포인터 추적·캐시 미스 감소).
// 풀이 재할당될 수 있어 Node& 를 push_back 너머로 들고 다니지 않는다.

#pragma once

#include "math/spatial/aabb.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

namespace mega {
namespace math {

template <class T>
class Quadtree {
public:
    void Init(const Aabb2& bounds, int maxItems = 8, int maxDepth = 8) {
        maxItems_ = maxItems;
        maxDepth_ = maxDepth;
        nodes_.clear();
        nodes_.push_back(Node{bounds, 0});
        size_ = 0;
    }

    void Clear() {
        Aabb2 b = nodes_.empty() ? Aabb2{} : nodes_[0].bounds;
        Init(b, maxItems_, maxDepth_);
    }

    void Insert(const Vec2& p, const T& data) {
        InsertInto(0, p, data);
        ++size_;
    }

    // 영역 쿼리: region 안에 든 모든 항목에 fn(const T&) 호출.
    template <class Fn>
    void Query(const Aabb2& region, Fn&& fn) const {
        QueryNode(0, region, fn);
    }

    // 반경 쿼리: center 에서 radius 안의 항목에 fn(const T&).
    template <class Fn>
    void QueryRadius(const Vec2& center, float radius, Fn&& fn) const {
        Aabb2 box = Aabb2::FromCenterHalf(center, {radius, radius});
        float r2 = radius * radius;
        QueryNode(0, box, [&](const Vec2& p, const T& d) {
            float dx = p.x - center.x, dy = p.y - center.y;
            if (dx * dx + dy * dy <= r2) fn(d);
        }, true);
    }

    size_t Size() const { return size_; }
    size_t NodeCount() const { return nodes_.size(); }

private:
    struct Item { Vec2 pos; T data; };
    struct Node {
        Aabb2            bounds;
        int              depth = 0;
        int              child[4] = {-1, -1, -1, -1};
        std::vector<Item> items;
        bool IsLeaf() const { return child[0] == -1; }
    };

    int Quadrant(const Vec2& c, const Vec2& p) const {
        return (p.x >= c.x ? 1 : 0) + (p.y >= c.y ? 2 : 0);
    }

    void Subdivide(int idx) {
        Aabb2 b   = nodes_[idx].bounds;     // 값 복사(이후 push_back 으로 풀 재할당 가능)
        int   dep = nodes_[idx].depth;
        Vec2  c   = b.Center();
        Aabb2 quad[4] = {
            {{b.min.x, b.min.y}, {c.x, c.y}},      // 0: 좌하
            {{c.x, b.min.y}, {b.max.x, c.y}},      // 1: 우하
            {{b.min.x, c.y}, {c.x, b.max.y}},      // 2: 좌상
            {{c.x, c.y}, {b.max.x, b.max.y}},      // 3: 우상
        };
        int base = (int)nodes_.size();
        for (int q = 0; q < 4; ++q) nodes_.push_back(Node{quad[q], dep + 1});
        for (int q = 0; q < 4; ++q) nodes_[idx].child[q] = base + q;

        // 기존 항목을 자식으로 분배.
        std::vector<Item> moved = std::move(nodes_[idx].items);
        nodes_[idx].items.clear();
        for (const Item& it : moved) {
            int q = Quadrant(c, it.pos);
            nodes_[nodes_[idx].child[q]].items.push_back(it);
        }
    }

    void InsertInto(int idx, const Vec2& p, const T& data) {
        // 분할되지 않은 가지를 만날 때까지 내려간다.
        while (!nodes_[idx].IsLeaf()) {
            Vec2 c = nodes_[idx].bounds.Center();
            idx = nodes_[idx].child[Quadrant(c, p)];
        }
        // 리프: 용량 여유가 있거나 최대 깊이면 그냥 담는다.
        if ((int)nodes_[idx].items.size() < maxItems_ || nodes_[idx].depth >= maxDepth_) {
            nodes_[idx].items.push_back(Item{p, data});
            return;
        }
        // 넘치면 분할 후 해당 자식에 삽입.
        Subdivide(idx);
        Vec2 c = nodes_[idx].bounds.Center();
        InsertInto(nodes_[idx].child[Quadrant(c, p)], p, data);
    }

    template <class Fn>
    void QueryNode(int idx, const Aabb2& region, Fn&& fn, bool wantPos = false) const {
        const Node& nd = nodes_[idx];
        if (!nd.bounds.Intersects(region)) return;
        if (nd.IsLeaf()) {
            for (const Item& it : nd.items)
                if (region.Contains(it.pos)) {
                    if constexpr (std::is_invocable_v<Fn, const Vec2&, const T&>) fn(it.pos, it.data);
                    else fn(it.data);
                }
            (void)wantPos;
            return;
        }
        for (int q = 0; q < 4; ++q) QueryNode(nd.child[q], region, fn, wantPos);
    }

    std::vector<Node> nodes_;
    int               maxItems_ = 8;
    int               maxDepth_ = 8;
    size_t            size_     = 0;
};

}  // namespace math
}  // namespace mega
