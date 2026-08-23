// compress.hpp — 무손실 압축.
//
// 이 헤더가 소유하는 계약 셋.
//   1. round-trip. decode(encode(x)) == x 를 어떤 입력에서도.
//   2. 엔트로피는 *선택한 모델* 의 하한이지 파일 크기의 예언이 아니다.
//      header · 빈도표 · padding · checksum 은 그 하한 밖에 있고, 표에 따로 세운다.
//   3. 복호기는 신뢰할 수 없는 입력을 읽는 parser 다. 잘린 것 · 손상된 것 ·
//      자원을 무한히 요구하는 것을 전부 *거부* 해야 한다.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace algo {

using Bytes = std::vector<uint8_t>;

// snippet-begin: compress-entropy
// 0차 엔트로피 H = -sum p log2 p, 심볼당 비트. 심볼을 독립으로 보는 모델의
// 하한이다 — 반복 구조가 있는 데이터는 이 값보다 훨씬 작게 줄 수 있고,
// 그때 이기는 것은 더 나은 코드가 아니라 더 나은 *모델* 이다.
inline double Entropy0(const Bytes& data) {
    if (data.empty()) return 0.0;
    uint64_t freq[256] = {0};
    for (uint8_t b : data) ++freq[b];
    double h = 0.0;
    const double n = double(data.size());
    for (int i = 0; i < 256; ++i) {
        if (!freq[i]) continue;
        const double p = double(freq[i]) / n;
        h -= p * std::log2(p);
    }
    return h;
}
// snippet-end: compress-entropy

// ----------------------------------------------------------------- 비트 IO

class BitWriter {
public:
    void Put(uint32_t value, int bits) {
        for (int i = bits - 1; i >= 0; --i) {
            acc_ = uint8_t((acc_ << 1) | ((value >> i) & 1u));
            if (++count_ == 8) { out_.push_back(acc_); acc_ = 0; count_ = 0; }
        }
    }
    // 남은 비트를 0 으로 채운다. padding 이 몇 비트인지 바깥이 알아야 복호기가
    // 마지막 코드를 잘못 읽지 않는다 — 그래서 프레임에 심볼 수를 싣는다.
    Bytes Finish() {
        if (count_ > 0) { out_.push_back(uint8_t(acc_ << (8 - count_))); acc_ = 0; count_ = 0; }
        return out_;
    }
    size_t BitCount() const { return out_.size() * 8 + size_t(count_); }

private:
    Bytes out_;
    uint8_t acc_ = 0;
    int count_ = 0;
};

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : p_(data), n_(size) {}
    // 실패를 상태로 남긴다 — 끝을 넘어 읽으면 0 을 돌려주되 ok_ 가 false 가 된다.
    uint32_t Get(int bits) {
        uint32_t value = 0;
        for (int i = 0; i < bits; ++i) {
            if (at_ >= n_ * 8) { ok_ = false; return value << (bits - i); }
            const uint8_t byte = p_[at_ / 8];
            value = (value << 1) | ((byte >> (7 - (at_ % 8))) & 1u);
            ++at_;
        }
        return value;
    }
    // 소비하지 않고 앞을 본다. lookup 복호는 "표 폭만큼 보고, 표가 알려 준 실제
    // 길이만큼만 소비" 하는 형태라 이 연산이 필요하다. 끝을 넘는 자리는 0 으로 읽는다.
    uint32_t Peek(int bits) const {
        uint32_t value = 0;
        for (int i = 0; i < bits; ++i) {
            const size_t pos = at_ + size_t(i);
            const uint32_t bit =
                (pos < n_ * 8) ? ((p_[pos / 8] >> (7 - (pos % 8))) & 1u) : 0u;
            value = (value << 1) | bit;
        }
        return value;
    }
    bool Ok() const { return ok_; }
    size_t BitsLeft() const { return (at_ >= n_ * 8) ? 0 : n_ * 8 - at_; }

private:
    const uint8_t* p_;
    size_t n_;
    size_t at_ = 0;
    bool ok_ = true;
};

// ---------------------------------------------------------- canonical Huffman

// snippet-begin: compress-huffman-lengths
// 코드 *길이* 만 구한다. 트리 모양은 저장할 필요가 없다 — 길이 표만 있으면
// canonical 규칙이 코드를 유일하게 재구성한다. 그것이 표 비용을 크게 줄인다.
// 길이 상한을 두는 이유는 복호 lookup 표 크기가 2^maxlen 이기 때문이다.
struct HuffmanLengths {
    uint8_t length[256] = {0};
    size_t  symbols = 0;
    uint8_t max_length = 0;
};

// 길이 상한 강제. 깊이를 그냥 잘라 내면 Kraft 합이 1 을 넘어 접두 성질이 깨진다 —
// 접두 부호가 아닌 것을 접두 부호라고 부르는 셈이다. 대신 JPEG Annex K 의 재분배를
// 쓴다. 한 번의 조정이 -2*2^-l + 2^-(l-1) + 2*2^-(j+1) - 2^-j = 0 이라 Kraft 합이
// 정확히 보존된다. 최적성은 조금 잃고(평균 길이가 미세하게 는다) 상한을 얻는다.
inline void LimitCodeLengths(HuffmanLengths& h, uint8_t limit) {
    if (h.max_length <= limit || h.symbols == 0) return;
    std::vector<size_t> count(size_t(h.max_length) + 1, 0);
    for (int s = 0; s < 256; ++s) if (h.length[s]) ++count[h.length[s]];

    for (size_t l = h.max_length; l > limit; --l) {
        while (count[l] > 0) {
            size_t j = l - 2;
            while (j > 0 && count[j] == 0) --j;
            if (j == 0) return;          // 상한이 심볼 수에 비해 너무 작다
            count[l]     -= 2;
            count[l - 1] += 1;
            count[j + 1] += 2;
            count[j]     -= 1;
        }
    }
    // 재배정. 원래 짧았던 심볼(=빈도가 큰 심볼)이 계속 짧도록 (길이, 심볼) 오름차순.
    std::vector<int> order;
    for (int s = 0; s < 256; ++s) if (h.length[s]) order.push_back(s);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (h.length[a] != h.length[b]) return h.length[a] < h.length[b];
        return a < b;
    });
    size_t at = 0;
    h.max_length = 0;
    for (size_t l = 1; l <= limit; ++l) {
        for (size_t k = 0; k < count[l]; ++k) {
            h.length[order[at++]] = uint8_t(l);
            h.max_length = uint8_t(l);
        }
    }
}

inline HuffmanLengths BuildHuffmanLengths(const uint64_t freq[256], uint8_t limit = 15) {
    HuffmanLengths out;
    struct Node { uint64_t weight; int left; int right; int symbol; };
    std::vector<Node> node;
    std::vector<int> live;
    for (int s = 0; s < 256; ++s) {
        if (!freq[s]) continue;
        node.push_back(Node{freq[s], -1, -1, s});
        live.push_back(int(node.size()) - 1);
        ++out.symbols;
    }
    if (out.symbols == 0) return out;
    if (out.symbols == 1) {
        // 심볼 하나짜리 알파벳. 길이 0 코드는 없으므로 1비트를 준다 —
        // 그러지 않으면 복호기가 한 걸음도 전진하지 못한다.
        out.length[node[0].symbol] = 1;
        out.max_length = 1;
        return out;
    }
    while (live.size() > 1) {
        // 가장 가벼운 둘. 동점은 노드 번호가 작은 쪽 — 결과가 결정적이어야
        // 같은 입력이 같은 바이트를 낸다.
        size_t a = 0, b = 1;
        if (node[live[b]].weight < node[live[a]].weight) std::swap(a, b);
        for (size_t k = 2; k < live.size(); ++k) {
            if (node[live[k]].weight < node[live[a]].weight) { b = a; a = k; }
            else if (node[live[k]].weight < node[live[b]].weight) { b = k; }
        }
        const int ia = live[a], ib = live[b];
        node.push_back(Node{node[ia].weight + node[ib].weight, ia, ib, -1});
        const int merged = int(node.size()) - 1;
        if (a < b) { live[a] = merged; live.erase(live.begin() + std::ptrdiff_t(b)); }
        else       { live[b] = merged; live.erase(live.begin() + std::ptrdiff_t(a)); }
    }
    // 깊이 부여
    std::vector<std::pair<int, int>> stack{{live[0], 0}};
    while (!stack.empty()) {
        const auto top = stack.back();
        stack.pop_back();
        const Node& nd = node[size_t(top.first)];
        if (nd.symbol >= 0) {
            const int depth = top.second > 0 ? top.second : 1;
            // 여기서 자르지 않는다. 자르면 Kraft 합이 깨진다.
            out.length[nd.symbol] = uint8_t(depth > 255 ? 255 : depth);
            if (out.length[nd.symbol] > out.max_length) out.max_length = out.length[nd.symbol];
            continue;
        }
        stack.push_back({nd.left, top.second + 1});
        stack.push_back({nd.right, top.second + 1});
    }
    LimitCodeLengths(out, limit);
    return out;
}

// Kraft 부등식. sum 2^-len <= 1 이어야 접두 부호가 존재한다. 손상된 길이표를
// 그대로 믿고 코드를 배정하면 겹치는 코드가 생겨 복호가 조용히 틀린다.
inline bool KraftOk(const HuffmanLengths& h, bool* exact = nullptr) {
    uint64_t sum = 0;
    const uint64_t one = 1ull << 32;
    for (int s = 0; s < 256; ++s) {
        if (!h.length[s]) continue;
        if (h.length[s] > 32) return false;
        sum += one >> h.length[s];
        if (sum > one) return false;
    }
    if (exact) *exact = (sum == one) || (h.symbols <= 1);
    return sum <= one;
}
// snippet-end: compress-huffman-lengths

// snippet-begin: compress-canonical
// canonical 코드 배정. 같은 길이끼리 심볼 번호 오름차순으로 연속된 코드를 준다.
// 길이가 증가할 때 code 를 왼쪽으로 미는(shift) 한 줄이 접두 성질의 전부다 —
// 그 줄을 빼면 서로 다른 심볼이 같은 비트열을 갖는다.
struct CanonicalCode {
    uint32_t code[256] = {0};
    uint8_t  length[256] = {0};
    uint8_t  max_length = 0;
};

inline CanonicalCode AssignCanonical(const HuffmanLengths& h) {
    CanonicalCode cc;
    std::memcpy(cc.length, h.length, sizeof(cc.length));
    cc.max_length = h.max_length;
    uint32_t code = 0;
    for (uint8_t len = 1; len <= h.max_length; ++len) {
        code <<= 1;                          // ← 길이가 하나 늘면 자리도 하나 는다
        for (int s = 0; s < 256; ++s) {
            if (h.length[s] != len) continue;
            cc.code[s] = code++;
        }
    }
    return cc;
}

// k비트 lookup 복호표. 한 번의 배열 조회로 심볼과 실제 소비 비트 수를 함께 얻는다.
// 표 크기는 2^max_length 이므로 길이 상한이 곧 메모리 상한이다.
struct DecodeTable {
    std::vector<int16_t> symbol;   // -1 이면 유효하지 않은 접두
    std::vector<uint8_t> used;
    uint8_t bits = 0;
};

inline DecodeTable BuildDecodeTable(const CanonicalCode& cc) {
    DecodeTable t;
    t.bits = cc.max_length ? cc.max_length : 1;
    t.symbol.assign(size_t(1) << t.bits, int16_t(-1));
    t.used.assign(size_t(1) << t.bits, 0);
    for (int s = 0; s < 256; ++s) {
        const uint8_t len = cc.length[s];
        if (!len) continue;
        const uint32_t base = cc.code[s] << (t.bits - len);
        const uint32_t span = 1u << (t.bits - len);
        // 손상된 길이표에서 배정된 코드는 표 폭을 넘을 수 있다. 호출부가 Kraft 로
        // 먼저 거르지만, 표를 만드는 쪽도 범위 밖을 쓰지 않아야 한다 — 복호기는
        // 신뢰할 수 없는 입력을 읽는 코드이고, 방어는 한 겹으로 두지 않는다.
        if (size_t(base) + size_t(span) > t.symbol.size()) continue;
        for (uint32_t k = 0; k < span; ++k) {
            t.symbol[base + k] = int16_t(s);
            t.used[base + k] = len;
        }
    }
    return t;
}
// snippet-end: compress-canonical

// snippet-begin: compress-frame
// 프레임 형식. magic · version · 원본 크기 · checksum 을 앞에 둔다.
// 이것들이 압축된 비트 밖에 있는 *가격* 이고, 작은 입력에서 압축이 지는 이유다.
//   magic     이 바이트열이 우리 형식인가
//   version   복호기가 이해하는 형식인가
//   raw_size  얼마나 나와야 하는가(자원 상한이자 종료 조건)
//   checksum  나온 것이 맞는가
struct FrameHeader {
    static const uint32_t kMagic = 0x314C4141u;   // "AAL1"
    static const uint16_t kVersion = 1;
};

inline void PutU32(Bytes& b, uint32_t v) {
    b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 24));
}
inline uint32_t GetU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline uint32_t Adler32(const uint8_t* data, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; ++i) { a = (a + data[i]) % 65521u; b = (b + a) % 65521u; }
    return (b << 16) | a;
}
// snippet-end: compress-frame

// snippet-begin: compress-huffman-codec
// 압축: [magic][version][raw_size][adler32][길이표 256바이트][비트열]
// 길이표를 심볼당 1바이트로 그대로 싣는다. 더 줄일 수 있지만, 표 비용을 눈에
// 보이게 두는 것이 이 lab 의 목적이다 — 측정 표가 이 256 바이트를 따로 센다.
inline Bytes HuffmanCompress(const Bytes& raw) {
    uint64_t freq[256] = {0};
    for (uint8_t b : raw) ++freq[b];
    const HuffmanLengths lengths = BuildHuffmanLengths(freq);
    const CanonicalCode cc = AssignCanonical(lengths);

    Bytes out;
    PutU32(out, FrameHeader::kMagic);
    out.push_back(uint8_t(FrameHeader::kVersion));
    out.push_back(0);
    PutU32(out, uint32_t(raw.size()));
    PutU32(out, Adler32(raw.data(), raw.size()));
    for (int s = 0; s < 256; ++s) out.push_back(cc.length[s]);

    BitWriter bw;
    for (uint8_t b : raw) bw.Put(cc.code[b], cc.length[b]);
    const Bytes bits = bw.Finish();
    out.insert(out.end(), bits.begin(), bits.end());
    return out;
}

// 복호. 모든 실패 경로가 false 를 돌려준다 — 예외도, 부분 출력도 없다.
inline bool HuffmanDecompress(const Bytes& frame, Bytes* out) {
    out->clear();
    const size_t kHeader = 4 + 1 + 1 + 4 + 4 + 256;
    if (frame.size() < kHeader) return false;
    if (GetU32(frame.data()) != FrameHeader::kMagic) return false;
    if (frame[4] != FrameHeader::kVersion) return false;
    const uint32_t raw_size = GetU32(frame.data() + 6);
    const uint32_t want_sum = GetU32(frame.data() + 10);

    HuffmanLengths lengths;
    for (int s = 0; s < 256; ++s) {
        lengths.length[s] = frame[14 + size_t(s)];
        if (lengths.length[s]) {
            ++lengths.symbols;
            if (lengths.length[s] > lengths.max_length) lengths.max_length = lengths.length[s];
        }
    }
    if (lengths.max_length > 24) return false;          // lookup 표 크기 상한
    if (raw_size > 0 && lengths.symbols == 0) return false;
    if (!KraftOk(lengths)) return false;                // ← 손상 표 거부
    if (raw_size == 0) return frame.size() == kHeader && want_sum == Adler32(nullptr, 0);

    const CanonicalCode cc = AssignCanonical(lengths);
    const DecodeTable table = BuildDecodeTable(cc);
    BitReader br(frame.data() + kHeader, frame.size() - kHeader);
    out->reserve(raw_size);
    for (uint32_t i = 0; i < raw_size; ++i) {
        if (br.BitsLeft() < 1) return false;            // 잘린 비트열
        // 남은 비트가 표 폭보다 적을 수 있다. Get 이 0 으로 채우므로 그대로 조회하되
        // 실제 소비량이 남은 비트를 넘으면 거부한다.
        const size_t left = br.BitsLeft();
        const uint32_t peek = br.Peek(table.bits);
        const int16_t sym = table.symbol[peek];
        if (sym < 0) return false;                      // 유효하지 않은 접두
        const uint8_t used = table.used[peek];
        if (used == 0 || used > left) return false;
        br.Get(used);
        out->push_back(uint8_t(sym));
    }
    if (Adler32(out->data(), out->size()) != want_sum) { out->clear(); return false; }
    return true;
}
// snippet-end: compress-huffman-codec

// -------------------------------------------------------------------- LZ77

// snippet-begin: compress-lz-match
// hash chain match finder. 3바이트 해시로 같은 접두를 가진 최근 위치들을 잇고,
// 그 사슬을 최대 depth 만큼만 따라간다 — depth 가 곧 압축률/속도 손잡이다.
struct LzToken {
    bool     literal = true;
    uint8_t  byte    = 0;
    uint32_t distance = 0;
    uint32_t length   = 0;
};

enum class ParseMode { Greedy, Lazy, Optimal };

class MatchFinder {
public:
    static const uint32_t kMinMatch = 3;
    static const uint32_t kMaxMatch = 258;

    explicit MatchFinder(const Bytes& data, uint32_t window = 4096, uint32_t depth = 32)
        : data_(data), window_(window), depth_(depth), head_(kHashSize, kNone),
          prev_(data.size(), kNone) {}

    void Insert(uint32_t at) {
        if (at + kMinMatch > data_.size()) return;
        const uint32_t h = Hash(at);
        prev_[at] = head_[h];
        head_[h] = at;
    }

    // at 위치에서 가장 긴 일치. 없으면 length 0.
    LzToken Find(uint32_t at) const {
        LzToken best;
        best.literal = true;
        best.byte = data_[at];
        if (at + kMinMatch > data_.size()) return best;
        const uint32_t h = Hash(at);
        uint32_t cursor = head_[h];
        uint32_t tries = depth_;
        while (cursor != kNone && tries-- > 0) {
            if (at - cursor > window_) break;
            uint32_t len = 0;
            const uint32_t room = uint32_t(data_.size() - at);
            const uint32_t cap = (room < kMaxMatch) ? room : kMaxMatch;
            while (len < cap && data_[cursor + len] == data_[at + len]) ++len;
            if (len >= kMinMatch && len > best.length) {
                best.literal  = false;
                best.length   = len;
                best.distance = at - cursor;
            }
            cursor = prev_[cursor];
        }
        return best;
    }

private:
    static const uint32_t kNone = uint32_t(-1);
    static const uint32_t kHashSize = 1 << 15;

    uint32_t Hash(uint32_t at) const {
        const uint32_t v = (uint32_t(data_[at]) << 16) |
                           (uint32_t(data_[at + 1]) << 8) | uint32_t(data_[at + 2]);
        return (v * 2654435761u) >> 17 & (kHashSize - 1);
    }

    const Bytes& data_;
    uint32_t window_;
    uint32_t depth_;
    std::vector<uint32_t> head_;
    std::vector<uint32_t> prev_;
};
// snippet-end: compress-lz-match

// snippet-begin: compress-lz-parse
// 세 파싱 전략. 같은 match finder 위에서 *선택 규칙만* 다르다.
//   Greedy   지금 가장 긴 것을 잡는다.
//   Lazy     다음 위치가 더 길면 지금은 리터럴로 흘린다.
//   Optimal  각 위치를 노드로 보고 비용 최단 경로를 푼다 — DAG 위 DP 다.
inline std::vector<LzToken> LzParse(const Bytes& data, ParseMode mode,
                                    uint32_t window = 4096, uint32_t depth = 32) {
    std::vector<LzToken> out;
    if (data.empty()) return out;
    MatchFinder finder(data, window, depth);

    if (mode == ParseMode::Optimal) {
        const size_t n = data.size();
        // cost[i] = i 부터 끝까지의 최소 비트 비용. 리터럴 9비트, 일치 24비트로
        // 근사한다(실제 부호화 비용의 단순 모형).
        std::vector<uint32_t> cost(n + 1, 0);
        std::vector<LzToken> pick(n);
        std::vector<LzToken> match(n);
        for (uint32_t i = 0; i < n; ++i) { match[i] = finder.Find(i); finder.Insert(i); }
        for (size_t i = n; i-- > 0;) {
            LzToken lit;
            lit.literal = true;
            lit.byte = data[i];
            uint32_t best = cost[i + 1] + 9;
            pick[i] = lit;
            const LzToken& m = match[i];
            if (!m.literal && m.length >= MatchFinder::kMinMatch) {
                for (uint32_t len = MatchFinder::kMinMatch; len <= m.length; ++len) {
                    const uint32_t candidate = cost[i + len] + 24;
                    if (candidate < best) {
                        best = candidate;
                        LzToken t = m;
                        t.length = len;
                        pick[i] = t;
                    }
                }
            }
            cost[i] = best;
        }
        for (size_t i = 0; i < n;) {
            out.push_back(pick[i]);
            i += pick[i].literal ? 1 : pick[i].length;
        }
        return out;
    }

    uint32_t at = 0;
    while (at < data.size()) {
        LzToken here = finder.Find(at);
        finder.Insert(at);
        if (mode == ParseMode::Lazy && !here.literal && at + 1 < data.size()) {
            const LzToken next = finder.Find(at + 1);
            if (!next.literal && next.length > here.length) {
                LzToken lit;
                lit.literal = true;
                lit.byte = data[at];
                out.push_back(lit);
                ++at;
                continue;
            }
        }
        if (here.literal) {
            out.push_back(here);
            ++at;
        } else {
            out.push_back(here);
            for (uint32_t k = 1; k < here.length && at + k < data.size(); ++k)
                finder.Insert(at + k);
            at += here.length;
        }
    }
    return out;
}
// snippet-end: compress-lz-parse

// snippet-begin: compress-lz-frame
// 토큰 직렬화. 리터럴은 [0][byte], 일치는 [1][len u16][dist u16].
inline Bytes LzEncode(const Bytes& raw, ParseMode mode, uint32_t window = 4096) {
    const std::vector<LzToken> tokens = LzParse(raw, mode, window);
    Bytes out;
    PutU32(out, FrameHeader::kMagic ^ 0x00010000u);
    out.push_back(uint8_t(FrameHeader::kVersion));
    out.push_back(0);
    PutU32(out, uint32_t(raw.size()));
    PutU32(out, Adler32(raw.data(), raw.size()));
    for (const LzToken& t : tokens) {
        if (t.literal) { out.push_back(0); out.push_back(t.byte); continue; }
        out.push_back(1);
        out.push_back(uint8_t(t.length)); out.push_back(uint8_t(t.length >> 8));
        out.push_back(uint8_t(t.distance)); out.push_back(uint8_t(t.distance >> 8));
    }
    return out;
}

// 복호기. 신뢰할 수 없는 parser 로 다룬다.
//   - 잘린 토큰 거부
//   - distance 가 지금까지 낸 바이트 수보다 크면 거부 (범위 밖 읽기)
//   - 출력 상한(raw_size)을 넘으면 거부
//   - **겹침 복사**: distance < length 면 source 와 destination 이 겹친다.
//     memcpy 의미론이 아니라 바이트 단위로 앞에서부터 복사해야 한다 —
//     그것이 "aaaa..." 를 (dist=1, len=100) 하나로 표현할 수 있게 하는 규칙이다.
inline bool LzDecode(const Bytes& frame, Bytes* out) {
    out->clear();
    const size_t kHeader = 4 + 1 + 1 + 4 + 4;
    if (frame.size() < kHeader) return false;
    if (GetU32(frame.data()) != (FrameHeader::kMagic ^ 0x00010000u)) return false;
    if (frame[4] != FrameHeader::kVersion) return false;
    const uint32_t raw_size = GetU32(frame.data() + 6);
    const uint32_t want_sum = GetU32(frame.data() + 10);
    if (raw_size > (1u << 28)) return false;             // 자원 상한
    out->reserve(raw_size);

    size_t at = kHeader;
    while (at < frame.size()) {
        const uint8_t kind = frame[at++];
        if (kind == 0) {
            if (at >= frame.size()) return false;
            if (out->size() + 1 > raw_size) return false;
            out->push_back(frame[at++]);
            continue;
        }
        if (kind != 1) return false;
        if (at + 4 > frame.size()) return false;
        const uint32_t length   = uint32_t(frame[at]) | (uint32_t(frame[at + 1]) << 8);
        const uint32_t distance = uint32_t(frame[at + 2]) | (uint32_t(frame[at + 3]) << 8);
        at += 4;
        if (length == 0 || distance == 0) return false;
        if (distance > out->size()) return false;
        // 상한 검사를 *더하기 전에* 한다. out->size() + length 를 먼저 계산해
        // 비교하면 큰 값에서 감기며 통과한다.
        if (length > raw_size - uint32_t(out->size())) return false;
        const size_t from = out->size() - distance;
        for (uint32_t k = 0; k < length; ++k) out->push_back((*out)[from + k]);
    }
    if (out->size() != raw_size) return false;
    if (Adler32(out->data(), out->size()) != want_sum) { out->clear(); return false; }
    return true;
}
// snippet-end: compress-lz-frame

// ------------------------------------------------------- 산술 부호기(엔트로피 팔)

// snippet-begin: compress-arith-coder
// 산술 부호기(range coder 계열). 심볼 하나를 정수 개 비트에 맞출 필요가 없어,
// 확률이 매우 치우친 알파벳에서 Huffman 의 "심볼당 최소 1비트" 하한을 넘어선다.
//
// 32비트 [low, high] 구간을 좁혀 가며, 상위 비트가 확정되면 흘려보낸다.
// 어려운 곳은 세 번째 분기다 — low 와 high 가 half 를 사이에 두고 가까워지면
// 상위 비트가 확정되지 않은 채 구간만 좁아진다(underflow). 그때는 가운데를
// 잘라내고 '보류 비트' 를 세어 두었다가, 다음에 확정되는 비트의 반대값으로 낸다.
// 이 처리를 빼면 대부분의 입력에서 잘 돌다가 특정 확률 배치에서만 깨진다.
//
// 정적 0차 모델만 쓴다(빈도표를 프레임에 싣는다). total <= 2^30 이어야
// 재정규화 뒤 range/total >= 1 이 보장된다 — 빈도를 4095 로 눌러 담는 이유다.
class ArithEncoder {
public:
    void Encode(uint32_t cum_low, uint32_t cum_high, uint32_t total) {
        const uint64_t range = uint64_t(high_) - uint64_t(low_) + 1;
        high_ = uint32_t(low_ + (range * cum_high) / total - 1);
        low_  = uint32_t(low_ + (range * cum_low) / total);
        for (;;) {
            if (high_ < kHalf) {
                PutBit(0);
            } else if (low_ >= kHalf) {
                PutBit(1);
                low_  -= kHalf;
                high_ -= kHalf;
            } else if (low_ >= kQuarter && high_ < kThreeQuarter) {
                ++pending_;                    // ← underflow. 확정을 미룬다.
                low_  -= kQuarter;
                high_ -= kQuarter;
            } else {
                break;
            }
            low_  = low_ << 1;
            high_ = (high_ << 1) | 1u;
        }
    }

    Bytes Finish() {
        ++pending_;
        if (low_ < kQuarter) PutBit(0);
        else                 PutBit(1);
        return bw_.Finish();
    }

    static const uint32_t kQuarter      = 0x40000000u;
    static const uint32_t kHalf         = 0x80000000u;
    static const uint32_t kThreeQuarter = 0xC0000000u;

private:
    // 확정 비트 하나를 내고, 보류돼 있던 비트를 그 반대값으로 함께 낸다.
    void PutBit(uint32_t bit) {
        bw_.Put(bit, 1);
        while (pending_ > 0) { bw_.Put(bit ^ 1u, 1); --pending_; }
    }

    BitWriter bw_;
    uint32_t low_  = 0;
    uint32_t high_ = 0xFFFFFFFFu;
    uint64_t pending_ = 0;
};

class ArithDecoder {
public:
    ArithDecoder(const uint8_t* data, size_t n) : br_(data, n) {
        for (int i = 0; i < 32; ++i) value_ = (value_ << 1) | br_.Get(1);
    }

    // 지금 구간이 가리키는 누적 빈도. 호출부가 이 값으로 심볼을 찾는다.
    uint32_t Target(uint32_t total) const {
        const uint64_t range = uint64_t(high_) - uint64_t(low_) + 1;
        const uint64_t scaled = ((uint64_t(value_) - low_ + 1) * total - 1) / range;
        return uint32_t(scaled >= total ? total - 1 : scaled);
    }

    void Advance(uint32_t cum_low, uint32_t cum_high, uint32_t total) {
        const uint64_t range = uint64_t(high_) - uint64_t(low_) + 1;
        high_ = uint32_t(low_ + (range * cum_high) / total - 1);
        low_  = uint32_t(low_ + (range * cum_low) / total);
        for (;;) {
            if (high_ < ArithEncoder::kHalf) {
                // 상위 비트 0 확정. 뺄 것이 없다.
            } else if (low_ >= ArithEncoder::kHalf) {
                value_ -= ArithEncoder::kHalf;
                low_   -= ArithEncoder::kHalf;
                high_  -= ArithEncoder::kHalf;
            } else if (low_ >= ArithEncoder::kQuarter &&
                       high_ < ArithEncoder::kThreeQuarter) {
                value_ -= ArithEncoder::kQuarter;
                low_   -= ArithEncoder::kQuarter;
                high_  -= ArithEncoder::kQuarter;
            } else {
                break;
            }
            low_   = low_ << 1;
            high_  = (high_ << 1) | 1u;
            // 비트열 끝을 넘어가면 0 을 읽는다. 부호기가 마지막에 채운 padding 과
            // 같은 자리이므로 raw_size 만큼 뽑고 나면 영향이 없다.
            value_ = (value_ << 1) | br_.Get(1);
        }
    }

private:
    BitReader br_;
    uint32_t low_   = 0;
    uint32_t high_  = 0xFFFFFFFFu;
    uint32_t value_ = 0;
};

// 정적 0차 모델 + 산술 부호기. 빈도표(256 x u16)를 프레임에 싣는다.
inline Bytes RangeCompress(const Bytes& raw) {
    uint64_t raw_freq[256] = {0};
    for (uint8_t b : raw) ++raw_freq[b];
    uint64_t peak = 1;
    for (int s = 0; s < 256; ++s) peak = std::max(peak, raw_freq[s]);

    uint32_t freq[256];
    for (int s = 0; s < 256; ++s) {
        if (!raw_freq[s]) { freq[s] = 0; continue; }
        // 0 이 아닌 빈도는 최소 1 을 유지해야 한다 — 확률 0 인 심볼은 부호화할 수 없다.
        const uint64_t scaled = raw_freq[s] * 4095ull / peak;
        freq[s] = uint32_t(scaled ? scaled : 1);
    }
    uint32_t cum[257];
    cum[0] = 0;
    for (int s = 0; s < 256; ++s) cum[s + 1] = cum[s] + freq[s];
    const uint32_t total = cum[256] ? cum[256] : 1;

    Bytes out;
    PutU32(out, FrameHeader::kMagic ^ 0x00020000u);
    out.push_back(uint8_t(FrameHeader::kVersion));
    out.push_back(0);
    PutU32(out, uint32_t(raw.size()));
    PutU32(out, Adler32(raw.data(), raw.size()));
    for (int s = 0; s < 256; ++s) {
        out.push_back(uint8_t(freq[s]));
        out.push_back(uint8_t(freq[s] >> 8));
    }
    ArithEncoder enc;
    for (uint8_t b : raw) enc.Encode(cum[b], cum[b + 1], total);
    const Bytes body = enc.Finish();
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

inline bool RangeDecompress(const Bytes& frame, Bytes* out) {
    out->clear();
    const size_t kHeader = 4 + 1 + 1 + 4 + 4 + 512;
    if (frame.size() < kHeader) return false;
    if (GetU32(frame.data()) != (FrameHeader::kMagic ^ 0x00020000u)) return false;
    if (frame[4] != FrameHeader::kVersion) return false;
    const uint32_t raw_size = GetU32(frame.data() + 6);
    const uint32_t want_sum = GetU32(frame.data() + 10);
    if (raw_size > (1u << 28)) return false;            // 자원 상한

    uint32_t cum[257];
    cum[0] = 0;
    for (int s = 0; s < 256; ++s) {
        const uint32_t f = uint32_t(frame[14 + size_t(s) * 2]) |
                           (uint32_t(frame[15 + size_t(s) * 2]) << 8);
        cum[s + 1] = cum[s] + f;
    }
    const uint32_t total = cum[256];
    if (raw_size == 0) return want_sum == Adler32(nullptr, 0);
    if (total == 0) return false;

    ArithDecoder dec(frame.data() + kHeader, frame.size() - kHeader);
    out->reserve(raw_size);
    for (uint32_t i = 0; i < raw_size; ++i) {
        const uint32_t target = dec.Target(total);
        // 누적표에서 target 이 들어가는 구간을 이진 탐색으로 찾는다.
        int lo = 0, hi = 255;
        while (lo < hi) {
            const int mid = lo + (hi - lo) / 2;
            if (cum[mid + 1] <= target) lo = mid + 1; else hi = mid;
        }
        if (cum[lo + 1] == cum[lo]) return false;       // 확률 0 인 심볼로 해독됐다
        dec.Advance(cum[lo], cum[lo + 1], total);
        out->push_back(uint8_t(lo));
    }
    if (Adler32(out->data(), out->size()) != want_sum) { out->clear(); return false; }
    return true;
}
// snippet-end: compress-arith-coder

}  // namespace algo
