// huffman — canonical Huffman. 코드 길이표 하나가 복호기 전체를 정한다.
//
// 이 저장소가 압축을 들이는 자리는 리플레이 파일 하나다. 그 파일의 내용은 프레임
// 조작과 아바타 자취인데, 분포가 극단적으로 치우쳐 있다 — 17개 액션 키 비트는
// 대부분 0 이고, 마우스 좌표는 프레임 사이 근접 반복이며, 전진 플래그는 사실상
// 상수다. 0차 엔트로피가 바이트당 1비트에 못 미치는 데이터를 8비트로 적고 있었다.
//
// ── 왜 canonical 인가 ─────────────────────────────────────────────
// 일반 Huffman 코드를 파일에 적으려면 트리를 적어야 한다. canonical 코드는 트리를
// 적지 않는다 — **심볼별 코드 길이**만 적으면 코드가 유일하게 복원된다. 규칙은
// 두 줄이다: 길이가 짧은 것이 먼저, 같은 길이는 심볼 번호 순. 그래서 256바이트의
// 길이표가 트리 전체를 대신한다.
//
//   길이가 하나 오를 때마다 다음 코드는 (지금까지 + 그 길이의 개수) << 1 이다.
//   그 `<< 1` 이 canonical 의 전부다. 지우면 길이가 다른 코드들이 접두 관계에
//   빠지고, 복호가 다른 심볼을 낸다.
//
// ── 왜 Kraft 를 검사하는가 ────────────────────────────────────────
// 길이표는 파일에서 온다 = 신뢰 경계 밖 입력이다. 임의의 256바이트가 유효한 코드
// 길이표는 아니다. sum 2^-len 이 1보다 크면 코드가 겹치고, 1보다 작으면 복호기가
// 어떤 비트열에서도 심볼을 못 찾는 상태(구멍)에 빠진다. **정확히 1** 만 받는다.
//
// 그 검사를 지워도 복호기는 크래시하지 않는다 — 그냥 조용히 다른 바이트를 낸다.
// 조용히 다른 답을 내는 디코더가 신뢰 경계에 있는 것이 이 파일이 막는 상태다.
//
// ── 길이 상한 15 ─────────────────────────────────────────────────
// 코드 길이를 15로 묶는다. 상한이 없으면 병적인 분포에서 깊이가 심볼 수까지
// 자라고, 그러면 (a) 길이표에 8비트로 못 적고 (b) 복호 lookup 표의 fallback 이
// 무한정 길어진다. 넘으면 빈도를 절반으로 접어 다시 짓는다. 전부 1 이 되면
// 깊이는 8 이하이므로 이 되풀이는 반드시 끝난다.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mega {
namespace algo {

inline constexpr size_t  kHuffmanSymbols   = 256;
inline constexpr uint8_t kHuffmanMaxLength = 15;
// 복호 lookup 표의 폭. 9비트면 512칸이고, 리플레이 분포에서 심볼의 대부분이 이
// 안에 들어온다. 더 긴 코드는 fallback 이 처리한다 — **그 fallback 이 없으면
// 라운드트립이 깨진다**는 것이 이 상수가 8이 아닌 9인 이유가 아니라, 어느 값이든
// fallback 이 필요하다는 사실의 이름이다.
inline constexpr int kHuffmanLookupBits = 9;

struct HuffmanCodes {
    uint16_t code[kHuffmanSymbols]{};
    uint8_t  length[kHuffmanSymbols]{};
};

// snippet-begin: huffman-kraft
// sum 2^-len == 1 인가. 2^15 로 스케일해 정수로만 센다 — 부동소수로 재면 같은
// 표가 컴파일러에 따라 통과하기도 실패하기도 한다.
inline bool HuffmanKraftComplete(const uint8_t* lengths) {
    uint32_t total = 0;
    const uint32_t full = 1u << kHuffmanMaxLength;
    bool any = false;
    for (size_t i = 0; i < kHuffmanSymbols; ++i) {
        if (lengths[i] == 0) continue;
        if (lengths[i] > kHuffmanMaxLength) return false;
        any = true;
        total += full >> lengths[i];
        if (total > full) return false;
    }
    return any && total == full;
}
// snippet-end: huffman-kraft

// snippet-begin: huffman-lengths
// 빈도 -> 코드 길이. 두 큐 방식이라 정렬 한 번 뒤에는 힙이 없고, 동점은 언제나
// 리프 먼저 · 심볼 번호 오름차순으로 갈린다(재현성).
//
// 심볼이 하나뿐이면 길이 1 을 그 심볼과 **바로 다음 심볼** 둘에 준다. 하나에만
// 주면 Kraft 합이 1/2 이라 위의 완전성 검사가 거절한다. 유령 심볼은 스트림에
// 한 번도 나오지 않으므로 복호에 영향이 없고, 그 대신 "불완전 코드도 받는다"는
// 예외를 디코더에 두지 않아도 된다.
inline bool HuffmanBuildLengths(const uint32_t* frequency, uint8_t* lengths) {
    std::memset(lengths, 0, kHuffmanSymbols);
    uint64_t scaled[kHuffmanSymbols];
    for (size_t i = 0; i < kHuffmanSymbols; ++i) scaled[i] = frequency[i];

    struct Node { uint64_t weight; int32_t left, right; };
    for (int attempt = 0; attempt < 40; ++attempt) {
        std::vector<uint32_t> leaves;
        for (size_t i = 0; i < kHuffmanSymbols; ++i)
            if (scaled[i] != 0) leaves.push_back(static_cast<uint32_t>(i));
        if (leaves.empty()) return false;
        if (leaves.size() == 1) {
            lengths[leaves[0]] = 1;
            lengths[(leaves[0] + 1) & 0xFFu] = 1;
            return true;
        }
        std::sort(leaves.begin(), leaves.end(), [&scaled](uint32_t a, uint32_t b) {
            if (scaled[a] != scaled[b]) return scaled[a] < scaled[b];
            return a < b;
        });

        const size_t leafCount = leaves.size();
        std::vector<Node> nodes;
        nodes.reserve(leafCount * 2);
        for (uint32_t symbol : leaves) nodes.push_back({scaled[symbol], -1, -1});

        std::vector<int32_t> internal;
        size_t nextLeaf = 0, nextInternal = 0;
        auto pick = [&]() {
            const bool leafLeft = nextLeaf < leafCount;
            const bool internalLeft = nextInternal < internal.size();
            // 동점이면 리프를 먼저 꺼낸다. 이 선택이 코드 길이 분포를 바꾸지만
            // 총 비트 수는 바꾸지 않고, 무엇보다 **매번 같은 표**를 낸다.
            const bool takeLeaf =
                leafLeft && (!internalLeft ||
                             nodes[nextLeaf].weight <=
                                 nodes[static_cast<size_t>(internal[nextInternal])].weight);
            if (takeLeaf) return static_cast<int32_t>(nextLeaf++);
            return internal[nextInternal++];
        };
        while ((leafCount - nextLeaf) + (internal.size() - nextInternal) > 1) {
            const int32_t a = pick();
            const int32_t b = pick();
            nodes.push_back({nodes[static_cast<size_t>(a)].weight +
                                 nodes[static_cast<size_t>(b)].weight,
                             a, b});
            internal.push_back(static_cast<int32_t>(nodes.size() - 1));
        }

        // 깊이 매기기. 재귀 대신 명시 스택 — 병적인 분포에서 깊이가 256까지
        // 자랄 수 있고, 그 깊이를 재는 코드가 그 깊이에 죽으면 안 된다.
        std::vector<uint8_t> depth(nodes.size(), 0);
        std::vector<int32_t> stack;
        stack.push_back(static_cast<int32_t>(nodes.size() - 1));
        uint32_t deepest = 0;
        while (!stack.empty()) {
            const int32_t at = stack.back();
            stack.pop_back();
            const Node& node = nodes[static_cast<size_t>(at)];
            if (node.left < 0) {
                deepest = std::max<uint32_t>(deepest, depth[static_cast<size_t>(at)]);
                continue;
            }
            const uint8_t child = static_cast<uint8_t>(
                std::min<uint32_t>(255u, depth[static_cast<size_t>(at)] + 1u));
            depth[static_cast<size_t>(node.left)] = child;
            depth[static_cast<size_t>(node.right)] = child;
            stack.push_back(node.left);
            stack.push_back(node.right);
        }

        if (deepest <= kHuffmanMaxLength) {
            for (size_t i = 0; i < leafCount; ++i)
                lengths[leaves[i]] = depth[i] == 0 ? uint8_t{1} : depth[i];
            return true;
        }
        for (size_t i = 0; i < kHuffmanSymbols; ++i)
            if (scaled[i] != 0) scaled[i] = (scaled[i] + 1) / 2;
    }
    return false;
}
// snippet-end: huffman-lengths

// snippet-begin: huffman-canonical
// 길이표 -> canonical 코드. 길이가 오를 때마다 `<< 1` 하는 그 한 줄이 접두
// 자유성을 만든다.
inline bool HuffmanAssignCanonical(const uint8_t* lengths, HuffmanCodes& out) {
    if (!HuffmanKraftComplete(lengths)) return false;
    uint32_t count[kHuffmanMaxLength + 1]{};
    for (size_t i = 0; i < kHuffmanSymbols; ++i) ++count[lengths[i]];
    count[0] = 0;

    uint32_t next[kHuffmanMaxLength + 2]{};
    uint32_t code = 0;
    for (uint8_t bits = 1; bits <= kHuffmanMaxLength; ++bits) {
        code = (code + count[bits - 1]) << 1;
        next[bits] = code;
    }
    for (size_t i = 0; i < kHuffmanSymbols; ++i) {
        out.length[i] = lengths[i];
        out.code[i] = 0;
        if (lengths[i] == 0) continue;
        out.code[i] = static_cast<uint16_t>(next[lengths[i]]++);
    }
    return true;
}
// snippet-end: huffman-canonical

// MSB 우선 비트 쓰기. 마지막 바이트는 0 으로 채운다.
class BitWriter {
public:
    void Put(uint32_t code, int bits) {
        for (int i = bits - 1; i >= 0; --i) {
            const uint32_t bit = (code >> i) & 1u;
            if (used_ == 0) bytes_.push_back(0);
            bytes_.back() |= static_cast<uint8_t>(bit << (7 - used_));
            used_ = (used_ + 1) & 7;
            ++bits_;
        }
    }
    const std::vector<uint8_t>& Bytes() const { return bytes_; }
    size_t BitCount() const { return bits_; }

private:
    std::vector<uint8_t> bytes_;
    size_t bits_ = 0;
    int    used_ = 0;
};

// snippet-begin: huffman-decoder
// k비트 lookup + 긴 코드 fallback.
//
// 표는 2^9 칸이고, 길이가 9 이하인 코드는 그 코드로 시작하는 **모든** 9비트
// 접두에 자기 자신을 심는다. 그러면 훑기는 9비트를 한 번 엿보고 표를 한 번 읽는
// 것으로 끝난다. 길이가 10 이상인 코드는 표에 들어가지 못하므로 canonical 의
// 성질(길이 L 의 코드는 firstCode[L] 부터 연속)을 그대로 써서 비트를 한 칸씩
// 늘려 가며 찾는다. **그 fallback 을 지우면 긴 코드를 쓰는 어떤 파일도 복원되지
// 않는다** — 그리고 치우친 분포는 언제나 긴 코드를 만든다.
class HuffmanDecoder {
public:
    bool Build(const uint8_t* lengths) {
        if (!HuffmanKraftComplete(lengths)) return false;
        HuffmanCodes codes;
        if (!HuffmanAssignCanonical(lengths, codes)) return false;

        for (size_t i = 0; i <= kHuffmanMaxLength; ++i) { count_[i] = 0; }
        for (size_t i = 0; i < kHuffmanSymbols; ++i) ++count_[lengths[i]];
        count_[0] = 0;

        sorted_.clear();
        sorted_.reserve(kHuffmanSymbols);
        for (uint8_t bits = 1; bits <= kHuffmanMaxLength; ++bits)
            for (size_t i = 0; i < kHuffmanSymbols; ++i)
                if (lengths[i] == bits) sorted_.push_back(static_cast<uint16_t>(i));

        uint32_t code = 0, index = 0;
        for (uint8_t bits = 1; bits <= kHuffmanMaxLength; ++bits) {
            firstCode_[bits] = code;
            firstIndex_[bits] = index;
            index += count_[bits];
            code = (code + count_[bits]) << 1;
        }

        lookupSymbol_.assign(size_t{1} << kHuffmanLookupBits, 0);
        lookupLength_.assign(size_t{1} << kHuffmanLookupBits, 0);
        for (size_t i = 0; i < kHuffmanSymbols; ++i) {
            const uint8_t bits = lengths[i];
            if (bits == 0 || bits > kHuffmanLookupBits) continue;
            const uint32_t shift = static_cast<uint32_t>(kHuffmanLookupBits - bits);
            const uint32_t base = static_cast<uint32_t>(codes.code[i]) << shift;
            for (uint32_t fill = 0; fill < (1u << shift); ++fill) {
                lookupSymbol_[base + fill] = static_cast<uint16_t>(i);
                lookupLength_[base + fill] = bits;
            }
        }
        ready_ = true;
        return true;
    }

    // coded[0..codedBytes) 에서 정확히 `expected` 심볼을 꺼낸다. 남는 비트는
    // 마지막 바이트의 채움뿐이어야 한다 — 그보다 많이 남으면 이 스트림은 이
    // 길이표로 만들어진 것이 아니다.
    bool Decode(const uint8_t* coded, size_t codedBytes, size_t expected,
                std::vector<uint8_t>& out) const {
        if (!ready_) return false;
        out.clear();
        out.reserve(expected);
        const size_t totalBits = codedBytes * 8;
        size_t at = 0;
        for (size_t produced = 0; produced < expected; ++produced) {
            const uint32_t window = Peek(coded, codedBytes, at);
            const uint8_t fast = lookupLength_[window];
            if (fast != 0) {
                at += fast;
                if (at > totalBits) return false;
                out.push_back(static_cast<uint8_t>(lookupSymbol_[window]));
                continue;
            }
            // 긴 코드. 한 비트씩 늘려 가며 그 길이의 구간에 들어오는지 본다.
            uint32_t cursor = window;   // 9비트를 이미 읽은 상태에서 출발
            uint32_t bits = kHuffmanLookupBits;
            bool decoded = false;
            while (bits <= kHuffmanMaxLength) {
                if (count_[bits] != 0 && cursor >= firstCode_[bits] &&
                    cursor - firstCode_[bits] < count_[bits]) {
                    const uint32_t slot = firstIndex_[bits] + (cursor - firstCode_[bits]);
                    if (slot >= sorted_.size()) return false;
                    at += bits;
                    if (at > totalBits) return false;
                    out.push_back(static_cast<uint8_t>(sorted_[slot]));
                    decoded = true;
                    break;
                }
                ++bits;
                if (bits > kHuffmanMaxLength) break;
                cursor = (cursor << 1) | ReadBit(coded, codedBytes, at + bits - 1);
            }
            if (!decoded) return false;
        }
        if (at > totalBits) return false;
        return totalBits - at < 8;
    }
    // snippet-end: huffman-decoder

    bool Ready() const { return ready_; }

private:
    static uint32_t ReadBit(const uint8_t* coded, size_t codedBytes, size_t bit) {
        const size_t index = bit >> 3;
        if (index >= codedBytes) return 0;
        return (coded[index] >> (7 - (bit & 7))) & 1u;
    }
    static uint32_t Peek(const uint8_t* coded, size_t codedBytes, size_t at) {
        uint32_t window = 0;
        for (int i = 0; i < kHuffmanLookupBits; ++i)
            window = (window << 1) | ReadBit(coded, codedBytes, at + static_cast<size_t>(i));
        return window;
    }

    bool                  ready_ = false;
    uint32_t              count_[kHuffmanMaxLength + 1]{};
    uint32_t              firstCode_[kHuffmanMaxLength + 2]{};
    uint32_t              firstIndex_[kHuffmanMaxLength + 2]{};
    std::vector<uint16_t> sorted_;
    std::vector<uint16_t> lookupSymbol_;
    std::vector<uint8_t>  lookupLength_;
};

// 빈도 세기 → 길이표 → canonical → 비트 쓰기. 길이표는 호출자가 파일에 적는다.
inline bool HuffmanEncode(const uint8_t* data, size_t size, uint8_t* lengths,
                          std::vector<uint8_t>& coded) {
    if (size == 0) return false;
    uint32_t frequency[kHuffmanSymbols]{};
    for (size_t i = 0; i < size; ++i) ++frequency[data[i]];
    if (!HuffmanBuildLengths(frequency, lengths)) return false;
    HuffmanCodes codes;
    if (!HuffmanAssignCanonical(lengths, codes)) return false;
    BitWriter writer;
    for (size_t i = 0; i < size; ++i)
        writer.Put(codes.code[data[i]], codes.length[data[i]]);
    coded = writer.Bytes();
    return true;
}

}  // namespace algo
}  // namespace mega
