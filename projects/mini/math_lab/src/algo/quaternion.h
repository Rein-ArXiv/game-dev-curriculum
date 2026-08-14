// math_lab 의 쿼터니언 / 회전 유틸. raylib 의 Quaternion 은 Vector4 별칭이라
// 직접 산수만 가능. 의미 있는 함수만 우리가 정의한다.
//
// 표준 컨벤션:
//   q = (x, y, z, w),  norm = sqrt(x²+y²+z²+w²)
//   회전 각 θ, 단위 축 axis  →  q = (sin(θ/2)·axis, cos(θ/2))
//   q1 ⊗ q2  → q2 회전을 먼저, q1 회전을 나중에 적용 (active rotation, v' = q v q⁻¹
//               샌드위치에서 (q1⊗q2) v (q1⊗q2)⁻¹ = q1(q2 v q2⁻¹)q1⁻¹, math part05)
//
// raylib 의 Vector3, Vector4 / Quaternion 위에서 동작.

#pragma once

#include "raylib.h"

#include <cmath>

namespace mathlab {

inline Quaternion QuatIdentity() { return {0, 0, 0, 1}; }

inline Quaternion QuatFromAxisAngle(Vector3 axis, float angleRad) {
    float lenSq = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
    if (lenSq < 1e-12f) return QuatIdentity();
    float inv = 1.0f / std::sqrt(lenSq);
    Vector3 a = {axis.x * inv, axis.y * inv, axis.z * inv};
    float half = angleRad * 0.5f;
    float s    = std::sin(half);
    return {a.x * s, a.y * s, a.z * s, std::cos(half)};
}

// Hamilton 곱. (a ⊗ b) 는 b 회전을 먼저, a 회전을 나중에 적용 (math part05).
//   실수부: w = aw·bw - a⃗·b⃗
//   벡터부: v⃗ = aw·b⃗ + bw·a⃗ + a⃗ × b⃗
inline Quaternion QuatMul(Quaternion a, Quaternion b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

// 켤레. 단위 쿼터니언에서는 q⁻¹ 과 같다.
inline Quaternion QuatConjugate(Quaternion q) {
    return {-q.x, -q.y, -q.z, q.w};
}

// Euler XYZ (extrinsic — 고정 월드 축 기준 X→Y→Z 순서로 적용, Tait-Bryan).
//   먼저 적용할 회전이 곱의 오른쪽: q = qZ ⊗ qY ⊗ qX.
//   같은 회전 행렬은 intrinsic Z-Y'-X'' 시퀀스와 동치다 (math part04).
inline Quaternion QuatFromEulerXYZ(float pitch, float yaw, float roll) {
    Quaternion qx = QuatFromAxisAngle({1, 0, 0}, pitch);
    Quaternion qy = QuatFromAxisAngle({0, 1, 0}, yaw);
    Quaternion qz = QuatFromAxisAngle({0, 0, 1}, roll);
    return QuatMul(QuatMul(qz, qy), qx);
}

inline float QuatNorm(Quaternion q) {
    return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
}

inline Quaternion QuatNormalize(Quaternion q) {
    float n = QuatNorm(q);
    if (n < 1e-12f) return QuatIdentity();
    float inv = 1.0f / n;
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

// q 로 vec3 회전. v' = q ⊗ (v, 0) ⊗ q⁻¹ 의 sandwich 를 그대로 전개.
inline Vector3 QuatRotateVec(Quaternion q, Vector3 v) {
    Quaternion p{v.x, v.y, v.z, 0.0f};
    Quaternion r = QuatMul(QuatMul(q, p), QuatConjugate(q));
    return {r.x, r.y, r.z};
}

// 쿼터니언 → 회전 행렬. raylib 의 Matrix 는 열 우선 저장 (m0,m4,m8,m12 가 첫 행).
//   R = I + 2w[v⃗]× + 2[v⃗]×²  를 성분으로 전개한 표준형.
inline Matrix QuatToMatrix(Quaternion q) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;

    Matrix m{};
    m.m0 = 1.0f - (yy + zz);  m.m1 = xy + wz;           m.m2  = xz - wy;           m.m3  = 0.0f;
    m.m4 = xy - wz;           m.m5 = 1.0f - (xx + zz);  m.m6  = yz + wx;           m.m7  = 0.0f;
    m.m8 = xz + wy;           m.m9 = yz - wx;           m.m10 = 1.0f - (xx + yy);  m.m11 = 0.0f;
    m.m12 = 0.0f;             m.m13 = 0.0f;             m.m14 = 0.0f;              m.m15 = 1.0f;
    return m;
}

// nlerp: 단순 선형 보간 + 정규화. 빠르고 시각적으로 충분.
inline Quaternion QuatNlerp(Quaternion a, Quaternion b, float t) {
    // 짧은 호 방향 보장
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
    }
    Quaternion r = {
        a.x + t * (b.x - a.x),
        a.y + t * (b.y - a.y),
        a.z + t * (b.z - a.z),
        a.w + t * (b.w - a.w),
    };
    return QuatNormalize(r);
}

// slerp: 단위 구 위 호를 따라. math part05 의 표준 정의.
inline Quaternion QuatSlerp(Quaternion a, Quaternion b, float t) {
    a = QuatNormalize(a);
    b = QuatNormalize(b);
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    if (d > 0.9995f) {
        return QuatNlerp(a, b, t);   // 거의 같음 — nlerp 가 더 안정
    }
    // 입력이 미세하게 정규화 깨지면 d > 1 → acos NaN. 방어용 clamp.
    if (d > 1.0f)  d = 1.0f;
    if (d < -1.0f) d = -1.0f;
    float theta  = std::acos(d);
    float sinT   = std::sin(theta);
    if (sinT < 1e-6f) return QuatNlerp(a, b, t);
    float wa     = std::sin((1 - t) * theta) / sinT;
    float wb     = std::sin(t * theta)       / sinT;
    return {
        a.x * wa + b.x * wb,
        a.y * wa + b.y * wb,
        a.z * wa + b.z * wb,
        a.w * wa + b.w * wb,
    };
}

// 짐벌락 검출: Tait-Bryan 시퀀스의 *가운데 축* 각도가 ±90° 근처면 바깥 두 축
// 회전이 한 축으로 퇴화한다. QuatFromEulerXYZ(q = qZ⊗qY⊗qX) 기준 가운데 축은
// Y(yaw) — yaw ≈ ±90° 에서 pitch(X)·roll(Z) 이 합쳐진다. 입력은 그 가운데 축
// 각도 (radian).
inline bool EulerNearGimbalLock(float middleAxisRad) {
    constexpr float kEps = 0.05f;  // 약 3°
    float p = std::fabs(middleAxisRad);
    float halfPi = 1.5707963f;
    return std::fabs(p - halfPi) < kEps;
}

}  // namespace mathlab
