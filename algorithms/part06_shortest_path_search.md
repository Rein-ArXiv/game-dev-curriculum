## 들어가며

최단 경로는 “가장 가까운 길” 하나가 아니다. 간선 수가 최소인지, 이동 시간·위험·자원 소비의 합이 최소인지, 음수 비용이 가능한지에 따라 알고리즘의 전제가 달라진다. Dijkstra에 음수 간선을 넣거나, A*의 휴리스틱을 이동 규칙과 다르게 만들면 코드는 종료해도 최적 경로 보장은 사라진다.

게임 탐색은 최적성만으로 평가할 수도 없다. 수천 agent가 공유 목표로 이동하거나, 월드가 바뀌거나, 한 프레임의 확장 수가 제한될 수 있다. 이 경우 역방향·다중 출발점 탐색, 계층화, 재계획, weighted search가 비용과 품질을 교환한다. 교환이 정당하려면 어떤 보장을 포기했고 품질 차이를 어떻게 측정하는지 명시해야 한다.

이 글은 최단 경로 문제의 비용 대수를 정의하고 BFS, DAG 최단 경로, Dijkstra, Bellman-Ford, Floyd-Warshall을 비교한다. A*의 admissibility와 consistency를 증명하고, 재개방·양방향 탐색·증분 탐색·시간 예산이 정확성에 미치는 영향을 분석한다.

---

## 목차
1. [최단 경로의 명세](#1-최단-경로의-명세)
2. [비가중·DAG 최단 경로](#2-비가중dag-최단-경로)
3. [Dijkstra 알고리즘](#3-dijkstra-알고리즘)
4. [음수 간선과 Bellman-Ford](#4-음수-간선과-bellman-ford)
5. [모든 쌍 최단 경로](#5-모든-쌍-최단-경로)
6. [A*와 휴리스틱](#6-a와-휴리스틱)
7. [재개방과 Weighted A*](#7-재개방과-weighted-a)
8. [다중 출발점·역방향·양방향 탐색](#8-다중-출발점역방향양방향-탐색)
9. [동적 그래프와 시간 예산](#9-동적-그래프와-시간-예산)
10. [경로 품질과 구현 비용](#10-경로-품질과-구현-비용)
11. [검증과 반례](#11-검증과-반례)

---

## 1. 최단 경로의 명세

가중 방향 그래프 $G=(V,E)$에서 각 간선 $(u,v)$는 비용 $w(u,v)$를 가진다. 경로 $p=(v_0,\dots,v_k)$의 비용은 보통 간선 비용의 합이다.

$$
w(p)=\sum_{i=0}^{k-1}w(v_i,v_{i+1})
$$

시작 $s$에서 $v$까지의 최단 거리 $\delta(s,v)$는 가능한 경로 비용의 최솟값이다. 도달 불가능하면 $\infty$다.

### 비용이 뜻하는 것

비용은 거리일 필요가 없다.

- 이동 시간
- 위험도와 소음
- stamina 소비
- 여러 목적을 정규화한 가중합

서로 단위가 다른 값을 단순 합산하면 weight의 의미와 튜닝 범위를 설명해야 한다. 위험 가중치를 바꾸는 순간 최단의 의미도 달라진다.

### 최적 부분 구조

최단 경로의 subpath도 해당 양 끝 사이의 최단 경로다. 더 싼 subpath가 있다면 원래 경로의 그 구간을 교체해 전체를 더 싸게 만들 수 있기 때문이다.

이 성질이 relaxation을 가능하게 한다. 현재 알려진 거리 `d[u]`를 통해 `v`에 더 싸게 갈 수 있으면

$$
d[v]\leftarrow\min(d[v],d[u]+w(u,v))
$$

로 갱신한다.

### 산술 계약

`INF + weight` overflow, 정수 비용 합의 wrap, 부동소수점 NaN은 비교 의미를 깨뜨린다. 무한 sentinel을 실제 최대값과 구분하고 덧셈 전에 범위를 검사한다. 결정적 실행이 필요하면 비용 표현과 tie-break를 고정한다.

## 2. 비가중·DAG 최단 경로

### BFS

모든 간선 비용이 같은 그래프에서는 BFS의 발견 계층이 최단 거리다. 단위 비용이 아니라 모두 같은 양의 상수 $c$여도 간선 수에 $c$를 곱하면 된다.

0과 1 비용만 있는 그래프는 0-1 BFS를 사용할 수 있다. 비용 0 간선은 deque 앞, 비용 1 간선은 뒤에 넣는다. 정점은 현재 거리 비감소 순서에 가깝게 처리되어 $O(V+E)$에 최단 거리를 구한다.

### DAG 최단 경로

DAG에서는 위상 순서대로 각 간선을 한 번 relax하면 $O(V+E)$이다. 음수 간선이 있어도 cycle이 없으므로 이미 처리한 의존으로 돌아오지 않는다.

```text
topological order를 계산한다.
d[start] = 0, 나머지는 INF로 둔다.
순서대로 u를 방문하며 모든 (u,v)를 relax한다.
```

작업 그래프의 최소 완료 시간이나 animation transition DAG처럼 cycle이 금지된 모델에서 유용하다. 일반 그래프를 DAG로 가정하는 것은 최적화가 아니라 명세 오류다.

## 3. Dijkstra 알고리즘

Dijkstra는 음이 아닌 간선 비용에서 아직 확정되지 않은 정점 중 잠정 거리 `d`가 최소인 정점을 확정한다.

```cpp
using QueueItem = std::pair<std::int64_t, int>;
std::priority_queue<QueueItem, std::vector<QueueItem>,
                    std::greater<QueueItem>> open;

distance[start] = 0;
open.push({0, start});
while (!open.empty()) {
    const auto [du, u] = open.top();
    open.pop();
    if (du != distance[u]) continue;

    for (const WeightedEdge e : graph[u]) {
        if (du > INF - e.weight) continue;
        const auto candidate = du + e.weight;
        if (candidate >= distance[e.to]) continue;
        distance[e.to] = candidate;
        parent[e.to] = u;
        open.push({candidate, e.to});
    }
}
```

Priority queue에 기존 항목을 수정하는 decrease-key가 없으면 개선된 항목을 새로 넣고 stale 항목을 꺼낼 때 버린다. 정확성은 유지되지만 queue 크기와 pop 횟수가 늘어난다.

### 정확성 증명

정점 $u$가 최소 잠정 거리로 처음 확정됐다고 하자. 실제 최단 경로 위에서 아직 확정되지 않은 첫 정점을 $y$, 그 직전 확정 정점을 $x$라 한다. $x$를 처리할 때 $(x,y)$를 relax했으므로

$$
d[y]\le\delta(s,x)+w(x,y)=\delta(s,y)
$$

다. 잠정 거리는 실제 경로 비용이므로 $d[y]\ge\delta(s,y)$이고 결국 같아진다. 간선 비용이 음이 아니므로 최단 경로에서 $y$가 $u$보다 앞이면

$$
\delta(s,y)\le\delta(s,u)
$$

다. $u$가 최소 잠정 거리로 선택됐으므로 $d[u]\le d[y]$다. 부등식을 모으면

$$
\delta(s,u)\le d[u]\le d[y]=\delta(s,y)\le\delta(s,u)
$$

이어서 $d[u]=\delta(s,u)$다.

음이 아닌 비용은 경로를 더 진행해도 비용이 줄지 않는다는 증명의 핵심 전제다.

### 복잡도

Binary heap과 인접 리스트를 사용하면 $O((V+E)\log V)$로 표현할 수 있다. Stale duplicate를 허용하면 queue 항목이 $O(E)$까지 생길 수 있어 $O(E\log E)$로 보는 편이 구현에 가깝다. Dense graph에서는 단순 배열로 최소 정점을 찾는 $O(V^2)$ 구현이 heap보다 나을 수 있다.

## 4. 음수 간선과 Bellman-Ford

음수 간선 하나만 있어도 Dijkstra의 확정 논리가 깨진다.

```text
S -> A: 1
S -> B: 2
B -> A: -2
```

`A`를 거리 1로 먼저 확정하지만 실제 최단은 `S->B->A`의 0이다.

### Bellman-Ford

간선 수가 최대 $k$인 최단 경로는 모든 간선을 $k$번 relax하면 반영된다. 단순 경로는 최대 $V-1$개 간선을 가지므로 모든 간선을 $V-1$회 relax하면 음수 cycle이 없는 최단 거리를 얻는다.

```cpp
for (int pass = 0; pass < vertexCount - 1; ++pass) {
    bool changed = false;
    for (const WeightedEdgeFull& e : edges) {
        if (distance[e.from] == INF) continue;
        const auto candidate = distance[e.from] + e.weight;
        if (candidate < distance[e.to]) {
            distance[e.to] = candidate;
            parent[e.to] = e.from;
            changed = true;
        }
    }
    if (!changed) break;
}
```

$V$번째 pass에서도 개선되는 정점이 있으면 시작점에서 도달 가능한 음수 cycle의 영향을 받는다. 음수 cycle을 반복하면 비용을 무한히 낮출 수 있어 유한 최단 경로가 존재하지 않는다.

### 음수 비용의 모델 점검

게임 이동 비용에 보상을 음수로 섞어 cycle을 만들면 “좋은 보상 경로”가 아니라 문제 정의가 무한 반복을 선호하게 될 수 있다. 비용을 모두 비음수로 재구성하거나, 방문 횟수·자원 상태를 정점에 포함해 cycle 의미를 제한해야 한다.

## 5. 모든 쌍 최단 경로

### Floyd-Warshall

정점 번호 $0..k-1$만 중간 정점으로 허용한 최단 거리를 $D^{(k)}[i][j]$라 하자.

$$
D^{(k)}[i][j]=\min\left(D^{(k-1)}[i][j],
D^{(k-1)}[i][k]+D^{(k-1)}[k][j]\right)
$$

이 recurrence를 3중 반복으로 계산하면 $O(V^3)$ 시간과 $O(V^2)$ 공간이다. 정점 수가 작고 많은 쌍 질의가 있을 때 단순하고 강한 기준 구현이다.

반복 순서에서 중간 정점 `k`가 가장 바깥이어야 “허용된 중간 정점 집합” 불변식이 유지된다.

```cpp
for (int k = 0; k < n; ++k)
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (d[i][k] != INF && d[k][j] != INF)
                d[i][j] = std::min(d[i][j], d[i][k] + d[k][j]);
```

계산 뒤 `d[v][v] < 0`이면 음수 cycle이 있다.

### 반복 Dijkstra와 Johnson

음이 아닌 sparse graph에서는 모든 정점에서 Dijkstra를 실행할 수 있다. 음수 간선은 있지만 음수 cycle이 없으면 Johnson 알고리즘이 잠재 함수로 간선 비용을 비음수로 재가중한 뒤 Dijkstra를 반복한다.

재가중은 경로 상대 순서를 보존해야 한다. 구현 복잡도가 가치 있는지는 정점·간선 수와 질의 횟수에 달려 있다. 작은 graph에서는 Floyd-Warshall이 더 검증하기 쉽다.

## 6. A*와 휴리스틱

A*는 시작점 누적 비용 $g(n)$과 목표까지 추정 비용 $h(n)$을 더한

$$
f(n)=g(n)+h(n)
$$

이 작은 정점을 먼저 확장한다. `h=0`이면 Dijkstra와 같다.

### Admissibility

휴리스틱이 실제 남은 최단 비용 $h^*(n)$을 과대평가하지 않으면 admissible이다.

$$
0\le h(n)\le h^*(n)
$$

Tree-search A*에서 admissibility는 목표가 queue에서 선택될 때 최적성을 보장한다. 최적 경로 위 frontier 정점 $n$은

$$
f(n)=g(n)+h(n)\le g(n)+h^*(n)=C^*
$$

이므로 비용이 $C^*$보다 큰 목표가 그보다 먼저 선택될 수 없다.

### Consistency

모든 간선 $(u,v)$에 대해

$$
h(u)\le w(u,v)+h(v)
$$

이면 consistent다. 경로를 따라 부등식을 합하면 admissibility를 얻는다. 또한 relaxation 경로에서

$$
f(v)=g(u)+w(u,v)+h(v)\ge g(u)+h(u)=f(u)
$$

이므로 $f$가 비감소한다. Consistent heuristic에서는 정점을 처음 queue에서 꺼낼 때 `g`가 최적이고 closed 정점을 재개방할 필요가 없다.

### Grid 휴리스틱

| 이동 모델 | 비용 | 적합한 거리 |
|---|---|---|
| 4방향 | 직선 1 | Manhattan |
| 8방향 | 직선 1, 대각 $\sqrt2$ | Octile |
| 자유 평면 이동 | Euclidean 거리 | Euclidean |

지형 비용 배율의 최솟값이 $c_{min}$이면 기하 거리에도 $c_{min}$을 곱해야 안전하다. 1보다 싼 지형을 허용하면서 그대로 거리를 사용하면 과대평가할 수 있다.

## 7. 재개방과 Weighted A*

### Inconsistent heuristic

Admissible하지만 inconsistent한 휴리스틱은 가능하다. 이 경우 더 싼 경로가 나중에 closed 정점으로 들어올 수 있다. 구현이 closed 정점을 무조건 건너뛰면 최적성을 잃는다.

```text
S->C: 1, S->A: 3
C->A: 1, A->G: 2
h(C)=3, h(A)=0, h(G)=0
```

`h(C)=3`은 실제 남은 비용 3과 같아 admissible하지만 `C->A`에서 `3 <= 1+0`을 만족하지 않아 inconsistent다. `A`가 비용 3으로 먼저 닫힌 뒤 `C`를 통해 비용 2가 발견된다. 재개방을 허용해야 최적 비용 4를 얻는다.

### Weighted A*

$$
f(n)=g(n)+w h(n),\qquad w>1
$$

로 두면 목표 방향을 더 강하게 선호해 확장을 줄일 수 있다. Admissible heuristic과 적절한 종료 조건에서는 해 비용이 최적의 $w$배 이내라는 bounded-suboptimal 보장을 얻는 형태가 있다. 구현 변형과 재개방 정책에 따라 보장 조건이 달라지므로 단순히 weight만 곱하고 같은 주장을 하면 안 된다.

제품에서는 다음을 측정한다.

```text
확장 정점 수
최적 대비 비용 비율
최악 품질과 seed별 분산
프레임당 확장 budget
경로를 반환하기까지의 wall time
```

## 8. 다중 출발점·역방향·양방향 탐색

### 다중 출발점

여러 목표를 거리 0으로 초기화해 역방향 탐색하면 모든 정점에서 가장 가까운 목표까지의 거리를 한 번에 구한다. 목표가 고정되고 agent가 많을 때 각 agent별 탐색을 반복하는 것보다 유리하다.

방향 graph에서는 간선을 뒤집어야 원래 정점에서 목표로 가는 비용이 맞는다.

### 양방향 탐색

시작과 목표에서 동시에 탐색해 frontier가 만나는 지점을 찾는다. 균일 branching factor가 $b$, 해 깊이가 $d$라면 단방향의 대략 $b^d$ 상태 대신 양쪽 $2b^{d/2}$ 규모를 기대할 수 있다.

실제 종료 조건은 “처음 만남”보다 복잡하다. 가중 graph의 bidirectional Dijkstra는 양쪽 최소 queue key와 현재 최선 연결 비용을 비교해 더 나은 경로가 없음을 확인해야 한다. 역방향 edge 비용과 heuristic의 방향성도 정확히 정의해야 한다.

### Shared search

같은 목표로 이동하는 다수 agent는 목표에서 계산한 distance field나 flow direction을 공유할 수 있다. 월드가 변하면 field 갱신 비용과 오래된 경로의 의미가 생긴다. 공유가 이득인지 여부는 agent 수와 목표 변경 빈도의 함수다.

## 9. 동적 그래프와 시간 예산

### 재탐색과 증분 탐색

그래프가 바뀔 때마다 처음부터 A*를 실행하는 것이 가장 단순한 기준 구현이다. 변경이 탐색 공간의 작은 부분에만 영향을 주고 같은 시작·목표 질의가 반복되면 LPA*나 D* Lite 계열이 이전 search tree의 일관되지 않은 정점만 갱신한다.

증분 알고리즘은 항상 빠른 것이 아니다. 변경이 넓거나 목표가 계속 바뀌면 유지 상태와 priority 갱신 비용이 전체 재탐색보다 클 수 있다.

### 프레임 분할

탐색을 여러 프레임으로 나누려면 open·closed·parent·graph version이 지속 상태가 된다. 중간에 graph가 바뀌면 다음 정책 중 하나가 필요하다.

- snapshot에서 끝까지 계산한다.
- version 불일치 시 결과를 폐기한다.
- 변경을 증분 알고리즘으로 반영한다.

프레임당 정점 확장 수를 고정하면 CPU 작업량은 제한되지만 한 확장의 이웃 수가 크게 다르면 시간 상한은 고정되지 않는다. 시간 budget은 clock 측정 overhead와 재현성 문제를 만들고, 노드 budget은 하드웨어 독립적이지만 실제 시간이 변한다.

### Anytime search

먼저 유효한 경로를 빠르게 만들고 시간이 더 생기면 품질을 개선할 수 있다. 언제 중단해도 반환 경로가 실제 edge로 이어지고 비용 계산이 맞아야 한다. “아직 최적 아님” 상태와 “경로 없음”을 구분한다.

## 10. 경로 품질과 구현 비용

### 경로의 모델 품질

Graph 최단 경로가 실제 이동 최적 경로라는 보장은 representation에 달려 있다. Grid 해상도가 낮으면 계단형 경로가 나오고, waypoint 연결이 부족하면 가능한 지름길을 표현하지 못한다.

경로 smoothing은 waypoint를 줄일 수 있지만 collision-free segment인지 다시 검증해야 한다. Smoothing 뒤 비용이 원래 graph 최단 비용과 달라져도 실제 이동 품질은 좋아질 수 있다.

### Priority queue와 메모리

탐색 비용은 확장 수만이 아니다.

$$
T\approx N_{expand}(C_{neighbors}+C_{heuristic}+C_{queue})
$$

상태 hash, 동적 할당, cache miss가 지배할 수 있다. Grid처럼 정점 ID가 dense하면 배열 distance와 parent가 hash map보다 빠르다. 탐색마다 배열을 초기화하는 비용은 generation stamp로 줄일 수 있다.

### Tie-break

같은 `f`에서 작은 `h` 또는 큰 `g`를 선호하면 목표 쪽 깊은 정점을 먼저 탐색해 확장 수가 달라질 수 있다. 최적성 조건을 유지하는 동률 안에서의 순서지만, 반환되는 동가 경로와 결정성이 달라진다.

재현 가능한 결과가 필요하면 `(f,h,node_id,insertion_sequence)` 같은 전체 순서를 정의한다. 부동소수점 `f`의 플랫폼 차이가 문제면 비용을 고정소수점으로 양자화하는 방안을 검토한다.

## 11. 검증과 반례

### 경로 유효성

반환 경로는 최소한 다음을 만족해야 한다.

```text
첫 정점은 start, 마지막 정점은 goal이다.
모든 인접 정점 쌍에 실제 edge가 있다.
보고한 비용은 edge 비용 합과 같다.
도달 불가를 빈 경로와 명확히 구분한다.
```

### 최적성 oracle

작은 graph에서는 모든 단순 경로 열거 또는 Floyd-Warshall을 oracle로 사용한다.

- 비음수 graph에서 Dijkstra와 Bellman-Ford가 같다.
- `h=0`인 A*와 Dijkstra가 같다.
- admissible·consistent heuristic A*가 Dijkstra와 같은 비용을 낸다.
- DAG 최단 경로가 Bellman-Ford와 같다.
- 모든 edge 비용에 양의 상수를 곱해도 최단 경로 순서는 같다.

### 생성할 반례

- 음수 간선은 있지만 음수 cycle은 없음
- 도달 가능한 음수 cycle과 도달 불가능한 음수 cycle
- 최단 경로가 여러 개이고 비용이 모두 같음
- 매우 긴 0비용 edge chain
- 비용 합이 정수 상한에 가까움
- admissible하지만 inconsistent한 heuristic
- 목표가 없거나 시작과 목표가 같음
- 한 정점의 degree가 매우 큼

### 성능 실험

같은 정점 수라도 graph 구조가 다르면 탐색량이 달라진다. Branching factor, 장애물 밀도, heuristic error, 경로 길이, queue peak, stale pop 수를 함께 기록한다. 무작위 map 평균만으로 좁은 corridor, 막힌 목표, 넓은 빈 공간 같은 대표 실패 형태를 가릴 수 없다.

---

## 결론

최단 경로 알고리즘의 보장은 간선 비용의 성질에서 나온다. BFS는 동일 비용, DAG relaxation은 비순환성, Dijkstra는 비음수 비용, Bellman-Ford는 유한 최단 경로, A*는 휴리스틱의 admissibility와 consistency에 의존한다. 전제를 바꾸면 알고리즘 이름이 같아도 증명은 유지되지 않는다.

게임 탐색에서는 최적 비용과 함께 확장 수, queue 메모리, 결정성, 동적 변경, 프레임 budget을 다뤄야 한다. 양방향·다중 출발점·증분·weighted 탐색은 특정 workload 구조를 이용해 비용을 줄이지만 각각 종료 조건과 품질 계약이 필요하다. 작은 oracle과 의도적인 반례로 경로 유효성과 최적성을 분리해 검증해야 한다.

## 더 읽을거리

- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms*, 4th ed. (MIT Press, 2022), Ch. 22~23 — 단일 출발점·모든 쌍 최단 경로와 정확성 증명.
- Steven S. Skiena, *The Algorithm Design Manual*, 3rd ed. (Springer, 2020), Ch. 8 — weighted graph 문제와 구현 선택의 실무 관점.
- Edsger W. Dijkstra, “A Note on Two Problems in Connexion with Graphs”, *Numerische Mathematik* 1, 1959 — Dijkstra 알고리즘 원전.
- Richard Bellman, “On a Routing Problem”, *Quarterly of Applied Mathematics* 16(1), 1958 — 반복 relaxation 기반 최단 경로의 고전.
- Peter E. Hart, Nils J. Nilsson, Bertram Raphael, “A Formal Basis for the Heuristic Determination of Minimum Cost Paths”, *IEEE TSSC* 4(2), 1968 — A*의 원전.
- Sven Koenig, Maxim Likhachev, “D* Lite”, *AAAI*, 2002 — 동적 graph의 증분 휴리스틱 탐색.
- Amit Patel, “Introduction to A*”, Red Blob Games — grid 이동 모델과 heuristic을 시각적으로 연결한 실무 자료.
