// 절차 맵: 시드 재현성 · 밀도 임계의 단조성 · 연결성 · 경로 · 광맥.
//
// 이 파일이 지키는 불변식 넷.
//   (1) 같은 시드는 같은 격자다. 호스트와 클라이언트가 맵을 주고받지 않고도 같은
//       세계를 보는 근거가 이것 하나뿐이다.
//   (2) 밀도 임계를 낮추면 개방 칸이 **단조 증가**한다. 임계가 실제로 밀도장을
//       읽고 있다는 것을 그 단조성이 증명한다.
//   (3) 채택된 맵은 언제나 스폰→코어가 연결돼 있다(tools::Level 의 flood fill).
//   (4) 경로는 개방 칸만 지나고 스폰에서 시작해 코어에서 끝난다.
#include "algo/dsu.hpp"
#include "game/defense_content.hpp"
#include "game/game.hpp"
#include "game/map_gen.hpp"
#include "math/noise.hpp"
#include "test_util.h"
#include "tools/procgen.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace mega;
using namespace mega::game;

static int OpenCells(const DefenseMap& map) {
    int open = 0;
    for (int y = 0; y < map.Rows(); ++y)
        for (int x = 0; x < map.Columns(); ++x)
            if (!map.Rock(x, y)) ++open;
    return open;
}

// (1) 같은 시드 → 같은 격자. 다른 시드 → 다른 격자.
static void TestSeedReproducible() {
    mapgen::TerrainParams params;
    params.seed = 3;
    DefenseMap a, b;
    CHECK(mapgen::Generate(params, a));
    CHECK(mapgen::Generate(params, b));
    CHECK_EQ(a.Columns(), b.Columns());
    CHECK_EQ(a.Rows(), b.Rows());
    bool identical = true;
    for (int y = 0; y < a.Rows(); ++y)
        for (int x = 0; x < a.Columns(); ++x)
            if (a.Tile(x, y) != b.Tile(x, y) || a.Vein(x, y) != b.Vein(x, y) ||
                a.Shade(x, y) != b.Shade(x, y))
                identical = false;
    CHECK(identical);
    CHECK_EQ(a.RouteLength(), b.RouteLength());

    params.seed = 4;
    DefenseMap other;
    CHECK(mapgen::Generate(params, other));
    int differences = 0;
    for (int y = 0; y < a.Rows(); ++y)
        for (int x = 0; x < a.Columns(); ++x)
            if (a.Tile(x, y) != other.Tile(x, y)) ++differences;
    CHECK(differences > 100);   // 시드가 정말 다른 세계를 만든다
}

// (2) 임계를 낮추면 개방 칸이 단조 증가한다. Hash2 의 믹싱 상수를 지워 밀도장이
// 축 정렬 줄무늬가 되면 이 단조성과 아래 분포 단언이 함께 무너진다.
static void TestCarveMonotonic() {
    mapgen::TerrainParams params;
    params.seed = 5;
    int previous = -1;
    for (float carve : {0.60f, 0.40f, 0.20f, 0.00f, -0.20f, -0.40f}) {
        params.carve = carve;
        DefenseMap map;
        mapgen::TerrainStats stats;
        CHECK(mapgen::Generate(params, map, &stats));
        const int open = OpenCells(map);
        CHECK(open >= previous);          // 단조 증가(같을 수는 있다)
        previous = open;
        CHECK(stats.valid);
    }
    // 양 끝은 실제로 달라야 한다 — 임계를 읽지 않는 구현도 "단조"는 만족한다.
    params.carve = 0.60f;
    DefenseMap tight;
    CHECK(mapgen::Generate(params, tight));
    params.carve = -0.40f;
    DefenseMap loose;
    CHECK(mapgen::Generate(params, loose));
    CHECK(OpenCells(loose) > OpenCells(tight) + 200);
}

// 밀도장이 축에 정렬돼 있지 않은가. Hash2 가 두 좌표를 섞지 않으면 x 를 바꿔도
// 값이 거의 그대로여서 행 단위 줄무늬가 생긴다.
static void TestDensityNotAxisAligned() {
    mapgen::TerrainParams params;
    const math::Noise noise(1234u);
    float rowVariance = 0.0f, columnVariance = 0.0f;
    for (int y = 1; y < 20; ++y) {
        float rowSum = 0.0f, rowSquare = 0.0f;
        for (int x = 1; x < 20; ++x) {
            const float d = mapgen::Density(noise, x, y, params);
            rowSum += d;
            rowSquare += d * d;
        }
        rowVariance += rowSquare / 19.0f - (rowSum / 19.0f) * (rowSum / 19.0f);
    }
    for (int x = 1; x < 20; ++x) {
        float columnSum = 0.0f, columnSquare = 0.0f;
        for (int y = 1; y < 20; ++y) {
            const float d = mapgen::Density(noise, x, y, params);
            columnSum += d;
            columnSquare += d * d;
        }
        columnVariance += columnSquare / 19.0f - (columnSum / 19.0f) * (columnSum / 19.0f);
    }
    // 두 축의 분산이 비슷해야 등방성이다. 한쪽이 0 에 가까우면 줄무늬다.
    CHECK(rowVariance > 0.05f);
    CHECK(columnVariance > 0.05f);
    const float ratio = rowVariance / columnVariance;
    CHECK(ratio > 0.25f && ratio < 4.0f);
}

// (3)(4) 연결성 · 경로의 성질.
static void TestRouteWellFormed() {
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        mapgen::TerrainParams params;
        params.seed = seed;
        DefenseMap map;
        mapgen::TerrainStats stats;
        CHECK(mapgen::Generate(params, map, &stats));
        CHECK(stats.valid);
        CHECK(stats.route >= 2);
        CHECK(map.ToLevel().Validate().Valid());

        const size_t length = map.RouteLength();
        CHECK(length == static_cast<size_t>(stats.route));
        const DefenseMap::Cell first = map.RouteCell(0);
        const DefenseMap::Cell last = map.RouteCell(length - 1);
        CHECK(first.x == map.SpawnCell().x && first.y == map.SpawnCell().y);
        CHECK(last.x == map.CoreCell().x && last.y == map.CoreCell().y);
        bool contiguous = true, walkable = true;
        for (size_t i = 0; i < length; ++i) {
            const DefenseMap::Cell c = map.RouteCell(i);
            if (map.Rock(c.x, c.y)) walkable = false;
            if (i == 0) continue;
            const DefenseMap::Cell p = map.RouteCell(i - 1);
            if (std::abs(p.x - c.x) + std::abs(p.y - c.y) != 1) contiguous = false;
        }
        CHECK(contiguous);
        CHECK(walkable);
        // 테두리는 언제나 바위다 — 아레나의 틀.
        CHECK(map.Rock(0, 0));
        CHECK(map.Rock(map.Columns() - 1, map.Rows() - 1));
    }
}

// 광맥은 개방 칸 위에만 있고, 바위 속에는 없다.
static void TestVeinsOnOpenCells() {
    mapgen::TerrainParams params;
    params.seed = 2;
    DefenseMap map;
    mapgen::TerrainStats stats;
    CHECK(mapgen::Generate(params, map, &stats));
    CHECK(stats.veins > 0);
    int veins = 0, inRock = 0;
    for (int y = 0; y < map.Rows(); ++y)
        for (int x = 0; x < map.Columns(); ++x)
            if (map.Vein(x, y)) {
                ++veins;
                if (map.Rock(x, y)) ++inRock;
            }
    CHECK_EQ(veins, stats.veins);
    CHECK_EQ(inRock, 0);
}

// 배치 규칙: 코어를 봉쇄하는 자리는 거절된다. 회랑 한 칸을 실제로 막아 본다.
static void TestPlacementConnectivityGate() {
    mapgen::TerrainParams params;
    params.seed = 6;
    params.carve = 0.9f;          // 거의 깎지 않는다 = 1칸 회랑 미로
    DefenseMap map;
    CHECK(mapgen::Generate(params, map));

    // 회랑 폭이 1이면 경로 위 어느 칸을 막아도 스폰→코어가 끊긴다.
    int blocked = 0, allowed = 0;
    for (size_t i = 1; i + 1 < map.RouteLength(); ++i) {
        const DefenseMap::Cell c = map.RouteCell(i);
        if (map.WouldRemainConnected(c.x, c.y)) ++allowed;
        else ++blocked;
    }
    CHECK(blocked > 0);
    // 스폰과 코어 자신은 언제나 통행 가능하다(점유 대상이 아니다).
    CHECK(map.Passable(map.SpawnCell().x, map.SpawnCell().y));
    CHECK(map.Passable(map.CoreCell().x, map.CoreCell().y));
    (void)allowed;
}

// **제품 경로 판정**: GameWorld::CanPlaceTower 가 그 검사를 실제로 부르는가.
// 위 테스트는 DefenseMap 의 함수를 직접 불렀다 — 규칙이 그것을 부르지 않아도
// 통과한다. 여기서는 배치 명령을 실제로 내려 거절을 확인한다.
static void TestWorldRejectsSealingPlacement() {
    mapgen::TerrainParams params;
    params.seed = 6;
    params.carve = 0.9f;              // 거의 깎지 않는다 = 1칸 회랑
    DefenseMap map;
    CHECK(mapgen::Generate(params, map));

    Switches switches;
    switches.map = MapSource::Procgen;
    GameWorld world;
    world.Configure(switches);
    world.Init(map, defense::DefaultWaves(), 100000, 1000.0f);

    int rejected = 0, accepted = 0;
    // 경로 위 칸을 차례로 눌러 본다. 1칸 회랑이면 대부분이 봉쇄를 만든다.
    for (size_t i = 1; i + 1 < world.Map()->RouteLength(); ++i) {
        const DefenseMap::Cell cell = world.Map()->RouteCell(i);
        if (world.Map()->Tile(cell.x, cell.y) != TileType::Buildable) continue;
        const math::Vec2 at = world.Map()->CellCenter(cell.x, cell.y);
        const bool can = world.CanPlaceTower(at);
        const bool placed = world.PlaceTowerType(at, TowerType::Cannon);
        CHECK_EQ(can, placed);        // 질의와 명령이 같은 답을 낸다
        if (placed) { ++accepted; break; }   // 하나라도 서면 경로가 바뀐다 — 여기서 멈춘다
        ++rejected;
        // 거절된 배치는 아무것도 바꾸지 않았다.
        CHECK_EQ(world.TowerCount(), 0);
        CHECK(world.Map()->ToLevel().Validate().Valid());
    }
    CHECK(rejected > 0);              // 연결성 게이트가 실제로 막았다
    (void)accepted;

    // 골드는 충분하다 — 거절의 이유가 골드가 아니라는 것.
    CHECK(world.Gold() >= 100000 - TowerSpecFor(TowerType::Cannon).cost);
}

// ── 골격 팔 둘 ─────────────────────────────────────────────────────
//
// 두 팔이 지켜야 하는 것은 같다: 신장 트리다. 다른 것은 형질이다.
//
// 채택된 간선 수를 세는 방법. 셀은 홀수 좌표에 있고, 셀 사이 벽은 (짝수, 홀수)
// 또는 (홀수, 짝수) 자리다. 그 자리가 열려 있으면 간선 하나가 채택된 것이다.
static int CarvedEdges(const tools::Level& level) {
    int edges = 0;
    for (int y = 1; y < level.Height() - 1; ++y)
        for (int x = 1; x < level.Width() - 1; ++x) {
            const bool wallSlot = ((x & 1) == 0) != ((y & 1) == 0);
            if (wallSlot && level.At(x, y) != '#') ++edges;
        }
    return edges;
}

// 격자 위 개방 칸을 서로소 집합으로 묶어 성분 수를 센다. flood fill 과 **다른
// 구조**로 같은 질문에 답하므로 두 판정이 같은 버그를 공유하지 않는다.
static size_t OpenComponents(const tools::Level& level) {
    const size_t count = static_cast<size_t>(level.Width()) * static_cast<size_t>(level.Height());
    algo::DisjointSet dsu(count);
    size_t open = 0;
    for (int y = 0; y < level.Height(); ++y)
        for (int x = 0; x < level.Width(); ++x) {
            if (level.At(x, y) == '#') continue;
            ++open;
            const size_t here = static_cast<size_t>(y * level.Width() + x);
            if (x + 1 < level.Width() && level.At(x + 1, y) != '#') dsu.Union(here, here + 1);
            if (y + 1 < level.Height() && level.At(x, y + 1) != '#')
                dsu.Union(here, here + static_cast<size_t>(level.Width()));
        }
    // 벽 칸은 각자 홀로 남으므로 성분 수에서 빼 준다.
    return dsu.Components() - (count - open);
}

// (1)(2)(3)(4) 두 팔 모두 신장 트리이고, 같은 시드는 같은 격자다.
static void TestMazeArmsAreSpanningTrees() {
    const int cw = 20, ch = 20;
    const int cells = cw * ch;
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        for (int arm = 0; arm < 2; ++arm) {
            const bool kruskal = arm == 1;
            const tools::Level level = tools::MazeGen::GenerateWith(kruskal, cw, ch, seed);
            // (1) 채택 간선 수 == 셀 수 - 1.
            CHECK_EQ(CarvedEdges(level), cells - 1);
            // (2) 사이클 0 — 간선 수 + 성분 수 == 정점 수. 성분이 1개이므로
            //     이 등식은 곧 (1) 과 같은 말이지만, 성분 수를 **DSU 로 따로
            //     세어** 확인하는 것이 요점이다.
            CHECK_EQ(OpenComponents(level), static_cast<size_t>(1));
            // (3) DSU 판정과 flood fill 판정이 일치한다.
            CHECK(level.Validate().Valid());
            // (4) 같은 시드 → 같은 격자.
            const tools::Level again = tools::MazeGen::GenerateWith(kruskal, cw, ch, seed);
            bool identical = true;
            for (int y = 0; y < level.Height(); ++y)
                for (int x = 0; x < level.Width(); ++x)
                    if (level.At(x, y) != again.At(x, y)) identical = false;
            CHECK(identical);
        }
    }
}

// 형질이 실제로 다른가. 분기 셀(개방 이웃이 3 이상인 셀)의 수를 센다 — 이것이
// "어디를 막을까"의 선택지 수이고, 그래서 밸런스 손잡이다.
static void TestMazeArmsDifferInShape() {
    const int cw = 20, ch = 20;
    int dfsBranches = 0, kruskalBranches = 0, dfsDeadEnds = 0, kruskalDeadEnds = 0;
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        for (int arm = 0; arm < 2; ++arm) {
            const tools::Level level = tools::MazeGen::GenerateWith(arm == 1, cw, ch, seed);
            int branches = 0, deadEnds = 0;
            for (int cy = 0; cy < ch; ++cy)
                for (int cx = 0; cx < cw; ++cx) {
                    const int x = 2 * cx + 1, y = 2 * cy + 1;
                    int degree = 0;
                    if (level.At(x + 1, y) != '#') ++degree;
                    if (level.At(x - 1, y) != '#') ++degree;
                    if (level.At(x, y + 1) != '#') ++degree;
                    if (level.At(x, y - 1) != '#') ++degree;
                    if (degree >= 3) ++branches;
                    if (degree == 1) ++deadEnds;
                }
            if (arm == 0) { dfsBranches += branches; dfsDeadEnds += deadEnds; }
            else          { kruskalBranches += branches; kruskalDeadEnds += deadEnds; }
        }
    }
    // 두 팔의 분기 수는 같지 않다. 같다면 스위치가 아무것도 바꾸지 않은 것이다.
    CHECK(dfsBranches != kruskalBranches);
    std::printf("  골격 형질 8시드 합계 — dfs 분기 %d 막다른길 %d / "
                "kruskal 분기 %d 막다른길 %d\n",
                dfsBranches, dfsDeadEnds, kruskalBranches, kruskalDeadEnds);
}

// 두 팔 모두 절차 생성 전 과정을 통과하는가 — 밀도장 감산 · flood fill 검증 ·
// 경로 재계산. 골격만 바꿨을 뿐 뒤의 파이프라인이 그대로 돈다는 것이 흡수의 조건이다.
static void TestKruskalArmSurvivesPipeline() {
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        mapgen::TerrainParams params;
        params.seed = seed;
        params.maze = MazeKind::Kruskal;
        DefenseMap map;
        mapgen::TerrainStats stats;
        CHECK(mapgen::Generate(params, map, &stats));
        CHECK(stats.valid);
        CHECK(map.RouteLength() >= 2);
        CHECK(map.ToLevel().Validate().Valid());
        CHECK(map.RecomputeRoute());
    }
}

static void RunTests() {
    TestWorldRejectsSealingPlacement();
    TestMazeArmsAreSpanningTrees();
    TestMazeArmsDifferInShape();
    TestKruskalArmSurvivesPipeline();
    TestSeedReproducible();
    TestCarveMonotonic();
    TestDensityNotAxisAligned();
    TestRouteWellFormed();
    TestVeinsOnOpenCells();
    TestPlacementConnectivityGate();
}

TEST_MAIN("map_procgen")
