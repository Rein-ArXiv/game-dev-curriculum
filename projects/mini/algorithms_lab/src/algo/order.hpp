// order.hpp — 순서와 선택.
//
// 이 헤더가 소유하는 계약은 셋이다.
//   1. 출력이 정렬돼 있다 (순서)
//   2. 출력이 입력의 multiset 이다 (원소 보존)
//   3. 안정 정렬은 같은 키의 상대 순서를 바꾸지 않는다 (안정성)
// 셋을 따로 세는 이유는 1 만 검사하면 원소를 잃은 구현이 통과하기 때문이다.
// `Rec::tag` 가 원래 위치를 들고 다니므로 2 와 3 을 같은 배열에서 판정할 수 있다.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "measure/counters.hpp"

namespace algo {

// snippet-begin: order-rec
// 정렬 대상. key 만 비교하고 tag 는 원래 위치를 들고 다닌다 — 안정성과 원소
// 보존을 판정하는 근거가 값 자체 안에 있어야 검사가 구현을 믿지 않는다.
struct Rec {
    uint32_t key = 0;
    uint32_t tag = 0;
};

inline bool ByKey(const Rec& a, const Rec& b) { return a.key < b.key; }
// snippet-end: order-rec

// ---------------------------------------------------------------- 이진 탐색

// snippet-begin: order-lower-bound
// 불변식: 답은 항상 반열린 구간 [lo, hi) 안에 있다.
//   구간 왼쪽 밖은 전부 `< key`, 오른쪽 밖은 전부 `>= key`.
// mid 를 (lo+hi)/2 로 쓰지 않는 이유는 크기가 SIZE_MAX/2 를 넘으면 넘치기 때문이다.
template <class T, class Less>
size_t LowerBound(const T* a, size_t n, const T& key, Less less,
                  measure::Counters* c = nullptr) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (c) ++c->comparisons;
        if (less(a[mid], key)) lo = mid + 1;   // a[mid] < key → 답은 오른쪽
        else                   hi = mid;       // a[mid] >= key → mid 도 후보다
    }
    return lo;
}

template <class T, class Less>
size_t UpperBound(const T* a, size_t n, const T& key, Less less,
                  measure::Counters* c = nullptr) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (c) ++c->comparisons;
        if (less(key, a[mid])) hi = mid;       // key < a[mid] → 답은 왼쪽
        else                   lo = mid + 1;
    }
    return lo;
}
// snippet-end: order-lower-bound

// ---------------------------------------------------------------- 비교 정렬

namespace detail {

template <class T, class Less>
void InsertionSort(T* a, size_t n, Less less, measure::Counters* c) {
    for (size_t i = 1; i < n; ++i) {
        T v = a[i];
        size_t j = i;
        while (j > 0) {
            if (c) ++c->comparisons;
            if (!less(v, a[j - 1])) break;
            a[j] = a[j - 1];
            if (c) c->moved_bytes += sizeof(T);
            --j;
        }
        a[j] = v;
        if (c) c->moved_bytes += sizeof(T);
    }
}

// 직접 구현한 sift-down. 마지막 원소 처리(자식이 하나뿐인 경우)가 여기서 갈린다.
template <class T, class Less>
void SiftDown(T* a, size_t n, size_t root, Less less, measure::Counters* c) {
    for (;;) {
        size_t child = 2 * root + 1;
        if (child >= n) break;
        // 오른쪽 자식이 *존재할 때만* 비교한다. child + 1 < n 을 <= 로 쓰면
        // 힙 밖을 읽는다.
        if (child + 1 < n) {
            if (c) ++c->comparisons;
            if (less(a[child], a[child + 1])) ++child;
        }
        if (c) ++c->comparisons;
        if (!less(a[root], a[child])) break;
        T tmp = a[root]; a[root] = a[child]; a[child] = tmp;
        if (c) c->moved_bytes += 3 * sizeof(T);
        root = child;
    }
}

template <class T, class Less>
void HeapSort(T* a, size_t n, Less less, measure::Counters* c) {
    if (n < 2) return;
    for (size_t i = n / 2; i-- > 0;) SiftDown(a, n, i, less, c);
    for (size_t end = n; end > 1; --end) {
        T tmp = a[0]; a[0] = a[end - 1]; a[end - 1] = tmp;
        if (c) c->moved_bytes += 3 * sizeof(T);
        SiftDown(a, end - 1, 0, less, c);
    }
}

// 세 값의 중앙값을 pivot 자리(hi)로 보낸다. 이미 정렬된 입력에서 quicksort 가
// O(n^2) 로 무너지는 것을 막는 가장 싼 방어다.
template <class T, class Less>
void MedianOfThree(T* a, size_t lo, size_t hi, Less less, measure::Counters* c) {
    const size_t mid = lo + (hi - lo) / 2;
    T x = a[lo], y = a[mid], z = a[hi];
    size_t pick = mid;
    // 비교자 호출을 한 번씩 센다 — 경로에 따라 2회 또는 3회다.
    auto lt = [&](const T& p, const T& q) { if (c) ++c->comparisons; return less(p, q); };
    if (lt(x, y)) { if (lt(y, z)) pick = mid; else pick = lt(x, z) ? hi : lo; }
    else          { if (lt(x, z)) pick = lo;  else pick = lt(y, z) ? hi : mid; }
    if (pick != hi) {
        T tmp = a[pick]; a[pick] = a[hi]; a[hi] = tmp;
        if (c) c->moved_bytes += 3 * sizeof(T);
    }
}

// Lomuto 분할. 전부 같은 값인 입력에서 한쪽으로 쏠리므로, 호출부가 깊이 상한으로
// 막는다 — 그것이 introsort 의 요지다.
template <class T, class Less>
size_t Partition(T* a, size_t lo, size_t hi, Less less, measure::Counters* c) {
    const T pivot = a[hi];
    size_t store = lo;
    for (size_t i = lo; i < hi; ++i) {
        if (c) ++c->comparisons;
        if (less(a[i], pivot)) {
            T tmp = a[i]; a[i] = a[store]; a[store] = tmp;
            if (c) c->moved_bytes += 3 * sizeof(T);
            ++store;
        }
    }
    T tmp = a[hi]; a[hi] = a[store]; a[store] = tmp;
    if (c) c->moved_bytes += 3 * sizeof(T);
    return store;
}

inline int FloorLog2(size_t n) {
    int k = 0;
    while (n > 1) { n >>= 1; ++k; }
    return k;
}

template <class T, class Less>
void IntroLoop(T* a, size_t lo, size_t hi, int depth, Less less, measure::Counters* c) {
    const size_t kCutoff = 16;
    while (hi > lo) {
        const size_t n = hi - lo + 1;
        if (n <= kCutoff) { InsertionSort(a + lo, n, less, c); return; }
        if (depth == 0)   { HeapSort(a + lo, n, less, c); return; }
        --depth;
        MedianOfThree(a, lo, hi, less, c);
        const size_t p = Partition(a, lo, hi, less, c);
        // 짧은 쪽을 재귀로, 긴 쪽을 반복으로 — 스택 깊이가 O(log n) 으로 묶인다.
        if (p - lo < hi - p) {
            if (p > lo) IntroLoop(a, lo, p - 1, depth, less, c);
            lo = p + 1;
        } else {
            IntroLoop(a, p + 1, hi, depth, less, c);
            if (p == lo) return;
            hi = p - 1;
        }
    }
}

}  // namespace detail

// snippet-begin: order-introsort
// quicksort + 깊이 초과 시 heapsort + 작은 구간 insertion sort.
// 깊이 상한 2*floor(log2 n) 은 "분할이 절반씩 되면 절대 닿지 않는" 값이다 —
// 닿았다는 것 자체가 pivot 선택이 무너졌다는 신호이고, 그때 heapsort 가 받는다.
template <class T, class Less>
void Introsort(std::vector<T>& a, Less less, measure::Counters* c = nullptr) {
    if (a.size() < 2) return;
    detail::IntroLoop(a.data(), 0, a.size() - 1,
                      2 * detail::FloorLog2(a.size()), less, c);
}
// snippet-end: order-introsort

// snippet-begin: order-merge-stable
// 안정 merge sort. 병합에서 `less(right, left)` 를 물어 *같으면 왼쪽을 먼저* 낸다.
// `less(left, right)` 로 뒤집어 물으면 같은 키의 순서가 뒤집혀 안정성이 죽는다.
template <class T, class Less>
void MergeSortStable(std::vector<T>& a, Less less, measure::Counters* c = nullptr) {
    const size_t n = a.size();
    if (n < 2) return;
    std::vector<T> buf(n);
    if (c) c->Peak(uint64_t(n) * sizeof(T));   // 임시 배열이 이 알고리즘의 공간 가격
    for (size_t width = 1; width < n; width *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * width) {
            const size_t mid = (lo + width < n) ? lo + width : n;
            const size_t hi  = (lo + 2 * width < n) ? lo + 2 * width : n;
            size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) {
                if (c) ++c->comparisons;
                if (less(a[j], a[i])) buf[k++] = a[j++];
                else                  buf[k++] = a[i++];
            }
            while (i < mid) buf[k++] = a[i++];
            while (j < hi)  buf[k++] = a[j++];
            if (c) c->moved_bytes += uint64_t(hi - lo) * sizeof(T);
        }
        a.swap(buf);
    }
    // width 를 두 배씩 올리며 swap 했으므로 결과가 buf 쪽에 있을 수 있다.
    // 위 루프는 매번 a 로 읽어 buf 로 쓰고 swap 하므로 종료 시 a 가 정답이다.
}
// snippet-end: order-merge-stable

// ---------------------------------------------------------------- 분포 정렬

// snippet-begin: order-counting
// counting sort. prefix sum 이 *배타적*(exclusive)이어야 한다 — 포함으로 쓰면
// 모든 원소가 한 칸씩 밀린다. 마지막 버킷은 배열 밖으로 나간다.
// 안정성: 입력을 앞에서부터 훑으며 offset 을 증가시키므로 같은 키의 순서가 보존된다.
inline void CountingSort(std::vector<Rec>& a, uint32_t key_range,
                         measure::Counters* c = nullptr) {
    if (a.empty()) return;
    std::vector<uint32_t> count(size_t(key_range) + 1, 0);
    for (const Rec& r : a) ++count[r.key];
    uint32_t sum = 0;
    for (size_t k = 0; k <= key_range; ++k) {
        const uint32_t here = count[k];
        count[k] = sum;      // ← 배타적: 자기 자신을 더하기 *전* 값
        sum += here;
    }
    std::vector<Rec> out(a.size());
    for (const Rec& r : a) out[count[r.key]++] = r;
    if (c) {
        c->moved_bytes += uint64_t(a.size()) * sizeof(Rec);
        c->Peak(uint64_t(out.size()) * sizeof(Rec) +
                uint64_t(count.size()) * sizeof(uint32_t));
    }
    a.swap(out);
}
// snippet-end: order-counting

// snippet-begin: order-radix
// LSD radix sort. r 비트씩 여러 pass 를 돈다.
//   - ping-pong 버퍼: 매 pass 마다 새 배열을 잡지 않는다.
//   - pass 생략: 히스토그램이 버킷 하나에 전부 몰렸으면 그 pass 는 항등이다.
//   - 안정성: 각 pass 가 안정이어야 전체가 정렬된다. 앞에서부터 흩뿌린다.
inline void RadixSortLsd(std::vector<Rec>& a, int radix_bits,
                         measure::Counters* c = nullptr) {
    const size_t n = a.size();
    if (n < 2) return;
    const int bits    = 32;
    const size_t kBuckets = size_t(1) << radix_bits;
    const uint32_t mask   = uint32_t(kBuckets - 1);

    std::vector<Rec> tmp(n);
    if (c) c->Peak(uint64_t(n) * sizeof(Rec) + uint64_t(kBuckets) * sizeof(size_t));

    std::vector<size_t> count(kBuckets);
    std::vector<Rec>* src = &a;
    std::vector<Rec>* dst = &tmp;

    for (int shift = 0; shift < bits; shift += radix_bits) {
        for (size_t k = 0; k < kBuckets; ++k) count[k] = 0;
        for (const Rec& r : *src) ++count[(r.key >> shift) & mask];
        // 이 pass 가 항등인가 — 전부 한 버킷이면 흩뿌릴 이유가 없다.
        bool single = false;
        for (size_t k = 0; k < kBuckets; ++k) {
            if (count[k] == n) { single = true; break; }
        }
        if (single) continue;

        size_t sum = 0;
        for (size_t k = 0; k < kBuckets; ++k) {   // 배타적 prefix sum
            const size_t here = count[k];
            count[k] = sum;
            sum += here;
        }
        for (const Rec& r : *src) (*dst)[count[(r.key >> shift) & mask]++] = r;
        if (c) c->moved_bytes += uint64_t(n) * sizeof(Rec);
        std::vector<Rec>* t = src; src = dst; dst = t;
    }
    if (src != &a) a = *src;
}
// snippet-end: order-radix

// snippet-begin: order-ordered-key
// IEEE-754 float 를 *순서를 보존하는* uint32 로 옮긴다. 분포 정렬은 정수 키만
// 흩뿌릴 수 있으므로, 실수 키를 다루려면 이 변환이 먼저 필요하다.
//   양수: 비트열이 이미 단조 증가한다 → 부호 비트만 켜서 음수 위로 올린다.
//   음수: 크기가 클수록 비트열이 크다 → 전부 반전해 순서를 뒤집는다.
// -0.0 은 +0.0 보다 작은 키가 되고, NaN 은 양 끝으로 간다 — 총 순서지만 수학적
// 대소와는 다르므로 NaN 이 섞인 입력의 '정렬됨' 판정은 이 키 기준이어야 한다.
inline uint32_t OrderedKey(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return (u & 0x80000000u) ? ~u : (u | 0x80000000u);
}

inline float OrderedKeyInverse(uint32_t k) {
    const uint32_t u = (k & 0x80000000u) ? (k & 0x7FFFFFFFu) : ~k;
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}
// snippet-end: order-ordered-key

// ------------------------------------------------------------------- 선택

// 결정론적 난수원. 적대 입력에 대한 방어가 "무작위 pivot" 인데, 그 무작위가
// 실행마다 달라지면 실패가 재현되지 않는다. 시드를 인자로 받는다.
class Lcg {
public:
    explicit Lcg(uint32_t seed) : s_(seed ? seed : 1u) {}
    uint32_t Next() { s_ = s_ * 1664525u + 1013904223u; return s_; }
    size_t Below(size_t n) { return n ? size_t(Next() % uint32_t(n)) : 0; }

private:
    uint32_t s_;
};

// snippet-begin: order-quickselect
// quickselect. k 번째(0-based)를 제자리에 놓고 돌아온다. 평균 O(n) 이지만
// 적대 입력 + 고정 pivot 이면 O(n^2) 다 — pivot 을 무작위로 고르는 이유. 다만 키가 전부 같은
// 입력은 Lomuto 분할이 pivot 과 무관하게 한쪽으로 쏠려 무작위화로도 O(n^2) 를 벗어나지 못한다.
inline Rec Quickselect(std::vector<Rec> a, size_t k, uint32_t seed,
                       measure::Counters* c = nullptr) {
    if (a.empty()) return Rec{};
    if (k >= a.size()) k = a.size() - 1;
    Lcg rng(seed);
    size_t lo = 0, hi = a.size() - 1;
    while (lo < hi) {
        const size_t pick = lo + rng.Below(hi - lo + 1);
        Rec tmp = a[pick]; a[pick] = a[hi]; a[hi] = tmp;
        const size_t p = detail::Partition(a.data(), lo, hi, ByKey, c);
        if (c) c->visited += hi - lo + 1;
        if (p == k) return a[p];
        if (p < k) lo = p + 1; else hi = p - 1;
    }
    return a[lo];
}
// snippet-end: order-quickselect

// snippet-begin: order-median-of-medians
// median-of-medians. 5개씩 묶어 각 그룹의 중앙값을 모으고, 그 중앙값들의
// 중앙값을 pivot 으로 쓴다. 최악에서도 3n/10 을 잘라 내는 것이 보장되므로
// 선형 시간이 최악에서도 성립한다 — 대신 상수가 quickselect 보다 크다.
inline Rec MedianOfMediansSelect(std::vector<Rec> a, size_t k,
                                 measure::Counters* c = nullptr) {
    if (a.empty()) return Rec{};
    if (k >= a.size()) k = a.size() - 1;
    for (;;) {
        if (c) c->visited += a.size();
        if (a.size() <= 5) {
            detail::InsertionSort(a.data(), a.size(), ByKey, c);
            return a[k];
        }
        std::vector<Rec> medians;
        medians.reserve((a.size() + 4) / 5);
        for (size_t i = 0; i < a.size(); i += 5) {
            const size_t len = (i + 5 <= a.size()) ? 5 : a.size() - i;
            detail::InsertionSort(a.data() + i, len, ByKey, c);
            medians.push_back(a[i + len / 2]);
        }
        const Rec pivot = MedianOfMediansSelect(medians, medians.size() / 2, c);
        std::vector<Rec> lo, eq, hi;
        for (const Rec& r : a) {
            if (c) ++c->comparisons;
            if (r.key < pivot.key)      lo.push_back(r);
            else if (r.key > pivot.key) hi.push_back(r);
            else                        eq.push_back(r);
        }
        if (k < lo.size())                    { a = lo; }
        else if (k < lo.size() + eq.size())   { return eq[k - lo.size()]; }
        else                                  { k -= lo.size() + eq.size(); a = hi; }
    }
}
// snippet-end: order-median-of-medians

// snippet-begin: order-topk
// 크기 k min-heap 으로 상위 k 개. 공간이 O(k) 라 n 이 스트림이어도 된다.
// 반환은 키 내림차순이다.
inline std::vector<Rec> TopKMinHeap(const std::vector<Rec>& a, size_t k,
                                    measure::Counters* c = nullptr) {
    std::vector<Rec> heap;
    if (k == 0) return heap;
    heap.reserve(k);
    auto Greater = [](const Rec& x, const Rec& y) { return x.key > y.key; };
    for (const Rec& r : a) {
        if (heap.size() < k) {
            heap.push_back(r);
            // 위로 올리기
            size_t i = heap.size() - 1;
            while (i > 0) {
                const size_t parent = (i - 1) / 2;
                if (c) ++c->comparisons;
                if (!Greater(heap[parent], heap[i])) break;
                Rec t = heap[parent]; heap[parent] = heap[i]; heap[i] = t;
                if (c) c->moved_bytes += 3 * sizeof(Rec);
                i = parent;
            }
            continue;
        }
        if (c) ++c->comparisons;
        if (r.key <= heap[0].key) continue;   // 최소보다 작으면 볼 것도 없다
        heap[0] = r;
        detail::SiftDown(heap.data(), heap.size(), 0, Greater, c);
    }
    // 남은 힙을 정렬해 내림차순으로 낸다. 여기에 insertion sort 를 쓰면 k 가 클 때
    // 이 마지막 줄 하나가 O(k^2) 로 전체를 지배한다 — 자료구조가 아니라 마무리가
    // 비용을 정하는 흔한 자리다.
    std::vector<Rec> out = heap;
    Introsort(out, ByKey, c);
    std::vector<Rec> rev(out.rbegin(), out.rend());
    return rev;
}
// snippet-end: order-topk

}  // namespace algo
