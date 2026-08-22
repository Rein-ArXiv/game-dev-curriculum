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

    // 기준 거리. 게임의 단위는 픽셀이라 1 로 두면 두 칸만 떨어져도 소리가
    // 사라진다 — 도메인이 이 값을 정해 주어야 한다는 것이 이 인자의 이유다.
    CHECK(Near(Attenuation(80.0f, 90.0f), 1.0f));
    CHECK(Attenuation(180.0f, 90.0f) < 1.0f);
    // 거리를 두 배로 하면 감쇠가 단조 감소한다.
    float previous = 1.0f;
    for (float d = 90.0f; d <= 90.0f * 64.0f; d *= 2.0f) {
        const float now = Attenuation(d, 90.0f);
        CHECK(now <= previous);
        previous = now;
    }
    CHECK(previous < 0.05f);                     // 충분히 멀면 사실상 들리지 않는다
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

    // 예산은 **에너지**로 잰다. equal-power 패닝에서 좌우 합은 중앙에서 최대
    // √2 까지 오르므로 "합이 1을 넘지 않는다"는 애초에 성립하지 않는 단언이고,
    // 성립하는 것은 l²+r² = 1 이다. 그리고 그 값이 패닝 전체에서 유지되는지가
    // 곧 "패닝이 음량을 바꾸지 않는다"이다.
    for (int step = -20; step <= 20; ++step) {
        const float pan = static_cast<float>(step) / 20.0f;
        PanGains(pan, l, r);
        CHECK(l >= 0.0f && r >= 0.0f);
        CHECK(Near(l * l + r * r, 1.0f, 1e-3f));
        CHECK(l + r <= 1.4143f);
    }
    // 범위 밖 입력은 잘린다(클램프).
    float clampedL, clampedR;
    PanGains(-9.0f, clampedL, clampedR);
    PanGains(-1.0f, l, r);
    CHECK(Near(clampedL, l)); CHECK(Near(clampedR, r));
}

// 게인이 이미 정해진 보이스의 합성. 제품에서 장치가 부르는 쪽이다.
static void TestMixVoices() {
    std::vector<float> tone(8, 1.0f);
    Voice a; a.samples = tone.data(); a.count = 8; a.gainL = 0.25f; a.gainR = 0.75f;
    Voice b; b.samples = tone.data(); b.count = 4; b.gainL = 0.10f; b.gainR = 0.10f;
    const Voice voices[2] = {a, b};

    float L[8], R[8];
    CHECK_EQ(MixVoices(voices, 2, L, R, 8), (size_t)2);
    // 짧은 보이스는 자기 길이만큼만 기여한다.
    CHECK(Near(L[0], 0.35f)); CHECK(Near(R[0], 0.85f));
    CHECK(Near(L[5], 0.25f)); CHECK(Near(R[5], 0.75f));

    // 보이스 상한. 넘치면 버린다 — 예산 없는 믹서는 프레임을 통째로 먹는다.
    std::vector<Voice> many(kMaxVoices + 8, a);
    CHECK_EQ(MixVoices(many.data(), many.size(), L, R, 8), kMaxVoices);
    CHECK(Near(L[0], 0.25f * static_cast<float>(kMaxVoices), 1e-3f));

    // 널 보이스는 건너뛴다(장치가 파형을 못 찾은 경우).
    Voice empty;
    const Voice mixedNull[2] = {empty, a};
    CHECK_EQ(MixVoices(mixedNull, 2, L, R, 8), (size_t)2);
    CHECK(Near(L[0], 0.25f));
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

    // 좌·우 극단에서 반대 채널이 0 으로 수렴한다. 감쇠가 곱해져도 그 관계는
    // 유지된다 — 방향과 거리는 곱해지는 두 축이지 섞이는 축이 아니다.
    Source hardRight; hardRight.pos = {400, 0, 0}; hardRight.refDist = 90.0f;
    Source hardLeft;  hardLeft.pos  = {-400, 0, 0}; hardLeft.refDist = 90.0f;
    SourceGains(lis, hardRight, gl, gr);
    CHECK(gl < gr * 0.02f);
    CHECK(gr > 0.0f);
    SourceGains(lis, hardLeft, gl, gr);
    CHECK(gr < gl * 0.02f);
    CHECK(gl > 0.0f);

    // 그리고 어떤 방향·거리에서도 에너지가 1 을 넘지 않는다.
    for (int step = 0; step < 32; ++step) {
        const float angle = static_cast<float>(step) * 0.196f;
        Source source;
        source.pos = {std::cos(angle) * 30.0f, std::sin(angle) * 30.0f, 0.0f};
        source.refDist = 90.0f;
        SourceGains(lis, source, gl, gr);
        CHECK(gl * gl + gr * gr <= 1.0f + 1e-3f);
    }
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
    TestMixVoices();
    TestSourceGains();
    TestMixStereo();
}

TEST_MAIN("engine_audio")
