#include "server_runtime.h"

#include <algorithm>
#include <limits>

namespace gnl {
namespace {

bool TokenEquals(std::span<const std::byte> payload,
                 const std::array<std::byte, 16>& token) {
    if (payload.size() != token.size()) return false;
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < token.size(); ++i) {
        diff |= std::to_integer<std::uint8_t>(payload[i] ^ token[i]);
    }
    return diff == 0;
}

std::int16_t DecodeI16(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint16_t value =
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 8u) |
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1]));
    return static_cast<std::int16_t>(value);
}

void AppendI32(std::vector<std::byte>& output, std::int32_t value) {
    AppendU32(output, static_cast<std::uint32_t>(value));
}

} // namespace

GameServerRuntime::GameServerRuntime(IServerBackend& backend)
    : backend_(backend), random_(std::random_device{}()) {}

void GameServerRuntime::OnNetworkEvent(const NetworkEvent& event) {
    switch (event.kind) {
    case NetworkEventKind::Connected:
        OnConnected(event.connection);
        break;
    case NetworkEventKind::Disconnected:
        OnDisconnected(event.connection);
        break;
    case NetworkEventKind::StreamBytes:
        OnStreamBytes(event.connection, event.payload);
        break;
    case NetworkEventKind::Datagram:
        OnDatagram(event.endpoint, event.payload);
        break;
    }
}

void GameServerRuntime::OnConnected(ConnectionId id) {
    clients_.try_emplace(id);
}

void GameServerRuntime::OnDisconnected(ConnectionId id) {
    clients_.erase(id);
}

void GameServerRuntime::OnStreamBytes(ConnectionId id,
                                      std::span<const std::byte> bytes) {
    auto it = clients_.find(id);
    if (it == clients_.end()) return;
    if (!it->second.decoder.Push(bytes)) {
        ++metrics_.rejectedPackets;
        backend_.Disconnect(id);
        return;
    }

    while (auto message = it->second.decoder.Pop()) {
        ++metrics_.controlFrames;
        OnControlMessage(id, *message);
        it = clients_.find(id);
        if (it == clients_.end()) return;
    }

    if (it->second.decoder.Failed()) {
        ++metrics_.rejectedPackets;
        backend_.Disconnect(id);
    }
}

void GameServerRuntime::OnControlMessage(ConnectionId id,
                                         const ControlMessage& message) {
    auto it = clients_.find(id);
    if (it == clients_.end()) return;
    ClientState& client = it->second;

    if (message.type == ControlType::Hello && !client.helloComplete) {
        client.token = NewToken();
        client.helloComplete = true;

        std::vector<std::byte> payload;
        AppendU64(payload, id);
        payload.insert(payload.end(), client.token.begin(), client.token.end());
        auto frame = EncodeControl(ControlType::Welcome, payload);
        if (!backend_.SendStream(id, frame)) backend_.Disconnect(id);
        return;
    }

    if (message.type == ControlType::Ping && client.helloComplete) {
        auto frame = EncodeControl(ControlType::Pong, message.payload);
        if (!backend_.SendStream(id, frame)) backend_.Disconnect(id);
        return;
    }

    ++metrics_.rejectedPackets;
    backend_.Disconnect(id);
}

void GameServerRuntime::OnDatagram(const Endpoint& endpoint,
                                   std::span<const std::byte> bytes) {
    ++metrics_.datagrams;
    auto message = DecodeDatagram(bytes);
    if (!message) {
        ++metrics_.rejectedPackets;
        return;
    }

    auto it = clients_.find(message->connection);
    if (it == clients_.end() || !it->second.helloComplete) {
        ++metrics_.rejectedPackets;
        return;
    }
    ClientState& client = it->second;

    if (message->type == DatagramType::Connect) {
        if (!TokenEquals(message->payload, client.token)) {
            ++metrics_.rejectedPackets;
            return;
        }
        client.endpoint = endpoint;
        client.udpBound = true;
        auto accepted = EncodeDatagram(DatagramType::ConnectAccepted,
                                       message->connection, message->sequence, {});
        backend_.SendDatagram(endpoint, accepted);
        return;
    }

    if (!client.udpBound || !(client.endpoint == endpoint)) {
        ++metrics_.rejectedPackets;
        return;
    }

    if (message->type == DatagramType::Input && message->payload.size() == 4) {
        if (client.hasInput &&
            !IsNewerSequence(message->sequence, client.lastInputSequence)) return;
        client.hasInput = true;
        client.lastInputSequence = message->sequence;
        client.moveX = DecodeI16(message->payload, 0);
        client.moveY = DecodeI16(message->payload, 2);
        return;
    }

    ++metrics_.rejectedPackets;
}

void GameServerRuntime::Advance(std::chrono::steady_clock::time_point now) {
    if (nextTick_ == std::chrono::steady_clock::time_point{}) nextTick_ = now;

    std::uint32_t catchUpTicks = 0;
    while (now >= nextTick_ && catchUpTicks < 4) {
        RunTick();
        nextTick_ += kTickInterval;
        ++catchUpTicks;
    }
    if (now >= nextTick_) nextTick_ = now + kTickInterval;
}

void GameServerRuntime::RunTick() {
    ++metrics_.ticks;
    for (auto& [id, client] : clients_) {
        static_cast<void>(id);
        client.positionX += client.moveX;
        client.positionY += client.moveY;
    }
    if (metrics_.ticks % kSnapshotEveryTicks == 0) BroadcastSnapshots();
}

void GameServerRuntime::BroadcastSnapshots() {
    ++snapshotSequence_;
    for (const auto& [id, client] : clients_) {
        if (!client.udpBound) continue;
        std::vector<std::byte> payload;
        AppendU32(payload, client.lastInputSequence);
        AppendI32(payload, client.positionX);
        AppendI32(payload, client.positionY);
        auto packet = EncodeDatagram(DatagramType::Snapshot, id,
                                     snapshotSequence_, payload);
        if (backend_.SendDatagram(client.endpoint, packet)) ++metrics_.snapshots;
    }
}

std::array<std::byte, 16> GameServerRuntime::NewToken() {
    std::array<std::byte, 16> token{};
    for (std::size_t word = 0; word < 2; ++word) {
        std::uint64_t value = random_();
        for (std::size_t byte = 0; byte < 8; ++byte) {
            token[word * 8 + byte] =
                static_cast<std::byte>((value >> (byte * 8)) & 0xffu);
        }
    }
    return token;
}

} // namespace gnl

