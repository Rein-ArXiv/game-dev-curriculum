// flow_field — 군집 경로. 한 번의 적분으로 모든 셀이 목표로 가는 방향을 갖는다.
//
// 한 목표를 향한 수천 개체를 개별 A* 로 풀면 매 프레임 수십 ms 다. Flow Field 는
// 목표에서 Dijkstra 로 *적분 필드*(각 셀→목표 최단 비용)를 한 번 만들고, 거기서
// *벡터 필드*(각 셀이 비용이 낮아지는 이웃을 가리킴)를 뽑는다. 개체는 자기 셀의
// 벡터를 lookup 만 하면 돼, 수천 개체가 같은 목표로 갈 때 두 자릿수 배 빠르다
// (ai 시리즈 Flow Field 챕터).

#pragma once

#include "ai/pathfinding/grid_map.hpp"
#include "math/vec.hpp"

#include <cmath>
#include <queue>
#include <vector>

namespace mega {
namespace ai {
namespace path {

class FlowField {
public:
    static constexpr float kInf = 1e30f;

    void Build(const GridMap& m, int gx, int gy) {
        w_ = m.Width(); h_ = m.Height();
        cost_.assign((size_t)w_ * h_, kInf);
        dir_.assign((size_t)w_ * h_, math::Vec2{0, 0});
        if (!m.Walkable(gx, gy)) return;

        const float kSqrt2 = 1.41421356f;
        static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};

        // 1) 적분 필드: 목표에서 Dijkstra.
        int goal = m.Index(gx, gy);
        cost_[goal] = 0;
        using PQ = std::pair<float, int>;
        std::priority_queue<PQ, std::vector<PQ>, std::greater<PQ>> open;
        open.push({0.0f, goal});
        while (!open.empty()) {
            auto [c, cur] = open.top(); open.pop();
            if (c > cost_[cur]) continue;
            int cx = cur % w_, cy = cur / w_;
            for (int d = 0; d < 8; ++d) {
                int nx = cx + dx[d], ny = cy + dy[d];
                if (!m.Walkable(nx, ny)) continue;
                if (dx[d] != 0 && dy[d] != 0)
                    if (!m.Walkable(cx + dx[d], cy) || !m.Walkable(cx, cy + dy[d])) continue;
                float step = (dx[d] != 0 && dy[d] != 0) ? kSqrt2 : 1.0f;
                int ni = m.Index(nx, ny);
                float nc = cost_[cur] + step;
                if (nc < cost_[ni]) { cost_[ni] = nc; open.push({nc, ni}); }
            }
        }

        // 2) 벡터 필드: 비용이 가장 낮아지는 이웃을 가리킨다.
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x) {
                int idx = m.Index(x, y);
                if (cost_[idx] >= kInf || idx == goal) continue;
                float best = cost_[idx];
                int bx = x, by = y;
                for (int d = 0; d < 8; ++d) {
                    int nx = x + dx[d], ny = y + dy[d];
                    if (!m.Walkable(nx, ny)) continue;
                    if (dx[d] != 0 && dy[d] != 0)
                        if (!m.Walkable(x + dx[d], y) || !m.Walkable(x, y + dy[d])) continue;
                    float c = cost_[m.Index(nx, ny)];
                    if (c < best) { best = c; bx = nx; by = ny; }
                }
                dir_[idx] = math::Normalize(math::Vec2{(float)(bx - x), (float)(by - y)});
            }
    }

    math::Vec2 Direction(int x, int y) const {
        if (x < 0 || x >= w_ || y < 0 || y >= h_) return {0, 0};
        return dir_[(size_t)y * w_ + x];
    }
    float Cost(int x, int y) const {
        if (x < 0 || x >= w_ || y < 0 || y >= h_) return kInf;
        return cost_[(size_t)y * w_ + x];
    }
    bool Reachable(int x, int y) const { return Cost(x, y) < kInf; }

private:
    int w_ = 0, h_ = 0;
    std::vector<float>      cost_;
    std::vector<math::Vec2> dir_;
};

}  // namespace path
}  // namespace ai
}  // namespace mega
