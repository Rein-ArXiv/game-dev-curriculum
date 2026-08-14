// 길찾기 알고리즘 — A* / Dijkstra / Greedy / JPS 를 같은 그리드에서 step 단위로
// 실행해 비교한다. ai/part06~07 의 이론을 살아 있는 시각화로 만든다.
//
// 설계:
//   - Reset() 으로 알고리즘·휴리스틱을 정하고 start 를 open 에 넣는다.
//   - Step() 한 번에 노드 하나를 expand (JPS 는 jump point 하나). open/closed
//     상태와 통계를 갱신한다. 더 진행할 게 있으면 true.
//   - 완료 시 BuildPath() 가 came-from 을 역추적해 경로를 만든다.
//   - State(idx) / GScore(idx) 로 시각화가 open(파랑)/closed(회색)/path(노랑)
//     을 색칠한다.
//
// 이동 모델: 8방향, 대각 비용 √2, *코너 컷 허용*. 표준 JPS와
// A*/Dijkstra의 최단 비용 동등성은 이 비용 모델에서 admissible한
// Octile/Euclidean/Chebyshev 휴리스틱으로 검증한다.

#pragma once

#include "grid/grid.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace ailab {

enum class Algo { AStar, Dijkstra, Greedy, JPS };
enum class Heur { Manhattan, Euclidean, Octile, Chebyshev };

inline const char* AlgoNames()  { return "A*;Dijkstra;Greedy;JPS"; }
inline const char* HeurNames() { return "Manhattan;Euclidean;Octile;Chebyshev"; }

struct PathStats {
    int   expanded   = 0;     // closed 로 확정된 노드 수
    int   maxOpen    = 0;     // open list 최대 크기 (메모리 근사)
    float pathCost   = 0.0f;  // 최종 경로 비용
    int   pathLen    = 0;     // 경로 셀 수
    bool  found      = false;
    bool  done       = false;
};

class Pathfinder {
public:
    enum CellState : uint8_t { Unseen = 0, Open = 1, Closed = 2 };

    void Reset(const Grid& g, Algo algo, Heur heur) {
        algo_ = algo;
        heur_ = heur;
        int n = g.Width() * g.Height();
        gScore_.assign((size_t)n, kInf);
        state_.assign((size_t)n, Unseen);
        from_.assign((size_t)n, -1);
        open_ = OpenQueue();
        counter_  = 0;
        openLive_ = 0;
        stats_ = PathStats{};
        path_.clear();

        Cell s = g.Start();
        int si = g.Index(s.x, s.y);
        gScore_[(size_t)si] = 0.0f;
        state_[(size_t)si]  = Open;
        Push(g, si, s);
    }

    // 노드 하나 expand. 더 진행할 게 있으면 true, 끝나면 false.
    bool Step(const Grid& g) {
        if (stats_.done) return false;
        if (open_.empty()) { stats_.done = true; stats_.found = false; return false; }

        Node top = open_.top();
        open_.pop();
        if (openLive_ > 0) --openLive_;
        int ci = top.idx;
        if (state_[(size_t)ci] == Closed) return true;   // 낡은 항목
        state_[(size_t)ci] = Closed;
        ++stats_.expanded;

        Cell cur = top.cell;
        if (cur == g.Goal()) { Finish(g, ci); return false; }

        if (algo_ == Algo::JPS) ExpandJPS(g, cur, ci);
        else                    ExpandGrid(g, cur, ci);
        return true;
    }

    void RunToCompletion(const Grid& g) {
        int guard = g.Width() * g.Height() * 4 + 16;
        while (Step(g) && --guard > 0) {}
        if (!stats_.done) stats_.done = true;
    }

    CellState State(int idx) const { return (CellState)state_[(size_t)idx]; }
    float GScore(int idx) const    { return gScore_[(size_t)idx]; }
    const std::vector<Cell>& Path() const { return path_; }
    const PathStats& Stats() const { return stats_; }
    int OpenCount() const { return (int)openLive_; }

private:
    static constexpr float kInf  = std::numeric_limits<float>::infinity();
    static constexpr float kDiag = 1.41421356f;

    struct Node {
        float f;
        uint64_t seq;   // tie-break: 먼저 들어온 순
        int idx;
        Cell cell;
    };
    struct NodeCmp {
        bool operator()(const Node& a, const Node& b) const {
            if (a.f != b.f) return a.f > b.f;     // 작은 f 우선
            return a.seq > b.seq;                  // 동률이면 먼저 들어온 것
        }
    };
    using OpenQueue = std::priority_queue<Node, std::vector<Node>, NodeCmp>;

    float Heuristic(Cell a, Cell b) const {
        float dx = std::fabs((float)(a.x - b.x));
        float dy = std::fabs((float)(a.y - b.y));
        switch (heur_) {
            case Heur::Manhattan: return dx + dy;
            case Heur::Euclidean: return std::sqrt(dx * dx + dy * dy);
            case Heur::Chebyshev: return std::max(dx, dy);
            case Heur::Octile:
            default:              return std::max(dx, dy) + (kDiag - 1.0f) * std::min(dx, dy);
        }
    }

    // f 값 = g + (알고리즘별 가중). Dijkstra: h 무시. Greedy: g 무시.
    float Priority(int idx, Cell c, const Grid& g) const {
        float gv = gScore_[(size_t)idx];
        float hv = Heuristic(c, g.Goal());
        switch (algo_) {
            case Algo::Dijkstra: return gv;
            case Algo::Greedy:   return hv;
            case Algo::AStar:
            case Algo::JPS:
            default:             return gv + hv;
        }
    }

    void Push(const Grid& g, int idx, Cell c) {
        open_.push(Node{Priority(idx, c, g), counter_++, idx, c});
        ++openLive_;
        if ((int)openLive_ > stats_.maxOpen) stats_.maxOpen = (int)openLive_;
    }

    // 코너 컷 허용 8방향 이웃.
    void ExpandGrid(const Grid& g, Cell cur, int ci) {
        static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        for (int k = 0; k < 8; ++k) {
            int nx = cur.x + dx[k], ny = cur.y + dy[k];
            if (!g.Walkable(nx, ny)) continue;
            int ni = g.Index(nx, ny);
            if (state_[(size_t)ni] == Closed) continue;
            float step = (dx[k] != 0 && dy[k] != 0) ? kDiag : 1.0f;
            float ng = gScore_[(size_t)ci] + step;
            if (ng < gScore_[(size_t)ni]) {
                gScore_[(size_t)ni] = ng;
                from_[(size_t)ni]   = ci;
                state_[(size_t)ni]  = Open;
                Push(g, ni, Cell{nx, ny});
            }
        }
    }

    // JPS: jump point 후속만 open 에 넣는다.
    void ExpandJPS(const Grid& g, Cell cur, int ci) {
        for (auto [ddx, ddy] : PrunedDirs(g, cur, ci)) {
            Cell jp;
            if (!Jump(g, cur.x, cur.y, ddx, ddy, jp)) continue;
            int ji = g.Index(jp.x, jp.y);
            if (state_[(size_t)ji] == Closed) continue;
            float ng = gScore_[(size_t)ci] + Octile(cur, jp);
            if (ng < gScore_[(size_t)ji]) {
                gScore_[(size_t)ji] = ng;
                from_[(size_t)ji]   = ci;
                state_[(size_t)ji]  = Open;
                Push(g, ji, jp);
            }
        }
    }

    static float Octile(Cell a, Cell b) {
        float dx = std::fabs((float)(a.x - b.x));
        float dy = std::fabs((float)(a.y - b.y));
        return std::max(dx, dy) + (kDiag - 1.0f) * std::min(dx, dy);
    }

    // 부모로부터의 방향에 따른 pruned + forced 이웃 방향.
    std::vector<std::pair<int,int>> PrunedDirs(const Grid& g, Cell c, int ci) const {
        std::vector<std::pair<int,int>> dirs;
        int pf = from_[(size_t)ci];
        if (pf < 0) {  // start: 모든 방향
            static const int adx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
            static const int ady[8] = {0, 0, 1, -1, 1, -1, 1, -1};
            for (int k = 0; k < 8; ++k)
                if (g.Walkable(c.x + adx[k], c.y + ady[k]))
                    dirs.emplace_back(adx[k], ady[k]);
            return dirs;
        }
        int px = pf % g.Width(), py = pf / g.Width();
        int dx = Sign(c.x - px), dy = Sign(c.y - py);

        auto add = [&](int ax, int ay) {
            if (g.Walkable(c.x + ax, c.y + ay)) dirs.emplace_back(ax, ay);
        };
        if (dx != 0 && dy != 0) {            // 대각
            add(dx, dy); add(dx, 0); add(0, dy);
            if (!g.Walkable(c.x - dx, c.y)) add(-dx, dy);   // forced
            if (!g.Walkable(c.x, c.y - dy)) add(dx, -dy);
        } else if (dx != 0) {                // 수평
            add(dx, 0);
            if (!g.Walkable(c.x, c.y + 1)) add(dx, 1);
            if (!g.Walkable(c.x, c.y - 1)) add(dx, -1);
        } else {                             // 수직
            add(0, dy);
            if (!g.Walkable(c.x + 1, c.y)) add(1, dy);
            if (!g.Walkable(c.x - 1, c.y)) add(-1, dy);
        }
        return dirs;
    }

    // (cx,cy) 에서 (dx,dy) 방향으로 점프. jump point 를 찾으면 out 에 담고 true.
    bool Jump(const Grid& g, int cx, int cy, int dx, int dy, Cell& out) const {
        int nx = cx + dx, ny = cy + dy;
        if (!g.Walkable(nx, ny)) return false;
        if (Cell{nx, ny} == g.Goal()) { out = {nx, ny}; return true; }

        if (dx != 0 && dy != 0) {            // 대각
            if ((g.Walkable(nx - dx, ny + dy) && !g.Walkable(nx - dx, ny)) ||
                (g.Walkable(nx + dx, ny - dy) && !g.Walkable(nx, ny - dy))) {
                out = {nx, ny}; return true;
            }
            Cell tmp;
            if (Jump(g, nx, ny, dx, 0, tmp) || Jump(g, nx, ny, 0, dy, tmp)) {
                out = {nx, ny}; return true;
            }
        } else if (dx != 0) {                // 수평
            if ((g.Walkable(nx + dx, ny + 1) && !g.Walkable(nx, ny + 1)) ||
                (g.Walkable(nx + dx, ny - 1) && !g.Walkable(nx, ny - 1))) {
                out = {nx, ny}; return true;
            }
        } else {                             // 수직
            if ((g.Walkable(nx + 1, ny + dy) && !g.Walkable(nx + 1, ny)) ||
                (g.Walkable(nx - 1, ny + dy) && !g.Walkable(nx - 1, ny))) {
                out = {nx, ny}; return true;
            }
        }
        return Jump(g, nx, ny, dx, dy, out);
    }

    static int Sign(int v) { return (v > 0) - (v < 0); }

    void Finish(const Grid& g, int goalIdx) {
        stats_.done  = true;
        stats_.found = true;
        BuildPath(g, goalIdx);
        stats_.pathCost = gScore_[(size_t)goalIdx];
        stats_.pathLen  = (int)path_.size();
    }

    void BuildPath(const Grid& g, int goalIdx) {
        path_.clear();
        int cur = goalIdx;
        std::vector<int> rev;
        while (cur >= 0) { rev.push_back(cur); cur = from_[(size_t)cur]; }
        // JPS 는 jump point 사이가 비어 있으므로 직선 보간으로 셀을 채운다.
        for (int i = (int)rev.size() - 1; i >= 0; --i) {
            int idx = rev[(size_t)i];
            Cell c{idx % g.Width(), idx / g.Width()};
            if (!path_.empty() && algo_ == Algo::JPS) {
                Cell prev = path_.back();
                int sx = Sign(c.x - prev.x), sy = Sign(c.y - prev.y);
                Cell w = prev;
                while (w != c) { w.x += sx; w.y += sy; path_.push_back(w); }
            } else {
                path_.push_back(c);
            }
        }
    }

    Algo algo_ = Algo::AStar;
    Heur heur_ = Heur::Octile;
    std::vector<float>   gScore_;
    std::vector<uint8_t> state_;
    std::vector<int>     from_;
    OpenQueue            open_;
    uint64_t             counter_  = 0;
    size_t               openLive_ = 0;   // stale 항목을 포함한 실제 priority_queue 크기
    PathStats            stats_;
    std::vector<Cell>    path_;
};

}  // namespace ailab
