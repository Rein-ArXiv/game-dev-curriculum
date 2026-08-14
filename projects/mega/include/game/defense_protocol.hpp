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

inline bool ReadCommand(const net::Bytes& bytes, Command& command) {
    Reader reader(bytes);
    if (!reader.Header(PacketType::Command)) return false;
    command.sequence = reader.U32();
    command.type = static_cast<CommandType>(reader.U8());
    command.x = reader.U16();
    command.y = reader.U16();
    command.argument = reader.U8();
    if (!reader.Done()) return false;
    return command.type >= CommandType::Place && command.type <= CommandType::Target;
}

inline net::Bytes WriteSnapshot(const GameWorld& world, uint32_t tick, uint32_t acknowledgedCommand) {
    static constexpr size_t kMaxTowers = 16;
    static constexpr size_t kMaxEnemies = 64;
    static constexpr size_t kMaxProjectiles = 20;

    size_t towerCount = std::min(world.Towers().size(), kMaxTowers);
    size_t enemyCount = std::min(world.Enemies().size(), kMaxEnemies);
    size_t projectileCount = std::min(world.Projectiles().size(), kMaxProjectiles);
    bool truncated = towerCount != world.Towers().size() ||
                     enemyCount != world.Enemies().size() ||
                     projectileCount != world.Projectiles().size();

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
    if (towerCount > 16 || enemyCount > 64 || projectileCount > 20) return false;

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
