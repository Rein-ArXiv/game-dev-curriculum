// range.hpp — 온라인 · 구간 · 스트림.
//
// 이 헤더의 중심 질문은 "요약이 역연산을 가지는가" 다.
//   합은 가진다  → prefix 차 하나로 임의 구간을 복원할 수 있다 (Fenwick 이 가능한 이유)
//   최댓값은 없다 → 무엇을 빼야 할지 알 수 없다 (그래서 단조 deque 나 트리가 필요하다)
// 이 차이는 코드로 설명하는 것이 아니라 *자료구조 선택 자체* 가 보여 준다.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

#include "measure/counters.hpp"

namespace algo {

// ------------------------------------------------------------ prefix · 차분

// snippet-begin: range-prefix
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
// snippet-end: range-prefix

// 2D prefix. 포함-배제로 네 항을 더하고 뺀다.
struct Prefix2D {
    size_t w = 0, h = 0;
    std::vector<int64_t> p;   // (h+1) x (w+1)

    void Build(const std::vector<int64_t>& grid, size_t width, size_t height) {
        w = width; h = height;
        p.assign((h + 1) * (w + 1), 0);
        for (size_t y = 0; y < h; ++y) {
            for (size_t x = 0; x < w; ++x) {
                p[(y + 1) * (w + 1) + (x + 1)] =
                    grid[y * w + x] + p[y * (w + 1) + (x + 1)] +
                    p[(y + 1) * (w + 1) + x] - p[y * (w + 1) + x];
            }
        }
    }
    // [x0, x1) x [y0, y1)
    int64_t Sum(size_t x0, size_t y0, size_t x1, size_t y1) const {
        return p[y1 * (w + 1) + x1] - p[y0 * (w + 1) + x1]
             - p[y1 * (w + 1) + x0] + p[y0 * (w + 1) + x0];
    }
};

// 차분 배열. 구간 갱신 O(1), 최종 확정 O(n). prefix 의 쌍대다.
struct Difference {
    std::vector<int64_t> d;

    explicit Difference(size_t n) : d(n + 1, 0) {}
    void AddRange(size_t l, size_t r, int64_t v) { d[l] += v; d[r] -= v; }
    std::vector<int64_t> Materialize() const {
        std::vector<int64_t> out(d.size() - 1, 0);
        int64_t run = 0;
        for (size_t i = 0; i + 1 < d.size(); ++i) { run += d[i]; out[i] = run; }
        return out;
    }
};

// ------------------------------------------------------------ 두 포인터

// snippet-begin: range-two-pointer
// 합이 target 이상인 가장 짧은 구간. 값이 모두 양수라는 사전조건이 있어야
// 오른쪽을 늘리면 합이 늘고 왼쪽을 줄이면 합이 준다는 단조성이 성립한다.
// 이 단조성이 깨지면(음수 허용) 두 포인터는 틀린 답을 낸다.
inline size_t ShortestAtLeast(const std::vector<int64_t>& a, int64_t target,
                              measure::Counters* c = nullptr) {
    size_t best = a.size() + 1;
    int64_t sum = 0;
    size_t l = 0;
    for (size_t r = 0; r < a.size(); ++r) {
        sum += a[r];
        while (sum >= target) {
            if (c) ++c->visited;
            const size_t len = r - l + 1;
            if (len < best) best = len;
            sum -= a[l++];
        }
    }
    return (best == a.size() + 1) ? 0 : best;
}
// snippet-end: range-two-pointer

// snippet-begin: range-sliding-max
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
// snippet-end: range-sliding-max

// snippet-begin: range-next-greater
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
// snippet-end: range-next-greater

// ------------------------------------------------------------ 이진 힙

// snippet-begin: range-binary-heap
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

    bool Empty() const { return h_.empty(); }
    size_t Size() const { return h_.size(); }
    const Node& Top() const { return h_.front(); }

    Node Pop(measure::Counters* c = nullptr) {
        Node top = h_.front();
        h_.front() = h_.back();
        h_.pop_back();
        size_t i = 0;
        for (;;) {
            size_t child = 2 * i + 1;
            if (child >= h_.size()) break;
            if (child + 1 < h_.size()) {
                if (c) ++c->comparisons;
                if (h_[child + 1].key < h_[child].key) ++child;
            }
            if (c) ++c->comparisons;
            if (h_[i].key <= h_[child].key) break;
            std::swap(h_[i], h_[child]);
            if (c) c->moved_bytes += 3 * sizeof(Node);
            i = child;
        }
        return top;
    }

private:
    std::vector<Node> h_;
};
// snippet-end: range-binary-heap

// 대조군. 같은 인터페이스를 정렬된 배열로 낸다 — push 가 O(n) 이지만 상수가 작다.
template <class T>
class SortedArrayQueue {
public:
    struct Node { int64_t key; T value; uint32_t gen; };

    void Push(int64_t key, const T& value, uint32_t gen, measure::Counters* c = nullptr) {
        // 키 내림차순으로 유지해 뒤에서 뽑는다(최소가 뒤).
        size_t lo = 0, hi = a_.size();
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            if (c) ++c->comparisons;
            if (a_[mid].key > key) lo = mid + 1; else hi = mid;
        }
        a_.insert(a_.begin() + std::ptrdiff_t(lo), Node{key, value, gen});
        if (c) c->moved_bytes += uint64_t(a_.size() - lo) * sizeof(Node);
    }
    bool Empty() const { return a_.empty(); }
    size_t Size() const { return a_.size(); }
    const Node& Top() const { return a_.back(); }
    Node Pop(measure::Counters* = nullptr) { Node n = a_.back(); a_.pop_back(); return n; }

private:
    std::vector<Node> a_;
};

// ------------------------------------------------------------ 좌표 압축

// 값 → 0..m-1 순위. 정렬 후 중복 제거하고 lower_bound 로 되찾는다.
struct Compressor {
    std::vector<int64_t> sorted;

    void Build(std::vector<int64_t> values) {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
        sorted = std::move(values);
    }
    size_t Rank(int64_t v) const {
        return size_t(std::lower_bound(sorted.begin(), sorted.end(), v) - sorted.begin());
    }
    size_t Size() const { return sorted.size(); }
};

// ------------------------------------------------------------ Fenwick

// snippet-begin: range-fenwick
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
    int64_t Range(size_t l, size_t r, measure::Counters* c = nullptr) const {
        return Prefix(r, c) - Prefix(l, c);
    }

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
    size_t Size() const { return n_; }

private:
    size_t n_;
    std::vector<int64_t> t_;
};
// snippet-end: range-fenwick

// ------------------------------------------------------------ segment tree

// snippet-begin: range-segment-summary
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
// snippet-end: range-segment-summary

// snippet-begin: range-maxsub-tree
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
    void Set(size_t index, int64_t v, measure::Counters* c = nullptr) {
        if (n_) SetImpl(1, 0, n_ - 1, index, v, c);
    }
    Summary Query(size_t l, size_t r, measure::Counters* c = nullptr) const {
        if (!n_ || l > r) return Summary{};
        return QueryImpl(1, 0, n_ - 1, l, r, c);
    }

private:
    size_t n_;
    std::vector<Summary> node_;

    void Build(size_t v, size_t lo, size_t hi, const std::vector<int64_t>& a) {
        if (lo == hi) { node_[v] = Summary::Leaf(a[lo]); return; }
        const size_t mid = lo + (hi - lo) / 2;
        Build(2 * v, lo, mid, a);
        Build(2 * v + 1, mid + 1, hi, a);
        node_[v] = Summary::Merge(node_[2 * v], node_[2 * v + 1]);
    }
    void SetImpl(size_t v, size_t lo, size_t hi, size_t index, int64_t value,
                 measure::Counters* c) {
        if (c) ++c->visited;
        if (lo == hi) { node_[v] = Summary::Leaf(value); return; }
        const size_t mid = lo + (hi - lo) / 2;
        if (index <= mid) SetImpl(2 * v, lo, mid, index, value, c);
        else              SetImpl(2 * v + 1, mid + 1, hi, index, value, c);
        node_[v] = Summary::Merge(node_[2 * v], node_[2 * v + 1]);
    }
    Summary QueryImpl(size_t v, size_t lo, size_t hi, size_t l, size_t r,
                      measure::Counters* c) const {
        if (c) ++c->visited;
        if (r < lo || hi < l) return Summary{};
        if (l <= lo && hi <= r) return node_[v];
        const size_t mid = lo + (hi - lo) / 2;
        return Summary::Merge(QueryImpl(2 * v, lo, mid, l, r, c),
                              QueryImpl(2 * v + 1, mid + 1, hi, l, r, c));
    }
};
// snippet-end: range-maxsub-tree

// snippet-begin: range-lazy-tag
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
// snippet-end: range-lazy-tag

// snippet-begin: range-segment-tree
// 구간 합 + lazy 대입/덧셈. 합은 tag 아래에서 닫힌 형태로 갱신된다 —
// 대입은 (값+덧셈)*길이, 덧셈은 기존 합 + 덧셈*길이. 그래서 잎까지 내려가지 않는다.
class SegmentTree {
public:
    explicit SegmentTree(const std::vector<int64_t>& a) : n_(a.size()) {
        sum_.assign(4 * (n_ ? n_ : 1), 0);
        tag_.assign(4 * (n_ ? n_ : 1), Tag{});
        if (n_) Build(1, 0, n_ - 1, a);
    }
    void Assign(size_t l, size_t r, int64_t v, measure::Counters* c = nullptr) {
        Tag t; t.assign = true; t.assign_val = v;
        if (n_ && l <= r) Update(1, 0, n_ - 1, l, r, t, c);
    }
    void Add(size_t l, size_t r, int64_t v, measure::Counters* c = nullptr) {
        Tag t; t.add = v;
        if (n_ && l <= r) Update(1, 0, n_ - 1, l, r, t, c);
    }
    int64_t Sum(size_t l, size_t r, measure::Counters* c = nullptr) {
        if (!n_ || l > r) return 0;
        return SumImpl(1, 0, n_ - 1, l, r, c);
    }
    int64_t At(size_t index, measure::Counters* c = nullptr) { return Sum(index, index, c); }

private:
    size_t n_;
    std::vector<int64_t> sum_;
    std::vector<Tag>     tag_;

    void Build(size_t v, size_t lo, size_t hi, const std::vector<int64_t>& a) {
        if (lo == hi) { sum_[v] = a[lo]; return; }
        const size_t mid = lo + (hi - lo) / 2;
        Build(2 * v, lo, mid, a);
        Build(2 * v + 1, mid + 1, hi, a);
        sum_[v] = sum_[2 * v] + sum_[2 * v + 1];
    }
    void ApplyTag(size_t v, size_t lo, size_t hi, const Tag& t) {
        if (t.Identity()) return;
        const int64_t len = int64_t(hi - lo + 1);
        if (t.assign) sum_[v] = (t.assign_val + t.add) * len;
        else          sum_[v] += t.add * len;
        tag_[v] = Tag::Compose(tag_[v], t);
    }
    void Push(size_t v, size_t lo, size_t hi) {
        if (tag_[v].Identity() || lo == hi) { tag_[v] = Tag{}; return; }
        const size_t mid = lo + (hi - lo) / 2;
        const Tag t = tag_[v];
        tag_[v] = Tag{};
        ApplyTag(2 * v, lo, mid, t);
        ApplyTag(2 * v + 1, mid + 1, hi, t);
    }
    void Update(size_t v, size_t lo, size_t hi, size_t l, size_t r, const Tag& t,
                measure::Counters* c) {
        if (c) ++c->visited;
        if (r < lo || hi < l) return;
        if (l <= lo && hi <= r) { ApplyTag(v, lo, hi, t); return; }
        Push(v, lo, hi);
        const size_t mid = lo + (hi - lo) / 2;
        Update(2 * v, lo, mid, l, r, t, c);
        Update(2 * v + 1, mid + 1, hi, l, r, t, c);
        sum_[v] = sum_[2 * v] + sum_[2 * v + 1];
    }
    int64_t SumImpl(size_t v, size_t lo, size_t hi, size_t l, size_t r,
                    measure::Counters* c) {
        if (c) ++c->visited;
        if (r < lo || hi < l) return 0;
        if (l <= lo && hi <= r) return sum_[v];
        Push(v, lo, hi);
        const size_t mid = lo + (hi - lo) / 2;
        return SumImpl(2 * v, lo, mid, l, r, c) + SumImpl(2 * v + 1, mid + 1, hi, l, r, c);
    }
};
// snippet-end: range-segment-tree

}  // namespace algo
