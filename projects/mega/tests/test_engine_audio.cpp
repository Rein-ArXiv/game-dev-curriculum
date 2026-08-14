// 오디오 검증: 거리 감쇠·패닝·다중 소스 믹싱.
#include "engine/audio.hpp"
#include "test_util.h"

#include <cmath>
#include <vector>

using namespace mega::engine;
using mega::math::Vec3;

static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

static void TestAttenuation() {
    CHECK(Near(Attenuation(0.5f), 1.0f));        // ref 안 → 1
    CHECK(Near(Attenuation(1.0f), 1.0f));
    CHECK(Attenuation(5.0f) < Attenuation(2.0f)); // 멀수록 작다
    CHECK(Attenuation(2.0f) < 1.0f);
    CHECK(Attenuation(100.0f) > 0.0f);            // 0으로 수렴(음수 아님)
}

static void TestPanning() {
    float l, r;
    PanGains(0.0f, l, r);                         // 중앙
    CHECK(Near(l, r));                            // 좌우 같음
    CHECK(Near(l * l + r * r, 1.0f, 1e-3f));      // equal-power

    PanGains(-1.0f, l, r);                        // 완전 좌
    CHECK(l > 0.99f); CHECK(r < 0.01f);

    PanGains(1.0f, l, r);                         // 완전 우
    CHECK(r > 0.99f); CHECK(l < 0.01f);
}

static void TestSourceGains() {
    Listener lis; lis.pos = {0, 0, 0}; lis.right = {1, 0, 0};

    // 오른쪽 소스 → 오른쪽 게인이 크다.
    Source rightSrc; rightSrc.pos = {5, 0, 0}; rightSrc.gain = 1.0f;
    float gl, gr;
    SourceGains(lis, rightSrc, gl, gr);
    CHECK(gr > gl);

    // 왼쪽 소스 → 왼쪽 게인이 크다.
    Source leftSrc; leftSrc.pos = {-5, 0, 0};
    SourceGains(lis, leftSrc, gl, gr);
    CHECK(gl > gr);

    // 먼 소스가 가까운 소스보다 총 게인 작다.
    Source near; near.pos = {0, 2, 0};
    Source far;  far.pos  = {0, 50, 0};
    float nl, nr, fl, fr;
    SourceGains(lis, near, nl, nr);
    SourceGains(lis, far, fl, fr);
    CHECK((nl + nr) > (fl + fr));
}

static void TestMixStereo() {
    Listener lis; lis.pos = {0, 0, 0}; lis.right = {1, 0, 0};
    std::vector<float> tone(4, 1.0f);             // 상수 신호

    Source s1; s1.pos = {5, 0, 0}; s1.samples = tone.data(); s1.count = 4; s1.gain = 1.0f;   // 우측
    Source s2; s2.pos = {-5, 0, 0}; s2.samples = tone.data(); s2.count = 4; s2.gain = 1.0f;  // 좌측

    float L[4], R[4];
    MixStereo(lis, {s1, s2}, L, R, 4);
    // 좌우 소스가 섞여 양 채널 모두 0이 아니고, 대칭이라 L≈R.
    CHECK(L[0] > 0); CHECK(R[0] > 0);
    CHECK(Near(L[0], R[0], 1e-2f));

    // 빈 소스 목록은 무음.
    float Z[4], Z2[4];
    MixStereo(lis, {}, Z, Z2, 4);
    CHECK(Near(Z[0], 0.0f)); CHECK(Near(Z2[0], 0.0f));
}

static void RunTests() {
    TestAttenuation();
    TestPanning();
    TestSourceGains();
    TestMixStereo();
}

TEST_MAIN("engine_audio")
