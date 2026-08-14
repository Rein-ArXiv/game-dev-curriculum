// scalar — 부동소수점 기본기: 상수, 각도 변환, 허용 오차 비교, fast 함수.
//
// 핵심 규약: 부동소수점은 `==` 로 비교하지 않는다. 같은 수식도 연산 순서·SIMD
// 여부에 따라 마지막 비트가 흔들리기 때문이다. 비교는 상대/절대 허용 오차나
// ULP 거리로만 한다(math 시리즈 부동소수점 챕터의 결론). 이 헤더가 그 도구를
// 제공하고, 위의 모든 수학 타입의 테스트가 이걸 쓴다.

#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace mega {
namespace math {

constexpr float kPi      = 3.14159265358979323846f;
constexpr float kTwoPi   = 6.28318530717958647692f;
constexpr float kHalfPi  = 1.57079632679489661923f;
constexpr float kInvPi   = 0.31830988618379067154f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kRad2Deg = 180.0f / kPi;
constexpr float kEpsilon = 1e-6f;

inline float Radians(float deg) { return deg * kDeg2Rad; }
inline float Degrees(float rad) { return rad * kRad2Deg; }

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Saturate(float v) { return Clamp(v, 0.0f, 1.0f); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float Sign(float x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); }
inline float Sqr(float x) { return x * x; }

// 상대 + 절대 허용 오차 비교. 0 근처는 절대 오차가, 큰 값은 상대 오차가 지배.
inline bool AlmostEqual(float a, float b, float relEps = 1e-5f, float absEps = 1e-6f) {
    float diff = std::fabs(a - b);
    if (diff <= absEps) return true;
    float largest = std::fmax(std::fabs(a), std::fabs(b));
    return diff <= relEps * largest;
}

// ULP(Unit in the Last Place) 거리 비교. 두 float 사이에 표현 가능한 float 가
// 몇 개 끼어 있는지로 근접도를 잰다. 부호 있는 정수 사상으로 단조성을 만든다.
inline bool AlmostEqualUlp(float a, float b, int maxUlps = 4) {
    if (a == b) return true;                       // +0 == -0 등 빠른 경로
    int32_t ia, ib;
    std::memcpy(&ia, &a, 4);
    std::memcpy(&ib, &b, 4);
    if ((ia < 0) != (ib < 0)) return false;        // 부호 다르면(0 근처 제외) 불일치
    // 부호 비트를 2의 보수 순서로 변환.
    if (ia < 0) ia = 0x80000000 - ia;
    if (ib < 0) ib = 0x80000000 - ib;
    int32_t ulps = ia > ib ? ia - ib : ib - ia;
    return ulps <= maxUlps;
}

// 빠른 역제곱근(Quake III) + 뉴턴 1스텝. 정확도가 필요 없는 정규화 근사에.
inline float FastInvSqrt(float x) {
    float xhalf = 0.5f * x;
    int32_t i;
    std::memcpy(&i, &x, 4);
    i = 0x5f3759df - (i >> 1);                     // 마법의 초기 추정
    std::memcpy(&x, &i, 4);
    x = x * (1.5f - xhalf * x * x);                // 뉴턴 1회
    return x;
}

}  // namespace math
}  // namespace mega
