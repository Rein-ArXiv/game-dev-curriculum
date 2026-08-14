// 제품 클라이언트 프레임 루프를 실제 UDP 위에서 돌린다.
//
// 왜 이 파일이 필요한가. 이 저장소의 넷 테스트는 전부 알고리즘을 검증한다 —
// ReliableOrderedSender 의 재전송, SnapshotView 의 순서 보장, wire 의 왕복.
// 그 알고리즘들은 이미 잘 덮여 있다. 덮이지 않은 것은 **배선**이었다:
// DefenseApp::PumpClient() 안에서 그 알고리즘을 소켓에 연결하는 몇 줄.
//
// 그 몇 줄을 통째로 죽여도(클라이언트가 명령을 한 번도 내보내지 않아도) 기존
// 테스트 53개가 전부 통과했다. 협동 클라이언트는 타워를 하나도 못 세우는데
// 초록이었다. 원인은 단순하다 — 어떤 테스트도 DefenseApp 을 구성한 적이 없고,
// mega_defense --selftest 는 Solo 로 돌아 PumpClient 를 한 번도 부르지 않는다.
//
// 여기서는 창도 스크립트도 아닌 테스트가 FrameInput 을 만들고, 그 뒤로는 제품이
// 도는 것과 완전히 같은 DefenseApp::Step() 이 돈다. 반대편 끝은 흉내가 아니라
// 진짜 UDP 소켓(net::UdpTransport)이고, 이 테스트가 호스트 역할을 맡는다.
//
// 결정성: 고정 sleep 이 없다. 모든 대기는 "조건이 참이 될 때까지 프레임을 돌린다
// + 데드라인" 형태이고, 통신은 루프백 한 쌍이라 순서와 도달이 보장된다.

#include "game/defense_app.hpp"
#include "game/defense_content.hpp"
#include "game/defense_protocol.hpp"
#include "net/udp_transport.hpp"
#include "test_util.h"

#include <chrono>
#include <cstdio>

using namespace mega;
using namespace mega::game;
namespace wire = mega::game::wire;

namespace {
using Clock = std::chrono::steady_clock;
}  // namespace

int main() {
    std::printf("[test] defense_client_loop\n");

    // ── 반대편 끝: 진짜 소켓을 쥔 호스트 ──────────────────────────────
    net::UdpTransport host;
    CHECK(host.OpenHost(0));
    const uint16_t port = host.LocalPort();
    CHECK(port != 0);

    // ── 검사 대상: 제품 클라이언트 그 자체 ────────────────────────────
    DefenseOptions options;
    options.mode = RunMode::Client;
    options.address = "127.0.0.1";
    options.port = port;

    DefenseApp app;
    CHECK(app.Init(options));
    CHECK_EQ(app.CommandPacketsSent(), 0ull);

    // 창이 만들었을 것과 같은 FrameInput — 맵 위 한 셀을 누른 프레임.
    const math::Vec2 target = app.Map().CellCenter(6, 1);
    FrameInput click;
    click.mouseX = static_cast<int>(target.x);
    click.mouseY = static_cast<int>(target.y);
    click.mouseClicked = true;

    // ── 1단계: 클라이언트 → 호스트. 명령이 실제로 소켓 밖으로 나가는가 ──
    bool helloSeen = false;
    bool commandSeen = false;
    wire::Command received;
    int framesToCommand = 0;
    const auto sendDeadline = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < sendDeadline && !commandSeen) {
        app.Step(click, kDefenseFixedDt);
        ++framesToCommand;
        net::Bytes packet;
        while (host.Receive(packet)) {
            if (wire::ReadHello(packet)) {
                host.AcceptLastSender();
                helloSeen = true;
                continue;
            }
            wire::Command command;
            if (wire::ReadCommand(packet, command)) {
                received = command;
                commandSeen = true;
            }
        }
    }

    CHECK(helloSeen);                       // 핸드셰이크가 나갔다
    CHECK(commandSeen);                     // ← 명령 재전송 배선이 죽으면 여기서 잡힌다
    CHECK(app.CommandPacketsSent() > 0);    // ← 같은 결함을 앱 안쪽에서 한 번 더
    CHECK(app.CommandsQueued() > 0);        // 입력이 명령을 만들기는 했다
    CHECK_EQ(received.sequence, 1u);        // 첫 명령이 통째로 유실되지 않았다
    CHECK(received.type == wire::CommandType::Place);
    CHECK_EQ(received.argument, static_cast<uint8_t>(TowerType::Cannon));
    CHECK_EQ(received.x, wire::EncodePosition(target.x));
    CHECK_EQ(received.y, wire::EncodePosition(target.y));
    if (!commandSeen) {
        std::printf("  (frames=%d queued=%llu sent=%llu)\n", framesToCommand,
                    static_cast<unsigned long long>(app.CommandsQueued()),
                    static_cast<unsigned long long>(app.CommandPacketsSent()));
    }

    const size_t unackedBefore = app.UnackedCommands();
    CHECK(unackedBefore > 0);

    // ── 2단계: 호스트 → 클라이언트. ack 와 복제가 돌아오는가 ──────────
    // 호스트 권위 월드에 받은 명령을 반영하고, 그 상태를 스냅샷으로 돌려준다.
    GameWorld world;
    world.Init(app.Map(), defense::DefaultWaves(), app.Balance().startGold,
               app.Balance().coreHp);
    CHECK(wire::ApplyCommand(world, received));
    CHECK_EQ(world.TowerCount(), 1);

    uint32_t hostTick = 0;
    const auto ackDeadline = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < ackDeadline &&
           (app.AcknowledgedCommand() == 0 || app.View().towers.empty())) {
        // 틱만 전진시킨다. 월드를 굴리지 않으므로 스냅샷 내용이 고정되고,
        // 검사 대상은 보간 시계가 아니라 배선 하나로 남는다.
        ++hostTick;
        host.Send(wire::WriteSnapshot(world, hostTick, received.sequence));
        app.Step(FrameInput{}, kDefenseFixedDt);
    }

    CHECK(app.SnapshotsReceived() > 0);
    CHECK_EQ(app.AcknowledgedCommand(), 1u);
    CHECK(app.UnackedCommands() < unackedBefore);   // ack 가 미확인 목록을 줄였다
    CHECK(app.Connected());
    CHECK(!app.View().towers.empty());              // 복제된 상태가 눈에 보인다
    CHECK_EQ(app.View().towers.size(), size_t{1});

    // 셀프테스트가 이 상태를 성공으로 읽는가 — 제품의 판정 기준 자체를 검사한다.
    CHECK(ClientRoundTripProven(app));

    std::printf("[test] defense_client_loop: frames=%d hello=%llu sent=%llu "
                "queued=%llu snapshots=%llu ack=%u\n",
                framesToCommand,
                static_cast<unsigned long long>(app.HelloPacketsSent()),
                static_cast<unsigned long long>(app.CommandPacketsSent()),
                static_cast<unsigned long long>(app.CommandsQueued()),
                static_cast<unsigned long long>(app.SnapshotsReceived()),
                app.AcknowledgedCommand());
    std::printf("[test] defense_client_loop: %d checks, %d failures\n",
                megatest::g_checks, megatest::g_failures);
    return megatest::g_failures == 0 ? 0 : 1;
}
