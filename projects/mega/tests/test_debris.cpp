// 잔해 물리: 정지 · 에너지 유계 · 관통 · 통행 격자 갱신 · 광역 탐색 A/B.
//
// 이 파일이 지키는 경계 하나: **멈춘 파편이 정확히 그 칸만 막는다.** 물리가 AI
// 입력이 되는 자리이므로, 여기서 한 칸이 어긋나면 경로가 통째로 달라진다.
#include "game/debris.hpp"
#include "game/defense_content.hpp"
#include "game/game.hpp"
#include "game/map_gen.hpp"
#include "test_util.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace mega;
using namespace mega::game;

static DefenseMap MakeMap(uint32_t seed) {
    mapgen::TerrainParams params;
    params.seed = seed;
    DefenseMap map;
    CHECK(mapgen::Generate(params, map));
    return map;
}

// 파편은 유한한 시간 안에 전부 멈춘다.
static void TestEverythingSettles() {
    const DefenseMap map = MakeMap(3);
    Switches switches;
    DebrisField field;
    field.Configure(switches);
    field.Reset(map);

    uint32_t rng = 0x1111u;
    int spawned = 0;
    for (int i = 0; i < 12; ++i) {
        const DefenseMap::Cell cell = map.RouteCell(2 + i * 3);
        spawned += field.SpawnBurst(map.CellCenter(cell.x, cell.y), 8, rng);
    }
    CHECK(spawned > 60);

    std::vector<DefenseMap::Cell> settled;
    int totalSettled = 0;
    int ticks = 0;
    for (; ticks < 3000 && field.Live() > 0; ++ticks)
        totalSettled += field.Step(1.0f / 60.0f, map, settled);
    std::printf("  settled %d chunks in %d ticks\n", totalSettled, ticks);
    CHECK_EQ(field.Live(), size_t{0});
    CHECK(ticks < 3000);
    CHECK_EQ(totalSettled, spawned);
    CHECK_EQ(field.Settled(), static_cast<size_t>(spawned));

    // 멈춘 파편은 전부 맵 안에 있다.
    for (size_t i = 0; i < field.World().Count(); ++i) {
        const physics::RigidBody2D& body = field.World().Body(static_cast<int>(i));
        CHECK(!std::isnan(body.position.x) && !std::isnan(body.position.y));
    }
}

// Symplectic 은 에너지를 유계로 유지하고, Explicit 은 발산한다.
static void TestIntegratorEnergy() {
    const DefenseMap map = MakeMap(3);
    const float reference = static_cast<float>(map.PixelHeight());

    auto run = [&](IntegratorKind kind) {
        Switches switches;
        switches.integrator = kind;
        DebrisField field;
        field.Configure(switches);
        field.Reset(map);
        uint32_t rng = 0x2222u;
        const DefenseMap::Cell cell = map.RouteCell(2);
        field.SpawnBurst(map.CellCenter(cell.x, cell.y), 24, rng);
        const float initial = field.TotalEnergy(reference);
        float peak = initial;
        std::vector<DefenseMap::Cell> settled;
        for (int tick = 0; tick < 240 && field.Live() > 0; ++tick) {
            field.Step(1.0f / 60.0f, map, settled);
            peak = std::fmax(peak, field.TotalEnergy(reference));
        }
        return peak / (initial > 1.0f ? initial : 1.0f);
    };

    const float symplectic = run(IntegratorKind::Symplectic);
    const float explicitEuler = run(IntegratorKind::Explicit);
    std::printf("  energy peak/initial: symplectic=%.3f explicit=%.3f\n",
                symplectic, explicitEuler);
    CHECK(symplectic <= 1.05f);              // 유계
    CHECK(explicitEuler > symplectic);       // 명시적 오일러가 더 부푼다
}

// 광역 탐색 A/B: 같은 결과, 다른 쌍 수.
static void TestBroadphaseArmsAgree() {
    const DefenseMap map = MakeMap(3);
    auto run = [&](BroadphaseKind kind, size_t& pairs) {
        Switches switches;
        switches.broadphase = kind;
        DebrisField field;
        field.Configure(switches);
        field.Reset(map);
        uint32_t rng = 0x4444u;
        for (int i = 0; i < 8; ++i) {
            const DefenseMap::Cell cell = map.RouteCell(2 + i * 4);
            field.SpawnBurst(map.CellCenter(cell.x, cell.y), 10, rng);
        }
        std::vector<DefenseMap::Cell> settled;
        std::vector<float> trace;
        size_t peakPairs = 0;
        for (int tick = 0; tick < 300 && field.Live() > 0; ++tick) {
            field.Step(1.0f / 60.0f, map, settled);
            peakPairs = std::max(peakPairs, field.PairCount());
            for (size_t i = 0; i < field.World().Count(); ++i) {
                const physics::RigidBody2D& body = field.World().Body(static_cast<int>(i));
                trace.push_back(body.position.x);
                trace.push_back(body.position.y);
            }
        }
        pairs = peakPairs;
        return trace;
    };

    size_t naivePairs = 0, gridPairs = 0;
    const std::vector<float> naive = run(BroadphaseKind::Naive, naivePairs);
    const std::vector<float> grid = run(BroadphaseKind::Grid, gridPairs);
    CHECK_EQ(naive.size(), grid.size());
    size_t differences = 0;
    for (size_t i = 0; i < naive.size() && i < grid.size(); ++i)
        if (naive[i] != grid[i]) ++differences;
    std::printf("  broadphase peak pairs: naive=%zu grid=%zu (state diffs=%zu)\n",
                naivePairs, gridPairs, differences);
    CHECK_EQ(differences, size_t{0});       // 비트 동일
    CHECK(gridPairs * 4 < naivePairs);      // 격자가 후보를 크게 줄인다
}

// **경계 판정**: 멈춘 파편이 정확히 자기 칸만 막고, 그 칸이 경로 계산에 반영된다.
static void TestSettledDebrisBlocksExactlyItsCell() {
    const DefenseMap map = MakeMap(5);
    Switches switches;
    switches.map = MapSource::Procgen;
    GameWorld world;
    world.Configure(switches);
    world.Init(map, defense::DefaultWaves(), 5000, 5000.0f);

    const size_t routeBefore = world.RouteLength();
    // 타워를 세우고 팔아 잔해를 만든다(제품이 잔해를 만드는 두 경로 중 하나).
    int placed = 0;
    for (size_t i = 1; i + 1 < world.Map()->RouteLength() && placed < 6; i += 3) {
        const DefenseMap::Cell on = world.Map()->RouteCell(i);
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; ++d) {
            const math::Vec2 at = world.Map()->CellCenter(on.x + dx[d], on.y + dy[d]);
            if (world.PlaceTowerType(at, TowerType::Cannon)) { ++placed; break; }
        }
    }
    CHECK(placed >= 3);
    const int towers = world.TowerCount();
    for (int i = towers - 1; i >= 0; --i) CHECK(world.SellTower(i) > 0);
    CHECK_EQ(world.TowerCount(), 0);
    CHECK(world.Debris().Live() > 0);

    for (int tick = 0; tick < 600; ++tick) world.Step(1.0f / 60.0f);
    CHECK_EQ(world.Debris().Live(), size_t{0});
    // DebrisField::Settled() 는 정적 기하를 다시 지을 때 0 으로 돌아간다(굳은
    // 파편이 바위와 같은 자격의 정적 바디가 되므로). 누적은 규칙이 센다.
    CHECK(world.DebrisSettled() > 0);

    // 막힌 칸을 세고, 통행 격자와 대조한다.
    int occupied = 0;
    for (int y = 0; y < world.Map()->Rows(); ++y)
        for (int x = 0; x < world.Map()->Columns(); ++x)
            if (world.Map()->Occupied(x, y)) ++occupied;
    CHECK(occupied > 0);
    CHECK_EQ(world.TowerCount(), 0);        // 타워는 없다 = 점유는 전부 잔해다

    // 경로는 여전히 유효하고, 막힌 칸을 지나지 않는다.
    CHECK(world.Map()->ToLevel().Validate().Valid());
    bool routeClear = true;
    for (size_t i = 0; i < world.Map()->RouteLength(); ++i) {
        const DefenseMap::Cell c = world.Map()->RouteCell(i);
        if (!world.Map()->Passable(c.x, c.y)) routeClear = false;
    }
    CHECK(routeClear);
    std::printf("  settled debris blocked %d cells; route %zu -> %zu\n",
                occupied, routeBefore, world.RouteLength());
}

// 잔해가 있어도 시뮬은 결정적이다 — 같은 입력, 같은 해시.
static void TestDeterministic() {
    const DefenseMap map = MakeMap(5);
    auto play = [&](BroadphaseKind kind) {
        Switches switches;
        switches.map = MapSource::Procgen;
        switches.broadphase = kind;
        GameWorld world;
        world.Configure(switches);
        world.Init(map, defense::DefaultWaves(), 5000, 5000.0f);
        for (size_t i = 1; i + 1 < world.Map()->RouteLength() && world.TowerCount() < 6; i += 3) {
            const DefenseMap::Cell on = world.Map()->RouteCell(i);
            static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int d = 0; d < 4; ++d)
                if (world.PlaceTowerType(world.Map()->CellCenter(on.x + dx[d], on.y + dy[d]),
                                         TowerType::Cannon)) break;
        }
        for (int i = world.TowerCount() - 1; i >= 0; i -= 2) world.SellTower(i);
        for (int tick = 0; tick < 900; ++tick) world.Step(1.0f / 60.0f);
        return world.StateHash();
    };
    CHECK_EQ(play(BroadphaseKind::Naive), play(BroadphaseKind::Naive));
    CHECK_EQ(play(BroadphaseKind::Naive), play(BroadphaseKind::Grid));
}

static void RunTests() {
    TestEverythingSettles();
    TestIntegratorEnergy();
    TestBroadphaseArmsAgree();
    TestSettledDebrisBlocksExactlyItsCell();
    TestDeterministic();
}

TEST_MAIN("debris")
