// 아바타 배선: 사거리 게이트와 프레임 입력 스트림이 **제품 안에서** 도는가.
//
// mp/rollback.hpp · mp/det_sim.hpp · mp/input.hpp 의 단위 검사는 이미 있다
// (test_mp_rollback · test_desync_detect · test_avatar_input). 그 셋은 알고리즘이
// 옳다는 것만 말하고, 그 알고리즘이 게임에 연결돼 있는지는 말하지 않는다. 배선을
// 통째로 잘라도 그 셋은 전부 초록이다.
//
// 여기서 도는 것은 DefenseApp 그 자체다. 세 가지를 세운다.
//
//   1. avatar_gate 두 팔 — 팔 0은 아바타와 무관하게 어디든 짓고, 팔 1은
//      사거리 밖을 거절한다. 같은 클릭이 두 팔에서 다른 결과를 낸다.
//   2. 프레임 입력 스트림이 양방향으로 흐른다 — 클라이언트가 매 프레임 보내고,
//      호스트가 받아 원격 아바타를 먹이고, 호스트도 자기 입력을 되보낸다.
//   3. 체크섬 대조가 실제로 일어나고(비교 횟수 > 0) 갈라지지 않는다(desync = 0).
//
// 소켓은 흉내가 아니라 진짜 루프백 UDP 한 쌍이다. 대기는 전부 "조건이 참이 될
// 때까지 프레임을 돌린다 + 데드라인" 형태라 고정 sleep 이 없다.

#include "game/defense_app.hpp"
#include "mp/det_sim.hpp"
#include "test_util.h"

#include <chrono>
#include <cstdio>

using namespace mega;
using namespace mega::game;

namespace {
using Clock = std::chrono::steady_clock;

// 계획한 칸을 클릭하는 한 프레임. 카메라가 항등인 팔이라 월드 좌표가 곧 화면
// 좌표지만, 제품과 같은 함수를 지나게 해 둔다.
FrameInput ClickAt(const DefenseApp& app, const math::Vec2& world) {
    FrameInput frame;
    const math::Vec2 screen = app.Camera().Project(world);
    frame.mouseX = static_cast<int>(screen.x);
    frame.mouseY = static_cast<int>(screen.y);
    frame.mouseClicked = true;
    frame.Press('1');                       // 캐논
    return frame;
}

// 아바타에서 가장 먼 건설 가능 칸. "사거리 밖"이 우연이 아니어야 한다.
DefenseMap::Cell FarBuildableCell(const DefenseApp& app) {
    const DefenseMap& map = app.Map();
    DefenseMap::Cell best{-1, -1};
    int bestDistance = -1;
    const int32_t here = app.AvatarCell();
    for (int y = 0; y < map.Rows(); ++y) {
        for (int x = 0; x < map.Columns(); ++x) {
            if (map.Tile(x, y) != TileType::Buildable || map.Occupied(x, y)) continue;
            const int32_t cell = app.AvatarCellOf(map.CellCenter(x, y));
            const int32_t dx = (cell % mp::DetState::kCells) - (here % mp::DetState::kCells);
            const int32_t dy = (cell / mp::DetState::kCells) - (here / mp::DetState::kCells);
            const int distance = static_cast<int>(dx * dx + dy * dy);
            if (distance > bestDistance) { bestDistance = distance; best = {x, y}; }
        }
    }
    return best;
}

}  // namespace

// ── 1. 사거리 게이트 두 팔 ─────────────────────────────────────────
// 같은 칸, 같은 클릭. 팔 0은 선다. 팔 1은 서지 않고 거절이 세어진다.
static void TestBuildReachGate(bool gate, int& towersOut, uint64_t& rejectedOut,
                               int& walkTicksOut) {
    DefenseOptions options;
    options.mode = RunMode::Solo;
    options.switches.avatarGate = gate;
    options.profilePath = "avatar_wiring_selftest.wal";
    // 두 팔이 같은 파일을 쓴다. 비우지 않으면 **둘째 팔이 첫째 팔의 지갑을
    // 물려받아** 출발하고, 그러면 "같은 칸·같은 클릭"이라는 이 검사의 전제가
    // 팔 하나에서만 참이 된다. 비우고 출발해야 두 팔이 정말 같은 조건이다.
    CHECK(TruncateFile(options.profilePath));

    DefenseApp app;
    CHECK(app.Init(options));

    // 아바타가 실제로 움직이려면 한 프레임은 돌아야 한다(Init 은 프레임이 아니다).
    app.Step(FrameInput{}, kDefenseFixedDt);

    const DefenseMap::Cell distant = FarBuildableCell(app);
    CHECK(distant.x >= 0);
    const math::Vec2 spot = app.Map().CellCenter(distant.x, distant.y);
    CHECK(!mp::DetState::CellsWithinReach(app.AvatarCell(), app.AvatarCellOf(spot),
                                          wire::kBuildReach));

    // 클릭 한 번. 골드는 충분하다(기본 밸런스의 시작 골드로 캐논 하나는 선다).
    app.Step(ClickAt(app, spot), kDefenseFixedDt);
    towersOut = app.World().TowerCount();
    rejectedOut = app.BuildRejectedOutOfReach();

    // 팔 1에서는 걸어가면 서야 한다. "영원히 못 짓는다"는 게이트가 아니라 결함이다.
    walkTicksOut = 0;
    if (gate) {
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (Clock::now() < deadline &&
               !mp::DetState::CellsWithinReach(app.AvatarCell(), app.AvatarCellOf(spot),
                                               wire::kBuildReach)) {
            FrameInput frame;
            const int32_t here = app.AvatarCell();
            const int32_t target = app.AvatarCellOf(spot);
            const int32_t hx = here % mp::DetState::kCells, hy = here / mp::DetState::kCells;
            const int32_t tx = target % mp::DetState::kCells, ty = target / mp::DetState::kCells;
            if (tx > hx) frame.Press('D'); else if (tx < hx) frame.Press('A');
            if (ty > hy) frame.Press('S'); else if (ty < hy) frame.Press('W');
            app.Step(frame, kDefenseFixedDt);
            ++walkTicksOut;
        }
        CHECK(mp::DetState::CellsWithinReach(app.AvatarCell(), app.AvatarCellOf(spot),
                                             wire::kBuildReach));
        app.Step(ClickAt(app, spot), kDefenseFixedDt);
        CHECK_EQ(app.World().TowerCount(), 1);      // 걸어갔더니 선다
    }
}

static void TestAvatarGateArms() {
    int towersOff = -1, towersOn = -1, walkOff = 0, walkOn = 0;
    uint64_t rejectedOff = 999, rejectedOn = 999;
    TestBuildReachGate(false, towersOff, rejectedOff, walkOff);
    TestBuildReachGate(true, towersOn, rejectedOn, walkOn);

    // (a) 결과가 다르다 — 이것이 스위치가 무언가를 바꾼다는 증거다.
    CHECK_EQ(towersOff, 1);          // 팔 0: 아바타와 무관하게 선다
    CHECK_EQ(towersOn, 0);           // 팔 1: 사거리 밖이라 안 선다
    CHECK_EQ(rejectedOff, 0ull);     // 팔 0은 거절 경로를 지나지 않는다
    CHECK_EQ(rejectedOn, 1ull);      // 팔 1은 정확히 한 번 거절했다
    CHECK(walkOn > 0);               // 걸어야 닿는 거리였다

    std::printf("  avatar_gate: off towers=%d rejected=%llu / on towers=%d rejected=%llu "
                "walk=%d틱\n",
                towersOff, static_cast<unsigned long long>(rejectedOff),
                towersOn, static_cast<unsigned long long>(rejectedOn), walkOn);
}

// ── 2·3. 프레임 입력 스트림과 체크섬 대조 ──────────────────────────
// 호스트도 클라이언트도 제품 DefenseApp 이다. 둘 다 같은 2인 서브시뮬을 굴리고,
// 그 두 사본이 갈라지지 않는지를 호스트가 스냅샷에 실은 (프레임, 체크섬)으로
// 클라이언트가 확인한다.
struct PairResult {
    uint64_t hostReceived = 0;
    uint64_t clientSent = 0;
    uint64_t clientReceived = 0;
    uint64_t comparisons = 0;
    uint64_t desyncs = 0;
    uint64_t stalls = 0;
    uint64_t rollbacks = 0;
    uint32_t clientFrame = 0;
    uint32_t clientChecksum = 0;
};

static void RunPair(bool predict, PairResult& out) {
    DefenseOptions hostOptions;
    hostOptions.mode = RunMode::Host;
    hostOptions.port = 0;                    // 임의 포트
    hostOptions.profilePath = "avatar_wiring_host.wal";
    // 예측 켠 팔과 끈 팔이 같은 파일을 쓴다 — 비우고 출발해야 두 팔이 같은 조건이다.
    CHECK(TruncateFile(hostOptions.profilePath));
    DefenseApp host;
    CHECK(host.Init(hostOptions));

    DefenseOptions clientOptions;
    clientOptions.mode = RunMode::Client;
    clientOptions.address = "127.0.0.1";
    clientOptions.port = host.LocalPort();
    clientOptions.switches.avatarPredict = predict;
    DefenseApp client;
    CHECK(client.Init(clientOptions));
    CHECK(clientOptions.port != 0);

    // 클라이언트는 계속 오른쪽으로 걷고, 호스트는 계속 아래로 걷는다. 둘 다
    // 움직여야 "상대 입력을 못 받으면 갈라진다"가 실제 위험이 된다.
    FrameInput clientInput;
    clientInput.Press('D');
    FrameInput hostInput;
    hostInput.Press('S');

    // 고정 반복 수. 시간 조건으로 끝내면 머신 속도가 결과를 정한다.
    for (int i = 0; i < 900; ++i) {
        host.Step(hostInput, kDefenseFixedDt);
        client.Step(clientInput, kDefenseFixedDt);
    }

    out.hostReceived = host.AvatarInputPacketsReceived();
    out.clientSent = client.AvatarInputPacketsSent();
    out.clientReceived = client.AvatarInputPacketsReceived();
    out.comparisons = client.AvatarComparisons();
    out.desyncs = client.AvatarDesyncs();
    out.stalls = client.AvatarStalls();
    out.rollbacks = client.AvatarRollbacks();
    out.clientFrame = client.AvatarFrame();
    out.clientChecksum = client.AvatarChecksum32();
}

static void TestAvatarStreamAndChecksum() {
    PairResult predicted;
    RunPair(true, predicted);

    // 배선 그 자체.
    CHECK(predicted.clientSent > 0);      // ← PumpClient 의 SendAvatarInput 을 지우면 죽는다
    CHECK(predicted.hostReceived > 0);    // ← PumpHost 의 FeedAvatarInput 을 지우면 죽는다
    CHECK(predicted.clientReceived > 0);  // ← 호스트가 자기 입력을 안 보내면 죽는다
    // 대조가 실제로 일어났고, 두 사본이 갈라지지 않았다.
    CHECK(predicted.comparisons > 0);     // ← 스냅샷의 avatarFrame 을 안 실으면 죽는다
    CHECK_EQ(predicted.desyncs, 0ull);

    std::printf("  아바타 스트림(predict=1): client_sent=%llu host_recv=%llu "
                "client_recv=%llu 대조=%llu desync=%llu frame=%u\n",
                static_cast<unsigned long long>(predicted.clientSent),
                static_cast<unsigned long long>(predicted.hostReceived),
                static_cast<unsigned long long>(predicted.clientReceived),
                static_cast<unsigned long long>(predicted.comparisons),
                static_cast<unsigned long long>(predicted.desyncs), predicted.clientFrame);

    // ── avatar_predict 두 팔 ──
    // 규약: (a) 결과가 같다 — 어느 팔에서도 두 사본이 갈라지지 않는다.
    //       (b) 비용이 다르다 — 예측 팔은 멈추지 않고 되감고, 비예측 팔은 멈춘다.
    PairResult stalled;
    RunPair(false, stalled);
    CHECK(stalled.comparisons > 0);       // 대조가 0 이면 desync=0 은 공허하다
    CHECK_EQ(stalled.desyncs, 0ull);
    CHECK_EQ(predicted.stalls, 0ull);     // 예측 팔은 원격을 기다리며 멈추지 않는다
    CHECK(stalled.stalls > 0);            // 비예측 팔은 멈춘다 — 그것이 비용이다
    CHECK(stalled.clientFrame < predicted.clientFrame);

    std::printf("  avatar_predict: 1 → stalls=%llu rollbacks=%llu frame=%u 대조=%llu / "
                "0 → stalls=%llu rollbacks=%llu frame=%u 대조=%llu\n",
                static_cast<unsigned long long>(predicted.stalls),
                static_cast<unsigned long long>(predicted.rollbacks), predicted.clientFrame,
                static_cast<unsigned long long>(predicted.comparisons),
                static_cast<unsigned long long>(stalled.stalls),
                static_cast<unsigned long long>(stalled.rollbacks), stalled.clientFrame,
                static_cast<unsigned long long>(stalled.comparisons));
}

// ── 4. snapshot_mode 두 팔 (제품 호스트 ↔ 제품 클라이언트) ────────
// test_defense_client_loop 은 코덱과 복원을 잰다. 여기서 재는 것은 **한 판
// 전체에서 호스트가 실제로 내보낸 바이트**다. 두 팔의 클라이언트가 같은 세계를
// 보고(타워 수·웨이브), 든 비용만 다르다는 것이 판정이다.
struct SnapshotArm {
    double   meanBytes = 0;
    double   maxBytes = 0;
    uint64_t snapshots = 0;
    uint64_t baselineMiss = 0;
    size_t   clientTowers = 0;
    uint16_t clientWave = 0;
    int      clientGold = 0;
};

static void RunSnapshotArm(SnapshotMode mode, SnapshotArm& out) {
    DefenseOptions hostOptions;
    hostOptions.mode = RunMode::Host;
    hostOptions.port = 0;
    hostOptions.profilePath = "avatar_wiring_host.wal";
    // 두 스냅샷 팔도 같은 파일을 쓴다 — 비우고 출발해야 두 팔이 같은 조건이다.
    CHECK(TruncateFile(hostOptions.profilePath));
    hostOptions.switches.snapshotMode = mode;
    DefenseApp host;
    CHECK(host.Init(hostOptions));

    DefenseOptions clientOptions;
    clientOptions.mode = RunMode::Client;
    clientOptions.address = "127.0.0.1";
    clientOptions.port = host.LocalPort();
    DefenseApp client;
    CHECK(client.Init(clientOptions));

    FrameInput idle;
    for (int i = 0; i < 1200; ++i) {
        host.Step(idle, kDefenseFixedDt);
        client.Step(idle, kDefenseFixedDt);
    }

    live::Histogram& bytes = host.SnapshotBytesHistogram();
    out.meanBytes = bytes.Mean();
    out.maxBytes = bytes.Max();
    out.snapshots = bytes.Count();
    out.baselineMiss = client.Metrics().GetCounter("snapshot_baseline_miss").Value();
    out.clientTowers = client.View().towers.size();
    out.clientWave = client.View().wave;
    out.clientGold = client.View().gold;
}

static void TestSnapshotModeArms() {
    SnapshotArm full, delta;
    RunSnapshotArm(SnapshotMode::Full, full);
    RunSnapshotArm(SnapshotMode::Delta, delta);

    CHECK(full.snapshots > 100);
    CHECK(delta.snapshots > 100);
    CHECK_EQ(delta.baselineMiss, 0ull);        // 호스트가 없는 기준선을 가리키지 않았다
    // (a) 결과 동일 — 두 팔의 클라이언트가 같은 세계를 본다.
    CHECK_EQ(full.clientTowers, delta.clientTowers);
    CHECK_EQ(full.clientWave, delta.clientWave);
    CHECK_EQ(full.clientGold, delta.clientGold);
    // (b) 비용 상이 — 차분 팔이 실제로 더 적게 내보낸다.
    CHECK(delta.meanBytes < full.meanBytes);
    CHECK(delta.maxBytes <= full.maxBytes);

    std::printf("  snapshot_mode: full 평균 %.1fB 최대 %.0fB (%llu장) / "
                "delta 평균 %.1fB 최대 %.0fB (%llu장) = %.1f%%\n",
                full.meanBytes, full.maxBytes,
                static_cast<unsigned long long>(full.snapshots),
                delta.meanBytes, delta.maxBytes,
                static_cast<unsigned long long>(delta.snapshots),
                100.0 * delta.meanBytes / full.meanBytes);
}

static void RunTests() {
    TestAvatarGateArms();
    TestAvatarStreamAndChecksum();
    TestSnapshotModeArms();
}

TEST_MAIN("avatar_wiring")
