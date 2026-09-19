// greedy.hpp — 탐욕과 교환 논증.
//
// 탐욕이 옳다는 주장은 언제나 같은 모양이다. "최적해 하나를 가져와, 그것을
// 탐욕의 첫 선택으로 바꿔도 여전히 최적이고 여전히 유효하다" 를 보인다(교환 논증).
// 이 헤더는 그 논증이 *성립하는* 문제와 *깨지는* 문제를 나란히 둔다 —
// 활동 선택은 성립하고, 0/1 배낭과 동전 {1,3,4} 는 깨진다.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "measure/counters.hpp"

namespace algo {

// ---------------------------------------------------------------- 활동 선택

struct Interval { int64_t start; int64_t finish; };

// snippet-begin: greedy-activity
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
// snippet-end: greedy-activity

// snippet-begin: greedy-edd
// 최대 지연 최소화(EDD). 마감 오름차순으로 처리한다. 교환 논증이 여기서는
// "인접한 두 작업의 순서를 뒤집어도 최대 지연이 늘지 않는다" 형태로 온다.
struct Job { int64_t length; int64_t due; };

inline int64_t MaxLatenessEdd(std::vector<Job> jobs, std::vector<size_t>* order = nullptr) {
    std::vector<size_t> id(jobs.size());
    for (size_t i = 0; i < jobs.size(); ++i) id[i] = i;
    std::sort(id.begin(), id.end(), [&](size_t a, size_t b) {
        if (jobs[a].due != jobs[b].due) return jobs[a].due < jobs[b].due;
        return a < b;
    });
    int64_t clock = 0, worst = INT64_MIN;
    for (size_t k : id) {
        clock += jobs[k].length;
        const int64_t lateness = clock - jobs[k].due;
        if (lateness > worst) worst = lateness;
    }
    if (order) *order = id;
    return jobs.empty() ? 0 : worst;
}
// snippet-end: greedy-edd

// -------------------------------------------------------------------- 배낭

struct Item { int64_t weight; int64_t value; };

// snippet-begin: greedy-fractional
// 분할 가능 배낭은 탐욕이 *최적* 이다 — 밀도 높은 순으로 담다가 마지막을 쪼갠다.
// 쪼갤 수 있다는 것이 교환 논증의 마지막 단계를 완성한다. 0/1 제약이 붙는 순간
// 그 단계가 사라지고 탐욕은 근사조차 보장하지 못한다.
// 반환은 분할 가능 최적값 그 자체(double)다 — 마지막 물건의 몫만 value*room/weight 로 계산한다.
inline double FractionalKnapsack(std::vector<Item> items, int64_t capacity) {
    std::vector<size_t> id(items.size());
    for (size_t i = 0; i < items.size(); ++i) id[i] = i;
    std::sort(id.begin(), id.end(), [&](size_t a, size_t b) {
        const double da = double(items[a].value) / double(items[a].weight);
        const double db = double(items[b].value) / double(items[b].weight);
        if (da != db) return da > db;
        return a < b;
    });
    double total = 0.0;
    int64_t room = capacity;
    for (size_t k : id) {
        if (room <= 0) break;
        if (items[k].weight <= room) {
            total += double(items[k].value);
            room -= items[k].weight;
        } else {
            total += double(items[k].value) * double(room) / double(items[k].weight);
            room = 0;
        }
    }
    return total;
}

// 같은 밀도 순서를 0/1 제약에 그대로 쓴 것. 최적이 아니다 —
// 용량 50, (10,60)/(20,100)/(30,120) 에서 160 을 내고 최적은 220 이다.
inline int64_t DensityGreedyKnapsack(std::vector<Item> items, int64_t capacity,
                                     std::vector<size_t>* chosen = nullptr) {
    std::vector<size_t> id(items.size());
    for (size_t i = 0; i < items.size(); ++i) id[i] = i;
    std::sort(id.begin(), id.end(), [&](size_t a, size_t b) {
        const double da = double(items[a].value) / double(items[a].weight);
        const double db = double(items[b].value) / double(items[b].weight);
        if (da != db) return da > db;
        return a < b;
    });
    int64_t total = 0, room = capacity;
    if (chosen) chosen->clear();
    for (size_t k : id) {
        if (items[k].weight > room) continue;
        room -= items[k].weight;
        total += items[k].value;
        if (chosen) chosen->push_back(k);
    }
    if (chosen) std::sort(chosen->begin(), chosen->end());
    return total;
}
// snippet-end: greedy-fractional

// ---------------------------------------------------------------- 집합 덮개

// snippet-begin: greedy-set-cover
// 집합 덮개 탐욕. 매 단계에서 "아직 안 덮인 원소를 가장 많이 덮는" 집합을 고른다.
// 최적이 아니지만 근사비가 조화합 H(n) = 1 + 1/2 + ... + 1/n 이하로 묶인다.
// 가중 판은 "새로 덮는 원소당 비용" 이 최소인 집합을 고른다 — 같은 상계가 성립한다.
struct SetCoverResult {
    std::vector<size_t> chosen;
    int64_t cost = 0;
    bool covered = false;
};

inline SetCoverResult SetCoverGreedy(size_t universe,
                                     const std::vector<std::vector<uint32_t>>& sets,
                                     const std::vector<int64_t>& cost,
                                     measure::Counters* c = nullptr) {
    SetCoverResult r;
    std::vector<uint8_t> done(universe, 0);
    size_t remaining = universe;
    std::vector<uint8_t> used(sets.size(), 0);
    while (remaining > 0) {
        size_t pick = sets.size();
        double best_ratio = 0.0;
        size_t best_gain = 0;
        for (size_t s = 0; s < sets.size(); ++s) {
            if (used[s]) continue;
            size_t gain = 0;
            for (uint32_t e : sets[s]) if (!done[e]) ++gain;
            if (c) ++c->visited;
            if (gain == 0) continue;
            const double ratio = double(cost[s]) / double(gain);
            // 동점은 집합 번호가 작은 쪽 — 결과가 결정적이어야 오라클과 대조된다.
            if (pick == sets.size() || ratio < best_ratio) {
                pick = s; best_ratio = ratio; best_gain = gain;
            }
        }
        if (pick == sets.size()) break;   // 더 덮을 수 없다
        used[pick] = 1;
        r.chosen.push_back(pick);
        r.cost += cost[pick];
        for (uint32_t e : sets[pick]) if (!done[e]) { done[e] = 1; --remaining; }
        (void)best_gain;
    }
    r.covered = (remaining == 0);
    std::sort(r.chosen.begin(), r.chosen.end());
    return r;
}

// H(n). 근사비 상계를 실측 gap 과 나란히 두려면 이 값이 필요하다.
inline double HarmonicBound(size_t n) {
    double h = 0.0;
    for (size_t i = 1; i <= n; ++i) h += 1.0 / double(i);
    return h;
}
// snippet-end: greedy-set-cover

// ---------------------------------------------------------------- 동전 교환

// snippet-begin: greedy-coin
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
// snippet-end: greedy-coin

// ------------------------------------------------------------ 온라인 탐욕

// snippet-begin: greedy-rent-or-buy
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
// snippet-end: greedy-rent-or-buy

}  // namespace algo
