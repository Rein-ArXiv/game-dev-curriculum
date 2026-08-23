## 들어가며

`algorithms_lab`은 알고리즘을 직접 구현하고, 그 구현이 지켜야 할 불변식을 느린 기준 구현과 대조해 확인하는 헤드리스 실험실이다. 외부 의존성이 없다 — 학습 대상이 알고리즘 자체이므로 라이브러리를 끌어오면 검증할 것이 사라진다. 표준 라이브러리는 오라클로만 쓴다.

첫 단계에서는 순서와 선택을 짓는다. 반열린 구간 이진 탐색, 안정 merge sort, introsort, counting sort, LSD radix sort, quickselect, median-of-medians, 크기 제한 힙 top-k. 그리고 이 lab의 성격을 정하는 두 장치를 함께 만든다 — 작업량을 세는 계측기와 적대 입력 생성기다.

---

## 목차
1. [세 개의 계약](#1-세-개의-계약)
2. [이진 탐색의 반열린 구간](#2-이진-탐색의-반열린-구간)
3. [비교 정렬 두 팔](#3-비교-정렬-두-팔)
4. [분포 정렬과 실수 키](#4-분포-정렬과-실수-키)
5. [선택 네 팔](#5-선택-네-팔)
6. [측정 장치](#6-측정-장치)
7. [실행과 관측](#7-실행과-관측)

---

## 1. 세 개의 계약

정렬 구현이 지켜야 하는 것은 하나가 아니라 셋이다.

| 계약 | 내용 | 이것만 빠지면 |
|---|---|---|
| 순서 | 출력이 비내림차순이다 | 정렬이 아니다 |
| 원소 보존 | 출력이 입력의 multiset이다 | 원소를 잃거나 복제한다 |
| 안정성 | 같은 키의 상대 순서가 유지된다 | 다중 키 정렬이 깨진다 |

순서만 검사하면 원소를 잃은 구현이 통과한다. 극단적으로, 입력을 버리고 빈 배열을 돌려주는 함수도 "정렬됨"을 만족한다. 그래서 값 자체가 원래 위치를 들고 다녀야 한다.

<!-- snippet: src/algo/order.hpp@order-rec -->
```cpp
// 정렬 대상. key 만 비교하고 tag 는 원래 위치를 들고 다닌다 — 안정성과 원소
// 보존을 판정하는 근거가 값 자체 안에 있어야 검사가 구현을 믿지 않는다.
struct Rec {
    uint32_t key = 0;
    uint32_t tag = 0;
};

inline bool ByKey(const Rec& a, const Rec& b) { return a.key < b.key; }
```

세 계약의 판정기는 `tests/oracle.h`에 있다. `SameMultiset`은 (key, tag) 쌍을 64비트로 접어 양쪽을 정렬해 비교하고, `Stable`은 같은 키 안에서 tag가 오름차순인지 본다 — 생성기가 tag를 원래 위치로 채우므로 이 조건이 곧 안정성이다.

안정성은 안정 팔에만 요구한다. introsort는 안정 정렬이 아니고, 그것을 요구하면 잘못된 검사가 된다. 검사가 구현에 없는 성질을 요구하면 그 검사는 설계를 왜곡한다.

## 2. 이진 탐색의 반열린 구간

이진 탐색이 틀리는 자리는 대소 비교가 아니라 구간 표현이다. 닫힌 구간 `[lo, hi]`로 쓰면 종료 조건과 갱신에 각각 한 칸씩 조정이 붙고, 그 둘이 어긋나면 무한 루프나 한 칸 밀림이 된다. 반열린 구간 `[lo, hi)`는 그 조정을 없앤다.

<!-- snippet: src/algo/order.hpp@order-lower-bound -->
```cpp
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
```

`UpperBound`는 같은 골격에서 물음만 뒤집는다 — `less(key, a[mid])`. 두 함수의 차가 곧 같은 키의 개수이고, 검사는 그 항등식을 단언한다. 한 칸 밀린 구현은 개수 검사에서 죽는다.

경계 입력은 빈 배열, 전부 같은 값, 키가 전부보다 작거나 큰 경우다. 전부 같은 값에서 `LowerBound`는 0을, `UpperBound`는 n을 내야 한다.

## 3. 비교 정렬 두 팔

introsort는 quicksort의 평균 속도와 heapsort의 최악 보장을 붙인 것이다. 분할이 무너져 재귀 깊이가 `2*floor(log2 n)`을 넘으면 그 구간을 heapsort로 넘긴다. 작은 구간은 insertion sort로 마무리한다.

<!-- snippet: src/algo/order.hpp@order-introsort -->
```cpp
// quicksort + 깊이 초과 시 heapsort + 작은 구간 insertion sort.
// 깊이 상한 2*floor(log2 n) 은 "분할이 절반씩 되면 절대 닿지 않는" 값이다 —
// 닿았다는 것 자체가 pivot 선택이 무너졌다는 신호이고, 그때 heapsort 가 받는다.
template <class T, class Less>
void Introsort(std::vector<T>& a, Less less, measure::Counters* c = nullptr) {
    if (a.size() < 2) return;
    detail::IntroLoop(a.data(), 0, a.size() - 1,
                      2 * detail::FloorLog2(a.size()), less, c);
}
```

안정 merge sort는 상향식이다. 병합에서 어느 쪽을 먼저 내느냐가 안정성을 정한다.

<!-- snippet: src/algo/order.hpp@order-merge-stable -->
```cpp
// 안정 merge sort. 병합에서 `less(right, left)` 를 물어 *같으면 왼쪽을 먼저* 낸다.
// `less(left, right)` 로 뒤집어 물으면 같은 키의 순서가 뒤집혀 안정성이 죽는다.
template <class T, class Less>
void MergeSortStable(std::vector<T>& a, Less less, measure::Counters* c = nullptr) {
    const size_t n = a.size();
    if (n < 2) return;
    std::vector<T> buf(n);
    if (c) c->Peak(uint64_t(n) * sizeof(T));   // 임시 배열이 이 알고리즘의 공간 가격
    // ...
}
```

비교자에 대한 요구는 엄격 약순서다. `<=`를 넘기면 `less(x, x)`가 참이 되어 분할이 경계를 넘고, 정렬이 아니라 범위 밖 접근이 된다. 이 lab의 비교자는 전부 `<`로만 짠다.

## 4. 분포 정렬과 실수 키

counting sort의 핵심은 배타적 prefix sum이다. 포함으로 쓰면 모든 원소가 한 칸씩 밀리고, 마지막 버킷은 배열 밖으로 나간다.

<!-- snippet: src/algo/order.hpp@order-counting -->
```cpp
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
    // ...
    a.swap(out);
}
```

LSD radix sort는 같은 흩뿌리기를 r비트씩 여러 pass로 돈다. 세 가지가 성능을 정한다 — ping-pong 버퍼로 매 pass마다 새 배열을 잡지 않는 것, 히스토그램이 한 버킷에 몰린 pass를 건너뛰는 것, 그리고 각 pass가 안정이라는 것.

<!-- snippet: src/algo/order.hpp@order-radix -->
```cpp
// LSD radix sort. r 비트씩 여러 pass 를 돈다.
//   - ping-pong 버퍼: 매 pass 마다 새 배열을 잡지 않는다.
//   - pass 생략: 히스토그램이 버킷 하나에 전부 몰렸으면 그 pass 는 항등이다.
//   - 안정성: 각 pass 가 안정이어야 전체가 정렬된다. 앞에서부터 흩뿌린다.
inline void RadixSortLsd(std::vector<Rec>& a, int radix_bits,
                         measure::Counters* c = nullptr) {
    // ...
    for (int shift = 0; shift < bits; shift += radix_bits) {
        for (size_t k = 0; k < kBuckets; ++k) count[k] = 0;
        for (const Rec& r : *src) ++count[(r.key >> shift) & mask];
        // 이 pass 가 항등인가 — 전부 한 버킷이면 흩뿌릴 이유가 없다.
        bool single = false;
        for (size_t k = 0; k < kBuckets; ++k) {
            if (count[k] == n) { single = true; break; }
        }
        if (single) continue;
        // ...
    }
    if (src != &a) a = *src;
}
```

분포 정렬은 정수 키만 흩뿌릴 수 있다. 실수 키를 다루려면 순서를 보존하는 정수 변환이 먼저 필요하다.

<!-- snippet: src/algo/order.hpp@order-ordered-key -->
```cpp
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
```

`-0.0`과 `+0.0`은 수학적으로 같지만 비트가 다르므로 키도 다르다. 이 lab은 그것을 결함으로 보지 않고 계약으로 적는다 — 이 변환이 만드는 것은 실수의 대소가 아니라 *총 순서*다.

## 5. 선택 네 팔

k번째 원소만 필요할 때 전체를 정렬하는 것은 낭비다. 네 가지 답이 있고, 서로 다른 이유로 서로 다른 구간에서 이긴다.

<!-- snippet: src/algo/order.hpp@order-quickselect -->
```cpp
// quickselect. k 번째(0-based)를 제자리에 놓고 돌아온다. 평균 O(n) 이지만
// 적대 입력 + 고정 pivot 이면 O(n^2) 다 — pivot 을 무작위로 고르는 이유.
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
```

무작위 pivot의 무작위성이 실행마다 달라지면 실패가 재현되지 않는다. 그래서 시드를 인자로 받는다 — 방어는 유지하고 재현성은 잃지 않는다.

median-of-medians는 5개씩 묶어 중앙값의 중앙값을 pivot으로 쓴다. 최악에서도 3n/10을 잘라내는 것이 보장되어 선형 시간이 최악에서도 성립한다. 대신 상수가 크다. 크기 k min-heap은 공간이 O(k)뿐이라 입력이 스트림이어도 되지만, 상위 m개를 답하므로 k번째로 작은 것을 얻으려면 m = n-k를 유지해야 한다.

## 6. 측정 장치

벽시계만 재면 두 가지를 잃는다. 시간은 기계마다 달라 재현되지 않고, 최적화기가 결과를 쓰지 않는 계산을 지워도 시간은 그럴듯하게 줄어든다. 그래서 측정은 두 층이다.

<!-- snippet: src/measure/counters.hpp@counters-struct -->
```cpp
struct Counters {
    uint64_t comparisons = 0;   // 비교자 호출 횟수
    uint64_t moved_bytes = 0;   // 원소 이동으로 옮긴 바이트
    uint64_t visited     = 0;   // 방문한 정점·노드·상태
    uint64_t probes      = 0;   // 해시 탐사 걸음
    uint64_t augments    = 0;   // 증가 경로·phase 횟수
    uint64_t peak_bytes  = 0;   // 임시 메모리 최대치
    uint64_t checksum    = 0;   // 결과를 소비한 흔적

    // 결과 checksum. 정렬 결과나 경로 비용을 여기에 섞어 두면, 그 값을 읽는 코드가
    // 벤치 바깥에 있으므로 컴파일러가 계산 자체를 죽은 코드로 지울 수 없다.
    void Consume(uint64_t value) {
        checksum ^= value + 0x9e3779b97f4a7c15ull + (checksum << 6) + (checksum >> 2);
    }
    void Peak(uint64_t bytes) { peak_bytes = (bytes > peak_bytes) ? bytes : peak_bytes; }
    void Reset() { *this = Counters{}; }
};
```

무작위 입력은 평균만 보여 준다. 알고 싶은 것은 "어떤 입력에서 무너지는가"이므로, 각 알고리즘의 가정을 정확히 겨냥한 모양을 손으로 만든다.

<!-- snippet: src/measure/workload.hpp@workload-sort-shapes -->
```cpp
// 정렬 워크로드의 모양. 이름이 곧 "무엇을 무너뜨리려는가" 다.
enum class SortShape {
    Random,        // 대조군
    Sorted,        // 이미 정렬 — 마지막 원소 pivot 을 O(n^2) 로 만든다
    Reversed,      // 역순 — 같은 이유
    AllEqual,      // 전부 동일 — Lomuto 분할이 한쪽으로 완전히 쏠린다
    TwoValues,     // 두 값 교대 — 분포 정렬의 히스토그램이 두 칸에 몰린다
    Sawtooth,      // 톱니 — 부분 정렬을 노린 최적화를 헛돌게 한다
    OrganPipe,     // 올라갔다 내려옴 — median-of-three 가 매번 최댓값을 고른다
    HighDigitSame, // 상위 digit 동일 — radix 의 상위 pass 가 전부 항등이 된다
    FewKeys,       // k << n — counting sort 가 이기는 구간
    ManyKeys       // k >> n — counting sort 가 히스토그램 비용에 짓눌린다
};
```

### 측정 결과

`algo_bench --sort`가 n=200,000, 25회 반복으로 낸 표에서 골라 읽는다(이 기계, MSVC Release).

| shape | arm | compares | moved KiB | P50 ms | P99 ms | peak KiB |
|---|---|---|---|---|---|---|
| random | introsort | 3,882,138 | 42,148 | 14.77 | 15.21 | 0 |
| random | mergesort | 3,333,300 | 28,125 | 19.70 | 20.65 | 1,562 |
| random | radix8 | 0 | 6,250 | 2.06 | 2.34 | 1,564 |
| organ-pipe | introsort | 11,727,693 | 125,060 | 17.93 | 28.93 | 0 |
| organ-pipe | mergesort | 1,830,044 | 28,125 | 4.30 | 4.92 | 1,562 |
| all-equal | radix8 | 0 | 0 | 1.72 | 1.89 | 1,564 |
| few-keys | counting | 0 | 1,562 | 0.70 | 1.02 | 1,562 |

읽을 것이 넷이다.

- **organ-pipe에서 introsort의 비교 횟수가 random의 3배다.** median-of-three가 매번 최댓값 근처를 고르는 모양이고, P99가 P50의 1.6배로 벌어진다. 깊이 상한이 heapsort로 넘겨 O(n²)는 막지만 상수는 그대로 문다.
- **all-equal에서 radix의 moved_bytes가 0이다.** 모든 pass의 히스토그램이 한 버킷에 몰려 전부 생략됐다. 생략 판정이 없으면 여기서 네 번 흩뿌린다.
- **radix8이 radix11보다 대개 빠르다.** 점근만 보면 r = log2(n) ≈ 17 근처가 pass 수를 최소로 만든다. 그런데 실측 최적은 8~11이다. 이유는 상수다 — r=8이면 히스토그램이 256칸(1KiB)이라 L1에 상주하고, r=11이면 2048칸(8KiB)이라 밀려난다. pass 하나를 줄인 이득을 캐시 미스가 먹는다.
- **mergesort의 peak가 1,562KiB다.** 이것이 안정성의 공간 가격이고, introsort의 0과 나란히 놓았을 때만 교환으로 보인다.

밀도 스윕은 판단의 입력이 "정수 키인가"가 아니라 n/k임을 보인다.

| n | k(키 종류) | radix8 ms | counting ms | introsort ms |
|---|---|---|---|---|
| 200,000 | 4 | 1.81 | 0.73 | 9.56 |
| 200,000 | 64 | 1.84 | 0.68 | 13.84 |
| 200,000 | 4,096 | 1.81 | 1.16 | 17.52 |
| 200,000 | 262,144 | 1.85 | 2.38 | 15.52 |

counting sort는 k가 n에 가까워질수록 히스토그램 비용에 짓눌려 radix에 추월당한다. k=4,096까지는 이기고 k=262,144에서는 진다. radix는 k와 무관하게 평평하다 — 키 폭이 고정이므로 pass 수가 데이터에 의존하지 않기 때문이다.

선택 팔의 교차점은 더 단순하다.

| input | k/n | arm | ms | compares |
|---|---|---|---|---|
| random | 0.001 | quickselect | 2.84 | 697,471 |
| random | 0.001 | median-of-medians | 21.35 | 3,290,500 |
| random | 0.001 | full sort | 31.6 | — |
| organ-pipe | 0.500 | quickselect | 1.52 | — |

quickselect가 전체 정렬보다 10배 빠르고, median-of-medians는 최악 보장의 대가로 quickselect의 7배 비교를 쓴다. 최악 보장이 필요 없는 워크로드에서 median-of-medians를 고르는 것은 보험료만 내는 셈이다.

## 7. 실행과 관측

### 무력화 변이

검사가 실제로 힘을 갖는지는 결함을 주입해 확인한다. 두 개를 넣었다.

**변이 1 — radix의 흩뿌리기를 뒤에서부터.** 각 pass가 안정이어야 전체가 정렬되는데, 역순으로 흩뿌리면 같은 키의 상대 순서가 pass마다 뒤집힌다. 최종 순서는 여전히 비내림차순이라 "정렬됨" 검사는 통과한다.

```cpp
// 주입한 결함
for (size_t z = n; z-- > 0;) { const Rec& r = (*src)[z]; (*dst)[count[(r.key >> shift) & mask]++] = r; }
```

```text
  ok   order_search_bounds
  FAIL order_sort_contract
  ok   order_ordered_key
  ok   order_selection
algo_tests: 1230 check(s) failed
```

`oracle::Stable`이 잡는다. tag가 없었다면 이 결함은 어떤 검사도 통과했을 것이다.

**변이 2 — `OrderedKey`의 부호 분기 제거.** `return u;`로 바꾸면 양수는 여전히 단조지만 음수가 전부 양수 위로 올라간다. `order_ordered_key`가 죽는다.

두 변이 모두 복원 뒤 소스 SHA-256이 주입 전과 동일함을 확인했다.

### 실행

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
.\build\Release\algo_tests.exe --step=01
```

관측 결과 — 이 단계가 추가한 네 검사가 이름과 함께 찍힌다. 실패하면 그 줄이 `FAIL`이 되고 어긋난 CHECK의 파일·줄 번호가 위에 함께 출력되며 종료 코드가 1이 된다.

```text
  ok   order_search_bounds
  ok   order_sort_contract
  ok   order_ordered_key
  ok   order_selection
algo_tests[step01]: 4/4 passed (856 check(s))
```

시드 무작위 property 층도 같은 계약을 훨씬 넓은 입력에서 확인한다.

```powershell
.\build\Release\algo_tests.exe --properties --step=01
```

```text
  ok   order/sort-contract          표본   900 · 비퇴화  76% · 건너뜀 0
  ok   order/select-agrees          표본   900 · 비퇴화  66% · 건너뜀 0
algo_tests[properties]: 2/2 passed (총 1800 표본, seed 20260824)
```

`비퇴화` 비율은 자명하게 통과하지 않은 표본의 몫이다. 이 값이 하한 아래로 떨어지면 property는 통과하면서도 아무것도 강제하지 않는 상태가 되므로, 하네스가 그 비율에도 단언을 건다.

측정 표는 별도 실행이다. `algo_bench`는 통과·실패로 갈리지 않으므로 검증이 아니다 — 다만 각 팔에 답 대조를 걸어 두어, 틀린 팔은 표에 오르기 전에 프로세스가 죽는다.

```powershell
.\build\Release\algo_bench.exe --sort
.\build\Release\algo_bench.exe --select
```

---

## 결론

순서·원소 보존·안정성 세 계약을 따로 검사하는 정렬 다섯 팔과 선택 네 팔을 만들었다. 계측기가 비교 횟수와 이동 바이트를 세고, 적대 입력 생성기가 각 알고리즘의 가정을 정확히 겨냥한 모양을 만든다.

측정이 보인 것은 점근이 답을 주지 않는 지점이다. radix의 최적 r은 점근이 가리키는 17이 아니라 8~11이고, 그 차이를 만드는 것은 히스토그램의 캐시 상주 여부다. counting sort의 판단 입력은 "정수 키인가"가 아니라 밀도 n/k이며, 그 경계는 이 기계에서 k≈10⁵ 근처에 섰다.

이 단계가 만든 것은 배열 하나를 정렬하는 능력이지, 스트림이나 외부 메모리나 병렬 정렬이 아니다. 키가 메모리에 다 들어간다는 전제가 모든 팔에 깔려 있고, 그 전제가 깨지는 규모는 여기서 다루지 않는다.
