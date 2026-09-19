// algo_tests — algorithms_lab 의 검증 배터리 (ctest 대상)
//
// 인자 없이 돌리면 고정 사례 전부를 실행한다.
//   algo_tests                 고정 사례 전부
//   algo_tests --step=NN       그 step 이 추가한 사례만 이름과 함께
//   algo_tests --list          사례 목록
//   algo_tests --properties    시드 무작위 property 층
//   algo_tests --properties --only=NAME --seed=N --sample=K   반례 하나 재현
//
// 파일 맨 아래 kCases 표가 사례의 step 소속을 정한다.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "algo/backtrack.hpp"
#include "algo/compress.hpp"
#include "algo/dict.hpp"
#include "algo/dp.hpp"
#include "algo/flow.hpp"
#include "algo/graph.hpp"
#include "algo/greedy.hpp"
#include "algo/order.hpp"
#include "algo/paths.hpp"
#include "algo/range.hpp"
#include "algo/text.hpp"
#include "measure/workload.hpp"
#include "oracle.h"
#include "property.h"

static int g_failed = 0;
static int g_checks = 0;

#define CHECK(cond) do { ++g_checks; if (!(cond)) { \
    std::printf("    CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); ++g_failed; } } while (0)

using namespace algo;
using measure::Corpus;
using measure::SortShape;

// ===================================================================== step01

static void TestSearchBounds() {
    const std::vector<int64_t> a = {1, 3, 3, 3, 5, 9};
    auto less = [](int64_t x, int64_t y) { return x < y; };
    CHECK(LowerBound(a.data(), a.size(), int64_t(3), less) == 1);
    CHECK(UpperBound(a.data(), a.size(), int64_t(3), less) == 4);
    CHECK(LowerBound(a.data(), a.size(), int64_t(0), less) == 0);
    CHECK(LowerBound(a.data(), a.size(), int64_t(100), less) == 6);
    CHECK(UpperBound(a.data(), a.size(), int64_t(100), less) == 6);
    // 빈 배열과 전부 동일한 배열 — 반열린 구간 불변식의 경계
    CHECK(LowerBound<int64_t>(nullptr, 0, 7, less) == 0);
    const std::vector<int64_t> same(8, 4);
    CHECK(LowerBound(same.data(), same.size(), int64_t(4), less) == 0);
    CHECK(UpperBound(same.data(), same.size(), int64_t(4), less) == 8);
    // 두 경계의 차가 곧 개수다. 한 칸 어긋난 구현은 여기서 죽는다.
    CHECK(UpperBound(a.data(), a.size(), int64_t(3), less) -
          LowerBound(a.data(), a.size(), int64_t(3), less) == 3);
}

// 다섯 팔이 같은 입력 복사본에서 같은 순서와 같은 multiset 을 낸다.
// 안정 팔 셋(merge · counting · radix)은 안정성까지 지킨다.
static void TestSortContract() {
    const SortShape shapes[] = {
        SortShape::Random, SortShape::Sorted, SortShape::Reversed,
        SortShape::AllEqual, SortShape::TwoValues, SortShape::Sawtooth,
        SortShape::OrganPipe, SortShape::HighDigitSame, SortShape::FewKeys,
    };
    for (SortShape shape : shapes) {
        for (size_t n : {size_t(0), size_t(1), size_t(2), size_t(17), size_t(600)}) {
            const std::vector<Rec> input = measure::MakeSortInput(shape, n, 0xA1B0u);
            std::vector<Rec> intro = input, merge = input, radix8 = input, radix11 = input;
            Introsort(intro, ByKey);
            MergeSortStable(merge, ByKey);
            RadixSortLsd(radix8, 8);
            RadixSortLsd(radix11, 11);

            CHECK(oracle::NonDecreasing(intro));
            CHECK(oracle::NonDecreasing(merge));
            CHECK(oracle::NonDecreasing(radix8));
            CHECK(oracle::NonDecreasing(radix11));
            // 순서만 보면 원소를 잃은 구현이 통과한다. multiset 을 따로 본다.
            CHECK(oracle::SameMultiset(input, intro));
            CHECK(oracle::SameMultiset(input, merge));
            CHECK(oracle::SameMultiset(input, radix8));
            CHECK(oracle::SameMultiset(input, radix11));
            // 안정성은 안정 팔에만 요구한다 — introsort 는 안정이 아니다.
            CHECK(oracle::Stable(merge));
            CHECK(oracle::Stable(radix8));
            CHECK(oracle::Stable(radix11));

            if (shape == SortShape::FewKeys && n > 0) {
                std::vector<Rec> counting = input;
                CountingSort(counting, 15);
                CHECK(oracle::NonDecreasing(counting));
                CHECK(oracle::SameMultiset(input, counting));
                CHECK(oracle::Stable(counting));
            }
        }
    }
}

// 실수 키의 순서 보존. -0.0 과 +0.0, 아주 작은 값과 아주 큰 값이 함께 있어야
// 부호 분기가 실제로 검사된다.
static void TestOrderedKey() {
    std::vector<float> values = measure::AdversarialFloats();
    std::vector<std::pair<float, uint32_t>> pairs;
    for (float f : values) pairs.push_back({f, OrderedKey(f)});
    // (1) 단조성: a < b 이면 key(a) < key(b)
    for (size_t i = 0; i < pairs.size(); ++i) {
        for (size_t j = 0; j < pairs.size(); ++j) {
            if (pairs[i].first < pairs[j].first) CHECK(pairs[i].second < pairs[j].second);
        }
    }
    // (2) -0.0 과 +0.0 은 수학적으로 같지만 비트가 달라 키도 다르다.
    //     총 순서를 만들려면 그 둘이 서로 다른 키를 가져야 한다.
    CHECK(OrderedKey(-0.0f) < OrderedKey(0.0f));
    // (3) 왕복
    for (float f : values) CHECK(OrderedKeyInverse(OrderedKey(f)) == f);
    // (4) 이 키로 정렬한 결과가 실제 대소 순서와 같다
    std::vector<Rec> recs;
    for (size_t i = 0; i < values.size(); ++i)
        recs.push_back(Rec{OrderedKey(values[i]), uint32_t(i)});
    RadixSortLsd(recs, 8);
    for (size_t i = 1; i < recs.size(); ++i) {
        const float prev = OrderedKeyInverse(recs[i - 1].key);
        const float cur  = OrderedKeyInverse(recs[i].key);
        CHECK(prev <= cur);
    }
}

static void TestSelection() {
    for (SortShape shape : {SortShape::Random, SortShape::AllEqual,
                            SortShape::OrganPipe, SortShape::Sorted}) {
        const std::vector<Rec> input = measure::MakeSortInput(shape, 401, 0x5EEDu);
        for (size_t k : {size_t(0), size_t(1), size_t(200), size_t(399), size_t(400)}) {
            const Rec want = oracle::NthByFullSort(input, k);
            // quickselect 와 median-of-medians 는 동점에서 tag 가 다를 수 있다.
            // 계약은 *키* 가 k 번째와 같다는 것이다.
            CHECK(Quickselect(input, k, 0x1234u).key == want.key);
            CHECK(MedianOfMediansSelect(input, k).key == want.key);
        }
        // top-k 는 키 내림차순이고 전체 정렬의 뒤 k 개와 키가 같아야 한다.
        std::vector<Rec> sorted = input;
        Introsort(sorted, ByKey);
        const std::vector<Rec> top = TopKMinHeap(input, 5);
        CHECK(top.size() == 5);
        for (size_t i = 0; i < top.size(); ++i)
            CHECK(top[i].key == sorted[sorted.size() - 1 - i].key);
    }
    // k 가 n 보다 크면 있는 만큼만 낸다.
    const std::vector<Rec> few = measure::MakeSortInput(SortShape::Random, 3, 7u);
    CHECK(TopKMinHeap(few, 10).size() == 3);
    CHECK(TopKMinHeap(few, 0).empty());
}

// ===================================================================== step02

static void TestPrefixAndDifference() {
    const std::vector<int64_t> a = {3, -1, 4, 1, -5, 9, 2, 6};
    const std::vector<int64_t> p = PrefixSum(a);
    CHECK(p.size() == a.size() + 1);
    for (size_t l = 0; l <= a.size(); ++l)
        for (size_t r = l; r <= a.size(); ++r)
            CHECK(RangeSum(p, l, r) == oracle::SlowRangeSum(a, l, r));
    CHECK(RangeSum(p, 3, 3) == 0);   // 빈 구간이 표현된다

    Difference d(a.size());
    d.AddRange(1, 4, 10);
    d.AddRange(0, 8, -1);
    const std::vector<int64_t> made = d.Materialize();
    for (size_t i = 0; i < a.size(); ++i) {
        const int64_t want = ((i >= 1 && i < 4) ? 10 : 0) - 1;
        CHECK(made[i] == want);
    }

    Prefix2D p2;
    const std::vector<int64_t> grid = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    p2.Build(grid, 3, 3);
    CHECK(p2.Sum(0, 0, 3, 3) == 45);
    CHECK(p2.Sum(1, 1, 3, 3) == 5 + 6 + 8 + 9);
    CHECK(p2.Sum(0, 0, 0, 0) == 0);
}

static void TestSlidingAndMonotonic() {
    measure::Rng rng(0xBEEFu);
    for (size_t n : {size_t(1), size_t(5), size_t(64)}) {
        std::vector<int64_t> a(n);
        for (size_t i = 0; i < n; ++i) a[i] = int64_t(rng.Below(50)) - 25;
        for (size_t w = 1; w <= n; ++w)
            CHECK(SlidingMax(a, w) == oracle::SlowSlidingMax(a, w));
        // 폭이 배열보다 크면 창이 하나도 없다
        CHECK(SlidingMax(a, n + 1).empty());
    }
    // 단조 stack. 정의대로 오른쪽을 직접 훑은 답과 대조한다.
    const std::vector<int64_t> a = {2, 1, 5, 6, 2, 3, 1};
    const std::vector<size_t> got = NextGreater(a);
    for (size_t i = 0; i < a.size(); ++i) {
        size_t want = a.size();
        for (size_t j = i + 1; j < a.size(); ++j)
            if (a[j] > a[i]) { want = j; break; }
        CHECK(got[i] == want);
    }
    // 두 포인터. 값이 양수라는 사전조건 안에서만 성립한다.
    const std::vector<int64_t> pos = {2, 3, 1, 2, 4, 3};
    CHECK(ShortestAtLeast(pos, 7) == 2);
    CHECK(ShortestAtLeast(pos, 100) == 0);
}

static void TestFenwick() {
    measure::Rng rng(0xF00Du);
    const size_t n = 64;
    std::vector<int64_t> plain(n, 0);
    Fenwick fw(n);
    for (int round = 0; round < 200; ++round) {
        const size_t at = rng.Below(uint32_t(n));
        const int64_t delta = int64_t(rng.Below(20));
        plain[at] += delta;
        fw.Add(at, delta);
        const size_t l = rng.Below(uint32_t(n));
        const size_t r = l + rng.Below(uint32_t(n - l) + 1);
        CHECK(fw.Range(l, r) == oracle::SlowRangeSum(plain, l, r));
    }
    // 마지막 원소. 1-based/0-based 혼선은 대부분의 질의를 통과시키고 여기서만 죽는다.
    CHECK(fw.Range(n - 1, n) == plain[n - 1]);
    CHECK(fw.Prefix(n) == oracle::SlowRangeSum(plain, 0, n));

    // binary lifting: prefix 가 처음 target 이상이 되는 위치.
    // 값이 음이 아니어야 prefix 가 단조가 되고, 그래야 이 탐색이 성립한다.
    std::vector<int64_t> weights(n, 0);
    Fenwick pick(n);
    for (size_t i = 0; i < n; ++i) {
        weights[i] = int64_t(rng.Below(5));
        pick.Add(i, weights[i]);
    }
    const int64_t total = pick.Prefix(n);
    for (int64_t target = 1; target <= total; ++target) {
        const size_t at = pick.LowerBoundPrefix(target);
        int64_t run = 0;
        size_t want = n;
        for (size_t i = 0; i < n; ++i) {
            run += weights[i];
            if (run >= target) { want = i; break; }
        }
        CHECK(at == want);
    }
}

// lazy tag 의 대수 계약을 *먼저* 검사하고, 그 위에서 트리를 검사한다.
static void TestSegmentTreeAlgebra() {
    Tag assign5; assign5.assign = true; assign5.assign_val = 5;
    Tag add3;    add3.add = 3;
    // 대입 뒤 덧셈 != 덧셈 뒤 대입. 순서가 의미를 가지는 것이 계약의 요지다.
    CHECK(Tag::Compose(assign5, add3).ApplyValue(100) == 8);
    CHECK(Tag::Compose(add3, assign5).ApplyValue(100) == 5);
    // Apply(Compose(a,b), x) == Apply(b, Apply(a, x))
    const int64_t xs[4] = {0, 1, -7, 42};
    Tag tags[4] = {assign5, add3, Tag{}, Tag{true, -2, 0}};
    for (const Tag& a : tags)
        for (const Tag& b : tags)
            for (int64_t x : xs)
                CHECK(Tag::Compose(a, b).ApplyValue(x) == b.ApplyValue(a.ApplyValue(x)));
    // 결합법칙
    for (const Tag& a : tags)
        for (const Tag& b : tags)
            for (const Tag& c : tags)
                for (int64_t x : xs)
                    CHECK(Tag::Compose(Tag::Compose(a, b), c).ApplyValue(x) ==
                          Tag::Compose(a, Tag::Compose(b, c)).ApplyValue(x));

    measure::Rng rng(0x5E60u);
    const size_t n = 40;
    std::vector<int64_t> plain(n);
    for (size_t i = 0; i < n; ++i) plain[i] = int64_t(rng.Below(20)) - 10;
    SegmentTree tree(plain);
    for (int round = 0; round < 300; ++round) {
        const size_t l = rng.Below(uint32_t(n));
        const size_t r = l + rng.Below(uint32_t(n - l));
        const uint32_t op = rng.Below(3);
        if (op == 0) {
            const int64_t v = int64_t(rng.Below(20)) - 10;
            for (size_t i = l; i <= r; ++i) plain[i] = v;
            tree.Assign(l, r, v);
        } else if (op == 1) {
            const int64_t v = int64_t(rng.Below(10)) - 5;
            for (size_t i = l; i <= r; ++i) plain[i] += v;
            tree.Add(l, r, v);
        } else {
            CHECK(tree.Sum(l, r) == oracle::SlowRangeSum(plain, l, r + 1));
        }
    }
    for (size_t i = 0; i < n; ++i) CHECK(tree.At(i) == plain[i]);
}

static void TestMaxSubarrayTree() {
    measure::Rng rng(0x3311u);
    const size_t n = 32;
    std::vector<int64_t> plain(n);
    for (size_t i = 0; i < n; ++i) plain[i] = int64_t(rng.Below(20)) - 10;
    MaxSubarrayTree tree(plain);
    for (int round = 0; round < 120; ++round) {
        const size_t at = rng.Below(uint32_t(n));
        const int64_t v = int64_t(rng.Below(20)) - 10;
        plain[at] = v;
        tree.Set(at, v);
        const size_t l = rng.Below(uint32_t(n));
        const size_t r = l + rng.Below(uint32_t(n - l));
        const Summary s = tree.Query(l, r);
        CHECK(s.best == oracle::SlowMaxSubarray(plain, l, r));
        CHECK(s.total == oracle::SlowRangeSum(plain, l, r + 1));
    }
}

// 이진 힙과 정렬 배열이 같은 pop 순서를 낸다. 자료구조가 달라도 계약은 같다.
static void TestPriorityQueueArms() {
    measure::Rng rng(0x0913u);
    BinaryHeap<uint32_t> heap;
    SortedArrayQueue<uint32_t> arr;
    std::vector<int64_t> keys;
    for (uint32_t i = 0; i < 500; ++i) {
        const int64_t key = int64_t(rng.Below(1000));
        heap.Push(key, i, 0);
        arr.Push(key, i, 0);
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    for (size_t i = 0; i < keys.size(); ++i) {
        CHECK(heap.Top().key == keys[i]);
        CHECK(arr.Top().key == keys[i]);
        heap.Pop();
        arr.Pop();
    }
    CHECK(heap.Empty() && arr.Empty());
    // 좌표 압축
    Compressor cz;
    cz.Build({50, 10, 50, 30, 10});
    CHECK(cz.Size() == 3);
    CHECK(cz.Rank(10) == 0 && cz.Rank(30) == 1 && cz.Rank(50) == 2);
}

// ===================================================================== step03

static void TestOpenAddressing() {
    const DeletePolicy policies[] = {DeletePolicy::Tombstone, DeletePolicy::BackwardShift,
                                     DeletePolicy::RobinHood};
    for (DeletePolicy policy : policies) {
        for (measure::KeyShape shape : {measure::KeyShape::Random,
                                        measure::KeyShape::CommonPrefix,
                                        measure::KeyShape::Colliding}) {
            const std::vector<std::string> keys = measure::MakeKeys(shape, 400, 0xC0FFEEu);
            OpenMap map(policy, 16);
            ChainMap chain(16);
            for (size_t i = 0; i < keys.size(); ++i) {
                map.Put(keys[i], int64_t(i));
                chain.Put(keys[i], int64_t(i));
            }
            for (size_t i = 0; i < keys.size(); ++i) {
                int64_t a = -1, b = -1;
                CHECK(map.Get(keys[i], &a));
                CHECK(chain.Get(keys[i], &b));
                CHECK(a == b);
            }
            CHECK(map.ProbeChainIntact());

            // 절반을 지운다. 지운 것은 사라지고 남은 것은 여전히 찾힌다 —
            // tombstone 을 EMPTY 로 되돌리는 결함은 여기서 죽는다.
            for (size_t i = 0; i < keys.size(); i += 2) {
                CHECK(map.Erase(keys[i]));
                CHECK(chain.Erase(keys[i]));
            }
            CHECK(map.ProbeChainIntact());
            for (size_t i = 0; i < keys.size(); ++i) {
                int64_t v = -1;
                const bool want = (i % 2 == 1);
                CHECK(map.Get(keys[i], &v) == want);
                CHECK(chain.Get(keys[i], &v) == want);
            }
            CHECK(map.Size() == keys.size() / 2);
            // 지운 것을 다시 넣는다
            for (size_t i = 0; i < keys.size(); i += 2) map.Put(keys[i], int64_t(i) * 10);
            CHECK(map.ProbeChainIntact());
            CHECK(map.Size() == keys.size());
            int64_t v = -1;
            CHECK(map.Get(keys[0], &v) && v == 0);
        }
    }
    // 없는 키 조회는 false 이고 표를 오염시키지 않는다.
    OpenMap map(DeletePolicy::Tombstone, 8);
    map.Put("a", 1);
    int64_t v = -1;
    CHECK(!map.Get("missing", &v));
    CHECK(!map.Erase("missing"));
    CHECK(map.Size() == 1);
}

static void TestBloomFilter() {
    BloomFilter bloom(8192, 5);
    const std::vector<std::string> inside = measure::MakeKeys(measure::KeyShape::Sequential, 800, 1u);
    for (const std::string& k : inside) bloom.Add(k);
    // 넣은 것은 반드시 있다고 말한다 — 거짓음성이 없다는 것이 이 자료구조의 계약이다.
    for (const std::string& k : inside) CHECK(bloom.MightContain(k));
    // 안 넣은 것에서 실측 거짓양성률이 예측 근처인가.
    const std::vector<std::string> outside = measure::MakeKeys(measure::KeyShape::Random, 4000, 2u);
    size_t hits = 0;
    for (const std::string& k : outside) if (bloom.MightContain(k)) ++hits;
    const double measured = double(hits) / double(outside.size());
    const double predicted = bloom.PredictedFalsePositiveRate();
    // 예측은 해시가 완전히 독립일 때의 값이다. 실측이 그 세 배를 넘거나
    // 예측이 실측의 세 배를 넘으면 모델과 구현이 어긋난 것이다.
    CHECK(measured <= predicted * 3.0 + 0.01);
    CHECK(predicted <= measured * 3.0 + 0.01);
}

static void TestSinglePatternSearch() {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"abababcabababcab", "ababc"},
        {"aaaaaaaaaa", "aaa"},              // 겹치는 일치
        {"aaaaaaaaaa", "aaaaaaaaaa"},       // 패턴 == 텍스트
        {"abc", "abcd"},                    // 패턴이 더 길다
        {"", "a"},
        {"mississippi", "issi"},
        {"the quick brown fox", " "},
        {std::string(200, 'x') + "y", "xxxy"},
    };
    RollingHash rk;
    for (const auto& tc : cases) {
        const std::vector<size_t> want = oracle::SlowSearch(tc.first, tc.second);
        CHECK(KmpSearch(tc.first, tc.second) == want);
        CHECK(ZSearch(tc.first, tc.second) == want);
        CHECK(BmhSearch(tc.first, tc.second) == want);
        CHECK(rk.Search(tc.first, tc.second, true) == want);
        CHECK(NaiveSearch(tc.first, tc.second) == want);
    }
    // prefix function 자체의 계약
    const std::vector<size_t> pi = PrefixFunction("ababaca");
    const std::vector<size_t> want_pi = {0, 0, 1, 2, 3, 0, 1};
    CHECK(pi == want_pi);
    const std::vector<size_t> z = ZArray("aaaaa");
    const std::vector<size_t> want_z = {5, 4, 3, 2, 1};
    CHECK(z == want_z);
}

// rolling hash 의 확인 단계가 실제로 필요한가. 해시가 충돌하는 텍스트를 만들어
// 확인을 끈 팔이 오보를 내는 것을 본다.
static void TestRollingHashVerification() {
    // base=2, mod=7 로 좁히면 충돌이 흔해진다. 이 좁은 파라미터가 곧 반례 생성기다.
    RollingHash weak(2, 7);
    const std::string text = "abcdefghijklmnopqrstuvwxyz0123456789";
    const std::string pat = "xyz";
    const std::vector<size_t> truth = oracle::SlowSearch(text, pat);
    const std::vector<size_t> verified = weak.Search(text, pat, true);
    const std::vector<size_t> unverified = weak.Search(text, pat, false);
    CHECK(verified == truth);
    // 확인을 끈 쪽은 오보를 낸다. 그것이 이 검사가 단언하는 바다.
    CHECK(unverified.size() > truth.size());
    // 넓은 파라미터에서는 두 팔이 실측상 같다 — "충돌이 드물다" 는 것이지
    // "없다" 가 아니라는 점이 위 반례로 확정된다.
    RollingHash wide;
    CHECK(wide.Search(text, pat, true) == truth);
}

static void TestAhoCorasick() {
    // 서로가 서로의 접미사인 패턴 집합. failure link 를 전부 root 로 두는 결함은
    // *정확히 이 입력에서만* 드러난다.
    const std::vector<std::string> pats = {"he", "she", "his", "hers", "s"};
    const std::string text = "ushers his shell here";
    AhoCorasick ac;
    for (const std::string& p : pats) ac.Add(p);
    ac.Build();
    std::vector<std::pair<size_t, size_t>> got;
    for (const auto& hit : ac.Search(text)) got.push_back({hit.end_pos, hit.pattern_id});
    std::sort(got.begin(), got.end());
    CHECK(got == oracle::SlowMultiSearch(text, pats));

    // 패턴이 하나뿐이면 KMP 와 같은 결과여야 한다.
    AhoCorasick one;
    one.Add("issi");
    one.Build();
    std::vector<size_t> starts;
    for (const auto& hit : one.Search("mississippi")) starts.push_back(hit.end_pos - 3);
    CHECK(starts == KmpSearch("mississippi", "issi"));

    // 패턴 수를 늘려도 텍스트를 한 번만 지나간다 — 상태 방문 수가 |text| 다.
    measure::Counters c;
    ac.Search(text, &c);
    CHECK(c.visited == text.size());
}

static void TestTrieArms() {
    const std::vector<std::string> words = {"car", "cart", "carbon", "dog", "do", "dodge"};
    for (bool dense : {true, false}) {
        Trie trie(dense);
        for (const std::string& w : words) trie.Insert(w);
        for (const std::string& w : words) CHECK(trie.Contains(w));
        CHECK(!trie.Contains("ca"));           // 접두는 단어가 아니다
        CHECK(trie.HasPrefix("ca"));           // 그러나 접두로는 존재한다
        CHECK(!trie.HasPrefix("z"));
        CHECK(trie.Contains("do"));            // 다른 단어의 접두이면서 단어
    }
    // 두 표현이 같은 노드 수를 쓰고, 자식 저장 바이트만 다르다.
    Trie dense(true), sparse(false);
    for (const std::string& w : words) { dense.Insert(w); sparse.Insert(w); }
    CHECK(dense.NodeCount() == sparse.NodeCount());
    CHECK(sparse.ChildBytes() < dense.ChildBytes());
}

// ===================================================================== step04

static Csr BuildUndirected(size_t n, const std::vector<Csr::Edge>& edges) {
    Csr g;
    g.Build(n, edges, true);
    return g;
}

static void TestBfsDfsInvariants() {
    // 사슬 + 가지
    const std::vector<Csr::Edge> edges = {
        {0, 1, 1}, {1, 2, 1}, {2, 3, 1}, {0, 4, 1}, {4, 3, 1}, {3, 5, 1}};
    const Csr g = BuildUndirected(6, edges);
    const BfsResult bfs = Bfs(g, 0);
    const std::vector<int64_t> want = {0, 1, 2, 2, 1, 3};
    CHECK(bfs.dist == want);
    // 계층 불변식: 모든 간선의 두 끝 거리 차가 1 이하다.
    for (const Csr::Edge& e : edges) {
        const int64_t d = bfs.dist[e.u] - bfs.dist[e.v];
        CHECK(d <= 1 && d >= -1);
    }
    // push 시점 표시. 각 정점이 정확히 한 번 큐에 들어간다.
    CHECK(bfs.pushes == 6);

    // DFS 의 괄호 구조: u 가 v 의 조상이면 tin[u] < tin[v] && tout[v] < tout[u].
    // 아니면 두 구간이 서로소다. 겹치기만 하는 쌍은 존재할 수 없다.
    const DfsResult dfs = DfsIterative(g);
    for (size_t u = 0; u < 6; ++u) {
        for (size_t v = 0; v < 6; ++v) {
            if (u == v) continue;
            const bool nested = dfs.tin[u] < dfs.tin[v] && dfs.tout[v] < dfs.tout[u];
            const bool nested_other = dfs.tin[v] < dfs.tin[u] && dfs.tout[u] < dfs.tout[v];
            const bool disjoint = dfs.tout[u] < dfs.tin[v] || dfs.tout[v] < dfs.tin[u];
            CHECK(nested || nested_other || disjoint);
        }
    }
    // 도달 불가는 -1 로 남는다
    const Csr split = BuildUndirected(4, {{0, 1, 1}, {2, 3, 1}});
    const BfsResult part = Bfs(split, 0);
    CHECK(part.dist[2] == -1 && part.dist[3] == -1);
}

static void TestCycleAndTopo() {
    // 평행 간선. '부모 정점으로 돌아가는 간선 무시' 로 짜면 이것을 놓친다.
    const Csr parallel = BuildUndirected(2, {{0, 1, 1}, {0, 1, 1}});
    CHECK(HasCycleUndirected(parallel));
    const Csr tree = BuildUndirected(3, {{0, 1, 1}, {1, 2, 1}});
    CHECK(!HasCycleUndirected(tree));
    const Csr triangle = BuildUndirected(3, {{0, 1, 1}, {1, 2, 1}, {2, 0, 1}});
    CHECK(HasCycleUndirected(triangle));

    // 방향 그래프: 완성된 정점으로 가는 간선은 순환이 아니다.
    Csr diamond;
    diamond.Build(4, {{0, 1, 1}, {0, 2, 1}, {1, 3, 1}, {2, 3, 1}}, false);
    CHECK(!HasCycleDirected(diamond));
    Csr loop;
    loop.Build(3, {{0, 1, 1}, {1, 2, 1}, {2, 0, 1}}, false);
    CHECK(HasCycleDirected(loop));

    // 위상 정렬. 결과가 결정적이고, 모든 간선이 순서를 지킨다.
    const TopoResult topo = TopoSortKahn(diamond);
    CHECK(topo.acyclic);
    const std::vector<uint32_t> want = {0, 1, 2, 3};
    CHECK(topo.order == want);
    std::vector<size_t> position(4);
    for (size_t i = 0; i < topo.order.size(); ++i) position[topo.order[i]] = i;
    const std::pair<uint32_t, uint32_t> arcs[4] = {{0, 1}, {0, 2}, {1, 3}, {2, 3}};
    for (const auto& e : arcs) CHECK(position[e.first] < position[e.second]);
    // 순환이면 정렬이 실패한다 — 부분 출력을 성공으로 보고하지 않는다.
    const TopoResult bad = TopoSortKahn(loop);
    CHECK(!bad.acyclic);
    CHECK(bad.order.size() < 3);
}

static void TestScc() {
    Csr g;
    g.Build(8, {{0, 1, 1}, {1, 2, 1}, {2, 0, 1},        // 성분 {0,1,2}
                {3, 4, 1}, {4, 3, 1},                    // 성분 {3,4}
                {2, 3, 1}, {4, 5, 1},                    // 성분 사이 간선
                {5, 6, 1}, {6, 7, 1}}, false);           // 단독 성분들
    const SccResult tar = SccTarjan(g);
    const SccResult kos = SccKosaraju(g);
    CHECK(tar.count == kos.count);
    CHECK(tar.count == 5);
    // 성분 번호는 다를 수 있다. 같은 것은 *분할* 이다.
    for (uint32_t u = 0; u < 8; ++u)
        for (uint32_t v = 0; v < 8; ++v)
            CHECK((tar.comp[u] == tar.comp[v]) == (kos.comp[u] == kos.comp[v]));

    // condensation 은 반드시 DAG 다. 아니면 두 성분이 하나였어야 한다.
    std::vector<Csr::Edge> cond;
    for (uint32_t u = 0; u < 8; ++u)
        for (size_t i = g.start[u]; i < g.start[u + 1]; ++i)
            if (tar.comp[u] != tar.comp[g.to[i]])
                cond.push_back({tar.comp[u], tar.comp[g.to[i]], 1});
    Csr cg;
    cg.Build(tar.count, cond, false);
    CHECK(!HasCycleDirected(cg));
}

static void TestArticulationAndBridges() {
    struct Case { size_t n; std::vector<Csr::Edge> edges; };
    const std::vector<Case> cases = {
        {5, {{0, 1, 1}, {1, 2, 1}, {2, 3, 1}, {3, 4, 1}}},                    // 경로
        {5, {{0, 1, 1}, {1, 2, 1}, {2, 0, 1}, {2, 3, 1}, {3, 4, 1}}},         // 삼각형 + 꼬리
        {4, {{0, 1, 1}, {1, 2, 1}, {2, 3, 1}, {3, 0, 1}}},                    // 사이클
        // 나비넥타이. 삼각형 둘이 정점 2 에서만 만난다 — 2 는 절단점이지만
        // *다리는 하나도 없다*. `low[v] >= tin[u]` 를 `>` 로 쓴 구현은 다른
        // 모양에서는 전부 맞히고 정확히 여기서만 절단점을 놓친다.
        {5, {{0, 1, 1}, {1, 2, 1}, {2, 0, 1}, {2, 3, 1}, {3, 4, 1}, {4, 2, 1}}},
        {2, {{0, 1, 1}, {0, 1, 1}}},                                          // 평행 간선
        {6, {{0, 1, 1}, {1, 2, 1}, {2, 0, 1}, {3, 4, 1}, {4, 5, 1}, {5, 3, 1}}},  // 두 성분
        {1, {}},
    };
    for (const Case& tc : cases) {
        const Csr g = BuildUndirected(tc.n, tc.edges);
        const CutResult got = ArticulationAndBridges(g);
        // 정점을 실제로 지우고 BFS 를 다시 돌린 답과 전 칸 일치해야 한다.
        CHECK(got.articulation == oracle::SlowArticulation(tc.n, tc.edges));
        CHECK(got.bridges == oracle::SlowBridges(tc.n, tc.edges));
    }
    // 평행 간선은 다리가 아니다 — 지워도 여전히 이어진다.
    const Csr par = BuildUndirected(2, {{0, 1, 1}, {0, 1, 1}});
    CHECK(ArticulationAndBridges(par).bridges.empty());
}

static void TestDsuArms() {
    const std::vector<Csr::Edge> edges = {
        {0, 1, 1}, {2, 3, 1}, {1, 2, 1}, {4, 5, 1}, {6, 6, 1}, {0, 3, 1}};
    const std::vector<uint32_t> want = oracle::ComponentIdByBfs(7, edges);
    for (bool compress : {false, true}) {
        for (bool by_size : {false, true}) {
            Dsu dsu(7, compress, by_size);
            for (const Csr::Edge& e : edges) dsu.Union(e.u, e.v);
            for (uint32_t u = 0; u < 7; ++u)
                for (uint32_t v = 0; v < 7; ++v)
                    CHECK(dsu.Connected(u, v) == (want[u] == want[v]));
        }
    }
    // 네 팔의 평균 find 깊이. 사슬처럼 붙이면 최적화의 차이가 수치로 드러난다.
    // 다만 union by size 를 끄는 것만으로는 *정답* 이 달라지지 않는다 — 그래서
    // 위 정확성 검사는 네 팔 모두 통과한다. 이것이 무력화 변이의 정직한 경계다.
    double depth[2][2];
    for (int ci = 0; ci < 2; ++ci) {
        for (int si = 0; si < 2; ++si) {
            Dsu dsu(2000, ci == 1, si == 1);
            for (uint32_t i = 1; i < 2000; ++i) dsu.Union(i, i - 1);   // 새 정점을 뿌리로 — 이 순서라야 사슬이 자란다
            dsu.ResetStats();
            for (uint32_t i = 0; i < 2000; ++i) dsu.Find(i);
            depth[ci][si] = dsu.AverageFindDepth();
        }
    }
    CHECK(depth[1][1] <= depth[0][0]);   // 둘 다 켠 쪽이 둘 다 끈 쪽보다 얕다
    CHECK(depth[1][1] < 2.0);            // 경로 압축이 실제로 눌렀는가
}

static void TestMst() {
    struct Case { size_t n; std::vector<Csr::Edge> edges; };
    const std::vector<Case> cases = {
        {4, {{0, 1, 1}, {1, 2, 2}, {2, 3, 3}, {0, 3, 4}, {0, 2, 5}}},
        {5, {{0, 1, 2}, {0, 3, 6}, {1, 2, 3}, {1, 3, 8}, {1, 4, 5}, {2, 4, 7}, {3, 4, 9}}},
        {3, {{0, 1, 5}, {1, 2, 5}, {0, 2, 5}}},        // 전부 동점
        {4, {{0, 1, 1}, {2, 3, 1}}},                   // 비연결
    };
    for (const Case& tc : cases) {
        const MstResult k = Kruskal(tc.n, tc.edges);
        const MstResult p = PrimDense(tc.n, tc.edges);
        bool spanning = false;
        const int64_t brute = oracle::BruteMstCost(tc.n, tc.edges, &spanning);
        CHECK(k.spanning == spanning);
        CHECK(p.spanning == spanning);
        if (spanning) {
            CHECK(k.total == brute);
            CHECK(p.total == brute);
        }
    }
    // MST 는 최단 경로를 보장하지 않는다. 두 목적 함수가 다르다는 반례.
    const std::vector<Csr::Edge> star = {{0, 1, 3}, {0, 2, 3}, {1, 2, 4}};
    const MstResult m = Kruskal(3, star);
    CHECK(m.total == 6);                            // 0-1, 0-2 를 고른다
    const Csr g = BuildUndirected(3, star);
    const PathResult sp = DijkstraHeap(g, 1);
    CHECK(sp.dist[2] == 4);                         // 최단 경로는 1-2 직행이다
    // 그런데 그 간선은 MST 에 없다.
    CHECK(std::find(m.edges.begin(), m.edges.end(), 2u) == m.edges.end());
}

// ===================================================================== step05

static void TestShortestPathArms() {
    measure::Rng rng(0x9A11u);
    for (int trial = 0; trial < 12; ++trial) {
        const size_t n = 6 + rng.Below(5);
        std::vector<Csr::Edge> edges;
        for (uint32_t u = 0; u < n; ++u)
            for (uint32_t v = 0; v < n; ++v) {
                if (u == v || rng.Below(100) >= 40) continue;
                edges.push_back({u, v, int64_t(rng.Below(9)) + 1});
            }
        Csr g;
        g.Build(n, edges, false);
        const auto floyd = oracle::SlowAllPairs(n, edges, false);
        for (uint32_t s = 0; s < n; ++s) {
            const PathResult dij  = DijkstraHeap(g, s);
            const PathResult dense = DijkstraDense(g, s);
            const PathResult bf   = BellmanFord(n, edges, s);
            for (uint32_t v = 0; v < n; ++v) {
                CHECK(dij.dist[v] == floyd[s][v]);
                CHECK(dense.dist[v] == floyd[s][v]);
                CHECK(bf.dist[v] == floyd[s][v]);
            }
            CHECK(!bf.negative_cycle);
        }
    }
    // 비용이 전부 1 이면 BFS 와 일치한다. 비용이 0/1 이면 0-1 BFS 와 일치한다.
    std::vector<Csr::Edge> unit;
    for (uint32_t u = 0; u + 1 < 8; ++u) unit.push_back({u, u + 1, 1});
    unit.push_back({0, 7, 1});
    Csr ug;
    ug.Build(8, unit, false);
    const BfsResult b = Bfs(ug, 0);
    const PathResult d = DijkstraHeap(ug, 0);
    for (uint32_t v = 0; v < 8; ++v) CHECK(b.dist[v] == d.dist[v]);

    std::vector<Csr::Edge> zero_one = {{0, 1, 0}, {1, 2, 1}, {0, 2, 1}, {2, 3, 0}, {1, 3, 1}};
    Csr zg;
    zg.Build(4, zero_one, false);
    const PathResult z = ZeroOneBfs(zg, 0);
    const PathResult zd = DijkstraHeap(zg, 0);
    for (uint32_t v = 0; v < 4; ++v) CHECK(z.dist[v] == zd.dist[v]);
}

// Dijkstra 의 확정 논리가 무엇에 기대는지를 반례 하나로 못 박는다.
static void TestNegativeEdgeBreaksDijkstra() {
    // S->A: 1, S->B: 2, B->A: -2. 진짜 최단 dist(A) = 0 이다.
    const std::vector<Csr::Edge> edges = {{0, 1, 1}, {0, 2, 2}, {2, 1, -2}};
    Csr g;
    g.Build(3, edges, false);

    const PathResult bf = BellmanFord(3, edges, 0);
    CHECK(bf.dist[1] == 0);
    CHECK(!bf.negative_cycle);

    // 확정 논리를 켠 교과서 Dijkstra 는 A 를 1 로 확정하고 끝낸다.
    DijkstraOptions strict;
    strict.final_on_settle = true;
    const PathResult wrong = DijkstraHeap(g, 0, strict);
    CHECK(wrong.dist[1] == 1);          // 틀린 답이 나오는 것을 그대로 단언한다
    CHECK(wrong.dist[1] != bf.dist[1]);

    // 확정을 포기하면(= 계속 완화하면) 답은 맞지만 그것은 이미 Dijkstra 가 아니다.
    DijkstraOptions relaxed;
    relaxed.final_on_settle = false;
    const PathResult loose = DijkstraHeap(g, 0, relaxed);
    CHECK(loose.dist[1] == 0);
    CHECK(loose.settled > wrong.settled);   // 대가는 확장 수다

    // 음수 순환. 최단 경로 자체가 정의되지 않는다는 것을 판정해야 한다.
    const std::vector<Csr::Edge> cyc = {{0, 1, 1}, {1, 2, -1}, {2, 1, -1}};
    const PathResult neg = BellmanFord(3, cyc, 0);
    CHECK(neg.negative_cycle);
}

// 도달 불가 정점에서 완화를 시도하면 INF + w 가 감긴다.
static void TestOverflowGuard() {
    // 0 에서 1 로만 갈 수 있고, 2->3 은 도달 불가 영역의 음수 간선이다.
    const std::vector<Csr::Edge> edges = {{0, 1, 5}, {2, 3, -1000}};
    const PathResult r = BellmanFord(4, edges, 0);
    CHECK(r.dist[1] == 5);
    CHECK(r.dist[2] == kInfCost);
    // 가드가 없으면 dist[3] = kInfCost - 1000 이 되어 *도달 가능해 보인다*.
    CHECK(r.dist[3] == kInfCost);
}

static void TestAStarGrid() {
    measure::Rng rng(0xA5A5u);
    for (int trial = 0; trial < 10; ++trial) {
        Grid grid;
        grid.w = 14;
        grid.h = 11;
        grid.blocked.assign(grid.w * grid.h, 0);
        for (size_t i = 0; i < grid.blocked.size(); ++i)
            grid.blocked[i] = (rng.Below(100) < 22) ? 1u : 0u;
        grid.blocked[0] = 0;
        grid.blocked.back() = 0;
        const uint32_t start = 0;
        const uint32_t goal = uint32_t(grid.w * grid.h - 1);

        // 참값은 휴리스틱 0 짜리 A* (= Dijkstra) 로 얻는다.
        AStarOptions zero;
        zero.heuristic = Heuristic::Zero;
        const AStarResult truth = AStarGrid(grid, start, goal, zero);

        AStarOptions manhattan;
        manhattan.heuristic = Heuristic::Manhattan;
        const AStarResult a = AStarGrid(grid, start, goal, manhattan);
        CHECK(a.found == truth.found);
        if (!truth.found) continue;
        // admissible + consistent 이면 최적이다.
        CHECK(a.cost == truth.cost);
        // 경로가 실제로 유효하고 보고 비용과 같다 — 독립 검사기가 판정한다.
        CHECK(ValidateGridPath(grid, a.path, start, goal, a.cost));
        CHECK(ValidateGridPath(grid, truth.path, start, goal, truth.cost));
        // 휴리스틱이 있는 쪽이 확장을 더 하지는 않는다.
        CHECK(a.expanded <= truth.expanded);

        // 가중 A*: w>1 이면 최적성을 잃을 수 있고, 대신 확장이 줄어든다.
        AStarOptions weighted = manhattan;
        weighted.weight = 3.0;
        const AStarResult w = AStarGrid(grid, start, goal, weighted);
        CHECK(w.found);
        CHECK(w.cost >= truth.cost);                    // 절대 더 좋아질 수 없다
        CHECK(ValidateGridPath(grid, w.path, start, goal, w.cost));
    }
    // 도달 불가
    Grid wall;
    wall.w = 3;
    wall.h = 3;
    wall.blocked = {0, 1, 0, 0, 1, 0, 0, 1, 0};
    AStarOptions opt;
    CHECK(!AStarGrid(wall, 0, 8, opt).found);
}

// admissible 이지만 inconsistent 한 휴리스틱. 재개방을 끄면 최적을 잃는다.
static void TestInconsistentHeuristic() {
    //  S=0, A=1, B=2, G=3
    //  0->1 (3)   0->2 (1)   2->1 (1)   1->3 (10)
    //  최적: 0->2->1->3 = 12.  나쁜 길: 0->1->3 = 13.
    const std::vector<Csr::Edge> edges = {{0, 1, 3}, {0, 2, 1}, {2, 1, 1}, {1, 3, 10}};
    Csr g;
    g.Build(4, edges, false);
    //  h*(A)=10 인데 h(A)=0 으로 느슨하게 두고, h(B)=6 은 h*(B)=11 이하라 admissible.
    //  그러나 h(B)=6 > cost(B,A) + h(A) = 1 이라 inconsistent 하다.
    const std::vector<int64_t> h = {0, 0, 6, 0};
    CHECK(!ConsistentOnGraph(g, h));
    for (uint32_t v = 0; v < 4; ++v) {
        const PathResult truth = DijkstraHeap(g, v);
        CHECK(h[v] <= truth.dist[3]);              // admissible 은 지킨다
    }
    const AStarGraphResult with_reopen = AStarGraph(g, 0, 3, h, true);
    const AStarGraphResult no_reopen   = AStarGraph(g, 0, 3, h, false);
    CHECK(with_reopen.found && no_reopen.found);
    CHECK(with_reopen.cost == 12);      // 최적
    CHECK(no_reopen.cost == 13);        // 재개방을 끄면 잃는다
    CHECK(with_reopen.reopened >= 1);
    CHECK(no_reopen.reopened == 0);

    // consistent 한 휴리스틱이면 두 정책이 같은 답을 낸다 — 재개방이 필요 없다.
    const std::vector<int64_t> good = {0, 0, 0, 0};
    CHECK(ConsistentOnGraph(g, good));
    CHECK(AStarGraph(g, 0, 3, good, true).cost == AStarGraph(g, 0, 3, good, false).cost);
}

// ===================================================================== step06

static void TestGreedyExchangeHolds() {
    // 활동 선택. 가장 빨리 끝나는 것을 고르는 규칙이 최적이다.
    const std::vector<Interval> items = {
        {1, 4}, {3, 5}, {0, 6}, {5, 7}, {3, 9}, {5, 9}, {6, 10}, {8, 11}, {8, 12},
        {2, 14}, {12, 16}};
    const std::vector<size_t> chosen = ActivitySelect(items);
    CHECK(chosen.size() == 4);
    // 고른 것들이 실제로 겹치지 않는다
    for (size_t i = 1; i < chosen.size(); ++i)
        CHECK(items[chosen[i - 1]].finish <= items[chosen[i]].start);
    // 전수 열거로 최대 개수를 확인한다 (n=11 이면 2048 가지)
    size_t best = 0;
    for (uint32_t mask = 0; mask < (1u << items.size()); ++mask) {
        std::vector<Interval> picked;
        for (size_t i = 0; i < items.size(); ++i)
            if (mask & (1u << i)) picked.push_back(items[i]);
        std::sort(picked.begin(), picked.end(),
                  [](const Interval& a, const Interval& b) { return a.start < b.start; });
        bool ok = true;
        for (size_t i = 1; i < picked.size(); ++i)
            if (picked[i].start < picked[i - 1].finish) { ok = false; break; }
        if (ok && picked.size() > best) best = picked.size();
    }
    CHECK(chosen.size() == best);

    // EDD. 마감 오름차순이 최대 지연을 최소화한다 — 순열 전수 열거로 확인한다.
    const std::vector<Job> jobs = {{3, 6}, {2, 8}, {1, 9}, {4, 9}, {3, 14}, {2, 15}};
    const int64_t edd = MaxLatenessEdd(jobs);
    std::vector<size_t> perm(jobs.size());
    for (size_t i = 0; i < jobs.size(); ++i) perm[i] = i;
    int64_t brute = INT64_MAX;
    do {
        int64_t clock = 0, worst = INT64_MIN;
        for (size_t k : perm) {
            clock += jobs[k].length;
            worst = std::max(worst, clock - jobs[k].due);
        }
        brute = std::min(brute, worst);
    } while (std::next_permutation(perm.begin(), perm.end()));
    CHECK(edd == brute);
}

static void TestGreedyExchangeBreaks() {
    // 0/1 배낭. 밀도 탐욕은 160, 최적은 220.
    const std::vector<Item> items = {{10, 60}, {20, 100}, {30, 120}};
    CHECK(DensityGreedyKnapsack(items, 50) == 160);
    CHECK(oracle::BruteKnapsack(items, 50) == 220);
    // 분할 가능하면 같은 순서가 최적이다 — 교환 논증의 마지막 단계가 살아난다.
    CHECK(FractionalKnapsack(items, 50) > 239.9 && FractionalKnapsack(items, 50) < 240.1);

    // 동전 {1,3,4} 로 6. 탐욕 3개, 최적 2개.
    const std::vector<int64_t> coins = {1, 3, 4};
    CHECK(CoinGreedy(coins, 6) == 3);
    CHECK(oracle::ExactCoinCount(coins, 6) == 2);
    // 정준 화폐계에서는 탐욕이 최적이다.
    const std::vector<int64_t> canonical = {1, 5, 10, 25};
    for (int64_t a = 0; a <= 99; ++a)
        CHECK(CoinGreedy(canonical, a) == oracle::ExactCoinCount(canonical, a));

    // 집합 덮개. 탐욕은 최적이 아니지만 조화합 상계 안에 있다.
    const std::vector<std::vector<uint32_t>> sets = {
        {0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 4, 5}, {2, 3, 6, 7}, {0, 2, 4, 6}};
    const std::vector<int64_t> cost(sets.size(), 1);
    const SetCoverResult greedy = SetCoverGreedy(8, sets, cost);
    const int64_t best = oracle::BruteSetCover(8, sets, cost);
    CHECK(greedy.covered);
    CHECK(greedy.cost >= best);
    CHECK(double(greedy.cost) <= double(best) * HarmonicBound(8) + 1e-9);

    // 온라인 대여-구매. break-even 전략의 경쟁비가 상계 안이다.
    for (int64_t buy : {int64_t(5), int64_t(10), int64_t(37)}) {
        double worst = 0.0;
        for (int64_t days = 1; days <= buy * 3; ++days)
            worst = std::max(worst, SkiRental(days, buy, buy - 1).Ratio());
        CHECK(worst <= CompetitiveBound(buy) + 1e-9);
        CHECK(worst > CompetitiveBound(buy) - 1e-9);   // 상계가 헐겁지 않다
    }
}

static void TestDpKnapsackArms() {
    measure::Rng rng(0xD9D9u);
    for (int trial = 0; trial < 25; ++trial) {
        const size_t n = 1 + rng.Below(12);
        std::vector<Item> items;
        for (size_t i = 0; i < n; ++i)
            items.push_back(Item{int64_t(rng.Below(15)) + 1, int64_t(rng.Below(40)) + 1});
        const int64_t capacity = int64_t(rng.Below(45)) + 1;

        const int64_t brute = oracle::BruteKnapsack(items, capacity);
        const KnapsackResult dp2 = Knapsack2D(items, capacity);
        const int64_t dp1 = Knapsack1D(items, capacity, true);
        const BranchBoundResult bb = BranchAndBoundKnapsack(items, capacity);
        CHECK(dp2.value == brute);
        CHECK(dp1 == brute);
        CHECK(bb.best == brute);
        // 복원된 선택이 실제로 그 값을 내고 용량을 지키는가.
        int64_t w = 0, v = 0;
        for (size_t k : dp2.chosen) { w += items[k].weight; v += items[k].value; }
        CHECK(w <= capacity);
        CHECK(v == dp2.value);
        // 밀도 탐욕은 절대 최적을 넘지 못한다.
        CHECK(DensityGreedyKnapsack(items, capacity) <= brute);
        // 분기 한정이 실제로 가지를 쳤는가 — 전수 2^n 보다 노드가 적어야 한다.
        if (n >= 8) CHECK(bb.nodes < (uint64_t(1) << n));
    }
}

// 1차원 압축의 순회 방향이 문제를 바꾼다.
static void TestDpDirectionChangesProblem() {
    const std::vector<Item> items = {{2, 3}, {3, 4}, {4, 5}};
    const int64_t capacity = 8;
    const int64_t once = Knapsack1D(items, capacity, true);    // 내림차순: 0/1
    const int64_t many = Knapsack1D(items, capacity, false);   // 오름차순: unbounded
    CHECK(once == oracle::BruteKnapsack(items, capacity));
    CHECK(once == 9);           // (3,4)+(4,5) — 물건마다 한 번씩만
    CHECK(many == 12);          // (2,3) 을 네 번 — 0/1 이면 불가능한 값
    CHECK(many > once);
    // 2차원 표는 방향에 무관하게 0/1 이다.
    CHECK(Knapsack2D(items, capacity).value == once);
}

static void TestDpStrings() {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"AGGTAB", "GXTXAYB"}, {"", "abc"}, {"abc", ""},
        {"abcd", "abcd"}, {"abc", "def"}, {"aaaa", "aa"}};
    for (const auto& tc : cases) {
        const LcsResult lcs = Lcs(tc.first, tc.second);
        CHECK(lcs.length == oracle::BruteLcs(tc.first, tc.second));
        CHECK(lcs.sequence.size() == lcs.length);
        // 복원된 것이 실제로 양쪽의 부분 수열인가 — 값만 맞고 복원이 틀린 결함을 잡는다.
        for (const std::string& s : {tc.first, tc.second}) {
            size_t j = 0;
            for (char ch : s) if (j < lcs.sequence.size() && lcs.sequence[j] == ch) ++j;
            CHECK(j == lcs.sequence.size());
        }
    }
    CHECK(EditDistance("kitten", "sitting") == 3);
    CHECK(EditDistance("", "abc") == 3);
    CHECK(EditDistance("abc", "abc") == 0);
    // metamorphic: 두 문자열 앞에 같은 고유 심볼을 붙이면 LCS 가 정확히 1 늘어난다.
    for (const auto& tc : cases) {
        const size_t before = Lcs(tc.first, tc.second).length;
        const size_t after = Lcs("~" + tc.first, "~" + tc.second).length;
        CHECK(after == before + 1);
    }
}

static void TestDpSequencesAndStructures() {
    measure::Rng rng(0x1155u);
    for (int trial = 0; trial < 20; ++trial) {
        const size_t n = 1 + rng.Below(14);
        std::vector<int64_t> a(n);
        for (size_t i = 0; i < n; ++i) a[i] = int64_t(rng.Below(12));
        const size_t brute = oracle::BruteLis(a);
        CHECK(LisQuadratic(a) == brute);
        const std::vector<int64_t> restored = LisRestore(a);
        CHECK(restored.size() == brute);
        for (size_t i = 1; i < restored.size(); ++i) CHECK(restored[i - 1] < restored[i]);
        // 복원된 것이 실제 부분 수열인가
        size_t j = 0;
        for (int64_t x : a) if (j < restored.size() && restored[j] == x) ++j;
        CHECK(j == restored.size());
    }
    // 구간 DP. 손으로 아는 사례 + 전수 열거 대조.
    const std::vector<int64_t> dims = {30, 35, 15, 5, 10, 20, 25};
    CHECK(MatrixChain(dims) == 15125);
    CHECK(MatrixChain({10, 20}) == 0);

    // 트리 DP. 작은 분기 트리(0→{1,2}, 1→{3,4})에서 최대 가중 독립 집합.
    std::vector<std::vector<uint32_t>> children = {{1, 2}, {3, 4}, {}, {}, {}};
    const std::vector<int64_t> weight = {10, 5, 6, 4, 4};
    CHECK(TreeMaxWeightIndependentSet(children, weight, 0) == 18);   // {0, 3, 4}
    // metamorphic: 트리 정점 id 를 뒤집어도 값이 같다.
    std::vector<std::vector<uint32_t>> flipped = {{}, {}, {}, {2, 1}, {3, 0}};
    const std::vector<int64_t> flipped_weight = {6, 4, 4, 5, 10};
    CHECK(TreeMaxWeightIndependentSet(flipped, flipped_weight, 4) == 18);

    // 비트마스크 DP. 대칭 비용 행렬의 열린 순회.
    const std::vector<std::vector<int64_t>> cost = {
        {0, 2, 9, 10}, {1, 0, 6, 4}, {15, 7, 0, 8}, {6, 3, 12, 0}};
    const int64_t tour = BitmaskMinTour(cost, 0);
    // 순열 전수 열거로 확인 (n=4)
    std::vector<size_t> rest = {1, 2, 3};
    int64_t brute = INT64_MAX;
    do {
        int64_t total = 0;
        size_t at = 0;
        for (size_t v : rest) { total += cost[at][v]; at = v; }
        brute = std::min(brute, total);
    } while (std::next_permutation(rest.begin(), rest.end()));
    CHECK(tour == brute);
}

// 값 전체를 c 배 하면 최적값도 c 배다. 용량이 늘면 최적값은 줄지 않는다.
static void TestDpMetamorphic() {
    measure::Rng rng(0x2468u);
    for (int trial = 0; trial < 15; ++trial) {
        const size_t n = 1 + rng.Below(9);
        std::vector<Item> items;
        for (size_t i = 0; i < n; ++i)
            items.push_back(Item{int64_t(rng.Below(10)) + 1, int64_t(rng.Below(20)) + 1});
        const int64_t capacity = int64_t(rng.Below(30)) + 1;
        const int64_t base = Knapsack2D(items, capacity).value;

        std::vector<Item> scaled = items;
        for (Item& it : scaled) it.value *= 7;
        CHECK(Knapsack2D(scaled, capacity).value == base * 7);

        CHECK(Knapsack2D(items, capacity + 5).value >= base);
        CHECK(Knapsack2D(items, capacity).value >= Knapsack2D(items, capacity - 1).value ||
              capacity == 1);
        // 물건 순서를 뒤집어도 최적값은 같다.
        std::vector<Item> reversed(items.rbegin(), items.rend());
        CHECK(Knapsack2D(reversed, capacity).value == base);
    }
}

// ===================================================================== step07

struct FlowCase { size_t n; uint32_t s, t; std::vector<std::array<int64_t, 3>> edges; };

static Residual BuildFlow(const FlowCase& fc) {
    Residual g(fc.n);
    for (const auto& e : fc.edges) g.AddEdge(uint32_t(e[0]), uint32_t(e[1]), e[2]);
    return g;
}

static void TestMaxFlowArms() {
    const std::vector<FlowCase> cases = {
        {6, 0, 5, {{0,1,16},{0,2,13},{1,2,10},{2,1,4},{1,3,12},{3,2,9},
                   {2,4,14},{4,3,7},{3,5,20},{4,5,4}}},
        {4, 0, 3, {{0,1,1000000},{0,2,1000000},{1,2,1},{1,3,1000000},{2,3,1000000}}},
        {2, 0, 1, {{0,1,5}}},
        {3, 0, 2, {}},                              // 경로 없음
        {4, 0, 3, {{0,1,3},{1,2,2},{2,3,3},{0,2,1},{1,3,1}}},
    };
    for (const FlowCase& fc : cases) {
        Residual a = BuildFlow(fc), b = BuildFlow(fc), c = BuildFlow(fc);
        const FlowResult ff = FordFulkerson(a, fc.s, fc.t);
        const FlowResult ek = EdmondsKarp(b, fc.s, fc.t);
        Dinic dinic;
        const FlowResult dn = dinic.Run(c, fc.s, fc.t);
        CHECK(ff.value == ek.value);
        CHECK(ek.value == dn.value);

        // 불변식 셋. 값이 맞아도 이 셋 중 하나가 깨질 수 있다 —
        // reverse index 짝이 어긋난 구현이 정확히 그렇게 죽는다.
        for (const Residual* g : {&a, &b, &c}) {
            CHECK(g->CapacityRespected());
            CHECK(g->Conserves(fc.s, fc.t));
        }
        CHECK(a.NetOutOf(fc.s) == ff.value);
        CHECK(b.NetOutOf(fc.s) == ek.value);
        CHECK(c.NetOutOf(fc.s) == dn.value);

        // certificate: 잔여 그래프에 증가 경로가 없고, 절단 용량이 유량과 같다.
        for (const Residual* g : {&a, &b, &c}) {
            const MinCut cut = ExtractMinCut(*g, fc.s);
            CHECK(!cut.source_side[fc.t]);           // 증가 경로가 없다
            CHECK(cut.capacity == ff.value);         // max-flow == min-cut
        }
    }
    // Edmonds-Karp 는 큰 용량에서도 증가 횟수가 유량값에 비례하지 않는다.
    Residual big = BuildFlow(cases[1]);
    const FlowResult ek = EdmondsKarp(big, 0, 3);
    CHECK(ek.value == 2000000);
    CHECK(ek.augments <= 4);      // 유량 200만인데 증가는 몇 번뿐이다
}

static void TestMatchingArms() {
    struct Case { measure::Bipartite b; };
    std::vector<measure::Bipartite> cases = {
        measure::HallViolatingBipartite(5),
        measure::UniquePerfectBipartite(6),
        measure::RandomBipartite(6, 6, 0.35, 11u),
        measure::RandomBipartite(7, 4, 0.5, 22u),
        measure::RandomBipartite(4, 4, 0.0, 33u),
    };
    for (const measure::Bipartite& b : cases) {
        const Matching dfs = BipartiteMatchingDfs(b.left, b.right, b.adj);
        const Matching hk  = HopcroftKarp(b.left, b.right, b.adj);
        const int64_t flow = MatchingViaFlow(b.left, b.right, b.adj);
        const size_t brute = oracle::BruteMatching(b.left, b.right, b.adj);
        CHECK(dfs.size == brute);
        CHECK(hk.size == brute);
        CHECK(size_t(flow) == brute);
        if (b.known_max) CHECK(brute == b.known_max);

        // 매칭이 실제로 매칭인가 — 한 정점이 두 번 쓰이지 않는다.
        for (const Matching* m : {&dfs, &hk}) {
            std::vector<uint8_t> used_right(b.right, 0);
            size_t counted = 0;
            for (size_t u = 0; u < b.left; ++u) {
                if (m->right_of[u] == -1) continue;
                const uint32_t v = uint32_t(m->right_of[u]);
                CHECK(!used_right[v]);
                used_right[v] = 1;
                CHECK(m->left_of[v] == int32_t(u));
                CHECK(std::find(b.adj[u].begin(), b.adj[u].end(), v) != b.adj[u].end());
                ++counted;
            }
            CHECK(counted == m->size);
        }
    }
    // Hopcroft-Karp 의 phase 수가 sqrt(V) 규모다.
    const measure::Bipartite big = measure::RandomBipartite(64, 64, 0.06, 99u);
    const Matching hk = HopcroftKarp(big.left, big.right, big.adj);
    CHECK(hk.phases <= 16);
    CHECK(hk.size == BipartiteMatchingDfs(big.left, big.right, big.adj).size);
}

static void TestMinCostAndAssignment() {
    measure::Rng rng(0x7777u);
    for (int trial = 0; trial < 8; ++trial) {
        const size_t n = 2 + rng.Below(5);
        std::vector<std::vector<int64_t>> cost(n, std::vector<int64_t>(n, 0));
        for (size_t i = 0; i < n; ++i)
            for (size_t j = 0; j < n; ++j) cost[i][j] = int64_t(rng.Below(30));
        std::vector<size_t> assign;
        const int64_t hung = Hungarian(cost, &assign);
        CHECK(hung == oracle::BruteAssignment(cost));
        // 배정이 실제 순열인가
        std::vector<uint8_t> seen(n, 0);
        int64_t total = 0;
        for (size_t i = 0; i < n; ++i) {
            CHECK(assign[i] < n);
            CHECK(!seen[assign[i]]);
            seen[assign[i]] = 1;
            total += cost[i][assign[i]];
        }
        CHECK(total == hung);

        // 같은 문제를 최소 비용 유량으로. 값이 같아야 한다.
        Residual g(2 * n + 2);
        const uint32_t s = uint32_t(2 * n), t = s + 1;
        for (uint32_t i = 0; i < n; ++i) g.AddEdge(s, i, 1, 0);
        for (uint32_t j = 0; j < n; ++j) g.AddEdge(uint32_t(n) + j, t, 1, 0);
        for (uint32_t i = 0; i < n; ++i)
            for (uint32_t j = 0; j < n; ++j)
                g.AddEdge(i, uint32_t(n) + j, 1, cost[i][j]);
        const MinCostResult mc = MinCostMaxFlow(g, s, t);
        CHECK(mc.flow == int64_t(n));
        CHECK(mc.cost == hung);
    }
}

static void TestCspApplyUndo() {
    // (가) 5 정점 사이클의 3색 칠하기. 홀수 사이클이라 2색으로는 불가능하다.
    // 후향 검사가 빠지면 3^5 = 243 이 나오는 자리 — 그 회귀는 여기서 잡는다.
    CspProblem c5;
    c5.variables = 5;
    c5.full_domain = 0b111;
    for (uint32_t i = 0; i < 5; ++i) c5.not_equal.push_back({i, (i + 1) % 5});
    CspSolver c5_base(c5, CspOptions{});
    CHECK(c5_base.CountSolutions() == 30);   // (3-1)^5 - (3-1) = 30
    CHECK(c5_base.HashStable());

    // (나) 가지치기의 *효과* 를 재는 인스턴스. C5·3색은 도메인이 좁아 전방 검사와
    // AC-3 가 한 번도 발동하지 않는다 — 어느 팔이든 노드 수가 기준과 똑같이 나온다.
    // 그래서 값 4개·차수 4인 순환 그래프 C9(1,2) 를 쓴다(bench 의 CSP 표와 같은 문제).
    CspProblem p;
    p.variables = 9;
    p.full_domain = 0b1111;
    for (uint32_t i = 0; i < p.variables; ++i) {
        p.not_equal.push_back({i, uint32_t((i + 1) % p.variables)});
        p.not_equal.push_back({i, uint32_t((i + 2) % p.variables)});
    }

    CspOptions plain;
    CspSolver base(p, plain);
    const size_t all = base.CountSolutions();
    CHECK(base.HashStable());      // Apply/Undo 가 서로의 역이다
    CHECK(all == 528);             // C9(1,2) 의 고유 4색 칠하기 528개 (전수 열거로 확인)

    // 가지치기를 켜도 해 *집합* 이 같아야 한다. 개수만 같은 것으로는 부족하다.
    std::vector<std::vector<int32_t>> base_set;
    CspSolver again(p, plain);
    again.CountSolutions(&base_set);
    std::sort(base_set.begin(), base_set.end());

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
        // 가지치기가 실제로 노드를 줄였는가. 등호를 허용하면 "켰는데 아무 일도
        // 일어나지 않음" 이 통과한다 — 각 팔이 단독으로 기준보다 적어야 한다.
        CHECK(solver.Stats().nodes < base.Stats().nodes);
    }
    // 2색으로는 해가 없다 (i, i+1, i+2 가 삼각형을 이룬다)
    CspProblem two = p;
    two.full_domain = 0b11;
    CspSolver none(two, plain);
    CHECK(none.CountSolutions() == 0);
    CHECK(none.HashStable());
}

// 대칭 깨기는 해를 잃지 않는다 — 잃는 것은 이름만 다른 같은 해다.
static void TestCspSymmetry() {
    CspProblem p;
    p.variables = 4;
    p.full_domain = 0b1111;
    for (uint32_t i = 0; i < 4; ++i)
        for (uint32_t j = i + 1; j < 4; ++j) p.not_equal.push_back({i, j});

    std::vector<std::vector<int32_t>> full;
    CspSolver plain(p, CspOptions{});
    plain.CountSolutions(&full);
    CHECK(full.size() == 24);      // 4! 개의 순열

    std::vector<std::vector<int32_t>> canonical;
    CspOptions sym;
    sym.symmetry_break = true;
    CspSolver broken(p, sym);
    broken.CountSolutions(&canonical);
    CHECK(canonical.size() == 1);  // 값 이름을 고정하면 해가 하나로 접힌다
    CHECK(broken.Stats().nodes < plain.Stats().nodes);

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
    std::sort(folded.begin(), folded.end());
    folded.erase(std::unique(folded.begin(), folded.end()), folded.end());
    std::sort(canonical.begin(), canonical.end());
    CHECK(folded == canonical);
}

// ===================================================================== step08

static void TestEntropyBound() {
    for (Corpus kind : {Corpus::Text, Corpus::StructuredBinary, Corpus::IntArray,
                        Corpus::HighlyRepetitive, Corpus::Random}) {
        const Bytes raw = measure::MakeCorpus(kind, 20000, 5u);
        const double h = Entropy0(raw);
        uint64_t freq[256] = {0};
        for (uint8_t b : raw) ++freq[b];
        const HuffmanLengths lengths = BuildHuffmanLengths(freq);
        bool exact = false;
        CHECK(KraftOk(lengths, &exact));
        CHECK(exact);          // 완전 이진 트리라 Kraft 합이 정확히 1 이다

        double bits = 0.0;
        for (int s = 0; s < 256; ++s) bits += double(freq[s]) * double(lengths.length[s]);
        const double avg = bits / double(raw.size());
        // H <= L < H + 1. 심볼 단위 부호의 이론 한계가 실측에서 성립한다.
        CHECK(avg >= h - 1e-9);
        CHECK(avg < h + 1.0 + 1e-9);
    }
}

static void TestHuffmanRoundTrip() {
    const Corpus kinds[] = {Corpus::Text, Corpus::StructuredBinary, Corpus::IntArray,
                            Corpus::FloatArray, Corpus::AlreadyCompressed,
                            Corpus::TinyMessage, Corpus::HighlyRepetitive, Corpus::Random};
    for (Corpus kind : kinds) {
        for (size_t size : {size_t(0), size_t(1), size_t(2), size_t(1000), size_t(30000)}) {
            const Bytes raw = measure::MakeCorpus(kind, size, 3u);
            const Bytes frame = HuffmanCompress(raw);
            Bytes back;
            CHECK(HuffmanDecompress(frame, &back));
            CHECK(back == raw);
        }
    }
    // 심볼이 하나뿐인 알파벳. 길이 0 코드를 주면 복호기가 전진하지 못한다.
    const Bytes same(5000, 'Z');
    Bytes back;
    CHECK(HuffmanDecompress(HuffmanCompress(same), &back));
    CHECK(back == same);
    // 난수 입력은 *커진다*. 무손실 압축의 셈 논증이 실측으로 나타나는 자리다.
    const Bytes noise = measure::MakeCorpus(Corpus::Random, 20000, 9u);
    CHECK(HuffmanCompress(noise).size() > noise.size());
}

static void TestCanonicalPrefixProperty() {
    measure::Rng rng(0xCAFEu);
    for (int trial = 0; trial < 40; ++trial) {
        uint64_t freq[256] = {0};
        const size_t alphabet = 2 + rng.Below(254);
        for (size_t s = 0; s < alphabet; ++s) freq[s] = 1 + rng.Below(1000);
        const HuffmanLengths lengths = BuildHuffmanLengths(freq);
        CHECK(KraftOk(lengths));
        CHECK(lengths.max_length <= 15);
        const CanonicalCode cc = AssignCanonical(lengths);
        // 접두 성질: 어떤 코드도 다른 코드의 접두가 아니다.
        for (int a = 0; a < 256; ++a) {
            if (!cc.length[a]) continue;
            for (int b = 0; b < 256; ++b) {
                if (a == b || !cc.length[b]) continue;
                if (cc.length[a] > cc.length[b]) continue;
                const uint32_t shifted = cc.code[b] >> (cc.length[b] - cc.length[a]);
                CHECK(shifted != cc.code[a]);
            }
        }
        // lookup 표가 모든 유효 접두를 정확히 덮는다.
        const DecodeTable table = BuildDecodeTable(cc);
        size_t covered = 0;
        for (size_t k = 0; k < table.symbol.size(); ++k)
            if (table.symbol[k] >= 0) ++covered;
        CHECK(covered == table.symbol.size());   // Kraft 합이 1 이므로 빈틈이 없다
    }
    // Kraft 를 깨는 길이표는 거부되어야 한다.
    HuffmanLengths bad;
    bad.symbols = 3;
    bad.max_length = 1;
    bad.length[0] = 1; bad.length[1] = 1; bad.length[2] = 1;   // 합 = 1.5 > 1
    CHECK(!KraftOk(bad));
}

static void TestLzRoundTripAndOverlap() {
    const ParseMode modes[] = {ParseMode::Greedy, ParseMode::Lazy, ParseMode::Optimal};
    const Corpus kinds[] = {Corpus::Text, Corpus::StructuredBinary, Corpus::IntArray,
                            Corpus::HighlyRepetitive, Corpus::TinyMessage, Corpus::Random};
    for (ParseMode mode : modes) {
        for (Corpus kind : kinds) {
            for (size_t size : {size_t(0), size_t(1), size_t(3), size_t(500), size_t(8000)}) {
                const Bytes raw = measure::MakeCorpus(kind, size, 4u);
                Bytes back;
                CHECK(LzDecode(LzEncode(raw, mode), &back));
                CHECK(back == raw);
            }
        }
    }
    // 겹침 복사. distance=1, length=200 은 memcpy 로 짜면 틀린다 — 원본이
    // 복사 도중 자라야 "aaaa..." 가 나온다.
    Bytes frame;
    PutU32(frame, FrameHeader::kMagic ^ 0x00010000u);
    frame.push_back(uint8_t(FrameHeader::kVersion));
    frame.push_back(0);
    const Bytes want(201, 'q');
    PutU32(frame, uint32_t(want.size()));
    PutU32(frame, Adler32(want.data(), want.size()));
    frame.push_back(0); frame.push_back('q');            // 리터럴 하나
    frame.push_back(1);
    frame.push_back(200); frame.push_back(0);            // length = 200
    frame.push_back(1); frame.push_back(0);              // distance = 1
    Bytes back;
    CHECK(LzDecode(frame, &back));
    CHECK(back == want);

    // 반복 데이터는 LZ 가 Huffman 을 크게 이긴다 — 모델의 차이가 결과의 차이다.
    const Bytes rep = measure::MakeCorpus(Corpus::HighlyRepetitive, 30000, 1u);
    CHECK(LzEncode(rep, ParseMode::Greedy).size() < HuffmanCompress(rep).size());
    // optimal 파싱이 greedy 보다 토큰을 더 쓰지 않는다.
    const Bytes text = measure::MakeCorpus(Corpus::Text, 20000, 1u);
    CHECK(LzParse(text, ParseMode::Optimal).size() <= LzParse(text, ParseMode::Greedy).size());
}

// 복호기를 신뢰할 수 없는 parser 로 다룬다.
static void TestDecoderRejectsHostileInput() {
    const Bytes raw = measure::MakeCorpus(Corpus::Text, 4000, 6u);
    const Bytes huff = HuffmanCompress(raw);
    const Bytes lz = LzEncode(raw, ParseMode::Greedy);
    Bytes out;

    // (1) 잘림. 모든 절단 위치에서 거부하거나 정확히 복원해야 한다 —
    //     "부분적으로 그럴듯한 출력" 은 없어야 한다.
    for (size_t cut = 0; cut < huff.size(); cut += 7) {
        const Bytes chopped(huff.begin(), huff.begin() + std::ptrdiff_t(cut));
        if (HuffmanDecompress(chopped, &out)) CHECK(out == raw);
        else CHECK(out.empty() || out.size() < raw.size());
    }
    for (size_t cut = 0; cut < lz.size(); cut += 11) {
        const Bytes chopped(lz.begin(), lz.begin() + std::ptrdiff_t(cut));
        if (LzDecode(chopped, &out)) CHECK(out == raw);
    }

    // (2) magic·version 손상
    Bytes bad_magic = huff;
    bad_magic[0] ^= 0xFF;
    CHECK(!HuffmanDecompress(bad_magic, &out));
    Bytes bad_version = huff;
    bad_version[4] = 99;
    CHECK(!HuffmanDecompress(bad_version, &out));

    // (3) 길이표 손상 — Kraft 검사가 잡는다
    Bytes bad_table = huff;
    for (size_t s = 0; s < 8; ++s) bad_table[14 + s] = 1;   // 코드 여덟 개가 전부 1비트
    CHECK(!HuffmanDecompress(bad_table, &out));

    // (4) checksum 손상 — 형식은 맞고 내용만 틀린 경우
    Bytes bad_sum = huff;
    bad_sum[10] ^= 0xFF;
    CHECK(!HuffmanDecompress(bad_sum, &out));
    CHECK(out.empty());

    // (5) LZ: 범위 밖 distance. 지금까지 낸 바이트보다 멀리 가리킨다.
    Bytes reach;
    PutU32(reach, FrameHeader::kMagic ^ 0x00010000u);
    reach.push_back(uint8_t(FrameHeader::kVersion));
    reach.push_back(0);
    PutU32(reach, 10);
    PutU32(reach, 12345);
    reach.push_back(1);
    reach.push_back(5); reach.push_back(0);
    reach.push_back(9); reach.push_back(0);     // distance 9 > 출력 0바이트
    CHECK(!LzDecode(reach, &out));

    // (6) LZ: 자원 상한. 선언한 원본 크기를 넘는 토큰.
    Bytes over;
    PutU32(over, FrameHeader::kMagic ^ 0x00010000u);
    over.push_back(uint8_t(FrameHeader::kVersion));
    over.push_back(0);
    PutU32(over, 4);
    PutU32(over, 1);
    for (int i = 0; i < 8; ++i) { over.push_back(0); over.push_back('z'); }
    CHECK(!LzDecode(over, &out));

    // (7) LZ: 알 수 없는 토큰 종류
    Bytes unknown = lz;
    unknown[14] = 7;
    CHECK(!LzDecode(unknown, &out));
}

static void TestRangeCoder() {
    const Corpus kinds[] = {Corpus::Text, Corpus::HighlyRepetitive, Corpus::IntArray,
                            Corpus::TinyMessage, Corpus::Random};
    for (Corpus kind : kinds) {
        for (size_t size : {size_t(0), size_t(1), size_t(400), size_t(9000)}) {
            const Bytes raw = measure::MakeCorpus(kind, size, 8u);
            Bytes back;
            CHECK(RangeDecompress(RangeCompress(raw), &back));
            CHECK(back == raw);
        }
    }
    // 극단적으로 치우친 알파벳에서 range coder 는 심볼당 1비트 하한을 넘어선다.
    Bytes skewed(40000, 'a');
    for (size_t i = 0; i < skewed.size(); i += 500) skewed[i] = 'b';
    const size_t huff_bits = HuffmanCompress(skewed).size() * 8;
    const size_t range_bits = RangeCompress(skewed).size() * 8;
    CHECK(range_bits < huff_bits);
    CHECK(double(huff_bits) / double(skewed.size()) >= 1.0);   // Huffman 은 못 넘는다
}

// ================================================================= property 층

static bool PropSortMultiset(const int64_t* p, prop::Observation& obs) {
    const size_t n = size_t(p[0]);
    const SortShape shape = SortShape(p[1]);
    const std::vector<Rec> input = measure::MakeSortInput(shape, n, uint64_t(p[2]));
    std::vector<Rec> intro = input, merge = input, radix = input;
    Introsort(intro, ByKey);
    MergeSortStable(merge, ByKey);
    RadixSortLsd(radix, int(p[3]));
    obs.interesting = (n >= 2);
    if (!obs.True("정렬됨(introsort)", oracle::NonDecreasing(intro))) return false;
    if (!obs.True("원소 보존(introsort)", oracle::SameMultiset(input, intro))) return false;
    if (!obs.True("원소 보존(radix)", oracle::SameMultiset(input, radix))) return false;
    if (!obs.True("안정성(merge)", oracle::Stable(merge))) return false;
    if (!obs.True("안정성(radix)", oracle::Stable(radix))) return false;
    // 두 안정 팔은 바이트 단위로 같은 배열을 낸다.
    for (size_t i = 0; i < n; ++i) {
        if (!obs.Equal("안정 팔 일치(key)", merge[i].key, radix[i].key)) return false;
        if (!obs.Equal("안정 팔 일치(tag)", merge[i].tag, radix[i].tag)) return false;
    }
    return true;
}

static bool PropSelectAgrees(const int64_t* p, prop::Observation& obs) {
    const size_t n = 1 + size_t(p[0]);
    const std::vector<Rec> input = measure::MakeSortInput(SortShape(p[1]), n, uint64_t(p[2]));
    const size_t k = size_t(p[3]) % n;
    obs.interesting = (n >= 8);
    const Rec want = oracle::NthByFullSort(input, k);
    if (!obs.Equal("quickselect", Quickselect(input, k, uint32_t(p[2]) | 1u).key, want.key))
        return false;
    if (!obs.Equal("median-of-medians", MedianOfMediansSelect(input, k).key, want.key))
        return false;
    return true;
}

static bool PropFenwickMatchesScan(const int64_t* p, prop::Observation& obs) {
    const size_t n = 1 + size_t(p[0]);
    measure::Rng rng(uint64_t(p[1]) | 1u);
    std::vector<int64_t> plain(n, 0);
    Fenwick fw(n);
    for (int round = 0; round < int(p[2]); ++round) {
        const size_t at = rng.Below(uint32_t(n));
        const int64_t delta = int64_t(rng.Below(50)) - 20;
        plain[at] += delta;
        fw.Add(at, delta);
    }
    obs.interesting = (n >= 4 && p[2] >= 4);
    for (size_t l = 0; l <= n; ++l) {
        for (size_t r = l; r <= n; ++r) {
            if (!obs.Equal("구간 합", fw.Range(l, r), oracle::SlowRangeSum(plain, l, r)))
                return false;
        }
    }
    return true;
}

static bool PropSegmentTreeLazy(const int64_t* p, prop::Observation& obs) {
    const size_t n = 1 + size_t(p[0]);
    measure::Rng rng(uint64_t(p[1]) | 1u);
    std::vector<int64_t> plain(n);
    for (size_t i = 0; i < n; ++i) plain[i] = int64_t(rng.Below(40)) - 20;
    SegmentTree tree(plain);
    obs.interesting = (n >= 4);
    for (int round = 0; round < int(p[2]); ++round) {
        const size_t l = rng.Below(uint32_t(n));
        const size_t r = l + rng.Below(uint32_t(n - l));
        if (rng.Below(2)) {
            const int64_t v = int64_t(rng.Below(30)) - 15;
            for (size_t i = l; i <= r; ++i) plain[i] = v;
            tree.Assign(l, r, v);
        } else {
            const int64_t v = int64_t(rng.Below(20)) - 10;
            for (size_t i = l; i <= r; ++i) plain[i] += v;
            tree.Add(l, r, v);
        }
        const size_t ql = rng.Below(uint32_t(n));
        const size_t qr = ql + rng.Below(uint32_t(n - ql));
        if (!obs.Equal("lazy 구간 합", tree.Sum(ql, qr),
                       oracle::SlowRangeSum(plain, ql, qr + 1)))
            return false;
    }
    return true;
}

static bool PropOpenLookup(const int64_t* p, prop::Observation& obs) {
    const size_t n = 1 + size_t(p[0]);
    const auto policy = DeletePolicy(p[1]);
    const std::vector<std::string> keys =
        measure::MakeKeys(measure::KeyShape(p[2]), n, uint64_t(p[3]) | 1u);
    OpenMap map(policy, 8);
    ChainMap chain(8);
    for (size_t i = 0; i < keys.size(); ++i) {
        map.Put(keys[i], int64_t(i));
        chain.Put(keys[i], int64_t(i));
    }
    for (size_t i = 0; i < keys.size(); i += 3) { map.Erase(keys[i]); chain.Erase(keys[i]); }
    obs.interesting = (n >= 16);
    if (!obs.True("조회 종료 불변식", map.ProbeChainIntact())) return false;
    for (size_t i = 0; i < keys.size(); ++i) {
        int64_t a = -1, b = -2;
        const bool ha = map.Get(keys[i], &a);
        const bool hb = chain.Get(keys[i], &b);
        if (!obs.Equal("존재 여부 일치", ha ? 1 : 0, hb ? 1 : 0)) return false;
        if (ha && !obs.Equal("값 일치", a, b)) return false;
    }
    return true;
}

static bool PropSearchAgrees(const int64_t* p, prop::Observation& obs) {
    measure::Rng rng(uint64_t(p[0]) | 1u);
    const size_t alphabet = 1 + size_t(p[1]);
    std::string text, pat;
    for (size_t i = 0; i < size_t(p[2]); ++i) text.push_back(char('a' + rng.Below(uint32_t(alphabet))));
    for (size_t i = 0; i < size_t(p[3]); ++i) pat.push_back(char('a' + rng.Below(uint32_t(alphabet))));
    const std::vector<size_t> want = oracle::SlowSearch(text, pat);
    obs.interesting = !want.empty();
    if (!obs.Equal("KMP 일치 수", int64_t(KmpSearch(text, pat).size()), int64_t(want.size())))
        return false;
    if (!obs.True("KMP 위치", KmpSearch(text, pat) == want)) return false;
    if (!obs.True("Z 위치", ZSearch(text, pat) == want)) return false;
    if (!obs.True("BMH 위치", BmhSearch(text, pat) == want)) return false;
    RollingHash rk;
    if (!obs.True("rolling hash 위치", rk.Search(text, pat, true) == want)) return false;
    // Aho-Corasick 을 패턴 하나로 돌려도 같아야 한다.
    if (!pat.empty()) {
        AhoCorasick ac;
        ac.Add(pat);
        ac.Build();
        std::vector<size_t> starts;
        for (const auto& hit : ac.Search(text)) starts.push_back(hit.end_pos + 1 - pat.size());
        if (!obs.True("Aho 위치", starts == want)) return false;
    }
    return true;
}

static bool PropGraphStructure(const int64_t* p, prop::Observation& obs) {
    const size_t n = 2 + size_t(p[0]);
    measure::Rng rng(uint64_t(p[1]) | 1u);
    std::vector<Csr::Edge> undirected, directed;
    for (uint32_t u = 0; u < n; ++u)
        for (uint32_t v = 0; v < n; ++v) {
            if (u == v) continue;
            if (rng.Below(100) >= uint32_t(p[2])) continue;
            directed.push_back({u, v, int64_t(rng.Below(9)) + 1});
            if (u < v) undirected.push_back({u, v, int64_t(rng.Below(9)) + 1});
        }
    obs.interesting = (undirected.size() >= 2);

    Csr dg;
    dg.Build(n, directed, false);
    const SccResult tar = SccTarjan(dg), kos = SccKosaraju(dg);
    if (!obs.Equal("SCC 개수", int64_t(tar.count), int64_t(kos.count))) return false;
    for (uint32_t u = 0; u < n; ++u)
        for (uint32_t v = 0; v < n; ++v)
            if (!obs.Equal("SCC 분할 일치", (tar.comp[u] == tar.comp[v]) ? 1 : 0,
                           (kos.comp[u] == kos.comp[v]) ? 1 : 0))
                return false;
    // condensation 은 DAG 다
    std::vector<Csr::Edge> cond;
    for (uint32_t u = 0; u < n; ++u)
        for (size_t i = dg.start[u]; i < dg.start[u + 1]; ++i)
            if (tar.comp[u] != tar.comp[dg.to[i]])
                cond.push_back({tar.comp[u], tar.comp[dg.to[i]], 1});
    Csr cg;
    cg.Build(tar.count, cond, false);
    if (!obs.True("condensation 은 DAG", !HasCycleDirected(cg))) return false;

    // 절단점·다리를 정의대로 다시 계산한 답과 대조
    const Csr ug = BuildUndirected(n, undirected);
    const CutResult cut = ArticulationAndBridges(ug);
    if (!obs.True("절단점", cut.articulation == oracle::SlowArticulation(n, undirected)))
        return false;
    if (!obs.True("다리", cut.bridges == oracle::SlowBridges(n, undirected))) return false;

    // DSU 의 연결 판정이 BFS 성분과 같다
    Dsu dsu(n, p[3] != 0, p[3] > 1);
    for (const Csr::Edge& e : undirected) dsu.Union(e.u, e.v);
    const std::vector<uint32_t> comp = oracle::ComponentIdByBfs(n, undirected);
    for (uint32_t u = 0; u < n; ++u)
        for (uint32_t v = 0; v < n; ++v)
            if (!obs.Equal("DSU 연결", dsu.Connected(u, v) ? 1 : 0,
                           (comp[u] == comp[v]) ? 1 : 0))
                return false;
    return true;
}

static bool PropShortestPath(const int64_t* p, prop::Observation& obs) {
    const size_t n = 2 + size_t(p[0]);
    measure::Rng rng(uint64_t(p[1]) | 1u);
    std::vector<Csr::Edge> edges;
    for (uint32_t u = 0; u < n; ++u)
        for (uint32_t v = 0; v < n; ++v) {
            if (u == v || rng.Below(100) >= uint32_t(p[2])) continue;
            edges.push_back({u, v, int64_t(rng.Below(uint32_t(p[3]))) + 1});
        }
    Csr g;
    g.Build(n, edges, false);
    obs.interesting = (edges.size() >= n);
    const auto floyd = oracle::SlowAllPairs(n, edges, false);
    for (uint32_t s = 0; s < n; ++s) {
        const PathResult dij = DijkstraHeap(g, s);
        const PathResult dense = DijkstraDense(g, s);
        const PathResult bf = BellmanFord(n, edges, s);
        for (uint32_t v = 0; v < n; ++v) {
            if (!obs.Equal("Dijkstra vs Floyd", dij.dist[v], floyd[s][v])) return false;
            if (!obs.Equal("dense vs Floyd", dense.dist[v], floyd[s][v])) return false;
            if (!obs.Equal("Bellman-Ford vs Floyd", bf.dist[v], floyd[s][v])) return false;
        }
        if (!obs.True("음수 순환 없음", !bf.negative_cycle)) return false;
    }
    return true;
}

static bool PropKnapsackVsBrute(const int64_t* p, prop::Observation& obs) {
    const size_t n = 1 + size_t(p[0]);
    measure::Rng rng(uint64_t(p[1]) | 1u);
    std::vector<Item> items;
    for (size_t i = 0; i < n; ++i)
        items.push_back(Item{int64_t(rng.Below(uint32_t(p[2]))) + 1,
                             int64_t(rng.Below(50)) + 1});
    const int64_t capacity = p[3];
    obs.interesting = (n >= 5);
    const int64_t brute = oracle::BruteKnapsack(items, capacity);
    if (!obs.Equal("2D DP", Knapsack2D(items, capacity).value, brute)) return false;
    if (!obs.Equal("1D DP", Knapsack1D(items, capacity, true), brute)) return false;
    if (!obs.Equal("분기 한정", BranchAndBoundKnapsack(items, capacity).best, brute))
        return false;
    if (!obs.True("탐욕 <= 최적", DensityGreedyKnapsack(items, capacity) <= brute))
        return false;
    // metamorphic: 값 전체를 3배 하면 최적값도 3배
    std::vector<Item> scaled = items;
    for (Item& it : scaled) it.value *= 3;
    if (!obs.Equal("값 배율", Knapsack2D(scaled, capacity).value, brute * 3)) return false;
    return true;
}

static bool PropFlowMinCut(const int64_t* p, prop::Observation& obs) {
    const size_t n = 3 + size_t(p[0]);
    measure::Rng rng(uint64_t(p[1]) | 1u);
    Residual a(n), b(n), c(n);
    size_t arcs = 0;
    for (uint32_t u = 0; u + 1 < n; ++u)
        for (uint32_t v = u + 1; v < n; ++v) {
            if (rng.Below(100) >= uint32_t(p[2])) continue;
            const int64_t cap = int64_t(rng.Below(20)) + 1;
            a.AddEdge(u, v, cap);
            b.AddEdge(u, v, cap);
            c.AddEdge(u, v, cap);
            ++arcs;
        }
    const uint32_t s = 0, t = uint32_t(n - 1);
    obs.interesting = (arcs >= n);
    const FlowResult ff = FordFulkerson(a, s, t);
    const FlowResult ek = EdmondsKarp(b, s, t);
    Dinic dinic;
    const FlowResult dn = dinic.Run(c, s, t);
    if (!obs.Equal("FF vs EK", ff.value, ek.value)) return false;
    if (!obs.Equal("EK vs Dinic", ek.value, dn.value)) return false;
    for (const Residual* g : {&a, &b, &c}) {
        if (!obs.True("용량 준수", g->CapacityRespected())) return false;
        if (!obs.True("유량 보존", g->Conserves(s, t))) return false;
        if (!obs.Equal("원천 순유출", g->NetOutOf(s), ff.value)) return false;
        const MinCut cut = ExtractMinCut(*g, s);
        if (!obs.True("증가 경로 없음", !cut.source_side[t])) return false;
        if (!obs.Equal("max-flow == min-cut", cut.capacity, ff.value)) return false;
    }
    return true;
}

static bool PropCompressRoundTrip(const int64_t* p, prop::Observation& obs) {
    const Corpus kind = Corpus(p[0]);
    const size_t size = size_t(p[1]);
    const Bytes raw = measure::MakeCorpus(kind, size, uint64_t(p[2]) | 1u);
    obs.interesting = (raw.size() >= 64);
    Bytes back;
    if (!obs.True("Huffman 왕복", HuffmanDecompress(HuffmanCompress(raw), &back))) return false;
    if (!obs.True("Huffman 동일", back == raw)) return false;
    if (!obs.True("range 왕복", RangeDecompress(RangeCompress(raw), &back))) return false;
    if (!obs.True("range 동일", back == raw)) return false;
    const ParseMode mode = ParseMode(p[3] % 3);
    if (!obs.True("LZ 왕복", LzDecode(LzEncode(raw, mode), &back))) return false;
    if (!obs.True("LZ 동일", back == raw)) return false;
    // 한 바이트를 손상시키면 복호가 거부하거나 다른 결과를 낸다 — 조용히 같을 수 없다.
    Bytes frame = HuffmanCompress(raw);
    if (raw.size() >= 32) {
        frame[10] ^= 0x40;                       // checksum 손상
        Bytes tampered;
        if (!obs.True("손상 거부", !HuffmanDecompress(frame, &tampered))) return false;
    }
    return true;
}

// -------------------------------------------------------------- property 표

static const prop::ParamSpec kSortParams[] = {
    {"n", 0, 400, 0}, {"shape", 0, 9, 0}, {"seed", 1, 1000000, 1}, {"radix_bits", 4, 11, 8},
};
static const prop::ParamSpec kSelectParams[] = {
    {"n", 0, 300, 0}, {"shape", 0, 9, 0}, {"seed", 1, 1000000, 1}, {"k", 0, 1000, 0},
};
static const prop::ParamSpec kFenwickParams[] = {
    {"n", 0, 60, 0}, {"seed", 1, 1000000, 1}, {"rounds", 0, 120, 0},
};
static const prop::ParamSpec kSegmentParams[] = {
    {"n", 0, 50, 0}, {"seed", 1, 1000000, 1}, {"rounds", 1, 80, 1},
};
static const prop::ParamSpec kDictParams[] = {
    {"n", 0, 300, 0}, {"policy", 0, 2, 0}, {"shape", 0, 3, 0}, {"seed", 1, 1000000, 1},
};
static const prop::ParamSpec kTextParams[] = {
    {"seed", 1, 1000000, 1}, {"alphabet", 0, 5, 0}, {"text_len", 0, 300, 0},
    {"pat_len", 0, 8, 1},
};
static const prop::ParamSpec kGraphParams[] = {
    {"n", 0, 8, 0}, {"seed", 1, 1000000, 1}, {"density", 0, 70, 30}, {"dsu_mode", 0, 2, 2},
};
static const prop::ParamSpec kPathParams[] = {
    {"n", 0, 8, 0}, {"seed", 1, 1000000, 1}, {"density", 0, 80, 40}, {"maxw", 1, 20, 1},
};
static const prop::ParamSpec kKnapsackParams[] = {
    {"n", 0, 11, 0}, {"seed", 1, 1000000, 1}, {"maxw", 1, 20, 1}, {"capacity", 1, 60, 1},
};
static const prop::ParamSpec kFlowParams[] = {
    {"n", 0, 7, 0}, {"seed", 1, 1000000, 1}, {"density", 10, 90, 50},
};
static const prop::ParamSpec kCompressParams[] = {
    {"corpus", 0, 7, 0}, {"size", 0, 4000, 0}, {"seed", 1, 1000000, 1}, {"parse", 0, 2, 0},
};

static const prop::Property kProperties[] = {
    {"01", "order/sort-contract", kSortParams, 4, PropSortMultiset, 900, 40,
     "정렬 결과는 순서 · multiset · (안정 팔은) 안정성을 동시에 지킨다"},
    {"01", "order/select-agrees", kSelectParams, 4, PropSelectAgrees, 900, 40,
     "quickselect 와 median-of-medians 가 전체 정렬의 k 번째와 같은 키를 낸다"},
    {"02", "range/fenwick-vs-scan", kFenwickParams, 3, PropFenwickMatchesScan, 450, 30,
     "Fenwick 의 모든 구간 합이 선형 스캔과 같다"},
    {"02", "range/segment-lazy", kSegmentParams, 3, PropSegmentTreeLazy, 700, 40,
     "대입·덧셈이 섞인 갱신 열 뒤에도 구간 합이 직접 계산과 같다"},
    {"03", "dict/open-lookup", kDictParams, 4, PropOpenLookup, 500, 30,
     "삭제 정책 셋이 chaining 과 같은 존재 여부·값을 내고 조회 종료 불변식을 지킨다"},
    {"03", "text/search-agrees", kTextParams, 4, PropSearchAgrees, 1400, 25,
     "다섯 탐색 팔이 naive 와 같은 위치 목록을 낸다"},
    {"04", "graph/structure-agrees", kGraphParams, 4, PropGraphStructure, 420, 40,
     "SCC 두 팔의 분할이 같고, condensation 이 DAG 이며, 절단점·다리·DSU 가 정의와 일치한다"},
    {"05", "paths/vs-floyd", kPathParams, 4, PropShortestPath, 420, 40,
     "Dijkstra 두 팔과 Bellman-Ford 가 Floyd-Warshall 과 같은 거리를 낸다"},
    {"06", "dp/knapsack-vs-brute", kKnapsackParams, 4, PropKnapsackVsBrute, 500, 40,
     "DP 두 판과 분기 한정이 전수 열거 최적값과 같고 탐욕은 그 이하다"},
    {"07", "flow/maxflow-mincut", kFlowParams, 3, PropFlowMinCut, 420, 40,
     "세 유량 팔이 같은 값을 내고 그 값이 잔여 도달 집합의 절단 용량과 같다"},
    {"08", "compress/roundtrip", kCompressParams, 4, PropCompressRoundTrip, 340, 40,
     "세 코덱이 어떤 코퍼스에서도 왕복하고 손상된 프레임을 거부한다"},
};

// ===================================================================== 실행부

struct Case {
    const char* step;
    const char* name;
    void (*fn)();
};

static const Case kCases[] = {
    {"01", "order_search_bounds",          TestSearchBounds},
    {"01", "order_sort_contract",          TestSortContract},
    {"01", "order_ordered_key",            TestOrderedKey},
    {"01", "order_selection",              TestSelection},
    {"02", "range_prefix_and_difference",  TestPrefixAndDifference},
    {"02", "range_sliding_and_monotonic",  TestSlidingAndMonotonic},
    {"02", "range_fenwick",                TestFenwick},
    {"02", "range_segment_tree_algebra",   TestSegmentTreeAlgebra},
    {"02", "range_max_subarray_tree",      TestMaxSubarrayTree},
    {"02", "range_priority_queue_arms",    TestPriorityQueueArms},
    {"03", "dict_open_addressing",         TestOpenAddressing},
    {"03", "dict_bloom_filter",            TestBloomFilter},
    {"03", "text_single_pattern",          TestSinglePatternSearch},
    {"03", "text_rolling_hash_verify",     TestRollingHashVerification},
    {"03", "text_aho_corasick",            TestAhoCorasick},
    {"03", "text_trie_arms",               TestTrieArms},
    {"04", "graph_bfs_dfs_invariants",     TestBfsDfsInvariants},
    {"04", "graph_cycle_and_topo",         TestCycleAndTopo},
    {"04", "graph_scc_arms",               TestScc},
    {"04", "graph_articulation_bridges",   TestArticulationAndBridges},
    {"04", "graph_dsu_arms",               TestDsuArms},
    {"04", "graph_mst_arms",               TestMst},
    {"05", "paths_arms_agree",             TestShortestPathArms},
    {"05", "paths_negative_edge",          TestNegativeEdgeBreaksDijkstra},
    {"05", "paths_overflow_guard",         TestOverflowGuard},
    {"05", "paths_astar_grid",             TestAStarGrid},
    {"05", "paths_inconsistent_heuristic", TestInconsistentHeuristic},
    {"06", "greedy_exchange_holds",        TestGreedyExchangeHolds},
    {"06", "greedy_exchange_breaks",       TestGreedyExchangeBreaks},
    {"06", "dp_knapsack_arms",             TestDpKnapsackArms},
    {"06", "dp_direction_changes_problem", TestDpDirectionChangesProblem},
    {"06", "dp_strings",                   TestDpStrings},
    {"06", "dp_sequences_and_structures",  TestDpSequencesAndStructures},
    {"06", "dp_metamorphic",               TestDpMetamorphic},
    {"07", "flow_maxflow_arms",            TestMaxFlowArms},
    {"07", "flow_matching_arms",           TestMatchingArms},
    {"07", "flow_min_cost_assignment",     TestMinCostAndAssignment},
    {"07", "csp_apply_undo",               TestCspApplyUndo},
    {"07", "csp_symmetry_breaking",        TestCspSymmetry},
    {"08", "compress_entropy_bound",       TestEntropyBound},
    {"08", "compress_huffman_roundtrip",   TestHuffmanRoundTrip},
    {"08", "compress_canonical_prefix",    TestCanonicalPrefixProperty},
    {"08", "compress_lz_roundtrip",        TestLzRoundTripAndOverlap},
    {"08", "compress_decoder_hostile",     TestDecoderRejectsHostileInput},
    {"08", "compress_range_coder",         TestRangeCoder},
};

static int RunProperties(int argc, char** argv) {
    prop::RunConfig config;
    std::string only, step;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--only=", 0) == 0) only = arg.substr(7);
        else if (arg.rfind("--seed=", 0) == 0) config.master_seed = std::strtoull(arg.c_str() + 7, nullptr, 10);
        else if (arg.rfind("--sample=", 0) == 0) config.only_sample = std::atoi(arg.c_str() + 9);
        else if (arg.rfind("--scale=", 0) == 0) config.scale = std::atoi(arg.c_str() + 8);
        else if (arg.rfind("--step=", 0) == 0) step = arg.substr(7);
    }

    int selected = 0, failed = 0;
    long long total_samples = 0;
    for (const prop::Property& p : kProperties) {
        if (!only.empty() && only != p.name) continue;
        if (!step.empty() && step != p.step) continue;
        ++selected;
        const prop::RunResult r = prop::Run(p, config);
        total_samples += r.samples;
        const int judged = r.samples - r.skipped;
        const int interest_pct = judged ? (r.interesting * 100 / judged) : 0;
        if (r.failed) {
            std::printf("  FAIL %-28s 표본 %d\n", p.name, r.samples);
            prop::ReportFailure(p, r, config, "algo_tests");
            ++failed;
            continue;
        }
        if (config.only_sample < 0 && interest_pct < p.min_interest_pct) {   // 비퇴화 하한은 표본 집단의 판정 — --sample 재현(표본 1개)에는 걸지 않는다
            // 자명하게 통과하는 표본만 모이면 property 는 아무것도 강제하지 않는다.
            std::printf("  FAIL %-28s 비퇴화 표본 %d%% < 하한 %d%%\n",
                        p.name, interest_pct, p.min_interest_pct);
            ++failed;
            continue;
        }
        std::printf("  ok   %-28s 표본 %5d · 비퇴화 %3d%% · 건너뜀 %d\n",
                    p.name, r.samples, interest_pct, r.skipped);
    }
    if (selected == 0) {
        std::printf("algo_tests: property 필터에 맞는 사례가 없습니다\n");
        return 2;
    }
    if (failed) {
        std::printf("algo_tests[properties]: %d/%d 실패\n", failed, selected);
        return 1;
    }
    std::printf("algo_tests[properties]: %d/%d passed (총 %lld 표본, seed %llu)\n",
                selected, selected, total_samples,
                (unsigned long long)config.master_seed);
    return 0;
}

int main(int argc, char** argv) {
    std::string step;
    bool properties = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--properties") properties = true;
        else if (arg == "--list") {
            for (const Case& c : kCases) std::printf("%s %s\n", c.step, c.name);
            for (const prop::Property& p : kProperties)
                std::printf("%s %s (property)\n", p.step, p.name);
            return 0;
        } else if (arg.rfind("--step=", 0) == 0) step = arg.substr(7);
        else if (arg.rfind("--", 0) != 0) {
            std::printf("usage: algo_tests [--step=NN] [--list] [--properties [--only=NAME]"
                        " [--seed=N] [--sample=K] [--scale=N]]\n");
            return 2;
        }
    }
    if (properties) return RunProperties(argc, argv);

    int selected = 0;
    for (const Case& c : kCases) {
        if (!step.empty() && step != c.step) continue;
        ++selected;
        const int before = g_failed;
        c.fn();
        if (!step.empty())
            std::printf("  %-4s %s\n", (g_failed == before) ? "ok" : "FAIL", c.name);
    }
    if (!step.empty() && selected == 0) {
        // 이름이 틀린 필터가 '0개 실행 = 성공' 으로 지나가면 안 된다.
        std::printf("algo_tests: no case belongs to step%s\n", step.c_str());
        return 2;
    }
    if (g_failed != 0) {
        std::printf("algo_tests: %d check(s) failed\n", g_failed);
        return 1;
    }
    if (step.empty())
        std::printf("algo_tests: all passed (%d case(s), %d check(s))\n",
                    selected, g_checks);
    else
        std::printf("algo_tests[step%s]: %d/%d passed (%d check(s))\n",
                    step.c_str(), selected, selected, g_checks);
    return 0;
}
