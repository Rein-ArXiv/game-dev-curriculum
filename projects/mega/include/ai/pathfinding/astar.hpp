// astar — 격자 위 A* 경로 탐색. 개체 단위 정밀 경로.
//
// f = g(시작부터 누적 비용) + h(목표까지 휴리스틱). 우선순위 큐에서 f 가 가장
// 작은 노드를 펴며, octile 휴리스틱(8방향에 admissible)이 최단 경로를 보장하면서
// 탐색 노드를 줄인다. 대각 이동은 모서리를 끼고 통과하지 못하게 막는다.
//
// 한 목표를 향한 수천 개체에는 Flow Field 가 낫지만(한 번의 적분으로 전체 커버),
// 개별 목표·동적 회피·정밀 경로에는 A* 가 맞는다(ai 시리즈 분기 기준).

#pragma once

#include "ai/pathfinding/grid_map.hpp"

#include <cmath>
#include <queue>
#include <vector>

namespace mega {
namespace ai {
namespace path {

class AStar {
public:
    // 시작→목표 경로를 셀 인덱스 배열로 반환(시작 포함, 목표 끝). 실패 시 false.
    static bool FindPath(const GridMap& m, int sx, int sy, int gx, int gy, std::vector<int>& out) {
        out.clear();
        if (!m.Walkable(sx, sy) || !m.Walkable(gx, gy)) return false;
        int n = m.Width() * m.Height();
        std::vector<float> g(n, kInf);
        std::vector<int>   came(n, -1);
        std::vector<uint8_t> closed(n, 0);

        const float kSqrt2 = 1.41421356f;
        int start = m.Index(sx, sy), goal = m.Index(gx, gy);
        g[start] = 0;

        using PQItem = std::pair<float, int>;   // (f, cell)
        std::priority_queue<PQItem, std::vector<PQItem>, std::greater<PQItem>> open;
        open.push({Heuristic(sx, sy, gx, gy), start});

        static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};

        while (!open.empty()) {
            int cur = open.top().second;
            open.pop();
            if (cur == goal) { Reconstruct(came, goal, out); return true; }
            if (closed[cur]) continue;
            closed[cur] = 1;

            int cx = cur % m.Width(), cy = cur / m.Width();
            for (int d = 0; d < 8; ++d) {
                int nx = cx + dx[d], ny = cy + dy[d];
                if (!m.Walkable(nx, ny)) continue;
                if (dx[d] != 0 && dy[d] != 0) {            // 대각: 모서리 컷 방지
                    if (!m.Walkable(cx + dx[d], cy) || !m.Walkable(cx, cy + dy[d])) continue;
                }
                int ni = m.Index(nx, ny);
                if (closed[ni]) continue;
                float step = (dx[d] != 0 && dy[d] != 0) ? kSqrt2 : 1.0f;
                float ng = g[cur] + step;
                if (ng < g[ni]) {
                    g[ni] = ng;
                    came[ni] = cur;
                    open.push({ng + Heuristic(nx, ny, gx, gy), ni});
                }
            }
        }
        return false;   // 도달 불가
    }

    // 셀 인덱스 열의 이동 비용 합. 대각 한 칸이 sqrt(2), 직교 한 칸이 1 이다 —
    // FindPath 가 g 를 쌓을 때 쓴 것과 **같은 두 상수**여야 한다. 이 함수가 있는
    // 이유는 A* 를 Flow Field 의 오라클로 쓰기 위해서다: 두 팔이 서로 다른
    // 최단 경로를 고를 수는 있어도 **비용은 같아야** 한다는 것이 유일하게
    // 성립하는 대조이고, 그 대조를 하려면 경로에서 비용을 다시 뽑을 수 있어야 한다.
    // snippet-begin: path-cost
    static float PathCost(const std::vector<int>& cells, int width) {
        const float kSqrt2 = 1.41421356f;
        float cost = 0;
        for (size_t i = 1; i < cells.size(); ++i) {
            const int px = cells[i - 1] % width, py = cells[i - 1] / width;
            const int cx = cells[i] % width,     cy = cells[i] / width;
            cost += (px != cx && py != cy) ? kSqrt2 : 1.0f;
        }
        return cost;
    }
    // snippet-end: path-cost

private:
    static constexpr float kInf = 1e30f;

    static float Heuristic(int x, int y, int gx, int gy) {
        float dx = (float)std::abs(x - gx), dy = (float)std::abs(y - gy);
        float mn = dx < dy ? dx : dy, mx = dx < dy ? dy : dx;
        return mx + (1.41421356f - 1.0f) * mn;     // octile
    }
    static void Reconstruct(const std::vector<int>& came, int goal, std::vector<int>& out) {
        for (int c = goal; c != -1; c = came[c]) out.push_back(c);
        for (size_t i = 0, j = out.size() - 1; i < j; ++i, --j) std::swap(out[i], out[j]);
    }
};

}  // namespace path
}  // namespace ai
}  // namespace mega
