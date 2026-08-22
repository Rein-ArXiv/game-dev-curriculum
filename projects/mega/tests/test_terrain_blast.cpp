// 발파: SDF 브러시 감산 · 통행 칸 증가 · 봉쇄 불가 · 명령열 재현성.
//
// 감산 연산(OpSubtract)이 이 기능의 전부다. OpUnion 으로 바꾸면 브러시가 무엇을
// 덮든 아무것도 열리지 않는다 — 아래 개방 칸 수 단언이 그것을 잡는다.
#include "game/defense_content.hpp"
#include "game/game.hpp"
#include "game/map_gen.hpp"
#include "game/terrain.hpp"
#include "test_util.h"

#include <algorithm>
#include <cmath>
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

static int RockCount(const DefenseMap& map) {
    int rock = 0;
    for (int y = 0; y < map.Rows(); ++y)
        for (int x = 0; x < map.Columns(); ++x)
            if (map.Rock(x, y)) ++rock;
    return rock;
}

// 브러시 안의 바위 칸이 정확히 그만큼 열린다 — 참조 계산은 전수 순회다.
static void TestOpensExactlyBrushVolume() {
    DefenseMap map = MakeMap(11);
    StaticGeometry geometry;
    geometry.Rebuild(map);

    const math::Vec2 center = map.CellCenter(map.Columns() / 2, map.Rows() / 2);
    const float radius = static_cast<float>(map.CellSize()) * 2.5f;
    const BlastBrush brush = BlastBrush::Sphere(center, radius);

    // 오라클: 브러시 SDF 가 음수인 바위 칸 수.
    int expected = 0;
    for (int y = 0; y < map.Rows(); ++y)
        for (int x = 0; x < map.Columns(); ++x) {
            if (!map.Rock(x, y)) continue;
            const math::Vec2 c = map.CellCenter(x, y);
            if (brush.Sd({c.x, c.y, 0.5f}) <= 0.0f) ++expected;
        }
    CHECK(expected > 0);

    const int rockBefore = RockCount(map);
    std::vector<uint32_t> scratch;
    std::vector<DefenseMap::Cell> opened;
    const BlastResult result = ApplyBlast(map, geometry, brush, StaticQueryKind::Naive,
                                          scratch, &opened);
    CHECK_EQ(result.opened, expected);
    CHECK_EQ(static_cast<int>(opened.size()), expected);
    CHECK_EQ(RockCount(map), rockBefore - expected);
    // 열린 칸은 전부 통행 가능해졌다.
    bool allPassable = true;
    for (const DefenseMap::Cell& c : opened)
        if (!map.Passable(c.x, c.y)) allPassable = false;
    CHECK(allPassable);
    // 색인은 지형이 바뀐 뒤에 다시 지어졌다.
    CHECK_EQ(static_cast<int>(geometry.PrimCount()), rockBefore - expected);
}

// 두 색인 팔이 같은 칸을 연다.
static void TestArmsOpenSameCells() {
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        DefenseMap naiveMap = MakeMap(seed);
        DefenseMap bvhMap = MakeMap(seed);
        StaticGeometry naiveGeometry, bvhGeometry;
        naiveGeometry.Rebuild(naiveMap);
        bvhGeometry.Rebuild(bvhMap);
        std::vector<uint32_t> scratch;
        std::vector<DefenseMap::Cell> naiveOpened, bvhOpened;

        for (int step = 0; step < 6; ++step) {
            const int cx = 4 + step * 5, cy = 6 + step * 4;
            const BlastBrush brush = BlastBrush::Sphere(
                naiveMap.CellCenter(cx, cy), static_cast<float>(naiveMap.CellSize()) * 2.0f);
            std::vector<DefenseMap::Cell> a, b;
            ApplyBlast(naiveMap, naiveGeometry, brush, StaticQueryKind::Naive, scratch, &a);
            ApplyBlast(bvhMap, bvhGeometry, brush, StaticQueryKind::Bvh, scratch, &b);
            std::sort(a.begin(), a.end(), [](const DefenseMap::Cell& l, const DefenseMap::Cell& r) {
                return l.y != r.y ? l.y < r.y : l.x < r.x;
            });
            std::sort(b.begin(), b.end(), [](const DefenseMap::Cell& l, const DefenseMap::Cell& r) {
                return l.y != r.y ? l.y < r.y : l.x < r.x;
            });
            CHECK_EQ(a.size(), b.size());
            bool same = a.size() == b.size();
            for (size_t i = 0; i < a.size() && same; ++i)
                if (a[i].x != b[i].x || a[i].y != b[i].y) same = false;
            CHECK(same);
        }
        CHECK_EQ(RockCount(naiveMap), RockCount(bvhMap));
    }
}

// 캡슐 브러시(선형 폭파)는 구보다 더 많이 연다 — 축을 실제로 읽는다는 증거.
static void TestCapsuleOpensMoreThanSphere() {
    DefenseMap sphereMap = MakeMap(9);
    DefenseMap capsuleMap = MakeMap(9);
    StaticGeometry g1, g2;
    g1.Rebuild(sphereMap);
    g2.Rebuild(capsuleMap);
    std::vector<uint32_t> scratch;

    const math::Vec2 a = sphereMap.CellCenter(10, 10);
    const math::Vec2 b = sphereMap.CellCenter(24, 10);
    const float radius = static_cast<float>(sphereMap.CellSize()) * 1.5f;

    BlastBrush sphere = BlastBrush::Sphere(a, radius);
    BlastBrush capsule;
    capsule.a = {a.x, a.y, 0.5f};
    capsule.b = {b.x, b.y, 0.5f};
    capsule.radius = radius;
    CHECK(!sphere.Capsule());
    CHECK(capsule.Capsule());

    const BlastResult sphereResult = ApplyBlast(sphereMap, g1, sphere, StaticQueryKind::Bvh, scratch);
    const BlastResult capsuleResult = ApplyBlast(capsuleMap, g2, capsule, StaticQueryKind::Bvh, scratch);
    CHECK(capsuleResult.opened > sphereResult.opened);
}

// 감산은 코어를 봉쇄할 수 없다 — 무작위 발파를 퍼부어도 연결성이 유지된다.
static void TestBlastNeverSeals() {
    DefenseMap map = MakeMap(13);
    StaticGeometry geometry;
    geometry.Rebuild(map);
    std::vector<uint32_t> scratch;
    uint32_t rng = 0x5eed1234u;
    auto next = [&rng] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
    for (int i = 0; i < 60; ++i) {
        const int x = static_cast<int>(next() % static_cast<uint32_t>(map.Columns()));
        const int y = static_cast<int>(next() % static_cast<uint32_t>(map.Rows()));
        ApplyBlast(map, geometry, BlastBrush::Sphere(map.CellCenter(x, y),
                                                     static_cast<float>(map.CellSize()) * 2.0f),
                   StaticQueryKind::Bvh, scratch);
        CHECK(map.ToLevel().Validate().Valid());
        CHECK(map.RecomputeRoute());
    }
}

// 호스트와 클라이언트가 같은 명령열에서 같은 격자를 얻는다.
static void TestDeterministicUnderCommands() {
    Switches switches;
    switches.map = MapSource::Procgen;
    switches.mapSeed = 3;

    auto play = [&](StaticQueryKind kind) {
        Switches local = switches;
        local.staticQuery = kind;
        DefenseMap map = MakeMap(static_cast<uint32_t>(local.mapSeed));
        GameWorld world;
        world.Configure(local);
        world.Init(map, defense::DefaultWaves(), 5000, 400.0f);
        for (int i = 0; i < 8; ++i) {
            const DefenseMap::Cell on = world.Map()->RouteCell(3 + i * 4);
            world.Blast(world.Map()->CellCenter(on.x, on.y));
        }
        for (int tick = 0; tick < 400; ++tick) world.Step(1.0f / 60.0f);
        return world.StateHash();
    };

    const uint64_t naive = play(StaticQueryKind::Naive);
    const uint64_t bvh = play(StaticQueryKind::Bvh);
    CHECK_EQ(naive, bvh);
}

// 발파는 골드를 쓰고, 뚫을 바위가 없으면 골드를 쓰지 않는다.
static void TestBlastCostsGoldOnlyWhenItOpens() {
    Switches switches;
    switches.map = MapSource::Procgen;
    DefenseMap map = MakeMap(7);
    GameWorld world;
    world.Configure(switches);
    world.Init(map, defense::DefaultWaves(), 200, 400.0f);

    // 경로 위(= 이미 열린 칸) 발파는 아무것도 열지 못한다.
    const DefenseMap::Cell open = world.Map()->RouteCell(1);
    const int goldBefore = world.Gold();
    // 경로 한복판은 주변도 대개 열려 있다. 열리지 않았다면 골드가 그대로여야 한다.
    const int opened = world.Blast(world.Map()->CellCenter(open.x, open.y));
    if (opened == 0) CHECK_EQ(world.Gold(), goldBefore);
    else             CHECK_EQ(world.Gold(), goldBefore - kBlastCost);

    // 골드가 모자라면 지형이 바뀌지 않는다.
    GameWorld poor;
    poor.Configure(switches);
    poor.Init(map, defense::DefaultWaves(), kBlastCost - 1, 400.0f);
    const size_t routeBefore = poor.RouteLength();
    bool anyOpened = false;
    for (int i = 0; i < 5; ++i) {
        const DefenseMap::Cell c = poor.Map()->RouteCell(2 + i * 6);
        if (poor.Blast(poor.Map()->CellCenter(c.x, c.y)) > 0) anyOpened = true;
    }
    CHECK(!anyOpened);
    CHECK_EQ(poor.OpenedCells().size(), size_t{0});
    CHECK_EQ(poor.RouteLength(), routeBefore);
}

static void RunTests() {
    TestOpensExactlyBrushVolume();
    TestArmsOpenSameCells();
    TestCapsuleOpensMoreThanSphere();
    TestBlastNeverSeals();
    TestDeterministicUnderCommands();
    TestBlastCostsGoldOnlyWhenItOpens();
}

TEST_MAIN("terrain_blast")
