// 조준 색인 3-way A/B: linear · grid · quadtree.
//
// 판정 순서가 이 파일의 구조다.
//   (a) 세 팔이 **매 틱 같은 타워에 같은 대상 행 번호**를 고른다(동점 포함).
//   (b) 그 위에서만 3000틱 상태 해시 비교가 의미를 갖는다.
//   (c) 그다음에야 비용을 잰다.
//
// (a) 없이 (b)만 보면 "빨라졌다"가 "덜 찾았다"와 구분되지 않는다.
#include "game/defense_content.hpp"
#include "game/game.hpp"
#include "game/map_gen.hpp"
#include "test_util.h"

#include <chrono>
#include <utility>
#include <cstdio>
#include <vector>

using namespace mega;
using namespace mega::game;

namespace {

DefenseMap ProcgenMap(uint32_t seed) {
    mapgen::TerrainParams params;
    params.seed = seed;
    DefenseMap map;
    CHECK(mapgen::Generate(params, map));
    return map;
}

// 색인 하나로 한 판을 돌리고 매 틱 조준 결과를 기록한다.
struct Trace {
    std::vector<int> targets;     // (틱 × 타워) 평탄화
    std::vector<uint64_t> hashes;
    uint64_t finalHash = 0;
    double milliseconds = 0.0;
};

Trace Play(TargetIndexKind kind, int ticks, bool procgen, int seed) {
    Switches switches;
    switches.targetIndex = kind;
    if (procgen) { switches.map = MapSource::Procgen; switches.mapSeed = seed; }

    GameWorld world;
    world.Configure(switches);
    const DefenseMap map = procgen ? ProcgenMap(static_cast<uint32_t>(seed))
                                   : DefenseMap::CreateDefault();
    world.Init(map, defense::DefaultWaves(), 4000, 4000.0f);

    // 타워를 여럿 세운다. 조준 질의가 실제로 돌아야 비교가 성립한다.
    int placed = 0;
    for (size_t i = 1; i + 1 < world.Map()->RouteLength() && placed < 12; i += 2) {
        const DefenseMap::Cell on = world.Map()->RouteCell(i);
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; ++d) {
            const math::Vec2 at = world.Map()->CellCenter(on.x + dx[d], on.y + dy[d]);
            if (!world.CanPlaceTower(at)) continue;
            if (world.PlaceTowerType(at, static_cast<TowerType>(placed % 4))) { ++placed; break; }
        }
    }
    CHECK(placed >= 6);

    // 타게팅 모드를 섞는다 — 세 모드 전부에서 동점 처리가 같아야 한다.
    for (int i = 0; i < world.TowerCount(); ++i)
        world.SetTowerTarget(i, static_cast<TargetMode>(i % 3));

    Trace trace;
    trace.targets.reserve(static_cast<size_t>(ticks) * static_cast<size_t>(world.TowerCount()));
    trace.hashes.reserve(static_cast<size_t>(ticks));

    const SightTest sight = procgen ? SightTest{&world.Geometry(), switches.staticQuery}
                                    : SightTest{};
    const auto start = std::chrono::steady_clock::now();
    for (int tick = 0; tick < ticks; ++tick) {
        world.Step(1.0f / 60.0f);
        // 조준 결과를 밖에서 다시 계산한다 — 시뮬 안의 선택과 같은 입력·같은 규칙.
        for (const Tower& tower : world.Towers())
            trace.targets.push_back(SelectTargetIn(tower.pos, world.CorePos(), world.Enemies(),
                                                   tower.range, tower.targetMode, sight));
        trace.hashes.push_back(world.StateHash());
    }
    trace.milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    trace.finalHash = world.StateHash();
    return trace;
}

void CompareArms(bool procgen, int seed, int ticks) {
    const Trace linear = Play(TargetIndexKind::Linear, ticks, procgen, seed);
    const Trace grid = Play(TargetIndexKind::Grid, ticks, procgen, seed);
    const Trace tree = Play(TargetIndexKind::Quadtree, ticks, procgen, seed);

    CHECK_EQ(linear.targets.size(), grid.targets.size());
    CHECK_EQ(linear.targets.size(), tree.targets.size());
    CHECK(linear.targets.size() > 1000);          // 조준이 실제로 일어났다

    size_t gridDiff = 0, treeDiff = 0;
    for (size_t i = 0; i < linear.targets.size(); ++i) {
        if (linear.targets[i] != grid.targets[i]) ++gridDiff;
        if (linear.targets[i] != tree.targets[i]) ++treeDiff;
    }
    CHECK_EQ(gridDiff, size_t{0});
    CHECK_EQ(treeDiff, size_t{0});

    size_t hashDiff = 0;
    for (size_t i = 0; i < linear.hashes.size(); ++i)
        if (linear.hashes[i] != grid.hashes[i] || linear.hashes[i] != tree.hashes[i]) ++hashDiff;
    CHECK_EQ(hashDiff, size_t{0});
    CHECK_EQ(linear.finalHash, grid.finalHash);
    CHECK_EQ(linear.finalHash, tree.finalHash);

    std::printf("  [%s seed=%d ticks=%d] linear=%.1fms grid=%.1fms quadtree=%.1fms\n",
                procgen ? "procgen" : "classic", seed, ticks,
                linear.milliseconds, grid.milliseconds, tree.milliseconds);
}

}  // namespace

// 세 팔이 같은 세계를 돈다 — Classic 맵(사격선 없음)과 절차 맵(사격선 있음) 양쪽.
static void TestArmsAgree() {
    CompareArms(false, 1, 3000);
    CompareArms(true, 3, 2000);
    CompareArms(true, 5, 2000);
}

// 경계에 정확히 걸린 적: 사거리 제곱과 거리 제곱이 같은 자리.
static void TestRangeBoundary() {
    EnemyStore enemies;
    enemies.Configure(EntityStoreKind::Aos, IdMapKind::Linear, 64);
    const math::Vec2 tower{100.0f, 100.0f};
    const float range = 50.0f;
    Enemy exact;
    exact.pos = {tower.x + range, tower.y};       // 거리 == 사거리
    exact.id = 1;
    enemies.Push(exact);
    Enemy outside;
    outside.pos = {tower.x + range + 0.01f, tower.y};
    outside.id = 2;
    enemies.Push(outside);

    math::Aabb2 bounds{{0, 0}, {480, 480}};
    TargetIndex index;
    for (TargetIndexKind kind : {TargetIndexKind::Grid, TargetIndexKind::Quadtree}) {
        index.Configure(kind, bounds, 64.0f);
        index.Rebuild(enemies);
        uint32_t candidates[8];
        const size_t found = index.Query(tower, range, candidates, 8);
        const int chosen = SelectTargetAmong(tower, {0, 0}, enemies, range,
                                             TargetMode::Nearest, candidates, found);
        CHECK_EQ(chosen, SelectTargetIn(tower, {0, 0}, enemies, range, TargetMode::Nearest));
        CHECK_EQ(chosen, 0);                       // 경계는 포함, 그 밖은 제외
    }
}

// 완전 동점(같은 좌표에 두 적): 승자는 언제나 행 번호가 낮은 쪽이다.
static void TestTiesGoToLowerRow() {
    EnemyStore enemies;
    enemies.Configure(EntityStoreKind::Aos, IdMapKind::Linear, 64);
    for (int i = 0; i < 5; ++i) {
        Enemy e;
        e.pos = {150.0f, 150.0f};                  // 다섯이 정확히 같은 자리
        e.id = static_cast<uint32_t>(i + 1);
        e.hp = 10.0f;
        enemies.Push(e);
    }
    const math::Vec2 tower{150.0f, 120.0f};
    math::Aabb2 bounds{{0, 0}, {480, 480}};
    TargetIndex index;
    uint32_t candidates[16];
    for (TargetIndexKind kind : {TargetIndexKind::Grid, TargetIndexKind::Quadtree}) {
        index.Configure(kind, bounds, 64.0f);
        index.Rebuild(enemies);
        const size_t found = index.Query(tower, 100.0f, candidates, 16);
        CHECK_EQ(found, size_t{5});
        // 후보가 오름차순이어야 동점 승자가 전수 스캔과 같다.
        bool ascending = true;
        for (size_t i = 1; i < found; ++i) if (candidates[i] < candidates[i - 1]) ascending = false;
        CHECK(ascending);
        for (TargetMode mode : {TargetMode::Nearest, TargetMode::First, TargetMode::Strongest}) {
            CHECK_EQ(SelectTargetAmong(tower, {0, 0}, enemies, 100.0f, mode, candidates, found),
                     SelectTargetIn(tower, {0, 0}, enemies, 100.0f, mode));
            CHECK_EQ(SelectTargetAmong(tower, {0, 0}, enemies, 100.0f, mode, candidates, found), 0);
        }
    }
}

// 사격선: 바위 뒤의 적은 세 팔 모두에서 똑같이 제외된다.
static void TestSightBlocksAllArms() {
    const DefenseMap map = ProcgenMap(2);
    StaticGeometry geometry;
    geometry.Rebuild(map);

    EnemyStore enemies;
    enemies.Configure(EntityStoreKind::Aos, IdMapKind::Linear, 256);
    // 바위 칸 하나를 사이에 둔 두 점을 찾는다.
    int found = 0;
    for (int y = 2; y < map.Rows() - 2 && found == 0; ++y)
        for (int x = 2; x < map.Columns() - 2 && found == 0; ++x) {
            if (!map.Rock(x, y)) continue;
            if (map.Rock(x - 1, y) || map.Rock(x + 1, y)) continue;
            const math::Vec2 tower = map.CellCenter(x - 1, y);
            const math::Vec2 behind = map.CellCenter(x + 1, y);
            Enemy e;
            e.pos = behind;
            e.id = 1;
            enemies.Push(e);
            const SightTest naive{&geometry, StaticQueryKind::Naive};
            const SightTest bvh{&geometry, StaticQueryKind::Bvh};
            const float range = static_cast<float>(map.CellSize()) * 6.0f;
            CHECK_EQ(SelectTargetIn(tower, {0, 0}, enemies, range, TargetMode::Nearest, naive), -1);
            CHECK_EQ(SelectTargetIn(tower, {0, 0}, enemies, range, TargetMode::Nearest, bvh), -1);
            // 가림이 없으면 같은 적이 선택된다.
            CHECK_EQ(SelectTargetIn(tower, {0, 0}, enemies, range, TargetMode::Nearest), 0);
            found = 1;
        }
    CHECK_EQ(found, 1);
}

// 규모를 올려 교차점을 찾는다. 적 64마리에서는 세 팔의 차이가 측정 잡음 안에
// 있다 — "색인이 이긴다"는 문장은 그 규모에서 참도 거짓도 아니다.
static void TestCostAtScale() {
    auto run = [](TargetIndexKind kind, size_t& enemies) {
        Switches switches;
        switches.scale = ScaleKind::Large;
        switches.targetIndex = kind;
        GameWorld world;
        world.Configure(switches);
        // 상한을 실제로 채우는 웨이브. 채우지 않으면 색인이 도는 규모가 아니다.
        // 체력을 아주 높게 잡아 상한을 실제로 채운 채 유지한다. 적이 죽어 나가면
        // 측정 규모가 매 틱 달라져 "적 2048에서 쟀다"는 문장이 성립하지 않는다.
        std::vector<WaveDef> waves{{2048, 1.0e6f, 24.0f, 1, 0.0f, EnemyType::Normal}};
        world.Init(DefenseMap::CreateDefault(), waves, 4000, 1e9f);
        int placed = 0;
        for (int y = 0; y < 15 && placed < 16; ++y)
            for (int x = 0; x < 16 && placed < 16; ++x)
                if (world.PlaceTowerType(world.Map()->CellCenter(x, y),
                                         static_cast<TowerType>(placed % 4))) ++placed;
        for (int i = 0; i < world.TowerCount(); ++i)
            world.SetTowerTarget(i, static_cast<TargetMode>(i % 3));

        const auto start = std::chrono::steady_clock::now();
        for (int tick = 0; tick < 600; ++tick) world.Step(1.0f / 60.0f);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        enemies = world.Enemies().Size();
        return std::pair<double, uint64_t>{ms, world.StateHash()};
    };

    size_t nLinear = 0, nGrid = 0, nTree = 0;
    const auto linear = run(TargetIndexKind::Linear, nLinear);
    const auto grid = run(TargetIndexKind::Grid, nGrid);
    const auto tree = run(TargetIndexKind::Quadtree, nTree);
    std::printf("  [scale=large towers=16 enemies=%zu ticks=600] linear=%.1fms grid=%.1fms quadtree=%.1fms\n",
                nLinear, linear.first, grid.first, tree.first);
    CHECK(nLinear > 1000);                 // 상한을 실제로 채웠다
    CHECK_EQ(nLinear, nGrid);
    CHECK_EQ(nLinear, nTree);
    CHECK_EQ(linear.second, grid.second);  // 같은 세계
    CHECK_EQ(linear.second, tree.second);
}

static void RunTests() {
    TestArmsAgree();
    TestCostAtScale();
    TestRangeBoundary();
    TestTiesGoToLowerRow();
    TestSightBlocksAllArms();
}

TEST_MAIN("target_index_ab")
