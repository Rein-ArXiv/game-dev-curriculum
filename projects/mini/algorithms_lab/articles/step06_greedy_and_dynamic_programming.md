## 들어가며

탐욕과 동적 계획을 한 단계에 둔다. 이유는 학습 편의가 아니라 **오라클이 같기 때문**이다. 작은 입력의 전수 열거 최적값 하나가 탐욕의 최적성·근사비와 DP 값을 동시에 판정한다. 두 기법을 나란히 두면 그 오라클을 한 번만 짓고 두 곳에서 쓴다.

탐욕이 옳다는 주장은 언제나 같은 모양이다 — 최적해 하나를 가져와 그것의 첫 선택을 탐욕의 첫 선택으로 바꿔도 여전히 최적이고 여전히 유효하다(교환 논증). 이 단계는 그 논증이 **성립하는** 문제와 **깨지는** 문제를 나란히 짓는다.

---

## 목차
1. [교환 논증이 성립하는 쪽](#1-교환-논증이-성립하는-쪽)
2. [교환 논증이 깨지는 쪽](#2-교환-논증이-깨지는-쪽)
3. [근사비의 상계와 실측](#3-근사비의-상계와-실측)
4. [1차원 압축과 순회 방향](#4-1차원-압축과-순회-방향)
5. [복원이 있는 DP와 없는 DP](#5-복원이-있는-dp와-없는-dp)
6. [구간·트리·비트마스크](#6-구간트리비트마스크)
7. [metamorphic 관계](#7-metamorphic-관계)
8. [측정과 관측](#8-측정과-관측)

---

## 1. 교환 논증이 성립하는 쪽

활동 선택은 "가장 빨리 **끝나는** 것"을 고른다. 가장 일찍 시작하는 것도, 가장 짧은 것도 아니다.

<!-- snippet: src/algo/greedy.hpp@greedy-activity -->
```cpp
// 가장 빨리 *끝나는* 것을 고른다. 가장 일찍 시작하는 것도, 가장 짧은 것도 아니다.
// 교환 논증: 최적해의 첫 활동을 "가장 빨리 끝나는 것" 으로 바꾸면 남는 시간이
// 줄지 않으므로 나머지 선택이 그대로 유효하다 — 개수가 줄지 않는다.
inline std::vector<size_t> ActivitySelect(std::vector<Interval> items,
                                          measure::Counters* c = nullptr) {
    std::vector<size_t> id(items.size());
    for (size_t i = 0; i < items.size(); ++i) id[i] = i;
    std::sort(id.begin(), id.end(), [&](size_t a, size_t b) {
        if (items[a].finish != items[b].finish) return items[a].finish < items[b].finish;
        return a < b;
    });
    std::vector<size_t> chosen;
    int64_t last_end = INT64_MIN;
    for (size_t k : id) {
        if (c) ++c->comparisons;
        if (items[k].start < last_end) continue;
        chosen.push_back(k);
        last_end = items[k].finish;
    }
    std::sort(chosen.begin(), chosen.end());
    return chosen;
}
```

검사는 논증을 믿지 않고 전수 열거로 확인한다. 활동 11개면 2,048가지 부분집합이고, 그중 겹치지 않는 것의 최대 개수를 직접 센다. 탐욕이 낸 4개가 그 최댓값과 같아야 한다. 고른 것들이 실제로 겹치지 않는지도 따로 본다 — 개수만 맞고 겹치는 답은 답이 아니다.

EDD(마감 오름차순 처리)는 최대 지연을 최소화한다. 여기서도 교환 논증이 성립하고, 검사는 720가지 순열을 전부 세어 확인한다.

## 2. 교환 논증이 깨지는 쪽

분할 가능 배낭은 탐욕이 최적이다 — 밀도 높은 순으로 담다가 마지막을 쪼갠다. **쪼갤 수 있다는 것**이 교환 논증의 마지막 단계를 완성한다.

<!-- snippet: src/algo/greedy.hpp@greedy-fractional mode=partial -->
```cpp
// 분할 가능 배낭은 탐욕이 *최적* 이다 — 밀도 높은 순으로 담다가 마지막을 쪼갠다.
// 쪼갤 수 있다는 것이 교환 논증의 마지막 단계를 완성한다. 0/1 제약이 붙는 순간
// 그 단계가 사라지고 탐욕은 근사조차 보장하지 못한다.
// 반환은 분할 가능 최적값 그 자체(double)다 — 마지막 물건의 몫만 value*room/weight 로 계산한다.
inline double FractionalKnapsack(std::vector<Item> items, int64_t capacity) {
    // ...
}

// 같은 밀도 순서를 0/1 제약에 그대로 쓴 것. 최적이 아니다 —
// 용량 50, (10,60)/(20,100)/(30,120) 에서 160 을 내고 최적은 220 이다.
inline int64_t DensityGreedyKnapsack(std::vector<Item> items, int64_t capacity,
                                     std::vector<size_t>* chosen = nullptr) {
    // ...
}
```

반례가 세 물건으로 끝난다.

```cpp
// 0/1 배낭. 밀도 탐욕은 160, 최적은 220.
const std::vector<Item> items = {{10, 60}, {20, 100}, {30, 120}};
CHECK(DensityGreedyKnapsack(items, 50) == 160);
CHECK(oracle::BruteKnapsack(items, 50) == 220);
// 분할 가능하면 같은 순서가 최적이다 — 교환 논증의 마지막 단계가 살아난다.
CHECK(FractionalKnapsack(items, 50) > 239.9 && FractionalKnapsack(items, 50) < 240.1);
```

밀도는 6, 5, 4다. 탐욕은 (10,60)과 (20,100)을 담아 용량 30을 쓰고 가치 160에서 멈춘다. 남은 20으로는 (30,120)이 안 들어간다. 최적은 밀도가 낮은 두 개 (20,100)+(30,120)을 담아 용량 50을 꽉 채우고 220이다. 쪼갤 수 있었다면 240이 나온다 — 세 값 160 / 220 / 240이 나란히 있는 것이 이 반례의 요지다.

동전 교환도 같은 구조다. 정준 화폐계에서는 탐욕이 최적이지만, 그것은 알고리즘의 성질이 아니라 **화폐 집합의 성질**이다.

<!-- snippet: src/algo/greedy.hpp@greedy-coin -->
```cpp
// 동전 교환 탐욕. 큰 액면부터 쓴다. 정준 화폐계에서는 최적이지만 {1,3,4} 로 6 을
// 만들면 4+1+1 로 3개를 쓴다 — 최적은 3+3 으로 2개다. 이 반례는 "탐욕이 옳은가"
// 가 알고리즘이 아니라 *입력 집합의 성질* 에 달렸다는 것을 보인다.
// 만들 수 없으면 -1.
inline int64_t CoinGreedy(std::vector<int64_t> coins, int64_t amount) {
    std::sort(coins.begin(), coins.end(), [](int64_t a, int64_t b) { return a > b; });
    int64_t used = 0;
    for (int64_t c : coins) {
        if (c <= 0) continue;
        const int64_t take = amount / c;
        used += take;
        amount -= take * c;
    }
    return (amount == 0) ? used : -1;
}
```

검사는 `{1,3,4}`로 6에서 탐욕 3개 대 최적 2개를 확인하고, 그 다음 `{1,5,10,25}`로 0부터 99까지 100개 금액 전부에서 탐욕과 최적이 일치함을 확인한다. 두 단정이 함께 있어야 "탐욕이 틀린다"가 아니라 "입력에 달렸다"가 된다. 최적값 오라클은 BFS 형태의 DP로, 탐욕과 골격이 다르다.

## 3. 근사비의 상계와 실측

집합 덮개 탐욕은 최적이 아니지만 근사비가 조화합 H(n) = 1 + 1/2 + ... + 1/n 이하로 묶인다.

<!-- snippet: src/algo/greedy.hpp@greedy-set-cover mode=partial -->
```cpp
// 집합 덮개 탐욕. 매 단계에서 "아직 안 덮인 원소를 가장 많이 덮는" 집합을 고른다.
// 최적이 아니지만 근사비가 조화합 H(n) = 1 + 1/2 + ... + 1/n 이하로 묶인다.
// 가중 판은 "새로 덮는 원소당 비용" 이 최소인 집합을 고른다 — 같은 상계가 성립한다.
struct SetCoverResult {
    std::vector<size_t> chosen;
    int64_t cost = 0;
    bool covered = false;
};
```

검사는 상계와 실측을 나란히 단언한다.

```cpp
const SetCoverResult greedy = SetCoverGreedy(8, sets, cost);
const int64_t best = oracle::BruteSetCover(8, sets, cost);
CHECK(greedy.covered);
CHECK(greedy.cost >= best);
CHECK(double(greedy.cost) <= double(best) * HarmonicBound(8) + 1e-9);
```

온라인 문제도 같은 형태로 다룬다. 스키 대여(하루 대여 1, 구매 B)에서 break-even 전략의 경쟁비는 2 - 1/B 이하다.

<!-- snippet: src/algo/greedy.hpp@greedy-rent-or-buy -->
```cpp
// 스키 대여 문제. 하루 대여 1, 구매 B. 며칠 탈지 모르는 채로 결정해야 한다.
// break-even 전략(B-1 일치 대여한 뒤 B 일째 구매)의 경쟁비는 2 - 1/B 이하다.
// 이 함수는 그 상계와 실측 최악비를 함께 낼 수 있게 비용 둘을 돌려준다.
struct RentOrBuy {
    int64_t online  = 0;
    int64_t offline = 0;   // 미래를 아는 최적
    double Ratio() const {
        return offline ? double(online) / double(offline) : 1.0;
    }
};

inline RentOrBuy SkiRental(int64_t days, int64_t buy_price, int64_t switch_after) {
    RentOrBuy r;
    if (days <= 0 || buy_price <= 0) return r;
    r.offline = (days < buy_price) ? days : buy_price;
    if (days <= switch_after) r.online = days;
    else                      r.online = switch_after + buy_price;
    return r;
}

inline double CompetitiveBound(int64_t buy_price) {
    return buy_price > 0 ? 2.0 - 1.0 / double(buy_price) : 1.0;
}
```

검사는 상계 준수와 **상계의 tightness**를 함께 본다.

```cpp
CHECK(worst <= CompetitiveBound(buy) + 1e-9);
CHECK(worst > CompetitiveBound(buy) - 1e-9);   // 상계가 헐겁지 않다
```

두 번째 줄이 없으면 상계가 아무리 헐거워도 통과한다. 헐거운 상계를 단언하는 검사는 검사가 아니다.

## 4. 1차원 압축과 순회 방향

0/1 배낭의 2차원 표는 정의가 그대로 코드가 된다. 표를 남겨 두면 복원도 자명하다 — 뒤에서부터 "값이 달라졌는가"만 보면 된다.

<!-- snippet: src/algo/dp.hpp@dp-knapsack-2d mode=partial -->
```cpp
// 0/1 배낭, 2차원 표. dp[i][w] = 앞 i 개만 쓰고 용량 w 일 때의 최댓값.
// 표를 그대로 두면 복원이 자명하다 — 뒤에서부터 "값이 달라졌는가" 를 보면 된다.
struct KnapsackResult {
    int64_t value = 0;
    std::vector<size_t> chosen;
    DpStats stats;
};
```

1차원으로 압축하면 순회 방향이 **의미를 바꾼다.**

<!-- snippet: src/algo/dp.hpp@dp-knapsack-1d -->
```cpp
// 1차원 압축. 순회 방향이 *의미* 를 바꾼다.
//   내림차순: dp[w - weight] 가 아직 이번 물건을 안 쓴 값이다 → 0/1 배낭
//   오름차순: 이미 이번 물건을 쓴 값을 다시 본다 → 같은 물건을 여러 번 → unbounded
// 같은 식에서 방향 하나로 두 문제가 갈린다.
inline int64_t Knapsack1D(const std::vector<Item>& items, int64_t capacity,
                          bool descending, DpStats* stats = nullptr) {
    const size_t cap = size_t(capacity < 0 ? 0 : capacity);
    std::vector<int64_t> dp(cap + 1, 0);
    for (const Item& it : items) {
        if (it.weight < 0) continue;   // 무게 0 인 물건도 0/1 의미 그대로 한 번만 더해진다
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
```

검사는 두 방향이 **다른 값**을 낸다는 것을 단언한다.

```cpp
const std::vector<Item> items = {{2, 3}, {3, 4}, {4, 5}};
const int64_t capacity = 8;
const int64_t once = Knapsack1D(items, capacity, true);    // 내림차순: 0/1
const int64_t many = Knapsack1D(items, capacity, false);   // 오름차순: unbounded
CHECK(once == oracle::BruteKnapsack(items, capacity));
CHECK(once == 9);           // (3,4)+(4,5) — 물건마다 한 번씩만
CHECK(many == 12);          // (2,3) 을 네 번 — 0/1 이면 불가능한 값
CHECK(many > once);
```

값 12는 0/1 제약에서 도달 불가능하다. 물건 셋의 가치 합이 3+4+5=12지만 무게 합이 9라 용량 8을 넘기 때문이다. 12가 나왔다는 것 자체가 "같은 물건을 여러 번 담았다"의 증거다.

## 5. 복원이 있는 DP와 없는 DP

LCS는 표를 남겨 복원한다. 별도 parent 표 없이 dp 값의 관계만으로 되짚을 수 있는 것이 이 recurrence의 성질이다.

<!-- snippet: src/algo/dp.hpp@dp-lcs mode=partial -->
```cpp
// 최장 공통 부분 수열. 복원은 표를 뒤에서 되짚는다 — 별도 parent 표를 두지 않고
// dp 값의 관계만으로 되짚을 수 있는 것이 이 recurrence 의 성질이다.
struct LcsResult {
    size_t length = 0;
    std::string sequence;
    DpStats stats;
};
```

검사는 길이만 보지 않는다. 복원된 문자열이 실제로 **양쪽의 부분 수열인지** 직접 확인한다 — 값은 맞고 복원이 틀린 결함이 흔하기 때문이다.

```cpp
// 복원된 것이 실제로 양쪽의 부분 수열인가 — 값만 맞고 복원이 틀린 결함을 잡는다.
for (const std::string& s : {tc.first, tc.second}) {
    size_t j = 0;
    for (char ch : s) if (j < lcs.sequence.size() && lcs.sequence[j] == ch) ++j;
    CHECK(j == lcs.sequence.size());
}
```

편집 거리는 롤링 배열로 두 줄만 들고 간다. 값은 얻지만 복원은 잃는다. 그 교환을 드러내려고 복원이 필요한 LCS와 나란히 두었다.

LIS는 두 판이다. O(n²)는 정의가 그대로 코드가 되고, O(n log n)은 다른 것을 유지한다.

<!-- snippet: src/algo/dp.hpp@dp-lis mode=partial -->
```cpp
// 최장 증가 부분 수열, 두 판.
//   O(n^2)  dp[i] = i 에서 끝나는 LIS 길이. 정의가 그대로 코드가 된다.
//   O(n log n) tail[k] = 길이 k+1 인 증가 수열의 *가능한 가장 작은 끝값*.
//   tail 은 정렬돼 있고, 그것이 이진 탐색이 가능한 유일한 이유다.
//   주의: tail 자체는 LIS 가 아니다 — 복원하려면 각 원소의 위치를 따로 기록해야 한다.
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
```

`tail` 배열을 그대로 답으로 내는 것이 흔한 결함이다. tail은 각 길이의 최소 끝값을 모은 것이지 실제 부분 수열이 아니다. 검사는 복원 결과가 증가하는지와 원본의 부분 수열인지를 함께 본다.

## 6. 구간·트리·비트마스크

구간 DP의 원형은 행렬 연쇄 곱이다. 상태가 구간이고 전이가 분할점이다. 손으로 아는 값(dims = {30,35,15,5,10,20,25}에서 15,125)으로 고정한다.

트리 DP는 상태가 (정점, 그 정점을 골랐는가) 둘이다.

<!-- snippet: src/algo/dp.hpp@dp-tree-mwis mode=partial -->
```cpp
// 트리 위 최대 가중 독립 집합. 상태가 (정점, 그 정점을 골랐는가) 둘이다.
// 트리라는 것이 여기서 하는 일은 딱 하나 — 자식들의 부분 문제가 서로 겹치지 않는다.
// 일반 그래프면 같은 정의가 성립하지 않는다(그 문제는 NP-hard 다).
        take[u] = weight[u];
        skip[u] = 0;
        for (uint32_t v : children[u]) {
            if (stats) ++stats->transitions;
            take[u] += skip[v];                     // 나를 고르면 자식은 못 고른다
            skip[u] += std::max(take[v], skip[v]);  // 안 고르면 자식은 자유다
        }
```

트리가 하는 일이 "자식들의 부분 문제가 겹치지 않는다"뿐이라는 점이 중요하다. 그 성질이 없으면 같은 정의가 성립하지 않는다.

비트마스크 DP는 상태 수가 n·2ⁿ이다. "지수"가 추상이 아니라 표 크기라는 것을 코드가 직접 말한다.

<!-- snippet: src/algo/dp.hpp@dp-bitmask mode=partial -->
```cpp
// 방문 집합을 비트마스크로 든 최소 비용 순회(열린 TSP). 상태 수가 n * 2^n 이라
// n 이 20 근처에서 메모리가 벽이 된다 — "지수" 가 추상이 아니라 표 크기다.
inline int64_t BitmaskMinTour(const std::vector<std::vector<int64_t>>& cost,
                              uint32_t start, DpStats* stats = nullptr) {
    const size_t n = cost.size();
    if (n == 0) return 0;
    if (n > 20) return -1;                       // 표가 감당 범위를 넘는다
    // ...
}
```

## 7. metamorphic 관계

전수 열거 오라클은 n이 작을 때만 쓸 수 있다. 큰 입력에서는 *입력 변환 전후의 관계*로 검증한다.

| 변환 | 기대 관계 |
|---|---|
| 모든 가치 × c | 최적값 × c |
| 용량 증가 | 최적값이 줄지 않는다 |
| 물건 순서 뒤집기 | 최적값 불변 |
| 두 문자열 앞에 같은 고유 심볼 | LCS + 1 |
| 트리 정점 id 순열 | 최대 가중 독립 집합 불변 |

이 관계들은 최적값을 몰라도 검사할 수 있다는 점에서 오라클과 다른 층이다. `dp_metamorphic`이 무작위 사례 15개에 대해 앞의 셋을, `dp_strings`가 LCS+1을, `dp_sequences_and_structures`가 트리 순열 불변을 확인한다.

## 8. 측정과 관측

### 배낭 네 팔

`algo_bench --dp`, 물건 22개.

| capacity | arm | ms | states/nodes | table KiB | gap |
|---|---|---|---|---|---|
| 60 | dp2d | 0.012 | 1,342 | 11.0 | 0 |
| 60 | dp1d | 0.001 | 1,180 | 0.5 | 0 |
| 60 | branch&bound | 0.004 | 42 | 0 | 0 |
| 60 | density greedy | 0.002 | 0 | 0 | 0 |
| 2,000 | dp2d | 0.256 | 44,022 | 359.6 | 0 |
| 2,000 | dp1d | 0.019 | 38,260 | 15.6 | 0 |
| 2,000 | branch&bound | 0.004 | 93 | 0 | 0 |
| 2,000 | density greedy | 0.001 | 0 | 0 | 0.0138 |

읽을 것이 셋이다.

**표 크기가 용량에 선형이다.** capacity 60에서 11KiB, 2,000에서 360KiB. 물건 수는 그대로인데 33배가 됐다. 배낭 DP가 "다항식"인 것은 *용량의 값*에 대해서지 입력 길이에 대해서가 아니다 — 용량을 십진 자릿수로 세면 지수다. 표 바이트 칸이 그 사실의 실측이다.

**1차원 압축이 표를 23배 줄이고 시간을 13배 줄인다.** 360KiB → 15.6KiB. 시간 이득이 나오는 이유는 압축된 표가 L1에 들어가기 때문이고, 잃는 것은 복원이다.

**분기 한정이 노드 93개로 끝난다.** 2²² = 4,194,304가지 조합을 완화 상계로 잘라 낸 결과다. capacity 2,000에서 DP보다 60배 빠르다. 다만 이것은 이 입력의 성질이다 — 완화가 느슨해지는 입력(가치와 무게가 무관한 경우)에서는 가지치기가 잘 안 되고 지수로 되돌아간다.

**밀도 탐욕의 gap이 1.38%다.** capacity 60에서는 0이고 2,000에서는 1.38%. 이 벤치는 무게 상한을 용량의 4분의 1로 잡아 무게가 용량에 비례하므로, 용량이 커져도 "마지막에 남는 자투리"의 상대 비중은 줄지 않는다 — 두 값의 차이는 용량의 효과가 아니라 두 입력의 성질이다. 이 값은 상계가 아니라 이 입력의 실측이며, 앞의 (10,60)/(20,100)/(30,120) 반례에서는 27%였다.

### 집합 덮개의 gap

| universe | sets | greedy | optimum | ratio | H(n) 상계 |
|---|---|---|---|---|---|
| 8 | 16 | 5 | 4 | 1.2500 | 2.7179 |
| 10 | 18 | 2 | 2 | 1.0000 | 2.9290 |

실측 근사비가 상계보다 훨씬 낮다. H(n) 상계는 최악을 위한 것이고, 무작위 사례에서 최악이 나오는 일은 드물다. 상계가 헐겁다는 사실 자체가 관찰 대상이다 — "이론 상계가 2.7이니 실제로도 2.7배쯤 나쁘겠지"라는 추론이 왜 틀리는지를 보여 준다.

### 무력화 변이

**0/1 배낭의 이전 행 참조를 현재 행으로.** `dp[i-1][w - weight]`를 `dp[i][w - weight]`로 바꿨다. 2차원 표를 쓰면서 unbounded 의미가 된다.

```text
  ok   greedy_exchange_holds
  ok   greedy_exchange_breaks
  FAIL dp_knapsack_arms
  FAIL dp_direction_changes_problem
  ok   dp_strings
  ok   dp_sequences_and_structures
  ok   dp_metamorphic
algo_tests: 42 check(s) failed
```

둘이 함께 죽는다. `dp_knapsack_arms`는 전수 열거 오라클과의 불일치로, `dp_direction_changes_problem`은 2차원 판이 9가 아니라 12를 내는 것으로 잡는다. 후자가 잡는 방식이 더 좁고 직접적이다 — "2차원 표는 방향에 무관하게 0/1이다"라는 단언이 곧 이 결함의 정의이기 때문이다.

`dp_metamorphic`은 **살아남는다.** 그 검사가 보는 것은 "가치를 c배 하면 최적값도 c배", "용량이 늘면 값이 줄지 않는다", "물건 순서 뒤집기에 불변"인데, unbounded 배낭도 이 세 관계를 전부 만족한다. metamorphic 관계는 절대값을 모르고도 검사할 수 있는 대신, *다른 문제로 바뀌는* 결함에는 무력하다. 이것이 metamorphic 층의 정직한 경계다.

복원 뒤 소스 SHA-256이 주입 전과 동일함을 확인했다.

### 실행

```powershell
cmake --build build --config Release
.\build\Release\algo_tests.exe --step=06
```

```text
  ok   greedy_exchange_holds
  ok   greedy_exchange_breaks
  ok   dp_knapsack_arms
  ok   dp_direction_changes_problem
  ok   dp_strings
  ok   dp_sequences_and_structures
  ok   dp_metamorphic
algo_tests[step06]: 7/7 passed (491 check(s))
```

```powershell
.\build\Release\algo_tests.exe --properties --step=06
```

```text
  ok   dp/knapsack-vs-brute         표본   500 · 비퇴화  49% · 건너뜀 0
algo_tests[properties]: 1/1 passed (총 500 표본, seed 20260824)
```

이 property는 무작위 배낭마다 DP 두 판과 분기 한정이 전수 열거 최적값과 같고, 탐욕은 그 이하이며, 가치를 3배 하면 최적값도 3배임을 함께 확인한다.

---

## 결론

교환 논증이 성립하는 문제와 깨지는 문제를 같은 오라클로 판정했다. 활동 선택과 EDD에서는 탐욕이 전수 열거 최적과 일치했고, 0/1 배낭에서는 160 대 220으로, 동전 {1,3,4}로 6에서는 3개 대 2개로 갈렸다. 동전 쪽은 같은 알고리즘이 정준 화폐계에서는 100개 금액 전부에서 최적이라는 것을 함께 단언해, 결론이 "탐욕이 틀린다"가 아니라 "입력 집합의 성질에 달렸다"가 되게 했다.

1차원 압축의 순회 방향이 두 개의 다른 문제를 만든다는 것은 9와 12라는 두 값으로 확정했다. 12는 0/1 제약에서 도달 불가능한 값이므로, 그 숫자 자체가 unbounded 의미론의 증거다.

측정에서 드러난 것은 배낭 DP의 "다항식"이 용량의 *값*에 대한 것이라는 사실이다. 용량을 33배 키우자 표가 33배 커졌다. 그리고 집합 덮개의 실측 근사비가 조화합 상계의 절반도 안 됐다 — 최악 상계로 평균을 예측하면 안 된다는 것의 실측이다.

여기서 다루지 않은 것은 근사 스킴(FPTAS)과 완화 기반 하계다. 분기 한정의 완화를 분할 가능 배낭 하나로 고정했고, 더 강한 완화(Lagrangian, LP)로 가지를 더 자르는 방향은 구현 범위 밖이다.
