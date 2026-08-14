// udp_transport -- non-blocking IPv4 UDP backend for the transport contract.
//
// LoopbackNet remains the deterministic fault-injection backend. This class is
// the product path: two independent processes exchange real datagrams through
// the operating system socket stack.

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

namespace mega {
namespace net {

class UdpTransport final : public ITransport {
public:
    static constexpr size_t kMaxDatagram = 1200;

    UdpTransport() {
#ifdef _WIN32
        WSADATA data{};
        runtimeReady_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        runtimeReady_ = true;
#endif
    }

    ~UdpTransport() override {
        Close();
#ifdef _WIN32
        if (runtimeReady_) WSACleanup();
#endif
    }

    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    bool OpenHost(uint16_t port) {
        Close();
        if (!runtimeReady_ || !CreateSocket()) return false;

        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(port);
        if (::bind(socket_, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
            SetLastSocketError("bind");
            Close();
            return false;
        }
        hostMode_ = true;
        return UpdateLocalPort();
    }

    bool OpenClient(const std::string& host, uint16_t port) {
        Close();
        if (!runtimeReady_ || !CreateSocket()) return false;

        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = 0;
        if (::bind(socket_, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
            SetLastSocketError("bind");
            Close();
            return false;
        }

        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &remote.sin_addr) != 1) {
            lastError_ = "join address must be an IPv4 literal";
            Close();
            return false;
        }
        std::memcpy(&peer_, &remote, sizeof(remote));
        peerLength_ = sizeof(remote);
        peerKnown_ = true;
        hostMode_ = false;
        return UpdateLocalPort();
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
        peerKnown_ = false;
        peerLength_ = 0;
        pendingPeerKnown_ = false;
        pendingPeerLength_ = 0;
        localPort_ = 0;
        hostMode_ = false;
    }

    void Send(const Bytes& data) override {
        if (socket_ == kInvalidSocket || !peerKnown_ || data.empty() || data.size() > kMaxDatagram) return;
        int sent = ::sendto(socket_, reinterpret_cast<const char*>(data.data()),
                            static_cast<int>(data.size()), 0,
                            reinterpret_cast<const sockaddr*>(&peer_), peerLength_);
        if (sent < 0) SetLastSocketError("sendto");
    }

    bool Receive(Bytes& out) override {
        if (socket_ == kInvalidSocket) return false;

        std::array<uint8_t, kMaxDatagram> buffer{};
        sockaddr_storage sender{};
        SocketLength senderLength = sizeof(sender);
        int received = ::recvfrom(socket_, reinterpret_cast<char*>(buffer.data()),
                                  static_cast<int>(buffer.size()), 0,
                                  reinterpret_cast<sockaddr*>(&sender), &senderLength);
        if (received < 0) {
            if (WouldBlock()) return false;
            SetLastSocketError("recvfrom");
            return false;
        }
        if (received == 0) return false;

        if (!peerKnown_) {
            if (!hostMode_) return false;
            pendingPeer_ = sender;
            pendingPeerLength_ = senderLength;
            pendingPeerKnown_ = true;
        } else if (!SameEndpoint(sender, senderLength)) {
            return false;
        }

        out.assign(buffer.begin(), buffer.begin() + received);
        return true;
    }

    // Host callers must validate the application handshake before accepting a
    // sender. This prevents an arbitrary first datagram from pinning the
    // single-peer transport for the rest of the process.
    bool AcceptLastSender() {
        if (!hostMode_ || peerKnown_ || !pendingPeerKnown_) return false;
        peer_ = pendingPeer_;
        peerLength_ = pendingPeerLength_;
        peerKnown_ = true;
        pendingPeerKnown_ = false;
        pendingPeerLength_ = 0;
        return true;
    }

    bool IsOpen() const { return socket_ != kInvalidSocket; }
    bool PeerKnown() const { return peerKnown_; }
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

    bool CreateSocket() {
        socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_ == kInvalidSocket) {
            SetLastSocketError("socket");
            return false;
        }
#ifdef _WIN32
        u_long nonBlocking = 1;
        if (ioctlsocket(socket_, FIONBIO, &nonBlocking) != 0) {
            SetLastSocketError("ioctlsocket");
            Close();
            return false;
        }
#else
        int flags = fcntl(socket_, F_GETFL, 0);
        if (flags < 0 || fcntl(socket_, F_SETFL, flags | O_NONBLOCK) != 0) {
            SetLastSocketError("fcntl");
            Close();
            return false;
        }
#endif
        lastError_.clear();
        return true;
    }

    bool UpdateLocalPort() {
        sockaddr_in local{};
        SocketLength length = sizeof(local);
        if (getsockname(socket_, reinterpret_cast<sockaddr*>(&local), &length) != 0) {
            SetLastSocketError("getsockname");
            Close();
            return false;
        }
        localPort_ = ntohs(local.sin_port);
        return true;
    }

    bool SameEndpoint(const sockaddr_storage& other, SocketLength otherLength) const {
        if (peerLength_ != otherLength || other.ss_family != peer_.ss_family) return false;
        if (other.ss_family != AF_INET) return false;
        const auto* a = reinterpret_cast<const sockaddr_in*>(&peer_);
        const auto* b = reinterpret_cast<const sockaddr_in*>(&other);
        return a->sin_port == b->sin_port && a->sin_addr.s_addr == b->sin_addr.s_addr;
    }

    bool WouldBlock() const {
#ifdef _WIN32
        int error = WSAGetLastError();
        return error == WSAEWOULDBLOCK;
#else
        return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
    }

    void SetLastSocketError(const char* operation) {
#ifdef _WIN32
        lastError_ = std::string(operation) + " failed: WSA " + std::to_string(WSAGetLastError());
#else
        lastError_ = std::string(operation) + " failed: errno " + std::to_string(errno);
#endif
    }

    SocketHandle socket_ = kInvalidSocket;
    sockaddr_storage peer_{};
    sockaddr_storage pendingPeer_{};
    SocketLength peerLength_ = 0;
    SocketLength pendingPeerLength_ = 0;
    uint16_t localPort_ = 0;
    bool runtimeReady_ = false;
    bool hostMode_ = false;
    bool peerKnown_ = false;
    bool pendingPeerKnown_ = false;
    std::string lastError_;
};

}  // namespace net
}  // namespace mega
