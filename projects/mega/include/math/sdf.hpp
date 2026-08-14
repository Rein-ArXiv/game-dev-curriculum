// sdf — 부호 거리 함수(Signed Distance Function).
//
// 한 점에서 형상 표면까지의 부호 거리(안=음수, 밖=양수)를 반환한다. 형상을
// 함수로 표현하면 합집합·차집합·부드러운 블렌딩이 min/max 연산으로 끝나고,
// 레이마칭·충돌 근사·절차적 형상에 쓴다(math 시리즈 SDF 챕터).
//
// 규약: 모든 함수는 형상 로컬 공간의 점 p 를 받는다. 월드 점은 Transform 의
// InverseTransformPoint 로 로컬로 내린 뒤 넣는다.

#pragma once

#include "math/scalar.hpp"
#include "math/vec.hpp"

#include <cmath>

namespace mega {
namespace math {

inline float SdSphere(const Vec3& p, float radius) {
    return Length(p) - radius;
}

// 중심 원점, 반쪽 길이 b 인 박스.
inline float SdBox(const Vec3& p, const Vec3& b) {
    Vec3 d{std::fabs(p.x) - b.x, std::fabs(p.y) - b.y, std::fabs(p.z) - b.z};
    Vec3 dpos = Max(d, Vec3{0, 0, 0});
    float outside = Length(dpos);
    float inside  = std::fmin(std::fmax(d.x, std::fmax(d.y, d.z)), 0.0f);
    return outside + inside;
}

// 선분 a-b 를 반지름 r 로 부풀린 캡슐.
inline float SdCapsule(const Vec3& p, const Vec3& a, const Vec3& b, float r) {
    Vec3 pa = p - a, ba = b - a;
    float h = Clamp(Dot(pa, ba) / Dot(ba, ba), 0.0f, 1.0f);
    return Length(pa - ba * h) - r;
}

// 법선 n(단위), 원점에서의 거리 h 인 평면.
inline float SdPlane(const Vec3& p, const Vec3& n, float h) {
    return Dot(p, n) + h;
}

// 합성 연산.
inline float OpUnion(float a, float b)     { return std::fmin(a, b); }
inline float OpIntersect(float a, float b) { return std::fmax(a, b); }
inline float OpSubtract(float a, float b)  { return std::fmax(-a, b); }   // b 에서 a 를 깎음

// 부드러운 합집합(k = 블렌드 폭). 경계가 매끈하게 이어진다.
inline float OpSmoothUnion(float a, float b, float k) {
    float h = Saturate(0.5f + 0.5f * (b - a) / k);
    return Lerp(b, a, h) - k * h * (1.0f - h);
}

}  // namespace math
}  // namespace mega
