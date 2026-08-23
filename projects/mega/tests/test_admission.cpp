#include "algo/aho_corasick.hpp"
#include "live/admission_token.hpp"
#include "test_util.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace mega;

namespace {

std::string Hex(std::span<const uint8_t> bytes) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (uint8_t byte : bytes) output << std::setw(2) << static_cast<int>(byte);
    return output.str();
}

std::string Base64Url(std::span<const uint8_t> bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string output;
    uint32_t accumulator = 0;
    int bits = 0;
    for (uint8_t byte : bytes) {
        accumulator = (accumulator << 8) | byte;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            output.push_back(alphabet[(accumulator >> bits) & 63]);
        }
    }
    if (bits > 0) output.push_back(alphabet[(accumulator << (6 - bits)) & 63]);
    return output;
}

std::string SignedToken(const std::string& json, std::span<const uint8_t> secret) {
    const std::string body = Base64Url(std::span(
        reinterpret_cast<const uint8_t*>(json.data()), json.size()));
    const auto signature = live::detail::Hmac(
        secret,
        std::span(reinterpret_cast<const uint8_t*>(body.data()), body.size()));
    return body + "." + Base64Url(signature);
}

void TestHmacKnownVector() {
    std::array<uint8_t, 20> key{};
    key.fill(0x0b);
    const std::string message = "Hi There";
    const auto digest = live::detail::Hmac(
        key,
        std::span(reinterpret_cast<const uint8_t*>(message.data()), message.size()));
    CHECK_EQ(Hex(digest),
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
}

void TestAdmissionContract() {
    const std::vector<uint8_t> secret = live::LocalDevelopmentSecret();
    const std::string json =
        R"({"Kind":"game","PlayerId":"alice","MatchId":"match-1","ServerId":"mega-local-1","TokenId":"token-1","ExpiresAt":4102444800})";
    std::string token = SignedToken(json, secret);
    live::GameAdmission admission;
    CHECK(live::VerifyGameAdmission(
        token, secret, "mega-local-1", 2'000'000'000, admission));
    CHECK_EQ(admission.playerId, "alice");
    CHECK_EQ(admission.matchId, "match-1");
    CHECK_EQ(admission.tokenId, "token-1");
    CHECK(!live::VerifyGameAdmission(
        token, secret, "another-server", 2'000'000'000, admission));
    CHECK(!live::VerifyGameAdmission(
        token, secret, "mega-local-1", 4'102'444'800, admission));
    token[4] = token[4] == 'A' ? 'B' : 'A';
    CHECK(!live::VerifyGameAdmission(
        token, secret, "mega-local-1", 2'000'000'000, admission));
}

// ── 다중 패턴 자동자 ───────────────────────────────────────────────
//
// 여섯 필드를 한 번에 훑는다. 속도가 이유가 아니라, "모든 일치"를 얻으면 중복을
// 셀 수 있고 중복을 세면 조용한 정책을 명시적 거부로 바꿀 수 있기 때문이다.

// (1) 회귀. 정상 토큰이 예전과 같은 판정을 받는다 — 위의 TestAdmissionContract
// 가 그것이고, 이 절은 그 위에 얹힌다.

// (2) 같은 최상위 필드가 두 번 나오는 페이로드. **흡수 전에는 통과했다** —
// 첫 일치를 조용히 채택했기 때문이다.
void TestDuplicateFieldRejected() {
    const std::vector<uint8_t> secret = live::LocalDevelopmentSecret();
    live::GameAdmission admission;

    // 앞의 PlayerId 는 정상, 뒤의 것이 주입이다. 예전 코드는 앞의 것을 골라
    // "alice" 로 승인했다.
    const std::string duplicated =
        R"({"Kind":"game","PlayerId":"alice","MatchId":"match-1","ServerId":"mega-local-1",)"
        R"("TokenId":"token-1","ExpiresAt":4102444800,"PlayerId":"mallory"})";
    CHECK(!live::VerifyGameAdmission(SignedToken(duplicated, secret), secret,
                                     "mega-local-1", 2'000'000'000, admission));

    // 뒤에 붙은 것이 ServerId 여도 마찬가지다.
    const std::string twoServers =
        R"({"Kind":"game","PlayerId":"alice","MatchId":"m","ServerId":"mega-local-1",)"
        R"("TokenId":"t","ExpiresAt":4102444800,"ServerId":"mega-local-1"})";
    CHECK(!live::VerifyGameAdmission(SignedToken(twoServers, secret), secret,
                                     "mega-local-1", 2'000'000'000, admission));

    // 필드가 아예 빠진 페이로드도 거부다(개수가 1이 아닌 것은 전부 거부).
    const std::string missing =
        R"({"Kind":"game","PlayerId":"alice","MatchId":"m","ServerId":"mega-local-1",)"
        R"("ExpiresAt":4102444800})";
    CHECK(!live::VerifyGameAdmission(SignedToken(missing, secret), secret,
                                     "mega-local-1", 2'000'000'000, admission));
}

// (3) 차분 오라클. 자동자가 낸 일치 multiset 이 패턴별 naive 검색 multiset 과
// 같은가. 무작위 페이로드에는 패턴 조각을 섞는다 — 접두가 겹치는 자리에서만
// failure link 가 실제로 일한다.
void TestAutomatonMatchesNaiveSearch() {
    const algo::AhoCorasick& automaton = live::detail::AdmissionAutomaton();
    std::vector<algo::AhoCorasick::Match> found;

    uint32_t rng = 0xC0FFEEu;
    auto next = [&rng]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };

    size_t totalMatches = 0;
    for (int trial = 0; trial < 400; ++trial) {
        std::string payload;
        const int pieces = 4 + static_cast<int>(next() % 24u);
        for (int p = 0; p < pieces; ++p) {
            const uint32_t roll = next() % 10u;
            if (roll < 4) {
                // 패턴 하나를 통째로 — 진짜 일치가 나온다.
                payload += live::detail::AdmissionPattern(next() % live::detail::kFieldCount);
            } else if (roll < 7) {
                // 패턴의 앞 조각 — failure link 가 일하는 자리다.
                const std::string whole =
                    live::detail::AdmissionPattern(next() % live::detail::kFieldCount);
                payload += whole.substr(0, 1 + next() % whole.size());
            } else {
                // 잡음. 따옴표와 콜론을 섞어야 부분 일치가 실제로 생긴다.
                static const char alphabet[] = "\":{},abKPMSTEidnyerx0123456789";
                payload.push_back(alphabet[next() % (sizeof(alphabet) - 1)]);
            }
        }

        automaton.Scan(payload, found);

        // naive: 패턴마다 처음부터 끝까지 겹침 허용 검색.
        std::vector<std::pair<uint32_t, size_t>> expected;
        for (uint32_t field = 0; field < live::detail::kFieldCount; ++field) {
            const std::string pattern = live::detail::AdmissionPattern(field);
            size_t at = payload.find(pattern);
            while (at != std::string::npos) {
                expected.emplace_back(field, at + pattern.size());
                at = payload.find(pattern, at + 1);
            }
        }

        std::vector<std::pair<uint32_t, size_t>> actual;
        actual.reserve(found.size());
        for (const algo::AhoCorasick::Match& match : found)
            actual.emplace_back(match.id, match.end);
        std::sort(expected.begin(), expected.end());
        std::sort(actual.begin(), actual.end());
        CHECK(expected == actual);
        totalMatches += actual.size();

        // 일치의 시작 자리도 옳다 — 값 파서가 그 자리부터 읽는다.
        for (const algo::AhoCorasick::Match& match : found) {
            const std::string pattern = live::detail::AdmissionPattern(match.id);
            CHECK_EQ(match.end - match.begin, pattern.size());
            CHECK(payload.compare(match.begin, pattern.size(), pattern) == 0);
        }
    }
    // 일치가 하나도 없었다면 이 대조는 "둘 다 빈 목록"만 확인한 것이다.
    CHECK(totalMatches > 400);
    std::printf("  차분 오라클 400회, 일치 %zu건이 naive 검색과 동일\n", totalMatches);
}

// (4) 페이로드 길이 상한.
void TestOversizedPayloadRejected() {
    const std::vector<uint8_t> secret = live::LocalDevelopmentSecret();
    live::GameAdmission admission;
    std::string padded =
        R"({"Kind":"game","PlayerId":"alice","MatchId":"m","ServerId":"mega-local-1",)"
        R"("TokenId":"t","ExpiresAt":4102444800,"Pad":")";
    padded.append(live::kMaxAdmissionPayloadBytes, 'x');
    padded += "\"}";
    CHECK(padded.size() > live::kMaxAdmissionPayloadBytes);
    CHECK(!live::VerifyGameAdmission(SignedToken(padded, secret), secret,
                                     "mega-local-1", 2'000'000'000, admission));

    // 상한 안쪽의 같은 모양은 통과한다 — 거절의 이유가 길이라는 것.
    std::string small =
        R"({"Kind":"game","PlayerId":"alice","MatchId":"m","ServerId":"mega-local-1",)"
        R"("TokenId":"t","ExpiresAt":4102444800,"Pad":")";
    small.append(64, 'x');
    small += "\"}";
    CHECK(live::VerifyGameAdmission(SignedToken(small, secret), secret,
                                    "mega-local-1", 2'000'000'000, admission));
}

// (5) 순서 불변: 서명 검증이 필드 스캔보다 **먼저** 온다. 서명이 깨진 입력에서는
// 자동자가 아예 돌지 않는다. 훑기 계수기가 그 사실을 관측 가능하게 만든다.
void TestSignatureCheckedBeforeScan() {
    const std::vector<uint8_t> secret = live::LocalDevelopmentSecret();
    live::GameAdmission admission;
    const std::string json =
        R"({"Kind":"game","PlayerId":"alice","MatchId":"m","ServerId":"mega-local-1",)"
        R"("TokenId":"t","ExpiresAt":4102444800})";

    std::string broken = SignedToken(json, secret);
    broken[broken.size() - 2] = broken[broken.size() - 2] == 'A' ? 'B' : 'A';

    const uint64_t before =
        live::detail::AdmissionScanCounter().load(std::memory_order_relaxed);
    CHECK(!live::VerifyGameAdmission(broken, secret, "mega-local-1",
                                     2'000'000'000, admission));
    CHECK_EQ(live::detail::AdmissionScanCounter().load(std::memory_order_relaxed), before);

    // 그리고 정상 토큰은 정확히 **한 번** 훑는다. 예전에는 여섯 번이었다.
    CHECK(live::VerifyGameAdmission(SignedToken(json, secret), secret, "mega-local-1",
                                    2'000'000'000, admission));
    CHECK_EQ(live::detail::AdmissionScanCounter().load(std::memory_order_relaxed), before + 1);
    std::printf("  서명 실패 시 훑기 0회, 정상 토큰 훑기 1회\n");
}

void RunTests() {
    TestHmacKnownVector();
    TestAdmissionContract();
    TestDuplicateFieldRejected();
    TestAutomatonMatchesNaiveSearch();
    TestOversizedPayloadRejected();
    TestSignatureCheckedBeforeScan();
}

}  // namespace

TEST_MAIN("admission")
