// 은신: 레이더 시야 · 벽 차폐 · 청각 · 망각 · 조준 후보 필터 · 와이어 비트.
//
// 이 파일이 지키는 경계 하나: **조준 후보가 기하가 아니라 인지로 정해진다.**
// 사거리 안에 있고 벽에도 가리지 않은 적이, 아무도 그를 감지하지 못했다는
// 이유만으로 조준되지 않아야 한다.
#include "ai/perception/perception.hpp"
#include "game/defense_content.hpp"
#include "game/defense_protocol.hpp"
#include "game/game.hpp"
#include "game/map_gen.hpp"
#include "test_util.h"

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

// 통행 격자 위 시야 레이캐스트. 벽 뒤는 보이지 않고, 벽을 피한 방향은 보인다.
static void TestOcclusionOnNavGrid() {
    ai::path::GridMap grid;
    grid.ParseAscii({
        ".....",
        ".....",
        "..#..",
        ".....",
        ".....",
    });
    const ai::perceive::VisionCone cone = RadarCone();
    const math::Vec2 eye{0.5f, 2.5f}, facing{1, 0};
    CHECK(!ai::perceive::CanSee(grid, eye, facing, cone, {4.5f, 2.5f}));   // 벽 너머
    CHECK(ai::perceive::CanSee(grid, eye, facing, cone, {4.5f, 0.5f}));    // 벽을 피함
    // 뒤쪽은 원뿔 밖이다(전체 시야각 140°).
    CHECK(!ai::perceive::CanSee(grid, eye, facing, cone, {0.5f, 4.9f}));
}

// 은신 개체는 드러나지 않으면 조준 후보가 아니다. 같은 위치·같은 사거리에서
// 노출 배열 하나만 바뀌어도 답이 갈린다.
static void TestTargetableFilter() {
    EnemyStore enemies;
    enemies.Configure(EntityStoreKind::Aos, IdMapKind::Linear, 8);
    Enemy stalker;
    stalker.pos = {40, 40};
    stalker.id = 1;
    stalker.type = EnemyType::Stalker;
    enemies.Push(stalker);

    const uint8_t hidden[1] = {0};
    const uint8_t shown[1] = {1};
    SightTest cloaked;
    cloaked.revealed = hidden;
    cloaked.revealedCount = 1;
    SightTest visible;
    visible.revealed = shown;
    visible.revealedCount = 1;

    CHECK_EQ(SelectTargetIn({40, 40}, {0, 0}, enemies, 200.0f, TargetMode::Nearest), 0);
    CHECK_EQ(SelectTargetIn({40, 40}, {0, 0}, enemies, 200.0f, TargetMode::Nearest, visible), 0);
    CHECK_EQ(SelectTargetIn({40, 40}, {0, 0}, enemies, 200.0f, TargetMode::Nearest, cloaked), -1);

    // 후보 목록 팔(격자·쿼드트리)도 같은 답을 내야 한다. 두 팔이 갈리면
    // target_index 스위치가 은신 규칙을 우회하는 통로가 된다.
    const uint32_t candidate[1] = {0};
    CHECK_EQ(SelectTargetAmong({40, 40}, {0, 0}, enemies, 200.0f, TargetMode::Nearest,
                               candidate, 1, visible), 0);
    CHECK_EQ(SelectTargetAmong({40, 40}, {0, 0}, enemies, 200.0f, TargetMode::Nearest,
                               candidate, 1, cloaked), -1);
}

// 목격 기억: 시야를 벗어난 뒤 forgetAfter 동안만 계속 드러나 있다.
static void TestMemoryWindow() {
    ai::perceive::Memory memory;
    memory.Observe({10, 10});
    CHECK(memory.hasTarget);
    const float dt = 1.0f / 60.0f;
    int ticks = 0;
    while (memory.hasTarget && ticks < 600) { memory.Update(dt, kStalkerForget); ++ticks; }
    std::printf("  망각까지 %d틱(%.2fs), forgetAfter=%.2fs\n", ticks,
                static_cast<double>(ticks) * static_cast<double>(dt),
                static_cast<double>(kStalkerForget));
    CHECK(!memory.hasTarget);
    // 반올림 한 틱까지만 허용한다.
    CHECK(ticks >= static_cast<int>(kStalkerForget * 60.0f) - 1);
    CHECK(ticks <= static_cast<int>(kStalkerForget * 60.0f) + 1);
}

// 레이더가 있는 세계와 없는 세계. 은신 개체가 실제로 드러나고, 실제로 죽는다.
struct StealthRun {
    uint32_t revealTicks = 0;
    int      killed = 0;
    float    coreHp = 0;
};

static StealthRun RunStealth(bool withRadar, int ticks) {
    Switches switches;
    switches.map = MapSource::Procgen;
    switches.mapSeed = 5;
    switches.pathing = PathingKind::FlowField;
    switches.stealth = true;

    const DefenseMap map = MakeMap(5);
    GameWorld world;
    world.Configure(switches);
    std::vector<WaveDef> waves{{12, 18.0f, 26.0f, 6, 0.40f, EnemyType::Stalker}};
    world.Init(map, waves, 100000, 4000.0f);

    // 경로 옆 건설 가능한 칸에 타워를 죽 세운다. 레이더 팔은 그중 절반을
    // 레이더로 바꾼다 — 같은 자리, 같은 수, 다른 종류다.
    int placed = 0;
    for (size_t i = 1; i + 1 < map.RouteLength() && placed < 8; i += 4) {
        const DefenseMap::Cell on = map.RouteCell(i);
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; ++d) {
            const math::Vec2 at = map.CellCenter(on.x + dx[d], on.y + dy[d]);
            const TowerType type = (withRadar && (placed % 2) == 1) ? TowerType::Radar
                                                                    : TowerType::Sniper;
            if (!world.PlaceTowerType(at, type)) continue;
            ++placed;
            break;
        }
    }
    CHECK(placed >= 6);

    StealthRun run;
    int spawned = 0;
    for (int tick = 0; tick < ticks; ++tick) {
        world.Step(1.0f / 60.0f);
        const EnemyStore& enemies = world.Enemies();
        if (static_cast<int>(enemies.Size()) > spawned) spawned = static_cast<int>(enemies.Size());
    }
    run.revealTicks = world.RevealTicks();
    run.coreHp = world.CoreHp();
    // 죽은 적 = 골드가 늘어난 만큼. 보상 6 x 배수 2 = 12.
    run.killed = (world.Gold() - 100000 + 8 * TowerSpecFor(TowerType::Sniper).cost) / 12;
    return run;
}

static void TestRadarChangesTheGame() {
    const StealthRun blind = RunStealth(false, 3000);
    const StealthRun seeing = RunStealth(true, 3000);
    std::printf("  레이더 없음  노출틱=%u 코어=%.0f\n",
                blind.revealTicks, static_cast<double>(blind.coreHp));
    std::printf("  레이더 있음  노출틱=%u 코어=%.0f\n",
                seeing.revealTicks, static_cast<double>(seeing.coreHp));
    // 레이더가 없으면 아무도 드러나지 않고, 코어가 그만큼 더 맞는다.
    CHECK_EQ(blind.revealTicks, 0u);
    CHECK(seeing.revealTicks > 0u);
    CHECK(seeing.coreHp > blind.coreHp);
}

// 노출 비트는 와이어를 건넌다 — **바이트를 늘리지 않고**.
static void TestRevealedCrossesTheWire() {
    Switches switches;
    switches.map = MapSource::Procgen;
    switches.mapSeed = 5;
    switches.pathing = PathingKind::FlowField;
    switches.stealth = true;
    GameWorld world;
    world.Configure(switches);
    std::vector<WaveDef> waves{{4, 18.0f, 26.0f, 6, 0.40f, EnemyType::Stalker}};
    world.Init(MakeMap(5), waves, 100000, 4000.0f);
    for (int tick = 0; tick < 120; ++tick) world.Step(1.0f / 60.0f);

    const net::Bytes bytes = wire::WriteSnapshot(world, 1, 0);
    CHECK(!bytes.empty());
    CHECK(bytes.size() <= wire::kMaxPacketBytes);
    wire::Snapshot decoded;
    CHECK(wire::ReadSnapshot(bytes, decoded));
    CHECK(!decoded.enemies.empty());
    size_t matched = 0;
    for (const wire::EnemyState& enemy : decoded.enemies) {
        CHECK(enemy.type == EnemyType::Stalker);   // 마스크가 종류를 되살린다
        for (size_t row = 0; row < world.Enemies().Size(); ++row) {
            if (world.Enemies().Id(row) != enemy.id) continue;
            CHECK_EQ(enemy.revealed, world.Revealed(row));
            ++matched;
        }
    }
    CHECK_EQ(matched, decoded.enemies.size());
    std::printf("  스냅샷 %zu바이트(예산 %zu) 적 %zu마리\n", bytes.size(),
                wire::kMaxPacketBytes, decoded.enemies.size());
}

static void RunTests() {
    TestOcclusionOnNavGrid();
    TestTargetableFilter();
    TestMemoryWindow();
    TestRadarChangesTheGame();
    TestRevealedCrossesTheWire();
}

TEST_MAIN("stealth")
