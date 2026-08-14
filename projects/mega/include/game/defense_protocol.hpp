// defense_protocol -- bounded wire format for authoritative co-op defense.
//
// Clients send commands, never state. The host validates each command against
// GameWorld and publishes compact snapshots. Every decoder is length checked;
// every collection has an explicit cap so one snapshot remains below the
// conservative 1200-byte UDP payload limit.

#pragma once

#include "game/game.hpp"
#include "net/sequence.hpp"
#include "net/session.hpp"
#include "net/transport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mega {
namespace game {
namespace wire {

static constexpr uint32_t kMagic = 0x3146444du;  // "MDF1" in little endian
static constexpr uint8_t kVersion = 1;
static constexpr size_t kMaxPacketBytes = 1200;

// ── 스냅샷 컬렉션 상한 ─────────────────────────────────────────────
// 이 셋은 게임플레이 규칙이 아니라 와이어 포맷의 일부다. 인코더가 잘라내는
// 개수와 디코더가 거부하는 개수는 정의상 같은 수여야 하므로 상수는 하나씩만
// 존재하고 WriteSnapshot · ReadSnapshot 이 함께 그것을 읽는다. 값을 바꾸면
// 프로토콜이 바뀐다 — kVersion 도 함께 올려야 한다.
static constexpr size_t kSnapshotMaxTowers = 16;
static constexpr size_t kSnapshotMaxEnemies = 64;
static constexpr size_t kSnapshotMaxProjectiles = 20;

// 상한이 어디서 왔는지는 산수다. 가득 찬 스냅샷 한 장이 1200바이트 예산 안에
// 들어가야 하고, 각 개수는 U8 한 바이트에 실린다. 상한을 올리면 둘 중 하나가
// 먼저 깨지므로 컴파일 시간에 센다.
static constexpr size_t kSnapshotFixedBytes = 8 + 4 + 4 + 4 + 2 + 2 + 2 + 1 + 3;
static constexpr size_t kSnapshotTowerBytes = 4 + 2 + 2 + 1 + 1 + 1;
static constexpr size_t kSnapshotEnemyBytes = 4 + 2 + 2 + 2 + 2 + 1;
static constexpr size_t kSnapshotProjectileBytes = 4 + 2 + 2;
static constexpr size_t kSnapshotMaxBytes =
    kSnapshotFixedBytes + kSnapshotMaxTowers * kSnapshotTowerBytes +
    kSnapshotMaxEnemies * kSnapshotEnemyBytes +
    kSnapshotMaxProjectiles * kSnapshotProjectileBytes;
static_assert(kSnapshotMaxBytes <= kMaxPacketBytes,
              "가득 찬 스냅샷이 UDP payload 예산을 넘는다");
static_assert(kSnapshotMaxTowers <= 255 && kSnapshotMaxEnemies <= 255 &&
                  kSnapshotMaxProjectiles <= 255,
              "컬렉션 개수는 U8 한 바이트로 실린다");

// 게임플레이 상한(GameWorld::kMaxTowers)과 프로토콜 상한은 같은 것이 아니다.
// 하나는 디자인 규칙이고 하나는 패킷 예산이라, 지금 값이 같은 것은 우연이다.
// 그래서 하나로 합치지 않는다 — 대신 묶여야 할 방향을 단언한다. 월드가 프로토콜
// 보다 많은 타워를 담을 수 있게 되면 초과분은 min() 에 잘려 모든 스냅샷에서
// 조용히 사라진다. 그 발산을 런타임이 아니라 빌드가 잡는다.
static_assert(GameWorld::kMaxTowers <= kSnapshotMaxTowers,
              "게임플레이 타워 상한이 스냅샷 상한을 넘으면 초과 타워가 모든 "
              "클라이언트에서 사라진다");

enum class PacketType : uint8_t {
    Hello = 1,
    Command = 2,
    Snapshot = 3,
    AdmissionHello = 4,
};
enum class CommandType : uint8_t { Place = 1, Upgrade = 2, Sell = 3, Target = 4 };

struct Command {
    uint32_t sequence = 0;
    CommandType type = CommandType::Place;
    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t argument = 0;
};

struct TowerState {
    uint32_t id = 0;
    math::Vec2 pos{0, 0};
    TowerType type = TowerType::Cannon;
    uint8_t tier = 1;
    TargetMode targetMode = TargetMode::Nearest;
};

struct EnemyState {
    uint32_t id = 0;
    math::Vec2 pos{0, 0};
    float hp = 0;
    float maxHp = 0;
    EnemyType type = EnemyType::Normal;
};

struct ProjectileState {
    uint32_t id = 0;
    math::Vec2 pos{0, 0};
};

struct Snapshot {
    uint32_t tick = 0;
    uint32_t acknowledgedCommand = 0;
    int gold = 0;
    float coreHp = 0;
    float coreMaxHp = 0;
    uint16_t wave = 0;
    bool won = false;
    bool lost = false;
    bool truncated = false;
    std::vector<TowerState> towers;
    std::vector<EnemyState> enemies;
    std::vector<ProjectileState> projectiles;
};

class Writer {
public:
    explicit Writer(PacketType type) {
        U32(kMagic);
        U8(kVersion);
        U8(static_cast<uint8_t>(type));
        U16(0);  // payload size, finalized by Finish()
    }

    void U8(uint8_t value) { bytes_.push_back(value); }
    void U16(uint16_t value) {
        bytes_.push_back(static_cast<uint8_t>(value));
        bytes_.push_back(static_cast<uint8_t>(value >> 8));
    }
    void U32(uint32_t value) {
        U16(static_cast<uint16_t>(value));
        U16(static_cast<uint16_t>(value >> 16));
    }
    void Raw(std::string_view value) {
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    net::Bytes Finish() {
        uint16_t payload = static_cast<uint16_t>(bytes_.size() - 8);
        bytes_[6] = static_cast<uint8_t>(payload);
        bytes_[7] = static_cast<uint8_t>(payload >> 8);
        return bytes_;
    }

private:
    net::Bytes bytes_;
};

class Reader {
public:
    explicit Reader(const net::Bytes& bytes) : bytes_(bytes) {}

    bool Header(PacketType expected) {
        if (bytes_.size() < 8) return false;
        uint32_t magic = U32();
        uint8_t version = U8();
        uint8_t type = U8();
        uint16_t payload = U16();
        if (!ok_ || magic != kMagic || version != kVersion ||
            type != static_cast<uint8_t>(expected)) return false;
        if (payload != bytes_.size() - 8) return false;
        return true;
    }

    uint8_t U8() {
        if (offset_ + 1 > bytes_.size()) { ok_ = false; return 0; }
        return bytes_[offset_++];
    }
    uint16_t U16() {
        uint16_t lo = U8();
        uint16_t hi = U8();
        return static_cast<uint16_t>(lo | (hi << 8));
    }
    uint32_t U32() {
        uint32_t lo = U16();
        uint32_t hi = U16();
        return lo | (hi << 16);
    }
    std::string Raw(size_t count) {
        if (offset_ + count > bytes_.size()) {
            ok_ = false;
            return {};
        }
        std::string value(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                          bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + count));
        offset_ += count;
        return value;
    }
    bool Done() const { return ok_ && offset_ == bytes_.size(); }

private:
    const net::Bytes& bytes_;
    size_t offset_ = 0;
    bool ok_ = true;
};

inline uint16_t EncodePosition(float value) {
    float clamped = std::clamp(value, 0.0f, 8191.875f);
    return static_cast<uint16_t>(std::lround(clamped * 8.0f));
}
inline float DecodePosition(uint16_t value) {
    return static_cast<float>(value) * 0.125f;
}
inline uint16_t EncodeHp(float value) {
    float clamped = std::clamp(value, 0.0f, 16383.75f);
    return static_cast<uint16_t>(std::lround(clamped * 4.0f));
}
inline float DecodeHp(uint16_t value) {
    return static_cast<float>(value) * 0.25f;
}

inline net::Bytes WriteHello() {
    Writer writer(PacketType::Hello);
    return writer.Finish();
}

inline bool ReadHello(const net::Bytes& bytes) {
    Reader reader(bytes);
    return reader.Header(PacketType::Hello) && reader.Done();
}

inline net::Bytes WriteAdmissionHello(std::string_view token) {
    if (token.empty() || token.size() > 1024) return {};
    Writer writer(PacketType::AdmissionHello);
    writer.U16(static_cast<uint16_t>(token.size()));
    writer.Raw(token);
    return writer.Finish();
}

inline bool ReadAdmissionHello(const net::Bytes& bytes, std::string& token) {
    Reader reader(bytes);
    if (!reader.Header(PacketType::AdmissionHello)) return false;
    const uint16_t length = reader.U16();
    if (length == 0 || length > 1024) return false;
    std::string decoded = reader.Raw(length);
    if (!reader.Done()) return false;
    token = std::move(decoded);
    return true;
}

inline net::Bytes WriteCommand(const Command& command) {
    Writer writer(PacketType::Command);
    writer.U32(command.sequence);
    writer.U8(static_cast<uint8_t>(command.type));
    writer.U16(command.x);
    writer.U16(command.y);
    writer.U8(command.argument);
    return writer.Finish();
}

// argument 의 의미는 type 이 정하지만, 그 도메인은 전부 와이어 포맷이 이미 아는
// 것이다 — TowerType 집합도 TargetMode 집합도 월드를 봐야 알 수 있는 값이 아니다.
// 그래서 이 검사는 디코더의 몫이고, 월드가 있어야 답할 수 있는 질문(그 칸이
// 건설 가능한가, 골드가 있는가, 거기 타워가 있는가)만 ApplyCommand 에 남는다.
//
// 여기서 통과시키고 ApplyCommand 에서 거절하는 것으로는 늦다. 호스트는 디코드에
// 성공한 명령을 곧바로 신뢰 채널에 Accept 하고, Accept 는 그 자체로 상태 변경이다
// (nextExpected_ 가 앞당겨진다). 기대 시퀀스에 얹힌 위조 명령 하나가 그 자리를
// 태우면 정직한 클라이언트가 같은 시퀀스로 보낼 진짜 명령은 영원히 중복 취급돼
// 사라진다 — 플레이어가 요청한 타워가 조용히 안 서는 것으로 보인다.
inline bool CommandArgumentInRange(CommandType type, uint8_t argument) {
    switch (type) {
        case CommandType::Place:
            return argument < static_cast<uint8_t>(TowerType::Count);
        case CommandType::Target:
            return argument <= static_cast<uint8_t>(TargetMode::Strongest);
        case CommandType::Upgrade:
        case CommandType::Sell:
            // 이 둘은 argument 를 읽지 않는다. 읽지 않는 필드는 0 이어야 한다 —
            // 아니면 같은 동작을 뜻하는 패킷이 256가지가 되고, 다음 버전이 이
            // 바이트에 의미를 주는 순간 옛 클라이언트의 쓰레기가 명령이 된다.
            return argument == 0;
    }
    return false;
}

inline bool ReadCommand(const net::Bytes& bytes, Command& command) {
    Reader reader(bytes);
    if (!reader.Header(PacketType::Command)) return false;

    // ReadSnapshot 과 같은 규율: 지역에 풀고, 전부 검증된 뒤에만 내보낸다.
    // 출력 인자에 먼저 쓰면 거부된 패킷의 범위 밖 값이 호출자 구조체에 남는다.
    Command decoded;
    decoded.sequence = reader.U32();
    decoded.type = static_cast<CommandType>(reader.U8());
    decoded.x = reader.U16();
    decoded.y = reader.U16();
    decoded.argument = reader.U8();
    if (!reader.Done()) return false;
    if (decoded.type < CommandType::Place || decoded.type > CommandType::Target) return false;
    if (!CommandArgumentInRange(decoded.type, decoded.argument)) return false;
    command = decoded;
    return true;
}

// truncatedOut 은 "이 스냅샷을 만들면서 엔티티를 버렸는가"를 **보낸 쪽**에 돌려준다.
// 같은 사실이 와이어 flags 로도 나가지만(받는 쪽이 읽는다), 그것만으로는 클라이언트가
// 붙어 있을 때만 관측된다. 잘림은 호스트에서 일어나는 손실이므로 호스트도 셀 수 있어야
// 한다 — 감사 #31 이 지적한 "생산만 되고 소비되지 않는 플래그"의 생산 쪽 출구다.
// 기본값 nullptr 이라 관심 없는 호출자는 그대로 둔다.
inline net::Bytes WriteSnapshot(const GameWorld& world, uint32_t tick,
                                uint32_t acknowledgedCommand,
                                bool* truncatedOut = nullptr) {
    size_t towerCount = std::min(world.Towers().size(), kSnapshotMaxTowers);
    size_t enemyCount = std::min(world.Enemies().size(), kSnapshotMaxEnemies);
    size_t projectileCount = std::min(world.Projectiles().size(), kSnapshotMaxProjectiles);
    bool truncated = towerCount != world.Towers().size() ||
                     enemyCount != world.Enemies().size() ||
                     projectileCount != world.Projectiles().size();
    if (truncatedOut) *truncatedOut = truncated;

    Writer writer(PacketType::Snapshot);
    writer.U32(tick);
    writer.U32(acknowledgedCommand);
    writer.U32(static_cast<uint32_t>(std::max(world.Gold(), 0)));
    writer.U16(EncodeHp(world.CoreHp()));
    writer.U16(EncodeHp(world.CoreMaxHp()));
    writer.U16(static_cast<uint16_t>(std::clamp(world.Wave(), 0, 65535)));
    uint8_t flags = static_cast<uint8_t>((world.Won() ? 1 : 0) |
                                         (world.Lost() ? 2 : 0) |
                                         (truncated ? 4 : 0));
    writer.U8(flags);
    writer.U8(static_cast<uint8_t>(towerCount));
    writer.U8(static_cast<uint8_t>(enemyCount));
    writer.U8(static_cast<uint8_t>(projectileCount));

    for (size_t i = 0; i < towerCount; ++i) {
        const Tower& tower = world.Towers()[i];
        writer.U32(tower.id);
        writer.U16(EncodePosition(tower.pos.x));
        writer.U16(EncodePosition(tower.pos.y));
        writer.U8(static_cast<uint8_t>(tower.type));
        writer.U8(static_cast<uint8_t>(tower.tier));
        writer.U8(static_cast<uint8_t>(tower.targetMode));
    }
    for (size_t i = 0; i < enemyCount; ++i) {
        const Enemy& enemy = world.Enemies()[i];
        writer.U32(enemy.id);
        writer.U16(EncodePosition(enemy.pos.x));
        writer.U16(EncodePosition(enemy.pos.y));
        writer.U16(EncodeHp(enemy.hp));
        writer.U16(EncodeHp(enemy.maxHp));
        writer.U8(static_cast<uint8_t>(enemy.type));
    }
    for (size_t i = 0; i < projectileCount; ++i) {
        const Projectile& projectile = world.Projectiles()[i];
        writer.U32(projectile.id);
        writer.U16(EncodePosition(projectile.pos.x));
        writer.U16(EncodePosition(projectile.pos.y));
    }
    net::Bytes bytes = writer.Finish();
    // 이 비교는 방어선이 아니라 마지막 자물쇠다. 실제 상한 보장은 위의
    // `static_assert(kSnapshotMaxBytes <= kMaxPacketBytes)` 가 컴파일 시간에 한다 —
    // 세 컬렉션이 전부 상한이면 1198바이트이므로 이 분기의 else 는 구성상 도달할 수
    // 없다(감사 #31: 변이 테스트에서 이 줄을 지워도 어떤 테스트도 실패하지 않았다.
    // 그것은 테스트의 구멍이 아니라 도달 불가라는 사실의 반영이다). 상한을 올리면
    // static_assert 가 먼저 빌드를 멈추므로 런타임에 여기까지 오지 않는다.
    return bytes.size() <= kMaxPacketBytes ? bytes : net::Bytes{};
}

inline bool ReadSnapshot(const net::Bytes& bytes, Snapshot& snapshot) {
    Reader reader(bytes);
    if (!reader.Header(PacketType::Snapshot)) return false;

    Snapshot decoded;
    decoded.tick = reader.U32();
    decoded.acknowledgedCommand = reader.U32();
    decoded.gold = static_cast<int>(reader.U32());
    decoded.coreHp = DecodeHp(reader.U16());
    decoded.coreMaxHp = DecodeHp(reader.U16());
    decoded.wave = reader.U16();
    uint8_t flags = reader.U8();
    decoded.won = (flags & 1) != 0;
    decoded.lost = (flags & 2) != 0;
    decoded.truncated = (flags & 4) != 0;
    uint8_t towerCount = reader.U8();
    uint8_t enemyCount = reader.U8();
    uint8_t projectileCount = reader.U8();
    // 디코더의 상한은 인코더가 자른 상한과 같은 상수여야 한다. 둘이 갈라지면
    // 한쪽은 보내고 한쪽은 거부하는 패킷이 생긴다.
    if (static_cast<size_t>(towerCount) > kSnapshotMaxTowers ||
        static_cast<size_t>(enemyCount) > kSnapshotMaxEnemies ||
        static_cast<size_t>(projectileCount) > kSnapshotMaxProjectiles) return false;

    decoded.towers.reserve(towerCount);
    for (uint8_t i = 0; i < towerCount; ++i) {
        TowerState tower;
        tower.id = reader.U32();
        tower.pos = {DecodePosition(reader.U16()), DecodePosition(reader.U16())};
        tower.type = static_cast<TowerType>(reader.U8());
        tower.tier = reader.U8();
        tower.targetMode = static_cast<TargetMode>(reader.U8());
        if (tower.type >= TowerType::Count || tower.tier < 1 || tower.tier > 3 ||
            tower.targetMode > TargetMode::Strongest) return false;
        decoded.towers.push_back(tower);
    }
    decoded.enemies.reserve(enemyCount);
    for (uint8_t i = 0; i < enemyCount; ++i) {
        EnemyState enemy;
        enemy.id = reader.U32();
        enemy.pos = {DecodePosition(reader.U16()), DecodePosition(reader.U16())};
        enemy.hp = DecodeHp(reader.U16());
        enemy.maxHp = DecodeHp(reader.U16());
        enemy.type = static_cast<EnemyType>(reader.U8());
        if (enemy.type >= EnemyType::Count || enemy.hp > enemy.maxHp + 0.25f) return false;
        decoded.enemies.push_back(enemy);
    }
    decoded.projectiles.reserve(projectileCount);
    for (uint8_t i = 0; i < projectileCount; ++i) {
        ProjectileState projectile;
        projectile.id = reader.U32();
        projectile.pos = {DecodePosition(reader.U16()), DecodePosition(reader.U16())};
        decoded.projectiles.push_back(projectile);
    }
    if (!reader.Done()) return false;
    snapshot = std::move(decoded);
    return true;
}

// ── 클라이언트 복제 ────────────────────────────────────────────────
// 스냅샷을 쌓고 분수 렌더 틱에서 보간하는 일반 로직은 net/snapshot.hpp ·
// net/session.hpp 가 갖는다. 여기 있는 것은 이 프로토콜 고유의 두 조각뿐이다 —
// id 로 대응 찾기와, 디펜스 스냅샷 필드의 보간.

template <class T>
const T* FindById(const std::vector<T>& values, uint32_t id) {
    for (const T& value : values) if (value.id == id) return &value;
    return nullptr;
}

// 두 스냅샷 사이 표시 상태 보간. a 에 대응이 없는(막 등장한) 엔티티는 보간할
// 과거가 없으므로 b 값을 그대로 쓴다. 권위 상태가 아니라 표시용이므로
// gold·wave·won/lost 같은 이산 필드는 최신(b) 값을 그대로 둔다.
inline Snapshot Lerp(const Snapshot& a, const Snapshot& b, float t) {
    Snapshot out = b;
    out.coreHp = a.coreHp + (b.coreHp - a.coreHp) * t;
    for (EnemyState& enemy : out.enemies) {
        const EnemyState* previous = FindById(a.enemies, enemy.id);
        if (!previous) continue;
        enemy.pos = previous->pos + (enemy.pos - previous->pos) * t;
        enemy.hp = previous->hp + (enemy.hp - previous->hp) * t;
    }
    for (ProjectileState& projectile : out.projectiles) {
        const ProjectileState* previous = FindById(a.projectiles, projectile.id);
        if (previous)
            projectile.pos = previous->pos + (projectile.pos - previous->pos) * t;
    }
    return out;
}

// 클라이언트가 그리는 복제 뷰. 버퍼·렌더 시계는 전부 net/ 것이고, 이 별칭은
// 스냅샷 타입만 이 프로토콜의 것으로 고정한다.
using SnapshotView = net::InterpolatedView<Snapshot>;

// 여기 남은 argument 검사는 ReadCommand 의 중복이 아니다. 솔로·호스트의 로컬
// 입력은 DefenseApp::Submit 에서 Command 를 손수 만들어 곧장 이리로 온다 —
// 디코더를 한 번도 지나지 않는 두 번째 입구다. 이 함수는 그 입구의 문지기다.
inline bool ApplyCommand(GameWorld& world, const Command& command) {
    math::Vec2 position{DecodePosition(command.x), DecodePosition(command.y)};
    switch (command.type) {
        case CommandType::Place:
            if (command.argument >= static_cast<uint8_t>(TowerType::Count)) return false;
            return world.PlaceTowerType(position, static_cast<TowerType>(command.argument));
        case CommandType::Upgrade: {
            int index = world.FindTowerAt(position);
            return index >= 0 && world.UpgradeTower(index);
        }
        case CommandType::Sell: {
            int index = world.FindTowerAt(position);
            return index >= 0 && world.SellTower(index) > 0;
        }
        case CommandType::Target: {
            int index = world.FindTowerAt(position);
            if (index < 0 || command.argument > static_cast<uint8_t>(TargetMode::Strongest)) return false;
            return world.SetTowerTarget(index, static_cast<TargetMode>(command.argument));
        }
    }
    return false;
}

}  // namespace wire
}  // namespace game
}  // namespace mega
