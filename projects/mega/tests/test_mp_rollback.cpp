// rollback 검증: 예측·되감기·재시뮬 후 상태가 "처음부터 안 경우"와 비트 동일.
#include "mp/det_sim.hpp"
#include "mp/input.hpp"
#include "mp/rollback.hpp"
#include "test_util.h"

#include <cstdint>
#include <vector>

using namespace mega::mp;

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

static uint64_t GroundTruth(const std::vector<std::vector<Input>>& inputs, int players, int F) {
    DetState s; s.Init(players);
    for (int f = 0; f < F; ++f) s.Step(inputs[f]);
    return s.Checksum();
}

// 플레이어 0=로컬(즉시), 1=원격(D프레임 지연). 예측 빗나감→되감기 후 정합.
static void RunRollback(int D, int F, uint32_t seed, bool expectRollback) {
    const int P = 2;
    auto inputs = MakeInputs(P, F + D + 4, seed);

    Rollback rb;
    rb.Init(P, D + 4);

    bool sawRollback = false;
    for (int f = 0; f < F; ++f) {
        rb.SetInput((uint32_t)f, 0, inputs[f][0], true);       // 로컬 즉시 확정
        if (f - D >= 0)                                        // 원격은 D 늦게 도착
            rb.SetInput((uint32_t)(f - D), 1, inputs[f - D][1], true);
        rb.Tick();
        if (rb.LastRollbackDepth() > 0) sawRollback = true;
    }
    // 남은 원격 입력 flush(마지막 D 프레임분) → 마지막 되감기·재시뮬.
    for (int f = F - D; f < F; ++f)
        if (f >= 0) rb.SetInput((uint32_t)f, 1, inputs[f][1], true);

    CHECK_EQ(rb.Frame(), (uint32_t)F);
    CHECK_EQ(rb.Checksum(), GroundTruth(inputs, P, F));        // 처음부터 안 것과 동일
    if (expectRollback) CHECK(sawRollback);                   // 실제로 되감기가 일어났다
}

static void TestRollbackConverges()  { RunRollback(4, 200, 123, true); }
static void TestLongerDelay()        { RunRollback(8, 200, 999, true); }
static void TestDelay1()             { RunRollback(1, 150, 7, true); }

// 예측이 우연히 맞으면(원격 입력 불변) 되감기 없이도 정합.
static void TestNoMispredict() {
    const int P = 2, F = 100, D = 3;
    std::vector<std::vector<Input>> inputs(F + D + 4, std::vector<Input>(P));
    for (auto& fr : inputs) { fr[0].moveX = 1; fr[1].moveX = -1; }   // 입력 불변
    Rollback rb; rb.Init(P, D + 4);
    for (int f = 0; f < F; ++f) {
        rb.SetInput((uint32_t)f, 0, inputs[f][0], true);
        if (f - D >= 0) rb.SetInput((uint32_t)(f - D), 1, inputs[f - D][1], true);
        rb.Tick();
    }
    for (int f = F - D; f < F; ++f) if (f >= 0) rb.SetInput((uint32_t)f, 1, inputs[f][1], true);
    CHECK_EQ(rb.Checksum(), GroundTruth(inputs, P, F));
}

static void RunTests() {
    TestRollbackConverges();
    TestLongerDelay();
    TestDelay1();
    TestNoMispredict();
}

TEST_MAIN("mp_rollback")
