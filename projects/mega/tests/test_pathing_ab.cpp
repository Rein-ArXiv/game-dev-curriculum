// 경로 팔 A/B: 폴리라인 · A* · 플로우 필드.
//
// 판정은 두 층이다.
//   (a) **오라클** — 같은 격자에서 A* 경로의 비용과 플로우 필드의 적분 비용이
//       같아야 한다. 두 방식은 서로 다른 최단 경로를 고를 수 있지만 비용은
//       같아야 하고, 그것이 유일하게 성립하는 대조다.
//   (b) **제품** — Classic 회랑에서는 통행 가능한 칸이 저작된 한 줄뿐이라 최단
//       경로가 유일하다. 그래서 세 팔이 **같은 세계**를 돌아야 한다.
//
// (b)에서 상태 해시를 그대로 쓰지 않는 이유는 하나다: 해시에는 `pathIndex` 가
// 들어 있고 그 열은 폴리라인 팔만 갱신한다(격자 팔에는 폴리라인 위의 자리라는
// 개념이 없다). 그래서 위치·체력·골드·타워로 만든 지문을 쓴다.
#include "ai/pathfinding/astar.hpp"
#include "ai/pathfinding/flow_field.hpp"
#include "ai/pathfinding/grid_map.hpp"
#include "game/defense_content.hpp"
#include "game/game.hpp"
#include "game/map_gen.hpp"
#include "test_util.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace mega;
using namespace mega::game;
using namespace mega::ai::path;

// ── (a) 오라클 ─────────────────────────────────────────────────────
// 절차 격자를 무작위로 찍어 A* 와 플로우 필드가 같은 비용을 내는지 본다.
static void TestCostOracle() {
    uint32_t rng = 0x51ed270bu;
    auto next = [&rng]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };

    int compared = 0, unreachable = 0;
    float worstDelta = 0;
    for (int trial = 0; trial < 200; ++trial) {
        const int w = 12 + static_cast<int>(next() % 14);
        const int h = 12 + static_cast<int>(next() % 14);
        GridMap grid;
        grid.Init(w, h, true);
        const int walls = (w * h) / 4;
        for (int i = 0; i < walls; ++i)
            grid.SetWalkable(static_cast<int>(next() % static_cast<uint32_t>(w)),
                             static_cast<int>(next() % static_cast<uint32_t>(h)), false);
        const int sx = 0, sy = 0, gx = w - 1, gy = h - 1;
        grid.SetWalkable(sx, sy, true);
        grid.SetWalkable(gx, gy, true);

        FlowField field;
        field.Build(grid, gx, gy);
        std::vector<int> path;
        const bool found = AStar::FindPath(grid, sx, sy, gx, gy, path);

        // 도달 가능성 판정이 두 팔에서 같아야 한다.
        CHECK_EQ(found, field.Reachable(sx, sy));
        if (!found) { ++unreachable; continue; }

        // 경로는 통행 불가 칸을 지나지 않는다.
        for (int cell : path) CHECK(grid.Walkable(cell % w, cell / w));

        const float astarCost = AStar::PathCost(path, w);
        const float fieldCost = field.Cost(sx, sy);
        const float delta = std::fabs(astarCost - fieldCost);
        if (delta > worstDelta) worstDelta = delta;
        CHECK(delta < 1e-3f);
        ++compared;
    }
    std::printf("  무작위 격자 200장: 비교 %d · 도달불가 %d · 최대 비용 차 %.6f\n",
                compared, unreachable, static_cast<double>(worstDelta));
    CHECK(compared > 100);      // 전부 도달 불가면 아무것도 비교하지 않은 것이다
    CHECK(unreachable > 0);     // 실패 경로도 실제로 밟혔다
}

// ── (b) 제품 ───────────────────────────────────────────────────────
// pathIndex 를 뺀 세계 지문. 위치는 비트 그대로 섞는다 — 반올림하면 갈라진
// 상태가 같은 지문을 낼 수 있다.
static uint64_t Fingerprint(const GameWorld& world) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    auto mixFloat = [&mix](float f) {
        uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof(bits));
        mix(bits);
    };
    mixFloat(world.CoreHp());
    mix(static_cast<uint64_t>(static_cast<int64_t>(world.Gold())));
    mix(static_cast<uint64_t>(world.Wave()));
    mix(world.Won() ? 2u : world.Lost() ? 3u : 1u);
    const EnemyStore& enemies = world.Enemies();
    mix(enemies.Size());
    for (size_t i = 0; i < enemies.Size(); ++i) {
        mix(enemies.Id(i));
        mixFloat(enemies.Pos(i).x);
        mixFloat(enemies.Pos(i).y);
        mixFloat(enemies.Hp(i));
        mix(enemies.Alive(i) ? 1u : 0u);
    }
    mix(world.Towers().size());
    for (const Tower& tower : world.Towers()) {
        mix(tower.id);
        mixFloat(tower.pos.x);
        mixFloat(tower.pos.y);
        mixFloat(tower.timer);
    }
    return h;
}

struct ClassicRun {
    uint64_t fingerprint = 0;
    int      towers = 0;
    float    coreHp = 0;
    double   milliseconds = 0;
    size_t   builds = 0, searches = 0;
};

// Classic 맵에서 한 매치를 돌린다. 타워는 고정 좌표에 세우고 조준 모드는
// 건드리지 않는다(기본 Nearest) — TargetMode::First 만 pathIndex 를 읽으므로,
// 그 모드를 쓰면 팔마다 다른 점수가 나와 비교가 성립하지 않는다.
static ClassicRun RunClassic(PathingKind kind, int ticks) {
    Switches switches;
    switches.pathing = kind;

    GameWorld world;
    world.Configure(switches);
    world.Init(DefenseMap::CreateDefault(), defense::DefaultWaves(), 600, 500.0f);

    static const DefenseMap::Cell kPlan[6] = {{6, 1}, {8, 2}, {11, 4}, {10, 6}, {5, 8}, {6, 12}};
    const DefenseMap& map = *world.Map();
    for (const DefenseMap::Cell& cell : kPlan)
        world.PlaceTowerType(map.CellCenter(cell.x, cell.y), TowerType::Cannon);

    const auto start = std::chrono::steady_clock::now();
    for (int tick = 0; tick < ticks; ++tick) world.Step(1.0f / 60.0f);
    const auto end = std::chrono::steady_clock::now();

    ClassicRun run;
    run.fingerprint = Fingerprint(world);
    run.towers = world.TowerCount();
    run.coreHp = world.CoreHp();
    run.milliseconds = std::chrono::duration<double, std::milli>(end - start).count();
    run.builds = world.Field().Builds();
    run.searches = world.Field().Searches();
    return run;
}

// 저작된 회랑에서는 최단 경로가 유일하다 → 세 팔이 같은 세계를 돈다.
//
// 900틱(15초)에서 멈추는 이유는 잡몹 상태 기계의 Enraged 때문이다. 격자 팔은
// 상태 기계를 돌리므로 스폰 후 25초를 넘긴 개체가 가속하는데, 폴리라인 팔에는
// 그 상태가 없다. 그 시각 전까지 두 팔은 같은 세계이고, 그 뒤로는 **의도적으로**
// 다른 세계다.
static void TestClassicCorridorAgrees() {
    const ClassicRun poly = RunClassic(PathingKind::Polyline, 900);
    const ClassicRun astar = RunClassic(PathingKind::AStar, 900);
    const ClassicRun field = RunClassic(PathingKind::FlowField, 900);
    std::printf("  poly      지문=%016llx 타워=%d 코어=%.1f %.2fms\n",
                static_cast<unsigned long long>(poly.fingerprint), poly.towers,
                static_cast<double>(poly.coreHp), poly.milliseconds);
    std::printf("  astar     지문=%016llx 타워=%d 코어=%.1f %.2fms (탐색 %zu회)\n",
                static_cast<unsigned long long>(astar.fingerprint), astar.towers,
                static_cast<double>(astar.coreHp), astar.milliseconds, astar.searches);
    std::printf("  flowfield 지문=%016llx 타워=%d 코어=%.1f %.2fms (재빌드 %zu회)\n",
                static_cast<unsigned long long>(field.fingerprint), field.towers,
                static_cast<double>(field.coreHp), field.milliseconds, field.builds);
    CHECK_EQ(poly.fingerprint, astar.fingerprint);
    CHECK_EQ(poly.fingerprint, field.fingerprint);
    // 두 격자 팔이 실제로 자기 일을 했는지 확인한다. 0 이면 아무도 길을 찾지
    // 않았다는 뜻이고, 그러면 위 일치는 "둘 다 폴리라인을 썼다"의 다른 이름이다.
    CHECK(astar.searches > 0);
    CHECK_EQ(field.builds, size_t{1});
    CHECK_EQ(astar.builds, size_t{0});
}

// ── 지형이 바뀔 때의 비용 ──────────────────────────────────────────
// 필드는 변경마다 한 번 다시 짓고, A* 는 개체마다 다시 찾는다. 그 갈림이
// 개체 수에 대해 어떻게 벌어지는지가 이 절의 측정 대상이다.
struct ChurnRun {
    double milliseconds = 0;
    size_t builds = 0, searches = 0;
    int    alive = 0;
};

static ChurnRun RunChurn(PathingKind kind, int enemies, int ticks, int churnEvery) {
    Switches switches;
    switches.map = MapSource::Procgen;
    switches.mapSeed = 3;
    switches.pathing = kind;
    switches.scale = ScaleKind::Large;

    mapgen::TerrainParams params;
    params.seed = 3;
    DefenseMap map;
    CHECK(mapgen::Generate(params, map));

    GameWorld world;
    world.Configure(switches);
    std::vector<WaveDef> waves{{enemies, 5000.0f, 18.0f, 1, 0.004f, EnemyType::Swarm}};
    world.Init(map, waves, 100000, 1e9f);

    // 지형을 흔드는 손: churnEvery 틱마다 바위 하나를 발파한다. 발파는 통행
    // 격자를 바꾸므로 필드는 재빌드를, A* 는 전량 무효화를 겪는다.
    std::vector<DefenseMap::Cell> rocks;
    for (int y = 1; y + 1 < map.Rows() && rocks.size() < 64; ++y)
        for (int x = 1; x + 1 < map.Columns() && rocks.size() < 64; ++x)
            if (map.Rock(x, y)) rocks.push_back({x, y});

    size_t rockCursor = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int tick = 0; tick < ticks; ++tick) {
        if (churnEvery > 0 && tick % churnEvery == 0 && rockCursor < rocks.size()) {
            world.Blast(map.CellCenter(rocks[rockCursor].x, rocks[rockCursor].y));
            ++rockCursor;
        }
        world.Step(1.0f / 60.0f);
    }
    const auto end = std::chrono::steady_clock::now();

    ChurnRun run;
    run.milliseconds = std::chrono::duration<double, std::milli>(end - start).count();
    run.builds = world.Field().Builds();
    run.searches = world.Field().Searches();
    run.alive = world.AliveEnemies();
    return run;
}

static void TestChurnCost() {
    std::printf("  지형이 바뀔 때(600틱, 60틱마다 발파 1회)\n");
    size_t firstBuilds = 0, lastBuilds = 0;
    size_t firstSearches = 0, lastSearches = 0;
    for (int count : {64, 256, 1024}) {
        const ChurnRun a = RunChurn(PathingKind::AStar, count, 600, 60);
        const ChurnRun f = RunChurn(PathingKind::FlowField, count, 600, 60);
        std::printf("    적 %4d  astar %7.1fms (탐색 %6zu)  flowfield %7.1fms (재빌드 %2zu)"
                    "  비 %.2fx\n",
                    count, a.milliseconds, a.searches, f.milliseconds, f.builds,
                    f.milliseconds > 0 ? a.milliseconds / f.milliseconds : 0.0);
        if (firstBuilds == 0) { firstBuilds = f.builds; firstSearches = a.searches; }
        lastBuilds = f.builds;
        lastSearches = a.searches;
        // A* 는 개체마다 다시 찾고, 필드는 개체를 세지 않는다.
        CHECK(a.searches > static_cast<size_t>(count));
        CHECK_EQ(f.searches, size_t{0});
        CHECK(f.builds > 0);
    }
    // **이 두 줄이 이 절의 결론이다.** 개체를 16배로 늘려도 필드 재빌드 수는
    // 거의 그대로이고(지형이 몇 번 바뀌었는가의 함수다), A* 탐색 수는 몇 배가
    // 된다. 재빌드가 개체 수를 따라 늘어난다면 필드 팔은 공유 목표의 이점을
    // 잃은 것이고, 그때 이 A/B 는 잴 것이 없다.
    CHECK(lastBuilds < firstBuilds * 2);
    CHECK(lastSearches > firstSearches * 2);
}

// 지형이 바뀌었는데 필드를 다시 짓지 않으면 세계가 갈린다. 그 사실을 여기서
// 명시적으로 만든다 — 갱신 배선이 살아 있다는 것을 무효화 실험으로 보인다.
static void TestFieldFollowsTerrain() {
    Switches switches;
    switches.map = MapSource::Procgen;
    switches.mapSeed = 3;
    switches.pathing = PathingKind::FlowField;

    mapgen::TerrainParams params;
    params.seed = 3;
    DefenseMap map;
    CHECK(mapgen::Generate(params, map));

    GameWorld world;
    world.Configure(switches);
    world.Init(map, defense::DefaultWaves(), 100000, 1e9f);
    const size_t before = world.Field().Builds();
    for (int tick = 0; tick < 60; ++tick) world.Step(1.0f / 60.0f);
    const size_t afterWarm = world.Field().Builds();
    CHECK_EQ(afterWarm, before + 1);          // 첫 틱에 한 번만 짓는다

    // 타워 하나를 세운다 = 칸 하나가 통행 불가가 된다 = 격자 버전이 오른다.
    const DefenseMap& live = *world.Map();
    bool placed = false;
    for (size_t i = 1; i + 1 < live.RouteLength() && !placed; ++i) {
        const DefenseMap::Cell on = live.RouteCell(i);
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; ++d)
            if (world.PlaceTowerType(live.CellCenter(on.x + dx[d], on.y + dy[d]),
                                     TowerType::Cannon)) { placed = true; break; }
    }
    CHECK(placed);
    world.Step(1.0f / 60.0f);
    std::printf("  필드 재빌드: 시작 %zu → 60틱 %zu → 타워 배치 뒤 %zu\n",
                before, afterWarm, world.Field().Builds());
    CHECK_EQ(world.Field().Builds(), afterWarm + 1);
    // 그 다음 틱들은 격자가 그대로이므로 다시 짓지 않는다.
    for (int tick = 0; tick < 60; ++tick) world.Step(1.0f / 60.0f);
    CHECK_EQ(world.Field().Builds(), afterWarm + 1);
}

static void RunTests() {
    TestCostOracle();
    TestClassicCorridorAgrees();
    TestFieldFollowsTerrain();
    TestChurnCost();
}

TEST_MAIN("pathing_ab")
