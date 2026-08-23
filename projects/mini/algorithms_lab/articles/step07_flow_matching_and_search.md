## 들어가며

유량·매칭과 되돌리는 탐색을 짓는다. 이 단계의 중심은 **max-flow == min-cut이 스스로를 증명하는 certificate**라는 것이다.

정리로서의 max-flow min-cut은 증명의 대상이다. 실행 관점에서는 다르다 — 알고리즘이 낸 숫자가 옳은지를, 답 표 없이, 매 실행이 스스로 확인할 수 있는 등식이다. 최대 유량이 끝난 잔여 그래프에서 원천에서 갈 수 있는 정점 집합 S를 잡으면, S에서 나가는 원래 간선의 용량 합이 곧 그 유량값과 같아야 한다. 이 등식이 깨지면 답이 최대가 아니거나 유량이 유량이 아니다.

탐색 쪽 계약은 하나다. **Apply와 Undo는 서로의 역이다.** 상태를 복사해 넘기면 자명하지만 비싸고, 변경 로그로 되돌리면 싸지만 로그가 실제 변경을 빠짐없이 담는다는 것을 검사가 지켜야 한다.

---

## 목차
1. [짝지어진 잔여 간선](#1-짝지어진-잔여-간선)
2. [세 유량 팔과 같은 골격](#2-세-유량-팔과-같은-골격)
3. [certificate로서의 min-cut](#3-certificate로서의-min-cut)
4. [매칭 세 팔과 Hall 조건](#4-매칭-세-팔과-hall-조건)
5. [변경 로그와 상태 해시](#5-변경-로그와-상태-해시)
6. [가지치기는 해를 잃지 않는다](#6-가지치기는-해를-잃지-않는다)
7. [대칭 깨기와 canonical 집합](#7-대칭-깨기와-canonical-집합)
8. [측정과 관측](#8-측정과-관측)

---

## 1. 짝지어진 잔여 간선

잔여 그래프의 모든 간선은 짝으로 존재한다. 정방향 간선을 짝수 인덱스에, 그 역간선을 바로 다음 홀수 인덱스에 두면 짝을 `id ^ 1`로 찾을 수 있다.

<!-- snippet: src/algo/flow.hpp@flow-residual mode=partial -->
```cpp
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
    // ...
};
```

짝 갱신을 빼면 유량은 그럴듯한 값을 낸다. 증가 경로를 찾고 병목만큼 밀어 넣는 절차 자체는 그대로 돌아가기 때문이다. 깨지는 것은 정점 보존이다.

그래서 결과 객체가 스스로를 검사할 수 있어야 한다.

<!-- snippet: src/algo/flow.hpp@flow-residual mode=partial -->
```cpp
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
```

## 2. 세 유량 팔과 같은 골격

Ford-Fulkerson, Edmonds-Karp, Dinic은 같은 골격을 공유한다. 증가 경로를 찾아 병목만큼 민다. 다른 것은 **경로를 어떻게 고르는가**뿐이고, 그 하나가 상계를 바꾼다.

<!-- snippet: src/algo/flow.hpp@flow-edmonds-karp mode=partial -->
```cpp
// Edmonds-Karp = Ford-Fulkerson + BFS. 가장 *짧은* 증가 경로를 고르면 증가
// 횟수가 O(V*E) 로 묶인다 — 용량 크기와 무관해진다. 같은 골격에서 탐색 순서
// 하나를 바꾼 것이 상계를 바꾼다.
        std::deque<uint32_t> q{s};
        seen[s] = 1;
        while (!q.empty() && !seen[t]) {
            const uint32_t u = q.front();
            q.pop_front();
            // ...
        }
        if (!seen[t]) break;
        int64_t bottleneck = kInfFlow;
        for (uint32_t v = t; v != s; v = g.At(via[v] ^ 1).to)
            bottleneck = std::min(bottleneck, g.Residualcap(via[v]));
        for (uint32_t v = t; v != s; v = g.At(via[v] ^ 1).to) g.Push(via[v], bottleneck);
```

Dinic은 한 걸음 더 간다. 층 그래프를 만들고 그 안에서 blocking flow를 한 번에 밀어낸다.

<!-- snippet: src/algo/flow.hpp@flow-dinic mode=partial -->
```cpp
// Dinic. 층 그래프를 만들고(BFS) 그 안에서 blocking flow 를 한 번에 밀어낸다(DFS).
// `iter` 가 정점마다 "이미 막힌 간선" 을 기억해 같은 간선을 다시 시도하지 않는
// 것이 이 알고리즘이 빠른 이유다 — 그것을 빼면 Ford-Fulkerson 으로 되돌아간다.
class Dinic {
    // ...
};
```

용량이 큰 그래프에서 Edmonds-Karp의 이득이 눈에 보인다. 검사는 유량 200만짜리 그래프에서 증가 횟수가 4회 이하임을 단언한다 — 유량값에 비례하지 않는다는 것의 직접적인 확인이다.

## 3. certificate로서의 min-cut

<!-- snippet: src/algo/flow.hpp@flow-min-cut -->
```cpp
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
```

검사가 매 사례, 매 팔에서 단언하는 것은 다섯이다.

```cpp
for (const Residual* g : {&a, &b, &c}) {
    CHECK(g->CapacityRespected());
    CHECK(g->Conserves(fc.s, fc.t));
}
CHECK(a.NetOutOf(fc.s) == ff.value);
// certificate: 잔여 그래프에 증가 경로가 없고, 절단 용량이 유량과 같다.
for (const Residual* g : {&a, &b, &c}) {
    const MinCut cut = ExtractMinCut(*g, fc.s);
    CHECK(!cut.source_side[fc.t]);           // 증가 경로가 없다
    CHECK(cut.capacity == ff.value);         // max-flow == min-cut
}
```

기대값 표가 하나도 없다는 점이 이 검사의 성격이다. 무작위 그래프 420개에 대해서도 같은 다섯을 확인하는 property가 돈다 — 답을 모르는 입력에서도 판정할 수 있는 것이 certificate의 힘이다.

## 4. 매칭 세 팔과 Hall 조건

이분 매칭을 세 가지로 푼다. DFS 증가 경로, Hopcroft-Karp, 그리고 최대 유량으로의 환원. 셋이 같은 크기를 내야 한다.

<!-- snippet: src/algo/flow.hpp@flow-hopcroft-karp mode=partial -->
```cpp
// Hopcroft-Karp. 한 phase 에서 *서로소인 최단 증가 경로를 모두* 찾는다.
// phase 수가 O(sqrt(V)) 로 묶이는 것이 이 알고리즘의 전부다 —
// 증가 한 번의 비용은 같고, 횟수의 상계가 달라진다.
        if (!found) break;
        ++m.phases;

        // DFS: 층을 정확히 한 칸씩 올라가는 경로만 쓴다.
        std::vector<uint8_t> used(right, 0);
        for (uint32_t u = 0; u < left; ++u) {
            if (m.right_of[u] != -1) continue;
            if (LayeredDfs(u, adj, dist, used, m, c)) { ++m.size; ++m.augments; }
        }
```

적대 입력은 Hall 조건을 정확히 한 subset에서만 위반하는 그래프다.

<!-- snippet: src/measure/workload.hpp@workload-bipartite mode=partial -->
```cpp
// Hall 조건을 정확히 한 subset 에서만 위반하는 이분 그래프.
// 왼쪽 0..k-1 이 오른쪽 0..k-2 만 볼 수 있으면 |N(S)| < |S| 인 S 가 하나 생긴다 —
// 완전 매칭이 없고, 최대 매칭 크기는 정확히 left-1 이다.
struct Bipartite {
    size_t left = 0, right = 0;
    std::vector<std::vector<uint32_t>> adj;
    size_t known_max = 0;   // 구성으로 알고 있는 최대 매칭 크기
};

inline Bipartite HallViolatingBipartite(size_t k) {
    Bipartite b;
    b.left = k;
    b.right = k;
    b.adj.assign(k, std::vector<uint32_t>());
    for (uint32_t u = 0; u < k; ++u)
        for (uint32_t v = 0; v + 1 < k; ++v) b.adj[u].push_back(v);
    b.known_max = (k == 0) ? 0 : k - 1;
    return b;
}
```

이 그래프의 최대 매칭 크기가 정확히 k-1이라는 것을 **구성으로** 안다. 오라클도 함께 확인하지만, 구성으로 아는 값이 있으면 오라클과 구현이 같은 오해를 공유할 여지까지 막을 수 있다.

검사는 크기만 보지 않는다. 낸 매칭이 실제로 매칭인지 — 한 정점이 두 번 쓰이지 않았는지, 양방향 대응이 일치하는지, 쓴 간선이 실제로 존재하는지 — 전부 확인한다.

Hungarian은 정사각 비용 행렬의 최소 할당을 O(n³)에 푼다. 오라클은 모든 순열이다. n=8이면 40,320가지이고, 그것을 다 세도 밀리초 단위다 — 오라클이 느려도 되는 것은 입력이 작기 때문이다.

## 5. 변경 로그와 상태 해시

CSP 탐색의 상태는 변수별 도메인 비트마스크다. 가지치기가 하는 일은 전부 "도메인에서 값을 지우는 것"이고, 되돌리기는 "지운 값을 되살리는 것"이다.

<!-- snippet: src/algo/backtrack.hpp@backtrack-solver mode=partial -->
```cpp
    // 변경 로그. 되돌리기는 이 로그를 역순으로 되감는 것뿐이다.
    void Remove(uint32_t var, uint32_t bits) {
        if (bits == 0) return;
        domain_[var] &= ~bits;
        log_.push_back(Undo{var, bits});
        ++stats_.undo_entries;
    }
    void Rewind(size_t mark) {
        while (log_.size() > mark) {
            const Undo u = log_.back();
            log_.pop_back();
            domain_[u.var] |= u.removed;    // ← Apply 의 정확한 역
        }
    }
```

로그가 완전한지는 상태 해시로 확인한다. 탐색 전후의 해시가 같아야 한다 — 도메인과 배정을 함께 섞으므로 Undo가 하나라도 빠지면 달라진다.

<!-- snippet: src/algo/backtrack.hpp@backtrack-solver mode=partial -->
```cpp
    // 상태 해시. 도메인과 배정을 함께 섞는다. Undo 가 하나라도 빠지면 달라진다.
    uint64_t StateHash() const {
        uint64_t h = 1469598103934665603ull;
        for (uint32_t d : domain_) { h ^= d; h *= 1099511628211ull; }
        for (int32_t v : value_)   { h ^= uint64_t(uint32_t(v)); h *= 1099511628211ull; }
        return h;
    }
```

기본 후향 검사가 별도로 있어야 한다는 점을 적어 둔다. 전방 검사와 AC-3는 **최적화**이고, 그것들을 끈 팔이 기준이 되려면 기준 자체가 제약을 지켜야 한다.

<!-- snippet: src/algo/backtrack.hpp@backtrack-solver mode=partial -->
```cpp
            // 기본 후향 검사. 이미 배정된 이웃과 충돌하면 그 값은 버린다.
            // 이것이 *없으면* 아래의 전방 검사·AC-3 는 최적화가 아니라 정확성의
            // 유일한 근거가 되어 버린다 — 가지치기를 끈 팔이 기준이 되려면
            // 기준 자체가 제약을 지켜야 한다.
            bool conflict = false;
            for (uint32_t w : neighbor_[var]) {
                if (value_[w] == int32_t(bit)) { conflict = true; break; }
            }
            if (conflict) { ++stats_.pruned_conflict; continue; }
```

첫 판본에는 이 검사가 없었다. 그러자 5정점 사이클의 3색 칠하기가 30개가 아니라 3⁵ = 243개를 냈다 — 제약을 전혀 확인하지 않았기 때문이다. 검사가 `all == 30`을 단언한 덕에 즉시 드러났다.

## 6. 가지치기는 해를 잃지 않는다

가지치기 팔이 옳다는 것은 "개수가 같다"로는 부족하다. **해 집합이 같아야** 한다.

```cpp
const CspOptions variants[] = {
    CspOptions{true, false, false, false},
    CspOptions{false, true, false, false},
    CspOptions{true, true, false, false},
    CspOptions{true, true, true, false},
};
for (const CspOptions& opt : variants) {
    std::vector<std::vector<int32_t>> got;
    CspSolver solver(p, opt);
    const size_t count = solver.CountSolutions(&got);
    CHECK(solver.HashStable());
    CHECK(count == all);
    std::sort(got.begin(), got.end());
    CHECK(got == base_set);
    // 가지치기가 실제로 노드를 줄였는가
    CHECK(solver.Stats().nodes <= base.Stats().nodes);
}
```

마지막 줄이 없으면 "가지치기를 켰는데 아무 일도 안 일어남"이 통과한다. 옳음과 효과를 함께 단언해야 한다.

## 7. 대칭 깨기와 canonical 집합

not-equal 제약만 있으면 값 이름은 완전히 교환 가능하다. 그러면 "이미 쓴 값 중 가장 큰 것 + 1"을 넘는 값을 새로 쓸 이유가 없다.

<!-- snippet: src/algo/backtrack.hpp@backtrack-solver mode=partial -->
```cpp
    // 값 대칭 깨기 마스크. not-equal 제약만 있으면 값 이름은 완전히 교환 가능하다 —
    // 그러면 "이미 쓴 값 중 가장 큰 것 + 1" 을 넘는 값을 새로 쓸 이유가 없다.
    // 잘려 나가는 것은 이름만 다른 같은 해뿐이다. 변수 선택 순서에 무관하도록
    // *배정된 모든* 변수를 본다 — 인덱스가 아니라 배정 여부가 기준이다.
    uint32_t CanonicalMask(size_t var) const {
        int32_t highest = -1;
        for (size_t v = 0; v < p_.variables; ++v) {
            if (v == var) continue;
            if (value_[v] != -1) highest = std::max(highest, value_[v]);
        }
        const uint32_t limit = uint32_t(highest + 1);
        if (limit >= 31) return ~0u;
        return (1u << (limit + 1)) - 1u;
    }
```

이 가지치기는 해의 **개수를 줄인다.** 그래서 "개수가 같다"로 검사할 수 없다. 대신 대칭을 끈 결과를 canonical화(값이 처음 등장하는 순서대로 0,1,2로 이름 재배정)한 뒤 중복을 제거한 집합과 비교한다.

```cpp
// 대칭 켠 결과 == 끈 결과의 canonical 집합.
// canonical 화: 값이 처음 등장하는 순서대로 0,1,2,... 로 이름을 바꾼다.
std::vector<std::vector<int32_t>> folded;
for (const std::vector<int32_t>& sol : full) {
    std::vector<int32_t> rename(32, -1);
    int32_t next = 0;
    std::vector<int32_t> out;
    for (int32_t v : sol) {
        if (rename[v] < 0) rename[v] = next++;
        out.push_back(rename[v]);
    }
    folded.push_back(out);
}
```

4변수 전부 서로 다른 값(4색)의 경우 전체 해가 24개(4!)이고 canonical 해는 1개다. 그 1이 24를 접은 결과와 같은지를 집합으로 확인한다.

분기 한정은 같은 골격의 최적화 판이다. 완화로 상계를 만들고 incumbent보다 나쁘면 가지를 버린다.

<!-- snippet: src/algo/backtrack.hpp@backtrack-branch-bound mode=partial -->
```cpp
// 0/1 배낭의 분기 한정. 완화(relaxation)로 상계를 만들고, 그 상계가 지금까지의
// 최선(incumbent)보다 나쁘면 그 가지를 통째로 버린다.
//   완화: 분할 가능 배낭 — 남은 물건을 밀도 순으로 담되 마지막을 쪼갠다.
// 완화가 *진짜 상계* 여야 한다. 상계가 실제 최적보다 낮으면 최적해를 잘라 낸다.
        if (f.at == items.size()) continue;
        if (FractionalBound(items, f.at, f.room, f.value) <= double(r.best)) {
            ++r.pruned_bound;                     // 이 가지는 최선을 넘을 수 없다
            continue;
        }
        stack.push_back(Frame{f.at + 1, f.room, f.value});          // 안 담는다
        if (items[f.at].weight <= f.room) {
            stack.push_back(Frame{f.at + 1, f.room - items[f.at].weight,
                                  f.value + items[f.at].value});    // 담는다
        } else {
            ++r.pruned_capacity;
        }
```

## 8. 측정과 관측

### 유량 세 팔

`algo_bench --flow`, 정점 160개의 무작위 DAG.

| arm | ms | augments | phases | value |
|---|---|---|---|---|
| ford-fulkerson | 1.00 | 181 | — | 1,001 |
| edmonds-karp | 0.75 | 120 | — | 1,001 |
| dinic | 0.36 | 120 | 12 | 1,001 |

Ford-Fulkerson이 증가를 181회 하는데 Edmonds-Karp는 120회다. DFS로 아무 경로나 잡으면 긴 경로를 타고 병목이 작아지고, BFS로 최단 경로를 잡으면 그런 일이 줄어든다. Dinic은 증가 횟수는 같지만 phase 12번으로 묶어 처리해 2배 빠르다 — 층 그래프를 한 번 만들어 그 안에서 여러 경로를 연속으로 밀기 때문이다.

### 매칭 세 팔

정점 900+900, 밀도 1%.

| arm | ms | size | augments | phases |
|---|---|---|---|---|
| dfs augmenting | 3.76 | 900 | 900 | — |
| hopcroft-karp | 0.24 | 900 | 900 | 4 |
| flow reduction | 2.04 | 900 | — | — |

증가 횟수는 900으로 같은데 시간이 15배 다르다. Hopcroft-Karp가 하는 일은 증가를 덜 하는 것이 아니라 **phase를 4번으로 묶는 것**이다. 각 phase에서 서로소인 최단 경로를 한꺼번에 찾으므로 BFS 층 계산이 900번이 아니라 4번이다.

유량 환원 팔은 DFS 판보다 빠르고 Hopcroft-Karp보다 느리다. 같은 문제를 더 일반적인 도구로 푸는 대가가 이 정도라는 것이 이 행의 뜻이다.

### CSP 가지치기

변수 9개, 값 4개, 각 변수가 두 이웃과 달라야 하는 제약.

| arm | ms | nodes | solutions | fwd prune | arc prune |
|---|---|---|---|---|---|
| plain | 0.086 | 1,841 | 528 | 0 | 0 |
| +forward check | 0.078 | 1,721 | 528 | 120 | 0 |
| +AC-3 | 1.490 | 1,649 | 528 | 0 | 72 |
| +MRV | 1.476 | 1,649 | 528 | 0 | 72 |
| +symmetry break | 0.065 | 71 | 22 | 0 | 3 |

읽을 것이 셋이다.

**AC-3가 노드를 줄이고 시간을 늘린다.** 1,721 → 1,649 노드(4% 감소)를 위해 0.078ms → 1.490ms(19배)를 쓴다. 매 노드마다 호 큐를 처음부터 다시 채우는 구현이라 상수가 크고, 이 문제의 제약이 성기어서 얻을 것이 적다. 가지치기의 강도와 비용은 별개이고, 강한 가지치기가 항상 이득은 아니다.

**MRV가 이 문제에서 아무것도 안 한다.** AC-3와 노드 수가 동일하다. 모든 변수가 대칭적인 구조라 "남은 값이 가장 적은 변수"가 사실상 아무거나이기 때문이다. 효과 없음을 그대로 표에 남긴다.

**대칭 깨기가 노드를 26배 줄인다.** 1,649 → 71. 대신 해가 528개에서 22개로 준다 — 잃은 506개는 이름만 다른 같은 해이고, 그것이 같음을 canonical 집합 대조가 확인한다. 값 대칭이 있는 문제에서 이 가지치기가 가장 큰 이득을 내는 이유가 여기 있다.

### 무력화 변이

**변이 1 — residual 짝 간선 갱신 제거.** `arc_[id ^ 1].flow -= amount;` 한 줄을 지웠다.

```text
  ok   flow_maxflow_arms
  FAIL flow_matching_arms
  ok   flow_min_cost_assignment
  ok   csp_apply_undo
  ok   csp_symmetry_breaking
```

`flow_maxflow_arms`가 살아남고 매칭이 죽는다. 이 결함은 역간선을 통한 "되돌리기"를 없애므로, 되돌릴 필요가 없는 그래프에서는 답이 그대로다. 매칭 환원 그래프는 되돌리기가 반드시 필요한 구조(왼쪽 정점이 이미 잡은 오른쪽을 양보해야 하는 경우)라 거기서 먼저 드러난다.

**변이 2 — CSP 되돌리기에서 지운 값을 복원하지 않기.** `domain_[u.var] |= u.removed;`를 지웠다. `csp_apply_undo`가 죽는다 — 상태 해시가 탐색 전후로 달라지고, 해 개수도 30보다 적게 나온다.

두 변이 모두 복원 뒤 소스 SHA-256이 주입 전과 동일함을 확인했다.

### 실행

```powershell
cmake --build build --config Release
.\build\Release\algo_tests.exe --step=07
```

```text
  ok   flow_maxflow_arms
  ok   flow_matching_arms
  ok   flow_min_cost_assignment
  ok   csp_apply_undo
  ok   csp_symmetry_breaking
algo_tests[step07]: 5/5 passed (354 check(s))
```

```powershell
.\build\Release\algo_tests.exe --properties --step=07
```

```text
  ok   flow/maxflow-mincut          표본   420 · 비퇴화  56% · 건너뜀 0
algo_tests[properties]: 1/1 passed (총 420 표본, seed 20260824)
```

이 property는 무작위 그래프마다 세 유량 팔이 같은 값을 내고, 용량 준수·유량 보존·원천 순유출이 성립하며, 그 값이 잔여 도달 집합의 절단 용량과 같은지를 확인한다. 기대값 표가 없어도 판정할 수 있다.

---

## 결론

max-flow min-cut을 정리가 아니라 실행 시 확인 가능한 등식으로 썼다. 그 덕에 답을 모르는 무작위 그래프에서도 "이 값이 최대인가"를 판정할 수 있고, property 층 420표본이 전부 그 등식으로 판정한다. 짝지어진 잔여 간선의 갱신을 빼는 결함은 값이 아니라 보존식과 매칭에서 먼저 드러났다.

세 유량 팔의 차이는 "경로를 어떻게 고르는가" 하나였고, 그 하나가 증가 횟수 181 대 120을 만들었다. 매칭에서는 증가 횟수가 같은데도 phase 묶기만으로 15배가 갈렸다 — 어떤 알고리즘의 이득은 일의 총량이 아니라 그 일의 **묶음 단위**에 있다.

CSP 쪽에서는 가지치기가 항상 이득이 아니라는 것이 수치로 나왔다. AC-3는 노드를 4% 줄이는 대가로 시간을 19배 썼고, MRV는 이 대칭적 문제에서 아무 효과가 없었다. 반면 값 대칭 깨기는 노드를 26배 줄였고, 잃은 해가 이름만 다른 같은 해임을 canonical 집합 대조로 확인했다.

여기서 다루지 않은 것은 push-relabel 계열과 일반 그래프(비이분) 매칭이다. 전자는 증가 경로라는 골격 자체를 버리는 접근이고, 후자는 blossom 축약이 필요해 이 lab의 세 팔이 공유하는 구조 밖에 있다.
