// Unit tests for the authoritative runtime state machine.
//
// GameServerRuntime only ever talks to IServerBackend, so a recording fake
// backend plus hand-built NetworkEvent values make every transition
// deterministic: no socket, no worker thread, no wall clock. Everything the
// runtime decides is observed either through RuntimeMetrics or through the
// bytes it handed to the backend.

#include "protocol.h"
#include "server_runtime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class FakeBackend final : public gnl::IServerBackend {
public:
    struct StreamSend {
        gnl::ConnectionId connection = 0;
        std::vector<std::byte> bytes;
    };
    struct DatagramSend {
        gnl::Endpoint endpoint{};
        std::vector<std::byte> bytes;
    };

    bool Start(const gnl::ServerConfig&, std::string&) override { return true; }

    std::size_t Poll(std::vector<gnl::NetworkEvent>&,
                     std::chrono::milliseconds) override {
        return 0;
    }

    bool SendStream(gnl::ConnectionId connection,
                    std::span<const std::byte> bytes) override {
        streams.push_back({connection, {bytes.begin(), bytes.end()}});
        return true;
    }

    bool SendDatagram(const gnl::Endpoint& endpoint,
                      std::span<const std::byte> bytes) override {
        datagrams.push_back({endpoint, {bytes.begin(), bytes.end()}});
        return true;
    }

    void Disconnect(gnl::ConnectionId connection) override {
        disconnects.push_back(connection);
    }

    void Stop() override {}

    std::vector<StreamSend> streams;
    std::vector<DatagramSend> datagrams;
    std::vector<gnl::ConnectionId> disconnects;
};

// A non-zero origin so the "nextTick_ is still default constructed" branch in
// Advance() is not accidentally the same thing as "now is the epoch".
const std::chrono::steady_clock::time_point kBase =
    std::chrono::steady_clock::time_point{} + std::chrono::seconds(1000);

gnl::Endpoint MakeEndpoint(std::uint8_t tag) {
    gnl::Endpoint endpoint;
    endpoint.size = 16;
    endpoint.storage[0] = std::byte{tag};
    endpoint.storage[1] = std::byte{0x7f};
    return endpoint;
}

void Deliver(gnl::GameServerRuntime& runtime, gnl::NetworkEventKind kind,
             gnl::ConnectionId connection, const gnl::Endpoint& endpoint,
             std::span<const std::byte> payload) {
    gnl::NetworkEvent event;
    event.kind = kind;
    event.connection = connection;
    event.endpoint = endpoint;
    event.payload.assign(payload.begin(), payload.end());
    runtime.OnNetworkEvent(event);
}

// TCP Hello -> Welcome. Returns the 16 byte token the server minted, which is
// the only way a client can later claim a UDP endpoint.
std::array<std::byte, 16> Handshake(FakeBackend& fake,
                                    gnl::GameServerRuntime& runtime,
                                    gnl::ConnectionId id) {
    Deliver(runtime, gnl::NetworkEventKind::Connected, id, gnl::Endpoint{}, {});
    const std::size_t before = fake.streams.size();

    auto hello = gnl::EncodeControl(gnl::ControlType::Hello, {});
    Deliver(runtime, gnl::NetworkEventKind::StreamBytes, id, gnl::Endpoint{}, hello);

    Require(fake.streams.size() == before + 1,
            "Hello must produce exactly one stream send");
    Require(fake.streams.back().connection == id,
            "Welcome goes back on the same connection");

    gnl::FrameDecoder decoder;
    Require(decoder.Push(fake.streams.back().bytes), "Welcome frame is well formed");
    auto welcome = decoder.Pop();
    Require(welcome.has_value(), "Welcome frame decodes");
    Require(welcome->type == gnl::ControlType::Welcome, "reply type is Welcome");
    Require(welcome->payload.size() == 24,
            "Welcome payload is 8 byte id + 16 byte token");

    std::size_t offset = 0;
    auto carriedId = gnl::ReadU64(welcome->payload, offset);
    Require(carriedId.has_value(), "Welcome id field readable");
    Require(*carriedId == id, "Welcome carries the big-endian connection id");
    Require(!decoder.Pop().has_value(), "Welcome is a single frame");

    std::array<std::byte, 16> token{};
    std::copy(welcome->payload.begin() + 8, welcome->payload.end(), token.begin());
    return token;
}

void BindUdp(gnl::GameServerRuntime& runtime, gnl::ConnectionId id,
             const gnl::Endpoint& endpoint,
             std::span<const std::byte> token, std::uint32_t sequence) {
    auto packet = gnl::EncodeDatagram(gnl::DatagramType::Connect, id, sequence, token);
    Deliver(runtime, gnl::NetworkEventKind::Datagram, 0, endpoint, packet);
}

void SendInput(gnl::GameServerRuntime& runtime, gnl::ConnectionId id,
               const gnl::Endpoint& endpoint, std::uint32_t sequence,
               std::int16_t moveX, std::int16_t moveY) {
    std::vector<std::byte> payload;
    gnl::AppendU16(payload, static_cast<std::uint16_t>(moveX));
    gnl::AppendU16(payload, static_cast<std::uint16_t>(moveY));
    auto packet = gnl::EncodeDatagram(gnl::DatagramType::Input, id, sequence, payload);
    Deliver(runtime, gnl::NetworkEventKind::Datagram, 0, endpoint, packet);
}

struct Snapshot {
    std::uint32_t sequence = 0;
    std::uint32_t ack = 0;
    std::int32_t positionX = 0;
    std::int32_t positionY = 0;
};

std::vector<Snapshot> CollectSnapshots(const FakeBackend& fake) {
    std::vector<Snapshot> result;
    for (const auto& record : fake.datagrams) {
        auto message = gnl::DecodeDatagram(record.bytes);
        Require(message.has_value(), "the runtime only emits decodable datagrams");
        if (message->type != gnl::DatagramType::Snapshot) continue;
        Require(message->payload.size() == 12,
                "snapshot payload is ack + positionX + positionY");
        std::size_t offset = 0;
        auto ack = gnl::ReadU32(message->payload, offset);
        auto x = gnl::ReadU32(message->payload, offset);
        auto y = gnl::ReadU32(message->payload, offset);
        Require(ack && x && y, "snapshot fields readable");
        Snapshot snapshot;
        snapshot.sequence = message->sequence;
        snapshot.ack = *ack;
        snapshot.positionX = static_cast<std::int32_t>(*x);
        snapshot.positionY = static_cast<std::int32_t>(*y);
        result.push_back(snapshot);
    }
    return result;
}

void AdvanceTicks(gnl::GameServerRuntime& runtime, int count) {
    // Advance() only runs a tick when now has reached nextTick_, and it caps
    // catch-up at 4, so N ticks need N calls spaced one interval apart.
    for (int i = 0; i < count; ++i) runtime.Advance(kBase + gnl::kTickInterval * i);
}

// (1) Hello -> Welcome shape.
void TestHelloProducesWelcome() {
    FakeBackend fake;
    gnl::GameServerRuntime runtime(fake);
    auto token = Handshake(fake, runtime, 1);

    Require(runtime.ClientCount() == 1, "the client is tracked after Hello");
    Require(runtime.Metrics().controlFrames == 1, "exactly one control frame consumed");
    Require(runtime.Metrics().rejectedPackets == 0, "a clean handshake rejects nothing");
    Require(fake.disconnects.empty(), "a clean handshake does not disconnect");

    bool anyNonZero = false;
    for (std::byte value : token) {
        if (value != std::byte{0}) anyNonZero = true;
    }
    Require(anyNonZero, "the minted token is not all zero");

    // A second Hello on the same connection is a protocol error.
    auto hello = gnl::EncodeControl(gnl::ControlType::Hello, {});
    Deliver(runtime, gnl::NetworkEventKind::StreamBytes, 1, gnl::Endpoint{}, hello);
    Require(runtime.Metrics().rejectedPackets == 1, "a repeated Hello is rejected");
    Require(fake.disconnects.size() == 1, "a repeated Hello closes the connection");
}

// (2) A forged token must not bind a UDP endpoint.
void TestForgedTokenIsRejected() {
    FakeBackend fake;
    gnl::GameServerRuntime runtime(fake);
    auto token = Handshake(fake, runtime, 1);
    const gnl::Endpoint client = MakeEndpoint(1);

    std::array<std::byte, 16> forged = token;
    forged[7] ^= std::byte{0xff};

    const std::uint64_t rejectedBefore = runtime.Metrics().rejectedPackets;
    const std::size_t sentBefore = fake.datagrams.size();
    BindUdp(runtime, 1, client, forged, 4);

    Require(runtime.Metrics().rejectedPackets == rejectedBefore + 1,
            "a forged token increments rejectedPackets exactly once");
    Require(fake.datagrams.size() == sentBefore,
            "a forged token is never answered with ConnectAccepted");

    // The connection stayed unbound, so input from it changes nothing and no
    // snapshot is ever addressed to it.
    SendInput(runtime, 1, client, 10, 5, 5);
    AdvanceTicks(runtime, 3);
    Require(runtime.Metrics().ticks == 3, "three ticks ran");
    Require(CollectSnapshots(fake).empty(), "an unbound client receives no snapshot");
}

// (3) Input is only accepted from the endpoint that completed Connect.
void TestForeignEndpointIsRejected() {
    FakeBackend fake;
    gnl::GameServerRuntime runtime(fake);
    auto token = Handshake(fake, runtime, 1);
    const gnl::Endpoint bound = MakeEndpoint(1);
    const gnl::Endpoint spoofed = MakeEndpoint(2);

    BindUdp(runtime, 1, bound, token, 4);
    Require(fake.datagrams.size() == 1, "Connect from the first endpoint is accepted");
    auto accepted = gnl::DecodeDatagram(fake.datagrams.front().bytes);
    Require(accepted.has_value(), "ConnectAccepted decodes");
    Require(accepted->type == gnl::DatagramType::ConnectAccepted, "reply is ConnectAccepted");
    Require(fake.datagrams.front().endpoint == bound, "reply goes to the bound endpoint");

    const std::uint64_t rejectedBefore = runtime.Metrics().rejectedPackets;
    SendInput(runtime, 1, spoofed, 10, 9, 9);
    Require(runtime.Metrics().rejectedPackets == rejectedBefore + 1,
            "input from a foreign endpoint is rejected");

    AdvanceTicks(runtime, 3);
    auto snapshots = CollectSnapshots(fake);
    Require(snapshots.size() == 1, "one snapshot after three ticks");
    Require(snapshots.back().positionX == 0 && snapshots.back().positionY == 0,
            "spoofed input must not move the authoritative position");
    Require(snapshots.back().ack == 0,
            "spoofed input must not become the acknowledged sequence");
}

// (4) Duplicate and older sequences are dropped, and (6) the snapshot ack is
// the newest applied sequence.
void TestStaleInputIsDroppedAndAcked() {
    FakeBackend fake;
    gnl::GameServerRuntime runtime(fake);
    auto token = Handshake(fake, runtime, 1);
    const gnl::Endpoint client = MakeEndpoint(1);
    BindUdp(runtime, 1, client, token, 4);

    // Start near the wrap point so the same test also covers wrapped compares.
    const std::uint32_t applied = 0xfffffff0u;
    SendInput(runtime, 1, client, applied, 3, 0);       // applied
    SendInput(runtime, 1, client, applied, 50, 0);      // duplicate, dropped
    SendInput(runtime, 1, client, applied - 1, -7, 0);  // older, dropped

    AdvanceTicks(runtime, 6);
    auto snapshots = CollectSnapshots(fake);
    Require(snapshots.size() == 2, "six ticks produce two snapshots");
    Require(snapshots[0].positionX == 9, "three ticks at moveX=+3");
    Require(snapshots[1].positionX == 18, "six ticks at moveX=+3");
    Require(snapshots[0].ack == applied && snapshots[1].ack == applied,
            "the snapshot acks the newest applied input sequence, not the last received");

    // Sequence 2 is newer than 0xfffffff0 once wrapping is accounted for.
    SendInput(runtime, 1, client, 2, 1, 0);
    AdvanceTicks(runtime, 9);
    auto wrapped = CollectSnapshots(fake);
    Require(wrapped.size() == 3, "nine ticks produce three snapshots");
    Require(wrapped[2].ack == 2u, "a wrapped-newer sequence replaces the old one");
    Require(wrapped[2].positionX == 21, "moveX became +1 for ticks 7..9");
}

// (5) Fixed-step authority and the 1-in-3 snapshot cadence.
void TestAuthoritativeTickAndSnapshotCadence() {
    FakeBackend fake;
    gnl::GameServerRuntime runtime(fake);
    auto token = Handshake(fake, runtime, 1);
    const gnl::Endpoint client = MakeEndpoint(1);
    BindUdp(runtime, 1, client, token, 4);
    SendInput(runtime, 1, client, 7, 3, -2);

    AdvanceTicks(runtime, 12);
    Require(runtime.Metrics().ticks == 12, "twelve spaced Advance calls run twelve ticks");

    auto snapshots = CollectSnapshots(fake);
    Require(snapshots.size() == 4, "twelve ticks broadcast four snapshots");
    Require(runtime.Metrics().snapshots == 4, "the snapshot metric matches the sends");

    for (std::size_t i = 0; i < snapshots.size(); ++i) {
        const std::int32_t tick = static_cast<std::int32_t>(3 * (i + 1));
        Require(snapshots[i].sequence == static_cast<std::uint32_t>(i + 1),
                "snapshot sequence increases by one per broadcast");
        Require(snapshots[i].ack == 7, "every snapshot acks the applied input");
        Require(snapshots[i].positionX == 3 * tick,
                "positionX integrates moveX once per tick");
        Require(snapshots[i].positionY == -2 * tick,
                "positionY integrates moveY once per tick");
    }
    Require(snapshots.back().positionX == 36, "final authoritative positionX is 36");
    Require(snapshots.back().positionY == -24, "final authoritative positionY is -24");
}

// (7) A long stall is clamped, and the clock is resynchronised afterwards.
void TestCatchUpIsBounded() {
    FakeBackend fake;
    gnl::GameServerRuntime runtime(fake);
    Handshake(fake, runtime, 1);

    runtime.Advance(kBase);
    Require(runtime.Metrics().ticks == 1, "the first Advance runs exactly one tick");

    const auto late = kBase + gnl::kTickInterval * 10;
    runtime.Advance(late);
    Require(runtime.Metrics().ticks == 5,
            "a ten interval stall is clamped to four catch-up ticks");

    runtime.Advance(late);
    Require(runtime.Metrics().ticks == 5,
            "the clock was resynchronised, so the same now runs no further ticks");
}

} // namespace

int main() {
    TestHelloProducesWelcome();
    TestForgedTokenIsRejected();
    TestForeignEndpointIsRejected();
    TestStaleInputIsDroppedAndAcked();
    TestAuthoritativeTickAndSnapshotCadence();
    TestCatchUpIsBounded();

    std::cout << "game_network_runtime_tests: all checks passed\n";
    return 0;
}
