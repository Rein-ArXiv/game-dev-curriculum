## 들어가며

모든 조합 문제에 다항시간 알고리즘이 있는 것은 아니다. 가능한 loadout, squad 배치, puzzle 상태, 절차 생성 제약을 정확히 만족하는 해를 찾으려면 최악에 지수 개의 후보를 조사해야 할 수 있다. 이때 완전 탐색을 피하는 척 heuristic 하나를 붙이는 것보다, 탐색 공간과 pruning 조건을 명시한 backtracking이 더 정확하고 검증 가능하다.

반대로 겉보기에 조합적인 할당 문제도 구조를 찾으면 다항시간에 풀린다. 용량을 가진 네트워크는 residual graph와 augmenting path로, 두 집합 사이 일대일 할당은 bipartite matching으로 모델링된다. 문제를 일반 NP-hard 형태로 보기 전에 flow·matching·interval·matroid 같은 특수 구조를 찾는 것이 알고리즘 설계의 핵심이다.

이 글은 subset·permutation 생성에서 시작해 backtracking, constraint propagation, symmetry breaking, branch and bound를 분석한다. 이어서 max flow·min cut, bipartite matching, min-cost flow·assignment를 유도하고, 정확한 탐색과 근사·시간 제한 해법 사이의 계약을 정리한다.

---

## 목차
1. [조합 공간과 출력 하한](#1-조합-공간과-출력-하한)
2. [Backtracking의 상태와 불변식](#2-backtracking의-상태와-불변식)
3. [Pruning과 대칭 제거](#3-pruning과-대칭-제거)
4. [Constraint Propagation](#4-constraint-propagation)
5. [Branch and Bound](#5-branch-and-bound)
6. [Flow Network와 Residual Graph](#6-flow-network와-residual-graph)
7. [Max-Flow Min-Cut](#7-max-flow-min-cut)
8. [Bipartite Matching](#8-bipartite-matching)
9. [비용이 있는 할당](#9-비용이-있는-할당)
10. [어려운 문제를 다루는 절차](#10-어려운-문제를-다루는-절차)
11. [게임 적용과 검증](#11-게임-적용과-검증)

---

## 1. 조합 공간과 출력 하한

$n$개 원소의 모든 부분집합은 $2^n$개, 모든 순열은 $n!$개다. 모든 결과를 실제로 출력해야 한다면 그 수보다 빠른 알고리즘은 존재할 수 없다.

```text
n=20: 2^n = 1,048,576
n=30: 2^n = 1,073,741,824
10! = 3,628,800
15! = 1,307,674,368,000
```

Big-O의 밑과 factorial은 작은 parameter 차이에도 실행 가능성을 바꾼다. 조합 탐색에서는 전체 입력 크기보다 지수화되는 parameter $k$를 명시해야 한다.

### 생성과 탐색

- 생성: 가능한 모든 객체를 출력한다.
- 결정: 조건을 만족하는 객체가 하나라도 있는지 답한다.
- 최적화: 가능한 객체 중 목적 값이 최적인 것을 찾는다.

결정·최적화는 pruning으로 많은 후보를 방문하지 않을 수 있지만 최악 입력에서는 전체 공간에 가까울 수 있다. 평균적 성공과 최악 보장을 구분한다.

### 작은 parameter의 가치

고정된 8개 대상의 포함 여부는 $2^8$가지다. 그러나 전체 월드 entity가 백만 개이고 그중에서 8개를 고르는 문제라면 공간은 $C(N,8)$이며, $N=10^6$이면 약 $2.48\times10^{43}$다. 서로 구별되는 8개 슬롯에 중복 없이 배치하면 $P(N,8)=C(N,8)\cdot8!$이다. 따라서 slot 수가 8로 작다는 사실만으로 탐색이 $2^8$로 줄어들지는 않는다. 작은 slot 수가 실제로 이득이 되려면 후보 축소, 상태 압축, matching 같은 별도 구조를 함께 설명해야 한다. 반대로 콘텐츠 설정으로 slot 수가 무제한 증가할 수 있으면 현재는 빠른 구현이 미래에는 폭발하므로 parameter 상한을 제품 계약에 둬야 한다.

## 2. Backtracking의 상태와 불변식

Backtracking은 부분해를 확장하다가 완전한 해로 이어질 수 없으면 되돌아간다.

```cpp
void Search(State& state, int depth) {
    if (IsComplete(state)) {
        EmitOrUpdateBest(state);
        return;
    }

    for (Choice c : GenerateChoices(state, depth)) {
        if (!Feasible(state, c)) continue;
        Apply(state, c);
        Search(state, depth + 1);
        Undo(state, c);
    }
}
```

핵심 불변식은 재귀 진입 시 `state`가 depth까지의 정확한 부분해이고 모든 hard constraint를 만족한다는 것이다. `Undo` 뒤 state는 논리적으로 관찰 가능한 값이 원래와 같아야 sibling branch가 서로 오염되지 않는다. Padding·capacity·cache·할당 주소처럼 의미를 바꾸지 않는 표현 차이까지 bitwise로 같을 필요는 없고, state hash가 같다는 것도 충돌 가능성 때문에 완전 복원의 증명은 아니다. Hash는 복구 누락을 탐지하는 보조 수단으로만 쓴다.

### Apply/Undo와 복사

상태 전체를 복사하면 구현이 단순하지만 상태 크기만큼 비용이 든다. In-place apply/undo는 빠르지만 복구 누락 위험이 크다.

안전한 방법은 mutation log를 두는 것이다.

```text
checkpoint = log.size
choice를 적용하며 변경 전 값을 log에 기록
재귀 호출
log를 checkpoint까지 역적용
```

예외·조기 return에도 복구되도록 scope guard를 사용할 수 있다. `GenerateChoices`가 `state` 내부를 참조하는 view를 반환하면 `Apply`가 그 range를 무효화할 수 있으므로, choice 목록을 snapshot하거나 반복자 안정성을 보장하고 예외·취소 시 rollback 계약을 명시한다.

### 순열 생성

각 depth에서 아직 사용하지 않은 원소를 하나 고르면 $n!$ leaf가 생긴다. `used`와 현재 prefix가 상태다.

중복 값이 있으면 index가 다른 같은 값을 바꾼 순열이 중복된다. 같은 depth에서 같은 값을 한 번만 선택하거나 정렬 후 이전 동일 값의 사용 조건을 둬 distinct permutation만 생성한다.

### 조합 생성

부분집합은 각 원소를 선택/비선택하는 binary tree로 만들 수 있다. 크기 $k$ 조합만 필요하면 남은 원소 수로 불가능한 branch를 자른다.

```text
chosen > k -> 실패
chosen + remaining < k -> 실패
```

단순하지만 완전한 bound다.

## 3. Pruning과 대칭 제거

Pruning은 현재 부분해 아래 어떤 completion도 유효하거나 더 좋을 수 없음을 증명해야 한다.

### Feasibility pruning

Hard constraint를 이미 위반한 branch를 제거한다.

- 사용 budget 초과
- 같은 slot 중복 배치
- graph coloring에서 인접 정점과 같은 색
- puzzle의 부분 배치가 규칙 위반

위반이 이후 선택으로 복구될 수 없는 **단조 위반**인지 확인한다. 현재 합이 목표를 넘었다고 잘라도 되는 것은 남은 수가 모두 음이 아닐 때뿐이다.

### Dominance pruning

같은 미래 선택 집합을 가진 두 부분 상태에서 하나가 비용·자원 모든 면에서 나쁘면 제거한다. 미래 가능성이 실제로 같은지 상태 key를 정확히 정의해야 한다.

### Symmetry breaking

서로 교환 가능한 객체가 있으면 같은 논리 해가 여러 순열로 나타난다. 동일 tower 세 개의 배치 순서를 모두 탐색할 이유가 없다.

Canonical condition을 둔다.

```text
동일 type 객체의 선택 index는 증가 순서다.
색 이름을 처음 등장 순서로 정규화한다.
회전·반사가 같은 board는 canonical transform 하나만 남긴다.
```

대칭 제거가 실제로 모든 동치류에서 적어도 하나의 대표를 남기는지 증명해야 한다. 너무 강한 조건은 유효 해 전체를 지울 수 있다.

### Variable·value ordering

탐색 순서는 정확성을 바꾸지 않지만 tree 크기를 크게 바꾼다.

- 가장 제약이 많은 변수(MRV)를 먼저 선택한다.
- 값은 성공 가능성이 높은 쪽, 즉 이웃 domain을 가장 적게 제한하는 값(least-constraining value)을 먼저 시도한다 — fail-first는 변수 선택의 원리이지 값 선택의 원리가 아니다.
- 최적화에서는 좋은 incumbent를 빨리 얻는 값을 먼저 시도한다.

Ordering heuristic의 효과는 instance 분포에 의존하므로 방문 node 수를 측정한다.

## 4. Constraint Propagation

Constraint Satisfaction Problem(CSP)은 변수 $X_i$, domain $D_i$, constraint 집합으로 구성된다. 단순 backtracking은 값을 하나 정한 뒤에만 위반을 본다. Propagation은 그 결정이 다른 domain에서 불가능하게 만든 값을 미리 제거한다.

### Forward checking

변수 하나를 할당하면 직접 연결된 미할당 변수 domain에서 충돌 값을 제거한다. 어떤 domain이 비면 즉시 backtrack한다.

제거한 값을 undo할 수 있도록 level별 trail을 저장해야 한다.

### Arc consistency

Binary constraint $C(X,Y)$에서 $X$의 값 $x$마다 이를 지지하는 $Y$ 값 $y$가 하나 이상 있으면 arc-consistent다.

$$
\forall x\in D_X,\ \exists y\in D_Y:\ C(x,y)
$$

지지가 없는 $x$를 제거하면 연결 arc를 다시 queue에 넣는다. AC-3는 이 과정을 fixed point까지 반복한다.

Arc consistency가 전체 해 존재를 보장하지는 않는다. Pairwise로는 모두 가능해도 세 변수 이상의 global constraint가 모순일 수 있다.

### Global constraint

`all-different`, 용량 합, 경로 연결성 같은 global constraint는 binary constraint로 분해하면 propagation이 약해질 수 있다. 전용 propagator는 더 강한 제거를 할 수 있지만 구현·검증 비용이 크다.

### 절차 생성

Tile adjacency 규칙 기반 생성은 CSP로 볼 수 있다. 가장 domain이 작은 cell을 선택하고 값을 고른 뒤 이웃 domain을 전파한다. 모순 시 backtracking하거나 restart한다.

Seed가 같아도 unordered container 순회와 동률 선택이 다르면 결과가 바뀐다. 변수·값 ordering과 RNG 소비 순서를 고정해야 재현 가능하다.

## 5. Branch and Bound

Branch and bound는 최적화 탐색에서 부분 상태로 달성 가능한 최선의 낙관적 bound를 계산한다. 최소화 문제에서 lower bound가 현재 최선 incumbent 이상이면 branch를 제거한다.

```text
best = 알려진 feasible 해 비용

Search(state):
  if LowerBound(state) >= best: prune
  if complete: best 갱신
  else: child를 유망한 순서로 탐색
```

`LowerBound(state) >= best`는 최적값 하나나 최적해 하나를 찾을 때는 안전하지만, 같은 최적값의 모든 해를 열거할 때는 동률 가지까지 제거한다. 모든 최적해를 모으려면 `LowerBound(state) > best`만 자르거나 동률 completion을 수집하는 규약을 별도로 둔다. 한 최적해 모드와 모든 최적해 모드를 구분한다.

### Bound의 조건

Lower bound는 실제 가능한 completion 비용보다 클 수 없다.

$$
LB(state)\le OPT(state)
$$

Bound가 너무 낮으면 안전하지만 pruning이 약하다. 실제 최적보다 높으면 최적 branch를 잘라 잘못된 답을 낸다.

### Relaxation으로 bound 만들기

어려운 제약을 완화한 문제의 최적값은 최소화 원문제의 lower bound가 된다. 0/1 knapsack 최대화에서는 남은 항목을 fractional하게 담아 얻는 값이 낙관적 upper bound다.

### DFS와 best-first

- DFS: 메모리가 작고 feasible incumbent를 빨리 찾을 수 있다.
- Best-first: bound가 가장 좋은 node부터 확장해 강한 탐색 순서를 제공하지만 open set 메모리가 커진다.

시간 제한 시 현재 incumbent를 반환하려면 그것이 실제 feasible 해인지 보장해야 한다. Optimality gap은 현재 한 노드의 bound가 아니라 아직 열려 있는 전체 frontier를 대표하는 lower bound로 계산한다. 최소화에서는 $L=\min(U,\min_{\text{frontier}} LB)$를 유지하고 $0\le U-OPT\le U-L$을 보고한다. Feasible incumbent가 아직 없는 경우와 frontier가 빈 경우는 별도 상태로 구분한다.

### Memoization과 차이

DP는 같은 상태의 최적 suffix 값을 재사용한다. Branch and bound는 현재 incumbent와 bound로 branch를 제거한다. 상태가 합쳐질 수 있으면 memoization을 결합할 수 있지만, path-dependent constraint를 key에서 빠뜨리면 잘못된 재사용이 된다.

## 6. Flow Network와 Residual Graph

Flow network는 directed graph $G=(V,E)$, source $s$, sink $t$, capacity $c(u,v)\ge0$를 가진다. Flow $f$는 capacity와 conservation을 만족한다.

$$
0\le f(u,v)\le c(u,v)
$$

$$
\sum_u f(u,v)=\sum_w f(v,w)\quad(v\ne s,t)
$$

Flow 값은 source에서 나간 순량이며 sink로 들어간 순량과 같다.

### Residual capacity

현재 flow 뒤 추가로 보낼 수 있는 양을 residual capacity로 표현한다.

$$
c_f(u,v)=c(u,v)-f(u,v)
$$

이미 보낸 flow를 취소할 수 있도록 reverse residual edge $(v,u)$에 용량 $f(u,v)$를 둔다. Reverse edge가 없으면 초기의 나쁜 경로 선택을 되돌릴 수 없다.

```cpp
struct FlowEdge {
    int to;
    int reverseIndex;
    std::int64_t capacity;
};

void AddEdge(std::vector<std::vector<FlowEdge>>& g,
             int u, int v, std::int64_t capacity) {
    assert(u != v);
    assert(capacity >= 0);
    FlowEdge forward{v, static_cast<int>(g[v].size()), capacity};
    FlowEdge reverse{u, static_cast<int>(g[u].size()), 0};
    g[u].push_back(forward);
    g[v].push_back(reverse);
}
```

이 구현은 $s$-$t$ 최대 유량에 기여하지 않는 self-loop를 입력 계약에서 거부한다. `u == v`를 허용하려면 같은 adjacency list에 두 edge를 넣을 때 달라지는 reverse index를 별도로 계산해야 한다.

Augmenting path에서 bottleneck $\Delta$만큼 forward residual을 줄이고 reverse residual을 늘린다. 원래 edge와 reverse edge index가 정확히 짝을 이뤄야 한다.

### Ford-Fulkerson

Residual graph에서 $s$-$t$ path가 있는 동안 augment한다. 정수 capacity에서 flow 값이 매번 최소 1 증가하므로 종료하고 $O(E|f^*|)$ 상한을 갖는다. 큰 capacity에서는 pseudo-polynomial이고, irrational capacity와 임의 path 선택에서는 종료 문제가 생길 수 있다.

### Edmonds-Karp와 Dinic

Edmonds-Karp는 BFS로 edge 수가 가장 적은 augmenting path를 골라 $O(VE^2)$을 보장한다.

Dinic은 residual graph의 BFS level graph를 만들고 그 안에서 blocking flow를 보낸다. 일반 capacity graph에서 $O(V^2E)$ 상한을 가지며 unit network에서는 더 강한 상한이 있다. 실제 성능은 graph 구조에 크게 의존한다.

## 7. Max-Flow Min-Cut

Cut $(S,T)$는 $s\in S$, $t\in T$인 정점 partition이다. Cut capacity는 $S$에서 $T$로 가는 원래 edge capacity 합이다.

$$
c(S,T)=\sum_{u\in S,v\in T}c(u,v)
$$

어떤 flow도 cut capacity를 넘을 수 없다. Conservation 때문에 source에서 sink로 가는 모든 순 flow는 cut을 지나야 한다.

### 정리의 증명 핵심

Residual graph에 augmenting path가 없을 때 $s$에서 residual edge로 도달 가능한 정점 집합을 $S$, 나머지를 $T$라 하자.

- $S\to T$ 원래 edge는 residual capacity가 0이므로 포화됐다.
- $T\to S$ 원래 edge에는 flow가 0이어야 한다. 양의 flow가 있으면 reverse residual로 $S$에서 $T$ 정점에 도달할 수 있기 때문이다.

따라서 현재 flow 값이 cut capacity와 같다. 모든 flow는 모든 cut 이하이고, 현재 flow와 같은 cut을 찾았으므로 둘 다 최적이다.

$$
\max |f|=\min c(S,T)
$$

이 dual certificate 덕분에 max flow 결과는 단순 숫자가 아니라 최적성 증거인 min cut을 함께 제공한다.

### 모델링 예

- 여러 생산원에서 소비처로 보내는 제한 자원
- zone 사이 최대 동시 전송 경로
- node-disjoint path: 정점을 in/out으로 분할하고 내부 edge capacity를 둠
- edge-disjoint path: edge capacity를 1로 둠

시간에 따른 순서, indivisible object, 여러 commodity가 있으면 일반 max flow 모델이 충분하지 않을 수 있다.

## 8. Bipartite Matching

Bipartite graph $G=(L\cup R,E)$에서 matching은 endpoint를 공유하지 않는 edge 집합이다. 최대 matching은 선택 edge 수를 최대화한다.

### Flow로 환원

```text
source -> 모든 L 정점: capacity 1
L -> R의 원래 edge: capacity 1
모든 R 정점 -> sink: capacity 1
```

정수 capacity에서는 정수 augmentation을 보존하는 max-flow 알고리즘(Ford-Fulkerson, Edmonds-Karp, Dinic 등)이 정수 최적 flow를 찾으므로 flow 1인 `L->R` edge가 matching이다. 정수 capacity가 보장하는 것은 정수 최적해의 존재와 정수 augmentation이며, 임의의 최적 flow가 모두 정수라는 뜻은 아니다.

### Augmenting path

현재 matching에 없는 edge와 있는 edge를 번갈아 지나며 unmatched L에서 unmatched R로 가는 path를 augmenting path라 한다. 이 path의 선택 상태를 뒤집으면 matching 크기가 1 증가한다.

Berge 정리는 augmenting path가 없을 때만 matching이 최대라고 말한다. 현재 matching이 최대가 아니면 더 큰 matching과의 symmetric difference에서 augmenting path가 나타난다.

### Hall 조건

모든 왼쪽 정점을 match할 수 있으려면 모든 subset $S\subseteq L$에 대해 이웃 집합 $N(S)$가

$$
|N(S)|\ge|S|
$$

을 만족해야 한다. 어떤 역할 후보 집합보다 가능한 player가 적으면 완전 할당은 불가능하다.

### Hopcroft-Karp

여러 vertex-disjoint shortest augmenting path를 phase마다 함께 찾아 bipartite maximum matching을 $O(E\sqrt V)$에 구한다. 작은 graph에서는 단순 DFS augmenting 구현이 더 간단하고 충분할 수 있다.

## 9. 비용이 있는 할당

Maximum matching은 match 수만 본다. Player-role 적합도나 이동 비용을 최소화하려면 edge 비용이 필요하다.

### Min-cost flow

Flow 단위마다 edge 비용 $cost(u,v)$가 있고 지정 flow를 보내는 총비용을 최소화한다.

$$
cost(f)=\sum_{(u,v)\in E}f(u,v)cost(u,v)
$$

Residual reverse edge에는 `-cost`를 둬 이전 선택을 취소할 수 있게 한다. Negative residual cost가 생기므로 shortest path 알고리즘의 전제를 맞추기 위해 potential을 사용한 reduced cost나 Bellman-Ford 계열 초기화가 필요할 수 있다. 다만 초기 feasible flow가 이미 최소비용이거나 negative residual cycle이 없어야 potential이 모든 residual edge에서 reduced cost를 음이 아니게 유지하며, 도달 가능한 정점 범위와 potential 갱신 범위를 함께 명시한다. 그 범위 밖의 negative cycle이나 도달 불가 정점은 별도 초기화나 다른 알고리즘으로 처리한다.

### Assignment와 Hungarian algorithm

$n\times n$ 비용 행렬에서 각 row와 column을 정확히 하나씩 짝짓는 assignment problem은 $O(n^3)$ Hungarian algorithm으로 풀 수 있다. Bipartite graph가 완전하지 않으면 금지 edge를 표현하고 완전 matching 가능성을 검사한다.

### 목적 함수의 함정

전체 비용 최소는 개인별 최악 불만을 제한하지 않는다. Matchmaking에서는 합계 MMR 차이가 작아도 한 player가 매우 나쁜 match를 받을 수 있다. Maximum edge cost, 분산, latency hard limit 같은 제약을 모델에 추가해야 한다.

여러 목적을 가중합으로 합치면 weight 단위와 tradeoff를 명시한다. Hard constraint를 큰 penalty로 흉내 내면 overflow와 잘못된 tradeoff가 생길 수 있으므로 가능한 경우 edge 자체를 제거한다.

## 10. 어려운 문제를 다루는 절차

### 특수 구조부터 찾는다

일반 문제를 곧바로 backtracking으로 풀기 전에 다음을 확인한다.

```text
Graph가 DAG·tree·bipartite인가?
Interval 또는 순서 구조가 있는가?
Constraint가 matroid·flow·matching으로 표현되는가?
지수화되는 parameter가 작게 고정되는가?
Input 전체를 아는 offline 재정렬이 가능한가?
```

특수 구조 하나가 지수 탐색을 polynomial algorithm으로 바꿀 수 있다.

### Exact와 heuristic 경계

Exact가 필요하지만 입력이 작은 경우:

- Backtracking + propagation
- Branch and bound
- Meet-in-the-middle
- Bitmask DP

입력이 크고 근사가 허용되는 경우:

- Greedy·local search
- Random restart
- Simulated annealing
- 제한된 beam search

Heuristic 결과에는 feasibility 검사와 시간 종료 동작이 필요하다.

### Meet-in-the-middle

$n$개 선택을 절반으로 나눠 각 절반의 $2^{n/2}$ 결과를 생성하고 정렬·hash로 결합하면 $2^n$을 대략 $2^{n/2}$ 공간·시간 규모로 줄이는 문제가 있다. 공간 사용이 커지고, 두 절반 요약으로 결합 가능한 목적이어야 한다.

### 시간 제한

시간이 끝났을 때 반환할 상태를 구분한다.

```text
feasible incumbent가 있음: 품질 미확정 해 반환 가능
frontier 전체를 대표하는 bound가 있음: optimality gap 보고 가능
feasible 해가 없음: 미발견인지 불가능 증명인지 구분
```

Timeout을 “해 없음”으로 처리하면 정확성 버그다.

## 11. 게임 적용과 검증

### 적용 지도

| 문제 | 모델 | 주의점 |
|---|---|---|
| 작은 squad loadout | Backtracking/bitmask | 역할 상호작용과 상한 |
| 제약 기반 room 배치 | CSP | 재시작과 불가능 진단 |
| bandwidth 경로 용량 | Max flow | 시간·packet indivisibility |
| player-role 일대일 배정 | Bipartite matching | hard eligibility |
| 비용 최소 team assignment | Min-cost flow/assignment | 공정성 목적 |
| 제한 시간 최적 배치 | Branch and bound | incumbent와 gap |

### Backtracking 검증

- 생성한 모든 해가 constraint를 만족한다.
- 작은 domain의 모든 feasible 해가 정확히 한 번 나온다.
- Apply/Undo 전후 논리적으로 관찰 가능한 state가 같고, state hash는 탐지 보조로만 사용한다.
- Symmetry breaking을 끄고 canonicalize한 결과 집합과 켠 결과가 같다.
- Pruning을 끈 기준 탐색과 최적 값이 같다.
- 동률 최적해를 모두 열거하는 모드에서는 `>=` 대신 `>` pruning을 사용해 최적값의 모든 해가 나오는지 확인한다.

### Flow 검증

모든 edge에 capacity constraint를 검사하고 source·sink 외 정점의 conservation을 확인한다.

```text
0 <= flow(e) <= capacity(e)
inflow(v) == outflow(v)
reported value == source net outflow
residual graph에 augmenting path가 없음
reachable residual set의 cut capacity == flow value
```

Min cut equality는 강한 최적성 certificate다.

### Matching 검증

- 선택 edge가 실제 graph에 있다.
- 각 정점 degree가 최대 1이다.
- Flow 환원 결과 크기와 같다.
- 작은 graph에서 모든 edge subset을 열거한 최대 크기와 같다.

### 경계 입력

- 변수·정점이 0개
- 해가 정확히 하나 또는 전혀 없음
- 모든 후보가 대칭
- capacity 0 edge와 평행 edge
- 여러 최대 flow·matching이 존재
- 비용 합 overflow 근처
- Hall 조건을 한 subset만 위반
- Bound가 매우 약해 전체 tree를 방문

### 계측

조합 탐색은 wall time만으로 원인을 알기 어렵다.

```text
방문 node와 leaf 수
pruning 이유별 횟수
최대 깊이와 open set peak
첫 feasible 해까지 시간
incumbent 개선 횟수
최종 optimality gap
flow phase·augment 횟수
```

같은 실행 시간이라도 강한 pruning과 우연히 작은 입력은 다른 시스템이다. 구조 지표를 함께 남겨야 콘텐츠 변화에 따른 폭발을 예측할 수 있다.

---

## 결론

Backtracking은 지수 탐색을 숨기지 않고 상태·선택·복구·pruning을 명시한다. Constraint propagation과 symmetry breaking은 해를 잃지 않는다는 증명 아래 search tree를 줄이고, branch and bound는 낙관적 bound와 feasible incumbent로 최적성 gap을 관리한다. Timeout과 불가능 증명은 반드시 구분해야 한다.

Flow와 matching은 많은 할당 문제의 조합 폭발을 residual 구조와 augmenting path로 바꾼다. Max-flow min-cut equality는 최적성 certificate를 제공하고, bipartite matching과 min-cost flow는 eligibility·용량·비용을 각각 모델링한다. 실제 문제에서는 일반 heuristic을 선택하기 전에 DAG·tree·interval·flow·matching 같은 특수 구조가 있는지 먼저 찾아야 한다.

## 더 읽을거리

- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms*, 4th ed. (MIT Press, 2022), Ch. 24~25 — max flow, bipartite matching, Hungarian assignment의 표준 증명.
- Steven S. Skiena, *The Algorithm Design Manual*, 3rd ed. (Springer, 2020), Ch. 9, 12 및 algorithm catalog — backtracking, pruning, 어려운 문제를 다루는 실무 절차.
- L. R. Ford Jr., D. R. Fulkerson, “Maximal Flow Through a Network”, *Canadian Journal of Mathematics* 8, 1956 — augmenting path와 max flow의 원전.
- Jack Edmonds, Richard M. Karp, “Theoretical Improvements in Algorithmic Efficiency for Network Flow Problems”, *JACM* 19(2), 1972 — Edmonds-Karp 분석.
- John E. Hopcroft, Richard M. Karp, “An $n^{5/2}$ Algorithm for Maximum Matchings in Bipartite Graphs”, *SIAM Journal on Computing* 2(4), 1973 — layered augmenting path matching.
- Rina Dechter, *Constraint Processing* (Morgan Kaufmann, 2003) — CSP propagation, consistency, search ordering의 체계적 서술.
