// algo_bench — A/B 팔 측정 러너.
//
//   algo_bench                  전부
//   algo_bench --sort           정렬 다섯 팔
//   algo_bench --select         선택 네 팔
//   algo_bench --queue          우선순위 큐 · 구간 질의 팔
//   algo_bench --dict           해시 표 네 팔 x load factor
//   algo_bench --text           문자열 탐색 팔
//   algo_bench --graph          SCC · MST · DSU 팔
//   algo_bench --paths          최단 경로 · A* 팔
//   algo_bench --dp             배낭 · 분기 한정 팔
//   algo_bench --flow           유량 · 매칭 팔
//   algo_bench --compress       압축 코퍼스 8종 x 코덱 팔
//   algo_bench --smoke          표본을 줄인 축소 실행 (ctest 등록용)
//
// --smoke 는 *측정용이 아니다*. 표본을 줄여 모든 팔의 실패 검사 경로를 매 ctest
// 실행에서 한 번씩 지나가게 하는 것이 목적이다. 벤치가 스스로 답을 검사하지 않으면
// "빠르지만 틀린" 팔이 표에서 1등을 한다.
#include <algorithm>
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
#include "measure/counters.hpp"
#include "measure/workload.hpp"

using namespace algo;
using measure::Corpus;
using measure::Counters;
using measure::Samples;
using measure::SortShape;
using measure::Timer;

static bool g_smoke = false;
static uint64_t g_sink = 0;

// 벤치의 실패 검사. 답이 틀린 팔은 표에 오르기 전에 프로세스를 죽인다.
static void Die(const char* what) {
    std::printf("\nBENCH FAILED: %s\n", what);
    std::exit(1);
}

static void Rule(const char* title) {
    std::printf("\n=== %s ===\n", title);
}

// ------------------------------------------------------------------- 정렬

static void BenchSort() {
    Rule("정렬 5팔 — 같은 입력 복사본, 비교 횟수 · 이동 바이트 · 지연 분포");
    const SortShape shapes[] = {SortShape::Random, SortShape::Sorted, SortShape::Reversed,
                                SortShape::AllEqual, SortShape::OrganPipe,
                                SortShape::HighDigitSame, SortShape::FewKeys};
    const size_t n = g_smoke ? 4000 : 200000;
    const int reps = g_smoke ? 3 : 25;
    std::printf("%-16s %-11s %10s %12s %9s %9s %9s %9s\n",
                "shape", "arm", "compares", "moved_KiB", "P50_ms", "P95_ms", "P99_ms", "peak_KiB");
    for (SortShape shape : shapes) {
        const std::vector<Rec> input = measure::MakeSortInput(shape, n, 0x51D5077u);
        const char* arms[] = {"introsort", "mergesort", "counting", "radix8", "radix11"};
        for (int arm = 0; arm < 5; ++arm) {
            // counting sort 는 키 범위가 좁을 때만 의미가 있다. 넓은 입력에서는
            // 히스토그램이 2^32 칸이 되므로 아예 돌리지 않고 표에 '-' 를 남긴다.
            uint32_t span = 0;
            for (const Rec& r : input) span = (r.key > span) ? r.key : span;
            if (arm == 2 && span > 1u << 20) {
                std::printf("%-16s %-11s %10s %12s %9s %9s %9s %9s\n",
                            measure::SortShapeName(shape), arms[arm], "-", "-", "-", "-", "-", "-");
                continue;
            }
            Counters total;
            Samples ms;
            for (int rep = 0; rep < reps; ++rep) {
                std::vector<Rec> work = input;
                Counters c;
                Timer t;
                switch (arm) {
                    case 0: Introsort(work, ByKey, &c); break;
                    case 1: MergeSortStable(work, ByKey, &c); break;
                    case 2: CountingSort(work, span, &c); break;
                    case 3: RadixSortLsd(work, 8, &c); break;
                    case 4: RadixSortLsd(work, 11, &c); break;
                }
                ms.Add(t.Ms());
                // 실패 검사. 정렬되지 않았거나 원소를 잃었으면 여기서 죽는다.
                for (size_t i = 1; i < work.size(); ++i)
                    if (work[i - 1].key > work[i].key) Die("정렬 결과가 순서를 지키지 않는다");
                if (work.size() != input.size()) Die("원소 수가 달라졌다");
                for (const Rec& r : work) c.Consume(r.key);
                total = c;
            }
            g_sink ^= total.checksum;
            std::printf("%-16s %-11s %10llu %12.1f %9.3f %9.3f %9.3f %9llu\n",
                        measure::SortShapeName(shape), arms[arm],
                        (unsigned long long)total.comparisons,
                        double(total.moved_bytes) / 1024.0,
                        ms.Percentile(50), ms.Percentile(95), ms.Percentile(99),
                        (unsigned long long)(total.peak_bytes / 1024));
        }
    }

    Rule("밀도 스윕 — n/k 가 분포 정렬의 판단 입력이다");
    std::printf("%-10s %-10s %12s %12s %12s\n", "n", "k(키 종류)", "radix8_ms", "counting_ms", "introsort_ms");
    const size_t sweep_n = g_smoke ? 4000 : 200000;
    for (uint32_t k : {uint32_t(4), uint32_t(64), uint32_t(4096), uint32_t(262144)}) {
        measure::Rng rng(0xD35170u);
        std::vector<Rec> base(sweep_n);
        for (size_t i = 0; i < sweep_n; ++i) {
            base[i].key = rng.Below(k);
            base[i].tag = uint32_t(i);
        }
        double t_radix = 0, t_count = 0, t_intro = 0;
        const int rounds = g_smoke ? 2 : 8;
        for (int rep = 0; rep < rounds; ++rep) {
            { std::vector<Rec> w = base; Timer t; RadixSortLsd(w, 8); t_radix += t.Ms(); g_sink ^= w[0].key; }
            { std::vector<Rec> w = base; Timer t; CountingSort(w, k); t_count += t.Ms(); g_sink ^= w[0].key; }
            { std::vector<Rec> w = base; Timer t; Introsort(w, ByKey); t_intro += t.Ms(); g_sink ^= w[0].key; }
        }
        std::printf("%-10zu %-10u %12.3f %12.3f %12.3f\n", sweep_n, k,
                    t_radix / rounds, t_count / rounds, t_intro / rounds);
    }
}

// ------------------------------------------------------------------- 선택

static void BenchSelect() {
    Rule("선택 4팔 — k/n 스윕. 교차점은 점근이 아니라 상수가 정한다");
    const size_t n = g_smoke ? 5000 : 400000;
    const std::vector<Rec> input = measure::MakeSortInput(SortShape::Random, n, 0x5E1EC7u);
    const std::vector<Rec> hostile = measure::MakeSortInput(SortShape::OrganPipe, n, 1u);
    std::printf("%-12s %-8s %-18s %10s %14s\n", "input", "k/n", "arm", "ms", "compares");
    const double fractions[] = {0.001, 0.05, 0.5};
    for (int hostile_input = 0; hostile_input < 2; ++hostile_input) {
        const std::vector<Rec>& data = hostile_input ? hostile : input;
        for (double f : fractions) {
            const size_t k = size_t(double(n) * f);
            struct Arm { const char* name; };
            for (int arm = 0; arm < 4; ++arm) {
                Counters c;
                Timer t;
                uint32_t key = 0;
                switch (arm) {
                    case 0: key = Quickselect(data, k, 0x1234u, &c).key; break;
                    case 1: key = MedianOfMediansSelect(data, k, &c).key; break;
                    case 2: {
                        // 크기 m min-heap 은 *상위 m 개* 를 답한다. k 번째로 작은 것을
                        // 얻으려면 m = n-k 를 유지해야 하므로, k 가 작을수록 이 팔이
                        // 하는 일이 늘어난다 — 이 팔은 k 가 n 에 가까울 때 이긴다.
                        const std::vector<Rec> top = TopKMinHeap(data, n - k, &c);
                        key = top.empty() ? 0 : top.back().key;
                        break;
                    }
                    default: {
                        std::vector<Rec> w = data;
                        Introsort(w, ByKey, &c);
                        key = w[k].key;
                        break;
                    }
                }
                const double ms = t.Ms();
                // 실패 검사. 네 팔이 같은 키를 내야 한다.
                static uint32_t reference = 0;
                if (arm == 0) reference = key;
                else if (key != reference) Die("선택 팔이 서로 다른 k 번째를 냈다");
                g_sink ^= key;
                const char* names[4] = {"quickselect", "median-of-medians", "size-(n-k) heap", "full sort"};
                std::printf("%-12s %-8.3f %-18s %10.3f %14llu\n",
                            hostile_input ? "organ-pipe" : "random", f, names[arm], ms,
                            (unsigned long long)c.comparisons);
            }
        }
    }
}

// -------------------------------------------------------- 큐 · 구간 질의

static void BenchQueue() {
    Rule("우선순위 큐 2팔 — 이진 힙 vs 정렬 배열");
    const size_t n = g_smoke ? 2000 : 200000;
    std::printf("%-18s %10s %14s %12s\n", "arm", "ms", "compares", "moved_KiB");
    {
        measure::Rng rng(0x9EE9u);
        std::vector<int64_t> keys(n);
        for (size_t i = 0; i < n; ++i) keys[i] = int64_t(rng.Below(1000000));
        int64_t last = -1;
        {
            BinaryHeap<uint32_t> h;
            Counters c;
            Timer t;
            for (size_t i = 0; i < n; ++i) h.Push(keys[i], uint32_t(i), 0, &c);
            while (!h.Empty()) { const auto top = h.Pop(&c); if (top.key < last) Die("힙이 순서를 깼다"); last = top.key; }
            std::printf("%-18s %10.3f %14llu %12.1f\n", "binary heap", t.Ms(),
                        (unsigned long long)c.comparisons, double(c.moved_bytes) / 1024.0);
        }
        const size_t small = n / 100 + 1;   // 정렬 배열은 삽입이 O(n) 이라 규모를 줄인다
        {
            SortedArrayQueue<uint32_t> q;
            Counters c;
            Timer t;
            for (size_t i = 0; i < small; ++i) q.Push(keys[i], uint32_t(i), 0, &c);
            last = -1;
            while (!q.Empty()) { const auto top = q.Pop(&c); if (top.key < last) Die("정렬 배열이 순서를 깼다"); last = top.key; }
            std::printf("%-18s %10.3f %14llu %12.1f  (n=%zu)\n", "sorted array", t.Ms(),
                        (unsigned long long)c.comparisons, double(c.moved_bytes) / 1024.0, small);
        }
    }

    Rule("구간 질의 3팔 — 질의/갱신 비율 스윕. 선형 스캔이 이기는 구간이 있다");
    std::printf("%-8s %-10s %12s %12s %12s\n", "n", "질의비율", "naive_ms", "fenwick_ms", "segtree_ms");
    const int ops = g_smoke ? 2000 : 200000;
    for (size_t n2 : {size_t(16), size_t(64), size_t(1024), size_t(65536)}) {
        for (int query_pct : {10, 90}) {
            measure::Rng rng(0x4A11u);
            std::vector<int64_t> plain(n2, 0);
            Fenwick fw(n2);
            SegmentTree seg(std::vector<int64_t>(n2, 0));
            int64_t sink = 0;
            // size_t(ops) 를 생성자 인자로 그대로 쓰면 함수 선언으로 파싱된다.
            const size_t op_count = size_t(ops);
            std::vector<uint32_t> at(op_count), lo(op_count), hi(op_count);
            std::vector<int> kind(op_count);
            for (int i = 0; i < ops; ++i) {
                kind[size_t(i)] = (int(rng.Below(100)) < query_pct) ? 1 : 0;
                at[size_t(i)] = rng.Below(uint32_t(n2));
                lo[size_t(i)] = rng.Below(uint32_t(n2));
                hi[size_t(i)] = lo[size_t(i)] + rng.Below(uint32_t(n2) - lo[size_t(i)]);
            }
            double t_naive, t_fen, t_seg;
            {
                Timer t;
                for (int i = 0; i < ops; ++i) {
                    if (kind[size_t(i)]) {
                        int64_t s = 0;
                        for (size_t j = lo[size_t(i)]; j <= hi[size_t(i)]; ++j) s += plain[j];
                        sink += s;
                    } else plain[at[size_t(i)]] += 1;
                }
                t_naive = t.Ms();
            }
            {
                Timer t;
                for (int i = 0; i < ops; ++i) {
                    if (kind[size_t(i)]) sink += fw.Range(lo[size_t(i)], hi[size_t(i)] + 1);
                    else fw.Add(at[size_t(i)], 1);
                }
                t_fen = t.Ms();
            }
            {
                Timer t;
                for (int i = 0; i < ops; ++i) {
                    if (kind[size_t(i)]) sink += seg.Sum(lo[size_t(i)], hi[size_t(i)]);
                    else seg.Add(at[size_t(i)], at[size_t(i)], 1);
                }
                t_seg = t.Ms();
            }
            // 실패 검사: 세 자료구조가 같은 배열 상태를 들고 있어야 한다.
            for (size_t j = 0; j < n2; ++j)
                if (fw.Range(j, j + 1) != plain[j] || seg.At(j) != plain[j])
                    Die("구간 질의 팔의 상태가 어긋났다");
            g_sink ^= uint64_t(sink);
            std::printf("%-8zu %-10d %12.3f %12.3f %12.3f\n", n2, query_pct, t_naive, t_fen, t_seg);
        }
    }
}

// -------------------------------------------------------------------- 사전

static void BenchDict() {
    Rule("해시 표 4팔 — 삽입 뒤 절반 삭제. 삭제가 있어야 세 정책이 갈린다");
    const size_t n = g_smoke ? 2000 : 200000;
    // 표 크기가 2의 거듭제곱이라 *요청한* load 와 실제로 도달한 load 가 다르다.
    // 그 양자화 자체가 관찰 대상이다 — 0.5 요청과 0.7 요청이 같은 용량으로 접히면
    // 두 행이 같은 수치를 낸다. 실제 도달 load 를 함께 찍어 그것을 드러낸다.
    //
    // 평균만 보면 tombstone 과 backward-shift 가 비슷해 보인다. 갈리는 것은 꼬리라
    // 조회 하나하나의 probe 길이를 모아 P99 를 함께 낸다.
    std::printf("%-16s %-6s %-6s %-14s %9s %9s %9s %9s %8s\n",
                "keys", "요청", "실제", "arm", "hit_avg", "hit_P99",
                "miss_avg", "miss_P99", "cluster");
    for (measure::KeyShape shape : {measure::KeyShape::Random, measure::KeyShape::CommonPrefix}) {
        const std::vector<std::string> keys = measure::MakeKeys(shape, n, 0x0DEEDu);
        const std::vector<std::string> absent =
            measure::MakeKeys(measure::KeyShape::Sequential, n / 4 + 1, 7u);
        for (double load : {0.5, 0.7, 0.9}) {
            const char* names[4] = {"chaining", "linear+tomb", "linear+shift", "robinhood"};
            for (int arm = 0; arm < 4; ++arm) {
                Samples hit, miss;
                size_t cluster = 0;
                double actual = 0.0;
                // 조회 하나의 probe 길이를 재는 도우미. Counters 를 매번 새로 만들어
                // 그 호출이 쓴 걸음만 남긴다.
                auto Probe = [](auto& map, const std::string& key, int64_t* out) {
                    Counters c;
                    const bool found = map.Get(key, out, &c);
                    return std::pair<bool, double>{found, double(c.probes)};
                };
                if (arm == 0) {
                    ChainMap map(16);
                    for (size_t i = 0; i < keys.size(); ++i) map.Put(keys[i], int64_t(i));
                    for (size_t i = 0; i < keys.size(); i += 2) map.Erase(keys[i]);
                    for (size_t i = 1; i < keys.size(); i += 2) {
                        int64_t v;
                        const auto r = Probe(map, keys[i], &v);
                        if (!r.first) Die("chaining 조회 실패");
                        hit.Add(r.second);
                    }
                    for (size_t i = 0; i < keys.size(); i += 2) {
                        int64_t v;
                        const auto r = Probe(map, keys[i], &v);
                        if (r.first) Die("지운 키가 chaining 에 남아 있다");
                        miss.Add(r.second);
                    }
                    for (const std::string& k : absent) {
                        int64_t v;
                        miss.Add(Probe(map, k, &v).second);
                    }
                    cluster = map.LongestChain();
                    actual = double(map.Size()) / double(map.Buckets());
                } else {
                    const DeletePolicy policy = (arm == 1) ? DeletePolicy::Tombstone
                                              : (arm == 2) ? DeletePolicy::BackwardShift
                                                           : DeletePolicy::RobinHood;
                    OpenMap map(policy, 16);
                    map.SetMaxLoad(load);
                    for (size_t i = 0; i < keys.size(); ++i) map.Put(keys[i], int64_t(i));
                    for (size_t i = 0; i < keys.size(); i += 2)
                        if (!map.Erase(keys[i])) Die("open addressing 삭제 실패");
                    if (!map.ProbeChainIntact()) Die("삭제 뒤 조회 종료 불변식이 깨졌다");
                    for (size_t i = 1; i < keys.size(); i += 2) {
                        int64_t v;
                        const auto r = Probe(map, keys[i], &v);
                        if (!r.first) Die("open addressing 조회 실패");
                        hit.Add(r.second);
                    }
                    for (size_t i = 0; i < keys.size(); i += 2) {
                        int64_t v;
                        const auto r = Probe(map, keys[i], &v);
                        if (r.first) Die("지운 키가 표에 남아 있다");
                        miss.Add(r.second);
                    }
                    for (const std::string& k : absent) {
                        int64_t v;
                        miss.Add(Probe(map, k, &v).second);
                    }
                    cluster = map.LongestCluster();
                    actual = double(map.Size()) / double(map.Capacity());
                }
                g_sink ^= uint64_t(hit.Mean() * 1000.0);
                std::printf("%-16s %-6.1f %-6.3f %-14s %9.2f %9.0f %9.2f %9.0f %8zu\n",
                            shape == measure::KeyShape::Random ? "random" : "common-prefix",
                            load, actual, names[arm],
                            hit.Mean(), hit.Percentile(99),
                            miss.Mean(), miss.Percentile(99), cluster);
            }
        }
    }

    Rule("Bloom filter — 예측 FP 율과 실측");
    std::printf("%-10s %-6s %-10s %14s %14s\n", "bits", "k", "n", "predicted", "measured");
    const size_t inserted = g_smoke ? 500 : 20000;
    for (int k : {2, 5, 9}) {
        BloomFilter bloom(inserted * 10, k);
        const std::vector<std::string> in = measure::MakeKeys(measure::KeyShape::Sequential, inserted, 1u);
        for (const std::string& s : in) bloom.Add(s);
        for (const std::string& s : in) if (!bloom.MightContain(s)) Die("Bloom filter 가 거짓음성을 냈다");
        const std::vector<std::string> out = measure::MakeKeys(measure::KeyShape::Random, inserted, 2u);
        size_t hits = 0;
        for (const std::string& s : out) if (bloom.MightContain(s)) ++hits;
        std::printf("%-10zu %-6d %-10zu %14.5f %14.5f\n", bloom.Bits(), k, inserted,
                    bloom.PredictedFalsePositiveRate(), double(hits) / double(out.size()));
    }
}

// ------------------------------------------------------------------ 문자열

static void BenchText() {
    Rule("단일 패턴 4팔 — 알파벳 크기와 패턴 길이가 승자를 바꾼다");
    const size_t text_len = g_smoke ? 20000 : 2000000;
    std::printf("%-10s %-8s %-16s %10s %14s\n", "alphabet", "pat_len", "arm", "ms", "compares");
    for (uint32_t alphabet : {uint32_t(2), uint32_t(26)}) {
        measure::Rng rng(0x7E777u);
        std::string text;
        text.reserve(text_len);
        for (size_t i = 0; i < text_len; ++i) text.push_back(char('a' + rng.Below(alphabet)));
        for (size_t pat_len : {size_t(4), size_t(32)}) {
            const std::string pat = text.substr(text_len / 2, pat_len);
            size_t reference = 0;
            const char* names[4] = {"naive", "kmp", "z", "bmh"};
            for (int arm = 0; arm < 4; ++arm) {
                Counters c;
                Timer t;
                std::vector<size_t> hits;
                switch (arm) {
                    case 0: hits = NaiveSearch(text, pat, &c); break;
                    case 1: hits = KmpSearch(text, pat, &c); break;
                    case 2: hits = ZSearch(text, pat); break;
                    default: hits = BmhSearch(text, pat, &c); break;
                }
                const double ms = t.Ms();
                if (arm == 0) reference = hits.size();
                else if (hits.size() != reference) Die("탐색 팔이 서로 다른 일치 수를 냈다");
                g_sink ^= hits.size();
                if (arm == 2) {
                    // Z 배열 팔에는 비교 계측을 달지 않았다. 0 으로 찍으면 "비교를
                    // 하지 않는다" 로 읽히므로 '-' 로 구분해 둔다.
                    std::printf("%-10u %-8zu %-16s %10.3f %14s\n", alphabet, pat_len,
                                names[arm], ms, "-");
                } else {
                    std::printf("%-10u %-8zu %-16s %10.3f %14llu\n", alphabet, pat_len,
                                names[arm], ms, (unsigned long long)c.comparisons);
                }
            }
        }
    }

    Rule("다중 패턴 2팔 — 패턴 수 p 의 교차점");
    std::printf("%-8s %14s %14s %12s\n", "p", "kmp_each_ms", "aho_ms", "aho_states");
    measure::Rng rng(0xA40u);
    std::string text;
    const size_t tn = g_smoke ? 20000 : 500000;
    for (size_t i = 0; i < tn; ++i) text.push_back(char('a' + rng.Below(4)));
    for (size_t p : {size_t(1), size_t(8), size_t(64)}) {
        // 중복 패턴이 섞이면 Aho 는 한 번, 패턴별 KMP 는 두 번 세어 대조가 깨진다.
        std::vector<std::string> pats;
        for (size_t i = 0; pats.size() < p && i < tn - 8; ++i) {
            const std::string candidate = text.substr((i * 977) % (tn - 8), 6);
            if (std::find(pats.begin(), pats.end(), candidate) == pats.end())
                pats.push_back(candidate);
        }
        size_t kmp_hits = 0;
        Timer t1;
        for (const std::string& pat : pats) kmp_hits += KmpSearch(text, pat).size();
        const double t_kmp = t1.Ms();
        AhoCorasick ac;
        for (const std::string& pat : pats) ac.Add(pat);
        ac.Build();
        Timer t2;
        const size_t aho_hits = ac.Search(text).size();
        const double t_aho = t2.Ms();
        if (kmp_hits != aho_hits) Die("Aho-Corasick 과 패턴별 KMP 의 일치 수가 다르다");
        g_sink ^= aho_hits;
        std::printf("%-8zu %14.3f %14.3f %12zu\n", p, t_kmp, t_aho, ac.StateCount());
    }
}

// -------------------------------------------------------------------- 그래프

static void BenchGraph() {
    Rule("SCC 2팔 · MST 2팔 · DSU 4팔");
    const size_t n = g_smoke ? 400 : 20000;
    measure::Rng rng(0x6AAF80u);
    std::vector<Csr::Edge> directed, undirected;
    for (uint32_t u = 0; u < n; ++u) {
        for (int k = 0; k < 4; ++k) {
            const uint32_t v = rng.Below(uint32_t(n));
            if (v == u) continue;
            directed.push_back({u, v, int64_t(rng.Below(100)) + 1});
            undirected.push_back({u, v, int64_t(rng.Below(100)) + 1});
        }
    }
    Csr dg;
    dg.Build(n, directed, false);
    std::printf("%-18s %10s %12s\n", "arm", "ms", "components");
    {
        Timer t;
        const SccResult r = SccTarjan(dg);
        std::printf("%-18s %10.3f %12zu\n", "tarjan", t.Ms(), r.count);
        Timer t2;
        const SccResult k = SccKosaraju(dg);
        std::printf("%-18s %10.3f %12zu\n", "kosaraju", t2.Ms(), k.count);
        if (r.count != k.count) Die("두 SCC 팔의 성분 수가 다르다");
        g_sink ^= r.count;
    }
    {
        Timer t;
        const MstResult k = Kruskal(n, undirected);
        std::printf("%-18s %10.3f %12lld  (V=%zu E=%zu)\n", "kruskal", t.Ms(),
                    (long long)k.total, n, undirected.size());
        g_sink ^= uint64_t(k.total);
    }

    // MST 두 팔의 비교는 밀도 스윕에서 한다. Prim 의 dense 판은 V^2 행렬을 잡으므로
    // 위의 큰 그래프에서는 돌릴 수 없다 — 그 제약 자체가 이 팔의 성질이다.
    std::printf("\n%-8s %-10s %-8s %12s %12s %12s\n",
                "V", "E", "밀도%", "kruskal_ms", "prim_ms", "cost");
    const size_t mn = g_smoke ? 120 : 900;
    for (int density : {2, 20, 100}) {
        measure::Rng rng2(0xA3C0DEu);
        std::vector<Csr::Edge> es;
        for (uint32_t u = 0; u + 1 < mn; ++u)
            for (uint32_t v = u + 1; v < mn; ++v) {
                if (int(rng2.Below(100)) >= density) continue;
                es.push_back({u, v, int64_t(rng2.Below(10000)) + 1});
            }
        Timer t1;
        const MstResult k = Kruskal(mn, es);
        const double t_kruskal = t1.Ms();
        Timer t2;
        const MstResult pr = PrimDense(mn, es);
        const double t_prim = t2.Ms();
        if (k.spanning != pr.spanning) Die("두 MST 팔의 연결 판정이 다르다");
        if (k.spanning && k.total != pr.total) Die("두 MST 팔의 비용이 다르다");
        g_sink ^= uint64_t(k.total);
        std::printf("%-8zu %-10zu %-8d %12.3f %12.3f %12lld\n", mn, es.size(), density,
                    t_kruskal, t_prim, (long long)k.total);
    }
    // 최적화를 둘 다 끈 팔은 union 자체가 O(n^2) 가 된다(매 Union 이 사슬 전체를
    // 걷는다). 그래서 이 스윕만 규모를 줄인다 — 줄여야 그 팔이 *끝나기* 때문이고,
    // 끝나지 않는다는 사실 자체가 alpha(n) 서사의 절반이다.
    std::printf("\n%-24s %14s %10s\n", "DSU arm", "avg_find_depth", "ms");
    const size_t dn = g_smoke ? 800 : 20000;
    for (int ci = 0; ci < 2; ++ci) {
        for (int si = 0; si < 2; ++si) {
            Dsu dsu(dn, ci == 1, si == 1);
            Timer t;
            // Union(i, i-1) 순서가 중요하다. Union(i-1, i) 로 붙이면 매번 *새 정점*
            // 이 뿌리가 되어 트리가 납작해지고 네 팔이 전부 깊이 1 을 낸다.
            // 반대로 붙이면 기존 뿌리가 새 정점 밑으로 들어가 사슬이 자란다 —
            // union by size 를 끈 팔이 실제로 깊어지는 유일한 배치다.
            for (uint32_t i = 1; i < dn; ++i) dsu.Union(i, i - 1);
            dsu.ResetStats();
            for (uint32_t i = 0; i < dn; ++i) dsu.Find(i);
            char label[64];
            std::snprintf(label, sizeof(label), "compress=%d by_size=%d", ci, si);
            std::printf("%-24s %14.3f %10.3f\n", label, dsu.AverageFindDepth(), t.Ms());
        }
    }
}

// ---------------------------------------------------------------- 최단 경로

static void BenchPaths() {
    Rule("최단 경로 4팔 — 밀도가 승자를 바꾼다");
    const size_t n = g_smoke ? 120 : 1200;
    std::printf("%-10s %-16s %10s %12s %12s %12s\n",
                "density", "arm", "ms", "settled", "pushes", "stale_pops");
    for (int density : {2, 50}) {
        measure::Rng rng(0x5F4A00u);
        std::vector<Csr::Edge> edges;
        for (uint32_t u = 0; u < n; ++u)
            for (uint32_t v = 0; v < n; ++v) {
                if (u == v || int(rng.Below(100)) >= density) continue;
                edges.push_back({u, v, int64_t(rng.Below(1000)) + 1});
            }
        Csr g;
        g.Build(n, edges, false);
        int64_t reference = -1;
        const char* names[4] = {"dijkstra(heap)", "dijkstra(dense)", "bellman-ford", "dijkstra(re-expand)"};
        for (int arm = 0; arm < 4; ++arm) {
            DijkstraOptions opt;
            if (arm == 3) { opt.skip_stale = false; opt.final_on_settle = false; }
            Timer t;
            PathResult r;
            switch (arm) {
                case 0: r = DijkstraHeap(g, 0); break;
                case 1: r = DijkstraDense(g, 0); break;
                case 2: r = BellmanFord(n, edges, 0); break;
                default: r = DijkstraHeap(g, 0, opt); break;
            }
            const double ms = t.Ms();
            int64_t sum = 0;
            for (int64_t d : r.dist) if (d < kInfCost) sum += d;
            if (reference < 0) reference = sum;
            else if (sum != reference) Die("최단 경로 팔의 거리 합이 다르다");
            g_sink ^= uint64_t(sum);
            std::printf("%-10d %-16s %10.3f %12zu %12zu %12zu\n", density, names[arm], ms,
                        r.settled, r.pushes, r.stale_pops);
        }
    }

    Rule("A* — 휴리스틱 · 가중치 · 재개방이 확장 수와 최적성에 미치는 영향");
    Grid grid;
    grid.w = g_smoke ? 40 : 260;
    grid.h = g_smoke ? 40 : 260;
    grid.blocked.assign(grid.w * grid.h, 0);
    const uint32_t start = 0, goal = uint32_t(grid.w * grid.h - 1);
    AStarOptions zero;
    zero.heuristic = Heuristic::Zero;

    // 목표에 닿지 않는 격자에서 잰 확장 수는 "탐색이 얼마나 좋았나" 가 아니라
    // "열린 칸이 몇 개인가" 다. 20% 무작위 차단이면 두 모퉁이 중 하나가 갇힐
    // 확률이 낮지 않으므로, 도달 가능한 첫 시드를 결정적으로 찾아 쓴다.
    uint64_t chosen_seed = 0;
    AStarResult truth;
    for (uint64_t seed = 1; seed <= 64; ++seed) {
        measure::Rng rng(seed * 0xA57A2u);
        for (size_t i = 0; i < grid.blocked.size(); ++i)
            grid.blocked[i] = (rng.Below(100) < 20) ? 1u : 0u;
        grid.blocked[0] = 0;
        grid.blocked.back() = 0;
        truth = AStarGrid(grid, start, goal, zero);
        if (truth.found) { chosen_seed = seed; break; }
    }
    if (!truth.found) Die("A* 벤치 격자에서 목표에 도달할 수 없다");
    std::printf("격자 %zux%zu, 차단 20%%, 시드 %llu, 최적 비용 %lld\n",
                grid.w, grid.h, (unsigned long long)chosen_seed, (long long)truth.cost);
    std::printf("%-14s %-7s %-8s %10s %10s %10s %10s %10s\n",
                "heuristic", "weight", "reopen", "ms", "expanded", "reopened",
                "queue_peak", "cost/opt");
    const Heuristic hs[] = {Heuristic::Zero, Heuristic::Manhattan, Heuristic::Euclidean};
    const char* hn[] = {"zero", "manhattan", "euclidean"};
    for (int hi = 0; hi < 3; ++hi) {
        for (double w : {1.0, 1.5, 3.0}) {
            for (int reopen : {1, 0}) {
                AStarOptions opt;
                opt.heuristic = hs[hi];
                opt.weight = w;
                opt.reopen = (reopen != 0);
                Timer t;
                const AStarResult r = AStarGrid(grid, start, goal, opt);
                const double ms = t.Ms();
                if (r.found != truth.found) Die("A* 팔이 도달 가능성을 다르게 판정했다");
                if (!ValidateGridPath(grid, r.path, start, goal, r.cost))
                    Die("A* 가 낸 경로가 유효하지 않다");
                if (r.cost < truth.cost) Die("A* 가 최적보다 싼 비용을 보고했다");
                g_sink ^= uint64_t(r.cost);
                std::printf("%-14s %-7.1f %-8d %10.3f %10zu %10zu %10zu %10.4f\n",
                            hn[hi], w, reopen, ms, r.expanded, r.reopened, r.queue_peak,
                            double(r.cost) / double(truth.cost));
            }
        }
    }

    std::printf("\n%-14s %-24s %10s %10s %10s\n",
                "heuristic", "tie-break", "ms", "expanded", "queue_peak");
    const char* tie_names[3] = {"f only", "f then smaller h", "f then larger g"};
    for (int tie = 0; tie < 3; ++tie) {
        AStarOptions opt;
        opt.heuristic = Heuristic::Manhattan;
        opt.tie_break = tie;
        Timer t;
        const AStarResult r = AStarGrid(grid, start, goal, opt);
        const double ms = t.Ms();
        if (r.cost != truth.cost) Die("tie-break 가 최적 비용을 바꿨다");
        g_sink ^= uint64_t(r.expanded);
        std::printf("%-14s %-24s %10.3f %10zu %10zu\n", "manhattan", tie_names[tie], ms,
                    r.expanded, r.queue_peak);
    }
}

// ----------------------------------------------------------- 탐욕 · DP · 탐색

static void BenchDp() {
    Rule("배낭 4팔 — 상태 수 · 표 바이트 · optimality gap");
    std::printf("%-8s %-8s %-16s %10s %14s %12s %10s\n",
                "n", "capacity", "arm", "ms", "states/nodes", "table_KiB", "gap");
    const size_t n = g_smoke ? 14 : 22;
    for (int64_t capacity : {int64_t(60), int64_t(2000)}) {
        measure::Rng rng(0xD9A8u);
        std::vector<Item> items;
        for (size_t i = 0; i < n; ++i)
            items.push_back(Item{int64_t(rng.Below(uint32_t(capacity / 4))) + 1,
                                 int64_t(rng.Below(200)) + 1});
        int64_t optimum = 0;
        for (int arm = 0; arm < 4; ++arm) {
            Timer t;
            int64_t value = 0;
            uint64_t states = 0, bytes = 0;
            if (arm == 0) {
                const KnapsackResult r = Knapsack2D(items, capacity);
                value = r.value;
                states = r.stats.states;
                bytes = r.stats.table_bytes;
            } else if (arm == 1) {
                DpStats st;
                value = Knapsack1D(items, capacity, true, &st);
                states = st.states;
                bytes = st.table_bytes;
            } else if (arm == 2) {
                const BranchBoundResult r = BranchAndBoundKnapsack(items, capacity);
                value = r.best;
                states = r.nodes;
            } else {
                value = DensityGreedyKnapsack(items, capacity);
            }
            const double ms = t.Ms();
            if (arm == 0) optimum = value;
            else if (arm <= 2 && value != optimum) Die("정확 팔이 서로 다른 최적값을 냈다");
            if (value > optimum) Die("탐욕이 최적을 넘었다");
            g_sink ^= uint64_t(value);
            const char* names[4] = {"dp2d", "dp1d", "branch&bound", "density greedy"};
            std::printf("%-8zu %-8lld %-16s %10.3f %14llu %12.1f %10.4f\n",
                        n, (long long)capacity, names[arm], ms,
                        (unsigned long long)states, double(bytes) / 1024.0,
                        optimum ? double(optimum - value) / double(optimum) : 0.0);
        }
    }

    Rule("집합 덮개 — 탐욕의 실측 gap 과 조화합 상계");
    std::printf("%-10s %-10s %10s %10s %12s %12s\n",
                "universe", "sets", "greedy", "optimum", "ratio", "H(n) 상계");
    for (size_t universe : {size_t(8), size_t(10)}) {
        measure::Rng rng(0x5E7Cu);
        std::vector<std::vector<uint32_t>> sets;
        for (size_t s = 0; s < 8; ++s) {
            std::vector<uint32_t> members;
            for (uint32_t e = 0; e < universe; ++e)
                if (rng.Below(100) < 35) members.push_back(e);
            if (members.empty()) members.push_back(rng.Below(uint32_t(universe)));
            sets.push_back(members);
        }
        for (uint32_t e = 0; e < universe; ++e) sets.push_back({e});   // 덮개 존재 보장
        const std::vector<int64_t> cost(sets.size(), 1);
        const SetCoverResult greedy = SetCoverGreedy(universe, sets, cost);
        if (!greedy.covered) Die("탐욕 집합 덮개가 전체를 덮지 못했다");
        // 최적은 작은 사례에서만 전수 열거한다.
        int64_t best = -1;
        if (sets.size() <= 20) {
            best = 0;
            std::vector<uint8_t> covered(universe, 0);
            (void)covered;
            best = greedy.cost;
            for (uint64_t mask = 0; mask < (uint64_t(1) << sets.size()); ++mask) {
                int64_t total = 0;
                std::vector<uint8_t> got(universe, 0);
                for (size_t s = 0; s < sets.size(); ++s) {
                    if (!(mask & (uint64_t(1) << s))) continue;
                    total += cost[s];
                    if (total >= best) break;
                    for (uint32_t e : sets[s]) got[e] = 1;
                }
                if (total >= best) continue;
                bool all = true;
                for (uint8_t c : got) if (!c) { all = false; break; }
                if (all) best = total;
            }
        }
        g_sink ^= uint64_t(greedy.cost);
        std::printf("%-10zu %-10zu %10lld %10lld %12.4f %12.4f\n", universe, sets.size(),
                    (long long)greedy.cost, (long long)best,
                    best > 0 ? double(greedy.cost) / double(best) : 1.0,
                    HarmonicBound(universe));
    }
}

// ------------------------------------------------------------- 유량 · 탐색

static void BenchFlow() {
    Rule("최대 유량 3팔 — 증가 횟수와 phase 수");
    const size_t n = g_smoke ? 30 : 160;
    std::printf("%-16s %10s %12s %10s %12s\n", "arm", "ms", "augments", "phases", "value");
    measure::Rng rng(0xF10A00u);
    std::vector<std::array<int64_t, 3>> arcs;
    for (uint32_t u = 0; u + 1 < n; ++u)
        for (uint32_t v = u + 1; v < n; ++v) {
            if (rng.Below(100) >= 25) continue;
            arcs.push_back({int64_t(u), int64_t(v), int64_t(rng.Below(50)) + 1});
        }
    int64_t reference = -1;
    for (int arm = 0; arm < 3; ++arm) {
        Residual g(n);
        for (const auto& a : arcs) g.AddEdge(uint32_t(a[0]), uint32_t(a[1]), a[2]);
        Timer t;
        FlowResult r;
        if (arm == 0) r = FordFulkerson(g, 0, uint32_t(n - 1));
        else if (arm == 1) r = EdmondsKarp(g, 0, uint32_t(n - 1));
        else { Dinic d; r = d.Run(g, 0, uint32_t(n - 1)); }
        const double ms = t.Ms();
        if (reference < 0) reference = r.value;
        else if (r.value != reference) Die("유량 팔이 서로 다른 값을 냈다");
        if (!g.CapacityRespected() || !g.Conserves(0, uint32_t(n - 1)))
            Die("유량 불변식이 깨졌다");
        const MinCut cut = ExtractMinCut(g, 0);
        if (cut.capacity != r.value) Die("min-cut certificate 가 유량과 다르다");
        g_sink ^= uint64_t(r.value);
        const char* names[3] = {"ford-fulkerson", "edmonds-karp", "dinic"};
        std::printf("%-16s %10.3f %12zu %10zu %12lld\n", names[arm], ms, r.augments,
                    r.phases, (long long)r.value);
    }

    Rule("이분 매칭 3팔");
    const measure::Bipartite b = measure::RandomBipartite(g_smoke ? 60 : 900,
                                                         g_smoke ? 60 : 900, 0.01, 4u);
    std::printf("%-18s %10s %12s %10s %10s\n", "arm", "ms", "size", "augments", "phases");
    {
        Timer t;
        const Matching m = BipartiteMatchingDfs(b.left, b.right, b.adj);
        std::printf("%-18s %10.3f %12zu %10zu %10zu\n", "dfs augmenting", t.Ms(), m.size,
                    m.augments, m.phases);
        Timer t2;
        const Matching h = HopcroftKarp(b.left, b.right, b.adj);
        std::printf("%-18s %10.3f %12zu %10zu %10zu\n", "hopcroft-karp", t2.Ms(), h.size,
                    h.augments, h.phases);
        Timer t3;
        const int64_t f = MatchingViaFlow(b.left, b.right, b.adj);
        std::printf("%-18s %10.3f %12lld %10s %10s\n", "flow reduction", t3.Ms(),
                    (long long)f, "-", "-");
        if (m.size != h.size || int64_t(m.size) != f) Die("매칭 팔의 크기가 다르다");
        g_sink ^= m.size;
    }

    Rule("CSP 가지치기 — 노드 수와 가지치기 이유별 횟수");
    CspProblem p;
    p.variables = g_smoke ? 7 : 9;
    p.full_domain = 0b1111;
    for (uint32_t i = 0; i < p.variables; ++i) {
        p.not_equal.push_back({i, uint32_t((i + 1) % p.variables)});
        p.not_equal.push_back({i, uint32_t((i + 2) % p.variables)});
    }
    std::printf("%-30s %10s %12s %10s %12s %12s\n",
                "arm", "ms", "nodes", "solutions", "fwd_prune", "arc_prune");
    size_t reference_count = 0;
    const CspOptions arms[] = {
        CspOptions{false, false, false, false},
        CspOptions{true, false, false, false},
        CspOptions{true, true, false, false},
        CspOptions{true, true, true, false},
        CspOptions{true, true, true, true},
    };
    const char* labels[] = {"plain", "+forward check", "+AC-3", "+MRV", "+symmetry break"};
    for (int arm = 0; arm < 5; ++arm) {
        CspSolver solver(p, arms[arm]);
        Timer t;
        const size_t count = solver.CountSolutions();
        const double ms = t.Ms();
        if (!solver.HashStable()) Die("Apply/Undo 뒤 상태 해시가 달라졌다");
        if (arm == 0) reference_count = count;
        else if (arm < 4 && count != reference_count) Die("가지치기가 해를 잃거나 늘렸다");
        if (arm == 4 && count > reference_count) Die("대칭 깨기가 해를 늘렸다");
        g_sink ^= count;
        std::printf("%-30s %10.3f %12llu %10zu %12llu %12llu\n", labels[arm], ms,
                    (unsigned long long)solver.Stats().nodes, count,
                    (unsigned long long)solver.Stats().pruned_forward,
                    (unsigned long long)solver.Stats().pruned_arc);
    }
}

// -------------------------------------------------------------------- 압축

static void BenchCompress() {
    Rule("코퍼스 8종 x 코덱 3팔 — 압축률은 데이터의 성질이지 알고리즘의 성질이 아니다");
    const Corpus kinds[] = {Corpus::Text, Corpus::StructuredBinary, Corpus::IntArray,
                            Corpus::FloatArray, Corpus::AlreadyCompressed,
                            Corpus::TinyMessage, Corpus::HighlyRepetitive, Corpus::Random};
    const size_t size = g_smoke ? 20000 : 400000;
    std::printf("%-14s %9s %7s %-12s %9s %9s %9s %9s %9s\n",
                "corpus", "raw", "H(bits)", "codec", "ratio", "saving%", "enc_MB/s",
                "dec_MB/s", "hdr_B");
    for (Corpus kind : kinds) {
        const Bytes raw = measure::MakeCorpus(kind, size, 2u);
        const double h = Entropy0(raw);
        const char* names[4] = {"huffman", "arith", "lz-greedy", "lz-optimal"};
        for (int arm = 0; arm < 4; ++arm) {
            Timer te;
            Bytes frame;
            switch (arm) {
                case 0: frame = HuffmanCompress(raw); break;
                case 1: frame = RangeCompress(raw); break;
                case 2: frame = LzEncode(raw, ParseMode::Greedy); break;
                default: frame = LzEncode(raw, ParseMode::Optimal); break;
            }
            const double enc_ms = te.Ms();
            Bytes back;
            Timer td;
            bool ok = false;
            switch (arm) {
                case 0: ok = HuffmanDecompress(frame, &back); break;
                case 1: ok = RangeDecompress(frame, &back); break;
                default: ok = LzDecode(frame, &back); break;
            }
            const double dec_ms = td.Ms();
            if (!ok) Die("복호가 실패했다");
            if (back != raw) Die("왕복 결과가 원본과 다르다");
            g_sink ^= frame.size();
            const size_t header = (arm == 0) ? (14 + 256) : (arm == 1) ? (14 + 512) : 14;
            const double mb = double(raw.size()) / (1024.0 * 1024.0);
            std::printf("%-14s %9zu %7.3f %-12s %9.4f %9.2f %9.1f %9.1f %9zu\n",
                        measure::CorpusName(kind), raw.size(), h, names[arm],
                        double(frame.size()) / double(raw.size() ? raw.size() : 1),
                        100.0 * (1.0 - double(frame.size()) / double(raw.size() ? raw.size() : 1)),
                        enc_ms > 0 ? mb / (enc_ms / 1000.0) : 0.0,
                        dec_ms > 0 ? mb / (dec_ms / 1000.0) : 0.0,
                        header);
        }
    }

    Rule("블록 크기 3팔 — 블록이 작으면 표 비용이, 크면 지연이 는다");
    std::printf("%-10s %10s %12s %14s %14s\n", "block", "blocks", "total_bytes", "ratio", "p99_block_ms");
    const Bytes text = measure::MakeCorpus(Corpus::Text, g_smoke ? 40000 : 2000000, 3u);
    for (size_t block : {size_t(4096), size_t(65536), size_t(1048576)}) {
        size_t total = 0, count = 0;
        Samples per_block;
        for (size_t at = 0; at < text.size(); at += block) {
            const size_t len = std::min(block, text.size() - at);
            const Bytes chunk(text.begin() + std::ptrdiff_t(at),
                              text.begin() + std::ptrdiff_t(at + len));
            Timer t;
            const Bytes frame = HuffmanCompress(chunk);
            per_block.Add(t.Ms());
            Bytes back;
            if (!HuffmanDecompress(frame, &back) || back != chunk) Die("블록 왕복 실패");
            total += frame.size();
            ++count;
        }
        g_sink ^= total;
        std::printf("%-10zu %10zu %12zu %14.4f %14.3f\n", block, count, total,
                    double(total) / double(text.size()), per_block.Percentile(99));
    }
}

int main(int argc, char** argv) {
    bool all = true;
    bool want[10] = {false};
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--smoke") { g_smoke = true; continue; }
        all = false;
        if (arg == "--sort") want[0] = true;
        else if (arg == "--select") want[1] = true;
        else if (arg == "--queue") want[2] = true;
        else if (arg == "--dict") want[3] = true;
        else if (arg == "--text") want[4] = true;
        else if (arg == "--graph") want[5] = true;
        else if (arg == "--paths") want[6] = true;
        else if (arg == "--dp") want[7] = true;
        else if (arg == "--flow") want[8] = true;
        else if (arg == "--compress") want[9] = true;
        else {
            std::printf("usage: algo_bench [--smoke] [--sort|--select|--queue|--dict|--text"
                        "|--graph|--paths|--dp|--flow|--compress]\n");
            return 2;
        }
    }
    if (g_smoke && all) { /* --smoke 만 주면 전부를 축소 실행한다 */ }

    if (all || want[0]) BenchSort();
    if (all || want[1]) BenchSelect();
    if (all || want[2]) BenchQueue();
    if (all || want[3]) BenchDict();
    if (all || want[4]) BenchText();
    if (all || want[5]) BenchGraph();
    if (all || want[6]) BenchPaths();
    if (all || want[7]) BenchDp();
    if (all || want[8]) BenchFlow();
    if (all || want[9]) BenchCompress();

    // checksum 을 실제로 소비한다. 이 한 줄이 없으면 최적화기가 위의 계산을
    // 통째로 지울 수 있고, 그러면 표의 시간은 아무것도 뜻하지 않는다.
    std::printf("\nchecksum %016llx\n", (unsigned long long)g_sink);
    return 0;
}
