// 경로 탐색 검증: GridMap 파싱, A*, Flow Field.
#include "ai/pathfinding/astar.hpp"
#include "ai/pathfinding/flow_field.hpp"
#include "ai/pathfinding/grid_map.hpp"
#include "test_util.h"

#include <vector>

using namespace mega::ai::path;

static void TestParse() {
    GridMap m;
    m.ParseAscii({
        "S....",
        ".###.",
        ".....",
        ".###.",
        "....G",
    });
    CHECK_EQ(m.Width(), 5);
    CHECK_EQ(m.Height(), 5);
    CHECK_EQ(m.StartX(), 0); CHECK_EQ(m.StartY(), 0);
    CHECK_EQ(m.GoalX(), 4);  CHECK_EQ(m.GoalY(), 4);
    CHECK(m.Walkable(0, 0));
    CHECK(!m.Walkable(1, 1));      // '#'
    CHECK(m.Walkable(0, 2));
}

static void TestAStarOpen() {
    GridMap m;
    m.Init(10, 10, true);          // 빈 격자
    std::vector<int> path;
    CHECK(AStar::FindPath(m, 0, 0, 9, 9, path));
    CHECK(!path.empty());
    CHECK_EQ(path.front(), m.Index(0, 0));
    CHECK_EQ(path.back(), m.Index(9, 9));
    // 빈 격자에서 (0,0)→(9,9) 는 대각 9칸 → 경로 10셀(시작 포함).
    CHECK_EQ((int)path.size(), 10);
}

static void TestAStarMaze() {
    GridMap m;
    m.ParseAscii({
        "S....",
        "####.",
        "....." ,
        ".####",
        "....G",
    });
    std::vector<int> path;
    CHECK(AStar::FindPath(m, m.StartX(), m.StartY(), m.GoalX(), m.GoalY(), path));
    CHECK_EQ(path.front(), m.Index(0, 0));
    CHECK_EQ(path.back(), m.Index(4, 4));
    // 경로의 모든 셀이 통행 가능.
    bool allWalk = true;
    for (int c : path) if (m.Walkable(c % m.Width(), c / m.Width()) == false) allWalk = false;
    CHECK(allWalk);
}

static void TestAStarBlocked() {
    GridMap m;
    m.ParseAscii({
        "S#G",
        ".#.",
        ".#.",
    });
    std::vector<int> path;
    CHECK(!AStar::FindPath(m, 0, 0, 2, 0, path));   // 벽으로 완전 분리 → 실패
}

static void TestFlowField() {
    GridMap m;
    m.Init(8, 8, true);
    FlowField ff;
    ff.Build(m, 7, 7);             // 목표 (7,7)

    CHECK_EQ(ff.Cost(7, 7), 0.0f); // 목표 비용 0
    CHECK(ff.Cost(0, 0) > 0.0f);   // 멀수록 큼
    CHECK(ff.Cost(0, 0) > ff.Cost(3, 3));
    CHECK(ff.Reachable(0, 0));

    // 벡터 필드를 따라가면 목표에 도달한다.
    int x = 0, y = 0, steps = 0;
    while (!(x == 7 && y == 7) && steps < 100) {
        mega::math::Vec2 d = ff.Direction(x, y);
        int nx = x + (d.x > 0.3f ? 1 : (d.x < -0.3f ? -1 : 0));
        int ny = y + (d.y > 0.3f ? 1 : (d.y < -0.3f ? -1 : 0));
        if (nx == x && ny == y) break;     // 막힘(없어야 함)
        x = nx; y = ny; ++steps;
    }
    CHECK(x == 7 && y == 7);       // 도달
}

static void TestFlowFieldUnreachable() {
    GridMap m;
    m.ParseAscii({
        "G.#..",
        "..#..",
        "..#..",
        "..#..",
        "..#.X",   // 오른쪽 영역은 벽으로 분리
    });
    FlowField ff;
    ff.Build(m, 0, 0);
    CHECK(ff.Reachable(0, 0));
    CHECK(ff.Reachable(1, 1));
    CHECK(!ff.Reachable(4, 4));     // 벽 너머 도달 불가
    CHECK(!ff.Reachable(3, 0));
}

static void RunTests() {
    TestParse();
    TestAStarOpen();
    TestAStarMaze();
    TestAStarBlocked();
    TestFlowField();
    TestFlowFieldUnreachable();
}

TEST_MAIN("ai_pathfinding")
