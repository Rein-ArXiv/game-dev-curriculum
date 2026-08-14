// mat — Mat3/Mat4. 열우선(column-major) 저장(OpenGL/GLM 관례).
//
// 원소 (행 r, 열 c) 는 m[c*4 + r] 에 있다. 열우선이면 Mat4*Vec4 가 열 단위
// SIMD 로딩에 맞고, GPU/그래픽 API 와 메모리 호환된다. 변환 합성은 M = T*R*S
// 순서(열우선에서 왼쪽 곱이 나중 적용)다.

#pragma once

#include "math/vec.hpp"

#include <cmath>

namespace mega {
namespace math {

struct Mat3 {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};   // 열우선, 기본 단위행렬

    float  At(int r, int c) const { return m[c * 3 + r]; }
    float& At(int r, int c) { return m[c * 3 + r]; }

    static Mat3 Identity() { return Mat3{}; }

    Vec3 operator*(const Vec3& v) const {
        return {At(0,0)*v.x + At(0,1)*v.y + At(0,2)*v.z,
                At(1,0)*v.x + At(1,1)*v.y + At(1,2)*v.z,
                At(2,0)*v.x + At(2,1)*v.y + At(2,2)*v.z};
    }
    Mat3 operator*(const Mat3& b) const {
        Mat3 r;
        for (int c = 0; c < 3; ++c)
            for (int row = 0; row < 3; ++row)
                r.At(row, c) = At(row,0)*b.At(0,c) + At(row,1)*b.At(1,c) + At(row,2)*b.At(2,c);
        return r;
    }
    Mat3 Transposed() const {
        Mat3 r;
        for (int c = 0; c < 3; ++c) for (int row = 0; row < 3; ++row) r.At(row, c) = At(c, row);
        return r;
    }
};

struct Mat4 {
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};   // 열우선 단위행렬

    float  At(int r, int c) const { return m[c * 4 + r]; }
    float& At(int r, int c) { return m[c * 4 + r]; }

    static Mat4 Identity() { return Mat4{}; }

    static Mat4 Translation(const Vec3& t) {
        Mat4 r;
        r.At(0,3) = t.x; r.At(1,3) = t.y; r.At(2,3) = t.z;
        return r;
    }
    static Mat4 Scale(const Vec3& s) {
        Mat4 r;
        r.At(0,0) = s.x; r.At(1,1) = s.y; r.At(2,2) = s.z;
        return r;
    }
    static Mat4 FromMat3(const Mat3& b) {
        Mat4 r;
        for (int c = 0; c < 3; ++c) for (int row = 0; row < 3; ++row) r.At(row, c) = b.At(row, c);
        return r;
    }

    Vec4 operator*(const Vec4& v) const {
        return {At(0,0)*v.x + At(0,1)*v.y + At(0,2)*v.z + At(0,3)*v.w,
                At(1,0)*v.x + At(1,1)*v.y + At(1,2)*v.z + At(1,3)*v.w,
                At(2,0)*v.x + At(2,1)*v.y + At(2,2)*v.z + At(2,3)*v.w,
                At(3,0)*v.x + At(3,1)*v.y + At(3,2)*v.z + At(3,3)*v.w};
    }
    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row)
                r.At(row, c) = At(row,0)*b.At(0,c) + At(row,1)*b.At(1,c)
                             + At(row,2)*b.At(2,c) + At(row,3)*b.At(3,c);
        return r;
    }
    Mat4 Transposed() const {
        Mat4 r;
        for (int c = 0; c < 4; ++c) for (int row = 0; row < 4; ++row) r.At(row, c) = At(c, row);
        return r;
    }

    // 점/방향 변환 헬퍼.
    Vec3 TransformPoint(const Vec3& p) const { Vec4 r = (*this) * Vec4{p, 1.0f}; return r.XYZ(); }
    Vec3 TransformDir(const Vec3& d) const { Vec4 r = (*this) * Vec4{d, 0.0f}; return r.XYZ(); }
};

// 4x4 역행렬(Mesa gluInvertMatrix 포팅, 열우선). 특이행렬이면 false.
inline bool Inverse(const Mat4& src, Mat4& out) {
    const float* m = src.m;
    float inv[16];
    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (std::fabs(det) < 1e-12f) return false;
    float invDet = 1.0f / det;
    for (int i = 0; i < 16; ++i) out.m[i] = inv[i] * invDet;
    return true;
}

// 우수좌표계 원근투영(깊이 [-1,1], OpenGL 관례). fovY 라디안.
inline Mat4 Perspective(float fovY, float aspect, float zNear, float zFar) {
    float f = 1.0f / std::tan(fovY * 0.5f);
    Mat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
    r.At(0,0) = f / aspect;
    r.At(1,1) = f;
    r.At(2,2) = (zFar + zNear) / (zNear - zFar);
    r.At(2,3) = (2.0f * zFar * zNear) / (zNear - zFar);
    r.At(3,2) = -1.0f;
    return r;
}

// 우수좌표계 LookAt 뷰 행렬.
inline Mat4 LookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f = Normalize(center - eye);   // 전방
    Vec3 s = Normalize(Cross(f, up));   // 우측
    Vec3 u = Cross(s, f);               // 보정된 상방
    Mat4 r;
    r.At(0,0) = s.x; r.At(0,1) = s.y; r.At(0,2) = s.z;
    r.At(1,0) = u.x; r.At(1,1) = u.y; r.At(1,2) = u.z;
    r.At(2,0) = -f.x; r.At(2,1) = -f.y; r.At(2,2) = -f.z;
    r.At(0,3) = -Dot(s, eye);
    r.At(1,3) = -Dot(u, eye);
    r.At(2,3) =  Dot(f, eye);
    return r;
}

}  // namespace math
}  // namespace mega
