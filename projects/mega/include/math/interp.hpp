// interp — 보간 곡선. float·Vec3 등 선형 공간 타입에 공통 적용(템플릿).
//
// 키프레임 사이를 잇는 곡선들이다. Lerp 는 직선, Hermite 는 양 끝점+접선,
// Catmull-Rom 은 제어점을 지나는 부드러운 곡선(접선을 이웃에서 자동 유추),
// Bezier 는 제어점이 당기는 곡선. 카메라 경로·애니메이션 커브·스플라인에 쓴다.
//
// 회전 보간은 선형 공간이 아니므로 여기 두지 않는다 — Slerp 는 quat.hpp 에 있다.

#pragma once

namespace mega {
namespace math {

template <class T>
T Lerp(const T& a, const T& b, float t) { return a + (b - a) * t; }

// smoothstep 이징(0..1): 양 끝에서 속도 0.
inline float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }
inline float SmootherStep(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

// Hermite: 끝점 p0,p1 과 접선 m0,m1. t∈[0,1].
template <class T>
T Hermite(const T& p0, const T& m0, const T& p1, const T& m1, float t) {
    float t2 = t * t, t3 = t2 * t;
    float h00 = 2*t3 - 3*t2 + 1;
    float h10 = t3 - 2*t2 + t;
    float h01 = -2*t3 + 3*t2;
    float h11 = t3 - t2;
    return p0 * h00 + m0 * h10 + p1 * h01 + m1 * h11;
}

// Catmull-Rom: p1→p2 구간을 지나며, 접선을 (p2-p0)/2, (p3-p1)/2 로 잡는다.
template <class T>
T CatmullRom(const T& p0, const T& p1, const T& p2, const T& p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return (p1 * 2.0f
            + (p2 - p0) * t
            + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2
            + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
}

// 3차 베지에: p0,p3 끝점, p1,p2 제어점. t=0→p0, t=1→p3.
template <class T>
T Bezier(const T& p0, const T& p1, const T& p2, const T& p3, float t) {
    float u = 1.0f - t;
    float uu = u * u, tt = t * t;
    return p0 * (uu * u) + p1 * (3.0f * uu * t) + p2 * (3.0f * u * tt) + p3 * (tt * t);
}

}  // namespace math
}  // namespace mega
