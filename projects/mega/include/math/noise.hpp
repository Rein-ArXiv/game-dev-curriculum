// noise — 절차적 노이즈: Perlin(개선판), Simplex, Worley(셀룰러), FBM.
//
// 결정적 의사난수 장(場)이다. 같은 좌표는 항상 같은 값을 내되, 공간적으로
// 부드럽게 이어진다(흰 잡음과 다른 점). 지형 높이맵, 구름, 흩뿌림, 흔들림에 쓴다.
//
// 기본은 Simplex 다 — Perlin 은 고차원에서 격자 방향 아티팩트가 두드러지지만,
// Simplex 는 심플렉스 격자로 등방성이 좋고 차원당 비용이 낮다(math 시리즈 노이즈
// 비교). Perlin 도 비교·호환을 위해 둔다. Worley 는 셀룰러(보로노이) 패턴 전용.
//
// 출력 범위: Perlin/Simplex ≈ [-1, 1], Worley(F1) ≥ 0.

#pragma once

#include "math/scalar.hpp"
#include "math/vec.hpp"

#include <cmath>
#include <cstdint>

namespace mega {
namespace math {

class Noise {
public:
    Noise() { Seed(0); }
    explicit Noise(uint32_t seed) { Seed(seed); }

    // 0..255 순열을 시드로 섞고 512로 복제(이중 인덱싱용).
    void Seed(uint32_t seed) {
        for (int i = 0; i < 256; ++i) perm_[i] = (uint8_t)i;
        uint32_t s = seed ? seed : 0x9e3779b9u;
        for (int i = 255; i > 0; --i) {                 // Fisher-Yates
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            int j = (int)(s % (uint32_t)(i + 1));
            uint8_t t = perm_[i]; perm_[i] = perm_[j]; perm_[j] = t;
        }
        for (int i = 0; i < 256; ++i) perm_[256 + i] = perm_[i];
    }

    float Perlin3(float x, float y, float z) const {
        int X = (int)std::floor(x) & 255, Y = (int)std::floor(y) & 255, Z = (int)std::floor(z) & 255;
        x -= std::floor(x); y -= std::floor(y); z -= std::floor(z);
        float u = Fade(x), v = Fade(y), w = Fade(z);
        int A  = perm_[X] + Y,  AA = perm_[A] + Z,  AB = perm_[A + 1] + Z;
        int B  = perm_[X + 1] + Y, BA = perm_[B] + Z, BB = perm_[B + 1] + Z;
        return Lerp3(w,
            Lerp3(v, Lerp3(u, Grad(perm_[AA], x, y, z),       Grad(perm_[BA], x - 1, y, z)),
                     Lerp3(u, Grad(perm_[AB], x, y - 1, z),   Grad(perm_[BB], x - 1, y - 1, z))),
            Lerp3(v, Lerp3(u, Grad(perm_[AA + 1], x, y, z - 1),     Grad(perm_[BA + 1], x - 1, y, z - 1)),
                     Lerp3(u, Grad(perm_[AB + 1], x, y - 1, z - 1), Grad(perm_[BB + 1], x - 1, y - 1, z - 1))));
    }
    float Perlin2(float x, float y) const { return Perlin3(x, y, 0.0f); }

    // Simplex 2D (Gustavson). 심플렉스 격자 + 방사 감쇠 커널.
    float Simplex2(float x, float y) const {
        static const float F2 = 0.366025403f;   // (sqrt(3)-1)/2
        static const float G2 = 0.211324865f;   // (3-sqrt(3))/6
        float s = (x + y) * F2;
        int i = (int)std::floor(x + s), j = (int)std::floor(y + s);
        float t = (i + j) * G2;
        float x0 = x - (i - t), y0 = y - (j - t);
        int i1 = (x0 > y0) ? 1 : 0, j1 = (x0 > y0) ? 0 : 1;
        float x1 = x0 - i1 + G2,        y1 = y0 - j1 + G2;
        float x2 = x0 - 1.0f + 2*G2,    y2 = y0 - 1.0f + 2*G2;
        int ii = i & 255, jj = j & 255;
        int gi0 = perm_[ii + perm_[jj]] % 12;
        int gi1 = perm_[ii + i1 + perm_[jj + j1]] % 12;
        int gi2 = perm_[ii + 1 + perm_[jj + 1]] % 12;
        float n0 = Corner(x0, y0, gi0);
        float n1 = Corner(x1, y1, gi1);
        float n2 = Corner(x2, y2, gi2);
        return 70.0f * (n0 + n1 + n2);
    }

    // Worley(셀룰러) F1: 가장 가까운 특징점까지 거리. 3x3 이웃 셀 탐색.
    float Worley2(float x, float y) const {
        int xi = (int)std::floor(x), yi = (int)std::floor(y);
        float f1 = 1e9f;
        for (int oy = -1; oy <= 1; ++oy)
            for (int ox = -1; ox <= 1; ++ox) {
                int cx = xi + ox, cy = yi + oy;
                uint32_t h = Hash2((uint32_t)cx, (uint32_t)cy);
                float fx = cx + (h & 0xFFFF) / 65535.0f;          // 셀 내 특징점
                float fy = cy + ((h >> 16) & 0xFFFF) / 65535.0f;
                float dx = fx - x, dy = fy - y;
                float d = dx * dx + dy * dy;
                if (d < f1) f1 = d;
            }
        return std::sqrt(f1);
    }

private:
    static float Fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static float Lerp3(float t, float a, float b) { return a + t * (b - a); }
    static float Grad(int hash, float x, float y, float z) {
        int h = hash & 15;
        float u = h < 8 ? x : y;
        float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
        return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
    }
    static float Corner(float x, float y, int gi) {
        static const int g[12][2] = {{1,1},{-1,1},{1,-1},{-1,-1},{1,0},{-1,0},
                                     {1,0},{-1,0},{0,1},{0,-1},{0,1},{0,-1}};
        float t = 0.5f - x * x - y * y;
        if (t < 0) return 0.0f;
        t *= t;
        return t * t * (g[gi][0] * x + g[gi][1] * y);
    }
    static uint32_t Hash2(uint32_t x, uint32_t y) {
        uint32_t h = x * 374761393u + y * 668265263u;   // 두 좌표 혼합
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }

    uint8_t perm_[512];
};

// FBM(fractal Brownian motion): 옥타브를 주파수 2배·진폭 절반으로 누적.
// noiseFn 은 (x,y)->[-1,1] 인 호출 가능 객체. 반환은 진폭 합으로 정규화.
template <class NoiseFn>
float FBM(NoiseFn&& noiseFn, float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) {
    float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum  += amp * noiseFn(x * freq, y * freq);
        norm += amp;
        freq *= lacunarity;
        amp  *= gain;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

}  // namespace math
}  // namespace mega
