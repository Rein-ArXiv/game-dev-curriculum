#include "grid/grid.h"
#include "pathfind/pathfinder.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':'  \
                      << __LINE__ << ")\n";                                   \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

// 실패 메시지에 맥락(맵 이름 · 알고리즘)을 붙인다. 200 맵 property test 에서
// 어느 케이스가 깨졌는지 알 수 없으면 진단이 불가능하다.
#define CHECK_CTX(expr, ctx)                                                  \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::cerr << "CHECK failed: " #expr << " [" << ctx << "] ("       \
                      << __FILE__ << ':' << __LINE__ << ")\n";                \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

bool Near(float a, float b, float epsilon = 1e-4f) {
    return std::fabs(a - b) <= epsilon;
}

// 이 lab 의 이동 비용 모델 (step02 "공정한 비교 조건" 1번).
constexpr float kDiagonalCost = 1.41421356f;

const char* Name(ailab::Algo algorithm) {
    switch (algorithm) {
        case ailab::Algo::AStar:    return "A*";
        case ailab::Algo::Dijkstra: return "Dijkstra";
        case ailab::Algo::Greedy:   return "Greedy";
        default:                    return "JPS";
    }
}

constexpr ailab::Algo kAllAlgorithms[4] = {
    ailab::Algo::Dijkstra, ailab::Algo::AStar, ailab::Algo::Greedy, ailab::Algo::JPS};

// Pathfinder 객체를 그대로 노출한다 — Path() 내용을 검사하려면 Stats() 만으로는
// 부족하다.
void SolveInto(const ailab::Grid& grid,
               ailab::Algo algorithm,
               ailab::Heur heuristic,
               ailab::Pathfinder& finder) {
    finder.Reset(grid, algorithm, heuristic);
    finder.RunToCompletion(grid);
}

ailab::PathStats Solve(const ailab::Grid& grid,
                       ailab::Algo algorithm,
                       ailab::Heur heuristic) {
    ailab::Pathfinder finder;
    SolveInto(grid, algorithm, heuristic, finder);
    return finder.Stats();
}

// Path() 자체의 계약: 시작·끝 셀, 인접성, 통행 가능성, 그리고 경로를 따라
// 다시 더한 비용이 보고된 pathCost 와 같은가.
void CheckPathShape(const ailab::Grid& grid,
                    ailab::Algo algorithm,
                    ailab::Heur heuristic,
                    const char* mapName) {
    ailab::Pathfinder finder;
    SolveInto(grid, algorithm, heuristic, finder);
    const ailab::PathStats& stats = finder.Stats();
    const std::vector<ailab::Cell>& path = finder.Path();

    std::string context = std::string(mapName) + "/" + Name(algorithm);
    CHECK_CTX(stats.done, context);
    if (!stats.found) {
        CHECK_CTX(path.empty(), context);
        CHECK_CTX(stats.pathLen == 0, context);
        return;
    }

    CHECK_CTX(!path.empty(), context);
    if (path.empty()) return;
    CHECK_CTX((int)path.size() == stats.pathLen, context);
    CHECK_CTX(path.front() == grid.Start(), context);
    CHECK_CTX(path.back() == grid.Goal(), context);

    float traced = 0.0f;
    for (size_t i = 0; i < path.size(); ++i) {
        CHECK_CTX(grid.Walkable(path[i].x, path[i].y), context);
        if (i == 0) continue;
        const int dx = std::abs(path[i].x - path[i - 1].x);
        const int dy = std::abs(path[i].y - path[i - 1].y);
        CHECK_CTX(dx <= 1 && dy <= 1, context);   // 연속 셀은 8-인접
        CHECK_CTX(dx + dy > 0, context);          // 제자리 반복 금지
        traced += (dx != 0 && dy != 0) ? kDiagonalCost : 1.0f;
    }
    CHECK_CTX(Near(traced, stats.pathCost, 1e-3f), context);
}

void CheckAllPathShapes(const ailab::Grid& grid, const char* mapName) {
    for (ailab::Algo algorithm : kAllAlgorithms)
        CheckPathShape(grid, algorithm, ailab::Heur::Octile, mapName);
}

void CheckOptimalAlgorithms(const ailab::Grid& grid) {
    const auto dijkstra = Solve(grid, ailab::Algo::Dijkstra, ailab::Heur::Octile);
    const auto astar = Solve(grid, ailab::Algo::AStar, ailab::Heur::Octile);
    const auto jps = Solve(grid, ailab::Algo::JPS, ailab::Heur::Octile);

    CHECK(dijkstra.done && astar.done && jps.done);
    CHECK(dijkstra.found == astar.found);
    CHECK(dijkstra.found == jps.found);
    if (dijkstra.found) {
        CHECK(Near(dijkstra.pathCost, astar.pathCost));
        CHECK(Near(dijkstra.pathCost, jps.pathCost));
        CHECK(dijkstra.pathLen > 0);
        CHECK(astar.pathLen > 0);
        CHECK(jps.pathLen > 0);
    }
}

// 32x20, x=15 수직벽에 y=4·y=15 두 구멍. 수평·수직 forced neighbor 만 자극한다.
ailab::Grid MakeForcedNeighborMap() {
    ailab::Grid grid;
    grid.Init(32, 20);
    for (int y = 0; y < grid.Height(); ++y)
        if (y != 4 && y != 15) grid.SetWall(15, y, true);
    return grid;
}

// 대각 forced neighbor 가 필수인 맵.
//
// 16x10, start(2,5) → goal(13,5). x=5 열이 y=1 한 칸만 남기고 막혀 있어
// 최단 경로는 (5,1) 구멍까지 대각으로 올라갔다가 다시 대각으로 내려와야 한다.
// (7,1) 의 벽 한 칸이 구멍 바로 뒤 코너에 붙어 있어, 구멍을 대각으로 빠져나온
// 노드에서 대각 forced neighbor 가 발생한다. PrunedDirs 의 대각 분기 두 줄
// (`if (!g.Walkable(c.x - dx, c.y)) add(-dx, dy);` / `... add(dx, -dy);`) 을
// 지우면 JPS 가 길은 찾지만 조용히 비최적이 된다 (14.89949 → 15.72792).
ailab::Grid MakeDiagonalForcedMap() {
    ailab::Grid grid;
    grid.Init(16, 10);
    for (int y = 0; y < grid.Height(); ++y)
        if (y != 1) grid.SetWall(5, y, true);
    grid.SetWall(7, 1, true);
    return grid;
}

void TestOpenGrid() {
    ailab::Grid grid;
    grid.Init(24, 16);
    CheckOptimalAlgorithms(grid);
    CheckAllPathShapes(grid, "open 24x16");

    const auto astar = Solve(grid, ailab::Algo::AStar, ailab::Heur::Euclidean);
    const auto chebyshev =
        Solve(grid, ailab::Algo::AStar, ailab::Heur::Chebyshev);
    const auto dijkstra =
        Solve(grid, ailab::Algo::Dijkstra, ailab::Heur::Octile);
    CHECK(Near(astar.pathCost, dijkstra.pathCost));
    CHECK(Near(chebyshev.pathCost, dijkstra.pathCost));
}

void TestForcedNeighbors() {
    const ailab::Grid grid = MakeForcedNeighborMap();
    CheckOptimalAlgorithms(grid);
    CheckAllPathShapes(grid, "forced 32x20");
}

// 항목 1(a) — 대각 forced neighbor 를 강제하는 고정 맵.
void TestDiagonalForcedNeighbors() {
    const ailab::Grid grid = MakeDiagonalForcedMap();
    CheckOptimalAlgorithms(grid);
    CheckAllPathShapes(grid, "diag-forced 16x10");

    const auto dijkstra = Solve(grid, ailab::Algo::Dijkstra, ailab::Heur::Octile);
    const auto jps = Solve(grid, ailab::Algo::JPS, ailab::Heur::Octile);
    CHECK(dijkstra.found && jps.found);
    // 실측 최단 비용 = 7√2 + 5 = 14.89949 (대각 7 회 + 직선 5 회).
    CHECK(Near(dijkstra.pathCost, 7.0f * kDiagonalCost + 5.0f, 1e-3f));
    CHECK(Near(jps.pathCost, dijkstra.pathCost, 1e-3f));
}

// 항목 1(b) — 시드 고정 랜덤 맵 200 개에서 JPS ≡ Dijkstra.
void TestJpsMatchesDijkstraOnRandomMaps() {
    int reachable = 0;
    for (unsigned seed = 1; seed <= 200; ++seed) {
        ailab::Grid grid;
        grid.Init(30, 22);
        grid.RandomWalls(seed, 0.28f);

        const auto dijkstra = Solve(grid, ailab::Algo::Dijkstra, ailab::Heur::Octile);
        const auto jps = Solve(grid, ailab::Algo::JPS, ailab::Heur::Octile);
        const std::string context = "random seed " + std::to_string(seed);

        CHECK_CTX(dijkstra.done && jps.done, context);
        CHECK_CTX(dijkstra.found == jps.found, context);
        if (!dijkstra.found) continue;
        ++reachable;
        CHECK_CTX(Near(dijkstra.pathCost, jps.pathCost, 1e-3f), context);

        if (seed % 10 == 0) {
            const std::string name = "random seed " + std::to_string(seed);
            for (ailab::Algo algorithm : kAllAlgorithms)
                CheckPathShape(grid, algorithm, ailab::Heur::Octile, name.c_str());
        }
    }
    // 전부 도달 불가라면 위 비교가 공허하다.
    CHECK(reachable > 150);
}

// 항목 3 — 8방향 비용 모델의 절대값. kDiag 를 1.0 으로 바꾸면 죽는다.
void TestAbsoluteCostModel() {
    ailab::Grid grid;
    grid.Init(24, 16);
    // 기본 start(2,8) → goal(21,8) 은 순수 수평 19 칸.
    for (ailab::Algo algorithm : kAllAlgorithms) {
        const auto stats = Solve(grid, algorithm, ailab::Heur::Octile);
        CHECK_CTX(stats.found, Name(algorithm));
        CHECK_CTX(Near(stats.pathCost, 19.0f, 1e-4f), Name(algorithm));
        CHECK_CTX(stats.pathLen == 20, Name(algorithm));
    }

    // goal 을 (21,15) 로 옮기면 대각 7 + 직선 12 가 강제된다.
    grid.SetGoal(ailab::Cell{21, 15});
    const float expected = 7.0f * kDiagonalCost + 12.0f;  // 21.899494
    for (ailab::Algo algorithm : kAllAlgorithms) {
        const auto stats = Solve(grid, algorithm, ailab::Heur::Octile);
        CHECK_CTX(stats.found, Name(algorithm));
        CHECK_CTX(Near(stats.pathCost, expected, 1e-4f), Name(algorithm));
        CHECK_CTX(stats.pathLen == 20, Name(algorithm));
    }
    CheckAllPathShapes(grid, "diagonal goal 24x16");

    // 한 칸 대각 이동의 비용 자체를 못 박는다.
    ailab::Grid unit;
    unit.Init(6, 6);
    unit.SetStart(ailab::Cell{1, 1});
    unit.SetGoal(ailab::Cell{2, 2});
    for (ailab::Algo algorithm : kAllAlgorithms) {
        const auto stats = Solve(unit, algorithm, ailab::Heur::Octile);
        CHECK_CTX(stats.found, Name(algorithm));
        CHECK_CTX(Near(stats.pathCost, kDiagonalCost, 1e-5f), Name(algorithm));
        CHECK_CTX(stats.pathLen == 2, Name(algorithm));
    }
}

// 항목 5 — README 가 대표 측정치로 내건 expanded / maxOpen 의 부등식 불변식.
void TestSearchEffort() {
    {
        ailab::Grid grid;
        grid.Init(48, 48);
        const auto dijkstra = Solve(grid, ailab::Algo::Dijkstra, ailab::Heur::Octile);
        const auto astar = Solve(grid, ailab::Algo::AStar, ailab::Heur::Octile);
        const auto jps = Solve(grid, ailab::Algo::JPS, ailab::Heur::Octile);

        CHECK(jps.expanded < astar.expanded);
        CHECK(astar.expanded <= dijkstra.expanded);
        CHECK(jps.expanded > 0);
        CHECK(jps.maxOpen > 0);
        CHECK(astar.maxOpen > 0);
        CHECK(dijkstra.maxOpen > 0);
        // 절대 규모 — 카운터를 지우거나 상수로 고정하면 여기서 죽는다.
        CHECK(dijkstra.expanded > 1000);
        CHECK(astar.expanded < 200);
        CHECK(jps.expanded < 10);
    }
    {
        const ailab::Grid grid = MakeForcedNeighborMap();
        const auto dijkstra = Solve(grid, ailab::Algo::Dijkstra, ailab::Heur::Octile);
        const auto astar = Solve(grid, ailab::Algo::AStar, ailab::Heur::Octile);
        const auto jps = Solve(grid, ailab::Algo::JPS, ailab::Heur::Octile);

        CHECK(jps.expanded < astar.expanded);
        CHECK(astar.expanded <= dijkstra.expanded);
        CHECK(jps.expanded > 0);
        CHECK(jps.maxOpen > 0);
        CHECK(dijkstra.expanded > 400);
        CHECK(astar.expanded > 100);
        CHECK(jps.expanded < 20);
        // expand 한 노드보다 open 최대 크기가 클 수는 없다 — Dijkstra 기준.
        CHECK(dijkstra.maxOpen <= dijkstra.expanded);
    }
}

// 항목 6 — Greedy 와 Manhattan 의 비최적성을 이론 명제 그대로 고정한다.
void TestGreedyAndManhattanSuboptimality() {
    {
        // GUI 의 Random walls 와 같은 시드. 실측: Dijkstra 46.31371,
        // Greedy(Octile) 49.62742.
        ailab::Grid grid;
        grid.Init(48, 48);
        grid.RandomWalls(1385u, 0.25f);
        const auto dijkstra = Solve(grid, ailab::Algo::Dijkstra, ailab::Heur::Octile);
        const auto greedy = Solve(grid, ailab::Algo::Greedy, ailab::Heur::Octile);
        CHECK(dijkstra.found && greedy.found);
        CHECK(greedy.pathCost > dijkstra.pathCost + 1e-3f);
        CHECK(Near(dijkstra.pathCost, 46.31371f, 1e-3f));
        CHECK(Near(greedy.pathCost, 49.62742f, 1e-3f));
        CheckPathShape(grid, ailab::Algo::Greedy, ailab::Heur::Octile, "greedy 48x48");
    }
    {
        // 빈 격자에서는 Greedy 도 최적. 요구는 '더 나쁘지 않다' 뿐이다.
        ailab::Grid grid;
        grid.Init(24, 16);
        const auto dijkstra = Solve(grid, ailab::Algo::Dijkstra, ailab::Heur::Octile);
        const auto greedy = Solve(grid, ailab::Algo::Greedy, ailab::Heur::Octile);
        CHECK(greedy.found);
        CHECK(greedy.pathCost >= dijkstra.pathCost - 1e-4f);
        CHECK(Near(greedy.pathCost, dijkstra.pathCost, 1e-4f));
    }
    {
        // Manhattan 은 대각 이동을 과대평가해 admissible 하지 않다.
        // 실측: Dijkstra(Octile) 29.14213, A*(Manhattan) 29.97056.
        ailab::Grid grid;
        grid.Init(30, 22);
        grid.RandomWalls(7u, 0.28f);
        const auto dijkstra = Solve(grid, ailab::Algo::Dijkstra, ailab::Heur::Octile);
        const auto manhattan = Solve(grid, ailab::Algo::AStar, ailab::Heur::Manhattan);
        const auto octile = Solve(grid, ailab::Algo::AStar, ailab::Heur::Octile);
        CHECK(dijkstra.found && manhattan.found && octile.found);
        CHECK(manhattan.pathCost > dijkstra.pathCost + 1e-3f);
        CHECK(Near(octile.pathCost, dijkstra.pathCost, 1e-3f));
        CHECK(Near(dijkstra.pathCost, 29.14213f, 1e-3f));
        CHECK(Near(manhattan.pathCost, 29.97056f, 1e-3f));
        // 비최적이어도 경로 자체는 유효해야 한다.
        CheckPathShape(grid, ailab::Algo::AStar, ailab::Heur::Manhattan,
                       "manhattan 30x22");
    }
}

void TestUnreachable() {
    ailab::Grid grid;
    grid.Init(20, 12);
    for (int y = 0; y < grid.Height(); ++y) grid.SetWall(10, y, true);

    for (ailab::Algo algorithm : kAllAlgorithms) {
        const auto result = Solve(grid, algorithm, ailab::Heur::Octile);
        CHECK_CTX(result.done, Name(algorithm));
        CHECK_CTX(!result.found, Name(algorithm));
        CHECK_CTX(result.pathLen == 0, Name(algorithm));
        CHECK_CTX(result.expanded > 0, Name(algorithm));
    }
    CheckAllPathShapes(grid, "unreachable 20x12");
}

void TestStartEqualsGoal() {
    ailab::Grid grid;
    grid.Init(8, 8);
    grid.SetGoal(grid.Start());
    const auto result = Solve(grid, ailab::Algo::AStar, ailab::Heur::Octile);
    CHECK(result.done && result.found);
    CHECK(Near(result.pathCost, 0.0f));
    CHECK(result.pathLen == 1);
    CheckAllPathShapes(grid, "start==goal 8x8");
}

}  // namespace

int main() {
    TestOpenGrid();
    TestForcedNeighbors();
    TestDiagonalForcedNeighbors();
    TestJpsMatchesDijkstraOnRandomMaps();
    TestAbsoluteCostModel();
    TestSearchEffort();
    TestGreedyAndManhattanSuboptimality();
    TestUnreachable();
    TestStartEqualsGoal();
    if (failures != 0) {
        std::cerr << failures << " pathfinder test(s) failed\n";
        return 1;
    }
    std::cout << "pathfinder tests passed\n";
    return 0;
}
