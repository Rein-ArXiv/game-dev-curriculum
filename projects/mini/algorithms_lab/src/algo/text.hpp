// text.hpp — 문자열 탐색.
//
// 네 알고리즘이 같은 답을 서로 다른 이유로 낸다.
//   KMP    실패 함수가 "지금까지 맞춘 접두사 중 다시 쓸 수 있는 가장 긴 것" 을 준다.
//   Z      각 위치에서 원본과 일치하는 길이. 같은 정보를 다른 좌표계로 적은 것이다.
//   BMH    불일치 문자로 건너뛴다. 최악은 나쁘지만 알파벳이 크면 실측이 가장 빠르다.
//   Aho    패턴이 여러 개일 때 입력을 한 번만 지나간다.
// 그리고 rolling hash 는 답을 *추측* 한다 — 그래서 확인이 필수다.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "measure/counters.hpp"

namespace algo {

// ------------------------------------------------------------------- naive

inline std::vector<size_t> NaiveSearch(const std::string& text, const std::string& pat,
                                       measure::Counters* c = nullptr) {
    std::vector<size_t> hits;
    if (pat.empty() || pat.size() > text.size()) return hits;
    for (size_t i = 0; i + pat.size() <= text.size(); ++i) {
        size_t j = 0;
        while (j < pat.size()) {
            if (c) ++c->comparisons;
            if (text[i + j] != pat[j]) break;
            ++j;
        }
        if (j == pat.size()) hits.push_back(i);
    }
    return hits;
}

// --------------------------------------------------------------------- KMP

// snippet-begin: text-kmp-prefix
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
// snippet-end: text-kmp-prefix

// ----------------------------------------------------------------------- Z

// z[i] = s 와 s[i..] 의 최장 공통 접두사 길이. z[0] 은 정의상 |s| 로 둔다.
inline std::vector<size_t> ZArray(const std::string& s) {
    const size_t n = s.size();
    std::vector<size_t> z(n, 0);
    if (n == 0) return z;
    z[0] = n;
    size_t l = 0, r = 0;   // 현재 알고 있는 가장 오른쪽 일치 구간 [l, r)
    for (size_t i = 1; i < n; ++i) {
        if (i < r) z[i] = std::min(r - i, z[i - l]);
        while (i + z[i] < n && s[z[i]] == s[i + z[i]]) ++z[i];
        if (i + z[i] > r) { l = i; r = i + z[i]; }
    }
    return z;
}

inline std::vector<size_t> ZSearch(const std::string& text, const std::string& pat) {
    std::vector<size_t> hits;
    if (pat.empty() || pat.size() > text.size()) return hits;
    // 패턴에 절대 나오지 않는 구분자가 필요하다. 0 바이트를 쓴다.
    const std::string joined = pat + '\0' + text;
    const std::vector<size_t> z = ZArray(joined);
    for (size_t i = pat.size() + 1; i < joined.size(); ++i) {
        if (z[i] >= pat.size()) hits.push_back(i - pat.size() - 1);
    }
    return hits;
}

// ------------------------------------------------------------------- BMH

// snippet-begin: text-bmh
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
// snippet-end: text-bmh

// ------------------------------------------------------------------- trie

// snippet-begin: text-trie
// 자식 표현 두 가지를 한 클래스에 담고 토글한다. 소문자 26 자면 고정 배열이
// 노드당 104 바이트를 쓰지만 분기가 O(1) 이고, 정렬 소배열은 실제 자식 수만큼만
// 쓰지만 이진 탐색이 붙는다. 어느 쪽이 이기는지는 *분기 계수* 가 정한다.
class Trie {
public:
    explicit Trie(bool dense_children) : dense_(dense_children) { node_.push_back(Node{}); }

    void Insert(const std::string& word) {
        size_t at = 0;
        for (char ch : word) {
            const int k = Index(ch);
            if (k < 0) return;
            size_t next = Child(at, k);
            if (next == kNil) {
                node_.push_back(Node{});
                next = node_.size() - 1;
                SetChild(at, k, next);
            }
            at = next;
        }
        node_[at].terminal = true;
    }

    bool Contains(const std::string& word, measure::Counters* c = nullptr) const {
        size_t at = 0;
        for (char ch : word) {
            const int k = Index(ch);
            if (k < 0) return false;
            if (c) ++c->probes;
            at = Child(at, k);
            if (at == kNil) return false;
        }
        return node_[at].terminal;
    }

    // 접두사로 시작하는 단어가 하나라도 있는가. trie 가 해시 표와 다른 지점이다.
    bool HasPrefix(const std::string& prefix, measure::Counters* c = nullptr) const {
        size_t at = 0;
        for (char ch : prefix) {
            const int k = Index(ch);
            if (k < 0) return false;
            if (c) ++c->probes;
            at = Child(at, k);
            if (at == kNil) return false;
        }
        return true;
    }

    size_t NodeCount() const { return node_.size(); }
    // 노드당 자식 저장 바이트. 두 표현의 공간 가격을 그대로 낸다.
    size_t ChildBytes() const {
        if (dense_) return node_.size() * 26 * sizeof(size_t);
        size_t total = 0;
        for (const Node& n : node_) total += n.sparse.size() * sizeof(Edge);
        return total;
    }

private:
    static const size_t kNil = size_t(-1);
    struct Edge { uint8_t k; size_t to; };
    struct Node {
        bool   terminal = false;
        size_t dense[26];
        std::vector<Edge> sparse;
        Node() { for (int i = 0; i < 26; ++i) dense[i] = kNil; }
    };

    static int Index(char ch) {
        if (ch >= 'a' && ch <= 'z') return ch - 'a';
        return -1;
    }
    size_t Child(size_t at, int k) const {
        if (dense_) return node_[at].dense[k];
        const std::vector<Edge>& e = node_[at].sparse;
        size_t lo = 0, hi = e.size();
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            if (e[mid].k < uint8_t(k)) lo = mid + 1; else hi = mid;
        }
        if (lo < e.size() && e[lo].k == uint8_t(k)) return e[lo].to;
        return kNil;
    }
    void SetChild(size_t at, int k, size_t to) {
        if (dense_) { node_[at].dense[k] = to; return; }
        std::vector<Edge>& e = node_[at].sparse;
        size_t lo = 0;
        while (lo < e.size() && e[lo].k < uint8_t(k)) ++lo;
        e.insert(e.begin() + std::ptrdiff_t(lo), Edge{uint8_t(k), to});
    }

    bool dense_;
    std::vector<Node> node_;
};
// snippet-end: text-trie

// ------------------------------------------------------------- Aho-Corasick

// snippet-begin: text-aho
// Aho-Corasick. 세 종류의 간선이 있다.
//   goto     문자를 소비하며 내려간다.
//   failure  불일치했을 때 "지금 읽은 접미사 중 trie 에 있는 가장 긴 것" 으로 옮긴다.
//   output   그 상태에서 끝나는 *다른* 패턴들로 이어지는 사슬.
// failure link 를 전부 root 로 두면 여전히 컴파일되고 대부분의 입력에서 답이
// 맞는다 — 패턴이 서로의 접미사인 입력에서만 틀린다. 그래서 검사가 그 입력을
// 반드시 포함해야 한다.
class AhoCorasick {
public:
    struct Hit { size_t end_pos; size_t pattern_id; };

    void Add(const std::string& pat) {
        if (pat.empty()) { ++pattern_count_; return; }
        if (node_.empty()) node_.push_back(Node{});
        size_t at = 0;
        for (char ch : pat) {
            const uint8_t k = uint8_t(ch);
            if (node_[at].next[k] == kNil) {
                node_.push_back(Node{});
                node_[at].next[k] = node_.size() - 1;
            }
            at = node_[at].next[k];
        }
        node_[at].output = pattern_count_;
        ++pattern_count_;
    }

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

    std::vector<Hit> Search(const std::string& text, measure::Counters* c = nullptr) const {
        std::vector<Hit> hits;
        if (!built_ || node_.empty()) return hits;
        size_t at = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            at = node_[at].next[uint8_t(text[i])];
            if (c) ++c->visited;
            // output link 사슬을 따라가며 *이 위치에서 끝나는 모든* 패턴을 낸다.
            for (size_t s = at; s != kNil; s = node_[s].out_link) {
                if (node_[s].output != kNone) hits.push_back(Hit{i, node_[s].output});
            }
        }
        return hits;
    }

    size_t StateCount() const { return node_.size(); }

private:
    static const size_t kNil  = size_t(-1);
    static const size_t kNone = size_t(-2);
    struct Node {
        size_t next[256];
        size_t fail = 0;
        size_t out_link = kNil;
        size_t output = kNone;
        Node() { for (int i = 0; i < 256; ++i) next[i] = kNil; }
    };

    std::vector<Node> node_;
    size_t pattern_count_ = 0;
    bool   built_ = false;
};
// snippet-end: text-aho

// ------------------------------------------------------------ rolling hash

// snippet-begin: text-rolling-hash
// Rabin-Karp. 해시가 같다는 것은 *후보* 라는 뜻일 뿐이다. 바이트 확인을 빼면
// 충돌 하나가 곧 오보가 된다 — 검사가 그 충돌을 실제로 만들어 확인한다.
class RollingHash {
public:
    RollingHash(uint64_t base = 257, uint64_t mod = 1000000007ull)
        : base_(base), mod_(mod) {}

    std::vector<size_t> Search(const std::string& text, const std::string& pat,
                               bool verify_bytes, measure::Counters* c = nullptr) const {
        std::vector<size_t> hits;
        const size_t m = pat.size(), n = text.size();
        if (m == 0 || m > n) return hits;

        uint64_t power = 1;
        for (size_t i = 1; i < m; ++i) power = (power * base_) % mod_;

        uint64_t target = 0, window = 0;
        for (size_t i = 0; i < m; ++i) {
            target = (target * base_ + uint8_t(pat[i])) % mod_;
            window = (window * base_ + uint8_t(text[i])) % mod_;
        }
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

private:
    uint64_t base_;
    uint64_t mod_;
};
// snippet-end: text-rolling-hash

}  // namespace algo
