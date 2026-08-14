// Flow field 의 integration field 생성 — goal 기점 8방향 Dijkstra.
//
// FlowFieldScene::Rebuild 가 이 함수로 필드를 만들고, 국소 정책은
// model/ai_model.h 의 SelectFlowDirection 이 담당한다. 두 단계를 모두
// raylib 비의존 함수로 두어야 헤드리스 테스트가 가능하다.
//
// 비용 모델은 pathfind/pathfinder.h 와 같다: 직선 1, 대각 √2. 도달 불가
// 셀(벽으로 막힌 영역, 벽 자신)은 +infinity 로 남는다.

#pragma once

#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace ailab {

constexpr float kFlowDiagonalCost = 1.41421356f;

template <typename Walkable>
std::vector<float> BuildIntegrationField(int width, int height,
                                         Walkable&& walkable,
                                         int goalX, int goalY) {
    constexpr float kUnreachable = std::numeric_limits<float>::infinity();
    if (width <= 0 || height <= 0) return {};

    std::vector<float> field((size_t)(width * height), kUnreachable);
    if (goalX < 0 || goalY < 0 || goalX >= width || goalY >= height) return field;

    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    const int goalIndex = goalY * width + goalX;
    field[(size_t)goalIndex] = 0.0f;
    open.push({0.0f, goalIndex});

    static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (!open.empty()) {
        const auto [d, idx] = open.top();
        open.pop();
        if (d > field[(size_t)idx]) continue;
        const int x = idx % width, y = idx / width;
        for (int k = 0; k < 8; ++k) {
            const int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
            if (!walkable(nx, ny)) continue;
            const float step = (dx[k] && dy[k]) ? kFlowDiagonalCost : 1.0f;
            const float nd = d + step;
            const int ni = ny * width + nx;
            if (nd < field[(size_t)ni]) {
                field[(size_t)ni] = nd;
                open.push({nd, ni});
            }
        }
    }
    return field;
}

}  // namespace ailab
