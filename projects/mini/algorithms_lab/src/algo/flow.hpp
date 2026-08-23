// flow.hpp — 유량과 매칭.
//
// 이 헤더의 중심은 max-flow == min-cut 이다. 그것은 정리이기도 하지만, 실행
// 관점에서는 *스스로를 증명하는 반환값* 이다 — 알고리즘이 낸 숫자가 잔여
// 그래프의 도달 집합이 만드는 절단 용량과 같은지를 매 실행이 확인할 수 있다.
// 그래서 이 층의 검사는 "기대값 표" 가 아니라 certificate 대조다.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

#include "measure/counters.hpp"

namespace algo {

inline constexpr int64_t kInfFlow = INT64_MAX / 4;

// snippet-begin: flow-residual
// 잔여 그래프. 간선을 항상 짝으로 넣고 짝의 인덱스를 `id ^ 1` 로 찾는다.
// 이 짝짓기가 어긋나면 유량은 그럴듯한 값을 내면서 정점 보존을 깬다 —
// 그래서 검사는 반환값만이 아니라 보존식을 따로 단언해야 한다.
class Residual {
public:
    struct Arc { uint32_t to; int64_t cap; int64_t flow; int64_t cost; };

    explicit Residual(size_t vertices) : head_(vertices) {}

    size_t AddEdge(uint32_t u, uint32_t v, int64_t capacity, int64_t cost = 0) {
        const size_t id = arc_.size();
        arc_.push_back(Arc{v, capacity, 0, cost});
        arc_.push_back(Arc{u, 0, 0, -cost});      // 역간선. 용량 0 에서 시작한다.
        head_[u].push_back(id);
        head_[v].push_back(id + 1);
        return id;
    }

    int64_t Residualcap(size_t id) const { return arc_[id].cap - arc_[id].flow; }
    void Push(size_t id, int64_t amount) {
        arc_[id].flow += amount;
        arc_[id ^ 1].flow -= amount;              // ← 짝 규칙이 여기 산다
    }
    void ResetFlow() { for (Arc& a : arc_) a.flow = 0; }

    size_t Vertices() const { return head_.size(); }
    const std::vector<size_t>& Out(uint32_t v) const { return head_[v]; }
    const Arc& At(size_t id) const { return arc_[id]; }
    size_t ArcCount() const { return arc_.size(); }

    // 잔여 용량이 남은 간선만 따라 s 에서 갈 수 있는 정점 집합.
    // min-cut certificate 의 재료다.
    std::vector<uint8_t> ReachableFrom(uint32_t s) const {
        std::vector<uint8_t> seen(head_.size(), 0);
        std::vector<uint32_t> stack{s};
        seen[s] = 1;
        while (!stack.empty()) {
            const uint32_t u = stack.back();
            stack.pop_back();
            for (size_t id : head_[u]) {
                if (Residualcap(id) <= 0) continue;
                const uint32_t v = arc_[id].to;
                if (seen[v]) continue;
                seen[v] = 1;
                stack.push_back(v);
            }
        }
        return seen;
    }

    // 유량 보존. 원천·수렁을 뺀 모든 정점에서 들어온 유량과 나간 유량이 같아야 한다.
    bool Conserves(uint32_t s, uint32_t t) const {
        for (uint32_t v = 0; v < head_.size(); ++v) {
            if (v == s || v == t) continue;
            int64_t net = 0;
            for (size_t id : head_[v]) {
                if ((id & 1) == 0) net -= arc_[id].flow;   // 정방향: 나간다
                else               net += arc_[id ^ 1].flow;
            }
            if (net != 0) return false;
        }
        return true;
    }
    // 0 <= f(e) <= c(e)
    bool CapacityRespected() const {
        for (size_t id = 0; id < arc_.size(); id += 2)
            if (arc_[id].flow < 0 || arc_[id].flow > arc_[id].cap) return false;
        return true;
    }
    int64_t NetOutOf(uint32_t s) const {
        int64_t net = 0;
        for (size_t id : head_[s]) {
            if ((id & 1) == 0) net += arc_[id].flow;
            else               net -= arc_[id ^ 1].flow;
        }
        return net;
    }

private:
    std::vector<Arc> arc_;
    std::vector<std::vector<size_t>> head_;
};
// snippet-end: flow-residual

struct FlowResult {
    int64_t value = 0;
    size_t  augments = 0;
    size_t  phases = 0;
    size_t  visited = 0;
};

// snippet-begin: flow-ford-fulkerson
// Ford-Fulkerson(DFS 로 증가 경로를 찾는다). 정수 용량이면 종료하지만 반복 횟수가
// 유량값에 비례한다 — 용량이 크면 경로 하나에 1씩만 흘려 O(f) 번 돈다.
inline FlowResult FordFulkerson(Residual& g, uint32_t s, uint32_t t,
                                measure::Counters* c = nullptr) {
    FlowResult r;
    g.ResetFlow();
    const size_t n = g.Vertices();
    for (;;) {
        std::vector<size_t> via(n, size_t(-1));
        std::vector<uint8_t> seen(n, 0);
        std::vector<uint32_t> stack{s};
        seen[s] = 1;
        while (!stack.empty()) {
            const uint32_t u = stack.back();
            stack.pop_back();
            ++r.visited;
            if (c) ++c->visited;
            if (u == t) break;
            for (size_t id : g.Out(u)) {
                if (g.Residualcap(id) <= 0) continue;
                const uint32_t v = g.At(id).to;
                if (seen[v]) continue;
                seen[v] = 1;
                via[v] = id;
                stack.push_back(v);
            }
        }
        if (!seen[t]) break;
        int64_t bottleneck = kInfFlow;
        for (uint32_t v = t; v != s; v = g.At(via[v] ^ 1).to)
            bottleneck = std::min(bottleneck, g.Residualcap(via[v]));
        for (uint32_t v = t; v != s; v = g.At(via[v] ^ 1).to) g.Push(via[v], bottleneck);
        r.value += bottleneck;
        ++r.augments;
        if (c) ++c->augments;
    }
    return r;
}
// snippet-end: flow-ford-fulkerson

// snippet-begin: flow-edmonds-karp
// Edmonds-Karp = Ford-Fulkerson + BFS. 가장 *짧은* 증가 경로를 고르면 증가
// 횟수가 O(V*E) 로 묶인다 — 용량 크기와 무관해진다. 같은 골격에서 탐색 순서
// 하나를 바꾼 것이 상계를 바꾼다.
inline FlowResult EdmondsKarp(Residual& g, uint32_t s, uint32_t t,
                              measure::Counters* c = nullptr) {
    FlowResult r;
    g.ResetFlow();
    const size_t n = g.Vertices();
    for (;;) {
        std::vector<size_t> via(n, size_t(-1));
        std::vector<uint8_t> seen(n, 0);
        std::deque<uint32_t> q{s};
        seen[s] = 1;
        while (!q.empty() && !seen[t]) {
            const uint32_t u = q.front();
            q.pop_front();
            ++r.visited;
            if (c) ++c->visited;
            for (size_t id : g.Out(u)) {
                if (g.Residualcap(id) <= 0) continue;
                const uint32_t v = g.At(id).to;
                if (seen[v]) continue;
                seen[v] = 1;
                via[v] = id;
                q.push_back(v);
            }
        }
        if (!seen[t]) break;
        int64_t bottleneck = kInfFlow;
        for (uint32_t v = t; v != s; v = g.At(via[v] ^ 1).to)
            bottleneck = std::min(bottleneck, g.Residualcap(via[v]));
        for (uint32_t v = t; v != s; v = g.At(via[v] ^ 1).to) g.Push(via[v], bottleneck);
        r.value += bottleneck;
        ++r.augments;
        if (c) ++c->augments;
    }
    return r;
}
// snippet-end: flow-edmonds-karp

// snippet-begin: flow-dinic
// Dinic. 층 그래프를 만들고(BFS) 그 안에서 blocking flow 를 한 번에 밀어낸다(DFS).
// `iter` 가 정점마다 "이미 막힌 간선" 을 기억해 같은 간선을 다시 시도하지 않는
// 것이 이 알고리즘이 빠른 이유다 — 그것을 빼면 Ford-Fulkerson 으로 되돌아간다.
class Dinic {
public:
    FlowResult Run(Residual& g, uint32_t s, uint32_t t, measure::Counters* c = nullptr) {
        FlowResult r;
        g.ResetFlow();
        const size_t n = g.Vertices();
        level_.assign(n, -1);
        iter_.assign(n, 0);
        for (;;) {
            if (!BuildLevels(g, s, t, c)) break;
            ++r.phases;
            std::fill(iter_.begin(), iter_.end(), size_t(0));
            for (;;) {
                const int64_t pushed = Advance(g, s, t, kInfFlow, c);
                if (pushed == 0) break;
                r.value += pushed;
                ++r.augments;
                if (c) ++c->augments;
            }
        }
        return r;
    }

private:
    std::vector<int64_t> level_;
    std::vector<size_t>  iter_;

    bool BuildLevels(const Residual& g, uint32_t s, uint32_t t, measure::Counters* c) {
        std::fill(level_.begin(), level_.end(), int64_t(-1));
        std::deque<uint32_t> q{s};
        level_[s] = 0;
        while (!q.empty()) {
            const uint32_t u = q.front();
            q.pop_front();
            if (c) ++c->visited;
            for (size_t id : g.Out(u)) {
                if (g.Residualcap(id) <= 0) continue;
                const uint32_t v = g.At(id).to;
                if (level_[v] != -1) continue;
                level_[v] = level_[u] + 1;
                q.push_back(v);
            }
        }
        return level_[t] != -1;
    }

    int64_t Advance(Residual& g, uint32_t u, uint32_t t, int64_t room,
                    measure::Counters* c) {
        if (u == t) return room;
        for (; iter_[u] < g.Out(u).size(); ++iter_[u]) {
            const size_t id = g.Out(u)[iter_[u]];
            const uint32_t v = g.At(id).to;
            if (g.Residualcap(id) <= 0 || level_[v] != level_[u] + 1) continue;
            if (c) ++c->visited;
            const int64_t got = Advance(g, v, t, std::min(room, g.Residualcap(id)), c);
            if (got <= 0) continue;
            g.Push(id, got);
            return got;
        }
        return 0;
    }
};
// snippet-end: flow-dinic

// snippet-begin: flow-min-cut
// min-cut certificate. 최대 유량이 끝난 잔여 그래프에서 s 도달 집합 S 를 잡으면,
// S 에서 나가는 *원래* 간선의 용량 합이 곧 절단 용량이고 최대 유량과 같다.
// 이 등식을 매 실행이 단언하면 "값이 진짜 최대인가" 를 답 표 없이 확인할 수 있다.
struct MinCut {
    int64_t capacity = 0;
    std::vector<uint8_t> source_side;
    std::vector<size_t>  crossing;   // 절단을 건너는 원래 간선 id
};

inline MinCut ExtractMinCut(const Residual& g, uint32_t s) {
    MinCut cut;
    cut.source_side = g.ReachableFrom(s);
    for (uint32_t u = 0; u < g.Vertices(); ++u) {
        if (!cut.source_side[u]) continue;
        for (size_t id : g.Out(u)) {
            if ((id & 1) != 0) continue;                   // 원래 간선만 센다
            const uint32_t v = g.At(id).to;
            if (cut.source_side[v]) continue;
            cut.capacity += g.At(id).cap;
            cut.crossing.push_back(id);
        }
    }
    return cut;
}
// snippet-end: flow-min-cut

// ------------------------------------------------------------------- 매칭

// snippet-begin: flow-matching
// 이분 매칭, DFS 증가 경로판. 왼쪽 정점 하나마다 한 번 시도한다.
// 각 시도에서 `seen` 을 초기화하지 않으면 다른 정점의 흔적이 남아 매칭을 놓친다.
struct Matching {
    size_t size = 0;
    std::vector<int32_t> left_of;    // 오른쪽 -> 왼쪽, 없으면 -1
    std::vector<int32_t> right_of;   // 왼쪽 -> 오른쪽, 없으면 -1
    size_t augments = 0;
    size_t phases = 0;
};

inline bool TryKuhn(uint32_t u, const std::vector<std::vector<uint32_t>>& adj,
                    std::vector<uint8_t>& seen, Matching& m, measure::Counters* c) {
    for (uint32_t v : adj[u]) {
        if (seen[v]) continue;
        seen[v] = 1;
        if (c) ++c->visited;
        if (m.left_of[v] == -1 || TryKuhn(uint32_t(m.left_of[v]), adj, seen, m, c)) {
            m.left_of[v] = int32_t(u);
            m.right_of[u] = int32_t(v);
            return true;
        }
    }
    return false;
}

inline Matching BipartiteMatchingDfs(size_t left, size_t right,
                                     const std::vector<std::vector<uint32_t>>& adj,
                                     measure::Counters* c = nullptr) {
    Matching m;
    m.left_of.assign(right, -1);
    m.right_of.assign(left, -1);
    for (uint32_t u = 0; u < left; ++u) {
        std::vector<uint8_t> seen(right, 0);
        if (TryKuhn(u, adj, seen, m, c)) { ++m.size; ++m.augments; }
    }
    return m;
}
// snippet-end: flow-matching

inline bool LayeredDfs(uint32_t u, const std::vector<std::vector<uint32_t>>& adj,
                       std::vector<int64_t>& dist, std::vector<uint8_t>& used,
                       Matching& m, measure::Counters* c);

// snippet-begin: flow-hopcroft-karp
// Hopcroft-Karp. 한 phase 에서 *서로소인 최단 증가 경로를 모두* 찾는다.
// phase 수가 O(sqrt(V)) 로 묶이는 것이 이 알고리즘의 전부다 —
// 증가 한 번의 비용은 같고, 횟수의 상계가 달라진다.
inline Matching HopcroftKarp(size_t left, size_t right,
                             const std::vector<std::vector<uint32_t>>& adj,
                             measure::Counters* c = nullptr) {
    Matching m;
    m.left_of.assign(right, -1);
    m.right_of.assign(left, -1);
    const int64_t kNone = -1;
    std::vector<int64_t> dist(left, kNone);

    for (;;) {
        // BFS: 매칭 안 된 왼쪽에서 시작해 층을 매긴다.
        std::deque<uint32_t> q;
        bool found = false;
        for (uint32_t u = 0; u < left; ++u) {
            if (m.right_of[u] == -1) { dist[u] = 0; q.push_back(u); }
            else                      dist[u] = kNone;
        }
        while (!q.empty()) {
            const uint32_t u = q.front();
            q.pop_front();
            if (c) ++c->visited;
            for (uint32_t v : adj[u]) {
                const int32_t w = m.left_of[v];
                if (w == -1) { found = true; continue; }
                if (dist[uint32_t(w)] != kNone) continue;
                dist[uint32_t(w)] = dist[u] + 1;
                q.push_back(uint32_t(w));
            }
        }
        if (!found) break;
        ++m.phases;

        // DFS: 층을 정확히 한 칸씩 올라가는 경로만 쓴다.
        std::vector<uint8_t> used(right, 0);
        for (uint32_t u = 0; u < left; ++u) {
            if (m.right_of[u] != -1) continue;
            if (LayeredDfs(u, adj, dist, used, m, c)) { ++m.size; ++m.augments; }
        }
    }
    return m;
}

inline bool LayeredDfs(uint32_t u, const std::vector<std::vector<uint32_t>>& adj,
                       std::vector<int64_t>& dist, std::vector<uint8_t>& used,
                       Matching& m, measure::Counters* c) {
    for (uint32_t v : adj[u]) {
        if (used[v]) continue;
        const int32_t w = m.left_of[v];
        if (w != -1 && dist[uint32_t(w)] != dist[u] + 1) continue;
        used[v] = 1;
        if (c) ++c->visited;
        if (w == -1 || LayeredDfs(uint32_t(w), adj, dist, used, m, c)) {
            m.left_of[v] = int32_t(u);
            m.right_of[u] = int32_t(v);
            return true;
        }
    }
    dist[u] = -1;
    return false;
}
// snippet-end: flow-hopcroft-karp

// 같은 문제를 최대 유량으로 환원한 팔. s -> 왼쪽(용량 1), 왼쪽 -> 오른쪽(1),
// 오른쪽 -> t(1). 세 팔이 같은 크기를 내야 한다.
inline int64_t MatchingViaFlow(size_t left, size_t right,
                               const std::vector<std::vector<uint32_t>>& adj) {
    Residual g(left + right + 2);
    const uint32_t s = uint32_t(left + right);
    const uint32_t t = s + 1;
    for (uint32_t u = 0; u < left; ++u) g.AddEdge(s, u, 1);
    for (uint32_t v = 0; v < right; ++v) g.AddEdge(uint32_t(left) + v, t, 1);
    for (uint32_t u = 0; u < left; ++u)
        for (uint32_t v : adj[u]) g.AddEdge(u, uint32_t(left) + v, 1);
    Dinic d;
    return d.Run(g, s, t).value;
}

// ---------------------------------------------------------------- 최소 비용

// snippet-begin: flow-min-cost
// 최소 비용 최대 유량. 역간선의 비용이 음수라 Dijkstra 를 그냥 쓸 수 없다.
// 잠재 함수(potential)로 비용을 재정의하면 —
//   reduced(u,v) = cost(u,v) + pi[u] - pi[v]
// 잔여 그래프의 모든 간선에서 이 값이 0 이상이 되어 Dijkstra 가 성립한다.
// 첫 pi 는 Bellman-Ford 로 잡는다(음수 비용 간선이 원래 있을 수 있으므로).
struct MinCostResult {
    int64_t flow = 0;
    int64_t cost = 0;
    size_t  augments = 0;
};

inline MinCostResult MinCostMaxFlow(Residual& g, uint32_t s, uint32_t t) {
    MinCostResult r;
    const size_t n = g.Vertices();
    std::vector<int64_t> pi(n, 0);

    // 초기 잠재: Bellman-Ford
    for (size_t round = 0; round < n; ++round) {
        bool changed = false;
        for (uint32_t u = 0; u < n; ++u) {
            if (pi[u] >= kInfFlow) continue;
            for (size_t id : g.Out(u)) {
                if (g.Residualcap(id) <= 0) continue;
                const uint32_t v = g.At(id).to;
                if (pi[u] + g.At(id).cost < pi[v]) { pi[v] = pi[u] + g.At(id).cost; changed = true; }
            }
        }
        if (!changed) break;
    }

    for (;;) {
        std::vector<int64_t> dist(n, kInfFlow);
        std::vector<size_t>  via(n, size_t(-1));
        std::vector<uint8_t> done(n, 0);
        dist[s] = 0;
        for (;;) {
            size_t u = n;
            for (size_t v = 0; v < n; ++v)
                if (!done[v] && dist[v] < kInfFlow && (u == n || dist[v] < dist[u])) u = v;
            if (u == n) break;
            done[u] = 1;
            for (size_t id : g.Out(uint32_t(u))) {
                if (g.Residualcap(id) <= 0) continue;
                const uint32_t v = g.At(id).to;
                const int64_t reduced = g.At(id).cost + pi[u] - pi[v];
                if (dist[u] + reduced < dist[v]) { dist[v] = dist[u] + reduced; via[v] = id; }
            }
        }
        if (dist[t] >= kInfFlow) break;
        for (size_t v = 0; v < n; ++v)
            if (dist[v] < kInfFlow) pi[v] += dist[v];

        int64_t bottleneck = kInfFlow;
        for (uint32_t v = t; v != s; v = g.At(via[v] ^ 1).to)
            bottleneck = std::min(bottleneck, g.Residualcap(via[v]));
        for (uint32_t v = t; v != s; v = g.At(via[v] ^ 1).to) {
            r.cost += bottleneck * g.At(via[v]).cost;
            g.Push(via[v], bottleneck);
        }
        r.flow += bottleneck;
        ++r.augments;
    }
    return r;
}
// snippet-end: flow-min-cost

// snippet-begin: flow-hungarian
// Hungarian(Jonker-Volgenant 형태의 O(n^3) 판). 정사각 비용 행렬의 최소 할당.
// 순열 전수 열거 오라클과 값이 같아야 한다 — n=8 이면 40,320 개를 다 세도 싸다.
inline int64_t Hungarian(const std::vector<std::vector<int64_t>>& cost,
                         std::vector<size_t>* assign = nullptr) {
    const size_t n = cost.size();
    if (n == 0) return 0;
    const int64_t kBig = INT64_MAX / 4;
    std::vector<int64_t> u(n + 1, 0), v(n + 1, 0);
    std::vector<size_t>  p(n + 1, 0), way(n + 1, 0);

    for (size_t i = 1; i <= n; ++i) {
        p[0] = i;
        size_t j0 = 0;
        std::vector<int64_t> minv(n + 1, kBig);
        std::vector<uint8_t> used(n + 1, 0);
        do {
            used[j0] = 1;
            const size_t i0 = p[j0];
            int64_t delta = kBig;
            size_t j1 = 0;
            for (size_t j = 1; j <= n; ++j) {
                if (used[j]) continue;
                const int64_t cur = cost[i0 - 1][j - 1] - u[i0] - v[j];
                if (cur < minv[j]) { minv[j] = cur; way[j] = j0; }
                if (minv[j] < delta) { delta = minv[j]; j1 = j; }
            }
            for (size_t j = 0; j <= n; ++j) {
                if (used[j]) { u[p[j]] += delta; v[j] -= delta; }
                else         { minv[j] -= delta; }
            }
            j0 = j1;
        } while (p[j0] != 0);
        do {
            const size_t j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }
    int64_t total = 0;
    if (assign) assign->assign(n, 0);
    for (size_t j = 1; j <= n; ++j) {
        total += cost[p[j] - 1][j - 1];
        if (assign) (*assign)[p[j] - 1] = j - 1;
    }
    return total;
}
// snippet-end: flow-hungarian

}  // namespace algo
