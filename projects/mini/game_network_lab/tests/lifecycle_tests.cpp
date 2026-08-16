// The backend lifetime contract is Start -> Stop -> Start on the same object.
// Both backends open Start() with an "already started" guard, which only means
// anything if a stopped server can be started again.
//
// The IOCP backend latches a stopping_ flag in Stop() so late completions stay
// quiet. A Start() that forgets to clear it comes back deaf: every preposted
// AcceptEx and WSARecvFrom is refused on the spot, so Start() fails outright.
// Asserting only that Start() returned true would still pass a server that came
// back without a single preposted accept, so every round also drives a real
// loopback connection through to the Connected and Disconnected events.

#include "network_types.h"
#include "step_cases.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
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

SocketHandle ConnectLoopback(std::uint16_t port) {
    SocketHandle socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == kInvalidSocket) return kInvalidSocket;
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

// connect() alone proves nothing: the kernel completes it out of the listen
// backlog whether or not a single accept is posted. Only the Connected event
// proves the backend still owns an accept.
bool AcceptsConnection(gnl::IServerBackend& server, std::uint16_t port) {
    SocketHandle client = ConnectLoopback(port);
    if (client == kInvalidSocket) return false;

    std::vector<gnl::NetworkEvent> events;
    gnl::ConnectionId connection = 0;
    for (int attempt = 0; attempt < 60 && connection == 0; ++attempt) {
        server.Poll(events, std::chrono::milliseconds(50));
        for (const gnl::NetworkEvent& event : events) {
            if (event.kind == gnl::NetworkEventKind::Connected) connection = event.connection;
        }
        events.clear();
    }
    if (connection == 0) {
        CloseSocket(client);
        return false;
    }

    // Close from the peer side and wait for the server to notice, so the server
    // is the passive closer and its listening port is free for the next round.
    CloseSocket(client);
    for (int attempt = 0; attempt < 60; ++attempt) {
        server.Poll(events, std::chrono::milliseconds(50));
        bool closed = false;
        for (const gnl::NetworkEvent& event : events) {
            if (event.kind == gnl::NetworkEventKind::Disconnected &&
                event.connection == connection) closed = true;
        }
        events.clear();
        if (closed) return true;
    }
    return false;
}

int RunBackendRestart() {
#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return Fail("WSAStartup");
#endif

    gnl::ServerConfig config;
    config.tcpPort = ReservePort(SOCK_STREAM, IPPROTO_TCP);
    config.udpPort = ReservePort(SOCK_DGRAM, IPPROTO_UDP);
    if (config.tcpPort == 0 || config.udpPort == 0) return Fail("reserve loopback ports");
    config.workerCount = 1;
    config.prepostedAccepts = 4;
    config.prepostedDatagrams = 4;

    auto server = gnl::CreatePlatformServer();

    constexpr int kRounds = 3;
    for (int round = 1; round <= kRounds; ++round) {
        std::string error;
        if (!server->Start(config, error)) {
            std::cerr << "FAIL: Start() round " << round
                      << " on a stopped server: " << error << '\n';
            return 1;
        }

        // The guard that makes the restart contract meaningful in the first place.
        std::string ignored;
        if (server->Start(config, ignored)) {
            server->Stop();
            std::cerr << "FAIL: round " << round
                      << " accepted a second Start() while running\n";
            return 1;
        }

        if (!AcceptsConnection(*server, config.tcpPort)) {
            server->Stop();
            std::cerr << "FAIL: round " << round
                      << " started but never accepted a loopback connection\n";
            return 1;
        }

        server->Stop();
    }

#if defined(_WIN32)
    WSACleanup();
#endif
    gnl_test::Observe(std::to_string(kRounds) +
                      " Start/Stop rounds: each refused a second Start(), then "
                      "accepted a loopback connection and reported its close");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    return gnl_test::Run("game_network_lifecycle_tests",
                         {
                             {gnl_test::kBackendStep, "backend_restart", RunBackendRestart},
                         },
                         argc, argv);
}
