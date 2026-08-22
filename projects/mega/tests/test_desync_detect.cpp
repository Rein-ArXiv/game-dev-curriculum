// desync 탐지: 체크섬이 갈라짐을 놓치지 않아야 한다.
//
// 아바타 서브시뮬이 정수인 이유는 하나다 — 같은 입력 열이 어떤 실행에서도 비트
// 동일한 상태를 내야 클라이언트의 예측을 호스트의 확정과 대조할 수 있기 때문이다.
// 그 대조의 단위가 체크섬이므로, 체크섬이 **갈라짐을 놓치는 경우**가 있으면
// desync 탐지 전체가 무의미해진다.
//
// 이 파일이 세우는 것 넷.
//   1. 같은 입력 열 → 매 프레임 같은 체크섬.
//   2. 입력 한 비트 차이 → 체크섬이 갈리고 **다시 붙지 않는다**.
//   3. 경계에 눌린 아바타의 칸 번호가 격자 밖으로 나가지 않는다.
//   4. 상태 사본이 되감기 스냅샷으로 충분하다.

#include "mp/det_sim.hpp"
#include "mp/input.hpp"
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
            in.SetBuild((next() % 11) == 0);
            all[f][p] = in;
        }
    return all;
}

static void TestSameInputsSameChecksum() {
    const int players = 2, frames = 500;
    const auto inputs = MakeInputs(players, frames, 20260822u);
    DetState a, b;
    a.Init(players);
    b.Init(players);
    bool everDiffered = false;
    for (int f = 0; f < frames; ++f) {
        a.Step(inputs[f]);
        b.Step(inputs[f]);
        if (a.Checksum() != b.Checksum()) everDiffered = true;
    }
    CHECK(!everDiffered);
    CHECK_EQ(a.frame, b.frame);
    CHECK_EQ(a.Checksum(), b.Checksum());
}

static void TestOneBitDivergesAndStaysDiverged() {
    const int players = 2, frames = 500;
    auto inputs = MakeInputs(players, frames, 777u);
    auto flipped = inputs;
    flipped[10][1].moveX = static_cast<int8_t>(-flipped[10][1].moveX);
    if (flipped[10][1] == inputs[10][1]) flipped[10][1].moveX = 1;   // 0 이었으면 강제로 다르게

    DetState honest, forked;
    honest.Init(players);
    forked.Init(players);
    int firstDivergence = -1, reconvergences = 0;
    bool diverged = false;
    for (int f = 0; f < frames; ++f) {
        honest.Step(inputs[f]);
        forked.Step(flipped[f]);
        const bool same = honest.Checksum() == forked.Checksum();
        if (!same && !diverged) { diverged = true; firstDivergence = f; }
        if (same && diverged) ++reconvergences;
    }
    std::printf("  입력 1개 차이: 첫 갈림 프레임 %d · 이후 다시 붙은 횟수 %d\n",
                firstDivergence, reconvergences);
    CHECK(diverged);
    CHECK_EQ(firstDivergence, 10);
    CHECK_EQ(reconvergences, 0);
}

// 위치만 비교하면 놓치는 경우. 두 실행이 갈렸다가 양쪽 다 같은 벽에 눌리면
// 좌표가 도로 같아진다 — 이력이 없으면 그 순간 체크섬이 일치해 "desync 가
// 없었다"가 된다.
static void TestWallHidesPositionButNotTrail() {
    const int players = 1;
    const int push = DetState::kWorld / DetState::kSpeed + 20;   // 벽까지 충분히

    std::vector<Input> right(players);
    right[0].moveX = 1;
    std::vector<Input> left(players);
    left[0].moveX = -1;

    DetState straight, detoured;
    straight.Init(players);
    detoured.Init(players);
    // 갈림: 한쪽만 처음 두 프레임에 왼쪽으로 갔다가 돌아온다.
    detoured.Step(left);
    detoured.Step(right);
    straight.Step(right);
    straight.Step(right);
    CHECK(straight.px[0] != detoured.px[0]);          // 실제로 갈렸다

    for (int f = 0; f < push; ++f) { straight.Step(right); detoured.Step(right); }

    CHECK_EQ(straight.px[0], DetState::kWorld);       // 둘 다 벽에 눌렸다
    CHECK_EQ(detoured.px[0], DetState::kWorld);
    CHECK_EQ(straight.py[0], detoured.py[0]);         // 좌표가 완전히 같다
    CHECK(straight.trail[0] != detoured.trail[0]);    // 이력은 갈린 채로 남는다
    CHECK(straight.Checksum() != detoured.Checksum());
    std::printf("  벽에 눌린 두 실행: 좌표 동일(%d) · 이력 %08x vs %08x\n",
                straight.px[0], straight.trail[0], detoured.trail[0]);
}

// 상태에 남는 칸 번호는 어떤 좌표에서도 격자 안이다. Step 안의 좌표 클램프와
// CellOf 안의 칸 클램프가 겹쳐 있어서, 둘 중 하나만 살아 있어도 이 성질이
// 유지된다 — 그래서 두 줄의 **순서**는 이 검사로 관측되지 않는다. 그 사실을
// 숨기지 않고 여기 적어 둔다. 이 검사가 지키는 것은 순서가 아니라 결과다.
static void TestCellsStayInsideTheGrid() {
    const int players = 1;
    std::vector<Input> pushBuild(players);
    pushBuild[0].moveX = 1;
    pushBuild[0].moveY = 1;
    pushBuild[0].SetBuild(true);

    DetState state;
    state.Init(players);
    const int steps = DetState::kWorld / DetState::kSpeed + 50;
    for (int f = 0; f < steps; ++f) state.Step(pushBuild);

    CHECK_EQ(state.px[0], DetState::kWorld);
    CHECK_EQ(state.py[0], DetState::kWorld);
    const int32_t cells = DetState::kCells * DetState::kCells;
    CHECK(state.buildCell[0] >= 0);
    CHECK(state.buildCell[0] < cells);                 // 격자 밖이 아니다
    CHECK_EQ(state.buildCell[0], cells - 1);           // 마지막 칸
    CHECK_EQ(state.Cell(0), cells - 1);

    // 반대쪽 벽도 같다.
    std::vector<Input> pull(players);
    pull[0].moveX = -1;
    pull[0].moveY = -1;
    pull[0].SetBuild(true);
    for (int f = 0; f < 2 * steps; ++f) state.Step(pull);
    CHECK_EQ(state.px[0], 0);
    CHECK_EQ(state.buildCell[0], 0);

    // 버튼을 떼면 요청이 사라진다(요청은 프레임 단위 사실이다).
    std::vector<Input> idle(players);
    state.Step(idle);
    CHECK_EQ(state.buildCell[0], DetState::kNoBuild);
}

static void TestReachRule() {
    // 건설 승인 규칙의 커널. 호스트와 클라이언트가 같은 함수를 부른다.
    const int32_t center = DetState::CellIndex(DetState::kWorld / 2, DetState::kWorld / 2);
    CHECK(DetState::CellsWithinReach(center, center, 0));
    CHECK(DetState::CellsWithinReach(center, center + 1, 1));
    CHECK(DetState::CellsWithinReach(center, center + DetState::kCells, 1));
    CHECK(DetState::CellsWithinReach(center, center + DetState::kCells + 1, 1));   // 대각
    CHECK(!DetState::CellsWithinReach(center, center + 2, 1));
    CHECK(!DetState::CellsWithinReach(center, center + 2 * DetState::kCells, 1));
    CHECK(!DetState::CellsWithinReach(center, DetState::kNoBuild, 3));

    // 행 끝에서 다음 행 첫 칸으로 넘어가는 자리는 인접이 아니다 — 1차원 번호로만
    // 보면 차이가 1이라 붙어 보인다.
    const int32_t rowEnd = DetState::CellIndex(DetState::kWorld - 1, 0);
    CHECK_EQ(rowEnd, DetState::kCells - 1);
    CHECK(!DetState::CellsWithinReach(rowEnd, rowEnd + 1, 1));
}

static void TestSnapshotRestoreIsExact() {
    const int players = 3, frames = 200;
    const auto inputs = MakeInputs(players, frames, 31337u);
    DetState live;
    live.Init(players);
    for (int f = 0; f < 60; ++f) live.Step(inputs[f]);

    const DetState saved = live;                      // 되감기 스냅샷
    const uint64_t savedChecksum = live.Checksum();
    for (int f = 60; f < frames; ++f) live.Step(inputs[f]);
    const uint64_t advanced = live.Checksum();
    CHECK(advanced != savedChecksum);

    DetState rewound = saved;                          // 되감고 같은 입력으로 재시뮬
    CHECK_EQ(rewound.Checksum(), savedChecksum);
    for (int f = 60; f < frames; ++f) rewound.Step(inputs[f]);
    CHECK_EQ(rewound.Checksum(), advanced);
    CHECK(rewound.px == live.px);
    CHECK(rewound.py == live.py);
    CHECK(rewound.score == live.score);
    CHECK(rewound.trail == live.trail);
    CHECK(rewound.buildCell == live.buildCell);
}

static void RunTests() {
    TestSameInputsSameChecksum();
    TestOneBitDivergesAndStaysDiverged();
    TestWallHidesPositionButNotTrail();
    TestCellsStayInsideTheGrid();
    TestReachRule();
    TestSnapshotRestoreIsExact();
}

TEST_MAIN("desync_detect")
