// 테스트 안의 *독립 참조 구현* — SAT / GJK+EPA 어느 쪽 코드도 쓰지 않는다.
//
// oracle 을 구현의 사본으로 만들면 같은 오류가 양쪽에 들어간다. 그래서 이 파일은
// 제품의 `SATCollide` 를 부르지 않고 두 값을 처음부터 다시 계산한다.
//
//   intersect : 축 위 두 투영 구간의 *교집합 길이* 의 최소
//   push      : 두 도형을 분리하는 데 필요한 *최소 이동 거리* = 진짜 MTD
//   push2     : 그 다음으로 작은 밀어내기 — 최소 축이 유일한지 판정에 쓴다
//
// 한 구간이 다른 구간을 *포함* 하면 (깊은 겹침) intersect 와 push 가 갈라진다.
// 과거 `SATCollide` 의 결함이 정확히 그 자리에 있었다.
//
// 두 테스트 실행 파일이 이 한 벌을 함께 쓴다. 참조를 두 벌 두면 둘이 따로 썩는다.

#pragma once

#include "scenes/narrow_phase_scene.h"

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace physlab::testkit {

struct AxisReference {
    bool  hit       = false;
    float intersect = 0.0f;
    float push      = 0.0f;
    float push2     = 0.0f;   // 두 번째로 작은 밀어내기 (없으면 push 와 같다)
};

inline AxisReference ReferenceDepths(const physlab::Polygon2D& a,
                                     const physlab::Polygon2D& b) {
    const auto va = a.WorldVerts();
    const auto vb = b.WorldVerts();
    AxisReference r;
    r.hit       = true;
    r.intersect = 1e30f;
    r.push      = 1e30f;
    r.push2     = 1e30f;

    auto project = [](const std::vector<Vector2>& v, Vector2 axis,
                      float& mn, float& mx) {
        mn = mx = v[0].x * axis.x + v[0].y * axis.y;
        for (size_t i = 1; i < v.size(); ++i) {
            const float d = v[i].x * axis.x + v[i].y * axis.y;
            mn = std::min(mn, d);
            mx = std::max(mx, d);
        }
    };

    // 후보 하나를 push / push2 에 반영한다. 두 밀어내기 방향은 서로 다른
    // 후보다 — 대칭 도형에서 둘이 같은 값을 내면 최소 축은 유일하지 않다.
    auto offer = [&r](float value) {
        if (value < r.push) {
            r.push2 = r.push;
            r.push  = value;
        } else if (value < r.push2) {
            r.push2 = value;
        }
    };

    auto runAxes = [&](const std::vector<Vector2>& v) {
        const int n = (int)v.size();
        for (int i = 0; i < n && r.hit; ++i) {
            const Vector2 p = v[(size_t)i];
            const Vector2 q = v[(size_t)((i + 1) % n)];
            Vector2 axis{-(q.y - p.y), q.x - p.x};
            const float len = std::sqrt(axis.x * axis.x + axis.y * axis.y);
            if (len < 1e-9f) continue;
            axis = {axis.x / len, axis.y / len};

            float amn, amx, bmn, bmx;
            project(va, axis, amn, amx);
            project(vb, axis, bmn, bmx);
            if (amx < bmn || bmx < amn) { r.hit = false; return; }

            r.intersect = std::min(r.intersect,
                                   std::min(amx, bmx) - std::max(amn, bmn));
            offer(amx - bmn);
            offer(bmx - amn);
        }
    };

    runAxes(va);
    if (r.hit) runAxes(vb);
    if (!r.hit) { r.intersect = 0.0f; r.push = 0.0f; r.push2 = 0.0f; }
    if (r.push2 > 1e29f) r.push2 = r.push;
    return r;
}

}  // namespace physlab::testkit
