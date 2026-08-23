// dp.hpp — 동적 계획.
//
// DP 를 설계한다는 것은 세 가지를 고정하는 일이다 —
//   상태     무엇을 알면 나머지가 과거와 무관해지는가
//   전이     상태 사이의 의존이 어느 방향인가
//   순서     의존하는 쪽이 먼저 계산되도록 어떻게 훑는가
// 1차원 압축에서 순회 방향이 의미를 바꾸는 것이 이 셋이 독립이 아니라는 증거다.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "algo/greedy.hpp"
#include "measure/counters.hpp"

namespace algo {

struct DpStats {
    uint64_t states      = 0;
    uint64_t transitions = 0;
    uint64_t table_bytes = 0;
};

// -------------------------------------------------------------------- 배낭

// snippet-begin: dp-knapsack-2d
// 0/1 배낭, 2차원 표. dp[i][w] = 앞 i 개만 쓰고 용량 w 일 때의 최댓값.
// 표를 그대로 두면 복원이 자명하다 — 뒤에서부터 "값이 달라졌는가" 를 보면 된다.
struct KnapsackResult {
    int64_t value = 0;
    std::vector<size_t> chosen;
    DpStats stats;
};

inline KnapsackResult Knapsack2D(const std::vector<Item>& items, int64_t capacity) {
    KnapsackResult r;
    const size_t n = items.size();
    const size_t cap = size_t(capacity < 0 ? 0 : capacity);
    std::vector<std::vector<int64_t>> dp(n + 1, std::vector<int64_t>(cap + 1, 0));
    for (size_t i = 1; i <= n; ++i) {
        const Item& it = items[i - 1];
        for (size_t w = 0; w <= cap; ++w) {
            ++r.stats.states;
            dp[i][w] = dp[i - 1][w];
            ++r.stats.transitions;
            if (it.weight <= int64_t(w)) {
                const int64_t take = dp[i - 1][w - size_t(it.weight)] + it.value;
                ++r.stats.transitions;
                if (take > dp[i][w]) dp[i][w] = take;
            }
        }
    }
    r.value = dp[n][cap];
    size_t w = cap;
    for (size_t i = n; i > 0; --i) {
        if (dp[i][w] == dp[i - 1][w]) continue;   // 안 담았다
        r.chosen.push_back(i - 1);
        w -= size_t(items[i - 1].weight);
    }
    std::sort(r.chosen.begin(), r.chosen.end());
    r.stats.table_bytes = uint64_t(n + 1) * uint64_t(cap + 1) * sizeof(int64_t);
    return r;
}
// snippet-end: dp-knapsack-2d

// snippet-begin: dp-knapsack-1d
// 1차원 압축. 순회 방향이 *의미* 를 바꾼다.
//   내림차순: dp[w - weight] 가 아직 이번 물건을 안 쓴 값이다 → 0/1 배낭
//   오름차순: 이미 이번 물건을 쓴 값을 다시 본다 → 같은 물건을 여러 번 → unbounded
// 같은 식에서 방향 하나로 두 문제가 갈린다.
inline int64_t Knapsack1D(const std::vector<Item>& items, int64_t capacity,
                          bool descending, DpStats* stats = nullptr) {
    const size_t cap = size_t(capacity < 0 ? 0 : capacity);
    std::vector<int64_t> dp(cap + 1, 0);
    for (const Item& it : items) {
        if (it.weight <= 0) continue;
        const size_t weight = size_t(it.weight);
        if (weight > cap) continue;
        if (descending) {
            for (size_t w = cap + 1; w-- > weight;) {
                if (stats) { ++stats->states; ++stats->transitions; }
                dp[w] = std::max(dp[w], dp[w - weight] + it.value);
            }
        } else {
            for (size_t w = weight; w <= cap; ++w) {
                if (stats) { ++stats->states; ++stats->transitions; }
                dp[w] = std::max(dp[w], dp[w - weight] + it.value);
            }
        }
    }
    if (stats) stats->table_bytes = uint64_t(cap + 1) * sizeof(int64_t);
    return dp[cap];
}
// snippet-end: dp-knapsack-1d

// ------------------------------------------------------------------ 문자열

// snippet-begin: dp-lcs
// 최장 공통 부분 수열. 복원은 표를 뒤에서 되짚는다 — 별도 parent 표를 두지 않고
// dp 값의 관계만으로 되짚을 수 있는 것이 이 recurrence 의 성질이다.
struct LcsResult {
    size_t length = 0;
    std::string sequence;
    DpStats stats;
};

inline LcsResult Lcs(const std::string& a, const std::string& b) {
    LcsResult r;
    const size_t n = a.size(), m = b.size();
    std::vector<std::vector<uint32_t>> dp(n + 1, std::vector<uint32_t>(m + 1, 0));
    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            ++r.stats.states;
            ++r.stats.transitions;
            if (a[i - 1] == b[j - 1]) dp[i][j] = dp[i - 1][j - 1] + 1;
            else                      dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
        }
    }
    r.length = dp[n][m];
    size_t i = n, j = m;
    while (i > 0 && j > 0) {
        if (a[i - 1] == b[j - 1]) { r.sequence.push_back(a[i - 1]); --i; --j; }
        else if (dp[i - 1][j] >= dp[i][j - 1]) --i;
        else --j;
    }
    std::reverse(r.sequence.begin(), r.sequence.end());
    r.stats.table_bytes = uint64_t(n + 1) * uint64_t(m + 1) * sizeof(uint32_t);
    return r;
}
// snippet-end: dp-lcs

// 편집 거리(Levenshtein). 롤링 배열로 두 줄만 들고 간다 — 값은 얻지만 복원은
// 잃는다. 그 교환을 드러내려고 복원이 필요한 LCS 와 나란히 둔다.
inline size_t EditDistance(const std::string& a, const std::string& b,
                           DpStats* stats = nullptr) {
    const size_t n = a.size(), m = b.size();
    std::vector<size_t> prev(m + 1), cur(m + 1);
    for (size_t j = 0; j <= m; ++j) prev[j] = j;
    for (size_t i = 1; i <= n; ++i) {
        cur[0] = i;
        for (size_t j = 1; j <= m; ++j) {
            if (stats) { ++stats->states; stats->transitions += 3; }
            const size_t sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0u : 1u);
            cur[j] = std::min(sub, std::min(prev[j] + 1, cur[j - 1] + 1));
        }
        prev.swap(cur);
    }
    if (stats) stats->table_bytes = uint64_t(2 * (m + 1)) * sizeof(size_t);
    return prev[m];
}

// -------------------------------------------------------------------- LIS

// snippet-begin: dp-lis
// 최장 증가 부분 수열, 두 판.
//   O(n^2)  dp[i] = i 에서 끝나는 LIS 길이. 정의가 그대로 코드가 된다.
//   O(n log n) tail[k] = 길이 k+1 인 증가 수열의 *가능한 가장 작은 끝값*.
//   tail 은 정렬돼 있고, 그것이 이진 탐색이 가능한 유일한 이유다.
//   주의: tail 자체는 LIS 가 아니다 — 복원하려면 각 원소의 위치를 따로 기록해야 한다.
inline size_t LisQuadratic(const std::vector<int64_t>& a, DpStats* stats = nullptr) {
    if (a.empty()) return 0;
    std::vector<size_t> dp(a.size(), 1);
    size_t best = 1;
    for (size_t i = 1; i < a.size(); ++i) {
        for (size_t j = 0; j < i; ++j) {
            if (stats) { ++stats->states; ++stats->transitions; }
            if (a[j] < a[i] && dp[j] + 1 > dp[i]) dp[i] = dp[j] + 1;
        }
        best = std::max(best, dp[i]);
    }
    return best;
}

inline std::vector<int64_t> LisRestore(const std::vector<int64_t>& a) {
    if (a.empty()) return {};
    std::vector<int64_t> tail;
    std::vector<size_t> tail_index, parent(a.size(), size_t(-1));
    for (size_t i = 0; i < a.size(); ++i) {
        const size_t k = size_t(std::lower_bound(tail.begin(), tail.end(), a[i]) - tail.begin());
        if (k > 0) parent[i] = tail_index[k - 1];
        if (k == tail.size()) { tail.push_back(a[i]); tail_index.push_back(i); }
        else                  { tail[k] = a[i];       tail_index[k] = i; }
    }
    std::vector<int64_t> out;
    for (size_t at = tail_index.back(); at != size_t(-1); at = parent[at])
        out.push_back(a[at]);
    std::reverse(out.begin(), out.end());
    return out;
}
// snippet-end: dp-lis

// ---------------------------------------------------------------- 구간 DP

// snippet-begin: dp-matrix-chain
// 행렬 연쇄 곱. 상태가 구간이고 전이가 *분할점* 이다 — 구간 DP 의 원형.
// dims 는 n+1 개: 행렬 i 의 크기가 dims[i] x dims[i+1].
inline int64_t MatrixChain(const std::vector<int64_t>& dims, DpStats* stats = nullptr,
                           std::vector<std::vector<size_t>>* split = nullptr) {
    if (dims.size() < 2) return 0;
    const size_t n = dims.size() - 1;
    std::vector<std::vector<int64_t>> dp(n, std::vector<int64_t>(n, 0));
    std::vector<std::vector<size_t>> at(n, std::vector<size_t>(n, 0));
    for (size_t len = 2; len <= n; ++len) {
        for (size_t i = 0; i + len <= n; ++i) {
            const size_t j = i + len - 1;
            dp[i][j] = INT64_MAX;
            if (stats) ++stats->states;
            for (size_t k = i; k < j; ++k) {
                if (stats) ++stats->transitions;
                const int64_t cost = dp[i][k] + dp[k + 1][j] +
                                     dims[i] * dims[k + 1] * dims[j + 1];
                if (cost < dp[i][j]) { dp[i][j] = cost; at[i][j] = k; }
            }
        }
    }
    if (split) *split = at;
    if (stats) stats->table_bytes = uint64_t(n) * uint64_t(n) * sizeof(int64_t);
    return dp[0][n - 1];
}
// snippet-end: dp-matrix-chain

// ------------------------------------------------------------------ 트리 DP

// snippet-begin: dp-tree-mwis
// 트리 위 최대 가중 독립 집합. 상태가 (정점, 그 정점을 골랐는가) 둘이다.
// 트리라는 것이 여기서 하는 일은 딱 하나 — 자식들의 부분 문제가 서로 겹치지 않는다.
// 일반 그래프면 같은 정의가 성립하지 않는다(그 문제는 NP-hard 다).
inline int64_t TreeMaxWeightIndependentSet(
    const std::vector<std::vector<uint32_t>>& children,
    const std::vector<int64_t>& weight, uint32_t root, DpStats* stats = nullptr) {
    const size_t n = weight.size();
    if (n == 0) return 0;
    std::vector<int64_t> take(n, 0), skip(n, 0);
    // 후위 순회를 명시적 스택으로. 재귀는 깊은 경로 트리에서 스택을 넘긴다.
    std::vector<std::pair<uint32_t, size_t>> stack{{root, 0}};
    while (!stack.empty()) {
        const uint32_t u = stack.back().first;
        size_t& it = stack.back().second;
        if (it < children[u].size()) { stack.push_back({children[u][it++], 0}); continue; }
        take[u] = weight[u];
        skip[u] = 0;
        for (uint32_t v : children[u]) {
            if (stats) ++stats->transitions;
            take[u] += skip[v];                     // 나를 고르면 자식은 못 고른다
            skip[u] += std::max(take[v], skip[v]);  // 안 고르면 자식은 자유다
        }
        if (stats) ++stats->states;
        stack.pop_back();
    }
    return std::max(take[root], skip[root]);
}
// snippet-end: dp-tree-mwis

// --------------------------------------------------------------- 비트마스크

// snippet-begin: dp-bitmask
// 방문 집합을 비트마스크로 든 최소 비용 순회(열린 TSP). 상태 수가 n * 2^n 이라
// n 이 20 근처에서 메모리가 벽이 된다 — "지수" 가 추상이 아니라 표 크기다.
inline int64_t BitmaskMinTour(const std::vector<std::vector<int64_t>>& cost,
                              uint32_t start, DpStats* stats = nullptr) {
    const size_t n = cost.size();
    if (n == 0) return 0;
    if (n > 20) return -1;                       // 표가 감당 범위를 넘는다
    const size_t full = size_t(1) << n;
    const int64_t kBig = INT64_MAX / 4;
    std::vector<int64_t> dp(full * n, kBig);
    dp[(size_t(1) << start) * n + start] = 0;
    for (size_t mask = 0; mask < full; ++mask) {
        for (size_t u = 0; u < n; ++u) {
            const int64_t here = dp[mask * n + u];
            if (here >= kBig) continue;
            if (stats) ++stats->states;
            for (size_t v = 0; v < n; ++v) {
                if (mask & (size_t(1) << v)) continue;
                if (cost[u][v] >= kBig) continue;
                if (stats) ++stats->transitions;
                const size_t next = mask | (size_t(1) << v);
                int64_t& slot = dp[next * n + v];
                if (here + cost[u][v] < slot) slot = here + cost[u][v];
            }
        }
    }
    int64_t best = kBig;
    for (size_t u = 0; u < n; ++u) best = std::min(best, dp[(full - 1) * n + u]);
    if (stats) stats->table_bytes = uint64_t(full) * uint64_t(n) * sizeof(int64_t);
    return (best >= kBig) ? -1 : best;
}
// snippet-end: dp-bitmask

}  // namespace algo
