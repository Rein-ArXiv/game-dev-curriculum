// ik — 2본 역운동학(평면). 발·손을 목표에 맞춘다.
//
// 다리·팔은 대부분 2본(허벅지-정강이)이라 코사인 법칙으로 *닫힌 해* 를 즉시 구한다.
// 루트에서 목표까지 거리 d 와 두 본 길이 l1,l2 로, 루트 관절 각도를 코사인 법칙으로
// 계산해 무릎(joint) 위치를 정하고 끝(end)이 목표에 닿게 한다. 도달 불가면 목표
// 방향으로 최대한 뻗는다(engine IK 챕터). 임의 체인은 CCD 로 반복(SolveCCD).

#pragma once

#include "math/scalar.hpp"
#include "math/vec.hpp"

#include <cmath>
#include <vector>

namespace mega {
namespace engine {

struct IkResult {
    math::Vec2 joint{0, 0};
    math::Vec2 end{0, 0};
    bool       reachable = false;
};

// 평면 2본 IK. bendPositive 면 관절이 +방향(왼쪽)으로 굽는다.
inline IkResult SolveTwoBone2D(const math::Vec2& root, float l1, float l2,
                               const math::Vec2& target, bool bendPositive = true) {
    IkResult r;
    math::Vec2 toTarget = target - root;
    float d = math::Length(toTarget);

    if (d < 1e-5f) { r.joint = root + math::Vec2{l1, 0}; r.end = r.joint; return r; }
    math::Vec2 dir = toTarget * (1.0f / d);
    math::Vec2 perp{-dir.y, dir.x};

    float reach = l1 + l2;
    if (d >= reach) {                               // 도달 불가 → 곧게 뻗음
        r.joint = root + dir * l1;
        r.end   = root + dir * reach;
        r.reachable = false;
        return r;
    }
    if (d <= std::fabs(l1 - l2)) {                  // 너무 가까움 → 접힘
        r.joint = root + dir * l1;
        r.end   = root + dir * (l1 - l2);
        r.reachable = false;
        return r;
    }

    // 코사인 법칙: 루트에서 본1 까지의 각.
    float cosA = (l1 * l1 + d * d - l2 * l2) / (2.0f * l1 * d);
    cosA = math::Clamp(cosA, -1.0f, 1.0f);
    float h = l1 * std::sqrt(1.0f - cosA * cosA);   // 목표선에서 관절까지 수직 거리
    float a = l1 * cosA;                            // 목표선 방향 성분
    float sign = bendPositive ? 1.0f : -1.0f;
    r.joint = root + dir * a + perp * (h * sign);
    r.end = target;                                 // 닿음(검증으로 확인)
    r.reachable = true;
    return r;
}

}  // namespace engine
}  // namespace mega
