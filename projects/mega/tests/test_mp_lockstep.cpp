// 결정적 시뮬 + lockstep 검증: 결정성·동기·입력 게이트.
#include "mp/det_sim.hpp"
#include "mp/lockstep.hpp"
#include "test_util.h"

#include <cstdint>
#include <vector>

using namespace mega::mp;

// 결정적 입력 열 생성(피어 수 × 프레임 수).
static std::vector<std::vector<Input>> MakeInputs(int players, int frames, uint32_t seed) {
    std::vector<std::vector<Input>> all(frames, std::vector<Input>(players));
    uint32_t s = seed ? seed : 1;
    auto nx = [&] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; };
    for (int f = 0; f < frames; ++f)
        for (int p = 0; p < players; ++p) {
            Input in;
            in.moveX = (int8_t)((nx() % 3) - 1);
            in.moveY = (int8_t)((nx() % 3) - 1);
            in.SetFire((nx() & 7) == 0);
            all[f][p] = in;
        }
    return all;
}

// 같은 입력 열 → 비트 동일한 상태(매 프레임 checksum 일치).
static void TestDeterminism() {
    const int P = 4, F = 300;
    auto inputs = MakeInputs(P, F, 777);

    DetState a, b;
    a.Init(P); b.Init(P);
    bool allEqual = true;
    for (int f = 0; f < F; ++f) {
        a.Step(inputs[f]);
        b.Step(inputs[f]);
        if (a.Checksum() != b.Checksum()) allEqual = false;
    }
    CHECK(allEqual);
    CHECK_EQ(a.Checksum(), b.Checksum());

    // 입력 하나만 달라도 결국 상태가 갈린다(checksum 민감도).
    DetState c; c.Init(P);
    auto inputs2 = inputs;
    inputs2[10][1].moveX ^= 1;
    for (int f = 0; f < F; ++f) c.Step(inputs2[f]);
    CHECK(c.Checksum() != a.Checksum());
}

// 두 lockstep 피어가 같은 입력을 (다른 순서로) 받아도 동일하게 전진.
static void TestLockstepSync() {
    const int P = 3, F = 200;
    auto inputs = MakeInputs(P, F, 4242);

    Lockstep peerA, peerB;
    peerA.Init(P, 2);
    peerB.Init(P, 2);

    bool synced = true;
    for (int f = 0; f < F; ++f) {
        // A 는 플레이어 오름차순, B 는 내림차순으로 입력 제출(순서 무관 검증).
        for (int p = 0; p < P; ++p)        peerA.SubmitInput((uint32_t)f, p, inputs[f][p]);
        for (int p = P - 1; p >= 0; --p)   peerB.SubmitInput((uint32_t)f, p, inputs[f][p]);
        CHECK(peerA.CanAdvance());
        CHECK(peerB.CanAdvance());
        peerA.Advance();
        peerB.Advance();
        if (peerA.Checksum() != peerB.Checksum()) synced = false;
    }
    CHECK(synced);
    CHECK_EQ(peerA.Frame(), (uint32_t)F);
}

// 입력 게이트: 모든 피어 입력이 모이기 전엔 전진 불가.
static void TestInputGate() {
    const int P = 3;
    Lockstep peer;
    peer.Init(P, 2);

    peer.SubmitInput(0, 0, Input{});
    CHECK(!peer.CanAdvance());          // 1/3
    peer.SubmitInput(0, 1, Input{});
    CHECK(!peer.CanAdvance());          // 2/3
    peer.SubmitInput(0, 2, Input{});
    CHECK(peer.CanAdvance());           // 3/3 → 전진 가능
    peer.Advance();
    CHECK_EQ(peer.Frame(), 1u);
    CHECK(!peer.CanAdvance());          // 다음 프레임 입력 없음 → 스톨
}

// lockstep 결과 == 단일 결정적 시뮬 결과(같은 입력).
static void TestLockstepEqualsSolo() {
    const int P = 2, F = 150;
    auto inputs = MakeInputs(P, F, 55);

    Lockstep peer; peer.Init(P, 2);
    DetState solo; solo.Init(P);
    for (int f = 0; f < F; ++f) {
        for (int p = 0; p < P; ++p) peer.SubmitInput((uint32_t)f, p, inputs[f][p]);
        peer.Advance();
        solo.Step(inputs[f]);
    }
    CHECK_EQ(peer.Checksum(), solo.Checksum());
}

static void RunTests() {
    TestDeterminism();
    TestLockstepSync();
    TestInputGate();
    TestLockstepEqualsSolo();
}

TEST_MAIN("mp_lockstep")
