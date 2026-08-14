// quat — 단위 쿼터니언. 회전의 1급 표현.
//
// 회전 행렬은 9개 수에 직교 제약이 숨어 누적 시 어긋나고(재정규화 필요),
// 오일러각은 김벌락이 있다. 단위 쿼터니언은 4개 수로 회전을 닫힌 형태로 담고,
// 보간(slerp)이 일정 각속도로 부드럽다. 누적·보간·직렬화는 쿼터니언으로 하고,
// 행렬은 필요할 때 펼친다(math 시리즈 결론).
//
// 규약: q = (x, y, z, w), w 가 스칼라부. 곱 a*b 는 "b 를 먼저, a 를 나중" 적용.

#pragma once

#include "math/mat.hpp"
#include "math/vec.hpp"

#include <cmath>

namespace mega {
namespace math {

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;   // 기본 = 단위(회전 없음)

    Quat() = default;
    Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static Quat Identity() { return Quat{}; }

    static Quat FromAxisAngle(const Vec3& axis, float angle) {
        Vec3 a = Normalize(axis);
        float h = angle * 0.5f;
        float s = std::sin(h);
        return {a.x * s, a.y * s, a.z * s, std::cos(h)};
    }

    // 오일러각(라디안) → 쿼터니언. 순서 Y(yaw)·X(pitch)·Z(roll) 합성.
    static Quat FromEuler(float pitch, float yaw, float roll) {
        Quat qx = FromAxisAngle({1, 0, 0}, pitch);
        Quat qy = FromAxisAngle({0, 1, 0}, yaw);
        Quat qz = FromAxisAngle({0, 0, 1}, roll);
        return qy * qx * qz;
    }

    Quat operator*(const Quat& b) const {
        return {
            w * b.x + x * b.w + y * b.z - z * b.y,
            w * b.y - x * b.z + y * b.w + z * b.x,
            w * b.z + x * b.y - y * b.x + z * b.w,
            w * b.w - x * b.x - y * b.y - z * b.z,
        };
    }
    Quat operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    Quat operator+(const Quat& b) const { return {x + b.x, y + b.y, z + b.z, w + b.w}; }
};

inline float Dot(const Quat& a, const Quat& b) { return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w; }
inline float Length(const Quat& q) { return std::sqrt(Dot(q, q)); }
inline Quat  Normalize(const Quat& q) {
    float l = Length(q);
    if (l < kEpsilon) return Quat::Identity();
    float inv = 1.0f / l;
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}
inline Quat Conjugate(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }   // 단위면 역원

// 벡터 회전: v' = v + 2w(q×v) + 2 q×(q×v). q*v*q^-1 의 효율 형태.
inline Vec3 Rotate(const Quat& q, const Vec3& v) {
    Vec3 u{q.x, q.y, q.z};
    Vec3 t = Cross(u, v) * 2.0f;
    return v + t * q.w + Cross(u, t);
}

// 단위 쿼터니언 → 회전 행렬(Mat3, 열우선).
inline Mat3 ToMat3(const Quat& q) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float xx = x*x, yy = y*y, zz = z*z;
    float xy = x*y, xz = x*z, yz = y*z;
    float wx = w*x, wy = w*y, wz = w*z;
    Mat3 r;
    r.At(0,0) = 1 - 2*(yy + zz); r.At(0,1) = 2*(xy - wz);     r.At(0,2) = 2*(xz + wy);
    r.At(1,0) = 2*(xy + wz);     r.At(1,1) = 1 - 2*(xx + zz); r.At(1,2) = 2*(yz - wx);
    r.At(2,0) = 2*(xz - wy);     r.At(2,1) = 2*(yz + wx);     r.At(2,2) = 1 - 2*(xx + yy);
    return r;
}
inline Mat4 ToMat4(const Quat& q) { return Mat4::FromMat3(ToMat3(q)); }

// 구면 선형 보간: 최단 경로 + 일정 각속도. 매우 가까우면 nlerp 로 폴백(0 나눗셈 회피).
inline Quat Slerp(const Quat& a, Quat b, float t) {
    float d = Dot(a, b);
    if (d < 0.0f) { b = b * -1.0f; d = -d; }   // 최단 호 선택(q 와 -q 는 같은 회전)
    if (d > 0.9995f) {                          // 거의 평행 → 선형 보간 후 정규화
        Quat r = a + (b + a * -1.0f) * t;
        return Normalize(r);
    }
    float theta0 = std::acos(d);
    float theta  = theta0 * t;
    float sin0   = std::sin(theta0);
    float s0 = std::sin(theta0 - theta) / sin0;
    float s1 = std::sin(theta) / sin0;
    return {a.x*s0 + b.x*s1, a.y*s0 + b.y*s1, a.z*s0 + b.z*s1, a.w*s0 + b.w*s1};
}

// 정규화 선형 보간(slerp 보다 싸고, 작은 각도에서 충분).
inline Quat Nlerp(const Quat& a, Quat b, float t) {
    if (Dot(a, b) < 0.0f) b = b * -1.0f;
    return Normalize(a + (b + a * -1.0f) * t);
}

}  // namespace math
}  // namespace mega
