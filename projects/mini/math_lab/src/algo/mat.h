// 4x4 행렬 대수 + 3D 벡터 연산 — math part02 (벡터) / part03 (행렬) /
// part06 (좌표계) 의 항등식을 코드로 고정하기 위한 헤더.
//
// raylib 의 Matrix 타입을 재사용하지만 raymath 의 MatrixMultiply /
// MatrixInvert 는 쓰지 않는다 (그 자체가 이 lab 의 학습 대상이다).
//
// 저장 규약: raylib 의 필드 이름 mN 은 N = col*4 + row 다. 즉 m0..m3 가
// 첫 번째 *열*, m12..m15 가 네 번째 열(= 평행이동 항)이다. 이 헤더의
// MatAt(m, row, col) 이 그 규약의 유일한 진입점이다.

#pragma once

#include "raylib.h"

#include "algo/quaternion.h"

#include <cmath>
#include <utility>

namespace mathlab {

// ---- 3D 벡터 (math part02) ----

inline Vector3 V3Add(Vector3 a, Vector3 b)  { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vector3 V3Sub(Vector3 a, Vector3 b)  { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vector3 V3Scale(Vector3 a, float s)  { return {a.x * s, a.y * s, a.z * s}; }
inline float   V3Dot(Vector3 a, Vector3 b)  { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vector3 V3Cross(Vector3 a, Vector3 b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

inline float V3Length(Vector3 a) { return std::sqrt(V3Dot(a, a)); }

inline Vector3 V3Normalize(Vector3 a) {
    float len = V3Length(a);
    if (len < 1e-12f) return {0, 0, 0};
    return V3Scale(a, 1.0f / len);
}

// a 를 b 방향으로 투영. (a·b̂)b̂
inline Vector3 V3Project(Vector3 a, Vector3 b) {
    float bb = V3Dot(b, b);
    if (bb < 1e-12f) return {0, 0, 0};
    return V3Scale(b, V3Dot(a, b) / bb);
}

// 법선 n (단위) 평면에 대한 반사. r = d - 2(d·n)n
inline Vector3 V3Reflect(Vector3 d, Vector3 n) {
    return V3Sub(d, V3Scale(n, 2.0f * V3Dot(d, n)));
}

// ---- 4x4 행렬 (math part03) ----

inline void MatToArray(const Matrix& m, float out[16]) {
    out[0]  = m.m0;  out[1]  = m.m1;  out[2]  = m.m2;  out[3]  = m.m3;
    out[4]  = m.m4;  out[5]  = m.m5;  out[6]  = m.m6;  out[7]  = m.m7;
    out[8]  = m.m8;  out[9]  = m.m9;  out[10] = m.m10; out[11] = m.m11;
    out[12] = m.m12; out[13] = m.m13; out[14] = m.m14; out[15] = m.m15;
}

inline Matrix MatFromArray(const float in[16]) {
    Matrix m{};
    m.m0  = in[0];  m.m1  = in[1];  m.m2  = in[2];  m.m3  = in[3];
    m.m4  = in[4];  m.m5  = in[5];  m.m6  = in[6];  m.m7  = in[7];
    m.m8  = in[8];  m.m9  = in[9];  m.m10 = in[10]; m.m11 = in[11];
    m.m12 = in[12]; m.m13 = in[13]; m.m14 = in[14]; m.m15 = in[15];
    return m;
}

// (row, col) 성분. 열 우선 저장이므로 인덱스는 col*4 + row.
inline float MatAt(const Matrix& m, int row, int col) {
    float a[16];
    MatToArray(m, a);
    return a[col * 4 + row];
}

inline Matrix MatIdentity() {
    const float a[16] = {1, 0, 0, 0,
                         0, 1, 0, 0,
                         0, 0, 1, 0,
                         0, 0, 0, 1};
    return MatFromArray(a);
}

// C = A·B. C[r][c] = Σ_k A[r][k]·B[k][c] — 행렬 곱은 비가환이다.
inline Matrix MatMul(const Matrix& a, const Matrix& b) {
    float A[16], B[16], C[16] = {};
    MatToArray(a, A);
    MatToArray(b, B);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += A[k * 4 + r] * B[c * 4 + k];
            C[c * 4 + r] = s;
        }
    }
    return MatFromArray(C);
}

inline Matrix MatTranspose(const Matrix& m) {
    float A[16], T[16] = {};
    MatToArray(m, A);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) T[r * 4 + c] = A[c * 4 + r];
    }
    return MatFromArray(T);
}

// 동차 좌표 점 변환 (w = 1) — 평행이동이 적용된다.
inline Vector3 MatTransformPoint(const Matrix& m, Vector3 v) {
    float a[16];
    MatToArray(m, a);
    return {
        a[0] * v.x + a[4] * v.y + a[8]  * v.z + a[12],
        a[1] * v.x + a[5] * v.y + a[9]  * v.z + a[13],
        a[2] * v.x + a[6] * v.y + a[10] * v.z + a[14],
    };
}

// 방향 벡터 변환 (w = 0) — 평행이동이 적용되지 않는다.
inline Vector3 MatTransformVector(const Matrix& m, Vector3 v) {
    float a[16];
    MatToArray(m, a);
    return {
        a[0] * v.x + a[4] * v.y + a[8]  * v.z,
        a[1] * v.x + a[5] * v.y + a[9]  * v.z,
        a[2] * v.x + a[6] * v.y + a[10] * v.z,
    };
}

inline float MatDeterminant(const Matrix& m) {
    float a[16];
    MatToArray(m, a);
    auto E = [&](int r, int c) { return a[c * 4 + r]; };
    auto det3 = [&](int c0, int c1, int c2) {
        return E(1, c0) * (E(2, c1) * E(3, c2) - E(2, c2) * E(3, c1))
             - E(1, c1) * (E(2, c0) * E(3, c2) - E(2, c2) * E(3, c0))
             + E(1, c2) * (E(2, c0) * E(3, c1) - E(2, c1) * E(3, c0));
    };
    return  E(0, 0) * det3(1, 2, 3)
          - E(0, 1) * det3(0, 2, 3)
          + E(0, 2) * det3(0, 1, 3)
          - E(0, 3) * det3(0, 1, 2);
}

// Gauss-Jordan 소거로 역행렬. 특이행렬이면 false 를 돌려주고 out 을 건드리지 않는다.
inline bool MatInvert(const Matrix& m, Matrix& out) {
    float a[16];
    MatToArray(m, a);

    double w[4][8];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) w[r][c] = (double)a[c * 4 + r];
        for (int c = 0; c < 4; ++c) w[r][4 + c] = (r == c) ? 1.0 : 0.0;
    }

    for (int col = 0; col < 4; ++col) {
        int piv = col;
        for (int r = col + 1; r < 4; ++r) {
            if (std::fabs(w[r][col]) > std::fabs(w[piv][col])) piv = r;
        }
        if (std::fabs(w[piv][col]) < 1e-12) return false;
        if (piv != col) {
            for (int c = 0; c < 8; ++c) std::swap(w[col][c], w[piv][c]);
        }
        double d = w[col][col];
        for (int c = 0; c < 8; ++c) w[col][c] /= d;
        for (int r = 0; r < 4; ++r) {
            if (r == col) continue;
            double f = w[r][col];
            if (f == 0.0) continue;
            for (int c = 0; c < 8; ++c) w[r][c] -= f * w[col][c];
        }
    }

    float inv[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) inv[c * 4 + r] = (float)w[r][4 + c];
    }
    out = MatFromArray(inv);
    return true;
}

inline Matrix MatInverse(const Matrix& m) {
    Matrix r = MatIdentity();
    MatInvert(m, r);
    return r;
}

// ---- 아핀 합성 (math part06) ----

inline Matrix MatTranslate(Vector3 t) {
    Matrix m = MatIdentity();
    m.m12 = t.x;
    m.m13 = t.y;
    m.m14 = t.z;
    return m;
}

inline Matrix MatScale(Vector3 s) {
    Matrix m = MatIdentity();
    m.m0  = s.x;
    m.m5  = s.y;
    m.m10 = s.z;
    return m;
}

inline Matrix MatRotate(Quaternion q) { return QuatToMatrix(QuatNormalize(q)); }

// 통상적인 합성 순서: 스케일 → 회전 → 평행이동. 열벡터 규약이므로 곱은 T·R·S.
inline Matrix MatComposeTRS(Vector3 t, Quaternion q, Vector3 s) {
    return MatMul(MatTranslate(t), MatMul(MatRotate(q), MatScale(s)));
}

}  // namespace mathlab
