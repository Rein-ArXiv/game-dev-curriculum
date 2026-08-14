// 노이즈 검증: 결정성·격자점·범위·연속성·Worley 비음수·FBM 유계.
#include "math/noise.hpp"
#include "test_util.h"

#include <cmath>

using namespace mega::math;

static void RunTests() {
    Noise n(1337);

    // --- 결정성: 같은 시드·좌표 → 같은 값 ---
    {
        CHECK_EQ(n.Perlin2(3.5f, 7.25f), n.Perlin2(3.5f, 7.25f));
        CHECK_EQ(n.Simplex2(3.5f, 7.25f), n.Simplex2(3.5f, 7.25f));
        Noise n2(1337);
        CHECK_EQ(n.Simplex2(10.1f, -4.3f), n2.Simplex2(10.1f, -4.3f));   // 같은 시드 재현
    }

    // --- 다른 시드는 대체로 다른 장(場) ---
    {
        Noise a(1), b(2);
        int diff = 0;
        for (int i = 0; i < 50; ++i) {
            float x = i * 0.37f, y = i * 0.91f;
            if (std::fabs(a.Simplex2(x, y) - b.Simplex2(x, y)) > 1e-6f) ++diff;
        }
        CHECK(diff > 40);   // 거의 모든 샘플이 달라야
    }

    // --- Perlin 격자점(정수 좌표)은 ≈ 0 (그래디언트 노이즈 성질) ---
    {
        bool zero = true;
        for (int i = -3; i <= 3; ++i)
            for (int j = -3; j <= 3; ++j)
                if (std::fabs(n.Perlin3((float)i, (float)j, 0.0f)) > 1e-5f) zero = false;
        CHECK(zero);
    }

    // --- 범위 + 변동성: 그리드 샘플이 [-1.1,1.1] 안, 양/음 모두 등장 ---
    {
        bool inRange = true, hasPos = false, hasNeg = false;
        bool sInRange = true, sHasPos = false, sHasNeg = false;
        for (int gx = 0; gx < 40; ++gx)
            for (int gy = 0; gy < 40; ++gy) {
                float x = gx * 0.3f + 0.13f, y = gy * 0.3f + 0.07f;
                float p = n.Perlin2(x, y);
                float s = n.Simplex2(x, y);
                if (std::fabs(p) > 1.1f || std::isnan(p)) inRange = false;
                if (std::fabs(s) > 1.1f || std::isnan(s)) sInRange = false;
                if (p > 0.05f) hasPos = true;  if (p < -0.05f) hasNeg = true;
                if (s > 0.05f) sHasPos = true; if (s < -0.05f) sHasNeg = true;
            }
        CHECK(inRange);  CHECK(hasPos);  CHECK(hasNeg);     // Perlin 변동
        CHECK(sInRange); CHECK(sHasPos); CHECK(sHasNeg);    // Simplex 변동
    }

    // --- 연속성: 작은 입력 변화 → 작은 출력 변화 ---
    {
        float maxJump = 0.0f;
        for (int i = 0; i < 200; ++i) {
            float x = i * 0.123f, y = i * 0.071f;
            float a = n.Simplex2(x, y);
            float b = n.Simplex2(x + 1e-3f, y);
            maxJump = std::fmax(maxJump, std::fabs(a - b));
        }
        CHECK(maxJump < 0.05f);   // 1e-3 이동에 0.05 미만 변화(부드러움)
    }

    // --- Worley: 비음수 + 결정성 ---
    {
        bool nonneg = true;
        for (int gx = 0; gx < 20; ++gx)
            for (int gy = 0; gy < 20; ++gy) {
                float w = n.Worley2(gx * 0.5f, gy * 0.5f);
                if (w < 0.0f || std::isnan(w)) nonneg = false;
            }
        CHECK(nonneg);
        CHECK_EQ(n.Worley2(2.3f, 5.7f), n.Worley2(2.3f, 5.7f));
    }

    // --- FBM: 유계(|.|≤1.1) + 결정성 ---
    {
        auto fn = [&](float x, float y) { return n.Simplex2(x, y); };
        bool bounded = true;
        for (int i = 0; i < 100; ++i) {
            float v = FBM(fn, i * 0.21f, i * 0.13f, 5);
            if (std::fabs(v) > 1.1f || std::isnan(v)) bounded = false;
        }
        CHECK(bounded);
        CHECK_EQ(FBM(fn, 1.5f, 2.5f, 4), FBM(fn, 1.5f, 2.5f, 4));
    }
}

TEST_MAIN("noise")
