// 절차적 노이즈 — Value, Perlin (gradient), Simplex (2D), FBM.
// math part10 의 식들을 게임 코드에 흔한 형태로.
//
// 모두 *결정론적 hash* 기반 — 같은 (x, y, seed) 면 같은 값.

#pragma once

#include <cmath>
#include <cstdint>

namespace mathlab {

namespace nz {

// 경량 정수 hash. Wang hash 의 변형. 분포 충분.
inline uint32_t HashU32(uint32_t x) {
    x = (x ^ 61u) ^ (x >> 16);
    x = x + (x << 3);
    x = x ^ (x >> 4);
    x = x * 0x27d4eb2du;
    x = x ^ (x >> 15);
    return x;
}

// snippet-begin: hash-2d
inline uint32_t Hash2(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    return HashU32(h);
}

// [0, 1) float 으로.
inline float HashTo01(int x, int y, uint32_t seed) {
    return (Hash2(x, y, seed) >> 8) * (1.0f / 16777216.0f);   // 24 bit
}
// snippet-end: hash-2d

// snippet-begin: fade-curves
inline float Smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }
inline float Quintic(float t)    { return t * t * t * (t * (t * 6 - 15) + 10); }
// snippet-end: fade-curves

// ---- Value Noise (2D) ----
// 격자 정점에 [0, 1) 값을 배치 + 셀 안에서 보간.
inline float Value2D(float x, float y, uint32_t seed) {
    int   ix = (int)std::floor(x);
    int   iy = (int)std::floor(y);
    float fx = x - (float)ix;
    float fy = y - (float)iy;

    float v00 = HashTo01(ix,     iy,     seed);
    float v10 = HashTo01(ix + 1, iy,     seed);
    float v01 = HashTo01(ix,     iy + 1, seed);
    float v11 = HashTo01(ix + 1, iy + 1, seed);

    float u = Smoothstep(fx);
    float v = Smoothstep(fy);
    float a = v00 + (v10 - v00) * u;
    float b = v01 + (v11 - v01) * u;
    return a + (b - a) * v;   // ∈ [0, 1]
}

// ---- Perlin Noise (2D, gradient) ----
// 격자 정점에 *단위 그라디언트* — 4 방향 (축 정렬) 으로 충분 (math part10).
// snippet-begin: perlin-2d
inline void Grad2(int ix, int iy, uint32_t seed, float& gx, float& gy) {
    uint32_t h = Hash2(ix, iy, seed);
    switch (h & 3) {
        case 0:  gx = 1;  gy = 0;  break;
        case 1:  gx = -1; gy = 0;  break;
        case 2:  gx = 0;  gy = 1;  break;
        default: gx = 0;  gy = -1; break;
    }
}

inline float Perlin2D(float x, float y, uint32_t seed) {
    int   ix = (int)std::floor(x);
    int   iy = (int)std::floor(y);
    float fx = x - (float)ix;
    float fy = y - (float)iy;

    float gx, gy;
    Grad2(ix,     iy,     seed, gx, gy);  float n00 = gx * fx       + gy * fy;
    Grad2(ix + 1, iy,     seed, gx, gy);  float n10 = gx * (fx - 1) + gy * fy;
    Grad2(ix,     iy + 1, seed, gx, gy);  float n01 = gx * fx       + gy * (fy - 1);
    Grad2(ix + 1, iy + 1, seed, gx, gy);  float n11 = gx * (fx - 1) + gy * (fy - 1);

    float u = Quintic(fx);
    float v = Quintic(fy);
    float a = n00 + (n10 - n00) * u;
    float b = n01 + (n11 - n01) * u;
    // 실측 범위 ≈ [-0.535, 0.535] (seed=7, 20만 표본 + 격자 스캔).
    // 축 정렬 단위 그라디언트 4종을 쓰므로 교과서의 [-1, 1] 보다 좁다.
    return a + (b - a) * v;
}
// snippet-end: perlin-2d

// ---- Simplex Noise (2D) — 단순화 버전 ----
// 정확한 Ken Perlin 의 Simplex 보다는 *정삼각형 격자 + radial fall-off*.
// 시각적으로 Simplex 와 같은 *방향성 없는 둥근 패턴*.
inline float Simplex2D(float x, float y, uint32_t seed) {
    constexpr float F2 = 0.36602540378f;   // (sqrt(3) - 1) / 2
    constexpr float G2 = 0.21132486540f;   // (3 - sqrt(3)) / 6

    float s  = (x + y) * F2;
    int   i  = (int)std::floor(x + s);
    int   j  = (int)std::floor(y + s);
    float t0 = (i + j) * G2;
    float X0 = i - t0;
    float Y0 = j - t0;
    float x0 = x - X0;
    float y0 = y - Y0;

    int i1, j1;
    if (x0 > y0) { i1 = 1; j1 = 0; } else { i1 = 0; j1 = 1; }

    float x1 = x0 - i1 + G2;
    float y1 = y0 - j1 + G2;
    float x2 = x0 - 1.0f + 2.0f * G2;
    float y2 = y0 - 1.0f + 2.0f * G2;

    auto contrib = [&](float xi, float yi, int gi, int gj) {
        float t = 0.5f - xi * xi - yi * yi;
        if (t < 0) return 0.0f;
        t *= t;
        float gx, gy;
        Grad2(gi, gj, seed, gx, gy);
        return t * t * (gx * xi + gy * yi);
    };

    float n0 = contrib(x0, y0, i,      j     );
    float n1 = contrib(x1, y1, i + i1, j + j1);
    float n2 = contrib(x2, y2, i + 1,  j + 1 );
    // 70.0 은 길이 √2 급 그래디언트 12종을 쓰는 표준 구현의 정규화 상수다.
    // 이 코드의 Grad2 는 축 정렬 단위 벡터 4종이라 그 상수가 맞지 않는다 —
    // 실측 범위 ≈ [-0.684, 0.684] (seed=7, 20만 표본 + 격자 스캔).
    return 70.0f * (n0 + n1 + n2);
}

// ---- FBM (Fractal Brownian Motion) ----
// 옥타브를 누적해 *멀티스케일 디테일*.
// snippet-begin: fbm-2d
template <class NoiseFn>
inline float FBM2D(float x, float y, uint32_t seed,
                    int octaves, float lacunarity, float gain,
                    NoiseFn&& fn) {
    float sum  = 0.0f;
    float amp  = 1.0f;
    float freq = 1.0f;
    float norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum  += fn(x * freq, y * freq, seed + (uint32_t)o) * amp;
        norm += amp;
        amp  *= gain;
        freq *= lacunarity;
    }
    return (norm > 0.0f) ? sum / norm : 0.0f;
}
// snippet-end: fbm-2d

}  // namespace nz
}  // namespace mathlab
