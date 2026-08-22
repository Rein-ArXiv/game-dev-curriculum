// 스톰프 — IK 출력이 권위 시뮬의 입력이 되는 자리.
//
// 보스가 한 걸음을 마칠 때마다 2본 IK 가 푼 **발** 좌표를 원점으로 반경 안의
// 타워가 피해를 받는다. 몸통 중심이 아니라 발이라는 것이 요점이다 — 몸통
// 중심이면 IK 는 그림일 뿐이고 지워도 게임이 같다.
//
// 그래서 이 파일이 묻는 것은 셋이다.
//
//   1. 호스트가 계산한 스톰프 원점과, **스냅샷만 보고** 클라이언트가 다시 푼
//      발 위치가 같은가. 같아야 화면에서 본 자리와 맞은 자리가 일치한다.
//   2. 그 원점이 실제로 타워를 깎고, 깎다 못해 부수는가.
//   3. 세 저장(aos·soa·ecs)이 같은 발을 같은 순서로 디디는가.

#include "game/defense_protocol.hpp"
#include "game/defense_view.hpp"
#include "game/game.hpp"
#include "test_util.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace mega;
using namespace mega::game;

namespace {

bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// 보스가 지나가는 길목에 타워를 세운다(셀프테스트 스크립트와 같은 자리들).
const DefenseMap::Cell kPlan[8] = {
    {6, 1}, {8, 2}, {10, 2}, {11, 4}, {13, 5}, {10, 6}, {8, 8}, {5, 8},
};

struct Fixture {
    GameWorld  world;
    DefenseMap map = DefenseMap::CreateDefault();

    void Start(EntityStoreKind kind, int bossCount = 1) {
        Switches switches;
        switches.entityStore = kind;
        world.Configure(switches);
        // 체력을 크게 잡아 보스가 매치 내내 살아 걷게 한다 — 스톰프를 재려면
        // 보스가 죽지 않아야 하고, 죽지 않게 만드는 가장 정직한 방법은 체력이다.
        world.Init(map, {WaveDef{bossCount, 400000.0f, 130.0f, 5, 0.6f, EnemyType::Boss}},
                   4000, 1.0e9f);
        for (const DefenseMap::Cell& cell : kPlan)
            world.PlaceTowerType(map.CellCenter(cell.x, cell.y), TowerType::Gatling);
    }

    int BossRow() const {
        const EnemyStore& enemies = world.Enemies();
        for (size_t i = 0; i < enemies.Size(); ++i)
            if (enemies.Alive(i) && enemies.Type(i) == EnemyType::Boss)
                return static_cast<int>(i);
        return -1;
    }
};

// ── 1. 호스트의 발과 클라이언트가 다시 푼 발 ────────────────────────
void TestClientRederivesTheSameFoot() {
    Fixture fixture;
    fixture.Start(EntityStoreKind::Aos);
    const BossPoses& poses = fixture.world.Poses();
    const float scale = fixture.world.BossScale();

    int compared = 0;
    int wireCompared = 0;
    uint32_t seen = 0;
    for (int tick = 0; tick < 4000 && compared < 6; ++tick) {
        fixture.world.Step(1.0f / 60.0f);
        if (fixture.world.Stomps() == seen) continue;
        seen = fixture.world.Stomps();
        const int row = fixture.BossRow();
        CHECK(row >= 0);
        if (row < 0) break;
        const math::Vec2 hostFoot = fixture.world.Enemies().LastStomp(static_cast<size_t>(row));

        // 클라이언트가 보는 것은 스냅샷뿐이다. 그 스냅샷에서 같은 IK 를 푼다.
        const wire::Snapshot view = CaptureView(fixture.world, static_cast<uint32_t>(tick));
        const wire::EnemyState* boss = nullptr;
        for (const auto& enemy : view.enemies)
            if (enemy.type == EnemyType::Boss) boss = &enemy;
        CHECK(boss != nullptr);
        if (!boss) break;
        const math::Vec2 replayed =
            BossPlantedFoot(poses, boss->pos, fixture.map.Path().back(), scale);
        // 스냅샷을 값으로 뜬 단계에서는 위치가 비트 그대로라 발도 비트 그대로다.
        CHECK(hostFoot.x == replayed.x);
        CHECK(hostFoot.y == replayed.y);
        ++compared;

        // 와이어를 실제로 건너면 위치가 1/8 px 로 양자화된다. 그 오차가 발까지
        // 얼마나 번지는가를 여기서 확인한다 — 사라지지 않고, 커지지도 않는다.
        const net::Bytes bytes = wire::WriteSnapshot(fixture.world, static_cast<uint32_t>(tick), 0);
        wire::Snapshot decoded;
        CHECK(wire::ReadSnapshot(bytes, decoded));
        for (const auto& enemy : decoded.enemies) {
            if (enemy.type != EnemyType::Boss) continue;
            const math::Vec2 overWire =
                BossPlantedFoot(poses, enemy.pos, fixture.map.Path().back(), scale);
            CHECK(Near(overWire.x, hostFoot.x, 0.5f));
            CHECK(Near(overWire.y, hostFoot.y, 0.5f));
            ++wireCompared;
        }
    }
    CHECK(compared >= 6);
    CHECK(wireCompared >= 6);
    std::printf("  발 대조 %d회(값) / %d회(와이어 왕복)\n", compared, wireCompared);
}

// ── 2. 그 발이 타워를 깎는다 ────────────────────────────────────────
void TestStompDamagesTowers() {
    Fixture fixture;
    fixture.Start(EntityStoreKind::Aos, 40);   // 보스 행렬이 같은 길목을 지난다
    const int placed = fixture.world.TowerCount();
    CHECK(placed > 0);

    float lowest = 1.0e9f;
    for (int tick = 0; tick < 12000; ++tick) {
        fixture.world.Step(1.0f / 60.0f);
        for (const Tower& tower : fixture.world.Towers())
            lowest = tower.hp < lowest ? tower.hp : lowest;
        if (fixture.world.TowerCount() < placed) break;
    }
    CHECK(fixture.world.Stomps() > 0);
    // 실제로 깎였고(체력이 초기값 아래로 내려갔고), 끝내 하나가 부서졌다.
    CHECK(lowest < kTowerBaseHp);
    CHECK(fixture.world.TowerCount() < placed);
    std::printf("  스톰프 %u회, 타워 %d → %d, 최저 체력 %.1f\n",
                fixture.world.Stomps(), placed, fixture.world.TowerCount(), lowest);

    // 피해 반경은 **발** 기준이다. 몸통 기준이면 이 단언이 성립할 이유가 없다.
    Fixture probe;
    probe.Start(EntityStoreKind::Aos);
    uint32_t seen = 0;
    bool checked = false;
    for (int tick = 0; tick < 4000 && !checked; ++tick) {
        probe.world.Step(1.0f / 60.0f);
        if (probe.world.Stomps() == seen) continue;
        seen = probe.world.Stomps();
        const int row = probe.BossRow();
        if (row < 0) break;
        const math::Vec2 foot = probe.world.Enemies().LastStomp(static_cast<size_t>(row));
        const math::Vec2 body = probe.world.Enemies().Pos(static_cast<size_t>(row));
        // 발은 몸통에서 떨어져 있다. 두 점이 같으면 "발 기준"이라는 말이 무의미하다.
        CHECK(math::Length(foot - body) > probe.world.BossScale() * 0.3f);
        checked = true;
    }
    CHECK(checked);
}

// ── 3. 세 저장이 같은 발을 디딘다 ───────────────────────────────────
void TestStoresStompAlike() {
    struct Trace {
        uint32_t stomps = 0;
        uint64_t hash = 0;
        std::vector<math::Vec2> feet;
    };
    auto run = [](EntityStoreKind kind) {
        Fixture fixture;
        fixture.Start(kind);
        Trace trace;
        uint32_t seen = 0;
        for (int tick = 0; tick < 3000; ++tick) {
            fixture.world.Step(1.0f / 60.0f);
            if (fixture.world.Stomps() == seen) continue;
            seen = fixture.world.Stomps();
            const int row = fixture.BossRow();
            if (row >= 0)
                trace.feet.push_back(fixture.world.Enemies().LastStomp(static_cast<size_t>(row)));
        }
        trace.stomps = fixture.world.Stomps();
        trace.hash = fixture.world.StateHash();
        return trace;
    };
    const Trace aos = run(EntityStoreKind::Aos);
    const Trace soa = run(EntityStoreKind::Soa);
    const Trace ecs = run(EntityStoreKind::Ecs);
    CHECK(aos.stomps > 4);
    CHECK_EQ(aos.stomps, soa.stomps);
    CHECK_EQ(aos.stomps, ecs.stomps);
    CHECK_EQ(aos.hash, soa.hash);
    CHECK_EQ(aos.hash, ecs.hash);
    CHECK_EQ(aos.feet.size(), soa.feet.size());
    CHECK_EQ(aos.feet.size(), ecs.feet.size());
    for (size_t i = 0; i < aos.feet.size() && i < ecs.feet.size(); ++i) {
        CHECK(aos.feet[i].x == soa.feet[i].x);
        CHECK(aos.feet[i].y == soa.feet[i].y);
        CHECK(aos.feet[i].x == ecs.feet[i].x);
        CHECK(aos.feet[i].y == ecs.feet[i].y);
    }
}

// ── 4. 리그가 없으면 시스템이 통째로 쉰다 ───────────────────────────
// 보스가 없는 웨이브에서 이 시스템은 아무 일도 하지 않아야 한다. ECS 팔은 그
// 판정을 조합 목록만 보고 내린다.
void TestNoBossNoStomp() {
    for (EntityStoreKind kind :
         {EntityStoreKind::Aos, EntityStoreKind::Soa, EntityStoreKind::Ecs}) {
        Switches switches;
        switches.entityStore = kind;
        GameWorld world;
        world.Configure(switches);
        const DefenseMap map = DefenseMap::CreateDefault();
        world.Init(map, {WaveDef{12, 60.0f, 40.0f, 5, 0.3f, EnemyType::Swarm}}, 4000, 1.0e9f);
        for (const DefenseMap::Cell& cell : kPlan)
            world.PlaceTowerType(map.CellCenter(cell.x, cell.y), TowerType::Gatling);
        for (int tick = 0; tick < 600; ++tick) world.Step(1.0f / 60.0f);
        CHECK_EQ(world.Stomps(), static_cast<uint32_t>(0));
        CHECK_EQ(world.Enemies().RigRows(), static_cast<size_t>(0));
        for (const Tower& tower : world.Towers()) CHECK(tower.hp == kTowerBaseHp);
    }
}

}  // namespace

static void RunTests() {
    TestClientRederivesTheSameFoot();
    TestStompDamagesTowers();
    TestStoresStompAlike();
    TestNoBossNoStomp();
}

TEST_MAIN("stomp")
