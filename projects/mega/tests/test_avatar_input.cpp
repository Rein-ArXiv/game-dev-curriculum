// 아바타 입력 레코드: 바이트가 계약이다.
//
// 프레임 입력은 명령과 달리 신뢰 채널을 타지 않는다. 손실을 재전송이 아니라
// **중복 전송**으로 메우므로 같은 프레임의 같은 입력이 여러 번 도착하고, 그때
// 두 인코딩이 같은 바이트를 내야 중복이 중복으로 보인다. 그래서 이 파일이 재는
// 것은 "왕복이 되는가"가 아니라 **바이트가 그 값인가**다.

#include "mp/input.hpp"
#include "test_util.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace mega::mp;

// ── 1. 골든 바이트 ─────────────────────────────────────────────────
// 필드 순서를 바꾸면 여기가 먼저 죽는다. 세 필드가 서로 다른 값이어야 순서
// 변경이 관측되므로 대칭인 값(0,0,0 / 1,1,1)은 쓰지 않는다.

static void TestGoldenBytes() {
    Input input;
    input.moveX = 1;
    input.moveY = -1;
    input.SetBuild(true);

    std::vector<uint8_t> bytes;
    WriteInput(input, bytes);
    CHECK_EQ(bytes.size(), kInputBytes);
    CHECK_EQ(bytes[0], (uint8_t)0x01);   // moveX = +1
    CHECK_EQ(bytes[1], (uint8_t)0xFF);   // moveY = -1 (2의 보수)
    CHECK_EQ(bytes[2], kButtonBuild);    // buttons

    const uint8_t* cursor = bytes.data();
    Input decoded;
    CHECK(ReadInput(cursor, bytes.data() + bytes.size(), decoded));
    CHECK(cursor == bytes.data() + bytes.size());
    CHECK(decoded == input);
    CHECK_EQ(decoded.moveX, (int8_t)1);
    CHECK_EQ(decoded.moveY, (int8_t)-1);
    CHECK(decoded.Build());
    CHECK(!decoded.Fire());
}

// ── 2. 도메인 전체 왕복 ────────────────────────────────────────────

static void TestEveryValidInputRoundTrips() {
    int count = 0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            for (uint8_t buttons = 0; buttons <= kButtonMask; ++buttons) {
                Input input;
                input.moveX = static_cast<int8_t>(x);
                input.moveY = static_cast<int8_t>(y);
                input.buttons = buttons;
                CHECK(InputInRange(input));
                std::vector<uint8_t> bytes;
                WriteInput(input, bytes);
                const uint8_t* cursor = bytes.data();
                Input decoded;
                CHECK(ReadInput(cursor, bytes.data() + bytes.size(), decoded));
                CHECK(decoded == input);
                ++count;
            }
    CHECK_EQ(count, 36);            // 3 x 3 x 4
}

// ── 3. 범위 밖 거부 ────────────────────────────────────────────────
// 클램프는 시뮬 안에도 있지만 디코더가 먼저 막아야 한다. 시뮬의 클램프는
// "이 값을 어떻게 다룰까"이고 디코더의 거부는 "이 패킷은 우리 것이 아니다"다.

static bool Decodes(int8_t moveX, int8_t moveY, uint8_t buttons) {
    std::vector<uint8_t> bytes{static_cast<uint8_t>(moveX), static_cast<uint8_t>(moveY), buttons};
    const uint8_t* cursor = bytes.data();
    Input decoded;
    return ReadInput(cursor, bytes.data() + bytes.size(), decoded);
}

static void TestOutOfRangeRejected() {
    CHECK(Decodes(1, -1, kButtonFire));
    CHECK(!Decodes(2, 0, 0));          // 축 상한 바로 밖
    CHECK(!Decodes(-2, 0, 0));
    CHECK(!Decodes(0, 3, 0));
    CHECK(!Decodes(127, 0, 0));        // 극단값
    CHECK(!Decodes(-128, 0, 0));
    CHECK(!Decodes(0, 0, 0x80));       // 정의되지 않은 버튼 비트
    CHECK(!Decodes(0, 0, 0x04));
    CHECK(!Decodes(0, 0, 0xFF));

    // 모자란 바이트.
    std::vector<uint8_t> two{0x01, 0x00};
    const uint8_t* cursor = two.data();
    Input decoded;
    CHECK(!ReadInput(cursor, two.data() + two.size(), decoded));
}

// ── 4. 프레임 스트림 ───────────────────────────────────────────────

static void TestInputStream() {
    std::vector<Input> frames;
    for (size_t i = 0; i < kMaxInputFrames; ++i) {
        Input input;
        input.moveX = static_cast<int8_t>((i % 3) - 1);
        input.moveY = static_cast<int8_t>(((i + 1) % 3) - 1);
        input.SetFire((i & 1) != 0);
        input.SetBuild((i % 4) == 0);
        frames.push_back(input);
    }

    const std::vector<uint8_t> bytes =
        WriteInputStream(4242, frames.data(), frames.size());
    CHECK_EQ(bytes.size(), kInputStreamHeaderBytes + kMaxInputFrames * kInputBytes);

    uint32_t baseFrame = 0;
    std::vector<Input> decoded;
    CHECK(ReadInputStream(bytes, baseFrame, decoded));
    CHECK_EQ(baseFrame, 4242u);
    CHECK_EQ(decoded.size(), frames.size());
    for (size_t i = 0; i < frames.size(); ++i) CHECK(decoded[i] == frames[i]);

    // 상한을 넘는 요청은 상한에서 잘린다(인코더는 순수 직렬화기다).
    std::vector<Input> tooMany(kMaxInputFrames + 5);
    const std::vector<uint8_t> clamped =
        WriteInputStream(1, tooMany.data(), tooMany.size());
    CHECK(ReadInputStream(clamped, baseFrame, decoded));
    CHECK_EQ(decoded.size(), kMaxInputFrames);

    // 잉여·부족 바이트는 거부. 다른 디코더와 같은 규율이다.
    std::vector<uint8_t> surplus = bytes;
    surplus.push_back(0);
    CHECK(!ReadInputStream(surplus, baseFrame, decoded));
    std::vector<uint8_t> truncated = bytes;
    truncated.pop_back();
    CHECK(!ReadInputStream(truncated, baseFrame, decoded));

    // 선언한 개수가 상한을 넘으면 거부.
    std::vector<uint8_t> forged = bytes;
    forged[4] = static_cast<uint8_t>(kMaxInputFrames + 1);
    CHECK(!ReadInputStream(forged, baseFrame, decoded));

    // 스트림 안의 입력 하나가 범위 밖이면 스트림 전체가 무효다.
    std::vector<uint8_t> poisoned = bytes;
    poisoned[kInputStreamHeaderBytes + 3] = 0x40;    // 두 번째 입력의 moveX
    CHECK(!ReadInputStream(poisoned, baseFrame, decoded));

    // 머리도 안 되는 짧은 패킷.
    CHECK(!ReadInputStream(std::vector<uint8_t>{1, 2, 3}, baseFrame, decoded));
}

// ── 5. 중복 전송이 실제로 손실을 메우는가 ──────────────────────────
// 프레임 입력을 신뢰 채널에 싣지 않기로 한 결정의 근거다. 최근 8프레임을 매번
// 다시 실으면, 8프레임보다 짧은 연속 손실은 다음 패킷 하나가 통째로 메운다.

static void TestRedundancyCoversShortLoss() {
    const int kFrames = 600;
    std::vector<Input> timeline(kFrames);
    uint32_t rng = 0x2545F491u;
    auto next = [&] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
    for (int f = 0; f < kFrames; ++f) {
        timeline[f].moveX = static_cast<int8_t>((next() % 3) - 1);
        timeline[f].moveY = static_cast<int8_t>((next() % 3) - 1);
        timeline[f].SetFire((next() & 7) == 0);
    }

    std::vector<bool> known(kFrames, false);
    int lost = 0;
    for (int f = 0; f < kFrames; ++f) {
        const bool dropped = (next() % 100) < 35;    // 35% 손실
        if (dropped) { ++lost; continue; }
        const size_t count = static_cast<size_t>(f + 1 < (int)kMaxInputFrames
                                                     ? f + 1
                                                     : (int)kMaxInputFrames);
        const uint32_t base = static_cast<uint32_t>(f + 1 - static_cast<int>(count));
        const std::vector<uint8_t> packet =
            WriteInputStream(base, &timeline[base], count);
        uint32_t decodedBase = 0;
        std::vector<Input> decoded;
        CHECK(ReadInputStream(packet, decodedBase, decoded));
        for (size_t i = 0; i < decoded.size(); ++i) {
            const size_t frame = decodedBase + i;
            CHECK(decoded[i] == timeline[frame]);
            known[frame] = true;
        }
    }
    // 마지막 8프레임은 아직 중복될 기회를 다 쓰지 못했으므로 세지 않는다 —
    // 그 구간의 손실은 이 규칙의 결함이 아니라 아직 오지 않은 패킷이다.
    const int settled = kFrames - static_cast<int>(kMaxInputFrames);
    int missing = 0;
    for (int f = 0; f < settled; ++f) if (!known[f]) ++missing;
    std::printf("  35%% 손실 · 8프레임 중복: 패킷 %d개 유실 · 끝내 못 받은 프레임 %d/%d\n",
                lost, missing, settled);
    CHECK(lost > 150);        // 실제로 많이 잃었다
    CHECK_EQ(missing, 0);     // 그런데 확정 구간의 프레임은 하나도 잃지 않았다
}

static void RunTests() {
    TestGoldenBytes();
    TestEveryValidInputRoundTrips();
    TestOutOfRangeRejected();
    TestInputStream();
    TestRedundancyCoversShortLoss();
}

TEST_MAIN("avatar_input")
