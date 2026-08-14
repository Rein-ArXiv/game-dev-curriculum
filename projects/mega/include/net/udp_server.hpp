// Non-blocking IPv4 UDP socket that preserves each datagram's sender.
//
// UdpTransport intentionally models one peer. A dedicated authoritative
// process needs endpoint-addressed send/receive so admission state can be kept
// independently for several clients.

#pragma once

#include "net/transport.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace mega::net {

struct UdpEndpoint {
    sockaddr_storage address{};
#ifdef _WIN32
    int length = 0;
#else
    socklen_t length = 0;
#endif

    std::string Key() const {
        if (address.ss_family != AF_INET) return {};
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(&address);
        char text[INET_ADDRSTRLEN]{};
        if (!inet_ntop(AF_INET, &ipv4->sin_addr, text, sizeof(text))) return {};
        return std::string(text) + ":" + std::to_string(ntohs(ipv4->sin_port));
    }
};

struct UdpDatagram {
    UdpEndpoint sender;
    Bytes bytes;
};

class UdpServer {
public:
    static constexpr size_t kMaxDatagram = 1200;

    UdpServer() {
#ifdef _WIN32
        WSADATA data{};
        runtimeReady_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        runtimeReady_ = true;
#endif
    }

    ~UdpServer() {
        Close();
#ifdef _WIN32
        if (runtimeReady_) WSACleanup();
#endif
    }

    UdpServer(const UdpServer&) = delete;
    UdpServer& operator=(const UdpServer&) = delete;

    bool Open(uint16_t port) {
        Close();
        if (!runtimeReady_) {
            lastError_ = "socket runtime initialization failed";
            return false;
        }
        socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_ == kInvalidSocket) return SetError("socket");
        if (!SetNonBlocking()) {
            Close();
            return false;
        }
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(port);
        if (::bind(socket_, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
            SetError("bind");
            Close();
            return false;
        }
        SocketLength length = sizeof(local);
        if (getsockname(socket_, reinterpret_cast<sockaddr*>(&local), &length) != 0) {
            SetError("getsockname");
            Close();
            return false;
        }
        localPort_ = ntohs(local.sin_port);
        lastError_.clear();
        return true;
    }

    void Close() {
        if (socket_ != kInvalidSocket) {
#ifdef _WIN32
            closesocket(socket_);
#else
            close(socket_);
#endif
        }
        socket_ = kInvalidSocket;
        localPort_ = 0;
    }

    bool Receive(UdpDatagram& datagram) {
        if (socket_ == kInvalidSocket) return false;
        std::array<uint8_t, kMaxDatagram> buffer{};
        UdpEndpoint sender;
        sender.length = sizeof(sender.address);
        const int received = ::recvfrom(
            socket_, reinterpret_cast<char*>(buffer.data()),
            static_cast<int>(buffer.size()), 0,
            reinterpret_cast<sockaddr*>(&sender.address), &sender.length);
        if (received < 0) {
            if (WouldBlock()) return false;
            SetError("recvfrom");
            return false;
        }
        if (received == 0 || sender.address.ss_family != AF_INET) return false;
        datagram.sender = sender;
        datagram.bytes.assign(buffer.begin(), buffer.begin() + received);
        return true;
    }

    bool SendTo(const UdpEndpoint& endpoint, const Bytes& bytes) {
        if (socket_ == kInvalidSocket || bytes.empty() || bytes.size() > kMaxDatagram ||
            endpoint.address.ss_family != AF_INET) return false;
        const int sent = ::sendto(
            socket_, reinterpret_cast<const char*>(bytes.data()),
            static_cast<int>(bytes.size()), 0,
            reinterpret_cast<const sockaddr*>(&endpoint.address), endpoint.length);
        if (sent != static_cast<int>(bytes.size())) return SetError("sendto");
        return true;
    }

    uint16_t LocalPort() const { return localPort_; }
    const std::string& LastError() const { return lastError_; }

private:
#ifdef _WIN32
    using SocketHandle = SOCKET;
    using SocketLength = int;
    static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
    using SocketHandle = int;
    using SocketLength = socklen_t;
    static constexpr SocketHandle kInvalidSocket = -1;
#endif

    bool SetNonBlocking() {
#ifdef _WIN32
        u_long enabled = 1;
        if (ioctlsocket(socket_, FIONBIO, &enabled) != 0) return SetError("ioctlsocket");
#else
        const int flags = fcntl(socket_, F_GETFL, 0);
        if (flags < 0 || fcntl(socket_, F_SETFL, flags | O_NONBLOCK) != 0)
            return SetError("fcntl");
#endif
        return true;
    }

    bool WouldBlock() const {
#ifdef _WIN32
        return WSAGetLastError() == WSAEWOULDBLOCK;
#else
        return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
    }

    bool SetError(const char* operation) {
#ifdef _WIN32
        lastError_ = std::string(operation) + " failed: WSA " +
                     std::to_string(WSAGetLastError());
#else
        lastError_ = std::string(operation) + " failed: errno " +
                     std::to_string(errno);
#endif
        return false;
    }

    SocketHandle socket_ = kInvalidSocket;
    uint16_t localPort_ = 0;
    bool runtimeReady_ = false;
    std::string lastError_;
};

}  // namespace mega::net
