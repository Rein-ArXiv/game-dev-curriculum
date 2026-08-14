// bvh — 경계 볼륨 계층(Bounding Volume Hierarchy). 3D 정적/준정적 광역 탐색.
//
// 프리미티브 AABB 들을 트리로 묶어, 영역/레이 쿼리가 겹치지 않는 가지를 통째로
// 버린다. 여기서는 최장축 median 분할로 빌드한다(구현이 단순하고 균형이 좋음).
// 프로덕션은 SAH(Surface Area Heuristic)로 빈을 나눠 분할 비용을 최소화하지만,
// 그 측정·튜닝은 math_lab 에 둔다(README 결정). 트리 노드는 벡터 풀에 평탄하게
// 저장하고 자식을 인덱스로 가리킨다.

#pragma once

#include "math/spatial/aabb.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace mega {
namespace math {

class Bvh {
public:
    void Build(const std::vector<Aabb3>& prims, int leafSize = 4) {
        prims_ = prims;
        order_.resize(prims_.size());
        for (size_t i = 0; i < order_.size(); ++i) order_[i] = (int)i;
        nodes_.clear();
        leafSize_ = leafSize < 1 ? 1 : leafSize;
        root_ = prims_.empty() ? -1 : BuildRecursive(0, (int)prims_.size());
    }

    // 영역 쿼리: region 과 AABB 가 겹치는 프리미티브 인덱스마다 fn(int) 호출.
    template <class Fn>
    void Query(const Aabb3& region, Fn&& fn) const {
        if (root_ >= 0) QueryNode(root_, region, fn);
    }

    size_t NodeCount() const { return nodes_.size(); }
    size_t PrimCount() const { return prims_.size(); }

private:
    struct Node {
        Aabb3 bounds;
        int   left = -1, right = -1;   // 내부 노드 자식
        int   start = 0, count = 0;    // 리프 프리미티브 범위(order_ 기준)
        bool  IsLeaf() const { return left == -1; }
    };

    static float Axis(const Vec3& v, int a) { return v[a]; }

    int BuildRecursive(int start, int count) {
        Aabb3 b = Aabb3::Empty();
        for (int i = start; i < start + count; ++i) b.Expand(prims_[order_[i]]);

        if (count <= leafSize_) {
            Node n;
            n.bounds = b; n.left = -1; n.start = start; n.count = count;
            nodes_.push_back(n);
            return (int)nodes_.size() - 1;
        }

        // 센트로이드 경계의 최장축에서 median 으로 가른다.
        Aabb3 cb = Aabb3::Empty();
        for (int i = start; i < start + count; ++i) cb.Expand(prims_[order_[i]].Center());
        int axis = cb.LongestAxis();
        int mid  = start + count / 2;
        std::nth_element(order_.begin() + start, order_.begin() + mid, order_.begin() + start + count,
            [&](int a, int c) { return Axis(prims_[a].Center(), axis) < Axis(prims_[c].Center(), axis); });

        int l = BuildRecursive(start, mid - start);
        int r = BuildRecursive(mid, start + count - mid);
        Node n;
        n.bounds = b; n.left = l; n.right = r;   // 자식 인덱스(이미 push 됨)
        nodes_.push_back(n);
        return (int)nodes_.size() - 1;
    }

    template <class Fn>
    void QueryNode(int idx, const Aabb3& region, Fn&& fn) const {
        const Node& n = nodes_[idx];
        if (!n.bounds.Intersects(region)) return;
        if (n.IsLeaf()) {
            for (int i = n.start; i < n.start + n.count; ++i)
                if (prims_[order_[i]].Intersects(region)) fn(order_[i]);
            return;
        }
        QueryNode(n.left, region, fn);
        QueryNode(n.right, region, fn);
    }

    std::vector<Aabb3> prims_;
    std::vector<int>   order_;
    std::vector<Node>  nodes_;
    int                root_     = -1;
    int                leafSize_ = 4;
};

}  // namespace math
}  // namespace mega
