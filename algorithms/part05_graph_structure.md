## 들어가며

그래프 알고리즘의 가장 어려운 단계는 BFS나 DFS 코드를 외우는 일이 아니라 문제의 무엇을 정점과 간선으로 볼지 결정하는 일이다. 같은 게임 월드도 타일, NavMesh polygon, 방, 작업, 상태를 정점으로 표현할 수 있다. 표현이 달라지면 연결성, 비용, 메모리, 정답의 의미가 모두 달라진다.

그래프 순회는 한 번 방문한 정점을 표시하는 단순한 반복처럼 보이지만, 발견 시점과 완료 시점은 cycle, 위상 순서, 강한 연결 요소, articulation point를 판정하는 구조를 만든다. Union-Find는 전체 경로를 저장하지 않고 연결 성분만 유지해 동적 간선 추가를 거의 상수 시간에 처리한다.

이 글은 그래프 모델링과 표현에서 시작해 BFS·DFS의 불변식을 증명한다. 방향·무방향 cycle, 위상 정렬, 강한 연결 요소, articulation·bridge, Union-Find, 최소 신장 트리를 연결성이라는 공통 질문으로 정리하고, 던전 생성·작업 그래프·의존성 진단에 적용하는 기준을 세운다.

---

## 목차
1. [그래프로 모델링하기](#1-그래프로-모델링하기)
2. [표현과 비용](#2-표현과-비용)
3. [BFS와 계층 구조](#3-bfs와-계층-구조)
4. [DFS와 시간 정보](#4-dfs와-시간-정보)
5. [Cycle 판정과 위상 정렬](#5-cycle-판정과-위상-정렬)
6. [강한 연결 요소](#6-강한-연결-요소)
7. [Articulation Point와 Bridge](#7-articulation-point와-bridge)
8. [Union-Find](#8-union-find)
9. [최소 신장 트리](#9-최소-신장-트리)
10. [동적 그래프와 검증](#10-동적-그래프와-검증)

---

## 1. 그래프로 모델링하기

그래프 $G=(V,E)$는 정점 집합 $V$와 간선 집합 $E$로 구성된다. 모델을 만들 때 다음을 먼저 답한다.

```text
정점 하나는 어떤 상태 또는 객체인가?
간선은 어떤 전이 또는 관계인가?
간선에 방향이 있는가?
평행 간선과 self-loop를 허용하는가?
그래프가 정적인가, 실행 중 변하는가?
출력이 경로인가, 연결 여부인가, 순서인가, 부분 구조인가?
```

작업 시스템에서 정점은 job이고 간선 `u -> v`는 `v`가 `u` 완료에 의존한다. 던전에서 정점은 방이고 무방향 간선은 통로다. 상태 탐색에서 정점은 월드 상태이고 간선은 가능한 행동이다.

### 명시적 그래프와 암시적 그래프

모든 간선을 저장할 필요는 없다. 4방향 grid는 좌표에서 이웃을 생성하고, 퍼즐 상태는 가능한 행동을 적용해 다음 상태를 생성한다. 이를 암시적 그래프라 한다.

명시적 그래프의 순회 비용 $O(V+E)$에서 $E$는 저장된 간선 수다. 암시적 그래프에서는 실제로 생성한 이웃 수와 한 이웃을 생성·검증하는 비용을 포함해야 한다.

### 상태 동등성

상태 그래프에서 “같은 정점”의 정의가 정확성을 결정한다. 위치만 key로 사용하면 열쇠 보유 여부가 다른 두 상태를 잘못 합칠 수 있다.

```text
state = (position, acquired_keys, door_states, remaining_budget)
```

모든 이력을 넣으면 정확하지만 상태 공간이 폭발한다. 미래 전이와 목표 판정에 영향을 주는 최소 충분 상태를 식별해야 한다.

## 2. 표현과 비용

### 인접 리스트

각 정점에 나가는 이웃 목록을 둔다. 공간은 $O(V+E)$이고 sparse graph 순회에 적합하다.

```cpp
struct Edge { int to; };
using Graph = std::vector<std::vector<Edge>>;
```

`vector<vector<Edge>>`는 단순하지만 정점마다 별도 할당이 생길 수 있다. CSR(compressed sparse row)은 모든 간선을 연속 배열에 두고 정점별 offset을 저장해 순회를 캐시 친화적으로 만든다. 대신 동적 삽입이 비싸다.

### 인접 행렬

$V\times V$ 행렬은 두 정점의 연결 여부를 $O(1)$에 답하지만 $O(V^2)$ 공간을 사용한다. Dense graph나 정점 수가 작고 bitset 연산을 사용할 때 유리하다.

### Edge list

간선 전체를 `(u,v)` 배열로 두면 모든 간선을 정렬하거나 batch 처리하기 쉽다. 특정 정점의 이웃 조회는 별도 index 없이는 $O(E)$다. 최소 신장 트리처럼 간선 중심 알고리즘에 자연스럽다.

### 방향과 역방향

방향 그래프에서 incoming edge가 자주 필요하면 reverse adjacency를 함께 저장할 수 있다. 메모리는 늘지만 강한 연결 요소, dependency 해제, 목표에서 역방향 탐색이 단순해진다.

## 3. BFS와 계층 구조

BFS는 시작 정점에서 간선 수가 적은 순서로 정점을 발견한다. 모든 간선 비용이 동일할 때 최단 간선 수를 구한다.

```cpp
std::vector<int> BfsDistance(const Graph& g, int start) {
    std::vector<int> distance(g.size(), -1);
    std::queue<int> q;
    distance[start] = 0;
    q.push(start);

    while (!q.empty()) {
        const int u = q.front();
        q.pop();
        for (const Edge e : g[u]) {
            if (distance[e.to] != -1) continue;
            distance[e.to] = distance[u] + 1;
            q.push(e.to);
        }
    }
    return distance;
}
```

### BFS 불변식

Queue에서 정점을 꺼내는 거리는 비감소한다. 거리 $d$인 정점을 처리할 때 새로 발견되는 정점은 거리 $d+1$이다. 더 짧은 경로가 있었다면 해당 정점은 더 앞 계층에서 이미 발견됐어야 한다.

방문 표시는 queue에서 꺼낼 때가 아니라 **넣을 때** 한다. 꺼낼 때 표시하면 같은 정점이 여러 부모에서 중복 삽입되어 메모리가 크게 늘 수 있다.

### 다중 출발점 BFS

여러 시작 정점을 모두 거리 0으로 queue에 넣으면 가장 가까운 시작점까지의 거리를 한 번에 계산한다. 여러 화재원에서 번지는 시간, 가장 가까운 안전 구역, 영역별 거리장에 사용할 수 있다.

가상의 super-source를 만들고 모든 시작점에 비용 0 간선을 연결한 것과 같다.

### 경로 복원

처음 발견할 때 `parent[v]=u`를 저장하면 목표에서 시작까지 역추적할 수 있다. 시작점의 parent를 특별한 sentinel로 두고, 도달하지 않은 정점과 구분한다.

## 4. DFS와 시간 정보

DFS는 한 경로를 끝까지 따라간 뒤 되돌아온다. 각 정점에 발견 시간 `tin`과 완료 시간 `tout`을 기록하면 DFS forest의 포함 관계가 드러난다.

정점 $u$가 $v$의 DFS 조상이면

$$
tin[u] < tin[v] < tout[v] < tout[u]
$$

이다. 재귀 호출 구간이 서로 포함되거나 겹치지 않는 괄호 구조를 이룬다.

```cpp
void Dfs(const Graph& g, int u, std::vector<int>& color,
         std::vector<int>& tin, std::vector<int>& tout, int& timer) {
    color[u] = 1;
    tin[u] = timer++;
    for (const Edge e : g[u]) {
        if (color[e.to] == 0) Dfs(g, e.to, color, tin, tout, timer);
    }
    color[u] = 2;
    tout[u] = timer++;
}
```

색 의미는 다음과 같다.

```text
WHITE(0): 아직 발견되지 않음
GRAY(1): 호출 stack 안에 있음
BLACK(2): 모든 나가는 간선 처리가 완료됨
```

### 재귀 깊이

긴 chain 그래프는 재귀 깊이 $V$를 만든다. 입력을 신뢰할 수 없거나 $V$가 크면 명시적 stack과 iterator 위치를 함께 저장해야 한다. 단순히 정점만 stack에 넣으면 재귀 DFS의 완료 시점을 재현하기 어렵다.

### Edge 분류

방향 그래프 DFS에서 간선 `(u,v)`를 볼 때 `v`가 GRAY면 현재 호출 stack의 조상으로 돌아가는 back edge다. 이는 cycle의 증거다. discovery/finish 시간으로 tree, back, forward, cross edge를 구분할 수 있다.

## 5. Cycle 판정과 위상 정렬

### 무방향 cycle

무방향 DFS에서는 부모로 돌아가는 간선을 cycle로 세면 안 된다. 방문한 이웃 `v`가 `parent[u]`가 아닐 때 cycle이 있다. 평행 간선을 허용하면 부모 정점만 비교해서는 두 번째 평행 간선을 놓칠 수 있으므로 edge ID를 구분해야 한다.

### 방향 cycle

방향 그래프에서 GRAY 정점으로 향하는 back edge가 있으면 cycle이다. BLACK 정점으로 향하는 edge는 이미 완료된 다른 부분 구조일 수 있어 cycle 증거가 아니다.

### Kahn 위상 정렬

DAG의 위상 순서는 모든 간선 `u -> v`에 대해 `u`가 `v`보다 먼저 오는 정점 순열이다. Kahn 알고리즘은 진입 차수 0인 정점을 반복 제거한다.

```cpp
std::vector<int> TopologicalOrder(const Graph& g) {
    std::vector<int> indegree(g.size(), 0);
    for (const auto& edges : g)
        for (const Edge e : edges) ++indegree[e.to];

    std::queue<int> ready;
    for (int v = 0; v < static_cast<int>(g.size()); ++v)
        if (indegree[v] == 0) ready.push(v);

    std::vector<int> order;
    while (!ready.empty()) {
        int u = ready.front(); ready.pop();
        order.push_back(u);
        for (const Edge e : g[u])
            if (--indegree[e.to] == 0) ready.push(e.to);
    }
    return order;
}
```

결과 크기가 $V$보다 작으면 남은 subgraph의 모든 정점이 양의 indegree를 갖는다. 유한 방향 그래프에서 predecessor를 계속 따라가면 정점을 반복하므로 cycle이 존재한다.

### 결정적 위상 순서

동시에 ready인 정점이 여러 개면 위상 순서는 유일하지 않다. 빌드·작업 결과가 순서에 민감하면 min-heap이나 안정 ID queue로 tie-break를 고정한다. 결정적 순서는 의존성 정확성과 별도 요구다.

### Cycle 진단

“위상 정렬 실패”만 보고하면 대규모 asset dependency에서 원인을 찾기 어렵다. DFS parent를 따라 실제 cycle 경로를 복원하거나, 남은 subgraph에서 SCC를 찾아 사용자에게 최소한의 순환 구성 요소를 보여 줘야 한다.

## 6. 강한 연결 요소

방향 그래프에서 두 정점 $u,v$가 서로 도달 가능하면 같은 강한 연결 요소(SCC)에 속한다. SCC를 각각 하나의 정점으로 축약한 condensation graph는 항상 DAG다. 축약 그래프에 cycle이 있다면 그 cycle의 SCC들이 사실 하나의 더 큰 SCC여야 하기 때문이다.

### Kosaraju 알고리즘

1. 원 그래프 DFS 완료 순서를 기록한다.
2. 모든 간선을 뒤집는다.
3. 완료 시간이 큰 정점부터 역그래프 DFS를 수행한다.
4. 한 DFS에서 방문한 집합이 SCC다.

시간은 $O(V+E)$이고 역그래프 저장이 필요하다. 첫 DFS에서 sink SCC의 정점들이 상대적으로 일찍 완료되고, 완료 역순으로 보면 아직 섞이지 않은 source 역할의 SCC를 분리할 수 있다.

### Tarjan low-link

Tarjan 알고리즘은 한 번의 DFS와 stack으로 SCC를 찾는다. `index[u]`는 발견 순서, `low[u]`는 DFS tree edge와 stack 안 정점으로 가는 edge를 사용해 도달 가능한 최소 index다.

`low[u]==index[u]`이면 `u`는 현재 SCC의 root다. Stack에서 `u`까지 pop한 정점들이 하나의 SCC다. 이미 SCC가 확정되어 stack에서 빠진 정점으로 가는 edge는 low-link 갱신에 사용하지 않는다.

### 적용

- 서로 순환 참조하는 asset·module 묶음 찾기
- quest 상태 전이의 되돌아올 수 있는 영역 분석
- 호출 그래프의 재귀 component 찾기
- dependency graph를 DAG 작업 단위로 축약하기

SCC는 cycle 하나를 찾는 것보다 강한 결과를 제공하지만, 사용자 진단에는 component 내부의 구체 cycle 경로도 유용하다.

## 7. Articulation Point와 Bridge

무방향 연결 그래프에서 정점 하나를 제거했을 때 component 수가 늘면 articulation point, 간선 하나를 제거했을 때 늘면 bridge다.

DFS tree에서 `low[v]`를 정점 `v`의 subtree가 tree edge와 back edge를 사용해 도달 가능한 최소 발견 시간으로 정의한다.

Tree edge `(u,v)`에 대해

$$
low[v] > tin[u]
$$

이면 `(u,v)`는 bridge다. `v` subtree가 `u`나 그 조상으로 돌아가는 다른 edge가 없기 때문이다.

Root가 아닌 `u`는 어떤 자식 `v`에 대해

$$
low[v] \ge tin[u]
$$

이면 articulation point다. DFS root는 독립된 DFS 자식이 둘 이상일 때만 articulation point다.

### 게임 적용과 한계

던전 그래프의 bridge는 유일한 연결 통로다. 의도적으로 choke point를 만들거나, 반대로 bridge를 추가 연결로 제거해 우회 경로를 보장할 수 있다.

그래프 연결성만으로 플레이 품질이 결정되지는 않는다. 통로 길이, 난이도, 시야, 자원 배치가 별도 제약이다. 알고리즘 결과는 생성기의 구조 조건 중 하나다.

## 8. Union-Find

Disjoint Set Union(DSU)은 서로 겹치지 않는 집합들의 partition을 유지한다.

```text
MakeSet(x): {x} 집합 생성
Find(x): x가 속한 집합 대표 반환
Union(a,b): 두 집합 병합
```

Tree parent로 집합을 표현하고 path compression과 union by rank/size를 결합한다.

```cpp
class Dsu {
public:
    explicit Dsu(int n) : parent_(n), size_(n, 1) {
        std::iota(parent_.begin(), parent_.end(), 0);
    }

    int Find(int x) {
        if (parent_[x] != x) parent_[x] = Find(parent_[x]);
        return parent_[x];
    }

    bool Union(int a, int b) {
        a = Find(a); b = Find(b);
        if (a == b) return false;
        if (size_[a] < size_[b]) std::swap(a, b);
        parent_[b] = a;
        size_[a] += size_[b];
        return true;
    }
private:
    std::vector<int> parent_, size_;
};
```

두 최적화를 함께 사용하면 $m$개 연산의 시간은 $O(m\alpha(n))$이다. inverse Ackermann 함수 $\alpha(n)$은 현실적 크기에서 매우 작지만 엄밀한 상수 시간은 아니다.

### DSU가 답하지 않는 것

일반 DSU는 간선 추가에는 강하지만 삭제 뒤 component 분리를 처리하지 못한다. 두 정점이 연결됐다는 사실은 답하지만 실제 경로도 저장하지 않는다. 삭제·경로 질의에는 다른 동적 그래프 구조나 offline 역순 처리가 필요하다.

### Offline 삭제

시간에 따라 간선이 삭제되기만 하는 질의를 모두 알고 있다면 시간을 뒤집어 삭제를 추가로 바꿀 수 있다. 마지막 상태에서 시작해 역순으로 간선을 Union하고 각 시점 연결 여부를 답한다. Offline이라는 계약이 알고리즘을 바꾼 사례다.

## 9. 최소 신장 트리

연결된 가중 무방향 그래프의 spanning tree는 모든 정점을 연결하는 $V-1$개 간선 집합이다. MST는 그중 총 가중치가 최소인 트리다.

MST는 두 정점 사이 최단 경로를 보장하지 않는다. 전체 연결 비용과 개별 경로 비용은 다른 목적 함수다.

### Cut property

정점을 두 집합 $(S,V\setminus S)$로 나눈 cut을 가로지르는 최소 비용 간선 $e$는 어떤 MST에 포함될 수 있다. $e$를 포함하지 않는 MST $T$에 $e$를 추가하면 cycle이 생긴다. 그 cycle의 다른 cut 횡단 간선 $e'$를 제거하면 spanning tree가 유지되고 $w(e)\le w(e')$이므로 비용이 늘지 않는다.

이 교환 논증이 Kruskal과 Prim의 정확성 기반이다.

### Kruskal

간선을 비용순으로 처리하고 서로 다른 component를 잇는 간선만 선택한다. DSU로 cycle을 판정한다.

```cpp
std::sort(edges.begin(), edges.end(), ByWeightThenStableId);
for (const WeightedEdge& e : edges) {
    if (dsu.Union(e.u, e.v)) tree.push_back(e);
}
```

정렬이 지배해 $O(E\log E)$이다. 그래프가 disconnected면 minimum spanning forest를 반환한다. 연결을 요구한다면 선택 간선 수가 $V-1$인지 검사해야 한다.

### Prim

현재 tree와 바깥 정점 사이 cut의 최소 간선을 반복 선택한다. Sparse graph에서는 priority queue와 adjacency list로 $O(E\log V)$에 구현할 수 있다. Dense graph에서는 단순 $O(V^2)$ 구현이 더 나을 수 있다.

### 동률과 결정성

가중치 동률이 있으면 MST는 여러 개일 수 있다. 생성 결과를 재현해야 하면 edge ID로 tie-break를 고정한다. 고정된 tie-break는 재현성을 주지만 게임플레이 다양성을 원하면 명시적 seed로 순서를 무작위화할 수 있다.

## 10. 동적 그래프와 검증

### 변경 중 순회의 의미

탐색 도중 그래프가 바뀌면 다음 중 하나를 계약해야 한다.

- snapshot을 고정한다.
- version이 바뀌면 탐색을 취소하고 다시 시작한다.
- 특정 변경만 증분 반영한다.
- read/write synchronization으로 변경을 막는다.

Container iterator가 유효하더라도 알고리즘 의미가 유효한 것은 아니다. 이미 방문한 정점에 새 간선이 생기면 단순 순회는 그 간선을 놓칠 수 있다.

### 검증 속성

작은 그래프는 brute-force와 구조 속성으로 검증한다.

```text
BFS parent 경로의 간선 수는 distance와 같다.
위상 순서에서는 모든 edge u->v가 pos[u] < pos[v]다.
SCC 내부 모든 정점 쌍은 서로 도달 가능하다.
SCC 사이 condensation graph는 DAG다.
DSU의 연결 답은 BFS component와 같다.
MST는 연결되고 edge 수가 V-1이며 cycle이 없다.
```

작은 그래프의 모든 edge subset을 열거하면 MST 최적 비용을 검증할 수 있다. 느린 oracle은 production 구현과 다른 구조를 사용해야 같은 버그를 공유하지 않는다.

### 경계 입력

- 정점 0개와 1개
- self-loop와 평행 간선
- 완전히 disconnected graph
- 긴 chain과 star
- 하나의 거대한 SCC와 모든 정점이 독립 SCC인 그래프
- 가중치가 모두 같은 그래프
- bridge 하나로 두 dense component를 연결한 그래프

### 성능 모델

$O(V+E)$라도 representation에 따라 실제 비용이 다르다. 이웃의 메모리 locality, 정점 ID 압축, 방문 배열 초기화, branch factor, component 크기 분포를 측정한다. 반복 탐색에서는 매번 `visited`를 지우는 대신 generation counter를 사용할 수 있지만 counter wrap 정책이 필요하다.

---

## 결론

그래프 알고리즘은 정점과 간선의 의미를 올바르게 정의하는 데서 시작한다. BFS는 거리 계층을, DFS는 발견·완료 구간을 만들며, 이 정보에서 cycle, 위상 순서, SCC, articulation point와 bridge를 유도할 수 있다. 각각은 단순 방문 순서가 아니라 연결 구조의 다른 질문에 답한다.

Union-Find는 간선 추가에 따른 component partition을 거의 상수의 상각 비용으로 유지하고, MST는 cut property의 교환 논증으로 전체 연결 비용을 최소화한다. 실제 시스템에서는 결정적 tie-break, 변경 중 snapshot, 진단 가능한 cycle 경로, representation의 메모리 비용까지 그래프 계약에 포함해야 한다.

## 더 읽을거리

- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms*, 4th ed. (MIT Press, 2022), Ch. 19~21 — disjoint set, 그래프 표현·순회·SCC, MST의 표준 증명.
- Steven S. Skiena, *The Algorithm Design Manual*, 3rd ed. (Springer, 2020), Ch. 7~8 — 그래프 문제를 식별하고 순회 결과를 처리하는 실무 설계.
- Robert E. Tarjan, “Depth-First Search and Linear Graph Algorithms”, *SIAM Journal on Computing* 1(2), 1972 — SCC, articulation 관련 DFS low-link 기법의 고전.
- J. B. Kruskal, “On the Shortest Spanning Subtree of a Graph and the Traveling Salesman Problem”, *Proceedings of the AMS* 7, 1956 — Kruskal MST 원전.
- Robert C. Prim, “Shortest Connection Networks and Some Generalizations”, *Bell System Technical Journal* 36, 1957 — Prim MST의 고전적 서술.
- Robert E. Tarjan, “Efficiency of a Good But Not Linear Set Union Algorithm”, *JACM* 22(2), 1975 — path compression과 union rank 분석.
