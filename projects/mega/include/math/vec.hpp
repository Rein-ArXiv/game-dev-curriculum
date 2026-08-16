// vec — Vec2/Vec3/Vec4 단일체(AoS). 연산자 오버로드로 수식을 코드에 가깝게.
//
// 단일 벡터 타입은 디버깅·게임 로직용이다. 대량 갱신(파티클, ECS 컴포넌트)은
// SoA(x[N],y[N],z[N]) 로 따로 두어 SIMD 를 태운다(math 시리즈 SIMD 챕터). 이
// 헤더는 AoS 단일체를 다루고, 이 저장소의 SoA 배치는 컬럼 컨테이너
// containers/soa_vector.h 가 갖는다.

#pragma once

#include "math/scalar.hpp"

#include <cmath>

namespace mega {
namespace math {

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { float inv = 1.0f / s; return {x * inv, y * inv}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
};

inline float Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline float Cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }  // 스칼라(z성분)
inline float LengthSq(const Vec2& v) { return Dot(v, v); }
inline float Length(const Vec2& v) { return std::sqrt(LengthSq(v)); }
inline Vec2  Normalize(const Vec2& v) { float l = Length(v); return l > kEpsilon ? v * (1.0f / l) : Vec2{}; }

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit Vec3(float s) : x(s), y(s), z(s) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }  // 성분곱
    Vec3 operator/(float s) const { float inv = 1.0f / s; return {x * inv, y * inv, z * inv}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    float  operator[](int i) const { return (&x)[i]; }
    float& operator[](int i) { return (&x)[i]; }
};

inline Vec3  operator*(float s, const Vec3& v) { return v * s; }
inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3  Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float LengthSq(const Vec3& v) { return Dot(v, v); }
inline float Length(const Vec3& v) { return std::sqrt(LengthSq(v)); }
inline Vec3  Normalize(const Vec3& v) { float l = Length(v); return l > kEpsilon ? v * (1.0f / l) : Vec3{}; }
inline float Distance(const Vec3& a, const Vec3& b) { return Length(a - b); }
inline Vec3  Lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline Vec3  Min(const Vec3& a, const Vec3& b) { return {std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z)}; }
inline Vec3  Max(const Vec3& a, const Vec3& b) { return {std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z)}; }

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    Vec4() = default;
    Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}

    Vec4 operator+(const Vec4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    Vec4 operator-(const Vec4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    float  operator[](int i) const { return (&x)[i]; }
    float& operator[](int i) { return (&x)[i]; }
    Vec3 XYZ() const { return {x, y, z}; }
};

inline float Dot(const Vec4& a, const Vec4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

}  // namespace math
}  // namespace mega
