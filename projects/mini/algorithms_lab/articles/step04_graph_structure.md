## 들어가며

그래프의 구조를 읽는 알고리즘을 짓는다. CSR 표현, BFS와 반복 DFS, 방향·무방향 순환 판정, Kahn 위상 정렬, Tarjan과 Kosaraju 두 가지 SCC, 절단점과 다리, DSU 네 조합, Kruskal과 Prim.

이 단계가 소유하는 계약은 넷이다.

1. BFS는 큐에 **넣을 때** 표시한다. 꺼낼 때 표시하면 같은 정점이 여러 번 들어간다.
2. DFS의 tin/tout은 괄호 구조를 이룬다 — 두 구간은 포함이거나 서로소이고, 겹치기만 하는 쌍은 존재할 수 없다.
3. SCC 축약 그래프(condensation)는 반드시 DAG다. 순환이 있다면 그 성분들이 애초에 하나였어야 한다.
4. MST는 최단 경로를 보장하지 않는다. 두 문제의 목적 함수가 다르다.

---

## 목차
1. [CSR과 counting sort](#1-csr과-counting-sort)
2. [BFS의 표시 시점](#2-bfs의-표시-시점)
3. [반복 DFS와 반복자 위치](#3-반복-dfs와-반복자-위치)
4. [순환 판정은 간선으로](#4-순환-판정은-간선으로)
5. [SCC 두 팔과 condensation](#5-scc-두-팔과-condensation)
6. [절단점과 다리, 한 글자 차이](#6-절단점과-다리-한-글자-차이)
7. [DSU 네 조합과 정직한 경계](#7-dsu-네-조합과-정직한-경계)
8. [측정과 관측](#8-측정과-관측)

---

## 1. CSR과 counting sort

CSR 빌더의 절차는 counting sort와 같다. 간선을 출발 정점 기준으로 한 번 세고, 배타적 접두합으로 시작 위치를 잡고, 두 번째 순회에서 제자리에 흩뿌린다.

<!-- snippet: src/algo/graph.hpp@graph-csr mode=partial -->
```cpp
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
    // ...
};
```

`edge_id`를 함께 싣는 것이 이 lab의 선택이다. 무방향 그래프에서 "타고 온 간선"과 "부모 정점"은 다르고, 그 차이가 평행 간선에서 드러난다.

## 2. BFS의 표시 시점

<!-- snippet: src/algo/graph.hpp@graph-bfs mode=partial -->
```cpp
// BFS. 계층 불변식: 큐 안에는 거리 d 인 정점과 d+1 인 정점만 있고, 그 순서다.
// `seen` 을 pop 시점에 켜면 같은 정점이 여러 번 push 되어 큐 길이가 간선 수만큼
// 자라고, 최단 거리 자체는 여전히 맞게 나온다 — 그래서 이 결함은 답이 아니라
// *큐 최대 길이* 로만 드러난다.
inline BfsResult Bfs(const Csr& g, uint32_t source, measure::Counters* c = nullptr) {
    // ...
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
```

이 결함이 답을 바꾸지 않는다는 점이 중요하다. pop 시점 표시로 짜도 최단 거리는 맞게 나온다 — 같은 정점이 여러 번 들어가지만 첫 번째 것이 가장 짧기 때문이다. 드러나는 것은 큐 최대 길이와 push 횟수뿐이다.

그래서 검사가 답만 보면 안 된다. `pushes == 6`(정점 수)을 단언한다. 계층 불변식도 함께 본다 — 모든 간선의 두 끝 거리 차가 1 이하다.

## 3. 반복 DFS와 반복자 위치

재귀 DFS는 깊은 경로 그래프에서 스택을 넘긴다. 반복 판으로 옮길 때 흔한 실수가 간선 반복자 위치를 저장하지 않는 것이다.

<!-- snippet: src/algo/graph.hpp@graph-dfs mode=partial -->
```cpp
// 반복 DFS. 재귀 대신 명시적 스택을 쓰되, *간선 반복자 위치* 를 함께 저장한다.
// 위치를 저장하지 않고 매번 처음부터 훑으면 같은 정점을 여러 번 열어 O(V*E) 가 된다.
// tin/tout 은 괄호 구조를 이룬다 — u 가 v 의 조상 <=> tin[u] < tin[v] && tout[v] < tout[u].
inline DfsResult DfsIterative(const Csr& g, measure::Counters* c = nullptr) {
    // ...
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
```

괄호 구조는 검사가 직접 확인한다. 모든 정점 쌍 (u, v)에 대해 세 관계 중 정확히 하나가 성립해야 한다 — u가 v를 품거나, v가 u를 품거나, 두 구간이 서로소거나.

## 4. 순환 판정은 간선으로

무방향 그래프의 순환 판정에서 "부모로 돌아가는 간선은 무시"를 *정점*으로 구현하면 평행 간선을 놓친다. u와 v 사이에 간선이 둘 있으면 그 자체가 순환인데, 두 번째 간선을 타고 v로 갈 때 "부모가 u니까 무시"가 되어 버린다.

<!-- snippet: src/algo/graph.hpp@graph-cycle-undirected -->
```cpp
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
```

방향 그래프는 다른 문제다. 회색(스택 위)과 검은색(끝난)을 구분해야 한다 — 이미 완성된 정점으로 가는 간선은 순환이 아니다.

Kahn 위상 정렬은 진입 차수 0인 정점을 꺼내며 진행한다. 결과가 전체 정점 수보다 짧으면 남은 부분에 순환이 있다.

<!-- snippet: src/algo/graph.hpp@graph-topo mode=partial -->
```cpp
// Kahn 위상 정렬. 진입 차수 0 인 정점을 꺼내고, 그 정점이 없앤 간선만큼 이웃의
// 차수를 줄인다. 결과가 전체 정점 수보다 짧으면 남은 부분에 순환이 있다.
// tie-break 를 최소 정점 번호로 고정해 출력이 결정적이다 — 그러지 않으면 같은
// 입력이 실행마다 다른(둘 다 유효한) 순서를 낸다.
struct TopoResult {
    std::vector<uint32_t> order;
    bool acyclic = false;
};
```

tie-break를 고정하는 것은 정확성이 아니라 재현성의 문제다. 위상 순서는 여러 개가 유효하고, 고정하지 않으면 골든 출력을 만들 수 없다.

## 5. SCC 두 팔과 condensation

Tarjan의 low-link는 "이 정점의 부분 트리에서 back edge로 닿을 수 있는 가장 작은 tin"이다. 갱신에 쓸 간선은 **스택 위에 있는** 정점으로 가는 것뿐이다.

<!-- snippet: src/algo/graph.hpp@graph-tarjan mode=partial -->
```cpp
// Tarjan low-link. low[v] = v 의 부분 트리에서 back edge 로 닿을 수 있는 가장
// 작은 tin. 스택 위에 있는 정점으로 가는 간선만 low 갱신에 쓴다 — 이미 완성된
// 다른 SCC 로 가는 간선을 섞으면 서로 다른 성분이 하나로 붙는다.
// ...
                if (tin[v] == -1) {
                    tin[v] = low[v] = clock++;
                    stack.push_back(v);
                    on_stack[v] = 1;
                    call.push_back({v, g.start[v]});
                } else if (on_stack[v]) {
                    low[u] = std::min(low[u], tin[v]);   // ← 스택 위일 때만
                }
```

Kosaraju는 전혀 다른 골격이다. 원 그래프에서 종료 시각 내림차순을 얻고, 역그래프를 그 순서로 훑는다. 두 번의 DFS와 역그래프 한 벌이 필요하다.

두 팔은 같은 *분할*을 내야 한다. 성분 번호는 다를 수 있으므로 검사는 번호가 아니라 "u와 v가 같은 성분인가"를 모든 쌍에서 대조한다.

condensation이 DAG라는 계약은 검사가 직접 만든다 — 성분 사이 간선만 모아 새 그래프를 짓고 `HasCycleDirected`로 확인한다. 이 검사는 그래프를 손으로 고르지 않고 property 층에서 무작위 그래프 420개에 대해서도 돈다.

## 6. 절단점과 다리, 한 글자 차이

두 조건이 부등호 하나 차이다.

<!-- snippet: src/algo/graph.hpp@graph-articulation mode=partial -->
```cpp
// 절단점과 다리. 두 조건이 *한 글자* 차이다.
//   다리   low[v] >  tin[u]     자식이 u 위로 못 올라간다 — 그 간선이 유일한 통로
//   절단점 low[v] >= tin[u]     자식이 u 까지만 올라간다 — u 를 지우면 끊어진다
// `>=` 를 `>` 로 쓰면 절단점 판정이 다리 판정이 되어, 다리가 아닌 절단점을 놓친다.
// 뿌리는 예외다 — 자식이 둘 이상일 때만 절단점이다.
struct CutResult {
    std::vector<uint8_t> articulation;
    std::vector<uint32_t> bridges;   // edge id 목록
};
```

이 결함이 어렵게 잡히는 이유가 있다. "삼각형 + 꼬리" 같은 흔한 모양에서는 절단점의 자식 간선이 동시에 다리이므로 `>`로 써도 결과가 같다. 틀리는 것은 **다리가 아닌 절단점**이 있는 모양뿐이다.

```cpp
// 나비넥타이. 삼각형 둘이 정점 2 에서만 만난다 — 2 는 절단점이지만
// *다리는 하나도 없다*. `low[v] >= tin[u]` 를 `>` 로 쓴 구현은 다른
// 모양에서는 전부 맞히고 정확히 여기서만 절단점을 놓친다.
{5, {{0, 1, 1}, {1, 2, 1}, {2, 0, 1}, {2, 3, 1}, {3, 4, 1}, {4, 2, 1}}},
```

이 사례는 처음부터 있던 것이 아니다. 결함을 주입했더니 고정 사례 여섯 개가 전부 통과하고 property 층에서만 죽었다 — 무작위 그래프는 나비넥타이를 만들지만 손으로 고른 여섯은 만들지 않았던 것이다. 그래서 사례를 하나 추가했다.

오라클은 low-link 논증을 전혀 쓰지 않는다. 정점을 실제로 지우고 BFS를 다시 돌려 성분 수가 늘었는지 본다 — 절단점의 정의 그대로다.

<!-- snippet: tests/oracle.h#SlowArticulation -->
```cpp
inline std::vector<uint8_t> SlowArticulation(size_t n,
                                             const std::vector<algo::Csr::Edge>& edges) {
    std::vector<uint8_t> out(n, 0);
    const size_t base = ComponentsWithout(n, edges, n);   // 아무것도 안 지운 성분 수
    for (size_t v = 0; v < n; ++v)
        out[v] = (ComponentsWithout(n, edges, v) > base) ? 1u : 0u;
    return out;
}
```

## 7. DSU 네 조합과 정직한 경계

DSU는 두 최적화를 각각 끌 수 있다.

<!-- snippet: src/algo/graph.hpp@graph-dsu mode=partial -->
```cpp
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
    // ...
};
```

여기서 무력화 변이의 정직한 경계를 하나 적어 둔다. **union by size를 끄는 것은 어떤 정확성 검사도 죽이지 않는다.** 네 조합 전부가 같은 연결 판정을 내고, 검사는 BFS 성분과의 일치만 본다. 최적화는 정의상 답을 바꾸지 않으므로 답을 보는 검사로는 잡을 수 없다.

그래서 이 lab은 그것을 검사로 잡으려 하지 않고 *측정*으로 드러낸다. 벤치가 평균 find 깊이를 찍고, 그 수치가 네 조합에서 갈린다. 검사가 못 잡는 것을 검사가 잡는 척하지 않는 것이 이 lab의 규칙이다.

MST는 두 팔이다. Kruskal은 간선을 비용 순으로 보며 DSU로 사이클을 판정하고, Prim의 dense 판은 힙 없이 V² 로 돈다. 오라클은 모든 간선 부분집합을 열거해 크기 n-1인 연결 집합 중 최소 비용을 찾는다 — E ≤ 20에서만 쓸 수 있고, 그것이 오라클의 사용 규칙이다.

## 8. 측정과 관측

### DSU 네 조합

`algo_bench --graph`가 정점 2만 개를 사슬로 합친 뒤 전 정점을 Find 한 결과다.

| arm | 평균 find 깊이 | union+find ms |
|---|---|---|
| compress=0 by_size=0 | 9,999.50 | 258.22 |
| compress=0 by_size=1 | 1.00 | 0.07 |
| compress=1 by_size=0 | 2.00 | 0.10 |
| compress=1 by_size=1 | 1.00 | 0.05 |

**union 순서가 이 표를 만든다.** `Union(i-1, i)`로 붙이면 기존 뿌리가 그대로 남고 새 정점이 그 밑에 잎으로 붙어 네 조합이 전부 깊이 1을 낸다. `Union(i, i-1)`로 뒤집어야 기존 뿌리가 새 정점 밑으로 들어가 사슬이 자란다. 최적화를 끈 팔이 실제로 깊어지는 배치가 이것뿐이라, 워크로드를 잘못 잡으면 "네 조합이 다 똑같다"는 잘못된 결론이 나온다.

깊이 9,999.5는 정확히 (n-1)/2다 — 완전한 사슬의 평균 깊이. 그리고 이 팔이 258ms가 되는데, union 루프 자체는 O(n)이고(새 정점과 현재 뿌리는 모두 깊이 0) 그 뒤 전 정점 Find 스윕이 사슬을 통째로 걷는 O(n²)이기 때문이다. 이 팔만 규모를 2만으로 줄여야 *끝난다*는 사실 자체가 alpha(n) 서사의 절반이다.

경로 압축만 켜도 깊이가 2로 떨어진다. 압축이 Find 한 번에 사슬을 통째로 평평하게 만들기 때문이다.

### SCC와 MST

| arm | ms | components |
|---|---|---|
| tarjan | 2.09 | 399 |
| kosaraju | 3.05 | 399 |

Kosaraju가 1.5배 느리다. DFS를 두 번 돌고 역그래프를 통째로 짓기 때문이다 — 메모리도 간선 수만큼 더 쓴다. 같은 답을 내는 두 알고리즘의 차이가 상수와 공간에만 있다.

MST의 승자는 밀도가 정한다.

| V | E | 밀도 | kruskal ms | prim ms | cost |
|---|---|---|---|---|---|
| 900 | 8,049 | 2% | 0.75 | 6.92 | 602,250 |
| 900 | 81,113 | 20% | 8.73 | 8.19 | 59,951 |
| 900 | 404,550 | 100% | 57.02 | 7.39 | 12,212 |

희소에서는 Kruskal이 9배 빠르고, 완전 그래프에서는 Prim이 8배 빠르다. 교차점은 밀도 20% 근처다. 이유는 각 알고리즘이 의존하는 양이 다르기 때문이다 — Kruskal은 E log E(정렬)에 지배되고 Prim의 dense 판은 E와 무관하게 V²다. E가 V²에 가까워지면 정렬 비용이 V² log V가 되어 진다.

Prim의 dense 판은 V² 행렬을 잡으므로 V=20,000 그래프에서는 아예 돌릴 수 없다. 그것이 이 팔의 성질이고, 표가 그것을 감추지 않도록 큰 그래프의 MST 행에는 Kruskal만 올린다.

### 무력화 변이

**절단점 조건 `>=`를 `>`로.** 다리 조건과 혼동한 형태다.

```text
  ok   graph_bfs_dfs_invariants
  ok   graph_cycle_and_topo
  ok   graph_scc_arms
  FAIL graph_articulation_bridges
  ok   graph_dsu_arms
  ok   graph_mst_arms
```

앞서 적었듯 이 변이는 처음에 고정 사례를 전부 통과했다. 나비넥타이 사례를 추가한 뒤에야 고정 사례에서도 죽는다. 무력화 변이가 검사 자체를 개선한 사례다. 복원 뒤 소스 SHA-256이 주입 전과 동일함을 확인했다.

### 실행

```powershell
cmake --build build --config Release
.\build\Release\algo_tests.exe --step=04
```

```text
  ok   graph_bfs_dfs_invariants
  ok   graph_cycle_and_topo
  ok   graph_scc_arms
  ok   graph_articulation_bridges
  ok   graph_dsu_arms
  ok   graph_mst_arms
algo_tests[step04]: 6/6 passed (349 check(s))
```

```powershell
.\build\Release\algo_tests.exe --properties --step=04
```

```text
  ok   graph/structure-agrees       표본   420 · 비퇴화  53% · 건너뜀 0
algo_tests[properties]: 1/1 passed (총 420 표본, seed 20260824)
```

이 property 하나가 무작위 그래프마다 SCC 두 팔의 분할 일치, condensation의 DAG 성질, 절단점·다리의 정의 대조, DSU와 BFS 성분의 일치를 함께 확인한다.

---

## 결론

그래프 알고리즘의 계약은 대부분 "답이 맞는가"보다 좁다. BFS의 표시 시점은 답을 바꾸지 않고 큐 길이만 바꾸며, union by size는 어떤 정확성 검사도 죽이지 않는다. 그래서 이 단계의 검증은 두 갈래로 나뉘었다 — 정의 그대로의 느린 오라클과 대조하는 검사, 그리고 검사가 볼 수 없는 것을 드러내는 측정.

절단점과 다리의 부등호 하나 차이는 나비넥타이 모양에서만 드러났다. 손으로 고른 사례 여섯 개가 그 모양을 담지 못했고 무작위 property 층이 먼저 잡았다는 사실이, 두 층을 함께 두는 이유를 그대로 보여 준다.

DSU 네 조합의 평균 find 깊이는 워크로드의 union 순서에 완전히 종속됐다. 순서를 뒤집으면 네 조합이 전부 깊이 1을 낸다 — 측정이 무엇을 재고 있는지 모르면 "차이가 없다"는 결론이 쉽게 나온다.

여기서 만든 그래프는 정적이다. 간선 삽입·삭제가 섞이는 동적 연결성과, 메모리에 다 담기지 않는 규모의 외부 그래프 순회는 구현 범위 밖이다.
