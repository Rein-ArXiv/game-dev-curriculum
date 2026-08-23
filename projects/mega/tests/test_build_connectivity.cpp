// 배치 판정의 두 팔이 같은 답을 내는가 — 그리고 그 판정이 게임을 갈리게 하지 않는가.
//
// 메이즈 디펜스의 배치 규칙은 "이 칸을 막아도 스폰에서 코어까지 갈 수 있는가"다.
// 그 질문에 답하는 구현이 둘이고, **구조가 서로 다르다**.
//
//   floodfill   후보 격자를 통째로 짓고(41x41 char 할당 + 복사) 4방향 flood fill
//               을 한 번 돈다. 칸 하나에 한 번.
//   artic       격자 버전당 low-link DFS 를 한 번 돌아 스폰-코어를 가르는 정점을
//               **전 칸에 대해** 미리 표시하고, 질의는 표를 한 번 읽는다.
//
// 두 구현이 같은 버그를 공유하지 않는다는 것이 이 파일의 값이다. 한쪽은 도달성을
// 직접 시뮬레이션하고, 다른 쪽은 DFS 트리의 low-link 부등식으로 판정한다 —
// 공통 원인이 없으므로 차분 대조가 실제 오라클이 된다.
//
// 이 파일이 단언하는 것 넷.
//   (1) 여러 시드의 생성 맵에서 **모든 칸**에 대해 두 판정이 일치한다.
//   (2) 발파(OpenCell)와 잔해 안착(SetOccupied)으로 격자를 바꾼 뒤에도 전 칸 일치.
//   (3) 스폰·코어·이미 못 가는 칸의 예외 규칙이 일치한다.
//   (4) 두 팔의 상태 해시가 **비트 동일**하다. 판정이 같으므로 게임 진행이 갈리면
//       안 되고, 갈리지 않는다는 것이 곧 이 흡수가 규칙 변경이 아니라는 증거다.

#include "algo/articulation.hpp"
#include "game/defense_content.hpp"
#include "game/game.hpp"
#include "game/map_gen.hpp"
#include "test_util.h"

#include <chrono>
#include <cstdio>
#include <vector>

using namespace mega;
using namespace mega::game;

namespace {

DefenseMap MakeMap(uint32_t seed, float carve) {
    mapgen::TerrainParams params;
    params.seed = seed;
    params.carve = carve;
    DefenseMap map;
    CHECK(mapgen::Generate(params, map));
    return map;
}

// 두 팔로 같은 격자를 묻는다. 격자 밖 한 칸까지 포함해 전 범위를 훑는다 —
// 예외 규칙의 경계가 정확히 거기 있다.
struct Disagreement {
    int  count = 0;
    int  firstX = -1, firstY = -1;
    bool floodVerdict = false, articVerdict = false;
};

Disagreement CompareAllCells(const DefenseMap& source) {
    DefenseMap flood = source;
    DefenseMap artic = source;
    flood.SetBuildCheck(BuildCheckKind::FloodFill);
    artic.SetBuildCheck(BuildCheckKind::Articulation);

    Disagreement result;
    for (int y = -1; y <= source.Rows(); ++y) {
        for (int x = -1; x <= source.Columns(); ++x) {
            const bool a = flood.WouldRemainConnected(x, y);
            const bool b = artic.WouldRemainConnected(x, y);
            if (a == b) continue;
            if (result.count == 0) {
                result.firstX = x;
                result.firstY = y;
                result.floodVerdict = a;
                result.articVerdict = b;
            }
            ++result.count;
        }
    }
    return result;
}

// (1) 여러 시드 · 여러 밀도 임계에서 전 칸 일치.
void TestVerdictsAgreeOnGeneratedMaps() {
    int cells = 0;
    int chokes = 0;
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        // carve 를 크게 잡으면 거의 깎지 않아 1칸 회랑이 되고, 그때 분리 정점이
        // 가장 많다. 작게 잡으면 넓은 방이 되어 거의 없다. 두 극단을 다 본다.
        for (float carve : {0.02f, 0.5f, 0.9f}) {
            const DefenseMap map = MakeMap(seed, carve);
            const Disagreement diff = CompareAllCells(map);
            if (diff.count != 0)
                std::printf("  불일치 seed=%u carve=%.2f 첫 칸=(%d,%d) flood=%d artic=%d 총 %d칸\n",
                            seed, static_cast<double>(carve), diff.firstX, diff.firstY,
                            diff.floodVerdict ? 1 : 0, diff.articVerdict ? 1 : 0, diff.count);
            CHECK_EQ(diff.count, 0);
            cells += (map.Rows() + 2) * (map.Columns() + 2);

            DefenseMap artic = map;
            artic.SetBuildCheck(BuildCheckKind::Articulation);
            chokes += static_cast<int>(artic.ChokeCount());
        }
    }
    // 전수 판정이 실제로 무언가를 표시했는가. 0 이면 두 팔이 "전부 배치 가능"에
    // 대해서만 합의한 것이고, 그때 이 대조는 아무것도 검사하지 않는다.
    CHECK(chokes > 0);
    std::printf("  전 칸 대조 %d칸 일치, 표시된 봉쇄 칸 합계 %d\n", cells, chokes);
}

// (2) 격자를 바꾼 뒤에도 일치하는가. 발파는 바위를 열고, 잔해 안착은 칸을 막는다.
void TestVerdictsAgreeAfterTerrainChanges() {
    DefenseMap map = MakeMap(4, 0.9f);
    CHECK_EQ(CompareAllCells(map).count, 0);

    // 발파: 바위 24칸을 연다.
    int opened = 0;
    for (int y = 1; y + 1 < map.Rows() && opened < 24; ++y)
        for (int x = 1; x + 1 < map.Columns() && opened < 24; ++x)
            if (map.OpenCell(x, y)) ++opened;
    CHECK(opened > 0);
    CHECK(map.RecomputeRoute());
    CHECK_EQ(CompareAllCells(map).count, 0);

    // 잔해 안착: 경로를 끊지 않는 칸만 골라 막는다(제품이 하는 그대로).
    DefenseMap gate = map;
    gate.SetBuildCheck(BuildCheckKind::Articulation);
    int blocked = 0;
    for (int y = 1; y + 1 < map.Rows() && blocked < 24; ++y)
        for (int x = 1; x + 1 < map.Columns() && blocked < 24; ++x) {
            if (!map.Passable(x, y)) continue;
            if (!gate.WouldRemainConnected(x, y)) continue;
            map.SetOccupied(x, y, true);
            gate.SetOccupied(x, y, true);
            ++blocked;
        }
    CHECK(blocked > 0);
    CHECK(map.RecomputeRoute());
    CHECK_EQ(CompareAllCells(map).count, 0);
    std::printf("  발파 %d칸 · 잔해 %d칸 뒤에도 전 칸 일치\n", opened, blocked);
}

// (3) 예외 규칙. 스폰·코어·격자 밖·이미 못 가는 칸에서 두 팔이 같아야 한다.
void TestExceptionRulesMatch() {
    DefenseMap map = MakeMap(5, 0.9f);
    DefenseMap flood = map;
    DefenseMap artic = map;
    flood.SetBuildCheck(BuildCheckKind::FloodFill);
    artic.SetBuildCheck(BuildCheckKind::Articulation);

    const DefenseMap::Cell spawn = map.SpawnCell();
    const DefenseMap::Cell core = map.CoreCell();
    // 스폰·코어는 막을 수 없다 — 후보 격자에서 그 두 칸이 'S'/'G' 로 덮어써진다.
    CHECK(flood.WouldRemainConnected(spawn.x, spawn.y));
    CHECK_EQ(artic.WouldRemainConnected(spawn.x, spawn.y),
             flood.WouldRemainConnected(spawn.x, spawn.y));
    CHECK_EQ(artic.WouldRemainConnected(core.x, core.y),
             flood.WouldRemainConnected(core.x, core.y));

    // 격자 밖.
    CHECK_EQ(artic.WouldRemainConnected(-1, 0), flood.WouldRemainConnected(-1, 0));
    CHECK_EQ(artic.WouldRemainConnected(map.Columns(), map.Rows()),
             flood.WouldRemainConnected(map.Columns(), map.Rows()));

    // 이미 못 가는 칸(바위)을 다시 막아도 아무것도 바뀌지 않는다.
    int rocks = 0;
    for (int y = 0; y < map.Rows() && rocks < 32; ++y)
        for (int x = 0; x < map.Columns() && rocks < 32; ++x) {
            if (!map.Rock(x, y)) continue;
            CHECK_EQ(artic.WouldRemainConnected(x, y), flood.WouldRemainConnected(x, y));
            ++rocks;
        }
    CHECK(rocks > 0);

    // 전수 표는 통행 칸에만 표시가 붙는다. 바위 칸이 표시되면 HUD 가 아무 뜻도
    // 없는 칸을 칠하게 된다.
    int paintedRock = 0;
    for (int y = 0; y < map.Rows(); ++y)
        for (int x = 0; x < map.Columns(); ++x)
            if (artic.Choke(x, y) && !map.Passable(x, y)) ++paintedRock;
    CHECK_EQ(paintedRock, 0);

    // floodfill 팔은 전수 표를 갖지 않는다 — 그 표를 채우는 것이 바로 이 팔이
    // 대신하는 일이기 때문이다.
    CHECK(flood.ChokeMask().empty());
    CHECK(!artic.ChokeMask().empty());
}

// (4) 두 팔의 게임 진행이 비트 동일한가. 판정이 같으므로 세계가 갈리면 안 된다.
uint64_t RunWorld(BuildCheckKind check, uint64_t* placedOut, uint64_t* rejectedOut) {
    Switches switches;
    switches.map = MapSource::Procgen;
    switches.mapSeed = 6;
    switches.buildCheck = check;

    // carve 를 중간에 둔다. 1칸 회랑(0.9)이면 경로 위 모든 칸이 봉쇄라 배치가
    // 한 번도 성공하지 않고, 넓은 방(0.02)이면 한 번도 거절되지 않는다. 둘 다
    // 일어나야 두 팔이 실제로 갈릴 기회를 얻는다.
    const DefenseMap map = MakeMap(6, 0.35f);
    GameWorld world;
    world.Configure(switches);
    world.Init(map, defense::DefaultWaves(), 200000, 5000.0f);

    uint64_t placed = 0, rejected = 0;
    // 경로 위 칸을 차례로 눌러 본다. 대부분은 봉쇄를 만들어 거절되고, 어쩌다
    // 하나가 서면 경로가 바뀌어 그 뒤의 판정이 통째로 달라진다 — 두 팔이 같은
    // 자리에서 같은 답을 내야만 상태 해시가 붙는다.
    for (int tick = 0; tick < 400; ++tick) {
        const DefenseMap* live = world.Map();
        if (live && live->RouteLength() > 2) {
            const size_t at = 1 + static_cast<size_t>(tick) % (live->RouteLength() - 2);
            const DefenseMap::Cell cell = live->RouteCell(at);
            const math::Vec2 where = live->CellCenter(cell.x, cell.y);
            if (world.PlaceTowerType(where, TowerType::Cannon)) ++placed;
            else ++rejected;
        }
        world.Step(1.0f / 60.0f);
    }
    if (placedOut) *placedOut = placed;
    if (rejectedOut) *rejectedOut = rejected;
    return world.StateHash();
}

void TestArmsProduceIdenticalWorlds() {
    uint64_t placedA = 0, rejectedA = 0, placedB = 0, rejectedB = 0;
    const uint64_t flood = RunWorld(BuildCheckKind::FloodFill, &placedA, &rejectedA);
    const uint64_t artic = RunWorld(BuildCheckKind::Articulation, &placedB, &rejectedB);
    CHECK_EQ(flood, artic);
    CHECK_EQ(placedA, placedB);
    CHECK_EQ(rejectedA, rejectedB);
    // 거절이 0 이면 연결성 게이트가 한 번도 답하지 않은 것이고, 그때 이 대조는
    // 아무것도 비교하지 않는다.
    CHECK(rejectedA > 0);
    // 배치도 실제로 성공해야 한다. 전부 거절이면 경로가 한 번도 바뀌지 않아
    // 두 팔이 같은 정적 격자만 보고 답한 셈이다.
    CHECK(placedA > 0);
    std::printf("  두 팔 상태 해시 0x%016llx 동일 (배치 %llu · 거절 %llu)\n",
                static_cast<unsigned long long>(flood),
                static_cast<unsigned long long>(placedA),
                static_cast<unsigned long long>(rejectedA));
}

// low-link 판정기 자체의 경계. 스폰과 코어가 애초에 끊긴 격자에서는 무엇을 막아도
// 끊긴 채여야 한다 — 그 상태를 "연결됨"으로 보고하면 배치 규칙이 통째로 열린다.
void TestDisconnectedGridRejectsEverything() {
    // 4x3 격자. 가운데 열이 전부 벽이라 좌우가 끊겨 있다.
    std::vector<uint8_t> passable(12, 1);
    for (int y = 0; y < 3; ++y) passable[static_cast<size_t>(y * 4 + 2)] = 0;
    algo::GridSeparators separators;
    CHECK(!separators.Build(passable.data(), 4, 3, 0, 0, 3, 0));
    CHECK(!separators.Connected());
    CHECK_EQ(separators.SeparatorCount(), static_cast<size_t>(0));

    // 벽을 하나 열면 좌우가 이어진다. 그 통로는 세 칸짜리 목이므로 (1,1)·(2,1)·
    // (3,1) 셋 다 분리 정점이다 — 셋 중 어느 하나만 막아도 코어가 떨어진다.
    passable[static_cast<size_t>(1 * 4 + 2)] = 1;
    CHECK(separators.Build(passable.data(), 4, 3, 0, 0, 3, 0));
    CHECK(separators.Connected());
    CHECK(separators.Separates(1, 1));
    CHECK(separators.Separates(2, 1));
    CHECK(separators.Separates(3, 1));
    CHECK_EQ(separators.SeparatorCount(), static_cast<size_t>(3));
    // 목을 우회하는 칸은 표시되지 않는다.
    CHECK(!separators.Separates(1, 0));
    CHECK(!separators.Separates(0, 1));
    CHECK(!separators.Separates(0, 2));
    // 스폰과 코어 자신은 표시되지 않는다.
    CHECK(!separators.Separates(0, 0));
    CHECK(!separators.Separates(3, 0));
}

// ── 무작위 위상에 대한 전수 대조 ───────────────────────────────────
//
// 절차 맵은 밀도장이 만든 **특정한 형질**만 갖는다. 그 형질 밖의 위상 — 고리에
// 매달린 막다른 가지, 두 지점에서 붙는 곁길 — 에서만 드러나는 결함이 있고,
// low-link 의 역간선 처리가 정확히 그 부류다.
//
// 그래서 무작위 벽 배치의 소격자를 많이 만들고, **칸을 실제로 지워 BFS 를 돌린
// 결과**와 대조한다. 이쪽 오라클에는 DFS 도 low 도 없다 — 정의를 그대로 실행한다.
bool ReachesGoal(const std::vector<uint8_t>& passable, int width, int height,
                 int sx, int sy, int gx, int gy, int blockX, int blockY) {
    auto open = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height) return false;
        if (x == blockX && y == blockY) return false;
        return passable[static_cast<size_t>(y * width + x)] != 0;
    };
    if (!open(sx, sy) || !open(gx, gy)) return false;
    std::vector<uint8_t> seen(passable.size(), 0);
    std::vector<int> queue{sy * width + sx};
    seen[static_cast<size_t>(sy * width + sx)] = 1;
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    for (size_t head = 0; head < queue.size(); ++head) {
        const int cx = queue[head] % width, cy = queue[head] / width;
        for (int d = 0; d < 4; ++d) {
            const int nx = cx + dx[d], ny = cy + dy[d];
            if (!open(nx, ny)) continue;
            const size_t next = static_cast<size_t>(ny * width + nx);
            if (seen[next]) continue;
            seen[next] = 1;
            queue.push_back(static_cast<int>(next));
        }
    }
    return seen[static_cast<size_t>(gy * width + gx)] != 0;
}

void TestRandomTopologiesAgainstBruteForce() {
    const int width = 7, height = 7;
    const int spawnX = 0, spawnY = 0, coreX = 6, coreY = 6;
    uint32_t rng = 0x5EED1234u;
    auto next = [&rng]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };

    int connectedGrids = 0, separators = 0, cells = 0;
    for (int trial = 0; trial < 4000; ++trial) {
        // 벽 밀도를 섞는다. 성긴 격자는 분리 정점이 거의 없고, 빽빽한 격자는
        // 애초에 끊긴다 — 흥미로운 위상은 그 사이에 있다.
        const uint32_t openPercent = 45 + next() % 40u;
        std::vector<uint8_t> passable(static_cast<size_t>(width * height), 0);
        for (size_t i = 0; i < passable.size(); ++i)
            passable[i] = (next() % 100u) < openPercent ? uint8_t{1} : uint8_t{0};
        passable[static_cast<size_t>(spawnY * width + spawnX)] = 1;
        passable[static_cast<size_t>(coreY * width + coreX)] = 1;

        algo::GridSeparators separatorsIndex;
        const bool built = separatorsIndex.Build(passable.data(), width, height,
                                                 spawnX, spawnY, coreX, coreY);
        const bool reachable = ReachesGoal(passable, width, height, spawnX, spawnY,
                                           coreX, coreY, -1, -1);
        CHECK_EQ(built, reachable);
        CHECK_EQ(separatorsIndex.Connected(), reachable);
        if (!reachable) continue;
        ++connectedGrids;

        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                if (!passable[static_cast<size_t>(y * width + x)]) continue;
                if ((x == spawnX && y == spawnY) || (x == coreX && y == coreY)) continue;
                ++cells;
                const bool stillReaches = ReachesGoal(passable, width, height, spawnX,
                                                      spawnY, coreX, coreY, x, y);
                const bool marked = separatorsIndex.Separates(x, y);
                if (marked != !stillReaches) {
                    ++megatest::g_failures;
                    std::printf("  FAIL trial=%d 칸=(%d,%d) 표시=%d 실제단절=%d\n",
                                trial, x, y, marked ? 1 : 0, stillReaches ? 0 : 1);
                    ++megatest::g_checks;
                    return;
                }
                ++megatest::g_checks;
                if (marked) ++separators;
            }
    }
    // 분리 정점이 하나도 없었다면 이 대조는 "전부 아니다"만 확인한 것이다.
    CHECK(separators > 100);
    std::printf("  무작위 격자 %d개(연결됨) · 칸 %d개 전수 대조, 분리 정점 %d개\n",
                connectedGrids, cells, separators);
}

// ── 비용 ───────────────────────────────────────────────────────────
//
// 이 저장소의 A/B 규약은 두 단계다 — (a) 두 팔의 결과가 같다는 단언, (b) 두 팔의
// 비용 수치. (a) 는 위 넷이 했다. 여기는 (b) 다.
//
// 재는 것은 "격자 전체를 한 번 묻는 비용"이다. HUD 가 격자를 칠하려면 그것이
// 프레임마다 필요한 수이고, 칸 질의 팔에서는 그 수가 격자 칸 수만큼의 flood fill 이다.
void MeasureQueryCost() {
    const DefenseMap source = MakeMap(3, 0.35f);
    DefenseMap flood = source;
    DefenseMap artic = source;
    flood.SetBuildCheck(BuildCheckKind::FloodFill);
    artic.SetBuildCheck(BuildCheckKind::Articulation);

    auto sweep = [](const DefenseMap& map) {
        int allowed = 0;
        for (int y = 0; y < map.Rows(); ++y)
            for (int x = 0; x < map.Columns(); ++x)
                if (map.WouldRemainConnected(x, y)) ++allowed;
        return allowed;
    };

    const int rounds = 20;
    const auto floodStart = std::chrono::steady_clock::now();
    int floodAllowed = 0;
    for (int i = 0; i < rounds; ++i) floodAllowed = sweep(flood);
    const auto floodEnd = std::chrono::steady_clock::now();

    const auto articStart = std::chrono::steady_clock::now();
    int articAllowed = 0;
    for (int i = 0; i < rounds; ++i) articAllowed = sweep(artic);
    const auto articEnd = std::chrono::steady_clock::now();

    CHECK_EQ(floodAllowed, articAllowed);
    const double floodMs =
        std::chrono::duration<double, std::milli>(floodEnd - floodStart).count() / rounds;
    const double articMs =
        std::chrono::duration<double, std::milli>(articEnd - articStart).count() / rounds;
    std::printf("  격자 전수 질의(%dx%d = %d칸) 1회 — floodfill %.2fms, artic %.3fms, %.0fx\n",
                source.Columns(), source.Rows(), source.Columns() * source.Rows(),
                floodMs, articMs, articMs > 0 ? floodMs / articMs : 0.0);
}

void RunTests() {
    TestRandomTopologiesAgainstBruteForce();
    TestVerdictsAgreeOnGeneratedMaps();
    TestVerdictsAgreeAfterTerrainChanges();
    TestExceptionRulesMatch();
    TestArmsProduceIdenticalWorlds();
    TestDisconnectedGridRejectsEverything();
    MeasureQueryCost();
}

}  // namespace

TEST_MAIN("build_connectivity")
