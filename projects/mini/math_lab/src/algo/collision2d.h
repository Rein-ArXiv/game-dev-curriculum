// 2D OBB-OBB SAT + 2D GJK. Step 5 의 시각화 대상.
//
// SAT: 두 OBB 의 4 분리축 후보 (각 OBB 의 두 면 법선) 에 투영.
// GJK: 2D 에서는 simplex 가 점 → 선분 → 삼각형 으로 진화. 삼각형이 원점을
//      품으면 종료, 아니면 한 정점을 버리고 선분으로 되돌아가 반복한다.
//
// math part12 / part13 의 식들을 시각화에 적합한 형태로.

#pragma once

#include "raylib.h"

#include <array>
#include <cmath>
#include <vector>

namespace mathlab {

struct OBB2D {
    Vector2 center;
    float   halfX;
    float   halfY;
    float   angle;   // radian

    std::array<Vector2, 2> Axes() const {
        float c = std::cos(angle);
        float s = std::sin(angle);
        return {Vector2{c, s}, Vector2{-s, c}};
    }

    std::array<Vector2, 4> Corners() const {
        auto ax = Axes();
        Vector2 ex = {ax[0].x * halfX, ax[0].y * halfX};
        Vector2 ey = {ax[1].x * halfY, ax[1].y * halfY};
        return {
            Vector2{center.x - ex.x - ey.x, center.y - ex.y - ey.y},
            Vector2{center.x + ex.x - ey.x, center.y + ex.y - ey.y},
            Vector2{center.x + ex.x + ey.x, center.y + ex.y + ey.y},
            Vector2{center.x - ex.x + ey.x, center.y - ex.y + ey.y},
        };
    }
};

inline float Dot2(Vector2 a, Vector2 b) { return a.x * b.x + a.y * b.y; }
inline Vector2 Sub2(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vector2 Neg2(Vector2 a)            { return {-a.x, -a.y}; }
// 2D 외적의 z 성분. GJK 의 삼각형 케이스에서 원점이 어느 변의 바깥인지 판정한다.
inline float TripleZ(Vector2 a, Vector2 b) { return a.x * b.y - a.y * b.x; }

// ==== SAT ====
struct SATAxisInfo {
    Vector2 axis;
    float   minA, maxA, minB, maxB;
    bool    separating;
    float   overlap;          // 분리되어 있으면 음수
};

inline SATAxisInfo SATEvaluateAxis(const std::array<Vector2, 4>& a,
                                     const std::array<Vector2, 4>& b,
                                     Vector2 axis) {
    SATAxisInfo r{};
    r.axis = axis;
    r.minA = r.maxA = Dot2(a[0], axis);
    r.minB = r.maxB = Dot2(b[0], axis);
    for (int i = 1; i < 4; ++i) {
        float pa = Dot2(a[i], axis);
        float pb = Dot2(b[i], axis);
        if (pa < r.minA) r.minA = pa; if (pa > r.maxA) r.maxA = pa;
        if (pb < r.minB) r.minB = pb; if (pb > r.maxB) r.maxB = pb;
    }
    float overlap = std::min(r.maxA, r.maxB) - std::max(r.minA, r.minB);
    r.overlap    = overlap;
    r.separating = (overlap < 0);
    return r;
}

struct SATResult {
    bool                       intersect;
    std::vector<SATAxisInfo>   axes;
    int                        mtvIdx;     // 최소 overlap 의 축 (intersect 시)
};

inline SATResult OBBOBBSat2D(const OBB2D& A, const OBB2D& B) {
    SATResult r{};
    auto cornersA = A.Corners();
    auto cornersB = B.Corners();
    auto axesA    = A.Axes();
    auto axesB    = B.Axes();

    Vector2 candidates[4] = {axesA[0], axesA[1], axesB[0], axesB[1]};
    r.axes.reserve(4);

    bool intersect = true;
    int  bestIdx   = -1;
    float bestOverlap = 1e30f;
    for (int i = 0; i < 4; ++i) {
        SATAxisInfo info = SATEvaluateAxis(cornersA, cornersB, candidates[i]);
        r.axes.push_back(info);
        if (info.separating) intersect = false;
        else if (info.overlap < bestOverlap) {
            bestOverlap = info.overlap;
            bestIdx     = i;
        }
    }
    r.intersect = intersect;
    r.mtvIdx    = intersect ? bestIdx : -1;
    return r;
}

// ==== GJK 2D ====
// 두 도형의 지원 함수만 알면 동작. 이 lab 은 OBB 만 다루므로 OBB 의 지원 함수.
// snippet-begin: gjk-support
inline Vector2 OBBSupport2D(const OBB2D& obb, Vector2 d) {
    auto axes = obb.Axes();
    float dx = Dot2(d, axes[0]);
    float dy = Dot2(d, axes[1]);
    float sx = (dx >= 0) ? obb.halfX : -obb.halfX;
    float sy = (dy >= 0) ? obb.halfY : -obb.halfY;
    return {
        obb.center.x + axes[0].x * sx + axes[1].x * sy,
        obb.center.y + axes[0].y * sx + axes[1].y * sy,
    };
}

inline Vector2 MinkowskiSupport2D(const OBB2D& a, const OBB2D& b, Vector2 d) {
    Vector2 p1 = OBBSupport2D(a, d);
    Vector2 p2 = OBBSupport2D(b, Neg2(d));
    return Sub2(p1, p2);
}
// snippet-end: gjk-support

struct GJKStep {
    std::vector<Vector2> simplex;   // 이 단계의 simplex
    Vector2              direction; // 다음 search direction
    Vector2              newPoint;  // 이 단계에서 추가된 지원점
    bool                 done;
    bool                 intersect;
    int                  iter;
};

// 시각화를 위해 step 별로 simplex 변화를 기록.
inline std::vector<GJKStep> GJK2DTraced(const OBB2D& A, const OBB2D& B,
                                          int maxIter = 32) {
    std::vector<GJKStep> trace;
    Vector2 d = {1, 0};   // 초기 임의 방향
    Vector2 a = MinkowskiSupport2D(A, B, d);

    GJKStep first{};
    first.simplex   = {a};
    first.newPoint  = a;
    first.direction = Neg2(a);
    first.done      = false;
    first.intersect = false;
    first.iter      = 0;
    trace.push_back(first);

    d = Neg2(a);
    if (d.x * d.x + d.y * d.y < 1e-12f) d = {1, 0};
    std::vector<Vector2> S = {a};

    for (int i = 1; i <= maxIter; ++i) {
        Vector2 newP = MinkowskiSupport2D(A, B, d);
        if (Dot2(newP, d) < 0) {
            // 분리됨
            GJKStep s{};
            s.simplex   = S;
            s.direction = d;
            s.newPoint  = newP;
            s.done      = true;
            s.intersect = false;
            s.iter      = i;
            trace.push_back(s);
            return trace;
        }
        S.push_back(newP);

        // simplex 진화
        if (S.size() == 2) {
            Vector2 a2 = S[1], b2 = S[0];
            Vector2 ab = Sub2(b2, a2);
            Vector2 ao = Neg2(a2);
            // 2D: ab 에 수직인 방향 중 ao 쪽
            Vector2 perp = {-ab.y, ab.x};
            if (Dot2(perp, ao) < 0) perp = {ab.y, -ab.x};
            d = perp;
            if (d.x * d.x + d.y * d.y < 1e-12f) {
                // ab 와 ao 가 평행 — 임의 수직축
                d = {-ab.y, ab.x};
            }
        } else if (S.size() == 3) {
            // 삼각형 — 원점 포함 검사. 표준 2D GJK 형태:
            //   원점이 edge AB 의 "C 반대편 (= 외부)" 에 있는가?
            //   triple-product 부호로 결정 — origin 과 C 가 같은 쪽이면 내부.
            //
            // 변마다: edge 벡터를 e, opposite vertex 를 V 라 할 때,
            //   sign(cross2D(e, AO))  vs  sign(cross2D(e, AV_opp))
            // 두 부호가 다르면 원점이 그 변의 외부 — opposite vertex 버림.
            Vector2 a2 = S[2], b2 = S[1], c2 = S[0];
            Vector2 ao = Neg2(a2);

            // 변 AB, 반대편 정점 C
            Vector2 ab = Sub2(b2, a2);
            Vector2 ac = Sub2(c2, a2);
            float   sAB_origin = TripleZ(ab, ao);
            float   sAB_C      = TripleZ(ab, ac);

            // 변 AC, 반대편 정점 B
            float   sAC_origin = TripleZ(ac, ao);
            float   sAC_B      = TripleZ(ac, ab);

            // edge 외향 법선 (origin 쪽으로 향함)
            auto outwardNormalOf = [](Vector2 e, Vector2 toOpp) {
                // perpendicular 두 가지 중 opposite vertex 와 반대편 향한 쪽.
                Vector2 p = {-e.y, e.x};
                if (TripleZ(e, p) * TripleZ(e, toOpp) > 0) p = {e.y, -e.x};
                return p;
            };

            bool outsideAB = (sAB_origin * sAB_C) < 0;
            bool outsideAC = (sAC_origin * sAC_B) < 0;

            if (outsideAB) {
                // 원점이 AB 외부 — C 버림
                Vector2 n = outwardNormalOf(ab, ac);
                S = {b2, a2};
                d = n;
            } else if (outsideAC) {
                // 원점이 AC 외부 — B 버림
                Vector2 n = outwardNormalOf(ac, ab);
                S = {c2, a2};
                d = n;
            } else {
                // 두 변 모두 안쪽 → 원점이 삼각형 내부.
                GJKStep s{};
                s.simplex   = S;
                s.direction = d;
                s.newPoint  = newP;
                s.done      = true;
                s.intersect = true;
                s.iter      = i;
                trace.push_back(s);
                return trace;
            }
        }

        GJKStep s{};
        s.simplex   = S;
        s.direction = d;
        s.newPoint  = newP;
        s.done      = false;
        s.intersect = false;
        s.iter      = i;
        trace.push_back(s);
    }
    // 최대 반복 도달 — 안전 종료
    GJKStep last{};
    last.simplex   = S;
    last.direction = d;
    last.done      = true;
    last.intersect = false;
    last.iter      = maxIter;
    trace.push_back(last);
    return trace;
}

}  // namespace mathlab
