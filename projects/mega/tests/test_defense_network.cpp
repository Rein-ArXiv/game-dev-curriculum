#include "game/defense_protocol.hpp"
#include "net/reliable_channel.hpp"
#include "net/session.hpp"
#include "net/transport.hpp"
#include "net/udp_transport.hpp"
#include "test_util.h"

#include <chrono>
#include <cmath>
#include <thread>

using namespace mega;
using namespace mega::game;
namespace wire = mega::game::wire;

static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

static bool ReceiveWithin(net::UdpTransport& transport, net::Bytes& out, int milliseconds) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (transport.Receive(out)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

static void TestAuthoritativeCommandAndSnapshotOverUdp() {
    DefenseMap map = DefenseMap::CreateDefault();
    GameWorld hostWorld;
    hostWorld.Init(map, {WaveDef{1, 10, 20, 5, 0.1f}}, 200, 100);

    net::UdpTransport host;
    net::UdpTransport client;
    CHECK(host.OpenHost(0));
    CHECK(client.OpenClient("127.0.0.1", host.LocalPort()));

    client.Send(net::Bytes{0xde, 0xad, 0xbe, 0xef});
    net::Bytes packet;
    CHECK(ReceiveWithin(host, packet, 500));
    CHECK(!wire::ReadHello(packet));
    CHECK(!host.PeerKnown());

    client.Send(wire::WriteHello());
    CHECK(ReceiveWithin(host, packet, 500));
    CHECK(wire::ReadHello(packet));
    CHECK(host.AcceptLastSender());
    CHECK(host.PeerKnown());

    math::Vec2 buildCell = map.CellCenter(2, 2);
    wire::Command command;
    command.sequence = 1;
    command.type = wire::CommandType::Place;
    command.x = wire::EncodePosition(buildCell.x);
    command.y = wire::EncodePosition(buildCell.y);
    command.argument = static_cast<uint8_t>(TowerType::Cannon);
    client.Send(wire::WriteCommand(command));

    CHECK(ReceiveWithin(host, packet, 500));
    wire::Command received;
    CHECK(wire::ReadCommand(packet, received));
    CHECK_EQ(received.sequence, 1u);
    CHECK(wire::ApplyCommand(hostWorld, received));
    CHECK_EQ(hostWorld.TowerCount(), 1);

    hostWorld.Step(1.0f / 60.0f);
    host.Send(wire::WriteSnapshot(hostWorld, 3, received.sequence));
    CHECK(ReceiveWithin(client, packet, 500));
    wire::Snapshot replica;
    CHECK(wire::ReadSnapshot(packet, replica));
    CHECK_EQ(replica.acknowledgedCommand, 1u);
    CHECK_EQ(replica.towers.size(), 1u);
    CHECK_EQ(replica.towers[0].type, TowerType::Cannon);
}

static void TestDefenseProtocolConvergesUnderFaults() {
    net::NetConditions conditions;
    conditions.loss = 0.35f;
    conditions.latencyTicks = 3;
    conditions.reorderProb = 0.65f;
    conditions.seed = 0xdefe1234u;
    net::LoopbackNet network(conditions);
    net::LoopbackEndpoint client(network, true);
    net::LoopbackEndpoint host(network, false);

    DefenseMap map = DefenseMap::CreateDefault();
    GameWorld hostWorld;
    hostWorld.Init(map, {WaveDef{1, 10, 20, 5, 0.1f}}, 500, 100);

    const DefenseMap::Cell cells[] = {{2, 2}, {4, 2}, {6, 1}};
    std::vector<wire::Command> pending;
    for (uint32_t sequence = 1; sequence <= 3; ++sequence) {
        wire::Command command;
        command.sequence = sequence;
        command.type = wire::CommandType::Place;
        const auto cell = cells[sequence - 1];
        const auto position = map.CellCenter(cell.x, cell.y);
        command.x = wire::EncodePosition(position.x);
        command.y = wire::EncodePosition(position.y);
        command.argument = static_cast<uint8_t>(
            (sequence - 1) % static_cast<uint32_t>(TowerType::Count));
        pending.push_back(command);
    }

    uint32_t hostAck = 0;
    uint32_t clientAck = 0;
    for (uint32_t tick = 0; tick < 4000 && clientAck < 3; ++tick) {
        if (tick % 7 == 0) {
            for (const auto& command : pending) {
                if (command.sequence > clientAck)
                    client.Send(wire::WriteCommand(command));
            }
        }
        if (tick % 3 == 0)
            host.Send(wire::WriteSnapshot(hostWorld, tick, hostAck));

        network.Pump();
        net::Bytes packet;
        while (host.Receive(packet)) {
            wire::Command command;
            if (wire::ReadCommand(packet, command) &&
                command.sequence == hostAck + 1) {
                wire::ApplyCommand(hostWorld, command);
                hostAck = command.sequence;
            }
        }
        while (client.Receive(packet)) {
            wire::Snapshot snapshot;
            if (wire::ReadSnapshot(packet, snapshot))
                clientAck = std::max(clientAck,
                                     snapshot.acknowledgedCommand);
        }
    }

    CHECK_EQ(hostAck, 3u);
    CHECK_EQ(clientAck, 3u);
    CHECK_EQ(hostWorld.TowerCount(), 3);
}

// ── 제품이 실제로 도는 코드 ────────────────────────────────────────
// 아래 두 케이스는 mega_defense 클라이언트/호스트 루프가 쓰는 바로 그 타입을
// 같은 설정으로 구동한다(wire::SnapshotView · net::ReliableOrderedSender/Receiver).
// 제품이 자기 사본을 갖고 있던 동안에는 이런 테스트가 제품에 아무 효력이 없었다.

static wire::Snapshot ReplicaSnapshot(uint32_t tick, float x, float hp) {
    wire::Snapshot snapshot;
    snapshot.tick = tick;
    snapshot.coreHp = hp;
    snapshot.coreMaxHp = 100.0f;
    snapshot.enemies.push_back({7, {x, 0.0f}, hp, 100.0f, EnemyType::Normal});
    snapshot.projectiles.push_back({9, {x * 2.0f, 0.0f}});
    return snapshot;
}

static void TestClientReplicaInterpolation() {
    // 제품 클라이언트(game/defense_app.hpp)와 같은 시계 설정: 8개 보관, 3틱 뒤에서 시작,
    // 최신에 2틱보다 가까이 붙지 않음.
    wire::SnapshotView replicas(net::InterpolationClock{3.0, 2.0, 8});
    CHECK(!replicas.Ready());

    CHECK(replicas.Add(ReplicaSnapshot(10, 0.0f, 100.0f)));
    CHECK(replicas.Ready());
    CHECK(!replicas.Add(ReplicaSnapshot(9, 999.0f, 1.0f)));   // 지나간 틱은 거절
    CHECK(replicas.Add(ReplicaSnapshot(20, 100.0f, 60.0f)));

    // 시계는 버퍼에 있는 가장 오래된 틱 아래로는 못 내려간다.
    replicas.Advance(0.0);
    CHECK(replicas.RenderTick() == 10.0);

    replicas.Advance(5.0);                                    // 두 스냅샷의 중간
    CHECK(replicas.RenderTick() == 15.0);
    const wire::Snapshot mid = replicas.Render(wire::Lerp);
    CHECK_EQ(mid.enemies.size(), 1u);
    CHECK(Near(mid.enemies[0].pos.x, 50.0f));
    CHECK(Near(mid.enemies[0].hp, 80.0f));
    CHECK(Near(mid.coreHp, 80.0f));
    CHECK_EQ(mid.projectiles.size(), 1u);
    CHECK(Near(mid.projectiles[0].pos.x, 100.0f));

    // 시계는 최신 스냅샷에 2틱보다 가까이 다가가지 않는다.
    replicas.Advance(100.0);
    CHECK(replicas.RenderTick() == 18.0);
}

static void TestHostCommandChannelSuppressesReplayAndReorders() {
    // 호스트가 클라이언트 명령을 받는 경로 그대로
    // (game/defense_app.hpp 의 PumpHost · game/defense_server.hpp 의 HandleDatagram).
    net::ReliableOrderedReceiver<wire::Command> remoteCommands(1);
    wire::Command command;
    command.type = wire::CommandType::Place;

    CHECK(remoteCommands.Accept(1, command));        // 첫 명령
    CHECK(!remoteCommands.Accept(1, command));       // 같은 명령의 재전송 → 무시
    CHECK_EQ(remoteCommands.LastDelivered(), 1u);
    CHECK_EQ(remoteCommands.PendingCount(), static_cast<size_t>(1));

    CHECK(remoteCommands.Accept(3, command));        // 앞당겨 도착 → 버퍼링
    CHECK_EQ(remoteCommands.LastDelivered(), 1u);    // 아직 전달되면 안 된다
    CHECK_EQ(remoteCommands.BufferedCount(), static_cast<size_t>(1));

    CHECK(remoteCommands.Accept(2, command));        // 구멍이 메워지며 2,3 연속 전달
    CHECK_EQ(remoteCommands.LastDelivered(), 3u);
    CHECK_EQ(remoteCommands.BufferedCount(), static_cast<size_t>(0));

    int drained = 0;
    wire::Command out;
    while (remoteCommands.Poll(out)) ++drained;
    CHECK_EQ(drained, 3);                            // 3개를 정확히 한 번씩
}

static void RunReliableCommandConvergence(uint32_t seed) {
    net::NetConditions conditions;
    conditions.loss = 0.35f;
    conditions.latencyTicks = 3;
    conditions.reorderProb = 0.65f;
    conditions.seed = seed;
    net::LoopbackNet network(conditions);
    net::LoopbackEndpoint client(network, true);
    net::LoopbackEndpoint host(network, false);

    DefenseMap map = DefenseMap::CreateDefault();
    GameWorld hostWorld;
    hostWorld.Init(map, {WaveDef{1, 10, 20, 5, 0.1f}}, 500, 100);

    net::ReliableOrderedSender<wire::Command> localCommands(1);
    net::ReliableOrderedReceiver<wire::Command> remoteCommands(1);

    const DefenseMap::Cell cells[] = {{2, 2}, {4, 2}, {6, 1}};
    for (uint32_t i = 0; i < 3; ++i) {
        wire::Command command;
        command.type = wire::CommandType::Place;
        const auto position = map.CellCenter(cells[i].x, cells[i].y);
        command.x = wire::EncodePosition(position.x);
        command.y = wire::EncodePosition(position.y);
        command.argument = static_cast<uint8_t>(
            i % static_cast<uint32_t>(TowerType::Count));
        localCommands.Queue(command);           // 시퀀스는 채널이 부여한다
    }

    // 루프를 끝까지 돌린다. ack 를 실은 스냅샷도 35% 손실되므로 클라이언트는
    // 호스트가 이미 반영한 명령을 반드시 다시 보낸다 — 중복 억제가 없으면
    // 타워가 두 번 지어지고 골드가 두 번 나간다.
    int applied = 0;
    for (uint32_t tick = 0; tick < 4000; ++tick) {
        if (tick % 7 == 0)
            localCommands.ForEachUnacked(
                [&](uint32_t sequence, const wire::Command& queued) {
                    wire::Command outgoing = queued;
                    outgoing.sequence = sequence;
                    client.Send(wire::WriteCommand(outgoing));
                });
        if (tick % 3 == 0)
            host.Send(wire::WriteSnapshot(hostWorld, tick,
                                          remoteCommands.LastDelivered()));

        network.Pump();
        net::Bytes packet;
        while (host.Receive(packet)) {
            wire::Command command;
            if (wire::ReadCommand(packet, command))
                remoteCommands.Accept(command.sequence, command);
        }
        wire::Command delivered;
        while (remoteCommands.Poll(delivered)) {
            wire::ApplyCommand(hostWorld, delivered);
            ++applied;
        }
        while (client.Receive(packet)) {
            wire::Snapshot snapshot;
            if (wire::ReadSnapshot(packet, snapshot))
                localCommands.AckThrough(snapshot.acknowledgedCommand);
        }
    }

    CHECK_EQ(applied, 3);                       // 재전송·재정렬에도 정확히 한 번씩
    CHECK_EQ(remoteCommands.LastDelivered(), 3u);
    CHECK_EQ(localCommands.UnackedCount(), static_cast<size_t>(0));
    CHECK_EQ(hostWorld.TowerCount(), 3);
}

static void TestReliableCommandCoreConvergesUnderFaults() {
    // 손실 35% 에서 첫 시도에 명령 3개가 모두 살아남을 확률은 약 27% 다. 시드
    // 하나로는 "재전송이 없어도 통과하는" 운 좋은 링크를 잡을 수 있어서 여러
    // 시드로 돌린다 — 재전송을 없애면 이 중 최소 하나는 반드시 깨진다.
    for (uint32_t seed : {0x5eed1234u, 0xa5a5a5a5u, 0x13579bdfu, 0x2468ace0u})
        RunReliableCommandConvergence(seed);
}

static void RunTests() {
    TestAuthoritativeCommandAndSnapshotOverUdp();
    TestDefenseProtocolConvergesUnderFaults();
    TestClientReplicaInterpolation();
    TestHostCommandChannelSuppressesReplayAndReorders();
    TestReliableCommandCoreConvergesUnderFaults();
}

TEST_MAIN("defense_network")
