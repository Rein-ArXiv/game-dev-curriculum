#include "game/defense_server.hpp"
#include "net/udp_transport.hpp"
#include "test_util.h"

#include <filesystem>
#include <span>

using namespace mega;
using namespace mega::game;
namespace wire = mega::game::wire;

static std::string Base64Url(std::span<const uint8_t> bytes) {
    constexpr char alphabet[] =
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

static std::string Token(const std::string& player, const std::string& match = "external-1") {
    const std::string json = "{\"Kind\":\"game\",\"PlayerId\":\"" + player +
        "\",\"MatchId\":\"" + match + "\",\"ServerId\":\"assignment-test\","
        "\"TokenId\":\"token-" + player + "\",\"ExpiresAt\":4102444800}";
    const auto body = Base64Url(std::span(
        reinterpret_cast<const uint8_t*>(json.data()), json.size()));
    const auto signature = live::detail::Hmac(live::LocalDevelopmentSecret(), std::span(
        reinterpret_cast<const uint8_t*>(body.data()), body.size()));
    return body + "." + Base64Url(signature);
}

static void TestOptions() {
    DefenseServerOptions options;
    CHECK(options.ValidAssignment());
    options.assignedPlayers = {"alice", "bob"};
    CHECK(!options.ValidAssignment());
    options.assignedMatchId = "external-1";
    CHECK(options.ValidAssignment());
    options.assignedPlayers = {"alice", "alice"};
    CHECK(!options.ValidAssignment());
    options.assignedPlayers = {"alice"};
    CHECK(!options.ValidAssignment());
    options.assignedPlayers = {"alice", "bob", "eve"};
    CHECK(!options.ValidAssignment());
    options.assignedPlayers = {"alice", ""};
    CHECK(!options.ValidAssignment());
    options.assignedPlayers = {"alice", "has space"};
    CHECK(!options.ValidAssignment());
    options.assignedPlayers = {"alice", std::string(65, 'x')};
    CHECK(!options.ValidAssignment());
    DefenseServer invalid;
    CHECK(!invalid.Init(options));

    std::vector<std::string> args{"server", "--match-id", "external-1",
                                  "--player-id", "alice", "--player-id", "bob"};
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    DefenseServerOptions parsed;
    CHECK(ParseDefenseServerOptions(static_cast<int>(argv.size()), argv.data(), parsed));
    CHECK_EQ(parsed.assignedMatchId, std::string("external-1"));
    CHECK_EQ(parsed.assignedPlayers.size(), size_t{2});
    DefenseServerOptions incomplete;
    CHECK(!ParseDefenseServerOptions(5, argv.data(), incomplete));
}

static void TestAdmission(const std::string& assets) {
    net::UdpTransport reservation;
    CHECK(reservation.OpenHost(0));
    DefenseServerOptions options;
    options.port = reservation.LocalPort();
    reservation.Close();
    options.assets = assets;
    options.serverId = "assignment-test";
    options.assignedMatchId = "external-1";
    options.assignedPlayers = {"alice", "bob"};
    DefenseServer server;
    const bool initialized = server.Init(options);
    CHECK(initialized);
    if (!initialized) return;
    CHECK(server.ContentVersion() > 0);

    net::UdpTransport first, second, outsider;
    CHECK(first.OpenClient("127.0.0.1", options.port));
    CHECK(second.OpenClient("127.0.0.1", options.port));
    CHECK(outsider.OpenClient("127.0.0.1", options.port));
    const auto pump = [&] {
        const auto deadline = DefenseServer::Clock::now() + std::chrono::milliseconds(80);
        do {
            server.Step();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (DefenseServer::Clock::now() < deadline);
    };
    const auto hello = [&](const std::string& player, const std::string& match = "external-1") {
        return wire::WriteAdmissionHello(Token(player, match), {}, server.ContentHash());
    };
    CHECK(first.Send(wire::WriteQueueRequest("alice", 1000)));
    CHECK(second.Send(wire::WriteQueueRequest("bob", 1000)));
    CHECK(first.Send(hello("alice", "wrong-match")));
    CHECK(outsider.Send(hello("eve")));
    CHECK(first.Send(wire::WriteAdmissionHello(Token("alice"), {}, server.ContentHash() ^ 1)));
    pump();
    CHECK_EQ(server.ClientCount(), size_t{0});
    CHECK_EQ(server.MatchesFormed(), uint64_t{0});
    CHECK_EQ(server.RejectedPackets(), uint64_t{5});
    CHECK_EQ(server.ContentRejects(), uint64_t{1});

    CHECK(first.Send(hello("alice")));
    pump();
    CHECK_EQ(server.ClientCount(), size_t{1});
    CHECK_EQ(server.GrantsSent(), uint64_t{1});
    net::Bytes packet;
    bool initialGrant = false;
    while (first.Receive(packet)) {
        wire::AdmissionGrant grant;
        if (wire::ReadAdmissionGrant(packet, grant)) initialGrant = true;
    }
    CHECK(initialGrant); // Discard it to model loss before the client applies the grant.
    CHECK(first.Send(hello("alice")));
    pump();
    bool recoveredGrant = false;
    while (first.Receive(packet)) {
        wire::AdmissionGrant grant;
        if (wire::ReadAdmissionGrant(packet, grant)) {
            recoveredGrant = true;
            CHECK_EQ(grant.contentHash, server.ContentHash());
        }
    }
    CHECK(recoveredGrant);
    CHECK_EQ(server.ClientCount(), size_t{1});
    CHECK_EQ(server.GrantsSent(), uint64_t{2});

    // Even an already admitted endpoint must not bypass the content check on retry.
    CHECK(first.Send(wire::WriteAdmissionHello(Token("alice"), {}, server.ContentHash() ^ 1)));
    CHECK(outsider.Send(hello("alice"))); // The same token cannot claim a second endpoint.
    CHECK(second.Send(hello("bob")));
    pump();
    CHECK_EQ(server.ContentRejects(), uint64_t{2});
    CHECK_EQ(server.ClientCount(), size_t{2});
    CHECK_EQ(server.GrantsSent(), uint64_t{3});
    CHECK(server.Tick() > 0);
    CHECK(server.SnapshotsSent() >= 2);
}

int main(int argc, char** argv) {
    (void)argc;
    TestOptions();
    TestAdmission((std::filesystem::path(argv[0]).parent_path() / "assets").string());
    std::printf("[test] external_assignment: %d checks, %d failures\n",
                megatest::g_checks, megatest::g_failures);
    return megatest::g_failures ? 1 : 0;
}
