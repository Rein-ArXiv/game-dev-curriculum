// paths.hpp — 최단 경로와 휴리스틱.
//
// 각 알고리즘의 *보장* 이 간선 비용의 성질에서 나온다. 그래서 이 헤더의 검사는
// "빠른가" 가 아니라 "그 성질이 깨졌을 때 무엇이 무너지는가" 를 본다.
//   BFS          모든 간선 비용이 같다
//   0-1 BFS      비용이 0 또는 1이다
//   DAG relax    순환이 없다
//   Dijkstra     비용이 음이 아니다  → 확정된 정점은 다시 좋아지지 않는다
//   Bellman-Ford 임의 비용. 음수 순환이면 최단 경로가 정의되지 않는다는 것까지 답한다
//   A*           휴리스틱이 admissible 하면 최적, consistent 하면 재개방이 필요 없다
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

#include "algo/graph.hpp"
#include "algo/range.hpp"
#include "measure/counters.hpp"

namespace algo {

inline constexpr int64_t kInfCost = INT64_MAX / 4;

struct PathResult {
    std::vector<int64_t>  dist;
    std::vector<uint32_t> parent;
    size_t settled   = 0;   // 확정(pop 후 실제로 확장)한 정점 수
    size_t pushes    = 0;
    size_t stale_pops = 0;  // 이미 확정된 항목을 꺼내 버린 횟수
    size_t queue_peak = 0;
    bool   negative_cycle = false;
};

inline PathResult MakeResult(size_t n) {
    PathResult r;
    r.dist.assign(n, kInfCost);
    r.parent.resize(n);
    for (uint32_t v = 0; v < n; ++v) r.parent[v] = v;
    return r;
}

// snippet-begin: paths-zero-one
// 0-1 BFS. 비용 0 간선은 deque 앞에, 1 은 뒤에 넣는다. deque 안의 거리는 두
// 값만 가지고 그 순서가 유지되므로 Dijkstra 의 우선순위 큐를 대체할 수 있다.
inline PathResult ZeroOneBfs(const Csr& g, uint32_t source, measure::Counters* c = nullptr) {
    PathResult r = MakeResult(g.n);
    if (g.n == 0) return r;
    std::deque<uint32_t> dq;
    r.dist[source] = 0;
    dq.push_back(source);
    while (!dq.empty()) {
        if (dq.size() > r.queue_peak) r.queue_peak = dq.size();
        const uint32_t u = dq.front();
        dq.pop_front();
        ++r.settled;
        if (c) ++c->visited;
        for (size_t i = g.start[u]; i < g.start[u + 1]; ++i) {
            const uint32_t v = g.to[i];
            const int64_t w = g.weight[i];
            if (r.dist[u] + w >= r.dist[v]) continue;
            r.dist[v] = r.dist[u] + w;
            r.parent[v] = u;
            ++r.pushes;
            if (w == 0) dq.push_front(v); else dq.push_back(v);
        }
    }
    return r;
}
// snippet-end: paths-zero-one

// snippet-begin: paths-dag
// DAG 완화. 위상 순서로 한 번만 훑으면 끝난다 — 음수 간선이 있어도 된다.
// 순환이 있으면 위상 순서가 없으므로 이 알고리즘 자체가 정의되지 않는다.
inline PathResult DagRelax(const Csr& g, uint32_t source) {
    PathResult r = MakeResult(g.n);
    const TopoResult topo = TopoSortKahn(g);
    if (!topo.acyclic) { r.negative_cycle = false; r.settled = 0; return r; }
    r.dist[source] = 0;
    for (uint32_t u : topo.order) {
        if (r.dist[u] >= kInfCost) continue;
        ++r.settled;
        for (size_t i = g.start[u]; i < g.start[u + 1]; ++i) {
            const uint32_t v = g.to[i];
            if (r.dist[u] + g.weight[i] < r.dist[v]) {
                r.dist[v] = r.dist[u] + g.weight[i];
                r.parent[v] = u;
            }
        }
    }
    return r;
}
// snippet-end: paths-dag

// snippet-begin: paths-dijkstra
// Dijkstra(이진 힙). 두 손잡이를 연다.
//   skip_stale       꺼낸 항목이 낡았으면 버린다. 끄면 *정확성은 그대로이고*
//                    같은 정점을 여러 번 확장하기만 한다 — 답이 아니라 확장
//                    수로만 드러나는 결함이 있다는 것을 이 팔이 보인다.
//   final_on_settle  교과서 Dijkstra 의 '확정' 논리. 한 번 꺼낸 정점은 다시
//                    좋아질 수 없다고 *믿는다*. 그 믿음의 근거는 비용이 음이
//                    아니라는 것뿐이다 — 음수 간선 하나가 그 근거를 없앤다.
struct DijkstraOptions {
    bool skip_stale      = true;
    bool final_on_settle = true;
};

inline PathResult DijkstraHeap(const Csr& g, uint32_t source,
                               const DijkstraOptions& opt = DijkstraOptions{},
                               measure::Counters* c = nullptr) {
    PathResult r = MakeResult(g.n);
    if (g.n == 0) return r;
    BinaryHeap<uint32_t> heap;
    std::vector<uint8_t> done(g.n, 0);
    r.dist[source] = 0;
    heap.Push(0, source, 0);
    ++r.pushes;
    while (!heap.Empty()) {
        if (heap.Size() > r.queue_peak) r.queue_peak = heap.Size();
        const auto top = heap.Pop(c);
        const uint32_t u = top.value;
        if (top.key > r.dist[u]) {
            ++r.stale_pops;
            if (opt.skip_stale) continue;
        }
        if (opt.final_on_settle && done[u]) continue;
        done[u] = 1;
        ++r.settled;
        if (c) ++c->visited;
        for (size_t i = g.start[u]; i < g.start[u + 1]; ++i) {
            const uint32_t v = g.to[i];
            if (opt.final_on_settle && done[v]) continue;   // ← '확정' 을 믿는 줄
            const int64_t candidate = r.dist[u] + g.weight[i];
            if (candidate >= r.dist[v]) continue;
            r.dist[v] = candidate;
            r.parent[v] = u;
            heap.Push(candidate, v, 0);
            ++r.pushes;
        }
    }
    return r;
}

// 밀집 그래프용 O(V^2). 힙도 재삽입도 없다 — E 가 V^2 에 가까우면 이쪽이 이긴다.
inline PathResult DijkstraDense(const Csr& g, uint32_t source, measure::Counters* c = nullptr) {
    PathResult r = MakeResult(g.n);
    if (g.n == 0) return r;
    std::vector<uint8_t> done(g.n, 0);
    r.dist[source] = 0;
    for (size_t k = 0; k < g.n; ++k) {
        size_t pick = g.n;
        for (size_t v = 0; v < g.n; ++v)
            if (!done[v] && (pick == g.n || r.dist[v] < r.dist[pick])) pick = v;
        if (pick == g.n || r.dist[pick] >= kInfCost) break;
        done[pick] = 1;
        ++r.settled;
        if (c) ++c->visited;
        for (size_t i = g.start[pick]; i < g.start[pick + 1]; ++i) {
            const uint32_t v = g.to[i];
            const int64_t candidate = r.dist[pick] + g.weight[i];
            if (candidate < r.dist[v]) { r.dist[v] = candidate; r.parent[v] = uint32_t(pick); }
        }
    }
    return r;
}
// snippet-end: paths-dijkstra

// snippet-begin: paths-bellman-ford
// Bellman-Ford. n-1 라운드면 어떤 최단 경로도 확정된다(단순 경로의 간선 수 상한).
// n 번째 라운드에서 여전히 줄어드는 간선이 있으면 음수 순환이 그 경로 위에 있다.
// 이 알고리즘의 반환값은 거리만이 아니라 "최단 경로가 정의되는가" 라는 판정이다.
inline PathResult BellmanFord(size_t n, const std::vector<Csr::Edge>& edges,
                              uint32_t source, measure::Counters* c = nullptr) {
    PathResult r = MakeResult(n);
    if (n == 0) return r;
    r.dist[source] = 0;
    for (size_t round = 0; round + 1 < n; ++round) {
        bool changed = false;
        for (const Csr::Edge& e : edges) {
            if (c) ++c->visited;
            if (r.dist[e.u] >= kInfCost) continue;
            if (r.dist[e.u] + e.w < r.dist[e.v]) {
                r.dist[e.v] = r.dist[e.u] + e.w;
                r.parent[e.v] = e.u;
                changed = true;
            }
        }
        ++r.settled;
        if (!changed) break;   // 조기 종료. 최악은 여전히 n-1 라운드다.
    }
    for (const Csr::Edge& e : edges) {
        if (r.dist[e.u] >= kInfCost) continue;
        if (r.dist[e.u] + e.w < r.dist[e.v]) { r.negative_cycle = true; break; }
    }
    return r;
}
// snippet-end: paths-bellman-ford

// 전 쌍 최단 거리(Floyd-Warshall)는 여기 없다. 그것은 이 헤더를 검증하는
// *오라클* 이므로 제품 트리가 아니라 tests/oracle.h 에 둔다.

// ---------------------------------------------------------------------- A*

// snippet-begin: paths-astar-grid
// 격자 위의 A*. 휴리스틱과 가중치와 재개방 정책을 전부 토글로 연다 —
// 이 셋의 조합이 "최적성" 과 "확장 수" 사이의 교환을 그대로 만든다.
enum class Heuristic { Zero, Manhattan, Octile, Euclidean };

struct Grid {
    size_t w = 0, h = 0;
    std::vector<uint8_t> blocked;
    bool diagonal = false;

    bool Free(size_t x, size_t y) const { return x < w && y < h && !blocked[y * w + x]; }
    uint32_t Id(size_t x, size_t y) const { return uint32_t(y * w + x); }
    size_t X(uint32_t id) const { return id % w; }
    size_t Y(uint32_t id) const { return id / w; }
};

struct AStarOptions {
    Heuristic heuristic = Heuristic::Manhattan;
    double    weight    = 1.0;    // f = g + w*h. w>1 이면 최적성을 잃는 대신 빨라진다
    bool      reopen    = true;   // closed 정점을 더 좋은 g 로 다시 열 것인가
    int       tie_break = 0;      // 0: f 만 · 1: f 다음 h 작은 것 · 2: f 다음 g 큰 것
};

struct AStarResult {
    bool found = false;
    int64_t cost = 0;
    std::vector<uint32_t> path;
    size_t expanded = 0;      // pop 해서 실제로 확장한 횟수
    size_t pushes = 0;
    size_t reopened = 0;
    size_t queue_peak = 0;
};

// 대각 이동 비용을 정수로 유지하려고 직교 10, 대각 14(≈10*sqrt2)를 쓴다.
inline constexpr int64_t kOrtho = 10;
inline constexpr int64_t kDiag  = 14;

inline int64_t HeuristicValue(Heuristic kind, int64_t dx, int64_t dy) {
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    switch (kind) {
        case Heuristic::Zero:      return 0;
        case Heuristic::Manhattan: return kOrtho * (dx + dy);
        case Heuristic::Octile: {
            const int64_t lo = (dx < dy) ? dx : dy;
            const int64_t hi = (dx < dy) ? dy : dx;
            return kOrtho * (hi - lo) + kDiag * lo;
        }
        case Heuristic::Euclidean:
            return int64_t(double(kOrtho) * std::sqrt(double(dx * dx + dy * dy)));
    }
    return 0;
}

inline AStarResult AStarGrid(const Grid& grid, uint32_t start, uint32_t goal,
                             const AStarOptions& opt, measure::Counters* c = nullptr) {
    AStarResult r;
    const size_t n = grid.w * grid.h;
    std::vector<int64_t> g(n, kInfCost);
    std::vector<uint32_t> parent(n);
    std::vector<uint8_t> closed(n, 0);
    for (uint32_t v = 0; v < n; ++v) parent[v] = v;
    if (n == 0 || !grid.Free(grid.X(start), grid.Y(start))) return r;

    auto H = [&](uint32_t id) {
        return int64_t(opt.weight * double(HeuristicValue(
            opt.heuristic,
            int64_t(grid.X(goal)) - int64_t(grid.X(id)),
            int64_t(grid.Y(goal)) - int64_t(grid.Y(id)))));
    };

    // 우선순위 키를 하나의 정수로 접는다. tie-break 를 키 안에 넣어야 힙이
    // 그것을 실제로 존중한다 — 밖에서 정렬하면 힙 순서와 어긋난다.
    auto Key = [&](uint32_t id) -> int64_t {
        const int64_t f = g[id] + H(id);
        if (opt.tie_break == 1) return f * 1000000 + H(id);
        if (opt.tie_break == 2) return f * 1000000 - g[id];
        return f * 1000000;
    };

    BinaryHeap<uint32_t> open;
    g[start] = 0;
    open.Push(Key(start), start, 0);
    ++r.pushes;

    const int dx8[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    const int dy8[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    const int dirs = grid.diagonal ? 8 : 4;

    while (!open.Empty()) {
        if (open.Size() > r.queue_peak) r.queue_peak = open.Size();
        const auto top = open.Pop(c);
        const uint32_t u = top.value;
        if (closed[u] && !opt.reopen) continue;
        if (top.key > Key(u)) continue;                 // 낡은 항목
        closed[u] = 1;
        ++r.expanded;
        if (c) ++c->visited;
        if (u == goal) {
            r.found = true;
            r.cost = g[u];
            for (uint32_t at = goal;; at = parent[at]) {
                r.path.push_back(at);
                if (at == parent[at]) break;
            }
            std::reverse(r.path.begin(), r.path.end());
            return r;
        }
        const size_t ux = grid.X(u), uy = grid.Y(u);
        for (int k = 0; k < dirs; ++k) {
            const int64_t nx = int64_t(ux) + dx8[k];
            const int64_t ny = int64_t(uy) + dy8[k];
            if (nx < 0 || ny < 0) continue;
            if (!grid.Free(size_t(nx), size_t(ny))) continue;
            const uint32_t v = grid.Id(size_t(nx), size_t(ny));
            const int64_t step = (k < 4) ? kOrtho : kDiag;
            // 재개방 금지면 확정된 정점의 g 를 *건드리지도* 않는다. 여기서 값만
            // 고치고 큐에 넣지 않으면 보고 비용과 실제 경로가 어긋난다.
            if (closed[v] && !opt.reopen) continue;
            if (g[u] + step >= g[v]) continue;
            g[v] = g[u] + step;
            parent[v] = u;
            if (closed[v]) { closed[v] = 0; ++r.reopened; }
            open.Push(Key(v), v, 0);
            ++r.pushes;
        }
    }
    return r;
}
// snippet-end: paths-astar-grid

// snippet-begin: paths-astar-graph
// 임의 그래프 위의 A*. 휴리스틱을 배열로 직접 받는다 — 격자 거리 공식으로는
// admissible 인데 inconsistent 한 사례를 만들 수 없기 때문이다.
// (h 가 tight 하면 자동으로 consistent 해진다. 비일관을 보려면 어떤 정점에서
//  h 가 *느슨해야* 한다.)
struct AStarGraphResult {
    bool    found = false;
    int64_t cost = 0;
    size_t  expanded = 0;
    size_t  reopened = 0;
    std::vector<uint32_t> path;
};

inline AStarGraphResult AStarGraph(const Csr& g, uint32_t start, uint32_t goal,
                                   const std::vector<int64_t>& h, bool reopen) {
    AStarGraphResult r;
    if (g.n == 0) return r;
    std::vector<int64_t> dist(g.n, kInfCost);
    std::vector<uint32_t> parent(g.n);
    std::vector<uint8_t> closed(g.n, 0);
    for (uint32_t v = 0; v < g.n; ++v) parent[v] = v;

    BinaryHeap<uint32_t> open;
    dist[start] = 0;
    open.Push(h[start], start, 0);
    while (!open.Empty()) {
        const auto top = open.Pop();
        const uint32_t u = top.value;
        if (closed[u]) continue;
        if (top.key > dist[u] + h[u]) continue;
        closed[u] = 1;
        ++r.expanded;
        if (u == goal) {
            r.found = true;
            r.cost = dist[u];
            for (uint32_t at = goal;; at = parent[at]) {
                r.path.push_back(at);
                if (at == parent[at]) break;
            }
            std::reverse(r.path.begin(), r.path.end());
            return r;
        }
        for (size_t i = g.start[u]; i < g.start[u + 1]; ++i) {
            const uint32_t v = g.to[i];
            if (closed[v] && !reopen) continue;     // ← 재개방 정책이 사는 줄
            const int64_t candidate = dist[u] + g.weight[i];
            if (candidate >= dist[v]) continue;
            dist[v] = candidate;
            parent[v] = u;
            if (closed[v]) { closed[v] = 0; ++r.reopened; }
            open.Push(candidate + h[v], v, 0);
        }
    }
    return r;
}

// 일관성(단조성) 판정. 모든 간선 (u,v) 에서 h(u) <= cost(u,v) + h(v) 인가.
inline bool ConsistentOnGraph(const Csr& g, const std::vector<int64_t>& h) {
    for (uint32_t u = 0; u < g.n; ++u)
        for (size_t i = g.start[u]; i < g.start[u + 1]; ++i)
            if (h[u] > g.weight[i] + h[g.to[i]]) return false;
    return true;
}
// snippet-end: paths-astar-graph

// snippet-begin: paths-consistency
// consistency(단조성) 검사기. 모든 간선 (u,v) 에 대해 h(u) <= cost(u,v) + h(v).
// admissible 인데 inconsistent 인 휴리스틱은 최적해를 *잃지는* 않지만, closed
// 재개방을 끄면 잃는다. 이 검사기는 그 두 성질을 구분해 보고한다.
struct HeuristicAudit {
    bool admissible = true;    // h(v) <= 실제 최단 비용(v -> goal)
    bool consistent = true;
    size_t violations = 0;
};

inline HeuristicAudit AuditHeuristic(const Grid& grid, uint32_t goal,
                                     const std::vector<int64_t>& h,
                                     const std::vector<int64_t>& true_cost) {
    HeuristicAudit a;
    const int dx4[4] = {1, -1, 0, 0};
    const int dy4[4] = {0, 0, 1, -1};
    for (size_t y = 0; y < grid.h; ++y) {
        for (size_t x = 0; x < grid.w; ++x) {
            if (!grid.Free(x, y)) continue;
            const uint32_t u = grid.Id(x, y);
            if (true_cost[u] < kInfCost && h[u] > true_cost[u]) {
                a.admissible = false;
                ++a.violations;
            }
            for (int k = 0; k < 4; ++k) {
                const int64_t nx = int64_t(x) + dx4[k], ny = int64_t(y) + dy4[k];
                if (nx < 0 || ny < 0 || !grid.Free(size_t(nx), size_t(ny))) continue;
                const uint32_t v = grid.Id(size_t(nx), size_t(ny));
                if (h[u] > kOrtho + h[v]) { a.consistent = false; ++a.violations; }
            }
        }
    }
    if (h[goal] != 0) a.consistent = false;
    return a;
}
// snippet-end: paths-consistency

// 경로 유효성 검사기. 알고리즘이 낸 경로가 실제로 (a) 시작에서 끝나고
// (b) 인접 정점만 잇고 (c) 보고한 비용과 같은지 독립적으로 확인한다.
inline bool ValidateGridPath(const Grid& grid, const std::vector<uint32_t>& path,
                             uint32_t start, uint32_t goal, int64_t reported) {
    if (path.empty()) return false;
    if (path.front() != start || path.back() != goal) return false;
    int64_t total = 0;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const int64_t dx = int64_t(grid.X(path[i + 1])) - int64_t(grid.X(path[i]));
        const int64_t dy = int64_t(grid.Y(path[i + 1])) - int64_t(grid.Y(path[i]));
        const int64_t ax = (dx < 0) ? -dx : dx;
        const int64_t ay = (dy < 0) ? -dy : dy;
        if (ax > 1 || ay > 1 || (ax == 0 && ay == 0)) return false;
        if (!grid.diagonal && ax + ay != 1) return false;
        total += (ax + ay == 2) ? kDiag : kOrtho;
        if (!grid.Free(grid.X(path[i + 1]), grid.Y(path[i + 1]))) return false;
    }
    return total == reported;
}

}  // namespace algo
