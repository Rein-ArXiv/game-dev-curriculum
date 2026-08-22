// id_map=linear|hash — id 대응을 어떻게 찾든 같은 답이 나오는가.
//
// 두 자리에 걸린 스위치다.
//   (1) 클라이언트 보간: wire::SnapshotInterpolator 가 a 의 개체를 id 로 찾는다.
//   (2) 시뮬: EnemyStore::FindRow 가 발사체의 targetId 를 행으로 바꾼다.
// 둘 다 지금은 선형 스캔이고, 개체 n·m 에서 O(n*m) 이다. 해시 팔은 그 곱을 합으로
// 바꾼다 — 대신 삭제·재삽입에서 탐사 사슬이 끊기면 조회가 조용히 실패한다. 그
// 실패 모드가 이 파일이 지키는 것이다.

#include "containers/hash_map.h"
#include "game/defense_protocol.hpp"
#include "game/game.hpp"
#include "profiler/trace.h"
#include "test_util.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace mega;
using namespace mega::game;

namespace {

// 프로파일러가 이름별로 접어 둔 총 시간(ms). step08 에서 프레임에 박은 그
// ScopeTimer 들이 여기서 A/B 의 계량기가 된다 — 벽시계 총합은 "달랐다"까지만
// 말하고, 어느 시스템에서 달랐는지는 이 수가 말한다.
double ScopeTotal(const char* name) {
    for (const profiler::AggEntry& entry :
         profiler::AggregateByName(profiler::Profiler::Get().Snapshot()))
        if (entry.name == name) return entry.totalMs;
    return 0.0;
}

bool Same(const math::Vec2& a, const math::Vec2& b) {
    return std::fabs(a.x - b.x) < 1e-6f && std::fabs(a.y - b.y) < 1e-6f;
}

wire::Snapshot MakeSnapshot(uint32_t tick, float offset, int count) {
    wire::Snapshot s;
    s.tick = tick;
    s.coreHp = 100.0f - offset;
    s.coreMaxHp = 100.0f;
    for (int i = 0; i < count; ++i) {
        wire::EnemyState e;
        e.id = static_cast<uint32_t>(i + 1);
        e.pos = {static_cast<float>(i) * 3.0f + offset, 40.0f + offset};
        e.hp = 30.0f - offset;
        e.maxHp = 30.0f;
        s.enemies.push_back(e);
        if (i % 3 == 0) {
            wire::ProjectileState p;
            p.id = static_cast<uint32_t>(1000 + i);
            p.pos = {static_cast<float>(i) - offset, 10.0f + offset};
            s.projectiles.push_back(p);
        }
    }
    return s;
}

// 같은 스냅샷 쌍에 대해 두 팔이 같은 보간을 낸다.
void TestInterpolationAgrees() {
    const wire::Snapshot a = MakeSnapshot(10, 0.0f, 40);
    wire::Snapshot b = MakeSnapshot(13, 6.0f, 40);
    // b 에만 있는 개체(막 등장) — 보간할 과거가 없으므로 b 값 그대로여야 한다.
    wire::EnemyState fresh;
    fresh.id = 9999;
    fresh.pos = {123.0f, 45.0f};
    fresh.hp = fresh.maxHp = 12.0f;
    b.enemies.push_back(fresh);

    wire::SnapshotInterpolator linear;
    wire::SnapshotInterpolator hashed;
    hashed.SetMode(IdMapKind::Hash);

    for (float t : {0.0f, 0.25f, 0.5f, 1.0f}) {
        const wire::Snapshot x = linear.Blend(a, b, t);
        const wire::Snapshot y = hashed.Blend(a, b, t);
        CHECK_EQ(x.enemies.size(), y.enemies.size());
        CHECK_EQ(x.projectiles.size(), y.projectiles.size());
        CHECK(x.coreHp == y.coreHp);
        for (size_t i = 0; i < x.enemies.size(); ++i) {
            CHECK_EQ(x.enemies[i].id, y.enemies[i].id);
            CHECK(Same(x.enemies[i].pos, y.enemies[i].pos));
            CHECK(x.enemies[i].hp == y.enemies[i].hp);
        }
        for (size_t i = 0; i < x.projectiles.size(); ++i)
            CHECK(Same(x.projectiles[i].pos, y.projectiles[i].pos));
        CHECK(Same(x.enemies.back().pos, fresh.pos));   // 과거 없는 개체는 그대로
    }

    // 두 번째 호출도 같아야 한다 — 맵을 재사용하므로 Clear 가 이전 세대를 완전히
    // 지우지 못하면 여기서 갈라진다.
    CHECK(Same(hashed.Blend(a, b, 0.5f).enemies[0].pos,
               linear.Blend(a, b, 0.5f).enemies[0].pos));
}

// backward-shift 삭제가 탐사 사슬을 끊지 않는가. 끊기면 Find 가 nullptr 을
// 돌려주고, 제품에서는 "파티클이 안 난다"·"발사체가 표적을 잃는다"로 나타난다.
void TestEraseThenReinsertKeepsLookups() {
    OpenAddressingHashMap<uint32_t, uint32_t> map;
    const uint32_t count = 512;
    for (uint32_t i = 0; i < count; ++i) map.Insert(i, i * 7 + 1);
    for (uint32_t i = 0; i < count; i += 2) CHECK(map.Erase(i));
    CHECK_EQ(map.Size(), static_cast<size_t>(count / 2));
    for (uint32_t i = 1; i < count; i += 2) {
        const uint32_t* v = map.Find(i);
        CHECK(v != nullptr);
        if (v) CHECK_EQ(*v, i * 7 + 1);
    }
    for (uint32_t i = 0; i < count; i += 2) CHECK(map.Find(i) == nullptr);
    // 지운 자리를 다시 채워도 조회가 산다.
    for (uint32_t i = 0; i < count; i += 2) map.Insert(i, i * 13 + 2);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t* v = map.Find(i);
        CHECK(v != nullptr);
        if (v) CHECK_EQ(*v, (i % 2 == 0) ? i * 13 + 2 : i * 7 + 1);
    }
}

// 시뮬 쪽 스위치. 두 팔의 상태 해시가 같아야 한다.
void TestSimulationAgrees() {
    auto run = [](IdMapKind mode) {
        Switches switches;
        switches.idMap = mode;
        switches.scale = ScaleKind::Large;     // 곱이 합으로 바뀌는 것이 보이는 규모
        GameWorld world;
        world.Configure(switches);
        const DefenseMap map = DefenseMap::CreateDefault();
        world.Init(map, GenerateWaves(6), 4000, 100000.0f);
        world.PlaceTowerType(map.CellCenter(6, 1), TowerType::Gatling);
        world.PlaceTowerType(map.CellCenter(8, 2), TowerType::Splash);
        world.PlaceTowerType(map.CellCenter(11, 4), TowerType::Cannon);
        for (int i = 0; i < 1500; ++i) world.Step(1.0f / 60.0f);
        return world.StateHash();
    };
    CHECK_EQ(run(IdMapKind::Linear), run(IdMapKind::Hash));
}


// 선형 스캔과 해시 조회의 비용. 발사체 하나가 표적을 찾을 때마다 적을 전수
// 스캔하므로, 적 n·발사체 m 에서 곱이 된다.
void ReportIdMapCost(int towerCount, const char* label) {
    auto run = [towerCount](IdMapKind mode, int ticks) {
        Switches switches;
        switches.idMap = mode;
        switches.scale = ScaleKind::Large;
        GameWorld world;
        world.Configure(switches);
        const DefenseMap map = DefenseMap::CreateDefault();
        std::vector<WaveDef> waves(4, WaveDef{9000, 900.0f, 30.0f, 1, 0.0f, EnemyType::Normal});
        world.Init(map, waves, 4000, 1e9f);
        const DefenseMap::Cell cells[16] = {
            {6, 1}, {8, 1}, {6, 2}, {8, 2}, {6, 3}, {8, 4}, {11, 2}, {13, 3},
            {11, 4}, {13, 5}, {11, 6}, {10, 8}, {8, 8}, {5, 8}, {3, 9}, {5, 10}};
        for (int i = 0; i < towerCount; ++i)
            world.PlaceTowerType(map.CellCenter(cells[i].x, cells[i].y), TowerType::Gatling);
        for (int i = 0; i < 600; ++i) world.Step(1.0f / 60.0f);
        profiler::Profiler::Get().Reset();
        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < ticks; ++i) world.Step(1.0f / 60.0f);
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - begin).count();
        std::printf("  %-6s enemies=%zu projectiles=%zu ticks=%d total=%.1fms"
                    "  projectile=%.2fms cleanup=%.2fms\n",
                    mode == IdMapKind::Hash ? "hash" : "linear",
                    world.Enemies().Size(), world.Projectiles().Size(), ticks, ms,
                    ScopeTotal("sim.projectile"), ScopeTotal("sim.cleanup"));
        return world.StateHash();
    };
    const int ticks = 600;
    std::printf("[id_map scale=large %s]\n", label);
    const uint64_t viaLinear = run(IdMapKind::Linear, ticks);
    const uint64_t viaHash = run(IdMapKind::Hash, ticks);
    CHECK_EQ(viaLinear, viaHash);
}

}  // namespace

static void RunTests() {
    TestInterpolationAgrees();
    TestEraseThenReinsertKeepsLookups();
    TestSimulationAgrees();
    ReportIdMapCost(3, "towers=3");
    ReportIdMapCost(16, "towers=16");
}

TEST_MAIN("id_map_ab")
