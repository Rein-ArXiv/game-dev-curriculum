// 2D BVH (top-down median split). Step 6 의 시각화 대상.
// 매 빌드 단계의 노드 박스를 trace 하면 단계별 애니메이션 가능.

#pragma once

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mathlab {

struct AABB2D {
    Vector2 min;
    Vector2 max;
    Vector2 Center() const { return {(min.x + max.x) * 0.5f,
                                       (min.y + max.y) * 0.5f}; }
    float   Width()  const { return max.x - min.x; }
    float   Height() const { return max.y - min.y; }
};

inline AABB2D AABBUnion(AABB2D a, AABB2D b) {
    return {{std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y)},
             {std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y)}};
}

// Ray vs AABB 2D (slab test). 분기 있는 안전 버전.
inline bool RayAABB2D(Vector2 ro, Vector2 rd, AABB2D box,
                       float& tNear, float& tFar) {
    tNear = -1e30f;
    tFar  =  1e30f;
    for (int i = 0; i < 2; ++i) {
        float ri  = (i == 0) ? ro.x : ro.y;
        float di  = (i == 0) ? rd.x : rd.y;
        float lo  = (i == 0) ? box.min.x : box.min.y;
        float hi  = (i == 0) ? box.max.x : box.max.y;
        if (std::fabs(di) < 1e-8f) {
            if (ri < lo || ri > hi) return false;
        } else {
            const float magnitude = std::max(std::fabs(di), 1e-8f);
            const float invD = std::copysign(1.0f / magnitude, di);
            float t0 = (lo - ri) * invD;
            float t1 = (hi - ri) * invD;
            if (t0 > t1) std::swap(t0, t1);
            tNear = std::max(tNear, t0);
            tFar  = std::min(tFar,  t1);
            if (tNear > tFar) return false;
        }
    }
    return tFar >= 0;
}

struct BVHNode2D {
    AABB2D bounds;
    int    leftChild  = -1;
    int    rightChild = -1;
    int    firstObj   = 0;
    int    objCount   = 0;
    int    depth      = 0;
};

// 빌드 후 트래버설 시 방문한 노드 trace.
struct RayTraversalResult {
    bool                  hit;
    float                 hitT;
    int                   hitObjIdx;
    std::vector<int>      visitedNodes;
};

class BVH2D {
public:
    void Build(const std::vector<AABB2D>& boxes,
                int leafThreshold = 4);

    const std::vector<BVHNode2D>& Nodes()        const { return nodes_; }
    const std::vector<int>&        ObjectIndex() const { return order_; }

    // 광선 트래버설 — 가까운 hit + 방문 노드 list.
    RayTraversalResult Trace(const std::vector<AABB2D>& boxes,
                               Vector2 ro, Vector2 rd,
                               float tMax = 1e30f) const;

    // 빌드된 전체 노드 수. 시각화의 visited 플래그 배열 크기 / 상태 표시용.
    // (단계별 애니메이션은 이 값이 아니라 노드의 depth 로 거른다.)
    int NumNodes() const { return (int)nodes_.size(); }

private:
    std::vector<BVHNode2D>  nodes_;
    std::vector<int>        order_;     // 객체 인덱스 (median 분할로 재배열)
    int                     leafThreshold_ = 4;

    int BuildRec(const std::vector<AABB2D>& boxes,
                  int begin, int end, int depth);
};

inline int BVH2D::BuildRec(const std::vector<AABB2D>& boxes,
                             int begin, int end, int depth) {
    BVHNode2D node{};
    node.depth = depth;
    AABB2D b = boxes[order_[begin]];
    for (int i = begin + 1; i < end; ++i) {
        b = AABBUnion(b, boxes[order_[i]]);
    }
    node.bounds = b;

    int count = end - begin;
    if (count <= leafThreshold_ || depth > 24) {
        node.firstObj = begin;
        node.objCount = count;
        nodes_.push_back(node);
        return (int)nodes_.size() - 1;
    }

    int axis = (b.Width() >= b.Height()) ? 0 : 1;
    int mid = (begin + end) / 2;
    std::nth_element(order_.begin() + begin,
                      order_.begin() + mid,
                      order_.begin() + end,
                      [&](int a, int c) {
                          float va = (axis == 0)
                                          ? boxes[a].Center().x
                                          : boxes[a].Center().y;
                          float vc = (axis == 0)
                                          ? boxes[c].Center().x
                                          : boxes[c].Center().y;
                          return va < vc;
                      });

    int idx = (int)nodes_.size();
    nodes_.push_back(node);   // placeholder

    int left  = BuildRec(boxes, begin, mid, depth + 1);
    int right = BuildRec(boxes, mid, end,  depth + 1);
    nodes_[idx].leftChild  = left;
    nodes_[idx].rightChild = right;
    return idx;
}

inline void BVH2D::Build(const std::vector<AABB2D>& boxes,
                           int leafThreshold) {
    nodes_.clear();
    order_.clear();
    if (boxes.empty()) return;
    leafThreshold_ = std::max(1, leafThreshold);
    order_.resize(boxes.size());
    for (int i = 0; i < (int)boxes.size(); ++i) order_[i] = i;
    BuildRec(boxes, 0, (int)boxes.size(), 0);
}

inline RayTraversalResult BVH2D::Trace(const std::vector<AABB2D>& boxes,
                                          Vector2 ro, Vector2 rd,
                                          float tMax) const {
    RayTraversalResult r{};
    r.hit       = false;
    r.hitT      = tMax;
    r.hitObjIdx = -1;
    if (nodes_.empty()) return r;

    std::vector<int> stack;
    stack.push_back(0);
    while (!stack.empty()) {
        int idx = stack.back();
        stack.pop_back();
        const BVHNode2D& n = nodes_[(size_t)idx];

        float tn, tf;
        if (!RayAABB2D(ro, rd, n.bounds, tn, tf)) continue;
        if (tn > r.hitT) continue;
        r.visitedNodes.push_back(idx);

        if (n.leftChild < 0) {
            // leaf: 객체별 AABB 검사
            for (int i = 0; i < n.objCount; ++i) {
                int objIdx = order_[(size_t)(n.firstObj + i)];
                float oTn, oTf;
                if (RayAABB2D(ro, rd, boxes[(size_t)objIdx], oTn, oTf)) {
                    float t = std::max(oTn, 0.0f);
                    if (t < r.hitT) {
                        r.hitT = t;
                        r.hit  = true;
                        r.hitObjIdx = objIdx;
                    }
                }
            }
        } else {
            stack.push_back(n.rightChild);
            stack.push_back(n.leftChild);
        }
    }
    return r;
}

}  // namespace mathlab
