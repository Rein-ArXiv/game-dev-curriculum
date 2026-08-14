#pragma once

#include "network_types.h"
#include "protocol.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <random>
#include <unordered_map>

namespace gnl {

// The fixed simulation step and the snapshot cadence are part of the observable
// runtime contract, not private tuning knobs: a test has to be able to advance
// the clock by exactly one tick. Defining them here keeps a single definition,
// so a test can never drift from the value the server actually runs at.
constexpr auto kTickInterval = std::chrono::microseconds(16'667);
constexpr std::uint64_t kSnapshotEveryTicks = 3;

struct RuntimeMetrics {
    std::uint64_t controlFrames = 0;
    std::uint64_t datagrams = 0;
    std::uint64_t rejectedPackets = 0;
    std::uint64_t snapshots = 0;
    std::uint64_t ticks = 0;
};

class GameServerRuntime {
public:
    explicit GameServerRuntime(IServerBackend& backend);

    void OnNetworkEvent(const NetworkEvent& event);
    void Advance(std::chrono::steady_clock::time_point now);
    const RuntimeMetrics& Metrics() const { return metrics_; }
    std::size_t ClientCount() const { return clients_.size(); }

private:
    struct ClientState {
        FrameDecoder decoder;
        std::array<std::byte, 16> token{};
        Endpoint endpoint{};
        bool helloComplete = false;
        bool udpBound = false;
        bool hasInput = false;
        std::uint32_t lastInputSequence = 0;
        std::int16_t moveX = 0;
        std::int16_t moveY = 0;
        std::int32_t positionX = 0;
        std::int32_t positionY = 0;
    };

    void OnConnected(ConnectionId id);
    void OnDisconnected(ConnectionId id);
    void OnStreamBytes(ConnectionId id, std::span<const std::byte> bytes);
    void OnControlMessage(ConnectionId id, const ControlMessage& message);
    void OnDatagram(const Endpoint& endpoint, std::span<const std::byte> bytes);
    void RunTick();
    void BroadcastSnapshots();
    std::array<std::byte, 16> NewToken();

    IServerBackend& backend_;
    std::unordered_map<ConnectionId, ClientState> clients_;
    std::mt19937_64 random_;
    std::chrono::steady_clock::time_point nextTick_{};
    std::uint32_t snapshotSequence_ = 0;
    RuntimeMetrics metrics_{};
};

} // namespace gnl

