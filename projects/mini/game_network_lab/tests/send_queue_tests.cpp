// The per-connection queued send byte limit is a backend contract that only the
// real backend can demonstrate: a receiver that never reads must not be hidden
// behind an unbounded queue. The test drives CreatePlatformServer() directly,
// connects a loopback TCP client that never calls recv, and keeps sending until
// the backend gives up on that connection.
//
// The two backends report the giving-up differently (IOCP returns false from
// SendStream, epoll queues the send and closes asynchronously), so the shared
// pass criterion is the observable one both promise: exactly one Disconnected
// event carrying kSendQueueOverflowError.

#include "network_types.h"
#include "step_cases.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace {

int Fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

void CloseSocket(SocketHandle socket) {
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

std::uint16_t ReservePort(int type, int protocol) {
    SocketHandle socket = ::socket(AF_INET, type, protocol);
    if (socket == kInvalidSocket) return 0;
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = 0;
    if (bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        CloseSocket(socket);
        return 0;
    }
#if defined(_WIN32)
    int length = sizeof(local);
#else
    socklen_t length = sizeof(local);
#endif
    if (getsockname(socket, reinterpret_cast<sockaddr*>(&local), &length) != 0) {
        CloseSocket(socket);
        return 0;
    }
    const std::uint16_t port = ntohs(local.sin_port);
    CloseSocket(socket);
    return port;
}

// A client with a tiny receive buffer that never reads: the server side send
// path blocks after a few tens of kilobytes no matter how the OS is tuned.
SocketHandle ConnectSilentClient(std::uint16_t port) {
    SocketHandle socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == kInvalidSocket) return kInvalidSocket;
    int receiveBuffer = 2048;
    setsockopt(socket, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&receiveBuffer), sizeof(receiveBuffer));
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server.sin_port = htons(port);
    if (connect(socket, reinterpret_cast<sockaddr*>(&server), sizeof(server)) != 0) {
        CloseSocket(socket);
        return kInvalidSocket;
    }
    return socket;
}

int RunBoundedSendQueue() {
#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return Fail("WSAStartup");
#endif

    gnl::ServerConfig config;
    config.tcpPort = ReservePort(SOCK_STREAM, IPPROTO_TCP);
    config.udpPort = ReservePort(SOCK_DGRAM, IPPROTO_UDP);
    config.loopbackOnly = true;
    if (config.tcpPort == 0 || config.udpPort == 0) return Fail("reserve loopback ports");
    config.workerCount = 1;
    config.prepostedAccepts = 4;
    config.prepostedDatagrams = 4;
    // Small enough that a stalled receiver reaches it in well under a second.
    config.maxQueuedSendBytesPerConnection = 8 * 1024;

    auto server = gnl::CreatePlatformServer();
    std::string error;
    if (!server->Start(config, error)) {
        std::cerr << "FAIL: server start: " << error << '\n';
        return 1;
    }

    SocketHandle client = ConnectSilentClient(config.tcpPort);
    if (client == kInvalidSocket) {
        server->Stop();
        return Fail("connect loopback client");
    }

    std::vector<gnl::NetworkEvent> events;
    gnl::ConnectionId connection = 0;
    for (int attempt = 0; attempt < 60 && connection == 0; ++attempt) {
        server->Poll(events, std::chrono::milliseconds(50));
        for (const gnl::NetworkEvent& event : events) {
            if (event.kind == gnl::NetworkEventKind::Connected) connection = event.connection;
        }
        events.clear();
    }
    if (connection == 0) {
        CloseSocket(client);
        server->Stop();
        return Fail("server never reported the accepted connection");
    }

    // 32 MiB of traffic aimed at a client that never reads. The queue limit has
    // to bite long before that; without a limit this simply buffers forever.
    const std::vector<std::byte> chunk(4096, std::byte{0xab});
    bool sendStreamRefused = false;
    int overflowDisconnects = 0;
    int otherDisconnects = 0;
    int sent = 0;
    for (; sent < 8192; ++sent) {
        if (!server->SendStream(connection, chunk)) {
            sendStreamRefused = true;
            break;
        }
        if (server->Poll(events, std::chrono::milliseconds(0)) != 0) {
            bool closed = false;
            for (const gnl::NetworkEvent& event : events) {
                if (event.kind == gnl::NetworkEventKind::Disconnected &&
                    event.connection == connection) {
                    closed = true;
                    if (event.error == gnl::kSendQueueOverflowError) ++overflowDisconnects;
                    else ++otherDisconnects;
                }
            }
            events.clear();
            if (closed) break;
        }
    }

    // Drain whatever the backend still owes, including the Disconnected event
    // the IOCP backend raises from inside the refused SendStream.
    for (int attempt = 0; attempt < 40; ++attempt) {
        server->Poll(events, std::chrono::milliseconds(25));
        for (const gnl::NetworkEvent& event : events) {
            if (event.kind == gnl::NetworkEventKind::Disconnected &&
                event.connection == connection) {
                if (event.error == gnl::kSendQueueOverflowError) ++overflowDisconnects;
                else ++otherDisconnects;
            }
        }
        events.clear();
        if (overflowDisconnects > 0 && attempt >= 4) break;
    }

    CloseSocket(client);
    server->Stop();
#if defined(_WIN32)
    WSACleanup();
#endif

    // snippet-begin: overflow-verdict
    if (sent >= 8192 && !sendStreamRefused && overflowDisconnects == 0) {
        return Fail("32 MiB was queued for a receiver that never reads: no send queue bound");
    }
    if (overflowDisconnects != 1) {
        std::cerr << "FAIL: expected exactly one Disconnected(kSendQueueOverflowError), got "
                  << overflowDisconnects << " after " << sent << " sends\n";
        return 1;
    }
    if (otherDisconnects != 0) {
        std::cerr << "FAIL: connection also closed for an unrelated reason ("
                  << otherDisconnects << " events)\n";
        return 1;
    }
    // snippet-end: overflow-verdict

    // The number of sends the peer absorbs before the bound bites depends on the
    // kernel send buffer, so it is reported rather than asserted. What the case
    // asserts is the pair after the arrow.
    gnl_test::Observe(
        "limit=8KiB, gave up after " + std::to_string(sent) + " of at most 8192 "
        "4KiB sends -> Disconnected(kSendQueueOverflowError) x" +
        std::to_string(overflowDisconnects) + ", other closes " +
        std::to_string(otherDisconnects));
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    return gnl_test::Run("game_network_send_queue_tests",
                         {
                             {"step02", "bounded_send_queue", RunBoundedSendQueue},
                         },
                         argc, argv);
}
