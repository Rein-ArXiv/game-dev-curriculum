// graph.hpp — 그래프 구조.
//
// 이 헤더가 소유하는 계약 넷.
//   1. BFS 는 큐에 *넣을 때* 표시한다. 꺼낼 때 표시하면 같은 정점이 여러 번 들어간다.
//   2. DFS 의 tin/tout 은 괄호 구조를 이룬다 — 두 구간은 포함이거나 서로소다.
//   3. SCC 축약 그래프(condensation)는 반드시 DAG 다.
//   4. MST 는 최단 경로를 보장하지 않는다. 두 문제의 목적 함수가 다르다.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

#include "measure/counters.hpp"

namespace algo {

// snippet-begin: graph-csr
// CSR(compressed sparse row). 간선을 (from) 기준으로 한 번 세고 배타적 prefix sum
// 으로 시작 위치를 잡은 뒤, 두 번째 순회에서 제자리에 흩뿌린다 — counting sort 와
// 같은 절차다. 인접 리스트의 포인터 추적이 사라져 순회가 캐시에 붙는다.
struct Csr {
    std::vector<size_t>  start;   // 크기 n+1
    std::vector<uint32_t> to;
    std::vector<int64_t>  weight;
    std::vector<uint32_t> edge_id;   // 무방향 그래프에서 평행 간선을 가르는 표지
    size_t n = 0;

    struct Edge { uint32_t u, v; int64_t w; };

    void Build(size_t vertices, const std::vector<Edge>& edges, bool undirected) {
        n = vertices;
        start.assign(n + 1, 0);
        const size_t slots = edges.size() * (undirected ? 2 : 1);
        to.assign(slots, 0);
        weight.assign(slots, 0);
        edge_id.assign(slots, 0);

        std::vector<size_t> count(n, 0);
        for (const Edge& e : edges) {
            ++count[e.u];
            if (undirected) ++count[e.v];
        }
        size_t sum = 0;
        for (size_t v = 0; v < n; ++v) { start[v] = sum; sum += count[v]; }
        start[n] = sum;

        std::vector<size_t> cursor = start;
        cursor.resize(n);
        for (size_t id = 0; id < edges.size(); ++id) {
            const Edge& e = edges[id];
            to[cursor[e.u]] = e.v;
            weight[cursor[e.u]] = e.w;
            edge_id[cursor[e.u]] = uint32_t(id);
            ++cursor[e.u];
            if (undirected) {
                to[cursor[e.v]] = e.u;
                weight[cursor[e.v]] = e.w;
                edge_id[cursor[e.v]] = uint32_t(id);
                ++cursor[e.v];
            }
        }
    }
    size_t Degree(size_t v) const { return start[v + 1] - start[v]; }
};
// snippet-end: graph-csr

// -------------------------------------------------------------------- BFS

// snippet-begin: graph-bfs
// BFS. 계층 불변식: 큐 안에는 거리 d 인 정점과 d+1 인 정점만 있고, 그 순서다.
// `seen` 을 pop 시점에 켜면 같은 정점이 여러 번 push 되어 큐 길이가 간선 수만큼
// 자라고, 최단 거리 자체는 여전히 맞게 나온다 — 그래서 이 결함은 답이 아니라
// *큐 최대 길이* 로만 드러난다.
struct BfsResult {
    std::vector<int64_t>  dist;     // 도달 불가는 -1
    std::vector<uint32_t> parent;   // 없으면 자기 자신
    size_t queue_peak = 0;
    size_t pushes = 0;
};

inline BfsResult Bfs(const Csr& g, uint32_t source, measure::Counters* c = nullptr) {
    BfsResult r;
    r.dist.assign(g.n, -1);
    r.parent.resize(g.n);
    for (uint32_t v = 0; v < g.n; ++v) r.parent[v] = v;
    if (g.n == 0) return r;

    std::deque<uint32_t> q;
    r.dist[source] = 0;
    q.push_back(source);
    ++r.pushes;
    while (!q.empty()) {
        if (q.size() > r.queue_peak) r.queue_peak = q.size();
        const uint32_t u = q.front();
        q.pop_front();
        if (c) ++c->visited;
        for (size_t i = g.start[u]; i < g.start[u + 1]; ++i) {
            const uint32_t v = g.to[i];
            if (r.dist[v] != -1) continue;      // ← push 시점 표시
            r.dist[v] = r.dist[u] + 1;
            r.parent[v] = u;
            q.push_back(v);
            ++r.pushes;
        }
    }
    return r;
}
// snippet-end: graph-bfs

// -------------------------------------------------------------------- DFS

// snippet-begin: graph-dfs
// 반복 DFS. 재귀 대신 명시적 스택을 쓰되, *간선 반복자 위치* 를 함께 저장한다.
// 위치를 저장하지 않고 매번 처음부터 훑으면 같은 정점을 여러 번 열어 O(V*E) 가 된다.
// tin/tout 은 괄호 구조를 이룬다 — u 가 v 의 조상 <=> tin[u] < tin[v] && tout[v] < tout[u].
struct DfsResult {
    std::vector<int64_t> tin, tout;
    std::vector<uint32_t> order;
};

inline DfsResult DfsIterative(const Csr& g, measure::Counters* c = nullptr) {
    DfsResult r;
    r.tin.assign(g.n, -1);
    r.tout.assign(g.n, -1);
    int64_t clock = 0;
    std::vector<std::pair<uint32_t, size_t>> stack;   // (정점, 다음에 볼 간선 슬롯)
    for (uint32_t s = 0; s < g.n; ++s) {
        if (r.tin[s] != -1) continue;
        r.tin[s] = clock++;
        r.order.push_back(s);
        stack.push_back({s, g.start[s]});
        while (!stack.empty()) {
            uint32_t u = stack.back().first;
            size_t& it = stack.back().second;
            if (it == g.start[u + 1]) {
                r.tout[u] = clock++;
                stack.pop_back();
                continue;
            }
            const uint32_t v = g.to[it++];
            if (c) ++c->visited;
            if (r.tin[v] != -1) continue;
            r.tin[v] = clock++;
            r.order.push_back(v);
            stack.push_back({v, g.start[v]});
        }
    }
    return r;
}
// snippet-end: graph-dfs

// ------------------------------------------------------------------ 순환

// 방향 그래프의 순환 판정. 회색(스택 위) 표시가 필요하다 — 검은색(끝난) 정점으로
// 가는 간선은 순환이 아니다.
inline bool HasCycleDirected(const Csr& g) {
    std::vector<uint8_t> color(g.n, 0);   // 0 white 1 gray 2 black
    std::vector<std::pair<uint32_t, size_t>> stack;
    for (uint32_t s = 0; s < g.n; ++s) {
        if (color[s] != 0) continue;
        color[s] = 1;
        stack.push_back({s, g.start[s]});
        while (!stack.empty()) {
            uint32_t u = stack.back().first;
            size_t& it = stack.back().second;
            if (it == g.start[u + 1]) { color[u] = 2; stack.pop_back(); continue; }
            const uint32_t v = g.to[it++];
            if (color[v] == 1) return true;
            if (color[v] != 0) continue;
            color[v] = 1;
            stack.push_back({v, g.start[v]});
        }
    }
    return false;
}

// snippet-begin: graph-cycle-undirected
// 무방향 그래프의 순환 판정. "부모로 돌아가는 간선은 무시" 를 *정점* 으로 하면
// 평행 간선 u-v 둘이 순환인데 놓친다. 무시할 것은 부모 정점이 아니라 *타고 온
// 그 간선* 이므로 edge id 로 가른다.
inline bool HasCycleUndirected(const Csr& g) {
    std::vector<uint8_t> seen(g.n, 0);
    std::vector<std::pair<uint32_t, uint32_t>> stack;   // (정점, 들어온 edge id)
    const uint32_t kNoEdge = uint32_t(-1);
    for (uint32_t s = 0; s < g.n; ++s) {
        if (seen[s]) continue;
        seen[s] = 1;
        stack.push_back({s, kNoEdge});
        while (!stack.empty()) {
            const uint32_t u = stack.back().first;
            const uint32_t via = stack.back().second;
            stack.pop_back();
            for (size_t i = g.start[u]; i < g.start[u + 1]; ++i) {
                if (g.edge_id[i] == via) continue;   // ← 정점이 아니라 간선으로 가른다
                const uint32_t v = g.to[i];
                if (seen[v]) return true;
                seen[v] = 1;
                stack.push_back({v, g.edge_id[i]});
            }
        }
    }
    return false;
}
// snippet-end: graph-cycle-undirected

// ------------------------------------------------------------- 위상 정렬

// snippet-begin: graph-topo
// Kahn 위상 정렬. 진입 차수 0 인 정점을 꺼내고, 그 정점이 없앤 간선만큼 이웃의
// 차수를 줄인다. 결과가 전체 정점 수보다 짧으면 남은 부분에 순환이 있다.
// tie-break 를 최소 정점 번호로 고정해 출력이 결정적이다 — 그러지 않으면 같은
// 입력이 실행마다 다른(둘 다 유효한) 순서를 낸다.
struct TopoResult {
    std::vector<uint32_t> order;
    bool acyclic = false;
};

inline TopoResult TopoSortKahn(const Csr& g) {
    TopoResult r;
    std::vector<size_t> indeg(g.n, 0);
    for (uint32_t u = 0; u < g.n; ++u)
        for (size_t i = g.start[u]; i < g.start[u + 1]; ++i) ++indeg[g.to[i]];

    // 최소 힙 대신 정렬된 후보 집합. n 이 작아 단순함이 이긴다.
    std::vector<uint32_t> ready;
    for (uint32_t v = 0; v < g.n; ++v) if (indeg[v] == 0) ready.push_back(v);
    std::sort(ready.begin(), ready.end());

    while (!ready.empty()) {
        const uint32_t u = ready.front();
        ready.erase(ready.begin());
        r.order.push_back(u);
        for (size_t i = g.start[u]; i < g.start[u + 1]; ++i) {
            const uint32_t v = g.to[i];
            if (--indeg[v] != 0) continue;
            const auto at = std::lower_bound(ready.begin(), ready.end(), v);
            ready.insert(at, v);
        }
    }
    r.acyclic = (r.order.size() == g.n);
    return r;
}
// snippet-end: graph-topo

// --------------------------------------------------------------------- SCC

// snippet-begin: graph-tarjan
// Tarjan low-link. low[v] = v 의 부분 트리에서 back edge 로 닿을 수 있는 가장
// 작은 tin. 스택 위에 있는 정점으로 가는 간선만 low 갱신에 쓴다 — 이미 완성된
// 다른 SCC 로 가는 간선을 섞으면 서로 다른 성분이 하나로 붙는다.
struct SccResult {
    std::vector<uint32_t> comp;   // 정점 -> 성분 번호
    size_t count = 0;
};

inline SccResult SccTarjan(const Csr& g, measure::Counters* c = nullptr) {
    SccResult r;
    r.comp.assign(g.n, uint32_t(-1));
    std::vector<int64_t> tin(g.n, -1), low(g.n, 0);
    std::vector<uint8_t> on_stack(g.n, 0);
    std::vector<uint32_t> stack;
    std::vector<std::pair<uint32_t, size_t>> call;
    int64_t clock = 0;

    for (uint32_t s = 0; s < g.n; ++s) {
        if (tin[s] != -1) continue;
        call.push_back({s, g.start[s]});
        tin[s] = low[s] = clock++;
        stack.push_back(s);
        on_stack[s] = 1;
        while (!call.empty()) {
            const uint32_t u = call.back().first;
            size_t& it = call.back().second;
            if (it != g.start[u + 1]) {
                const uint32_t v = g.to[it++];
                if (c) ++c->visited;
                if (tin[v] == -1) {
                    tin[v] = low[v] = clock++;
                    stack.push_back(v);
                    on_stack[v] = 1;
                    call.push_back({v, g.start[v]});
                } else if (on_stack[v]) {
                    low[u] = std::min(low[u], tin[v]);   // ← 스택 위일 때만
                }
                continue;
            }
            call.pop_back();
            if (!call.empty()) {
                const uint32_t parent = call.back().first;
                low[parent] = std::min(low[parent], low[u]);
            }
            if (low[u] == tin[u]) {          // u 가 SCC 의 뿌리
                for (;;) {
                    const uint32_t w = stack.back();
                    stack.pop_back();
                    on_stack[w] = 0;
                    r.comp[w] = uint32_t(r.count);
                    if (w == u) break;
                }
                ++r.count;
            }
        }
    }
    return r;
}
// snippet-end: graph-tarjan

// snippet-begin: graph-kosaraju
// Kosaraju. 원 그래프에서 종료 시각 내림차순을 얻고, *역* 그래프를 그 순서로
// 훑는다. 두 번의 DFS 와 역그래프 한 벌이 필요하다 — Tarjan 보다 메모리가 더 든다.
inline SccResult SccKosaraju(const Csr& g) {
    SccResult r;
    r.comp.assign(g.n, uint32_t(-1));

    // 1) 종료 순서
    std::vector<uint8_t> seen(g.n, 0);
    std::vector<uint32_t> finish;
    std::vector<std::pair<uint32_t, size_t>> stack;
    for (uint32_t s = 0; s < g.n; ++s) {
        if (seen[s]) continue;
        seen[s] = 1;
        stack.push_back({s, g.start[s]});
        while (!stack.empty()) {
            const uint32_t u = stack.back().first;
            size_t& it = stack.back().second;
            if (it == g.start[u + 1]) { finish.push_back(u); stack.pop_back(); continue; }
            const uint32_t v = g.to[it++];
            if (seen[v]) continue;
            seen[v] = 1;
            stack.push_back({v, g.start[v]});
        }
    }

    // 2) 역그래프
    std::vector<Csr::Edge> rev;
    rev.reserve(g.to.size());
    for (uint32_t u = 0; u < g.n; ++u)
        for (size_t i = g.start[u]; i < g.start[u + 1]; ++i)
            rev.push_back(Csr::Edge{g.to[i], u, g.weight[i]});
    Csr rg;
    rg.Build(g.n, rev, false);

    // 3) 종료 시각 내림차순으로 역그래프 DFS
    std::vector<uint8_t> seen2(g.n, 0);
    for (size_t k = finish.size(); k-- > 0;) {
        const uint32_t s = finish[k];
        if (seen2[s]) continue;
        seen2[s] = 1;
        std::vector<uint32_t> plain{s};
        while (!plain.empty()) {
            const uint32_t u = plain.back();
            plain.pop_back();
            r.comp[u] = uint32_t(r.count);
            for (size_t i = rg.start[u]; i < rg.start[u + 1]; ++i) {
                const uint32_t v = rg.to[i];
                if (seen2[v]) continue;
                seen2[v] = 1;
                plain.push_back(v);
            }
        }
        ++r.count;
    }
    return r;
}
// snippet-end: graph-kosaraju

// -------------------------------------------------- articulation · bridge

// snippet-begin: graph-articulation
// 절단점과 다리. 두 조건이 *한 글자* 차이다.
//   다리   low[v] >  tin[u]     자식이 u 위로 못 올라간다 — 그 간선이 유일한 통로
//   절단점 low[v] >= tin[u]     자식이 u 까지만 올라간다 — u 를 지우면 끊어진다
// `>=` 를 `>` 로 쓰면 절단점 판정이 다리 판정이 되어, 다리가 아닌 절단점을 놓친다.
// 뿌리는 예외다 — 자식이 둘 이상일 때만 절단점이다.
struct CutResult {
    std::vector<uint8_t> articulation;
    std::vector<uint32_t> bridges;   // edge id 목록
};

inline CutResult ArticulationAndBridges(const Csr& g) {
    CutResult r;
    r.articulation.assign(g.n, 0);
    std::vector<int64_t> tin(g.n, -1), low(g.n, 0);
    std::vector<uint32_t> via(g.n, uint32_t(-1));
    std::vector<size_t> root_children(g.n, 0);
    std::vector<std::pair<uint32_t, size_t>> stack;
    int64_t clock = 0;

    for (uint32_t s = 0; s < g.n; ++s) {
        if (tin[s] != -1) continue;
        tin[s] = low[s] = clock++;
        stack.push_back({s, g.start[s]});
        while (!stack.empty()) {
            const uint32_t u = stack.back().first;
            size_t& it = stack.back().second;
            if (it == g.start[u + 1]) {
                stack.pop_back();
                if (stack.empty()) break;
                const uint32_t p = stack.back().first;
                low[p] = std::min(low[p], low[u]);
                if (p == s) {
                    ++root_children[p];
                } else if (low[u] >= tin[p]) {
                    r.articulation[p] = 1;
                }
                if (low[u] > tin[p]) r.bridges.push_back(via[u]);
                continue;
            }
            const size_t slot = it++;
            const uint32_t v = g.to[slot];
            if (g.edge_id[slot] == via[u]) continue;   // 타고 온 그 간선만 무시
            if (tin[v] != -1) { low[u] = std::min(low[u], tin[v]); continue; }
            tin[v] = low[v] = clock++;
            via[v] = g.edge_id[slot];
            stack.push_back({v, g.start[v]});
        }
        if (root_children[s] >= 2) r.articulation[s] = 1;
    }
    std::sort(r.bridges.begin(), r.bridges.end());
    return r;
}
// snippet-end: graph-articulation

// --------------------------------------------------------------------- DSU

// snippet-begin: graph-dsu
// Disjoint set union. 두 최적화를 각각 끌 수 있다 —
//   경로 압축 : find 중 지나간 정점을 뿌리에 직접 붙인다
//   union by size : 작은 트리를 큰 트리 밑에 붙인다
// 둘 다 켜면 상각 역아커만 함수 alpha(n) 이다. 하나만 켜도 O(log n) 이고,
// 둘 다 끄면 최악 O(n) 이다 — 이 lab 은 그 셋의 *평균 find 깊이* 를 실측한다.
class Dsu {
public:
    Dsu(size_t n, bool path_compression, bool union_by_size)
        : parent_(n), size_(n, 1), compress_(path_compression), by_size_(union_by_size) {
        for (size_t i = 0; i < n; ++i) parent_[i] = uint32_t(i);
    }

    uint32_t Find(uint32_t x, measure::Counters* c = nullptr) {
        uint32_t root = x;
        size_t depth = 0;
        while (parent_[root] != root) { root = parent_[root]; ++depth; }
        find_calls_ += 1;
        find_depth_ += depth;
        if (c) c->visited += depth + 1;
        if (compress_) {
            while (parent_[x] != root) {
                const uint32_t next = parent_[x];
                parent_[x] = root;
                x = next;
            }
        }
        return root;
    }

    bool Union(uint32_t a, uint32_t b, measure::Counters* c = nullptr) {
        uint32_t ra = Find(a, c), rb = Find(b, c);
        if (ra == rb) return false;
        if (by_size_ && size_[ra] < size_[rb]) { const uint32_t t = ra; ra = rb; rb = t; }
        parent_[rb] = ra;
        size_[ra] += size_[rb];
        --components_extra_;
        return true;
    }

    bool Connected(uint32_t a, uint32_t b) { return Find(a) == Find(b); }
    double AverageFindDepth() const {
        return find_calls_ ? double(find_depth_) / double(find_calls_) : 0.0;
    }
    void ResetStats() { find_calls_ = 0; find_depth_ = 0; }
    size_t Merges() const { return size_t(-components_extra_); }

private:
    std::vector<uint32_t> parent_;
    std::vector<size_t>   size_;
    bool compress_;
    bool by_size_;
    uint64_t find_calls_ = 0;
    uint64_t find_depth_ = 0;
    int64_t  components_extra_ = 0;
};
// snippet-end: graph-dsu

// --------------------------------------------------------------------- MST

// snippet-begin: graph-kruskal
// Kruskal. 간선을 비용 오름차순으로 보고 사이클을 만들지 않으면 채택한다.
// 사이클 판정이 DSU 다 — 그래서 이 알고리즘은 정렬 O(E log E) 에 지배된다.
struct MstResult {
    int64_t total = 0;
    std::vector<uint32_t> edges;   // 채택된 edge id
    bool spanning = false;         // 전체가 하나로 이어졌는가
};

inline MstResult Kruskal(size_t n, std::vector<Csr::Edge> edges) {
    MstResult r;
    std::vector<uint32_t> id(edges.size());
    for (uint32_t i = 0; i < edges.size(); ++i) id[i] = i;
    // 비용 → 정점쌍 순으로 정렬해 동점에서도 결과가 결정적이다.
    std::sort(id.begin(), id.end(), [&](uint32_t a, uint32_t b) {
        if (edges[a].w != edges[b].w) return edges[a].w < edges[b].w;
        if (edges[a].u != edges[b].u) return edges[a].u < edges[b].u;
        return edges[a].v < edges[b].v;
    });
    Dsu dsu(n, true, true);
    for (uint32_t e : id) {
        if (!dsu.Union(edges[e].u, edges[e].v)) continue;
        r.total += edges[e].w;
        r.edges.push_back(e);
    }
    r.spanning = (n == 0) || (r.edges.size() + 1 == n);
    std::sort(r.edges.begin(), r.edges.end());
    return r;
}
// snippet-end: graph-kruskal

// snippet-begin: graph-prim
// Prim. 트리를 한 정점에서 키운다. 밀집 그래프에서는 힙 없이 O(V^2) 로 도는
// 형태가 캐시에 붙어 실측이 더 빠르다 — 그것이 이 팔의 존재 이유다.
inline MstResult PrimDense(size_t n, const std::vector<Csr::Edge>& edges) {
    MstResult r;
    if (n == 0) { r.spanning = true; return r; }
    const int64_t kInf = INT64_MAX / 4;
    std::vector<std::vector<int64_t>> w(n, std::vector<int64_t>(n, kInf));
    std::vector<std::vector<uint32_t>> id(n, std::vector<uint32_t>(n, uint32_t(-1)));
    for (uint32_t e = 0; e < edges.size(); ++e) {
        const Csr::Edge& x = edges[e];
        if (x.w < w[x.u][x.v]) {
            w[x.u][x.v] = w[x.v][x.u] = x.w;
            id[x.u][x.v] = id[x.v][x.u] = e;
        }
    }
    std::vector<int64_t> best(n, kInf);
    std::vector<uint32_t> from(n, uint32_t(-1));
    std::vector<uint8_t> in(n, 0);
    best[0] = 0;
    for (size_t k = 0; k < n; ++k) {
        size_t pick = n;
        for (size_t v = 0; v < n; ++v)
            if (!in[v] && (pick == n || best[v] < best[pick])) pick = v;
        if (pick == n || best[pick] >= kInf) break;   // 비연결
        in[pick] = 1;
        r.total += best[pick];
        if (from[pick] != uint32_t(-1)) r.edges.push_back(id[from[pick]][pick]);
        for (size_t v = 0; v < n; ++v) {
            if (in[v] || w[pick][v] >= kInf) continue;
            if (w[pick][v] < best[v]) { best[v] = w[pick][v]; from[v] = uint32_t(pick); }
        }
    }
    r.spanning = (r.edges.size() + 1 == n);
    std::sort(r.edges.begin(), r.edges.end());
    return r;
}
// snippet-end: graph-prim

}  // namespace algo
