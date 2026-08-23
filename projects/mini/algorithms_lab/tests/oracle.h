// oracle.h — 느린 기준 구현 모음.
//
// 오라클의 가치는 속도가 아니라 *독립성* 이다. 제품 구현과 같은 아이디어로 짜면
// 같은 오해를 공유해 둘 다 같은 답을 틀리게 낸다. 그래서 여기 있는 구현은
// 의도적으로 다른 골격을 쓴다 —
//   최단 경로  → 전 쌍 반복 완화(Floyd-Warshall)
//   MST        → 모든 간선 부분집합 열거
//   절단점     → 정점을 실제로 지우고 BFS 를 다시 돌린다
//   매칭       → 모든 부분집합 열거
//   배낭·DP    → 모든 선택 조합 전수 열거
//   문자열     → 위치마다 바이트를 직접 비교
//   할당       → 모든 순열
// 전부 지수·고차 다항이라 작은 입력에만 쓴다. 그것이 오라클의 사용 규칙이다.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "algo/dp.hpp"
#include "algo/graph.hpp"
#include "algo/greedy.hpp"
#include "algo/paths.hpp"

namespace oracle {

// ---------------------------------------------------------------- 정렬·선택

// multiset 이 보존됐는가. 순서만 보면 원소를 잃은 구현이 통과한다.
inline bool SameMultiset(const std::vector<algo::Rec>& a, const std::vector<algo::Rec>& b) {
    if (a.size() != b.size()) return false;
    std::vector<uint64_t> x, y;
    x.reserve(a.size());
    y.reserve(b.size());
    for (const algo::Rec& r : a) x.push_back((uint64_t(r.key) << 32) | r.tag);
    for (const algo::Rec& r : b) y.push_back((uint64_t(r.key) << 32) | r.tag);
    std::sort(x.begin(), x.end());
    std::sort(y.begin(), y.end());
    return x == y;
}

inline bool NonDecreasing(const std::vector<algo::Rec>& a) {
    for (size_t i = 1; i < a.size(); ++i)
        if (a[i - 1].key > a[i].key) return false;
    return true;
}

// 안정성. 같은 키 안에서 tag 가 오름차순이면 원래 순서가 보존된 것이다
// (생성기가 tag 를 원래 위치로 채워 넣기 때문).
inline bool Stable(const std::vector<algo::Rec>& a) {
    for (size_t i = 1; i < a.size(); ++i)
        if (a[i - 1].key == a[i].key && a[i - 1].tag > a[i].tag) return false;
    return true;
}

// k 번째 원소를 std::sort 로 직접 구한다.
inline algo::Rec NthByFullSort(std::vector<algo::Rec> a, size_t k) {
    std::sort(a.begin(), a.end(), [](const algo::Rec& x, const algo::Rec& y) {
        if (x.key != y.key) return x.key < y.key;
        return x.tag < y.tag;
    });
    return a[k];
}

// ------------------------------------------------------------------- 구간

// 모든 구간을 직접 합산한다.
inline int64_t SlowRangeSum(const std::vector<int64_t>& a, size_t l, size_t r) {
    int64_t s = 0;
    for (size_t i = l; i < r; ++i) s += a[i];
    return s;
}

// 윈도별 max_element.
inline std::vector<int64_t> SlowSlidingMax(const std::vector<int64_t>& a, size_t w) {
    std::vector<int64_t> out;
    if (w == 0 || a.size() < w) return out;
    for (size_t i = 0; i + w <= a.size(); ++i)
        out.push_back(*std::max_element(a.begin() + std::ptrdiff_t(i),
                                        a.begin() + std::ptrdiff_t(i + w)));
    return out;
}

// 최대 연속 부분합. 정의대로 모든 (l, r) 을 센다.
inline int64_t SlowMaxSubarray(const std::vector<int64_t>& a, size_t l, size_t r) {
    int64_t best = INT64_MIN;
    for (size_t i = l; i <= r; ++i) {
        int64_t run = 0;
        for (size_t j = i; j <= r; ++j) {
            run += a[j];
            if (run > best) best = run;
        }
    }
    return best;
}

// ------------------------------------------------------------------- 문자열

// 위치마다 바이트를 직접 비교한다.
inline std::vector<size_t> SlowSearch(const std::string& text, const std::string& pat) {
    std::vector<size_t> hits;
    if (pat.empty() || pat.size() > text.size()) return hits;
    for (size_t i = 0; i + pat.size() <= text.size(); ++i) {
        bool same = true;
        for (size_t j = 0; j < pat.size(); ++j)
            if (text[i + j] != pat[j]) { same = false; break; }
        if (same) hits.push_back(i);
    }
    return hits;
}

// 여러 패턴을 각각 따로 찾는다. Aho-Corasick 의 결과와 multiset 이 같아야 한다.
inline std::vector<std::pair<size_t, size_t>> SlowMultiSearch(
    const std::string& text, const std::vector<std::string>& pats) {
    std::vector<std::pair<size_t, size_t>> hits;   // (끝 위치, 패턴 번호)
    for (size_t p = 0; p < pats.size(); ++p)
        for (size_t at : SlowSearch(text, pats[p]))
            hits.push_back({at + pats[p].size() - 1, p});
    std::sort(hits.begin(), hits.end());
    return hits;
}

// -------------------------------------------------------------------- 그래프

// 전 쌍 최단 거리를 반복 완화로만 구한다(Floyd-Warshall). 우선순위 큐도 '확정'
// 개념도 쓰지 않으므로 Dijkstra 계열과 같은 오해를 공유할 여지가 거의 없다.
// O(V^3) 이라 작은 그래프에서만 쓴다.
inline std::vector<std::vector<int64_t>> SlowAllPairs(
    size_t n, const std::vector<algo::Csr::Edge>& edges, bool undirected) {
    std::vector<std::vector<int64_t>> d(n, std::vector<int64_t>(n, algo::kInfCost));
    for (size_t v = 0; v < n; ++v) d[v][v] = 0;
    for (const algo::Csr::Edge& e : edges) {
        if (e.w < d[e.u][e.v]) d[e.u][e.v] = e.w;
        if (undirected && e.w < d[e.v][e.u]) d[e.v][e.u] = e.w;
    }
    for (size_t k = 0; k < n; ++k)
        for (size_t i = 0; i < n; ++i) {
            if (d[i][k] >= algo::kInfCost) continue;
            for (size_t j = 0; j < n; ++j) {
                if (d[k][j] >= algo::kInfCost) continue;
                if (d[i][k] + d[k][j] < d[i][j]) d[i][j] = d[i][k] + d[k][j];
            }
        }
    return d;
}

// 정점 하나를 지우고 BFS 를 다시 돌려 성분 수가 늘었는지 본다.
// 절단점의 *정의* 그대로다 — low-link 논증을 전혀 쓰지 않는다.
inline size_t ComponentsWithout(size_t n, const std::vector<algo::Csr::Edge>& edges,
                                size_t removed) {
    std::vector<std::vector<uint32_t>> adj(n);
    for (const algo::Csr::Edge& e : edges) {
        if (e.u == removed || e.v == removed) continue;
        adj[e.u].push_back(e.v);
        adj[e.v].push_back(e.u);
    }
    std::vector<uint8_t> seen(n, 0);
    size_t comps = 0;
    for (size_t s = 0; s < n; ++s) {
        if (s == removed || seen[s]) continue;
        ++comps;
        std::vector<uint32_t> stack{uint32_t(s)};
        seen[s] = 1;
        while (!stack.empty()) {
            const uint32_t u = stack.back();
            stack.pop_back();
            for (uint32_t v : adj[u]) {
                if (seen[v]) continue;
                seen[v] = 1;
                stack.push_back(v);
            }
        }
    }
    return comps;
}

inline std::vector<uint8_t> SlowArticulation(size_t n,
                                             const std::vector<algo::Csr::Edge>& edges) {
    std::vector<uint8_t> out(n, 0);
    const size_t base = ComponentsWithout(n, edges, n);   // 아무것도 안 지운 성분 수
    for (size_t v = 0; v < n; ++v)
        out[v] = (ComponentsWithout(n, edges, v) > base) ? 1u : 0u;
    return out;
}

// 간선 하나를 지우고 성분 수를 다시 센다.
inline std::vector<uint32_t> SlowBridges(size_t n,
                                         const std::vector<algo::Csr::Edge>& edges) {
    std::vector<uint32_t> out;
    const size_t base = ComponentsWithout(n, edges, n);
    for (size_t drop = 0; drop < edges.size(); ++drop) {
        std::vector<algo::Csr::Edge> rest;
        for (size_t i = 0; i < edges.size(); ++i)
            if (i != drop) rest.push_back(edges[i]);
        if (ComponentsWithout(n, rest, n) > base) out.push_back(uint32_t(drop));
    }
    std::sort(out.begin(), out.end());
    return out;
}

// 모든 간선 부분집합을 열거해 최소 신장 비용을 찾는다. E <= 20 에서만 쓴다.
inline int64_t BruteMstCost(size_t n, const std::vector<algo::Csr::Edge>& edges,
                            bool* spanning) {
    const size_t m = edges.size();
    int64_t best = INT64_MAX;
    bool any = false;
    for (uint64_t mask = 0; mask < (uint64_t(1) << m); ++mask) {
        size_t picked = 0;
        for (size_t i = 0; i < m; ++i) if (mask & (uint64_t(1) << i)) ++picked;
        if (picked + 1 != n) continue;                 // 신장 트리는 정확히 n-1 개
        std::vector<algo::Csr::Edge> subset;
        int64_t cost = 0;
        for (size_t i = 0; i < m; ++i) {
            if (!(mask & (uint64_t(1) << i))) continue;
            subset.push_back(edges[i]);
            cost += edges[i].w;
        }
        if (ComponentsWithout(n, subset, n) != 1) continue;   // 연결이어야 트리
        any = true;
        if (cost < best) best = cost;
    }
    if (spanning) *spanning = any;
    return any ? best : 0;
}

// DSU 의 연결 판정을 BFS 성분으로 대조한다.
inline std::vector<uint32_t> ComponentIdByBfs(size_t n,
                                              const std::vector<algo::Csr::Edge>& edges) {
    std::vector<std::vector<uint32_t>> adj(n);
    for (const algo::Csr::Edge& e : edges) {
        adj[e.u].push_back(e.v);
        adj[e.v].push_back(e.u);
    }
    std::vector<uint32_t> comp(n, uint32_t(-1));
    uint32_t next = 0;
    for (size_t s = 0; s < n; ++s) {
        if (comp[s] != uint32_t(-1)) continue;
        std::vector<uint32_t> stack{uint32_t(s)};
        comp[s] = next;
        while (!stack.empty()) {
            const uint32_t u = stack.back();
            stack.pop_back();
            for (uint32_t v : adj[u]) {
                if (comp[v] != uint32_t(-1)) continue;
                comp[v] = next;
                stack.push_back(v);
            }
        }
        ++next;
    }
    return comp;
}

// --------------------------------------------------------------------- 매칭

// 오른쪽 정점의 모든 부분집합 x 왼쪽 배정 순열 — 대신 재귀 전수 탐색으로
// 최대 매칭 크기를 센다. 증가 경로 개념을 쓰지 않는다.
inline size_t BruteMatchingImpl(size_t u, size_t left,
                                const std::vector<std::vector<uint32_t>>& adj,
                                std::vector<uint8_t>& used) {
    if (u == left) return 0;
    size_t best = BruteMatchingImpl(u + 1, left, adj, used);   // u 를 안 쓴다
    for (uint32_t v : adj[u]) {
        if (used[v]) continue;
        used[v] = 1;
        const size_t got = 1 + BruteMatchingImpl(u + 1, left, adj, used);
        used[v] = 0;
        if (got > best) best = got;
    }
    return best;
}

inline size_t BruteMatching(size_t left, size_t right,
                            const std::vector<std::vector<uint32_t>>& adj) {
    std::vector<uint8_t> used(right, 0);
    return BruteMatchingImpl(0, left, adj, used);
}

// 모든 순열을 세는 최소 할당. n <= 8 에서만.
inline int64_t BruteAssignment(const std::vector<std::vector<int64_t>>& cost) {
    const size_t n = cost.size();
    std::vector<size_t> perm(n);
    for (size_t i = 0; i < n; ++i) perm[i] = i;
    int64_t best = INT64_MAX;
    do {
        int64_t total = 0;
        for (size_t i = 0; i < n; ++i) total += cost[i][perm[i]];
        if (total < best) best = total;
    } while (std::next_permutation(perm.begin(), perm.end()));
    return n ? best : 0;
}

// ------------------------------------------------------------------- 최적화

// 0/1 배낭의 전수 열거. greedy 의 근사비와 DP 의 값을 한 오라클이 함께 판정한다.
inline int64_t BruteKnapsack(const std::vector<algo::Item>& items, int64_t capacity) {
    const size_t n = items.size();
    int64_t best = 0;
    for (uint64_t mask = 0; mask < (uint64_t(1) << n); ++mask) {
        int64_t w = 0, v = 0;
        for (size_t i = 0; i < n; ++i) {
            if (!(mask & (uint64_t(1) << i))) continue;
            w += items[i].weight;
            v += items[i].value;
        }
        if (w <= capacity && v > best) best = v;
    }
    return best;
}

// 동전 교환의 진짜 최소 개수. BFS 로 푼다 — greedy 와 골격이 다르다.
inline int64_t ExactCoinCount(const std::vector<int64_t>& coins, int64_t amount) {
    if (amount < 0) return -1;
    std::vector<int64_t> best(size_t(amount) + 1, -1);
    best[0] = 0;
    for (int64_t a = 1; a <= amount; ++a) {
        for (int64_t c : coins) {
            if (c <= 0 || c > a) continue;
            if (best[size_t(a - c)] < 0) continue;
            const int64_t cand = best[size_t(a - c)] + 1;
            if (best[size_t(a)] < 0 || cand < best[size_t(a)]) best[size_t(a)] = cand;
        }
    }
    return best[size_t(amount)];
}

// 집합 덮개의 최적. 부분집합 전수 열거.
inline int64_t BruteSetCover(size_t universe,
                             const std::vector<std::vector<uint32_t>>& sets,
                             const std::vector<int64_t>& cost) {
    const size_t m = sets.size();
    int64_t best = INT64_MAX;
    for (uint64_t mask = 0; mask < (uint64_t(1) << m); ++mask) {
        std::vector<uint8_t> covered(universe, 0);
        int64_t total = 0;
        for (size_t s = 0; s < m; ++s) {
            if (!(mask & (uint64_t(1) << s))) continue;
            total += cost[s];
            for (uint32_t e : sets[s]) covered[e] = 1;
        }
        bool all = true;
        for (uint8_t c : covered) if (!c) { all = false; break; }
        if (all && total < best) best = total;
    }
    return (best == INT64_MAX) ? -1 : best;
}

// LCS 의 전수 열거 판. a 의 부분집합을 전부 만들어 b 의 부분 수열인지 본다.
inline size_t BruteLcs(const std::string& a, const std::string& b) {
    size_t best = 0;
    for (uint32_t mask = 0; mask < (1u << a.size()); ++mask) {
        std::string sub;
        for (size_t i = 0; i < a.size(); ++i)
            if (mask & (1u << i)) sub.push_back(a[i]);
        size_t j = 0;
        for (char ch : b) {
            if (j < sub.size() && sub[j] == ch) ++j;
        }
        if (j == sub.size() && sub.size() > best) best = sub.size();
    }
    return best;
}

// 최장 증가 부분 수열의 전수 열거.
inline size_t BruteLis(const std::vector<int64_t>& a) {
    size_t best = 0;
    for (uint32_t mask = 0; mask < (1u << a.size()); ++mask) {
        std::vector<int64_t> sub;
        for (size_t i = 0; i < a.size(); ++i)
            if (mask & (1u << i)) sub.push_back(a[i]);
        bool increasing = true;
        for (size_t i = 1; i < sub.size(); ++i)
            if (sub[i - 1] >= sub[i]) { increasing = false; break; }
        if (increasing && sub.size() > best) best = sub.size();
    }
    return best;
}

}  // namespace oracle
