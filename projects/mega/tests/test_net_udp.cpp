#include "net/udp_transport.hpp"
#include "test_util.h"

#include <chrono>
#include <thread>

using mega::net::Bytes;
using mega::net::UdpTransport;

static bool ReceiveWithin(UdpTransport& transport, Bytes& out, int milliseconds) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (transport.Receive(out)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

static void TestRealSocketRoundTrip() {
    UdpTransport host;
    UdpTransport client;
    CHECK(host.OpenHost(0));
    CHECK(host.LocalPort() != 0);
    CHECK(client.OpenClient("127.0.0.1", host.LocalPort()));

    Bytes ping{0x4d, 0x45, 0x47, 0x41};
    client.Send(ping);

    Bytes received;
    CHECK(ReceiveWithin(host, received, 500));
    CHECK(received == ping);
    CHECK(!host.PeerKnown());
    CHECK(host.AcceptLastSender());
    CHECK(host.PeerKnown());

    Bytes pong{0x4f, 0x4b};
    host.Send(pong);
    CHECK(ReceiveWithin(client, received, 500));
    CHECK(received == pong);
}

static void RunTests() {
    TestRealSocketRoundTrip();
}

TEST_MAIN("net_udp")
