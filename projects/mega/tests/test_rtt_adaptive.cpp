// 적응 타이밍: 두 상수를 측정값이 대체한다.
//
// 보간 지연 3.0틱과 재전송 주기 100ms 는 링크를 보고 고른 값이 아니었다. 이
// 파일은 (a) ack 시각에서 뽑은 왕복 표본이 실제 링크로 수렴하는지, (b) 그 표본이
// 만든 재전송 주기가 손실 구간에서 대역폭을 지키는지, (c) 지터 항을 빼면 그
// 대역폭이 무너지는지를 잰다.
//
// 시계는 가상이다. 실시간을 쓰면 CI 부하가 그대로 표본에 섞여 같은 코드가
// 실행마다 다른 수를 낸다.

#include "net/rtt.hpp"
#include "net/packet.hpp"
#include "test_util.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

using namespace mega;

// ── 결정적 지연선 ──────────────────────────────────────────────────

struct DelayLine {
    struct Item { double arriveMs; net::PacketHeader header; };

    std::vector<Item> pending;
    float loss = 0.0f;
    uint32_t rng = 0x9E3779B9u;

    double Rand01() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return static_cast<double>(rng >> 8) * (1.0 / 16777216.0);
    }
    void Send(double nowMs, double delayMs, const net::PacketHeader& header) {
        if (loss > 0.0f && Rand01() < static_cast<double>(loss)) return;
        pending.push_back({nowMs + delayMs, header});
    }
    // 도착한 것을 도착 시각 오름차순으로 꺼낸다.
    std::vector<net::PacketHeader> Drain(double nowMs) {
        std::vector<Item> due, remain;
        for (Item& item : pending) {
            if (item.arriveMs <= nowMs) due.push_back(item);
            else remain.push_back(item);
        }
        pending.swap(remain);
        std::sort(due.begin(), due.end(),
                  [](const Item& a, const Item& b) { return a.arriveMs < b.arriveMs; });
        std::vector<net::PacketHeader> out;
        out.reserve(due.size());
        for (const Item& item : due) out.push_back(item.header);
        return out;
    }
};

// 한 방향 지연이 base ± jitter 인 링크 위에서 N번 왕복하고 추정치를 돌려준다.
struct LinkResult {
    float rtt = 0;
    float jitter = 0;
    float timeout = 0;
    uint64_t samples = 0;
};

static LinkResult MeasureLink(double oneWayBaseMs, double oneWayJitterMs, int exchanges) {
    net::RttSampler sampler(64, 0.1f);
    net::PacketSequencer localSeq;
    net::AckWindow localWindow, remoteWindow;
    DelayLine toRemote, toLocal;
    toRemote.rng = 0x1B873593u;
    toLocal.rng = 0xCC9E2D51u;

    // 스텝은 20ms 다. 지연선은 스텝 경계에서만 배달하므로, 재는 지연이 스텝의
    // 배수가 아니면 그 반올림이 그대로 표본에 섞인다 — 링크가 아니라 하네스를
    // 재게 된다. 20/80/200 은 전부 20 의 배수라 반올림이 0 이다.
    double now = 0.0;
    const double stepMs = 20.0;
    for (int i = 0; i < exchanges; ++i) {
        const uint16_t seq = localSeq.Next();
        sampler.OnSent(seq, now);
        const double outbound = oneWayBaseMs + toRemote.Rand01() * oneWayJitterMs;
        toRemote.Send(now, outbound, localWindow.Header(seq));

        now += stepMs;

        for (const net::PacketHeader& header : toRemote.Drain(now)) {
            remoteWindow.OnReceived(header.seq);
            const double inbound = oneWayBaseMs + toLocal.Rand01() * oneWayJitterMs;
            toLocal.Send(now, inbound, remoteWindow.Header(0));
        }
        for (const net::PacketHeader& header : toLocal.Drain(now))
            sampler.OnRemoteHeader(header, now);
    }
    LinkResult result;
    result.rtt = sampler.Rtt();
    result.jitter = sampler.Jitter();
    result.timeout = sampler.Timeout();
    result.samples = sampler.Estimator().Samples();
    return result;
}

static void TestConvergesToLinkLatency() {
    const double latencies[] = {20.0, 80.0, 200.0};
    for (double oneWay : latencies) {
        const LinkResult result = MeasureLink(oneWay, 0.0, 400);
        const double expected = oneWay * 2.0;
        std::printf("  편도 %.0fms → 추정 RTT %.1fms (기대 %.0f) 지터 %.2f 표본 %llu\n",
                    oneWay, result.rtt, expected,
                    result.jitter, static_cast<unsigned long long>(result.samples));
        CHECK(result.samples > 300);
        CHECK(result.rtt > expected - 1.0f);
        CHECK(result.rtt < expected + 1.0f);
        CHECK(result.jitter < 0.5f);        // 흔들림이 없으면 지터도 없다
    }
}

static void TestJitterShowsUpInTimeout() {
    const LinkResult steady = MeasureLink(80.0, 0.0, 400);
    const LinkResult jittery = MeasureLink(80.0, 120.0, 400);
    std::printf("  지터 없음: rtt %.1f 지터 %.2f 타임아웃 %.1f\n",
                steady.rtt, steady.jitter, steady.timeout);
    std::printf("  지터 있음: rtt %.1f 지터 %.2f 타임아웃 %.1f\n",
                jittery.rtt, jittery.jitter, jittery.timeout);
    CHECK(jittery.jitter > 10.0f);
    CHECK(jittery.timeout > steady.timeout + 40.0f);
}

static void TestSpikeDoesNotStampede() {
    net::RttEstimator estimator(0.1f);
    for (int i = 0; i < 200; ++i) estimator.Sample(160.0f);
    const float before = estimator.Rtt();
    estimator.Sample(800.0f);                       // 한 번의 스파이크
    const float after = estimator.Rtt();
    std::printf("  스파이크 800ms 한 번: rtt %.1f → %.1f (타임아웃 %.1f)\n",
                before, after, estimator.Timeout());
    CHECK(before > 159.0f && before < 161.0f);
    CHECK(after < 230.0f);                          // EWMA 라 한 번에 끌려가지 않는다
    CHECK(after > before);                          // 그래도 반응은 한다
}

static void TestKarnRejectsResentSamples() {
    net::RttSampler sampler(16, 0.1f);
    sampler.OnSent(1, 0.0);
    sampler.OnSent(1, 300.0);                       // 같은 seq 재전송
    net::AckWindow window;
    window.OnReceived(1);
    CHECK_EQ(sampler.OnRemoteHeader(window.Header(0), 400.0), 0);
    CHECK(!sampler.Ready());                        // 표본이 하나도 없다

    sampler.OnSent(2, 500.0);
    net::AckWindow second;
    second.OnReceived(2);
    CHECK_EQ(sampler.OnRemoteHeader(second.Header(0), 620.0), 1);
    CHECK(sampler.Ready());
    CHECK(sampler.Rtt() > 119.0f && sampler.Rtt() < 121.0f);
}

// ── 상수가 측정값으로 바뀐 자리 ────────────────────────────────────

static void TestAdaptiveTiming() {
    net::AdaptiveTiming timing;
    net::RttEstimator cold(0.1f);
    CHECK(!cold.Ready());
    CHECK(timing.DelayTicks(cold) == timing.fallbackDelayTicks);   // 모르면 옛 상수
    CHECK(timing.ResendMs(cold) == timing.fallbackResendMs);

    // 지연은 크지만 흔들리지 않는 링크. 스냅샷은 재전송되지 않는 연속 스트림이라
    // 도착 *간격*이 흔들리지 않으면 보간 지연을 늘릴 이유가 없다 — RTT 를 그대로
    // 지연에 넣는 설계가 기각된 자리가 여기다.
    const LinkResult far = MeasureLink(200.0, 0.0, 400);
    net::RttEstimator farEstimator(0.1f);
    for (int i = 0; i < 200; ++i) farEstimator.Sample(far.rtt);
    const double farDelay = timing.DelayTicks(farEstimator);

    // 같은 평균인데 흔들리는 링크.
    net::RttEstimator jitteryEstimator(0.1f);
    for (int i = 0; i < 200; ++i) jitteryEstimator.Sample(i % 2 ? 320.0f : 480.0f);
    const double jitteryDelay = timing.DelayTicks(jitteryEstimator);

    std::printf("  보간 지연: 400ms 무지터 %.2f틱 · 400ms 지터 %.2f틱 (rtt %.0f/%.0f)\n",
                farDelay, jitteryDelay, farEstimator.Rtt(), jitteryEstimator.Rtt());
    CHECK(farDelay == timing.fallbackDelayTicks);   // 지연만으로는 늘지 않는다
    CHECK(jitteryDelay > farDelay);                 // 흔들리면 늘어난다
    CHECK(jitteryDelay <= timing.maxDelayTicks);
    CHECK(timing.LeadTicks(jitteryEstimator) < jitteryDelay);
    CHECK(timing.LeadTicks(cold) >= 1.0);

    // 재전송 주기는 반대다 — 왕복 시간 자체가 하한이다.
    CHECK(timing.ResendMs(farEstimator) > 390.0);
    CHECK(timing.ResendMs(jitteryEstimator) > timing.ResendMs(farEstimator));
    net::RttEstimator instant(0.1f);
    for (int i = 0; i < 50; ++i) instant.Sample(1.0f);
    CHECK(timing.ResendMs(instant) == timing.minResendMs);   // 하한에서 잘린다
}

// ── 재전송 폭주 ────────────────────────────────────────────────────
//
// 미확인 메시지를 Timeout() 마다 다시 보내는 최소 재전송기. 링크는 편도 50ms +
// 지터 최대 150ms 이고 ack 의 30% 를 잃는다. 지터 항이 살아 있으면 타임아웃이
// 흔들림을 덮으므로 재전송은 진짜 손실에만 일어난다.

struct ResendReport {
    int sends = 0;
    int delivered = 0;
    double timeoutMs = 0;
    float rtt = 0;
    float jitter = 0;
};

static ResendReport RunResender(int messages, double simMs) {
    net::RttSampler sampler(256, 0.1f);
    net::AdaptiveTiming timing;
    net::PacketSequencer seq;
    net::AckWindow localWindow, remoteWindow;

    // 지연선이 나르는 것: 패킷 헤더 + 그 패킷이 실은 메시지 번호(-1 = ack 전용).
    struct Wire { net::PacketHeader header; int message; };
    struct Pipe {
        struct Item { double arriveMs; Wire wire; };
        std::vector<Item> pending;
        float loss = 0.0f;
        uint32_t rng = 1;
        double Rand01() {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return static_cast<double>(rng >> 8) * (1.0 / 16777216.0);
        }
        void Send(double nowMs, double delayMs, const Wire& wire) {
            if (loss > 0.0f && Rand01() < static_cast<double>(loss)) return;
            pending.push_back({nowMs + delayMs, wire});
        }
        std::vector<Wire> Drain(double nowMs) {
            std::vector<Item> due, remain;
            for (Item& item : pending) {
                if (item.arriveMs <= nowMs) due.push_back(item);
                else remain.push_back(item);
            }
            pending.swap(remain);
            std::sort(due.begin(), due.end(),
                      [](const Item& a, const Item& b) { return a.arriveMs < b.arriveMs; });
            std::vector<Wire> out;
            for (const Item& item : due) out.push_back(item.wire);
            return out;
        }
    };
    Pipe toRemote, toLocal;
    toRemote.rng = 0x85EBCA6Bu;
    toLocal.rng = 0xC2B2AE35u;
    toLocal.loss = 0.30f;                   // ack 의 30% 가 사라진다

    struct Pending { double lastSentMs = -1e9; bool acked = false; };
    std::vector<Pending> outbox(messages);
    std::vector<bool> arrived(messages, false);
    std::map<uint16_t, int> carried;         // seq → 그 패킷이 실은 메시지

    ResendReport report;
    double now = 0.0;
    const double stepMs = 8.0;
    while (now < simMs) {
        const double resend = timing.ResendMs(sampler.Estimator());
        for (int i = 0; i < messages; ++i) {
            if (outbox[i].acked) continue;
            if (now - outbox[i].lastSentMs < resend) continue;
            const uint16_t s = seq.Next();
            sampler.OnSent(s, now);
            carried[s] = i;
            toRemote.Send(now, 50.0 + toRemote.Rand01() * 150.0, Wire{localWindow.Header(s), i});
            outbox[i].lastSentMs = now;
            ++report.sends;
        }
        now += stepMs;

        for (const Wire& wire : toRemote.Drain(now)) {
            remoteWindow.OnReceived(wire.header.seq);
            if (wire.message >= 0 && !arrived[wire.message]) {
                arrived[wire.message] = true;
                ++report.delivered;
            }
            toLocal.Send(now, 50.0 + toLocal.Rand01() * 150.0, Wire{remoteWindow.Header(0), -1});
        }
        for (const Wire& wire : toLocal.Drain(now)) {
            sampler.OnRemoteHeader(wire.header, now);
            for (const auto& [sentSeq, message] : carried)
                if (net::HeaderAcknowledges(wire.header, sentSeq)) outbox[message].acked = true;
        }
    }
    report.timeoutMs = timing.ResendMs(sampler.Estimator());
    report.rtt = sampler.Rtt();
    report.jitter = sampler.Jitter();
    return report;
}

static void TestResendStaysBounded() {
    const ResendReport report = RunResender(60, 12000.0);
    std::printf("  재전송: 메시지 60개 · 12초 · 전송 %d회 · 전달 %d개 · "
                "rtt %.0f 지터 %.0f 최종 주기 %.0fms\n",
                report.sends, report.delivered, report.rtt, report.jitter,
                report.timeoutMs);
    CHECK_EQ(report.delivered, 60);
    // 지터 항이 살아 있으면 주기가 흔들림을 덮으므로 재전송이 진짜 손실에만
    // 일어난다. 지터 갱신을 지우면 주기가 rtt 로 내려앉아(646 → 266ms) 아직
    // 날아가고 있는 패킷을 다시 보내기 시작하고, 전송 수가 147 → 170 으로 는다.
    // 두 단언이 함께 있는 이유: 주기만 보면 "왜 그게 나쁜가"가 안 보이고,
    // 전송 수만 보면 "왜 늘었는가"가 안 보인다.
    CHECK(report.timeoutMs > 400.0);
    CHECK(report.sends < 160);
}

static void RunTests() {
    TestConvergesToLinkLatency();
    TestJitterShowsUpInTimeout();
    TestSpikeDoesNotStampede();
    TestKarnRejectsResentSamples();
    TestAdaptiveTiming();
    TestResendStaysBounded();
}

TEST_MAIN("rtt_adaptive")
