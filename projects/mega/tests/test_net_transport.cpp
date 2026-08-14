// loopback transport + packet 직렬화 검증.
#include "net/packet.hpp"
#include "net/transport.hpp"
#include "test_util.h"

#include <cstdint>
#include <vector>

using namespace mega::net;

static Bytes Msg(uint8_t tag, int n) {
    Bytes b(n);
    for (int i = 0; i < n; ++i) b[i] = (uint8_t)(tag + i);
    return b;
}

static void TestIdealDelivery() {
    LoopbackNet net;                       // 손실·지연 0
    LoopbackEndpoint a(net, true), b(net, false);

    a.Send(Msg(10, 4));
    a.Send(Msg(20, 4));
    net.Pump();

    Bytes r;
    CHECK(b.Receive(r)); CHECK_EQ(r[0], (uint8_t)10);
    CHECK(b.Receive(r)); CHECK_EQ(r[0], (uint8_t)20);
    CHECK(!b.Receive(r));                   // 더 없음
}

static void TestLoss() {
    NetConditions c; c.loss = 1.0f;        // 전부 손실
    LoopbackNet net(c);
    LoopbackEndpoint a(net, true), b(net, false);
    for (int i = 0; i < 10; ++i) a.Send(Msg(1, 2));
    net.Pump();
    Bytes r;
    CHECK(!b.Receive(r));                   // 아무것도 도착 안 함
}

static void TestLatency() {
    NetConditions c; c.latencyTicks = 3;
    LoopbackNet net(c);
    LoopbackEndpoint a(net, true), b(net, false);
    a.Send(Msg(7, 2));
    Bytes r;
    net.Pump(); CHECK(!b.Receive(r));       // 1틱: 아직
    net.Pump(); CHECK(!b.Receive(r));       // 2틱
    net.Pump(); CHECK(b.Receive(r));        // 3틱: 도착
    CHECK_EQ(r[0], (uint8_t)7);
}

static void TestReorderAllArrive() {
    NetConditions c; c.reorderProb = 0.8f; c.latencyTicks = 1; c.seed = 12345;
    LoopbackNet net(c);
    LoopbackEndpoint a(net, true), b(net, false);
    const int N = 50;
    for (int i = 0; i < N; ++i) { Bytes m(1); m[0] = (uint8_t)i; a.Send(m); }
    // 충분히 펌프하면 손실 없으니 전부 도착(순서는 섞일 수 있음).
    int got = 0; bool seen[N] = {false};
    for (int t = 0; t < 20; ++t) {
        net.Pump();
        Bytes r;
        while (b.Receive(r)) { seen[r[0]] = true; ++got; }
    }
    CHECK_EQ(got, N);
    bool all = true; for (int i = 0; i < N; ++i) if (!seen[i]) all = false;
    CHECK(all);                             // 손실 없음: 전부 도착
}

static void TestBidirectional() {
    LoopbackNet net;
    LoopbackEndpoint a(net, true), b(net, false);
    a.Send(Msg(100, 2));
    b.Send(Msg(200, 2));
    net.Pump();
    Bytes r;
    CHECK(b.Receive(r)); CHECK_EQ(r[0], (uint8_t)100);   // A→B
    CHECK(a.Receive(r)); CHECK_EQ(r[0], (uint8_t)200);   // B→A
}

static void TestPacketSerialize() {
    PacketHeader h; h.seq = 1000; h.ack = 999; h.ackBits = 0xDEADBEEF;
    Bytes payload = Msg(42, 8);
    Bytes wire = WritePacket(h, payload);

    PacketHeader h2; Bytes p2;
    CHECK(ReadPacket(wire, h2, p2));
    CHECK_EQ(h2.seq, (uint16_t)1000);
    CHECK_EQ(h2.ack, (uint16_t)999);
    CHECK_EQ(h2.ackBits, 0xDEADBEEFu);
    CHECK(p2 == payload);
}

static void TestSeqCompare() {
    CHECK(SeqGreater(5, 3));
    CHECK(!SeqGreater(3, 5));
    CHECK(SeqGreater(1, 65535));            // wrap: 1 이 65535 보다 최신
    CHECK(!SeqGreater(65535, 1));
    CHECK_EQ(SeqDiff(5, 3), 2);
    CHECK_EQ(SeqDiff(1, 65535), 2);         // 순환 거리
}

static void RunTests() {
    TestIdealDelivery();
    TestLoss();
    TestLatency();
    TestReorderAllArrive();
    TestBidirectional();
    TestPacketSerialize();
    TestSeqCompare();
}

TEST_MAIN("net_transport")
