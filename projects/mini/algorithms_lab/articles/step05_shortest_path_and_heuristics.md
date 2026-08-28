## 들어가며

최단 경로 알고리즘 각각의 *보장*은 간선 비용의 성질에서 나온다. 그래서 이 단계의 검증은 "빠른가"가 아니라 "그 성질이 깨졌을 때 무엇이 무너지는가"를 본다.

| 알고리즘 | 전제 | 전제가 깨지면 |
|---|---|---|
| BFS | 모든 간선 비용이 같다 | 거리가 아니라 홉 수를 센다 |
| 0-1 BFS | 비용이 0 또는 1이다 | deque 안 두 값 불변식이 깨진다 |
| DAG relaxation | 순환이 없다 | 위상 순서가 없어 알고리즘이 정의되지 않는다 |
| Dijkstra | 비용이 음이 아니다 | 확정된 정점이 나중에 더 좋아진다 |
| Bellman-Ford | 없음 | 음수 순환이면 최단 경로 자체가 정의되지 않는다 |
| A* | admissible | 최적이 아닌 답을 낸다 |

Floyd-Warshall은 오라클로만 쓴다 — 골격이 완전히 달라 같은 오해를 공유하지 않는다. 그래서 제품 트리(`src/algo/paths.hpp`)가 아니라 검사 쪽인 `tests/oracle.h`에 `oracle::SlowAllPairs`로 둔다.

---

## 목차
1. [확정 논리와 그 근거](#1-확정-논리와-그-근거)
2. [음수 간선 반례](#2-음수-간선-반례)
3. [오버플로 가드](#3-오버플로-가드)
4. [A*의 세 손잡이](#4-a의-세-손잡이)
5. [admissible과 consistent의 차이](#5-admissible과-consistent의-차이)
6. [경로 유효성 검사기](#6-경로-유효성-검사기)
7. [측정과 관측](#7-측정과-관측)

---

## 1. 확정 논리와 그 근거

Dijkstra가 하는 일의 핵심은 "한 번 꺼낸 정점은 다시 좋아질 수 없다"고 **믿는** 것이다. 그 믿음의 근거는 비용이 음이 아니라는 것뿐이다. 이 lab은 그 믿음을 토글로 열어 둔다.

<!-- snippet: src/algo/paths.hpp@paths-dijkstra mode=partial -->
```cpp
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
```

우선순위 감소 연산 대신 항목을 새로 넣고 꺼낼 때 낡은 것을 버리는 것(lazy deletion)이 이 구현의 형태다. `skip_stale`을 끄면 정확성은 그대로이고 같은 정점을 여러 번 확장하기만 한다 — 답을 보는 검사로는 잡을 수 없고 확장 수로만 드러난다.

## 2. 음수 간선 반례

음수 간선이 하나만 있어도 확정 논리가 깨진다는 것을 반례로 못 박는다.

```cpp
// S->A: 1, S->B: 2, B->A: -2. 진짜 최단 dist(A) = 0 이다.
const std::vector<Csr::Edge> edges = {{0, 1, 1}, {0, 2, 2}, {2, 1, -2}};
```

Dijkstra는 A를 거리 1로 먼저 꺼내 확정한다. 나중에 B(거리 2)를 꺼내 A를 0으로 줄일 수 있음을 발견하지만, 이미 확정했으므로 무시한다.

검사는 **틀린 답이 나오는 것을 그대로 단언한다.**

```cpp
DijkstraOptions strict;
strict.final_on_settle = true;
const PathResult wrong = DijkstraHeap(g, 0, strict);
CHECK(wrong.dist[1] == 1);          // 틀린 답이 나오는 것을 그대로 단언한다
CHECK(wrong.dist[1] != bf.dist[1]);
```

확정을 포기하면(계속 완화하면) 답은 맞는다. 그러나 그것은 이미 Dijkstra가 아니다 — 대가가 확장 수로 나타나고, 검사는 `loose.settled > wrong.settled`를 단언해 그 대가를 명시한다.

Bellman-Ford는 전제가 없는 대신 판정을 낸다. n-1 라운드면 어떤 최단 경로도 확정되고(단순 경로의 간선 수 상한), n번째 라운드에서 여전히 줄어드는 간선이 있으면 음수 순환이 있다.

<!-- snippet: src/algo/paths.hpp@paths-bellman-ford mode=partial -->
```cpp
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
```

## 3. 오버플로 가드

`if (r.dist[e.u] >= kInfCost) continue;` 한 줄이 무엇을 막는지는 도달 불가 정점이 있어야 보인다.

```cpp
// 0 에서 1 로만 갈 수 있고, 2->3 은 도달 불가 영역의 음수 간선이다.
const std::vector<Csr::Edge> edges = {{0, 1, 5}, {2, 3, -1000}};
const PathResult r = BellmanFord(4, edges, 0);
CHECK(r.dist[1] == 5);
CHECK(r.dist[2] == kInfCost);
// 가드가 없으면 dist[3] = kInfCost - 1000 이 되어 *도달 가능해 보인다*.
CHECK(r.dist[3] == kInfCost);
```

Bellman-Ford는 도달 가능성과 무관하게 모든 간선을 훑으므로 이 가드가 실제로 일한다. 무한대를 `INT64_MAX`가 아니라 `INT64_MAX / 4`로 잡은 것도 같은 이유다 — 가드를 빠져나간 덧셈이 있어도 부호가 뒤집히지 않을 여유를 둔다.

## 4. A*의 세 손잡이

격자 A*는 휴리스틱·가중치·재개방 정책을 전부 토글로 연다. 이 셋의 조합이 "최적성"과 "확장 수" 사이의 교환을 그대로 만든다.

<!-- snippet: src/algo/paths.hpp@paths-astar-grid mode=partial -->
```cpp
struct AStarOptions {
    Heuristic heuristic = Heuristic::Manhattan;
    double    weight    = 1.0;    // f = g + w*h. w>1 이면 최적성을 잃는 대신 빨라진다
    bool      reopen    = true;   // closed 정점을 더 좋은 g 로 다시 열 것인가
    int       tie_break = 0;      // 0: f 만 · 1: f 다음 h 작은 것 · 2: f 다음 g 큰 것
};
```

tie-break를 우선순위 키 *안에* 접어 넣는 것이 중요하다. 힙 바깥에서 정렬하면 힙 순서와 어긋난다.

```cpp
// 우선순위 키를 하나의 정수로 접는다. tie-break 를 키 안에 넣어야 힙이
// 그것을 실제로 존중한다 — 밖에서 정렬하면 힙 순서와 어긋난다.
auto Key = [&](uint32_t id) -> int64_t {
    const int64_t f = g[id] + H(id);
    if (opt.tie_break == 1) return f * 1000000 + H(id);
    if (opt.tie_break == 2) return f * 1000000 - g[id];
    return f * 1000000;
};
```

대각 이동을 정수 비용으로 유지하려고 직교 10, 대각 14(≈10√2)를 쓴다. 실수 비용은 비교의 동점 판정을 흐리게 만들어 tie-break 실험을 무의미하게 만든다.

## 5. admissible과 consistent의 차이

admissible은 "h가 실제 남은 비용을 넘지 않는다"이고, consistent는 "모든 간선에서 h(u) ≤ cost(u,v) + h(v)"다. consistent이면 admissible이지만 역은 아니다.

admissible이지만 inconsistent한 휴리스틱은 최적해를 잃지 *않는다* — 재개방을 허용하는 한. 재개방을 끄면 잃는다. 그것을 격자 거리 공식으로는 만들 수 없어서, 임의 그래프 위의 A*를 따로 짓는다.

<!-- snippet: src/algo/paths.hpp@paths-astar-graph mode=partial -->
```cpp
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
```

반례의 구조는 이렇다.

```cpp
//  S=0, A=1, B=2, G=3
//  0->1 (3)   0->2 (1)   2->1 (1)   1->3 (10)
//  최적: 0->2->1->3 = 12.  나쁜 길: 0->1->3 = 13.
const std::vector<Csr::Edge> edges = {{0, 1, 3}, {0, 2, 1}, {2, 1, 1}, {1, 3, 10}};
//  h*(A)=10 인데 h(A)=0 으로 느슨하게 두고, h(B)=6 은 h*(B)=11 이하라 admissible.
//  그러나 h(B)=6 > cost(B,A) + h(A) = 1 이라 inconsistent 하다.
const std::vector<int64_t> h = {0, 0, 6, 0};
```

왜 h(A)가 느슨해야 하는지에는 이유가 있다. 비일관을 만들려면 어떤 간선 (Y, X)에서 `h(Y) > cost(Y,X) + h(X)`여야 하는데, admissible이 동시에 요구하는 것은 `h(Y) ≤ cost(Y,X) + h*(X)`다. 두 부등식이 함께 성립하려면 `h(X) < h*(X)`가 반드시 필요하다. 그래서 "느슨한 h가 하나 있어야 한다"는 것이 구조적 요구다.

검사가 단언하는 것은 셋이다.

```cpp
CHECK(!ConsistentOnGraph(g, h));
CHECK(with_reopen.cost == 12);      // 최적
CHECK(no_reopen.cost == 13);        // 재개방을 끄면 잃는다
```

그리고 consistent한 휴리스틱에서는 두 정책이 같은 답을 낸다는 것도 함께 본다 — 재개방이 필요 없다는 것이 consistent의 정의적 귀결이기 때문이다.

## 6. 경로 유효성 검사기

알고리즘이 낸 비용만 믿으면 안 된다. 경로가 실제로 시작에서 끝나고, 인접 칸만 잇고, 막힌 칸을 지나지 않고, 보고한 비용과 같은지 독립적으로 확인한다.

<!-- snippet: src/algo/paths.hpp#ValidateGridPath -->
```cpp
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
```

벤치도 이 검사기를 부른다. 팔이 틀린 경로를 내면 표에 오르기 전에 프로세스가 죽는다.

## 7. 측정과 관측

### 최단 경로 네 팔

`algo_bench --paths`, 정점 1,200개.

| 밀도 | arm | ms | settled | pushes | stale pops |
|---|---|---|---|---|---|
| 2% | dijkstra(heap) | 0.33 | 1,200 | 3,322 | 2,122 |
| 2% | dijkstra(dense) | 1.38 | 1,200 | 0 | 0 |
| 2% | bellman-ford | 0.23 | 9 라운드 | 0 | 0 |
| 50% | dijkstra(heap) | 3.28 | 1,200 | 6,872 | 5,672 |
| 50% | dijkstra(dense) | 1.91 | 1,200 | 0 | 0 |
| 50% | bellman-ford | 6.06 | 8 라운드 | 0 | 0 |

밀도가 승자를 바꾼다. 2%에서는 힙 판이 dense 판의 4배 빠르고, 50%에서는 dense 판이 힙 판보다 1.7배 빠르다. 힙 판이 미는 항목 수(pushes)가 간선 수에 비례하는 반면 dense 판은 V²로 고정이기 때문이다.

stale pop이 push의 60% 이상이라는 점이 눈에 띈다. lazy deletion의 대가가 이만큼이고, 그것이 우선순위 감소를 지원하는 힙(Fibonacci 등)이 존재하는 이유다. 다만 그 힙의 상수가 커서 실측에서 이기는 경우는 드물다.

Bellman-Ford의 `settled` 칸은 라운드 수다. 조기 종료가 있어서 이 그래프에서는 8~9 라운드 만에 끝난다 — 최악인 n-1 = 1,199 라운드와는 거리가 멀다. 밀도 2%에서 Bellman-Ford가 가장 빠른 것은 그 때문이고, 이는 "Bellman-Ford는 느리다"는 요약이 워크로드에 따라 틀릴 수 있음을 보여 준다.

### A*의 세 손잡이

260×260 격자, 20% 차단, 최적 비용 5,180.

| heuristic | weight | reopen | ms | expanded | reopened | cost/opt |
|---|---|---|---|---|---|---|
| zero | 1.0 | 1 | 3.17 | 53,859 | 0 | 1.0000 |
| manhattan | 1.0 | 1 | 1.37 | 16,610 | 0 | 1.0000 |
| manhattan | 1.5 | 1 | 0.23 | 768 | 12 | 1.0772 |
| manhattan | 1.5 | 0 | 0.24 | 758 | 0 | 1.0849 |
| manhattan | 3.0 | 1 | 0.22 | 691 | 3 | 1.1120 |
| euclidean | 1.0 | 1 | 3.76 | 51,150 | 0 | 1.0000 |
| euclidean | 1.5 | 1 | 0.24 | 754 | 0 | 1.0000 |

**휴리스틱 0은 Dijkstra다.** 열린 칸 전부(약 54,000)를 확장한다.

**Manhattan은 확장을 3.2배 줄이고 여전히 최적이다.** 4방향 격자에서 Manhattan은 tight하고 consistent하므로 최적성을 잃지 않는다.

**가중치 1.5가 확장을 21배 더 줄인다.** 16,610 → 768. 대가는 비용 7.7% 초과다. 이 교환비 — 21배 빠르고 8% 나쁜 경로 — 가 게임 경로 탐색에서 weighted A*를 쓰는 이유 전체다.

**Euclidean은 4방향 격자에서 손해다.** Manhattan보다 느슨해서(대각선 거리를 재는데 대각 이동은 못 한다) 확장이 3배 많고 시간도 2.7배다. 휴리스틱은 admissible한 것 중 *가장 tight한* 것이 좋다.

**재개방 토글의 효과는 이 격자에서 작다.** Manhattan w=1.5에서 재개방 12회, 비용 차이 0.7%p. w=1일 때는 재개방이 0이다 — consistent 휴리스틱이므로 원래 필요 없다. 이 토글이 크게 작용하는 것은 inconsistent 휴리스틱일 때이고, 그것은 위의 4정점 반례가 12 대 13으로 보여 준다.

tie-break는 별도 표다.

| tie-break | ms | expanded | queue peak |
|---|---|---|---|
| f only | 1.59 | 16,610 | 4,605 |
| f then smaller h | 0.39 | 2,376 | 1,430 |

같은 최적 비용에서 확장이 7배 줄었다. f가 같은 항목이 많을 때 목표에 가까운 쪽을 먼저 여는 것만으로 이만큼 달라진다. 비용 한 줄로 얻는 이득 중 가장 큰 축에 속한다.

### 무력화 변이

**변이 1 — Bellman-Ford의 도달 불가 가드 제거.** `if (r.dist[e.u] >= kInfCost) continue;` 첫 줄을 지웠다.

```text
  ok   paths_arms_agree
  ok   paths_negative_edge
  FAIL paths_overflow_guard
  ok   paths_astar_grid
  ok   paths_inconsistent_heuristic
```

다른 검사는 전부 살아남는다. 도달 불가 정점에서 나가는 음수 간선이라는 특정 모양에서만 드러나기 때문이다.

**변이 2 — A*의 재개방 금지 줄을 항상 통과시키기.** `if (closed[v] && !reopen) continue;`를 `if (false) continue;`로 바꿔 정책 토글 자체를 무력화했다. `paths_inconsistent_heuristic`이 죽는다 — 재개방을 끈 팔이 13이 아니라 12를 내므로, 정책이 실제로 작동한다는 단정이 깨진다.

두 변이 모두 복원 뒤 소스 SHA-256이 주입 전과 동일함을 확인했다.

### 실행

```powershell
cmake --build build --config Release
.\build\Release\algo_tests.exe --step=05
```

```text
  ok   paths_arms_agree
  ok   paths_negative_edge
  ok   paths_overflow_guard
  ok   paths_astar_grid
  ok   paths_inconsistent_heuristic
algo_tests[step05]: 5/5 passed (2071 check(s))
```

```powershell
.\build\Release\algo_tests.exe --properties --step=05
```

```text
  ok   paths/vs-floyd               표본   420 · 비퇴화  55% · 건너뜀 0
algo_tests[properties]: 1/1 passed (총 420 표본, seed 20260824)
```

이 property는 무작위 그래프마다 Dijkstra 두 팔과 Bellman-Ford가 Floyd-Warshall의 전 쌍 거리와 일치하는지, 그리고 음수 순환이 없다고 올바르게 판정하는지를 모든 출발점에서 확인한다.

---

## 결론

각 최단 경로 알고리즘이 보장하는 것은 전제와 짝지어져 있다. 음수 간선 하나가 Dijkstra의 확정 논리를 깨는 것을 3정점 반례로 확정했고, 그때 나오는 틀린 값을 검사가 그대로 단언하도록 두었다. 확정을 포기하면 답은 맞지만 확장 수가 늘고, 그 대가도 수치로 남겼다.

A*의 세 손잡이 중 실측에서 가장 크게 작용한 것은 가중치와 tie-break였다. 가중치 1.5가 확장을 21배 줄이고 비용을 7.7% 올렸고, tie-break를 f 다음 h로 바꾸는 것만으로 최적성을 유지한 채 확장이 7배 줄었다. 재개방 정책은 이 격자에서 거의 작동하지 않았는데, 그 이유가 "휴리스틱이 consistent라 원래 필요 없다"는 것이고, 그것이 필요해지는 경우는 4정점 인공 반례로 따로 보였다.

여기서 다루지 않은 것은 전처리 기반 가속(contraction hierarchies, ALT 랜드마크)과 양방향 탐색이다. 그것들은 질의 전에 그래프를 오래 전처리해 두는 형태이고, 이 lab의 팔은 전부 질의마다 처음부터 시작한다.
