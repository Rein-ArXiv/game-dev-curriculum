## 들어가며

이 단계의 중심 질문은 하나다. **요약이 역연산을 가지는가.**

합은 가진다. 두 접두합의 차 하나로 임의 구간의 합이 복원된다. 최댓값은 가지지 않는다 — 창에서 원소 하나가 빠질 때 무엇을 빼야 할지 알 수 없다. 이 차이는 설명으로 전달되는 것이 아니라 *자료구조 선택 자체*가 드러낸다. 그래서 접두합·차분 배열·두 포인터는 합 쪽에, 단조 deque·단조 stack·segment tree는 최댓값 쪽에 선다.

여기에 직접 구현한 이진 힙과 좌표 압축, Fenwick tree, lazy propagation segment tree를 더한다.

---

## 목차
1. [접두합과 차분 배열](#1-접두합과-차분-배열)
2. [역연산이 없는 요약](#2-역연산이-없는-요약)
3. [Fenwick tree와 1-based 경계](#3-fenwick-tree와-1-based-경계)
4. [lazy tag의 대수](#4-lazy-tag의-대수)
5. [결합 요약으로 최대 부분합](#5-결합-요약으로-최대-부분합)
6. [우선순위 큐 두 팔](#6-우선순위-큐-두-팔)
7. [측정과 관측](#7-측정과-관측)

---

## 1. 접두합과 차분 배열

접두합 배열의 크기가 n이 아니라 n+1인 것이 요점이다. 빈 구간을 표현할 자리가 있어야 `[l, r)` 질의가 경계에서 갈라지지 않는다.

<!-- snippet: src/algo/range.hpp@range-prefix -->
```cpp
// prefix[i] = a[0..i). 크기가 n+1 인 것이 요점이다 — 빈 구간을 표현할 자리가
// 있어야 [l, r) 질의가 경계에서 갈라지지 않는다.
inline std::vector<int64_t> PrefixSum(const std::vector<int64_t>& a) {
    std::vector<int64_t> p(a.size() + 1, 0);
    for (size_t i = 0; i < a.size(); ++i) p[i + 1] = p[i] + a[i];
    return p;
}

inline int64_t RangeSum(const std::vector<int64_t>& prefix, size_t l, size_t r) {
    return prefix[r] - prefix[l];   // [l, r)
}
```

차분 배열은 접두합의 쌍대다. 구간 갱신이 O(1)이고 확정이 O(n)이다 — 갱신이 몰리고 조회가 마지막에 한 번뿐인 워크로드의 답이다. 검사는 모든 `(l, r)` 쌍에 대해 접두합의 답과 직접 합산의 답을 대조한다. n=8이면 45쌍이고, 한 칸 밀린 구현은 그중 어딘가에서 반드시 걸린다.

## 2. 역연산이 없는 요약

폭 w 슬라이딩 최댓값은 deque가 값 내림차순을 유지하는 것으로 푼다.

<!-- snippet: src/algo/range.hpp@range-sliding-max -->
```cpp
// 폭 w 슬라이딩 최댓값. deque 는 *값 내림차순* 을 유지한다.
//   뒤 제거 조건이 `<` 인지 `<=` 인지가 정확성이 아니라 deque 크기를 바꾼다.
//   `<=` 로 같은 값을 밀어내면 중복이 쌓이지 않는다. 어느 쪽이든 최댓값은 같지만,
//   `<` 로 두고 인덱스 만료를 앞에서 지우지 않으면 만료된 값이 답으로 나온다.
inline std::vector<int64_t> SlidingMax(const std::vector<int64_t>& a, size_t w,
                                       measure::Counters* c = nullptr) {
    std::vector<int64_t> out;
    if (w == 0 || a.size() < w) return out;
    std::deque<size_t> dq;   // 인덱스. 값 내림차순.
    for (size_t i = 0; i < a.size(); ++i) {
        while (!dq.empty() && dq.front() + w <= i) dq.pop_front();   // 만료
        while (!dq.empty() && a[dq.back()] <= a[i]) {
            if (c) ++c->comparisons;
            dq.pop_back();
        }
        dq.push_back(i);
        if (i + 1 >= w) out.push_back(a[dq.front()]);
    }
    return out;
}
```

deque가 값이 아니라 *인덱스*를 담는 것이 만료 처리의 전부다. 값만 담으면 그 값이 창 밖으로 나갔는지 알 수 없다.

단조 stack은 같은 아이디어를 다른 방향으로 쓴다. 각 인덱스가 한 번 들어가고 한 번 나오므로 총 비용이 선형이다.

<!-- snippet: src/algo/range.hpp@range-next-greater -->
```cpp
// 단조 stack. out[i] = 오른쪽에서 처음으로 a[i] 보다 큰 원소의 인덱스, 없으면 n.
// stack 은 값 증가 순서를 유지하지 않는다 — *감소* 순서를 유지하고, 새 값이
// 그보다 크면 팝하면서 답을 확정한다. 각 인덱스가 한 번 들어가고 한 번 나온다.
inline std::vector<size_t> NextGreater(const std::vector<int64_t>& a,
                                       measure::Counters* c = nullptr) {
    const size_t n = a.size();
    std::vector<size_t> out(n, n);
    std::vector<size_t> st;
    for (size_t i = 0; i < n; ++i) {
        while (!st.empty() && a[st.back()] < a[i]) {
            if (c) ++c->comparisons;
            out[st.back()] = i;
            st.pop_back();
        }
        st.push_back(i);
    }
    return out;
}
```

두 포인터는 사전조건이 있는 기법이다. 값이 모두 양수여야 "오른쪽을 늘리면 합이 늘고 왼쪽을 줄이면 합이 준다"는 단조성이 성립한다. 음수를 허용하면 이 기법은 틀린 답을 낸다 — 코드를 고쳐서 되는 문제가 아니라 전제가 사라진 것이다.

## 3. Fenwick tree와 1-based 경계

Fenwick tree의 내부는 1-based다. `i & -i`가 "가장 낮은 켜진 비트"를 뜻하려면 인덱스가 1부터여야 한다. 바깥 API는 0-based로 받고 안에서 +1 한다.

<!-- snippet: src/algo/range.hpp@range-fenwick -->
```cpp
// Fenwick(binary indexed) tree. 내부는 1-based 다 — i & -i 가 "가장 낮은 켜진
// 비트" 를 뜻하려면 인덱스가 1 부터여야 한다. 바깥 API 는 0-based 로 받고
// 안에서 +1 한다. 이 경계에서 한 칸이 어긋나면 대부분의 질의는 여전히 그럴듯한
// 값을 내고 마지막 원소에서만 틀린다.
class Fenwick {
public:
    explicit Fenwick(size_t n) : n_(n), t_(n + 1, 0) {}

    void Add(size_t index0, int64_t delta, measure::Counters* c = nullptr) {
        for (size_t i = index0 + 1; i <= n_; i += i & (~i + 1)) {
            t_[i] += delta;
            if (c) ++c->visited;
        }
    }
    // a[0..count) 의 합
    int64_t Prefix(size_t count, measure::Counters* c = nullptr) const {
        int64_t s = 0;
        for (size_t i = count; i > 0; i -= i & (~i + 1)) {
            s += t_[i];
            if (c) ++c->visited;
        }
        return s;
    }
    // ...
    // binary lifting: prefix 합이 처음으로 target 이상이 되는 위치.
    // 모든 값이 음이 아니어야 prefix 가 단조가 되고, 그래야 이 탐색이 성립한다.
    size_t LowerBoundPrefix(int64_t target, measure::Counters* c = nullptr) const {
        size_t pos = 0;
        int64_t rest = target;
        size_t step = 1;
        while (step * 2 <= n_) step *= 2;
        for (; step > 0; step /= 2) {
            if (pos + step <= n_ && t_[pos + step] < rest) {
                pos += step;
                rest -= t_[pos];
                if (c) ++c->visited;
            }
        }
        return pos;   // 0-based 로 pos 번째가 답
    }
    // ...
};
```

`i & (~i + 1)`은 `i & -i`와 같지만 부호 없는 타입에서 단항 마이너스 경고를 피한다.

binary lifting 검색은 "접두합이 처음으로 target 이상이 되는 위치"를 트리 위에서 O(log n)에 찾는다. 이 탐색이 성립하는 유일한 근거는 값이 음이 아니어서 접두합이 단조라는 것이다. 음수 값이 하나라도 들어가면 이 함수는 아무 의미도 없는 값을 낸다 — 그것이 이 함수의 사전조건이고, 검사도 음이 아닌 값으로만 부른다.

검사는 200회의 무작위 갱신 뒤 매번 무작위 구간을 직접 합산과 대조하고, 마지막 원소를 따로 한 번 더 본다. 1-based/0-based 혼선은 대부분의 질의를 통과시키고 정확히 마지막 원소에서만 틀리기 때문이다.

## 4. lazy tag의 대수

구간 대입과 구간 덧셈이 섞이면 순서가 의미를 가진다. 대입 뒤 덧셈은 "대입값 + 덧셈값"을 대입한 것과 같고, 덧셈 뒤 대입은 그냥 대입이다 — 앞의 덧셈이 지워진다.

<!-- snippet: src/algo/range.hpp@range-lazy-tag -->
```cpp
// lazy tag 의 대수. 두 연산(구간 대입 · 구간 덧셈)이 섞이면 *순서* 가 의미를 가진다.
//   대입 뒤 덧셈  = (대입값 + 덧셈값) 을 대입한 것과 같다
//   덧셈 뒤 대입  = 그냥 대입 — 앞의 덧셈이 지워진다
// Compose(old, fresh) 는 "old 를 먼저, fresh 를 나중에 적용한 것과 같은 하나의
// tag" 를 만든다. 이 규칙이 틀리면 같은 갱신 집합이 순서에 따라 다른 답을 낸다.
//
// 계약 두 줄:
//   Apply(Compose(a, b), x) == Apply(b, Apply(a, x))          (합성 = 순차 적용)
//   Compose(Compose(a, b), c) == Compose(a, Compose(b, c))    (결합법칙)
struct Tag {
    bool    assign     = false;
    int64_t assign_val = 0;
    int64_t add        = 0;

    bool Identity() const { return !assign && add == 0; }

    static Tag Compose(const Tag& old_tag, const Tag& fresh) {
        Tag out = old_tag;
        if (fresh.assign) {          // 나중에 오는 대입이 앞의 전부를 지운다
            out.assign     = true;
            out.assign_val = fresh.assign_val;
            out.add        = 0;
        }
        out.add += fresh.add;        // 덧셈은 대입 뒤에도 살아남는다
        return out;
    }
    int64_t ApplyValue(int64_t v) const { return (assign ? assign_val : v) + add; }
};
```

검사가 트리를 보기 전에 이 두 줄을 먼저 본다. tag 네 종류의 모든 쌍과 삼중쌍에 대해 합성과 순차 적용이 같은지, 그리고 합성이 결합법칙을 만족하는지를 값 네 개에서 단언한다. 트리는 그 위에 선다 — 대수가 틀리면 트리는 어떻게 짜도 틀린다.

트리 쪽 검사는 300라운드 무작위 연산이다. 매 라운드 대입·덧셈·질의 중 하나를 고르고, 같은 연산을 평범한 배열에도 적용한 뒤 구간 합을 대조한다.

## 5. 결합 요약으로 최대 부분합

최대 연속 부분합을 구간 합성으로 답하려면 네 값이 필요하다.

<!-- snippet: src/algo/range.hpp@range-segment-summary -->
```cpp
// 결합 요약. 최대 연속 부분합을 구간 합성으로 답하려면 네 값이 필요하다 —
// 전체 합, 접두 최대, 접미 최대, 구간 안 최대. 합성이 결합법칙을 만족하는 것이
// 트리가 성립하는 유일한 근거다.
struct Summary {
    int64_t total  = 0;
    int64_t prefix = 0;
    int64_t suffix = 0;
    int64_t best   = 0;
    bool    empty  = true;

    static Summary Leaf(int64_t v) {
        Summary s;
        s.total = v; s.prefix = v; s.suffix = v; s.best = v; s.empty = false;
        return s;
    }
    static Summary Merge(const Summary& l, const Summary& r) {
        if (l.empty) return r;
        if (r.empty) return l;
        Summary s;
        s.empty  = false;
        s.total  = l.total + r.total;
        s.prefix = std::max(l.prefix, l.total + r.prefix);
        s.suffix = std::max(r.suffix, r.total + l.suffix);
        // 경계를 넘는 후보가 l.suffix + r.prefix 다. 이 항을 빼면 답이 조각난다.
        s.best   = std::max(std::max(l.best, r.best), l.suffix + r.prefix);
        return s;
    }
};
```

이 요약을 쓰는 트리는 **점 대입만** 지원한다. 그것은 게으름이 아니라 대수의 결과다.

<!-- snippet: src/algo/range.hpp@range-maxsub-tree -->
```cpp
// 최대 연속 부분합 트리. 갱신은 점 대입뿐이다 — 구간 덧셈을 지원하지 않는 것이
// 게으름이 아니라 대수의 결과다. 상수 c 를 구간에 더했을 때 새 best 는 옛 요약
// 네 값만으로 결정되지 않는다(최적 부분합의 *길이* 를 알아야 c*len 을 더한다).
// 요약에 길이를 넣어도 최적 부분합의 길이는 c 에 따라 바뀐다. 닫히지 않는다.
class MaxSubarrayTree {
public:
    explicit MaxSubarrayTree(const std::vector<int64_t>& a) : n_(a.size()) {
        node_.assign(4 * (n_ ? n_ : 1), Summary{});
        if (n_) Build(1, 0, n_ - 1, a);
    }
    // ...
};
```

그래서 이 lab에는 트리가 둘이다. 합 트리는 lazy 대입·덧셈을 감당하고(합은 tag 아래에서 닫힌 형태로 갱신된다), 최대 부분합 트리는 점 대입만 받는다. 하나로 합치려는 시도가 잘못되는 자리를 코드 모양이 그대로 보여 준다.

## 6. 우선순위 큐 두 팔

직접 구현한 최소 힙은 우선순위 감소 연산을 지원하지 않는다. 대신 새 항목을 넣고 옛 항목은 세대 번호로 무효화한다.

<!-- snippet: src/algo/range.hpp@range-binary-heap -->
```cpp
// 직접 구현한 최소 힙. 게임 루프가 쓰는 형태를 그대로 둔다 —
// 우선순위가 *내려가는* 갱신을 지원하지 않고, 대신 새 항목을 넣고 옛 항목은
// 세대 번호로 무효화한다(lazy deletion). 상각은 싸지만 만료 항목이 쌓인 뒤
// 한 번에 정리되는 tick 이 P99 spike 로 나타난다.
template <class T>
class BinaryHeap {
public:
    struct Node { int64_t key; T value; uint32_t gen; };

    void Push(int64_t key, const T& value, uint32_t gen, measure::Counters* c = nullptr) {
        h_.push_back(Node{key, value, gen});
        size_t i = h_.size() - 1;
        while (i > 0) {
            const size_t parent = (i - 1) / 2;
            if (c) ++c->comparisons;
            if (h_[parent].key <= h_[i].key) break;
            std::swap(h_[parent], h_[i]);
            if (c) c->moved_bytes += 3 * sizeof(Node);
            i = parent;
        }
    }
    // ...
};
```

sift-down에서 오른쪽 자식을 *존재할 때만* 비교하는 것이 마지막 원소 처리의 전부다. `child + 1 < n`을 `<=`로 쓰면 힙 밖을 읽는다.

대조군은 정렬된 배열이다. 같은 인터페이스를 내지만 삽입이 O(n)이다. 두 자료구조는 같은 pop 순서 계약을 지켜야 하고, 검사는 500개 무작위 키에서 그것을 단언한다.

## 7. 측정과 관측

### 구간 질의 세 팔

`algo_bench --queue`의 질의/갱신 비율 스윕이다. 연산 20만 회, 갱신은 점 갱신, 질의는 무작위 구간 합.

| n | 질의 비율 | naive ms | fenwick ms | segtree ms |
|---|---|---|---|---|
| 16 | 10% | 0.51 | 1.67 | 9.96 |
| 16 | 90% | 2.36 | 2.44 | 9.83 |
| 1,024 | 10% | 1.03 | 2.71 | 27.42 |
| 1,024 | 90% | 5.96 | 3.59 | 33.59 |
| 65,536 | 10% | 34.60 | 3.46 | 52.82 |
| 65,536 | 90% | 303.08 | 4.73 | 67.70 |

이 표의 결론은 두 줄이다.

**n이 수십 규모면 선형 스캔이 이긴다.** n=16, 질의 10%에서 naive가 Fenwick의 3분의 1이다. 질의 90%로 올려도 겨우 비슷해진다. 트리 순회의 분기와 포인터 산술이 16개 원소를 그냥 더하는 것보다 비싸다는 뜻이고, 점근은 이 구간에 대해 아무 말도 하지 않는다.

**n=65,536에서는 자릿수가 갈린다.** 질의 90%에서 naive 303ms 대 Fenwick 4.7ms — 64배다. 교차점은 이 기계에서 n이 대략 1,000 근처에 섰다.

segment tree가 Fenwick보다 10배 이상 느린 것은 점근이 같음에도 그렇다. Fenwick은 배열 하나에 루프 두 개고, segment tree는 재귀 호출과 lazy push가 붙는다. 대신 segment tree는 합 말고도 결합 가능한 어떤 요약이든 답할 수 있다 — 그 일반성의 가격이 이 배수다.

우선순위 큐 두 팔은 규모를 다르게 잡아야 비교가 성립한다.

| arm | n | ms | compares | moved KiB |
|---|---|---|---|---|
| binary heap | 200,000 | 18.78 | 6,519,730 | 152,699 |
| sorted array | 2,001 | 0.52 | 19,189 | 15,656 |

정렬 배열은 삽입마다 뒤쪽 전체를 밀어야 해서 n을 100분의 1로 줄여야 끝난다. 그런데도 이동 바이트가 힙의 10분의 1에 이른다 — n을 100배 줄였는데 이동은 10분의 1이라는 것은, 같은 n이면 이동이 10배라는 뜻이다.

### 무력화 변이

**`Tag::Compose`에서 대입이 앞의 덧셈을 지우지 않게.** `out.add = 0;` 한 줄을 지웠다. 대부분의 갱신 열은 여전히 맞는 답을 낸다 — 대입과 덧셈이 같은 구간에 겹쳐 들어와야만 드러나기 때문이다.

```text
  ok   range_prefix_and_difference
  ok   range_sliding_and_monotonic
  ok   range_fenwick
  FAIL range_segment_tree_algebra
  ok   range_max_subarray_tree
  ok   range_priority_queue_arms
algo_tests: 4 check(s) failed
```

죽은 것이 트리 검사가 아니라 *대수 검사*라는 점이 중요하다. `Apply(Compose(a, b), x) == Apply(b, Apply(a, x))`를 tag 쌍 전부에서 확인하는 열여섯 줄이 결함을 트리보다 먼저, 더 좁게 잡는다. 복원 뒤 소스 SHA-256이 주입 전과 동일함을 확인했다.

### 실행

```powershell
cmake --build build --config Release
.\build\Release\algo_tests.exe --step=02
```

```text
  ok   range_prefix_and_difference
  ok   range_sliding_and_monotonic
  ok   range_fenwick
  ok   range_segment_tree_algebra
  ok   range_max_subarray_tree
  ok   range_priority_queue_arms
algo_tests[step02]: 6/6 passed (2206 check(s))
```

property 층은 같은 계약을 무작위 갱신 열에서 확인한다.

```powershell
.\build\Release\algo_tests.exe --properties --step=02
```

```text
  ok   range/fenwick-vs-scan        표본   450 · 비퇴화  45% · 건너뜀 0
  ok   range/segment-lazy           표본   700 · 비퇴화  66% · 건너뜀 0
algo_tests[properties]: 2/2 passed (총 1150 표본, seed 20260824)
```

측정 표는 `.\build\Release\algo_bench.exe --queue`로 따로 낸다.

---

## 결론

역연산의 유무가 자료구조를 가른다. 합은 접두합 차 하나로 임의 구간을 답하고, 최댓값은 그럴 수 없어 단조 자료구조나 트리를 요구한다. 그 경계는 lazy tag를 섞을 때 다시 나타난다 — 합은 tag 아래에서 닫힌 형태로 갱신되지만 최대 부분합은 그렇지 않아, 이 lab의 트리가 둘로 나뉘었다.

측정이 보인 것은 점근이 침묵하는 구간이다. n이 수십일 때는 선형 스캔이 Fenwick을 이기고, 교차점은 이 기계에서 n≈1,000 근처였다. 같은 O(log n)인 Fenwick과 segment tree 사이에도 10배 이상의 상수 차이가 있고, 그 차이가 사는 이유는 segment tree가 합 말고도 답할 수 있다는 일반성이다.

여기서 만든 트리는 정적 크기다. 삽입·삭제로 원소 수가 바뀌는 동적 구간 자료구조와 영속(persistent) 판본은 이 구현 범위 밖이고, 좌표 압축은 그 부재를 우회하는 장치일 뿐 대체하지 않는다.
