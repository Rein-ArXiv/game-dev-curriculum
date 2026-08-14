// host-authoritative 세션 검증: 스냅샷 보간이 호스트 궤적을 추종한다(손실 하에서도).
#include "net/session.hpp"
#include "net/snapshot.hpp"
#include "net/transport.hpp"
#include "test_util.h"

#include <cmath>
#include <vector>

using namespace mega::net;
using mega::math::Vec2;

// 호스트 엔티티 1개가 +x 로 등속 이동. 클라이언트가 보간해 그 과거 궤적을 재현하는가.
static float RunTracking(NetConditions cond, int snapshotInterval, float interpDelay) {
    LoopbackNet net(cond);
    LoopbackEndpoint hostEp(net, true), clientEp(net, false);

    HostSession host;
    host.Add(1, Vec2{0, 0}, 100);
    const float vx = 2.0f;                    // 틱당 +2

    ClientView client(interpDelay);
    SnapshotBuffer truth;                     // 호스트 지상진실(비교용)

    float sumErr = 0; int samples = 0;
    const int T = 600;
    for (int t = 0; t < T; ++t) {
        // 호스트 시뮬: 엔티티 전진.
        host.Entities()[0].pos.x += vx;
        host.Advance();
        truth.Add(host.Make());

        // 스냅샷 송신(비신뢰).
        if (host.Tick() % snapshotInterval == 0) {
            Snapshot s = host.Make();
            hostEp.Send(WriteSnapshot(s));
        }
        net.Pump();

        // 클라이언트 수신.
        Bytes raw;
        while (clientEp.Receive(raw)) client.OnSnapshotBytes(raw);

        // 워밍업 후 추종 오차 측정: 클라이언트 렌더 vs 호스트 진실(같은 렌더 틱).
        if (t > 100 && client.Ready()) {
            float rt = client.RenderTick();
            auto rendered = client.Render();
            auto truthAt = truth.Sample(rt);
            if (!rendered.empty() && !truthAt.empty()) {
                float err = std::fabs(rendered[0].pos.x - truthAt[0].pos.x);
                sumErr += err; ++samples;
            }
        }
    }
    return samples ? sumErr / samples : 1e9f;
}

static void TestPerfectLink() {
    // 손실 0: 보간이 호스트 과거 궤적을 거의 정확히 재현.
    NetConditions c; c.latencyTicks = 1;
    float err = RunTracking(c, 3, 4.0f);
    CHECK(err < 0.5f);                         // 평균 오차 < 0.5 단위
}

static void TestWithLoss() {
    // 20% 스냅샷 손실: 다음 스냅샷이 따라잡아 오차가 유계로 유지.
    NetConditions c; c.latencyTicks = 1; c.loss = 0.2f; c.seed = 17;
    float err = RunTracking(c, 3, 5.0f);
    CHECK(err < 6.0f);                         // 손실에도 추종(유계 오차)
}

static void TestClientSeesEntities() {
    LoopbackNet net;
    LoopbackEndpoint hostEp(net, true), clientEp(net, false);
    HostSession host;
    host.Add(1, Vec2{0, 0}, 100);
    host.Add(2, Vec2{10, 10}, 50);
    host.Add(3, Vec2{-5, 5}, 75);
    ClientView client(2.0f);

    for (int t = 0; t < 30; ++t) {
        host.Advance();
        Snapshot s = host.Make();
        hostEp.Send(WriteSnapshot(s));
        net.Pump();
        Bytes raw;
        while (clientEp.Receive(raw)) client.OnSnapshotBytes(raw);
    }
    CHECK(client.Ready());
    auto ents = client.Render();
    CHECK_EQ(ents.size(), (size_t)3);          // 호스트의 3개 엔티티를 본다
}

static void RunTests() {
    TestPerfectLink();
    TestWithLoss();
    TestClientSeesEntities();
}

TEST_MAIN("net_session")
