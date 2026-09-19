## 들어가며

사전과 문자열 탐색을 짓는다. 해시 표는 분리 연쇄와 open addressing 세 정책(tombstone · backward shift · Robin Hood)으로, 문자열 탐색은 naive · KMP · Z · Boyer-Moore-Horspool · rolling hash 다섯 팔과 다중 패턴용 Aho-Corasick으로 나눈다. trie는 자식 표현 두 가지를 토글로 연다.

이 단계가 소유하는 계약은 둘이다. 첫째, `equal(a, b) => hash(a) == hash(b)`는 한 방향만 요구된다. 해시가 같은데 키가 다른 것은 결함이 아니라 정상이고, 표는 그것을 감당하도록 설계돼야 한다. 둘째, open addressing의 조회 종료 불변식 — 키가 표에 있으면 홈 슬롯부터 그 키의 실제 위치까지 *한 번도 쓰이지 않은* 슬롯이 없다. tombstone은 그 불변식을 지키기 위한 최소 장치다.

---

## 목차
1. [조회 종료 불변식](#1-조회-종료-불변식)
2. [삭제 정책 세 팔](#2-삭제-정책-세-팔)
3. [Bloom filter의 예측과 실측](#3-bloom-filter의-예측과-실측)
4. [단일 패턴 네 팔](#4-단일-패턴-네-팔)
5. [해시는 추측이다](#5-해시는-추측이다)
6. [Aho-Corasick의 세 간선](#6-aho-corasick의-세-간선)
7. [측정과 관측](#7-측정과-관측)

---

## 1. 조회 종료 불변식

open addressing의 조회는 Empty 슬롯을 만나면 멈춘다. 그 종료가 옳으려면 불변식이 필요하다 — 표에 있는 모든 키에 대해, 홈 슬롯부터 실제 위치까지의 경로에 Empty가 없어야 한다.

삭제가 슬롯을 Empty로 되돌리면 이 불변식이 깨진다. 지워진 키를 지나 앉아 있던 다른 키의 조회가 그 자리에서 끊긴다. tombstone은 "여기는 쓰인 적 있다"만 남겨 경로를 잇는다.

<!-- snippet: src/algo/dict.hpp@dict-open-policy -->
```cpp
// 삭제를 어떻게 처리하는가가 세 팔을 가른다.
//   Tombstone     지운 자리에 묘비를 남긴다. 조회는 묘비를 지나쳐 계속 간다.
//                 간단하지만 묘비가 쌓이면 조회가 실패해도 길어진다.
//   BackwardShift 지운 자리로 뒤쪽 항목을 당겨 온다. 묘비가 없어 조회가 깨끗하지만
//                 삭제가 클러스터 길이만큼 일한다.
//   RobinHood     넣을 때 "자기 홈에서 더 멀리 밀린 쪽" 에게 자리를 양보한다.
//                 probe 길이의 *분산* 이 줄어 P99 가 좋아진다.
enum class DeletePolicy { Tombstone, BackwardShift, RobinHood };
```

불변식을 말로 적어 두는 것으로는 부족하다. 표가 직접 확인할 수 있어야 한다.

<!-- snippet: src/algo/dict.hpp@dict-open-map mode=partial -->
```cpp
    // 조회 종료 불변식의 직접 검사. 살아 있는 모든 키에 대해, 홈부터 실제 위치까지
    // Empty 슬롯이 없어야 한다. 이 검사가 tombstone 을 EMPTY 로 되돌리는 결함을 잡는다.
    bool ProbeChainIntact() const {
        for (size_t at = 0; at < slot_.size(); ++at) {
            if (slot_[at].state != State::Used) continue;
            size_t i = size_t(slot_[at].hash & uint64_t(slot_.size() - 1));
            while (i != at) {
                if (slot_[i].state == State::Empty) return false;
                i = (i + 1) & (slot_.size() - 1);
            }
        }
        return true;
    }
```

검사는 400개 키를 넣고 절반을 지운 뒤 매번 이것을 부른다. 세 정책 모두, 세 가지 키 모양 모두에서.

## 2. 삭제 정책 세 팔

backward shift는 지운 자리로 뒤쪽 항목을 당겨 온다. 여기가 이 lab을 만들면서 실제로 결함이 나온 자리다.

<!-- snippet: src/algo/dict.hpp@dict-open-map mode=partial -->
```cpp
    // 지운 자리로 뒤쪽 항목을 당겨 온다.
    //
    // 당길 수 있는 항목은 "홈에서 볼 때 구멍이 지금 자리보다 가까운" 것뿐이다.
    // 홈이 (구멍, 현재자리] 구간 안에 있으면 그 항목은 내려올 수 없다 — 내려오면
    // 자기 홈보다 앞에 앉게 되어 자기 자신의 조회가 끊긴다.
    //
    // 중요한 것은 그런 항목을 만났을 때 **거기서 멈추지 않는다** 는 점이다.
    // 멈추고 구멍을 비우면, 그 항목보다 뒤에 있으면서 홈이 구멍보다 앞인 항목들이
    // 전부 조회 불가가 된다. 그래서 j 는 Empty 를 만날 때까지 계속 전진하고,
    // 옮길 수 있는 것을 만날 때만 구멍이 따라 이동한다.
    void BackwardShift(size_t hole) {
        const size_t m = slot_.size();
        size_t j = hole;
        for (size_t guard = 0; guard < m; ++guard) {
            j = (j + 1) & (m - 1);
            if (slot_[j].state != State::Used) break;   // 군집의 끝
            const size_t home = size_t(slot_[j].hash & uint64_t(m - 1));
            const size_t to_hole = (hole + m - home) & (m - 1);
            const size_t to_j    = (j + m - home) & (m - 1);
            if (to_hole > to_j) continue;               // 못 내려온다. 그러나 계속 훑는다.
            slot_[hole] = slot_[j];
            hole = j;
        }
        slot_[hole] = Slot{};
    }
```

첫 판본은 옮길 수 없는 항목을 만나면 거기서 멈추고 구멍을 비웠다. 한 걸음씩 보면 그것이 옳아 보인다 — 바로 다음 칸이 못 내려오면 더 볼 것이 없어 보이니까. 틀렸다. 그 항목보다 *뒤에* 있으면서 홈이 구멍보다 앞인 항목들이 남아 있고, 그것들의 조회 경로가 구멍에서 끊긴다.

`ProbeChainIntact`가 이것을 잡았다. 키 400개 중 삭제 4~8건 뒤에 불변식이 깨졌고, 그 뒤의 `Erase`가 실패했다. 만약 검사가 "지운 것은 없고 남은 것은 있다"만 봤다면 증상은 훨씬 나중에, 훨씬 이해하기 어려운 형태로 나타났을 것이다.

Robin Hood는 다른 각도로 접근한다. 삽입할 때 "자기 홈에서 더 멀리 밀린 쪽"에게 자리를 양보하면 probe 길이의 분산이 줄어든다. 평균은 크게 안 변하고 꼬리가 짧아진다 — 그것이 이 정책의 전부이고, 측정에서 P99로만 보인다.

## 3. Bloom filter의 예측과 실측

Bloom filter는 없다고 말하면 확실히 없고, 있다고 말하면 아닐 수도 있다. n개를 넣은 뒤 예측 거짓양성률은 `(1 - e^(-kn/m))^k`다.

<!-- snippet: src/algo/dict.hpp@dict-bloom mode=partial -->
```cpp
// Bloom filter. 없다고 말하면 확실히 없고, 있다고 말하면 아닐 수도 있다.
// n 개를 넣은 뒤 예측 거짓양성률은 (1 - e^(-k n / m))^k 다.
// k 개의 독립 해시 대신 두 해시의 선형 결합(g_i = h1 + i*h2)을 쓴다 — 이론적
// FP 율을 거의 그대로 유지하면서 해시 계산이 둘로 준다.
class BloomFilter {
public:
    // ...
    void Add(const std::string& key) {
        const uint64_t h1 = HashKey(key, seed_);
        const uint64_t h2 = HashKey(key, seed_ ^ 0xA5A5A5A5A5A5A5A5ull) | 1ull;
        for (int i = 0; i < k_; ++i) {
            const size_t bit = size_t((h1 + uint64_t(i) * h2) % uint64_t(m_));
            word_[bit / 64] |= (1ull << (bit % 64));
        }
        ++inserted_;
    }
    // ...
    // 예측 FP 율. 실측과 이 값을 나란히 두는 것이 이 자료구조의 학습 지점이다.
    double PredictedFalsePositiveRate() const {
        const double m = double(m_), n = double(inserted_), k = double(k_);
        const double filled = 1.0 - std::exp(-k * n / m);
        double r = 1.0;
        for (int i = 0; i < k_; ++i) r *= filled;
        return r;
    }
    // ...
};
```

검사는 넣은 것이 전부 "있다"로 나오는지(거짓음성 부재)를 먼저 보고, 안 넣은 4,000개에서 실측 FP율이 예측의 3배 안에 드는지 본다. 예측은 해시가 완전히 독립일 때의 값이고 구현은 두 해시의 선형 결합을 쓰므로 정확히 같을 수 없다 — 검사는 그 사실을 반영해 배수로 경계를 잡는다.

## 4. 단일 패턴 네 팔

KMP의 prefix function은 "지금까지 맞춘 접두사 중 다시 쓸 수 있는 가장 긴 것"을 준다.

<!-- snippet: src/algo/text.hpp@text-kmp-prefix -->
```cpp
// prefix function. pi[i] = pat[0..i] 의 접두사이면서 접미사인 가장 긴 *진* 문자열의 길이.
// 계산 자체가 KMP 를 패턴에 대해 돌리는 것과 같다 — 그래서 O(m) 이다.
inline std::vector<size_t> PrefixFunction(const std::string& pat) {
    std::vector<size_t> pi(pat.size(), 0);
    for (size_t i = 1; i < pat.size(); ++i) {
        size_t k = pi[i - 1];
        // 불일치면 더 짧은 경계로 물러난다. k 는 절대 늘지 않으므로 총 감소량이
        // 총 증가량을 넘지 못한다 — 이것이 상각 선형의 근거다.
        while (k > 0 && pat[i] != pat[k]) k = pi[k - 1];
        if (pat[i] == pat[k]) ++k;
        pi[i] = k;
    }
    return pi;
}

inline std::vector<size_t> KmpSearch(const std::string& text, const std::string& pat,
                                     measure::Counters* c = nullptr) {
    std::vector<size_t> hits;
    if (pat.empty() || pat.size() > text.size()) return hits;
    const std::vector<size_t> pi = PrefixFunction(pat);
    size_t k = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        while (k > 0 && text[i] != pat[k]) {
            if (c) ++c->comparisons;
            k = pi[k - 1];
        }
        if (c) ++c->comparisons;
        if (text[i] == pat[k]) ++k;
        if (k == pat.size()) {
            hits.push_back(i + 1 - pat.size());
            k = pi[k - 1];              // 겹치는 일치도 놓치지 않는다
        }
    }
    return hits;
}
```

일치 후 `k = 0`으로 리셋하면 겹치는 일치를 놓친다. `"aaaaaaaaaa"`에서 `"aaa"`를 찾으면 8개가 나와야 하는데 3개만 나온다. 검사 목록에 그 입력이 들어 있다.

BMH는 정반대 전략이다. 창의 마지막 문자로 이동 폭을 정한다.

<!-- snippet: src/algo/text.hpp@text-bmh -->
```cpp
// Boyer-Moore-Horspool. 창의 마지막 문자로 이동 폭을 정한다.
// shift[c] = 패턴의 *마지막 문자를 제외한* 부분에서 c 가 나오는 가장 오른쪽 위치
// 기준의 거리. 마지막 문자를 포함하면 이동 폭이 0 이 되어 제자리를 맴돈다.
inline std::vector<size_t> BmhSearch(const std::string& text, const std::string& pat,
                                     measure::Counters* c = nullptr) {
    std::vector<size_t> hits;
    const size_t m = pat.size(), n = text.size();
    if (m == 0 || m > n) return hits;
    size_t shift[256];
    for (size_t i = 0; i < 256; ++i) shift[i] = m;
    for (size_t i = 0; i + 1 < m; ++i) shift[uint8_t(pat[i])] = m - 1 - i;

    size_t at = 0;
    while (at + m <= n) {
        size_t j = m;
        while (j > 0) {
            if (c) ++c->comparisons;
            if (text[at + j - 1] != pat[j - 1]) break;
            --j;
        }
        if (j == 0) hits.push_back(at);
        at += shift[uint8_t(text[at + m - 1])];
    }
    return hits;
}
```

`i + 1 < m`으로 마지막 문자를 제외하는 것이 핵심이다. 포함하면 그 문자에 대한 shift가 0이 되어 창이 전진하지 못한다.

네 팔은 전부 같은 위치 목록을 내야 한다. 오라클은 위치마다 바이트를 직접 비교하는 구현이다 — 제품 구현과 골격이 전혀 달라 같은 오해를 공유하지 않는다.

## 5. 해시는 추측이다

rolling hash는 답을 추측한다. 해시가 같다는 것은 후보라는 뜻일 뿐이다.

<!-- snippet: src/algo/text.hpp@text-rolling-hash mode=partial -->
```cpp
// Rabin-Karp. 해시가 같다는 것은 *후보* 라는 뜻일 뿐이다. 바이트 확인을 빼면
// 충돌 하나가 곧 오보가 된다 — 검사가 그 충돌을 실제로 만들어 확인한다.
class RollingHash {
public:
    // ...
        for (size_t at = 0;; ++at) {
            if (c) ++c->probes;
            if (window == target) {
                if (!verify_bytes) {
                    hits.push_back(at);                       // ← 이 분기가 결함이다
                } else {
                    bool same = true;
                    for (size_t j = 0; j < m; ++j) {
                        if (c) ++c->comparisons;
                        if (text[at + j] != pat[j]) { same = false; break; }
                    }
                    if (same) hits.push_back(at);
                }
            }
            if (at + m >= n) break;
            // 앞 글자를 빼고 뒤 글자를 넣는다. 모듈러 뺄셈이 음수가 되지 않도록
            // mod_ 를 한 번 더한다.
            window = (window + mod_ - (uint8_t(text[at]) * power) % mod_) % mod_;
            window = (window * base_ + uint8_t(text[at + m])) % mod_;
        }
        return hits;
    }
    // ...
};
```

"확인이 필요하다"를 문장으로 적는 대신 반례로 만든다. base=2, mod=7로 좁히면 해시 값이 7가지뿐이라 충돌이 흔해진다. 그 파라미터가 곧 반례 생성기다.

```cpp
RollingHash weak(2, 7);
const std::string text = "abcdefghijklmnopqrstuvwxyz0123456789";
const std::string pat = "xyz";
const std::vector<size_t> truth = oracle::SlowSearch(text, pat);
CHECK(weak.Search(text, pat, true) == truth);
// 확인을 끈 쪽은 오보를 낸다. 그것이 이 검사가 단언하는 바다.
CHECK(weak.Search(text, pat, false).size() > truth.size());
```

넓은 파라미터에서는 두 팔이 실측상 같다. 그것은 "충돌이 드물다"는 뜻이지 "없다"는 뜻이 아니고, 위 반례가 그 구분을 확정한다.

## 6. Aho-Corasick의 세 간선

Aho-Corasick에는 세 종류의 간선이 있다. goto는 문자를 소비하며 내려가고, failure는 불일치했을 때 "지금 읽은 접미사 중 trie에 있는 가장 긴 것"으로 옮기고, output link는 그 상태에서 끝나는 다른 패턴들로 이어진다.

<!-- snippet: src/algo/text.hpp@text-aho mode=partial -->
```cpp
    // BFS 순서로 failure 를 채운다. 부모의 failure 가 먼저 확정돼야 하므로
    // 깊이 순서가 필수다.
    void Build() {
        if (node_.empty()) node_.push_back(Node{});
        std::deque<size_t> queue;
        for (int k = 0; k < 256; ++k) {
            const size_t child = node_[0].next[k];
            if (child == kNil) { node_[0].next[k] = 0; continue; }
            node_[child].fail = 0;
            queue.push_back(child);
        }
        while (!queue.empty()) {
            const size_t at = queue.front();
            queue.pop_front();
            // output link: 자기 failure 사슬에서 가장 가까운 종료 상태
            const size_t f = node_[at].fail;
            node_[at].out_link = (node_[f].output != kNone) ? f : node_[f].out_link;
            for (int k = 0; k < 256; ++k) {
                const size_t child = node_[at].next[k];
                if (child == kNil) {
                    node_[at].next[k] = node_[node_[at].fail].next[k];  // goto 자동화
                    continue;
                }
                node_[child].fail = node_[node_[at].fail].next[k];
                queue.push_back(child);
            }
        }
        built_ = true;
    }
```

failure link를 전부 root로 두어도 코드는 컴파일되고 대부분의 입력에서 답이 맞는다. 틀리는 것은 root가 아닌 failure link가 실제로 쓰이는 입력 — 어떤 접두의 진접미사가 다시 trie에 있는 경우(패턴이 서로의 접미사이거나 한 패턴 안에 자기 겹침이 있을 때)뿐이다. 그래서 검사가 반드시 그런 입력을 포함해야 한다.

```cpp
const std::vector<std::string> pats = {"he", "she", "his", "hers", "s"};
const std::string text = "ushers his shell here";
```

`she` 안에 `he`가 있고 `his` 안에 `he`는 없지만 `hers` 안에 `he`가 있으며 `s`는 `his`·`hers`의 접미사다. 오라클은 패턴별로 따로 찾아 (끝 위치, 패턴 번호) multiset을 만들고, Aho의 결과와 정렬 후 비교한다.

검사는 하나를 더 단언한다 — 패턴 수를 늘려도 텍스트를 한 번만 지나간다는 것이다. 상태 방문 수가 정확히 `|text|`여야 한다.

## 7. 측정과 관측

### 해시 표 네 팔

`algo_bench --dict`는 키 20만 개를 넣고 절반을 지운 뒤 조회 하나하나의 probe 길이를 모아 평균과 P99를 낸다. 삭제가 없으면 tombstone과 backward-shift는 같은 숫자를 내고(Robin Hood는 삽입 시 재배치와 조기 종료 때문에 삭제가 없어도 갈린다), 두 삭제 정책을 가르는 것은 삭제이므로 삭제가 이 표의 전제다.

| 요청 load | 실제 load | arm | hit avg | hit P99 | miss avg | miss P99 | cluster |
|---|---|---|---|---|---|---|---|
| 0.5 | 0.191 | linear+tomb | 1.30 | 5 | 2.88 | 10 | 24 |
| 0.5 | 0.191 | linear+shift | 1.12 | 3 | 1.26 | 4 | 10 |
| 0.5 | 0.191 | robinhood | 1.12 | 2 | 1.21 | 3 | 10 |
| 0.9 | 0.381 | linear+tomb | 2.60 | 22 | 12.24 | 73 | 164 |
| 0.9 | 0.381 | linear+shift | 1.31 | 5 | 1.81 | 8 | 19 |
| 0.9 | 0.381 | robinhood | 1.31 | 3 | 1.50 | 4 | 19 |
| 0.9 | 0.381 | chaining | 1.19 | 3 | 0.38 | 2 | 5 |

읽을 것이 셋이다.

**요청 load와 실제 load가 다르다.** 표 크기가 2의 거듭제곱이므로 용량이 양자화된다. 0.5 요청과 0.7 요청이 같은 용량(524,288)으로 접혀 완전히 같은 행을 낸다. 이것은 측정 오류가 아니라 구현의 성질이고, 요청값만 찍었다면 "0.5와 0.7의 차이가 없다"는 잘못된 결론을 낼 뻔했다.

**tombstone의 대가는 실패 조회에 몰린다.** load 0.9에서 miss 평균이 12.24, P99가 73이다. 성공 조회는 2.60/22로 훨씬 낫다. 이유는 명확하다 — 성공 조회는 키를 찾으면 멈추지만 실패 조회는 묘비를 전부 지나 Empty까지 가야 한다. 삭제가 잦은 표에서 tombstone을 고르면 실패 조회가 서서히 비싸진다.

**Robin Hood의 이득은 평균이 아니라 꼬리다.** hit 평균은 backward shift와 똑같이 1.31인데 P99는 5 대 3이다. miss P99는 8 대 4다. 분산을 줄이는 정책이므로 평균만 보는 표에서는 이 정책을 고를 이유가 보이지 않는다.

### 문자열 탐색

| alphabet | pat_len | arm | ms | compares |
|---|---|---|---|---|
| 2 | 4 | naive | 12.62 | 3,752,511 |
| 2 | 4 | kmp | 12.52 | 2,624,493 |
| 2 | 4 | bmh | 8.76 | 1,700,075 |
| 26 | 32 | naive | 3.28 | 2,079,992 |
| 26 | 32 | kmp | 3.14 | 2,076,961 |
| 26 | 32 | bmh | 0.54 | 114,751 |

알파벳이 2일 때 KMP는 naive보다 비교를 30% 덜 하지만 시간은 같다. 되돌아가지 않는 것의 이득이 실패 함수 조회 비용에 상쇄된다. 알파벳이 26이고 패턴이 32자면 BMH가 naive의 6배 빠르다 — 비교 횟수가 18분의 1이기 때문이고, 그것은 불일치 문자로 창을 최대 32칸, 무작위 텍스트에서 평균 약 18.6칸(26·(1−(25/26)^32))씩 뛰어넘기 때문이다.

Z 배열 팔은 이 구현에서 가장 느리다(10ms 내외). 패턴과 텍스트를 이어 붙인 새 문자열을 만들고 그 전체의 Z 배열을 계산하므로, 시간뿐 아니라 메모리도 `|text| + |pat|`만큼 더 쓴다. Z 배열의 값은 다른 문제(주기 검출 등)에 있고 단일 패턴 검색만 보면 KMP를 이길 이유가 없다.

다중 패턴의 교차점은 훨씬 선명하다.

| p (패턴 수) | 패턴별 KMP ms | Aho ms | Aho 상태 수 |
|---|---|---|---|
| 1 | 2.20 | 1.33 | 7 |
| 8 | 18.25 | 1.32 | 44 |
| 64 | 138.83 | 1.50 | 248 |

KMP는 패턴 수에 선형이고 Aho는 거의 평평하다. p=1에서 이미 Aho가 이기는 것은 텍스트가 50만 자라 상태 표 구축 비용이 묻히기 때문이다.

### 무력화 변이

**변이 1 — Aho-Corasick의 failure link를 전부 root로.** `node_[child].fail = 0;`으로 바꿨다.

```text
  ok   dict_open_addressing
  ok   dict_bloom_filter
  ok   text_single_pattern
  ok   text_rolling_hash_verify
  FAIL text_aho_corasick
  ok   text_trie_arms
```

단일 패턴 검사는 살아남는다. 그 검사가 KMP·Z·BMH·rolling hash만 돌리고 Aho-Corasick을 부르지 않기 때문이다. 패턴이 하나여도 `issi`/`mississippi`처럼 자기 겹침이 있으면 root 복귀는 틀리며, 그 경우는 `text_aho_corasick` 안의 단일 패턴 블록이 함께 잡는다.

**변이 2 — rolling hash의 바이트 확인 제거.** `if (!verify_bytes)` 조건을 `if (true)`로 바꿔 확인 분기를 죽였다. `text_rolling_hash_verify`가 죽는다 — 확인을 켠 팔이 오보를 내기 시작하므로 `verified == truth` 단정이 깨진다.

두 변이 모두 복원 뒤 소스 SHA-256이 주입 전과 동일함을 확인했다.

### 실행

```powershell
cmake --build build --config Release
.\build\Release\algo_tests.exe --step=03
```

```text
  ok   dict_open_addressing
  ok   dict_bloom_filter
  ok   text_single_pattern
  ok   text_rolling_hash_verify
  ok   text_aho_corasick
  ok   text_trie_arms
algo_tests[step03]: 6/6 passed (22529 check(s))
```

```powershell
.\build\Release\algo_tests.exe --properties --step=03
```

```text
  ok   dict/open-lookup             표본   500 · 비퇴화  63% · 건너뜀 0
  ok   text/search-agrees           표본  1400 · 비퇴화  40% · 건너뜀 0
algo_tests[properties]: 2/2 passed (총 1900 표본, seed 20260824)
```

측정 표는 `.\build\Release\algo_bench.exe --dict`와 `--text`로 따로 낸다.

---

## 결론

open addressing의 조회 종료 불변식이 이 단계의 중심이었다. 그 불변식을 표가 스스로 검사할 수 있게 만든 것이 backward shift 구현의 실제 결함을 잡았다 — 한 걸음만 보고 멈추는 판본은 군집 뒤쪽 항목들의 조회 경로를 끊었고, "지운 것은 없고 남은 것은 있다"만 보는 검사로는 그것이 훨씬 늦게 드러났을 것이다.

삭제 정책 세 팔의 차이는 평균이 아니라 꼬리에 있다. tombstone은 실패 조회 P99가 load 0.9에서 73까지 갔고, Robin Hood는 같은 평균에서 P99를 4로 눌렀다. 평균만 찍는 표는 이 정책들을 구분하지 못한다.

문자열 쪽에서는 rolling hash가 확인 없이는 오보를 낸다는 것을 좁은 파라미터로 실증했다. 다중 패턴에서 Aho-Corasick과 패턴별 KMP의 교차점은 패턴 하나 근처에 있었다.

여기서 다루지 않은 것은 suffix automaton과 suffix array 계열이다. 그것들은 텍스트를 한 번 전처리해 두고 임의 패턴을 빠르게 답하는 구조인데, 이 lab의 팔은 전부 패턴을 먼저 알고 텍스트를 지나가는 형태다. 전처리 대상이 반대다.
