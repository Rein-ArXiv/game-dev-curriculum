#pragma once

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <memory>
#include <vector>

namespace gnl {

using ConnectionId = std::uint64_t;

struct Endpoint {
    std::array<std::byte, 128> storage{};
    std::uint32_t size = 0;

    friend bool operator==(const Endpoint&, const Endpoint&) = default;
};

enum class NetworkEventKind : std::uint8_t {
    Connected,
    Disconnected,
    StreamBytes,
    Datagram,
};

struct NetworkEvent {
    NetworkEventKind kind{};
    ConnectionId connection = 0;
    Endpoint endpoint{};
    std::vector<std::byte> payload;
    int error = 0;
};

// Both backends close a connection with this error code in the Disconnected
// event when the per-connection queued send byte limit would be exceeded.
inline constexpr int kSendQueueOverflowError = ENOBUFS;

struct ServerConfig {
    std::uint16_t tcpPort = 42000;
    std::uint16_t udpPort = 42001;
    std::uint32_t workerCount = 0;
    std::uint32_t prepostedAccepts = 16;
    std::uint32_t prepostedDatagrams = 32;
    std::uint32_t maxConnections = 4096;
    std::size_t maxQueuedSendBytesPerConnection = 256 * 1024;
    // true: 127.0.0.1 에만 bind. 테스트와 단일 PC 실행용 — 외부 인터페이스를 열지
    // 않으므로 Windows 방화벽의 "이 앱의 통신 허용" 알림이 뜨지 않는다.
    bool loopbackOnly = false;
};

class IServerBackend {
public:
    virtual ~IServerBackend() = default;

    virtual bool Start(const ServerConfig& config, std::string& error) = 0;
    virtual std::size_t Poll(std::vector<NetworkEvent>& output,
                             std::chrono::milliseconds timeout) = 0;
    virtual bool SendStream(ConnectionId connection,
                            std::span<const std::byte> bytes) = 0;
    virtual bool SendDatagram(const Endpoint& endpoint,
                              std::span<const std::byte> bytes) = 0;
    virtual void Disconnect(ConnectionId connection) = 0;
    virtual void Stop() = 0;
};

std::unique_ptr<IServerBackend> CreatePlatformServer();

} // namespace gnl
