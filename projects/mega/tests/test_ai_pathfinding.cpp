// 경로 탐색 검증: GridMap 파싱, A*, Flow Field.
#include "ai/pathfinding/astar.hpp"
#include "ai/pathfinding/flow_field.hpp"
#include "ai/pathfinding/grid_map.hpp"
#include "test_util.h"

#include <cmath>
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

// 통행 격자는 **칸 하나씩** 갱신된다. 타워 배치·발파·잔해 안착이 전부 그
// 연산 하나이므로, 정확히 그 칸만 바뀌는지가 격자의 기본 계약이다.
static void TestSetWalkableTouchesOneCell() {
    GridMap m;
    m.Init(5, 4, true);
    // 기준 상태를 뜬다.
    std::vector<int> before;
    for (int y = 0; y < m.Height(); ++y)
        for (int x = 0; x < m.Width(); ++x) before.push_back(m.Walkable(x, y) ? 1 : 0);

    m.SetWalkable(2, 1, false);
    int changed = 0;
    for (int y = 0; y < m.Height(); ++y)
        for (int x = 0; x < m.Width(); ++x) {
            const int now = m.Walkable(x, y) ? 1 : 0;
            if (now != before[(size_t)(y * m.Width() + x)]) {
                ++changed;
                CHECK_EQ(x, 2);
                CHECK_EQ(y, 1);
            }
        }
    CHECK_EQ(changed, 1);
    m.SetWalkable(2, 1, true);
    CHECK(m.Walkable(2, 1));
}

// 경계 밖 질의와 쓰기. Index(x, y) 는 y * w + x 라 x = -1 이 **앞 줄의 마지막
// 칸**을 가리킨다 — InBounds 검사가 없으면 경계 밖 쓰기가 멀쩡한 칸을 오염시킨다.
static void TestOutOfBoundsIsSafe() {
    GridMap m;
    m.Init(5, 4, true);
    CHECK(!m.Walkable(-1, 0));      // 밖은 통행 불가로 폴백
    CHECK(!m.Walkable(5, 0));
    CHECK(!m.Walkable(0, -1));
    CHECK(!m.Walkable(0, 4));

    m.SetWalkable(-1, 2, false);    // (4,1) 을 건드리면 안 된다
    CHECK(m.Walkable(4, 1));
    m.SetWalkable(5, 1, false);     // (0,2) 를 건드리면 안 된다
    CHECK(m.Walkable(0, 2));
    // 격자 전체가 그대로다.
    int walkable = 0;
    for (int y = 0; y < m.Height(); ++y)
        for (int x = 0; x < m.Width(); ++x) if (m.Walkable(x, y)) ++walkable;
    CHECK_EQ(walkable, 20);
}

// A* 경로의 비용과 플로우 필드 적분 비용은 같은 두 상수(1 · sqrt2)로 쌓인다.
static void TestPathCostMatchesField() {
    GridMap m;
    m.ParseAscii({
        "S....",
        ".##..",
        "..#..",
        "...#.",
        "....G",
    });
    std::vector<int> path;
    CHECK(AStar::FindPath(m, m.StartX(), m.StartY(), m.GoalX(), m.GoalY(), path));
    FlowField ff;
    ff.Build(m, m.GoalX(), m.GoalY());
    const float astar = AStar::PathCost(path, m.Width());
    const float field = ff.Cost(m.StartX(), m.StartY());
    CHECK(std::fabs(astar - field) < 1e-4f);
    // 한 칸짜리 경로의 비용은 0 이다(시작 = 목표).
    std::vector<int> single{m.Index(0, 0)};
    CHECK_EQ(AStar::PathCost(single, m.Width()), 0.0f);
}

static void RunTests() {
    TestParse();
    TestSetWalkableTouchesOneCell();
    TestOutOfBoundsIsSafe();
    TestPathCostMatchesField();
    TestAStarOpen();
    TestAStarMaze();
    TestAStarBlocked();
    TestFlowField();
    TestFlowFieldUnreachable();
}

TEST_MAIN("ai_pathfinding")
