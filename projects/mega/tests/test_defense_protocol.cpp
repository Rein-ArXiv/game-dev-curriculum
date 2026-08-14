#include "game/defense_protocol.hpp"
#include "test_util.h"

#include <limits>

using namespace mega;
using namespace mega::game;
using namespace mega::game::wire;

static void TestCommandRoundTripAndAuthority() {
    DefenseMap map = DefenseMap::CreateDefault();
    GameWorld world;
    world.Init(map, {WaveDef{1, 10, 20, 5, 0.1f}}, 200, 100);

    math::Vec2 buildCell = map.CellCenter(2, 2);
    Command sent;
    sent.sequence = 7;
    sent.type = CommandType::Place;
    sent.x = EncodePosition(buildCell.x);
    sent.y = EncodePosition(buildCell.y);
    sent.argument = static_cast<uint8_t>(TowerType::Cannon);

    Command received;
    CHECK(ReadCommand(WriteCommand(sent), received));
    CHECK_EQ(received.sequence, 7u);
    CHECK(ApplyCommand(world, received));
    CHECK_EQ(world.TowerCount(), 1);

    // The path is authoritative: a forged placement on it is rejected.
    math::Vec2 pathCell = map.CellCenter(7, 2);
    received.sequence = 8;
    received.x = EncodePosition(pathCell.x);
    received.y = EncodePosition(pathCell.y);
    CHECK(!ApplyCommand(world, received));
    CHECK_EQ(world.TowerCount(), 1);
}

static void TestSnapshotRoundTripAndBounds() {
    DefenseMap map = DefenseMap::CreateDefault();
    GameWorld world;
    world.Init(map, {WaveDef{3, 10, 20, 5, 0.1f}}, 200, 100);
    CHECK(world.PlaceTowerType(map.CellCenter(2, 2), TowerType::Gatling));
    for (int i = 0; i < 10; ++i) world.Step(1.0f / 60.0f);

    net::Bytes bytes = WriteSnapshot(world, 42, 9);
    CHECK(!bytes.empty());
    CHECK(bytes.size() <= kMaxPacketBytes);

    Snapshot decoded;
    CHECK(ReadSnapshot(bytes, decoded));
    CHECK_EQ(decoded.tick, 42u);
    CHECK_EQ(decoded.acknowledgedCommand, 9u);
    CHECK_EQ(decoded.gold, world.Gold());
    CHECK_EQ(decoded.towers.size(), world.Towers().size());
    CHECK_EQ(decoded.enemies.size(), world.Enemies().size());

    bytes.pop_back();
    CHECK(!ReadSnapshot(bytes, decoded));
}

static void TestHelloValidation() {
    net::Bytes hello = WriteHello();
    CHECK(ReadHello(hello));
    hello[0] ^= 0xff;
    CHECK(!ReadHello(hello));

    const std::string token = "header.signature";
    net::Bytes admission = WriteAdmissionHello(token);
    std::string decoded;
    CHECK(ReadAdmissionHello(admission, decoded));
    CHECK_EQ(decoded, token);
    admission.pop_back();
    CHECK(!ReadAdmissionHello(admission, decoded));
    CHECK(WriteAdmissionHello("").empty());
}

static void TestSequenceWrapComparison() {
    constexpr uint32_t max = std::numeric_limits<uint32_t>::max();
    CHECK(net::SequenceMoreRecent(0, max));
    CHECK(net::SequenceMoreRecent(1, max));
    CHECK(!net::SequenceMoreRecent(max, 0));
    CHECK(net::SequenceAtOrBefore(max, 0));
    CHECK(net::SequenceAtOrBefore(0, 0));
    CHECK(!net::SequenceAtOrBefore(1, 0));
}

static void RunTests() {
    TestCommandRoundTripAndAuthority();
    TestSnapshotRoundTripAndBounds();
    TestHelloValidation();
    TestSequenceWrapComparison();
}

TEST_MAIN("defense_protocol")
