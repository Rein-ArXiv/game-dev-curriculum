// workload.hpp — 적대 입력 생성기.
//
// 무작위 입력은 알고리즘의 *평균* 만 보여 준다. 이 lab 이 알고 싶은 것은 평균이
// 아니라 "어떤 입력에서 무너지는가" 이므로, 각 알고리즘의 가정을 정확히 겨냥한
// 입력을 손으로 만든다 — 정렬됨 · 역순 · 전부 동일 · pivot 불균형 · 충돌 해시 ·
// Hall 조건을 한 subset 에서만 위반하는 이분 그래프 같은 것들.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "algo/order.hpp"

namespace measure {

// 결정론적 난수원. 실행마다 같은 워크로드가 나와야 두 팔의 비교가 성립한다.
class Rng {
public:
    explicit Rng(uint64_t seed) : s_(seed ? seed : 0x9e3779b97f4a7c15ull) {}
    uint64_t Next() {
        s_ ^= s_ << 13; s_ ^= s_ >> 7; s_ ^= s_ << 17;
        return s_;
    }
    uint32_t Below(uint32_t n) { return n ? uint32_t(Next() % n) : 0; }
    double Unit() { return double(Next() >> 11) * (1.0 / 9007199254740992.0); }

private:
    uint64_t s_;
};

// snippet-begin: workload-sort-shapes
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

inline std::vector<algo::Rec> MakeSortInput(SortShape shape, size_t n, uint64_t seed) {
    Rng rng(seed);
    std::vector<algo::Rec> a(n);
    for (size_t i = 0; i < n; ++i) a[i].tag = uint32_t(i);
    switch (shape) {
        case SortShape::Random:
            for (size_t i = 0; i < n; ++i) a[i].key = uint32_t(rng.Next());
            break;
        case SortShape::Sorted:
            for (size_t i = 0; i < n; ++i) a[i].key = uint32_t(i) * 3u;
            break;
        case SortShape::Reversed:
            for (size_t i = 0; i < n; ++i) a[i].key = uint32_t(n - i) * 3u;
            break;
        case SortShape::AllEqual:
            for (size_t i = 0; i < n; ++i) a[i].key = 42u;
            break;
        case SortShape::TwoValues:
            for (size_t i = 0; i < n; ++i) a[i].key = (i & 1u) ? 7u : 900001u;
            break;
        case SortShape::Sawtooth:
            for (size_t i = 0; i < n; ++i) a[i].key = uint32_t(i % 64) * 1000u;
            break;
        case SortShape::OrganPipe:
            for (size_t i = 0; i < n; ++i)
                a[i].key = uint32_t((i < n / 2) ? i : (n - i)) * 2u;
            break;
        case SortShape::HighDigitSame:
            // 상위 16비트를 고정한다. radix r=8 이면 마지막 두 pass 가 항등이다.
            for (size_t i = 0; i < n; ++i) a[i].key = 0x00AB0000u | (uint32_t(rng.Next()) & 0xFFFFu);
            break;
        case SortShape::FewKeys:
            for (size_t i = 0; i < n; ++i) a[i].key = rng.Below(16);
            break;
        case SortShape::ManyKeys:
            for (size_t i = 0; i < n; ++i) a[i].key = uint32_t(rng.Next() & 0x3FFFFFFFu);
            break;
    }
    return a;
}

inline const char* SortShapeName(SortShape s) {
    switch (s) {
        case SortShape::Random:        return "random";
        case SortShape::Sorted:        return "sorted";
        case SortShape::Reversed:      return "reversed";
        case SortShape::AllEqual:      return "all-equal";
        case SortShape::TwoValues:     return "two-values";
        case SortShape::Sawtooth:      return "sawtooth";
        case SortShape::OrganPipe:     return "organ-pipe";
        case SortShape::HighDigitSame: return "high-digit-same";
        case SortShape::FewKeys:       return "few-keys";
        case SortShape::ManyKeys:      return "many-keys";
    }
    return "?";
}
// snippet-end: workload-sort-shapes

// snippet-begin: workload-float-keys
// 실수 키의 경계값 모음. NaN · ±0 · ±inf · 비정규 수가 섞여야 순서 보존 변환이
// 실제로 검사된다. 이 목록에 -0.0 과 +0.0 이 둘 다 있는 것이 핵심이다.
inline std::vector<float> AdversarialFloats() {
    return {
        0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f,
        1e-30f, -1e-30f, 1e30f, -1e30f,
        3.4028235e38f, -3.4028235e38f,
        1.1754944e-38f, -1.1754944e-38f,
        1.4e-45f, -1.4e-45f,
        123.456f, -123.456f, 2.0f, -2.0f, 1024.0f, -1024.0f,
    };
}
// snippet-end: workload-float-keys

// snippet-begin: workload-hash-keys
// 해시 표를 겨냥한 키 집합. Colliding 은 표 크기가 2의 거듭제곱일 때 같은 홈
// 슬롯으로 몰리도록 만들어진 것이 아니라 — seed 를 모르면 그럴 수 없다 —
// *같은 접두를 공유하는* 키를 대량으로 만들어 문자열 비교 비용을 겨냥한다.
enum class KeyShape { Random, Sequential, CommonPrefix, Colliding };

// 생성기의 계약: 돌려주는 키는 **서로 다르다**.
// 이것이 계약이어야 하는 이유는 소비자 쪽에 있다. "n 개를 넣고 절반을 지우면
// size 가 n/2 다" 같은 단정은 키가 전부 다를 때만 성립한다. 무작위 문자열은
// n 이 커지면 생일 문제로 실제로 부딪히므로(길이 6 짜리 키 2만 개면 충돌 기댓값이
// 1 근처다) 여기서 한 번 걸러 준다. 충돌한 키에는 구분자를 붙여 결정성을 지킨다.
inline std::vector<std::string> MakeKeys(KeyShape shape, size_t n, uint64_t seed) {
    Rng rng(seed);
    std::vector<std::string> keys;
    std::set<std::string> seen;
    keys.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        switch (shape) {
            case KeyShape::Random: {
                std::string s;
                const size_t len = 6 + size_t(rng.Below(10));
                for (size_t k = 0; k < len; ++k) s.push_back(char('a' + rng.Below(26)));
                keys.push_back(s);
                break;
            }
            case KeyShape::Sequential:
                keys.push_back("key" + std::to_string(i));
                break;
            case KeyShape::CommonPrefix:
                keys.push_back(std::string(24, 'x') + std::to_string(i));
                break;
            case KeyShape::Colliding:
                // 같은 홈 슬롯을 노리는 대신 *짧은 알파벳* 으로 서로 비슷하게 만든다.
                keys.push_back(std::string(3, char('a' + (i % 3))) +
                               std::to_string(i % 8) + std::to_string(i));
                break;
        }
        if (!seen.insert(keys.back()).second) {
            keys.back() += "~" + std::to_string(i);       // 충돌을 결정적으로 푼다
            seen.insert(keys.back());
        }
    }
    return keys;
}
// snippet-end: workload-hash-keys

// snippet-begin: workload-bipartite
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

// 완전 매칭이 하나뿐인 그래프 — 왼쪽 i 는 오른쪽 i 와 i+1 만 본다(마지막은 i 만).
inline Bipartite UniquePerfectBipartite(size_t k) {
    Bipartite b;
    b.left = k;
    b.right = k;
    b.adj.assign(k, std::vector<uint32_t>());
    for (uint32_t u = 0; u < k; ++u) {
        b.adj[u].push_back(u);
        if (u + 1 < k) b.adj[u].push_back(u + 1);
    }
    b.known_max = k;
    return b;
}

inline Bipartite RandomBipartite(size_t left, size_t right, double density, uint64_t seed) {
    Rng rng(seed);
    Bipartite b;
    b.left = left;
    b.right = right;
    b.adj.assign(left, std::vector<uint32_t>());
    for (uint32_t u = 0; u < left; ++u)
        for (uint32_t v = 0; v < right; ++v)
            if (rng.Unit() < density) b.adj[u].push_back(v);
    return b;
}
// snippet-end: workload-bipartite

// snippet-begin: workload-corpus
// 압축 코퍼스 8종. 압축률은 데이터의 성질이지 알고리즘의 성질이 아니라는 것을
// 이 여덟이 보인다 — 난수 입력에서는 어떤 무손실 압축기도 *커진다*.
enum class Corpus {
    Text, StructuredBinary, IntArray, FloatArray,
    AlreadyCompressed, TinyMessage, HighlyRepetitive, Random
};

inline std::vector<uint8_t> MakeCorpus(Corpus kind, size_t target, uint64_t seed) {
    Rng rng(seed);
    std::vector<uint8_t> out;
    out.reserve(target);
    switch (kind) {
        case Corpus::Text: {
            static const char* words[] = {
                "the ", "quick ", "brown ", "fox ", "jumps ", "over ", "lazy ", "dog ",
                "and ", "then ", "runs ", "away ", "into ", "forest ", "again ", "\n"};
            while (out.size() < target) {
                const char* w = words[rng.Below(16)];
                for (const char* p = w; *p; ++p) out.push_back(uint8_t(*p));
            }
            break;
        }
        case Corpus::StructuredBinary: {
            // 16바이트 레코드: id(4) 고정 태그(4) 좌표(4) 패딩(4)
            uint32_t id = 0;
            while (out.size() < target) {
                const uint32_t fields[4] = {id++, 0xDEADBEEFu, uint32_t(rng.Below(1000)), 0};
                for (int f = 0; f < 4; ++f)
                    for (int b = 0; b < 4; ++b) out.push_back(uint8_t(fields[f] >> (8 * b)));
            }
            break;
        }
        case Corpus::IntArray: {
            int32_t value = 0;
            while (out.size() < target) {
                value += int32_t(rng.Below(8)) - 3;      // 작은 증분 — 상위 바이트가 안 변한다
                for (int b = 0; b < 4; ++b) out.push_back(uint8_t(uint32_t(value) >> (8 * b)));
            }
            break;
        }
        case Corpus::FloatArray: {
            float value = 1.0f;
            while (out.size() < target) {
                value = value * 1.0001f + 0.5f;
                uint32_t bits;
                std::memcpy(&bits, &value, 4);
                for (int b = 0; b < 4; ++b) out.push_back(uint8_t(bits >> (8 * b)));
            }
            break;
        }
        case Corpus::AlreadyCompressed:
        case Corpus::Random:
            while (out.size() < target) out.push_back(uint8_t(rng.Next()));
            break;
        case Corpus::TinyMessage: {
            const char* msg = "{\"ok\":true,\"n\":7}";
            for (const char* p = msg; *p; ++p) out.push_back(uint8_t(*p));
            break;
        }
        case Corpus::HighlyRepetitive:
            while (out.size() < target) out.push_back(uint8_t('A' + (out.size() % 4)));
            break;
    }
    if (kind != Corpus::TinyMessage && out.size() > target) out.resize(target);
    return out;
}

inline const char* CorpusName(Corpus c) {
    switch (c) {
        case Corpus::Text:              return "text";
        case Corpus::StructuredBinary:  return "struct-bin";
        case Corpus::IntArray:          return "int-array";
        case Corpus::FloatArray:        return "float-array";
        case Corpus::AlreadyCompressed: return "precompressed";
        case Corpus::TinyMessage:       return "tiny-msg";
        case Corpus::HighlyRepetitive:  return "repetitive";
        case Corpus::Random:            return "random";
    }
    return "?";
}
// snippet-end: workload-corpus

}  // namespace measure
