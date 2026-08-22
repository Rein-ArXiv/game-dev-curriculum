#include "game/defense_protocol.hpp"
#include "net/reliable_channel.hpp"
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
    CHECK_EQ(decoded.enemies.size(), world.Enemies().Size());

    bytes.pop_back();
    CHECK(!ReadSnapshot(bytes, decoded));
}

// 길이가 완전히 정합한 스냅샷 패킷을 임의 개수로 손수 만든다. 개수 바이트만
// 고쳐서는 뒤따르는 바이트 수가 어긋나 길이 검사에 먼저 걸리므로, 상한 검사
// 자체를 자극하려면 본문까지 개수에 맞춰 채워야 한다.
static net::Bytes ForgeSnapshot(size_t towers, size_t enemies, size_t projectiles) {
    Writer writer(PacketType::Snapshot);
    writer.U32(1);                    // tick
    writer.U32(0);                    // acknowledgedCommand
    writer.U32(100);                  // gold
    writer.U16(EncodeHp(50.0f));      // coreHp
    writer.U16(EncodeHp(100.0f));     // coreMaxHp
    writer.U16(1);                    // wave
    writer.U8(0);                     // flags
    writer.U8(static_cast<uint8_t>(towers));
    writer.U8(static_cast<uint8_t>(enemies));
    writer.U8(static_cast<uint8_t>(projectiles));
    for (size_t i = 0; i < towers; ++i) {
        writer.U32(static_cast<uint32_t>(i + 1));
        writer.U16(EncodePosition(45.0f));
        writer.U16(EncodePosition(45.0f));
        writer.U8(static_cast<uint8_t>(TowerType::Cannon));
        writer.U8(1);
        writer.U8(static_cast<uint8_t>(TargetMode::Nearest));
    }
    for (size_t i = 0; i < enemies; ++i) {
        writer.U32(static_cast<uint32_t>(1000 + i));
        writer.U16(EncodePosition(60.0f));
        writer.U16(EncodePosition(60.0f));
        writer.U16(EncodeHp(5.0f));
        writer.U16(EncodeHp(10.0f));
        writer.U8(static_cast<uint8_t>(EnemyType::Normal));
    }
    for (size_t i = 0; i < projectiles; ++i) {
        writer.U32(static_cast<uint32_t>(2000 + i));
        writer.U16(EncodePosition(75.0f));
        writer.U16(EncodePosition(75.0f));
    }
    return writer.Finish();
}

// 엔티티 상한. 기대값은 전부 리터럴이다 — 구현이 쓰는 상수와 비교하면 사본이
// 함께 바뀔 때도 통과하는 항등식이 되어 아무것도 지키지 못한다.
static void TestEntityCaps() {
    CHECK(GameWorld::kMaxTowers == static_cast<size_t>(16));   // 게임플레이 상한
    CHECK(kSnapshotMaxTowers == static_cast<size_t>(16));      // 프로토콜 상한
    CHECK(kSnapshotMaxEnemies == static_cast<size_t>(64));
    CHECK(kSnapshotMaxProjectiles == static_cast<size_t>(20));

    // 게임플레이 상한이 실제로 작동한다: 골드도 빈 셀도 남았는데 17번째가 안 선다.
    const DefenseMap map = DefenseMap::CreateDefault();
    GameWorld world;
    world.Init(map, {WaveDef{1, 10, 20, 5, 100.0f}}, 100000, 100);
    int placed = 0;
    int refusedOnEmptyCell = 0;
    for (int y = 0; y < map.Rows(); ++y) {
        for (int x = 0; x < map.Columns(); ++x) {
            if (world.PlaceTowerType(map.CellCenter(x, y), TowerType::Gatling)) ++placed;
            else if (placed == 16 && map.Tile(x, y) == TileType::Buildable) ++refusedOnEmptyCell;
        }
    }
    CHECK_EQ(placed, 16);
    CHECK_EQ(world.TowerCount(), 16);
    CHECK(refusedOnEmptyCell > 0);   // 지형이 아니라 상한 때문에 거절됐다
    CHECK(world.Gold() > 0);         // 골드가 모자라 멈춘 것도 아니다

    // 상한을 정확히 채운 월드는 잘림 없이 한 패킷에 담겨 왕복한다.
    net::Bytes full = WriteSnapshot(world, 1, 0);
    CHECK(!full.empty());
    CHECK(full.size() <= kMaxPacketBytes);
    Snapshot decoded;
    CHECK(ReadSnapshot(full, decoded));
    CHECK_EQ(decoded.towers.size(), static_cast<size_t>(16));
    CHECK(!decoded.truncated);

    // 디코더의 상한이 인코더의 상한과 정확히 같은 자리에 있는가. 세 컬렉션을
    // 가득 채운 패킷은 예산 안에 들어가고 받아들여져야 하며, 어느 하나라도
    // 한 개 초과하면 거부돼야 한다.
    const net::Bytes atCap = ForgeSnapshot(16, 64, 20);
    CHECK(atCap.size() <= kMaxPacketBytes);
    CHECK(ReadSnapshot(atCap, decoded));
    CHECK_EQ(decoded.towers.size(), static_cast<size_t>(16));
    CHECK_EQ(decoded.enemies.size(), static_cast<size_t>(64));
    CHECK_EQ(decoded.projectiles.size(), static_cast<size_t>(20));
    CHECK(!ReadSnapshot(ForgeSnapshot(17, 64, 20), decoded));
    CHECK(!ReadSnapshot(ForgeSnapshot(16, 65, 20), decoded));
    CHECK(!ReadSnapshot(ForgeSnapshot(16, 64, 21), decoded));
}

// 상한을 넘는 월드는 조용히 잘리면 안 된다 — 잘렸다는 사실이 보내는 쪽과 받는 쪽
// 양쪽에 남아야 한다. 감사 #31 은 `truncated` 가 **생산만 되고 아무도 읽지 않는**
// 플래그라고 지적했다. 여기서 켜지는 경로를 실제로 자극한다.
//
// 자극 방법: 웨이브 하나에 와이어 상한(적 64)을 넘는 100마리를 넣고 interval 0 으로
// 한 틱에 전부 스폰시킨다. speed 0 이라 아무도 코어에 닿지 않으므로 다음 틱까지
// 100마리가 전부 살아 있다. 제품 콘텐츠(assets/waves.txt)는 이 상태를 만들 수 없고,
// 만들 수 없다는 것이 곧 "제품에서는 이 플래그가 항상 0" 이라는 뜻이다.
static void TestSnapshotTruncationSignalsLoss() {
    GameWorld world;
    // 동시 생존 상한을 large 로 올려야 100마리가 한 스텝에 다 선다. small(64)에서는
    // 스폰이 상한에 막혀 스냅샷 상한(64)을 넘길 수 없고, 그래서 이 시나리오 자체가
    // 만들어지지 않는다 — 그 사실이 곧 "제품 규모에서는 잘림이 없다"의 근거다.
    Switches switches;
    switches.scale = ScaleKind::Large;
    world.Configure(switches);
    world.Init(math::Vec2{400, 400}, math::Vec2{40, 40},
               {WaveDef{100, 1000.0f, 0.0f, 1, 0.0f, EnemyType::Normal}}, 0, 10000.0f);
    world.Step(1.0f / 60.0f);
    CHECK_EQ(world.Enemies().Size(), static_cast<size_t>(100));

    bool truncated = false;
    const net::Bytes bytes = WriteSnapshot(world, 9, 0, &truncated);
    CHECK(truncated);                        // 보내는 쪽이 손실을 안다
    CHECK(!bytes.empty());                   // 잘랐으므로 예산 안에 들어간다
    CHECK(bytes.size() <= kMaxPacketBytes);

    Snapshot decoded;
    CHECK(ReadSnapshot(bytes, decoded));
    CHECK(decoded.truncated);                // 받는 쪽도 안다
    CHECK_EQ(decoded.enemies.size(), kSnapshotMaxEnemies);

    // 항상 켜지는 플래그는 켜지지 않는 플래그와 똑같이 아무 말도 하지 않는다.
    // 상한 이하 월드에서는 양쪽 다 꺼져 있어야 한다.
    GameWorld withinCap;
    withinCap.Init(math::Vec2{400, 400}, math::Vec2{40, 40},
                   {WaveDef{3, 1000.0f, 0.0f, 1, 0.0f, EnemyType::Normal}}, 0, 10000.0f);
    withinCap.Step(1.0f / 60.0f);
    bool spuriousTruncation = true;          // 인코더가 덮어쓰지 않으면 실패한다
    const net::Bytes small = WriteSnapshot(withinCap, 9, 0, &spuriousTruncation);
    CHECK(!spuriousTruncation);
    CHECK(ReadSnapshot(small, decoded));
    CHECK(!decoded.truncated);
    CHECK_EQ(decoded.enemies.size(), static_cast<size_t>(3));
}

// 헤더의 payload 길이 필드를 실제 크기에 맞춰 다시 봉인한다. 이게 없으면
// 잉여 바이트는 헤더의 길이 검사에 먼저 걸려, 본문을 다 읽고 나서의 소진 검사는
// 자극조차 되지 않는다 — 기존 pop_back() 변형이 정확히 그 상태였고, 그래서
// Reader::Done() 은 이 파일에서 한 번도 검증된 적이 없었다.
static void ResealPayloadLength(net::Bytes& bytes) {
    const uint16_t payload = static_cast<uint16_t>(bytes.size() - 8);
    bytes[6] = static_cast<uint8_t>(payload);
    bytes[7] = static_cast<uint8_t>(payload >> 8);
}

static net::Bytes CommandBytes(CommandType type, uint8_t argument, uint32_t sequence = 3) {
    Command sent;
    sent.sequence = sequence;
    sent.type = type;
    sent.x = EncodePosition(45.0f);
    sent.y = EncodePosition(45.0f);
    sent.argument = argument;
    return WriteCommand(sent);   // 인코더는 순수 직렬화기다. 신뢰 경계는 디코더다.
}

static bool CommandDecodes(CommandType type, uint8_t argument) {
    Command received;
    return ReadCommand(CommandBytes(type, argument), received);
}

// argument 의 의미는 type 이 정하지만, 그 도메인은 전부 와이어 포맷이 이미 아는
// 것이다 — TowerType 집합도 TargetMode 집합도 월드를 봐야 알 수 있는 값이 아니다.
// 따라서 여기가 검사할 자리다. 월드가 있어야 답할 수 있는 것(그 칸이 건설
// 가능한가, 골드가 있는가, 거기 타워가 있는가)만 ApplyCommand 의 몫이다.
static void TestCommandArgumentRange() {
    // 도메인 안의 값은 전부 통과해야 한다 — 거부가 과하지 않다는 반대 방향 증명.
    for (uint8_t t = 0; t < static_cast<uint8_t>(TowerType::Count); ++t)
        CHECK(CommandDecodes(CommandType::Place, t));
    for (uint8_t m = 0; m <= static_cast<uint8_t>(TargetMode::Strongest); ++m)
        CHECK(CommandDecodes(CommandType::Target, m));
    CHECK(CommandDecodes(CommandType::Upgrade, 0));
    CHECK(CommandDecodes(CommandType::Sell, 0));

    // 도메인 밖은 거부된다. 경계 바로 바깥과 극단값을 둘 다 짚는다 — 경계만
    // 보면 부등호 방향이 뒤집힌 검사도 통과할 수 있다.
    CHECK(!CommandDecodes(CommandType::Place, static_cast<uint8_t>(TowerType::Count)));
    CHECK(!CommandDecodes(CommandType::Place, 200));
    CHECK(!CommandDecodes(CommandType::Place, 255));
    CHECK(!CommandDecodes(CommandType::Target,
                          static_cast<uint8_t>(TargetMode::Strongest) + 1));
    CHECK(!CommandDecodes(CommandType::Target, 255));

    // Upgrade·Sell 은 argument 를 읽지 않는다. 읽지 않는 필드는 0 이어야 한다 —
    // 아니면 같은 동작을 뜻하는 패킷이 256가지가 되고, 다음 버전이 이 바이트에
    // 의미를 주는 순간 옛 클라이언트의 쓰레기가 조용히 명령이 된다.
    CHECK(!CommandDecodes(CommandType::Upgrade, 1));
    CHECK(!CommandDecodes(CommandType::Sell, 255));

    // 알려지지 않은 type 은 계속 거부된다(기존 검사의 회귀 방지).
    CHECK(!CommandDecodes(static_cast<CommandType>(0), 0));
    CHECK(CommandDecodes(CommandType::Blast, 0));            // 5 = 발파(G2 에서 생겼다)
    CHECK(!CommandDecodes(CommandType::Blast, 1));           // argument 를 읽지 않는다
    CHECK(!CommandDecodes(static_cast<CommandType>(6), 0));
    CHECK(!CommandDecodes(static_cast<CommandType>(255), 0));

    // 거부된 패킷은 출력 인자를 건드리지 않는다. 반환값을 무시하는 호출자가
    // 생기더라도 범위 밖 값이 구조체에 남아 있으면 안 된다.
    Command received;
    received.sequence = 111;
    received.type = CommandType::Sell;
    received.argument = 2;
    CHECK(!ReadCommand(CommandBytes(CommandType::Place, 200, 77), received));
    CHECK_EQ(received.sequence, 111u);
    CHECK(received.type == CommandType::Sell);
    CHECK_EQ(received.argument, static_cast<uint8_t>(2));
}

// 왜 이것이 디코더의 일이어야 하는가 — ApplyCommand 가 나중에 거절하는 것으로는
// 늦다. ReadCommand 가 통과시키는 순간 호스트는 그 명령을 신뢰 채널에 Accept
// 하고, Accept 는 그 자체로 상태 변경이다(nextExpected_ 를 앞당긴다). 기대
// 시퀀스에 얹힌 위조 명령 하나가 그 자리를 태우면, 정직한 클라이언트가 같은
// 시퀀스로 보낼 진짜 명령은 영원히 중복 취급돼 사라진다.
static void TestOutOfRangeCommandCannotBurnSequence() {
    const DefenseMap map = DefenseMap::CreateDefault();
    GameWorld world;
    world.Init(map, {WaveDef{1, 10, 20, 5, 100.0f}}, 500, 100);

    net::ReliableOrderedReceiver<Command> channel{1};   // 호스트와 같은 시작 시퀀스
    auto HostReceive = [&](const net::Bytes& packet) {  // 호스트 수신 단계와 같은 모양
        Command command;
        if (!ReadCommand(packet, command)) return false;
        return channel.Accept(command.sequence, command);
    };

    const math::Vec2 cell = map.CellCenter(2, 2);
    Command command;
    command.sequence = 1;
    command.type = CommandType::Place;
    command.x = EncodePosition(cell.x);
    command.y = EncodePosition(cell.y);

    command.argument = 200;                    // 범위 밖 TowerType
    HostReceive(WriteCommand(command));        // 채널에 닿지 않아야 한다

    command.argument = static_cast<uint8_t>(TowerType::Cannon);
    CHECK(HostReceive(WriteCommand(command))); // 시퀀스 1 이 아직 살아 있다

    Command delivered;
    CHECK(channel.Poll(delivered));
    CHECK_EQ(delivered.argument, static_cast<uint8_t>(TowerType::Cannon));
    CHECK(ApplyCommand(world, delivered));
    CHECK_EQ(world.TowerCount(), 1);           // 플레이어가 요청한 타워가 실제로 섰다
}

// 잉여 바이트가 붙은 패킷은 거부돼야 한다. 두 방향을 다 짚는다 — 길이 필드를
// 그대로 둔 것(헤더 검사에 걸린다)과 길이 필드까지 다시 봉인한 것(헤더를
// 통과하므로 본문 소진 검사만이 남는다). 후자가 진짜 검사다.
static void TestSurplusBytesRejected() {
    const DefenseMap map = DefenseMap::CreateDefault();
    GameWorld world;
    world.Init(map, {WaveDef{3, 10, 20, 5, 0.1f}}, 200, 100);
    CHECK(world.PlaceTowerType(map.CellCenter(2, 2), TowerType::Gatling));
    for (int i = 0; i < 10; ++i) world.Step(1.0f / 60.0f);

    const net::Bytes clean = WriteSnapshot(world, 42, 9);
    Snapshot snapshot;
    CHECK(ReadSnapshot(clean, snapshot));

    net::Bytes surplus = clean;
    surplus.push_back(0x7f);
    CHECK(!ReadSnapshot(surplus, snapshot));    // 길이 필드가 어긋난다
    ResealPayloadLength(surplus);
    CHECK(!ReadSnapshot(surplus, snapshot));    // 길이는 맞지만 남는 바이트가 있다

    // 개수 바이트를 올리지 않은 채 엔티티 하나 분량을 통째로 덧붙여도 마찬가지.
    // 인코더가 선언한 개수 밖의 바이트는 무슨 모양이든 패킷을 무효로 만든다.
    net::Bytes extraTower = clean;
    for (size_t i = 0; i < kSnapshotTowerBytes; ++i) extraTower.push_back(0);
    ResealPayloadLength(extraTower);
    CHECK(!ReadSnapshot(extraTower, snapshot));

    net::Bytes doubled = clean;
    doubled.insert(doubled.end(), clean.begin(), clean.end());
    ResealPayloadLength(doubled);
    CHECK(!ReadSnapshot(doubled, snapshot));

    // 잉여 바이트 거부는 스냅샷만의 규칙이 아니다 — 모든 디코더가 같아야 한다.
    net::Bytes commandBytes = CommandBytes(CommandType::Place,
                                           static_cast<uint8_t>(TowerType::Cannon));
    Command received;
    CHECK(ReadCommand(commandBytes, received));
    commandBytes.push_back(0);
    CHECK(!ReadCommand(commandBytes, received));
    ResealPayloadLength(commandBytes);
    CHECK(!ReadCommand(commandBytes, received));

    net::Bytes hello = WriteHello();
    CHECK(ReadHello(hello));
    hello.push_back(1);
    ResealPayloadLength(hello);
    CHECK(!ReadHello(hello));

    net::Bytes admission = WriteAdmissionHello("header.signature");
    std::string token;
    CHECK(ReadAdmissionHello(admission, token));
    admission.push_back('x');                   // 선언된 토큰 길이 밖의 바이트
    ResealPayloadLength(admission);
    CHECK(!ReadAdmissionHello(admission, token));
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
    TestEntityCaps();
    TestSnapshotTruncationSignalsLoss();
    TestCommandArgumentRange();
    TestOutOfRangeCommandCannotBurnSequence();
    TestSurplusBytesRejected();
    TestHelloValidation();
    TestSequenceWrapComparison();
}

TEST_MAIN("defense_protocol")
