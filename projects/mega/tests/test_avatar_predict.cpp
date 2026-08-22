// 아바타 예측: 되감은 결과가 "처음부터 알았을 경우"와 비트 동일해야 한다.
//
// 예측이 가치를 갖는 조건은 하나다 — 틀렸을 때 흔적 없이 고쳐질 것. 되감기 후
// 상태가 확정 입력만으로 처음부터 굴린 상태와 한 비트라도 다르면, 그 차이는
// 프레임마다 쌓여 결국 desync 가 된다. 그래서 이 파일의 중심 단언은 언제나
// "재시뮬 결과 == 지상 진실"이다.
//
// 함께 재는 것 셋.
//   - 두 팔(avatar_predict=1|0)이 **같은 상태**에 도달하고, 다른 것은 도달 시점뿐.
//   - 되감기 깊이가 상한 이하이고, 상한을 넘는 정정은 조용히 삼켜지지 않는다.
//   - 보관 창이 매치 길이에 비례해 자라지 않는다.

#include "mp/det_sim.hpp"
#include "mp/input.hpp"
#include "mp/rollback.hpp"
#include "test_util.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace mega::mp;

static std::vector<std::vector<Input>> MakeInputs(int players, int frames, uint32_t seed) {
    std::vector<std::vector<Input>> all(frames, std::vector<Input>(players));
    uint32_t s = seed ? seed : 1;
    auto next = [&] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; };
    for (int f = 0; f < frames; ++f)
        for (int p = 0; p < players; ++p) {
            Input in;
            in.moveX = static_cast<int8_t>((next() % 3) - 1);
            in.moveY = static_cast<int8_t>((next() % 3) - 1);
            in.SetFire((next() & 7) == 0);
            in.SetBuild((next() % 13) == 0);
            all[f][p] = in;
        }
    return all;
}

static uint64_t GroundTruth(const std::vector<std::vector<Input>>& inputs, int players,
                            int frames) {
    DetState state;
    state.Init(players);
    for (int f = 0; f < frames; ++f) state.Step(inputs[f]);
    return state.Checksum();
}

struct ArmReport {
    uint64_t checksum = 0;
    uint32_t frame = 0;
    uint64_t rollbacks = 0;
    uint64_t stalls = 0;
    uint64_t late = 0;
    int      maxDepth = 0;
    int      ticksAttempted = 0;
    size_t   retainedFrames = 0;
    size_t   retainedSlots = 0;
};

// 플레이어 0 = 로컬(즉시 확정), 1 = 원격(delay 프레임 뒤 도착).
// predict=false 면 원격이 도착할 때까지 Tick 이 전진을 거부한다.
static ArmReport RunArm(bool predict, int delay, int frames, uint32_t seed) {
    const int players = 2;
    const auto inputs = MakeInputs(players, frames + delay + 8, seed);

    Rollback rollback;
    rollback.Init(players, delay + 4);
    rollback.SetPrediction(predict);

    ArmReport report;
    // 한 스텝 = 네트워크 시간 한 칸. 로컬 입력은 그 자리에서 확정되고, 원격 입력은
    // delay 칸 뒤에 도착한다. 예측 팔은 매 스텝 전진하고, 비예측 팔은 원격이
    // 도착할 때까지 전진하지 못한다 — 그 차이가 두 팔의 전부다.
    const int maxSteps = frames + delay + 16;
    for (int step = 0; step < maxSteps && rollback.Frame() < static_cast<uint32_t>(frames);
         ++step) {
        if (step < frames)
            rollback.SetInput(static_cast<uint32_t>(step), 0, inputs[step][0], true);
        const int arrived = step - delay;
        if (arrived >= 0 && arrived < frames)
            rollback.SetInput(static_cast<uint32_t>(arrived), 1, inputs[arrived][1], true);
        ++report.ticksAttempted;
        rollback.Tick();
    }
    // 남은 원격 확정을 흘려보낸다(예측 팔의 마지막 되감기).
    for (int f = frames - delay; f < frames; ++f)
        if (f >= 0) rollback.SetInput(static_cast<uint32_t>(f), 1, inputs[f][1], true);

    report.checksum = rollback.Checksum();
    report.frame = rollback.Frame();
    report.rollbacks = rollback.Rollbacks();
    report.stalls = rollback.Stalls();
    report.late = rollback.LateConfirmations();
    report.maxDepth = rollback.MaxRollbackDepth();
    report.retainedFrames = rollback.RetainedFrames();
    report.retainedSlots = rollback.RetainedInputSlots();
    return report;
}

static void TestBothArmsReachTheSameState() {
    const int frames = 300, delay = 4;
    const uint32_t seed = 424242u;
    const auto inputs = MakeInputs(2, frames + delay + 8, seed);
    const uint64_t truth = GroundTruth(inputs, 2, frames);

    const ArmReport predicted = RunArm(true, delay, frames, seed);
    const ArmReport stalled = RunArm(false, delay, frames, seed);

    std::printf("  avatar_predict=1: 프레임 %u · 되감기 %llu회 · 최대 깊이 %d · "
                "멈춤 %llu · Tick 시도 %d\n",
                predicted.frame, (unsigned long long)predicted.rollbacks,
                predicted.maxDepth, (unsigned long long)predicted.stalls,
                predicted.ticksAttempted);
    std::printf("  avatar_predict=0: 프레임 %u · 되감기 %llu회 · 최대 깊이 %d · "
                "멈춤 %llu · Tick 시도 %d\n",
                stalled.frame, (unsigned long long)stalled.rollbacks,
                stalled.maxDepth, (unsigned long long)stalled.stalls,
                stalled.ticksAttempted);

    CHECK_EQ(predicted.frame, (uint32_t)frames);
    CHECK_EQ(stalled.frame, (uint32_t)frames);
    CHECK_EQ(predicted.checksum, truth);      // 되감은 결과가 지상 진실과 같다
    CHECK_EQ(stalled.checksum, truth);        // 멈춘 팔도 같은 상태에 도달한다
    CHECK(predicted.rollbacks > 0);           // 예측 팔은 실제로 빗나갔다
    CHECK_EQ(stalled.rollbacks, (uint64_t)0); // 멈춘 팔은 되감을 일이 없다
    CHECK(stalled.stalls > 0);                // 대신 멈췄다
    CHECK_EQ(predicted.stalls, (uint64_t)0);
    CHECK(predicted.maxDepth <= delay + 4);   // 상한 이하
    CHECK_EQ(predicted.late, (uint64_t)0);
    CHECK_EQ(stalled.late, (uint64_t)0);
}

static void TestDeeperDelayStillConverges() {
    const int frames = 400;
    for (int delay : {1, 4, 8}) {
        const uint32_t seed = 900u + static_cast<uint32_t>(delay);
        const auto inputs = MakeInputs(2, frames + delay + 8, seed);
        const ArmReport report = RunArm(true, delay, frames, seed);
        CHECK_EQ(report.frame, (uint32_t)frames);
        CHECK_EQ(report.checksum, GroundTruth(inputs, 2, frames));
        CHECK(report.maxDepth <= delay + 4);
        CHECK_EQ(report.late, (uint64_t)0);
    }
}

// 상한을 넘은 정정은 되감을 상태가 없다. 그것을 조용히 삼키면 이 피어는 호스트와
// 다른 세계를 들고 아무 일 없다는 듯 계속 돈다.
static void TestTooLateConfirmationIsReported() {
    const int players = 2;
    const auto inputs = MakeInputs(players, 200, 5150u);
    Rollback rollback;
    rollback.Init(players, 8);
    for (int f = 0; f < 100; ++f) {
        rollback.SetInput(static_cast<uint32_t>(f), 0, inputs[f][0], true);
        rollback.SetInput(static_cast<uint32_t>(f), 1, inputs[f][1], true);
        CHECK(rollback.Tick());
    }
    CHECK(!rollback.DesyncSuspected());
    CHECK_EQ(rollback.LateConfirmations(), (uint64_t)0);

    Input late = inputs[20][1];
    late.moveX = static_cast<int8_t>(-late.moveX);
    rollback.SetInput(20, 1, late, true);          // 92프레임 전 — 창 밖이다
    CHECK_EQ(rollback.LateConfirmations(), (uint64_t)1);
    CHECK(rollback.DesyncSuspected());
    CHECK_EQ(rollback.Frame(), (uint32_t)100);     // 상태는 건드리지 않는다
}

// 보관 창이 매치 길이에 비례해 자라지 않는다. 자라면 예측 한 번이 표 전체를
// 훑으므로 프레임 비용이 경기 시간에 비례한다.
static void TestRetentionIsBounded() {
    const int players = 2, frames = 3000, maxRollback = 8;
    const auto inputs = MakeInputs(players, frames, 616161u);
    Rollback rollback;
    rollback.Init(players, maxRollback);
    for (int f = 0; f < frames; ++f) {
        rollback.SetInput(static_cast<uint32_t>(f), 0, inputs[f][0], true);
        rollback.SetInput(static_cast<uint32_t>(f), 1, inputs[f][1], true);
        CHECK(rollback.Tick());
    }
    std::printf("  3000프레임 후 보관: 스냅샷 %zu · 입력 슬롯 %zu (상한 %d)\n",
                rollback.RetainedFrames(), rollback.RetainedInputSlots(), maxRollback);
    CHECK(rollback.RetainedFrames() <= (size_t)maxRollback + 1);
    CHECK(rollback.RetainedInputSlots() <= (size_t)maxRollback + 1);
    CHECK_EQ(rollback.Checksum(), GroundTruth(inputs, players, frames));
}

// 창 밖으로 밀린 확정 입력이 예측의 바닥으로 접혀 있어야 한다. 접지 않고 버리면
// 오래 침묵한 플레이어의 예측이 "입력 없음"으로 되돌아간다.
static void TestFoldedFloorKeepsPrediction() {
    const int players = 2;
    Rollback rollback;
    rollback.Init(players, 4);

    Input walking;
    walking.moveX = 1;
    Input still;

    // 프레임 0 에만 원격 입력이 확정되고, 그 뒤로는 오지 않는다.
    rollback.SetInput(0, 0, still, true);
    rollback.SetInput(0, 1, walking, true);
    CHECK(rollback.Tick());
    for (int f = 1; f < 40; ++f) {
        rollback.SetInput(static_cast<uint32_t>(f), 0, still, true);
        CHECK(rollback.Tick());                    // 원격은 예측으로 채운다
    }

    // 예측이 "직전 확정 반복"이었다면 원격 아바타는 40프레임 내내 오른쪽으로 갔다.
    DetState expected;
    expected.Init(players);
    std::vector<Input> frameInputs(players);
    frameInputs[0] = still;
    frameInputs[1] = walking;
    for (int f = 0; f < 40; ++f) expected.Step(frameInputs);
    CHECK_EQ(rollback.Checksum(), expected.Checksum());
    CHECK(rollback.State().px[1] > rollback.State().px[0]);
    CHECK_EQ(rollback.LateConfirmations(), (uint64_t)0);
}

static void RunTests() {
    TestBothArmsReachTheSameState();
    TestDeeperDelayStillConverges();
    TestTooLateConfirmationIsReported();
    TestRetentionIsBounded();
    TestFoldedFloorKeepsPrediction();
}

TEST_MAIN("avatar_predict")
