// Offline verification for the live-service game admission token.
//
// The control plane signs `base64url(json).base64url(hmac_sha256)` with a
// shared deployment secret. A game server verifies the signature, audience,
// and expiry before binding the token ID to a UDP endpoint.
//
// ── 여섯 번 훑던 것을 한 번으로 ───────────────────────────────────
// 페이로드에서 여섯 개의 필드를 뽑을 때, 예전에는 필드마다 `json.find(prefix)` 를
// 한 번씩 — 같은 비신뢰 문자열을 처음부터 여섯 번 — 훑었다. 속도가 문제였던 적은
// 없다. 페이로드는 수백 바이트다.
//
// 문제는 **첫 일치만 채택한다**는 것이었다. 같은 최상위 필드가 두 번 적힌
// 페이로드에서 앞의 것이 조용히 이긴다. 서명이 유효한 토큰 안에서 필드가 두 번
// 나오는 상태는 발급기의 결함이거나 주입이고, 둘 다 승인 대상이 아니다.
//
// 다중 패턴 자동자는 첫 일치가 아니라 **모든 일치**를 낸다. 그래서 세는 것이
// 가능해지고, 세는 것이 가능해지면 조용한 정책을 명시적 거부로 바꿀 수 있다.
// 여섯 패턴은 고정이므로 자동자는 프로세스당 한 번만 짓는다.
//
// 순서 불변 하나가 이 파일에서 바뀌지 않는다: **서명 검증이 필드 스캔보다 먼저
// 온다.** 서명이 깨진 입력에서는 자동자가 아예 돌지 않는다.

#pragma once

#include "algo/aho_corasick.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mega::live {

// 페이로드 상한. 서명이 맞더라도 무한정 긴 JSON 을 훑지 않는다 — 자동자는 한 번
// 훑지만 그 한 번의 길이에도 예산이 있어야 한다.
inline constexpr size_t kMaxAdmissionPayloadBytes = 4096;

struct GameAdmission {
    std::string playerId;
    std::string matchId;
    std::string serverId;
    std::string tokenId;
    int64_t expiresAt = 0;
};

namespace detail {

class Sha256 {
public:
    void Update(std::span<const uint8_t> input) {
        bitLength_ += static_cast<uint64_t>(input.size()) * 8;
        for (uint8_t byte : input) {
            block_[blockSize_++] = byte;
            if (blockSize_ == block_.size()) {
                Transform(block_.data());
                blockSize_ = 0;
            }
        }
    }

    std::array<uint8_t, 32> Finish() {
        const uint64_t messageBits = bitLength_;
        block_[blockSize_++] = 0x80;
        if (blockSize_ > 56) {
            while (blockSize_ < 64) block_[blockSize_++] = 0;
            Transform(block_.data());
            blockSize_ = 0;
        }
        while (blockSize_ < 56) block_[blockSize_++] = 0;
        for (int shift = 56; shift >= 0; shift -= 8)
            block_[blockSize_++] = static_cast<uint8_t>(messageBits >> shift);
        Transform(block_.data());

        std::array<uint8_t, 32> digest{};
        for (size_t i = 0; i < state_.size(); ++i) {
            digest[i * 4] = static_cast<uint8_t>(state_[i] >> 24);
            digest[i * 4 + 1] = static_cast<uint8_t>(state_[i] >> 16);
            digest[i * 4 + 2] = static_cast<uint8_t>(state_[i] >> 8);
            digest[i * 4 + 3] = static_cast<uint8_t>(state_[i]);
        }
        return digest;
    }

private:
    static constexpr std::array<uint32_t, 64> kRound = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };

    static uint32_t Rotate(uint32_t value, int bits) {
        return (value >> bits) | (value << (32 - bits));
    }

    void Transform(const uint8_t* block) {
        uint32_t words[64]{};
        for (size_t i = 0; i < 16; ++i) {
            words[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                       (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                       (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                       static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (size_t i = 16; i < 64; ++i) {
            const uint32_t s0 = Rotate(words[i - 15], 7) ^
                                Rotate(words[i - 15], 18) ^ (words[i - 15] >> 3);
            const uint32_t s1 = Rotate(words[i - 2], 17) ^
                                Rotate(words[i - 2], 19) ^ (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (size_t i = 0; i < 64; ++i) {
            const uint32_t s1 = Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25);
            const uint32_t choose = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + choose + kRound[i] + words[i];
            const uint32_t s0 = Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + majority;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    std::array<uint32_t, 8> state_ = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    std::array<uint8_t, 64> block_{};
    size_t blockSize_ = 0;
    uint64_t bitLength_ = 0;
};

inline std::array<uint8_t, 32> Hash(std::span<const uint8_t> input) {
    Sha256 hash;
    hash.Update(input);
    return hash.Finish();
}

inline std::array<uint8_t, 32> Hmac(
    std::span<const uint8_t> key, std::span<const uint8_t> message) {
    std::array<uint8_t, 64> normalized{};
    if (key.size() > normalized.size()) {
        const auto digest = Hash(key);
        std::copy(digest.begin(), digest.end(), normalized.begin());
    } else {
        std::copy(key.begin(), key.end(), normalized.begin());
    }
    std::array<uint8_t, 64> innerPad{};
    std::array<uint8_t, 64> outerPad{};
    for (size_t i = 0; i < normalized.size(); ++i) {
        innerPad[i] = normalized[i] ^ 0x36;
        outerPad[i] = normalized[i] ^ 0x5c;
    }
    Sha256 inner;
    inner.Update(innerPad);
    inner.Update(message);
    const auto innerDigest = inner.Finish();
    Sha256 outer;
    outer.Update(outerPad);
    outer.Update(innerDigest);
    return outer.Finish();
}

inline bool ConstantTimeEqual(
    std::span<const uint8_t> left, std::span<const uint8_t> right) {
    if (left.size() != right.size()) return false;
    uint8_t difference = 0;
    for (size_t i = 0; i < left.size(); ++i) difference |= left[i] ^ right[i];
    return difference == 0;
}

inline int Base64Value(char value, bool url) {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == (url ? '-' : '+')) return 62;
    if (value == (url ? '_' : '/')) return 63;
    return -1;
}

inline std::optional<std::vector<uint8_t>> DecodeBase64(
    std::string_view text, bool url) {
    if (text.size() % 4 == 1) return std::nullopt;
    std::vector<uint8_t> output;
    output.reserve((text.size() * 3) / 4 + 3);
    uint32_t accumulator = 0;
    int bits = 0;
    bool padding = false;
    for (char value : text) {
        if (value == '=') {
            padding = true;
            continue;
        }
        if (padding) return std::nullopt;
        const int decoded = Base64Value(value, url);
        if (decoded < 0) return std::nullopt;
        accumulator = (accumulator << 6) | static_cast<uint32_t>(decoded);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<uint8_t>(accumulator >> bits));
            accumulator &= (1u << bits) - 1u;
        }
    }
    if (bits != 0 && accumulator != 0) return std::nullopt;
    return output;
}

// 값 파싱은 **시작 자리를 받는다**. 어디서 시작할지는 자동자가 정하고, 여기서는
// 그 자리부터의 문법만 본다 — 찾기와 읽기를 갈라 두면 "첫 일치"라는 정책이
// 파서 안에 숨지 못한다.
inline std::optional<std::string> JsonStringAt(std::string_view json, size_t cursor) {
    std::string result;
    while (cursor < json.size()) {
        const char value = json[cursor++];
        if (value == '"') return result;
        if (value == '\\') {
            if (cursor >= json.size()) return std::nullopt;
            const char escaped = json[cursor++];
            switch (escaped) {
                case '"': case '\\': case '/': result.push_back(escaped); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default: return std::nullopt;
            }
        } else {
            result.push_back(value);
        }
    }
    return std::nullopt;
}

inline std::optional<int64_t> JsonIntegerAt(std::string_view json, size_t begin) {
    size_t end = begin;
    while (end < json.size() &&
           (json[end] == '-' || (json[end] >= '0' && json[end] <= '9'))) ++end;
    int64_t value = 0;
    const auto result = std::from_chars(json.data() + begin, json.data() + end, value);
    if (result.ec != std::errc{} || result.ptr != json.data() + end) return std::nullopt;
    return value;
}

// ── 필드 스캔 ─────────────────────────────────────────────────────
// 여섯 패턴 고정. 순서가 곧 id 이고, id 가 곧 아래 배열의 첨자다.
enum AdmissionField : uint32_t {
    kFieldKind = 0,
    kFieldPlayerId,
    kFieldMatchId,
    kFieldServerId,
    kFieldTokenId,
    kFieldExpiresAt,
    kFieldCount,
};

inline const char* AdmissionPattern(uint32_t field) {
    switch (field) {
        case kFieldKind:      return "\"Kind\":\"";
        case kFieldPlayerId:  return "\"PlayerId\":\"";
        case kFieldMatchId:   return "\"MatchId\":\"";
        case kFieldServerId:  return "\"ServerId\":\"";
        case kFieldTokenId:   return "\"TokenId\":\"";
        default:              return "\"ExpiresAt\":";
    }
}

// snippet-begin: admission-automaton
// 프로세스당 한 번. 패턴 집합이 고정이므로 토큰마다 다시 짓는 것은 순수한 낭비다.
inline const algo::AhoCorasick& AdmissionAutomaton() {
    static const algo::AhoCorasick automaton = [] {
        algo::AhoCorasick built;
        for (uint32_t field = 0; field < kFieldCount; ++field)
            built.Add(AdmissionPattern(field), field);
        built.Build();
        return built;
    }();
    return automaton;
}

// 페이로드를 한 번 훑어 필드별 (등장 횟수, 값이 시작하는 자리)를 채운다.
// **중복이 곧 거부**다 — 세지 않으면 앞의 것이 조용히 이긴다.
struct AdmissionFields {
    uint32_t count[kFieldCount]{};
    size_t   valueAt[kFieldCount]{};
};

// 이 페이로드를 몇 번 훑었는가. 두 가지를 관측 가능하게 만드는 계측이다 —
// 토큰 하나에 훑기가 정확히 한 번이라는 것, 그리고 **서명이 깨진 입력에서는
// 아예 돌지 않는다**는 순서 불변. 순서 불변은 코드를 읽어서는 확인되지만
// 검사로는 확인되지 않던 성질이었다.
inline std::atomic<uint64_t>& AdmissionScanCounter() {
    static std::atomic<uint64_t> scans{0};
    return scans;
}

inline AdmissionFields ScanAdmissionFields(std::string_view json) {
    AdmissionScanCounter().fetch_add(1, std::memory_order_relaxed);
    AdmissionFields fields;
    std::vector<algo::AhoCorasick::Match> matches;
    AdmissionAutomaton().Scan(json, matches);
    for (const algo::AhoCorasick::Match& match : matches) {
        if (match.id >= kFieldCount) continue;
        ++fields.count[match.id];
        if (fields.count[match.id] == 1) fields.valueAt[match.id] = match.end;
    }
    return fields;
}
// snippet-end: admission-automaton

}  // namespace detail

inline std::vector<uint8_t> LocalDevelopmentSecret() {
    static constexpr std::string_view phrase = "live-service-lab-local-secret";
    const auto digest = detail::Hash(std::span(
        reinterpret_cast<const uint8_t*>(phrase.data()), phrase.size()));
    return {digest.begin(), digest.end()};
}

inline std::optional<std::vector<uint8_t>> DecodeSecret(std::string_view base64) {
    auto decoded = detail::DecodeBase64(base64, false);
    if (!decoded || decoded->size() < 32) return std::nullopt;
    return decoded;
}

inline bool VerifyGameAdmission(
    std::string_view token,
    std::span<const uint8_t> secret,
    std::string_view expectedServerId,
    int64_t nowUnixSeconds,
    GameAdmission& admission) {
    const size_t separator = token.find('.');
    if (separator == 0 || separator == std::string_view::npos ||
        separator != token.rfind('.')) return false;
    const std::string_view body = token.substr(0, separator);
    const std::string_view signatureText = token.substr(separator + 1);
    const auto signature = detail::DecodeBase64(signatureText, true);
    const auto jsonBytes = detail::DecodeBase64(body, true);
    if (!signature || !jsonBytes || signature->size() != 32) return false;
    const auto expected = detail::Hmac(
        secret,
        std::span(reinterpret_cast<const uint8_t*>(body.data()), body.size()));
    if (!detail::ConstantTimeEqual(*signature, expected)) return false;

    // 여기까지 오면 서명이 맞다. 페이로드 훑기는 그 뒤에만 일어난다.
    if (jsonBytes->size() > kMaxAdmissionPayloadBytes) return false;
    const std::string json(jsonBytes->begin(), jsonBytes->end());

    // snippet-begin: admission-scan
    const detail::AdmissionFields fields = detail::ScanAdmissionFields(json);
    for (uint32_t field = 0; field < detail::kFieldCount; ++field) {
        // 없으면 불완전한 토큰이고, 둘 이상이면 발급기 결함이거나 주입이다.
        // 예전 코드는 후자를 통과시켰다 — 앞의 값을 조용히 골랐기 때문이다.
        if (fields.count[field] != 1) return false;
    }
    const auto kind = detail::JsonStringAt(json, fields.valueAt[detail::kFieldKind]);
    const auto playerId = detail::JsonStringAt(json, fields.valueAt[detail::kFieldPlayerId]);
    const auto matchId = detail::JsonStringAt(json, fields.valueAt[detail::kFieldMatchId]);
    const auto serverId = detail::JsonStringAt(json, fields.valueAt[detail::kFieldServerId]);
    const auto tokenId = detail::JsonStringAt(json, fields.valueAt[detail::kFieldTokenId]);
    const auto expiresAt = detail::JsonIntegerAt(json, fields.valueAt[detail::kFieldExpiresAt]);
    // snippet-end: admission-scan
    if (!kind || *kind != "game" || !playerId || playerId->empty() ||
        !matchId || matchId->empty() || !serverId || *serverId != expectedServerId ||
        !tokenId || tokenId->empty() || !expiresAt || *expiresAt <= nowUnixSeconds) {
        return false;
    }
    admission = {*playerId, *matchId, *serverId, *tokenId, *expiresAt};
    return true;
}

}  // namespace mega::live
