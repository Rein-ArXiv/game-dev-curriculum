// 신뢰·순서 채널 + RTT 추정 검증(손실·재정렬 하에서 복구·정렬).
#include "net/reliable_channel.hpp"
#include "net/rtt.hpp"
#include "net/transport.hpp"
#include "test_util.h"

#include <cstdint>
#include <limits>
#include <vector>

using namespace mega::net;

static Bytes MsgU32(uint32_t v) {
    Bytes b(4);
    b[0] = (uint8_t)(v & 0xFF); b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF); b[3] = (uint8_t)((v >> 24) & 0xFF);
    return b;
}
static uint32_t ReadU32(const Bytes& b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

// A → B 로 N 메시지를 보내고, 조건 하에서 전부 순서대로 도착하는지 확인.
static void RunDelivery(NetConditions cond, int N, int maxTicks) {
    LoopbackNet net(cond);
    LoopbackEndpoint epA(net, true), epB(net, false);
    ReliableOrderedChannel chA(epA), chB(epB);

    for (int i = 0; i < N; ++i) chA.Send(MsgU32((uint32_t)i));

    std::vector<uint32_t> got;
    for (int t = 0; t < maxTicks && (int)got.size() < N; ++t) {
        chA.Update();
        chB.Update();
        net.Pump();
        Bytes m;
        while (chB.Poll(m)) got.push_back(ReadU32(m));
    }

    CHECK_EQ((int)got.size(), N);            // 전부 도착
    bool ordered = true;
    for (int i = 0; i < (int)got.size(); ++i) if (got[i] != (uint32_t)i) ordered = false;
    CHECK(ordered);                          // 보낸 순서대로
}

static void TestIdeal()    { RunDelivery(NetConditions{}, 100, 500); }
static void TestLossy()    { NetConditions c; c.loss = 0.3f; c.seed = 7;   RunDelivery(c, 100, 2000); }
static void TestReorder()  { NetConditions c; c.reorderProb = 0.7f; c.latencyTicks = 2; c.seed = 11; RunDelivery(c, 100, 2000); }
static void TestHarsh()    { NetConditions c; c.loss = 0.4f; c.reorderProb = 0.5f; c.latencyTicks = 3; c.seed = 99; RunDelivery(c, 80, 4000); }

static void TestNoDuplicates() {
    // 손실+재전송이 있어도 앱은 각 메시지를 정확히 한 번만 받는다.
    NetConditions c; c.loss = 0.3f; c.seed = 5;
    LoopbackNet net(c);
    LoopbackEndpoint epA(net, true), epB(net, false);
    ReliableOrderedChannel chA(epA), chB(epB);
    const int N = 50;
    for (int i = 0; i < N; ++i) chA.Send(MsgU32((uint32_t)i));
    std::vector<int> count(N, 0);
    for (int t = 0; t < 2000 && (int)chB.Delivered() < N; ++t) {
        chA.Update(); chB.Update(); net.Pump();
        Bytes m;
        while (chB.Poll(m)) { uint32_t v = ReadU32(m); if (v < (uint32_t)N) ++count[v]; }
    }
    bool exactlyOnce = true;
    for (int i = 0; i < N; ++i) if (count[i] != 1) exactlyOnce = false;
    CHECK(exactlyOnce);                      // 중복·누락 없음
}

static void TestSequenceWrap() {
    LoopbackNet net(NetConditions{});
    LoopbackEndpoint epA(net, true), epB(net, false);
    const uint32_t initial = std::numeric_limits<uint32_t>::max() - 1;
    ReliableOrderedChannel chA(epA, initial), chB(epB, initial);
    for (uint32_t value = 0; value < 4; ++value) chA.Send(MsgU32(value));

    std::vector<uint32_t> got;
    for (int tick = 0; tick < 100 && got.size() < 4; ++tick) {
        chA.Update();
        chB.Update();
        net.Pump();
        Bytes message;
        while (chB.Poll(message)) got.push_back(ReadU32(message));
    }
    CHECK_EQ(got.size(), static_cast<size_t>(4));
    for (uint32_t value = 0; value < got.size(); ++value)
        CHECK_EQ(got[value], value);
}

static void TestRtt() {
    RttEstimator e(0.1f);
    CHECK(!e.Ready());
    e.Sample(100.0f);
    CHECK(e.Ready());
    CHECK(e.Rtt() == 100.0f);                // 첫 표본 = 초기값
    for (int i = 0; i < 200; ++i) e.Sample(50.0f);   // 50으로 수렴
    CHECK(e.Rtt() > 49.0f && e.Rtt() < 51.0f);
    CHECK(e.Jitter() < 2.0f);                // 안정되면 지터 작음
    // 스파이크 한 번엔 과민 반응 안 함.
    float before = e.Rtt();
    e.Sample(500.0f);
    CHECK(e.Rtt() < before + 100.0f);        // 천천히 따라감(평활)
    CHECK(e.Timeout() > e.Rtt());            // 타임아웃 = rtt + 4·jitter
}

// 위 TestRtt 는 추정기 자체를 가상 표본으로 재고, test_rtt_adaptive 는 가상
// 시계로 재전송 주기를 잰다. 여기서 남는 질문은 하나다 — **실제 채널 위에서**
// 표본이 나오는가. 나오지 않으면 추정기는 영원히 fallback 을 돌려주고, 그
// 상태는 "링크가 빠르다"와 밖에서 구분되지 않는다.
static void TestRttSamplerOverChannel() {
    RttSampler sampler(64, 0.1f);
    AckWindow  remote;
    PacketSequencer seq;

    double now = 0.0;
    int fresh = 0;
    for (int i = 0; i < 40; ++i) {
        const uint16_t sent = seq.Next();
        sampler.OnSent(sent, now);
        now += 12.0;                       // 편도 12ms
        remote.OnReceived(sent);           // 상대가 받았다
        now += 12.0;                       // 그 헤더가 돌아오기까지 또 12ms
        fresh += sampler.OnRemoteHeader(remote.Header(seq.Peek()), now);
    }
    CHECK(fresh > 0);                      // ← 표본이 하나도 없으면 여기서 죽는다
    CHECK(sampler.Ready());
    CHECK(sampler.Rtt() > 0.0f);
    CHECK(sampler.Timeout() >= sampler.Rtt());
    std::printf("  실채널 RTT 표본 %d개 rtt=%.1fms timeout=%.1fms\n",
                fresh, sampler.Rtt(), sampler.Timeout());
}

static void RunTests() {
    TestIdeal();
    TestLossy();
    TestReorder();
    TestHarsh();
    TestNoDuplicates();
    TestSequenceWrap();
    TestRtt();
    TestRttSamplerOverChannel();
}

TEST_MAIN("net_reliable")
