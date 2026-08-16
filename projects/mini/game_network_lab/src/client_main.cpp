#include "protocol.h"
#include "server_runtime.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

class SocketRuntime {
public:
    SocketRuntime() {
#if defined(_WIN32)
        WSADATA data{};
        ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        ready_ = true;
#endif
    }
    ~SocketRuntime() {
#if defined(_WIN32)
        if (ready_) WSACleanup();
#endif
    }
    bool Ready() const { return ready_; }

private:
    bool ready_ = false;
};

void CloseSocket(SocketHandle socket) {
    if (socket == kInvalidSocket) return;
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

bool SendAll(SocketHandle socket, std::span<const std::byte> bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        int chunk = static_cast<int>(std::min<std::size_t>(
            bytes.size() - offset, static_cast<std::size_t>(0x7fffffff)));
        int sent = send(socket,
                        reinterpret_cast<const char*>(bytes.data() + offset),
                        chunk, 0);
        if (sent <= 0) return false;
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

SocketHandle ConnectTcp(const std::string& host, std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* result = nullptr;
    std::string service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &result) != 0) {
        return kInvalidSocket;
    }

    SocketHandle socket = kInvalidSocket;
    for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
        socket = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (socket == kInvalidSocket) continue;
        if (connect(socket, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) break;
        CloseSocket(socket);
        socket = kInvalidSocket;
    }
    freeaddrinfo(result);
    return socket;
}

bool ResolveUdp(const std::string& host, std::uint16_t port,
                sockaddr_storage& address, int& length) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* result = nullptr;
    std::string service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &result) != 0 ||
        result == nullptr || result->ai_addrlen > sizeof(address)) {
        if (result) freeaddrinfo(result);
        return false;
    }
    std::memcpy(&address, result->ai_addr, result->ai_addrlen);
    length = static_cast<int>(result->ai_addrlen);
    freeaddrinfo(result);
    return true;
}

void SetReceiveTimeout(SocketHandle socket, int milliseconds) {
#if defined(_WIN32)
    DWORD timeout = static_cast<DWORD>(milliseconds);
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout{};
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = (milliseconds % 1000) * 1000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
}

std::optional<gnl::ControlMessage> ReceiveControl(SocketHandle socket) {
    gnl::FrameDecoder decoder;
    std::array<std::byte, 4096> buffer{};
    for (;;) {
        int bytes = recv(socket, reinterpret_cast<char*>(buffer.data()),
                         static_cast<int>(buffer.size()), 0);
        if (bytes <= 0) return std::nullopt;
        if (!decoder.Push(std::span(buffer).first(static_cast<std::size_t>(bytes)))) {
            return std::nullopt;
        }
        if (auto message = decoder.Pop()) return message;
    }
}

void AppendI16(std::vector<std::byte>& output, std::int16_t value) {
    gnl::AppendU16(output, static_cast<std::uint16_t>(value));
}

std::int32_t ReadI32(std::span<const std::byte> input, std::size_t& offset) {
    auto value = gnl::ReadU32(input, offset);
    return value ? static_cast<std::int32_t>(*value) : 0;
}

std::uint32_t ParseUnsigned(const char* text, std::uint32_t fallback) {
    char* end = nullptr;
    unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > 0xfffffffful) return fallback;
    return static_cast<std::uint32_t>(value);
}

} // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    std::uint16_t tcpPort = 42000;
    std::uint16_t udpPort = 42001;
    std::uint32_t ticks = 180;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string_view option = argv[i];
        std::uint32_t value = ParseUnsigned(argv[i + 1], 0);
        if (option == "--host") host = argv[i + 1];
        else if (option == "--tcp-port" && value <= 65535) tcpPort = static_cast<std::uint16_t>(value);
        else if (option == "--udp-port" && value <= 65535) udpPort = static_cast<std::uint16_t>(value);
        else if (option == "--ticks") ticks = value;
    }

    SocketRuntime runtime;
    if (!runtime.Ready()) {
        std::cerr << "game_client: socket runtime initialization failed\n";
        return 1;
    }

    SocketHandle tcp = ConnectTcp(host, tcpPort);
    if (tcp == kInvalidSocket) {
        std::cerr << "game_client: TCP connect failed\n";
        return 1;
    }

    std::string name = "local-client";
    std::span<const char> nameChars(name.data(), name.size());
    auto nameBytes = std::as_bytes(nameChars);
    auto hello = gnl::EncodeControl(gnl::ControlType::Hello, nameBytes);
    if (!SendAll(tcp, hello)) {
        std::cerr << "game_client: hello send failed\n";
        CloseSocket(tcp);
        return 1;
    }

    auto welcome = ReceiveControl(tcp);
    if (!welcome || welcome->type != gnl::ControlType::Welcome ||
        welcome->payload.size() != 24) {
        std::cerr << "game_client: invalid welcome\n";
        CloseSocket(tcp);
        return 1;
    }
    std::size_t offset = 0;
    auto connection = gnl::ReadU64(welcome->payload, offset);
    if (!connection) {
        CloseSocket(tcp);
        return 1;
    }
    std::array<std::byte, 16> token{};
    std::copy_n(welcome->payload.begin() + static_cast<std::ptrdiff_t>(offset),
                token.size(), token.begin());

    SocketHandle udp = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_storage serverAddress{};
    int serverAddressLength = 0;
    if (udp == kInvalidSocket ||
        !ResolveUdp(host, udpPort, serverAddress, serverAddressLength)) {
        std::cerr << "game_client: UDP setup failed\n";
        CloseSocket(udp);
        CloseSocket(tcp);
        return 1;
    }
    SetReceiveTimeout(udp, 20);

    auto connectPacket = gnl::EncodeDatagram(
        gnl::DatagramType::Connect, *connection, 1, token);
    sendto(udp, reinterpret_cast<const char*>(connectPacket.data()),
           static_cast<int>(connectPacket.size()), 0,
           reinterpret_cast<sockaddr*>(&serverAddress), serverAddressLength);

    std::array<std::byte, 2048> receiveBuffer{};
    int received = recvfrom(udp, reinterpret_cast<char*>(receiveBuffer.data()),
                            static_cast<int>(receiveBuffer.size()), 0, nullptr, nullptr);
    if (received <= 0) {
        std::cerr << "game_client: UDP handshake timed out\n";
        CloseSocket(udp);
        CloseSocket(tcp);
        return 1;
    }
    auto accepted = gnl::DecodeDatagram(
        std::span(receiveBuffer).first(static_cast<std::size_t>(received)));
    if (!accepted || accepted->type != gnl::DatagramType::ConnectAccepted) {
        std::cerr << "game_client: UDP handshake rejected\n";
        CloseSocket(udp);
        CloseSocket(tcp);
        return 1;
    }

    std::cout << "connected: id=" << *connection << '\n';
    std::int32_t predictedX = 0;
    std::int32_t predictedY = 0;
    std::uint32_t snapshots = 0;
    auto nextTick = std::chrono::steady_clock::now();

    for (std::uint32_t sequence = 1; sequence <= ticks; ++sequence) {
        std::int16_t moveX = sequence < ticks / 2 ? 12 : -12;
        std::int16_t moveY = (sequence / 30) % 2 == 0 ? 4 : -4;
        std::vector<std::byte> inputPayload;
        AppendI16(inputPayload, moveX);
        AppendI16(inputPayload, moveY);
        auto input = gnl::EncodeDatagram(
            gnl::DatagramType::Input, *connection, sequence, inputPayload);
        sendto(udp, reinterpret_cast<const char*>(input.data()),
               static_cast<int>(input.size()), 0,
               reinterpret_cast<sockaddr*>(&serverAddress), serverAddressLength);

        predictedX += moveX;
        predictedY += moveY;
        received = recvfrom(udp, reinterpret_cast<char*>(receiveBuffer.data()),
                            static_cast<int>(receiveBuffer.size()), 0, nullptr, nullptr);
        if (received > 0) {
            auto packet = gnl::DecodeDatagram(
                std::span(receiveBuffer).first(static_cast<std::size_t>(received)));
            if (packet && packet->type == gnl::DatagramType::Snapshot &&
                packet->payload.size() == 12) {
                std::size_t snapshotOffset = 0;
                auto acknowledged = gnl::ReadU32(packet->payload, snapshotOffset);
                std::int32_t authoritativeX = ReadI32(packet->payload, snapshotOffset);
                std::int32_t authoritativeY = ReadI32(packet->payload, snapshotOffset);
                std::int32_t errorX = authoritativeX - predictedX;
                std::int32_t errorY = authoritativeY - predictedY;
                predictedX = authoritativeX;
                predictedY = authoritativeY;
                ++snapshots;
                std::cout << "snapshot=" << packet->sequence
                          << " ack=" << acknowledged.value_or(0)
                          << " pos=(" << authoritativeX << ',' << authoritativeY << ')'
                          << " correction=(" << errorX << ',' << errorY << ")\n";
            }
        }

        nextTick += gnl::kTickInterval;
        std::this_thread::sleep_until(nextTick);
    }

    std::cout << "complete: input_ticks=" << ticks
              << " snapshots=" << snapshots
              << " predicted=(" << predictedX << ',' << predictedY << ")\n";
    CloseSocket(udp);
    CloseSocket(tcp);
    return snapshots > 0 ? 0 : 2;
}
