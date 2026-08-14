#include "live/admission_token.hpp"
#include "test_util.h"

#include <array>
#include <iomanip>
#include <sstream>
#include <string>
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

void RunTests() {
    TestHmacKnownVector();
    TestAdmissionContract();
}

}  // namespace

TEST_MAIN("admission")
