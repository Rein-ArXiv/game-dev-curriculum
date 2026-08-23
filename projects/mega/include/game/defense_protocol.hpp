// defense_protocol -- bounded wire format for authoritative co-op defense.
//
// Clients send commands, never state. The host validates each command against
// GameWorld and publishes compact snapshots. Every decoder is length checked;
// every collection has an explicit cap so one snapshot remains below the
// conservative 1200-byte UDP payload limit.

#pragma once

#include "algo/quickselect.hpp"
#include "game/game.hpp"
#include "live/feature_flag.hpp"
#include "mp/det_sim.hpp"
#include "mp/input.hpp"
#include "net/packet.hpp"
#include "platform/byteorder.hpp"
#include "platform/detect.hpp"
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
// 7 은 스냅샷의 적 64자리가 **저장 행 순서**가 아니라 relevancy 로 채워질 수 있게
// 된 단계다. 프레이밍 바이트는 하나도 바뀌지 않았지만 같은 월드가 다른 64를 실을
// 수 있으므로, 버전을 올리지 않으면 두 빌드가 "같은 프로토콜"이라고 주장하면서
// 서로 다른 세계를 그린다. 기본 팔은 그대로 rows 다 — 팔이 결과를 바꾼다면
// 그것은 스위치가 아니라 변경이라는 규약이 여기서도 유효하고, 그래서 팔을
// 바꾸는 쪽이 옵트인이다.
static constexpr uint8_t kVersion = 7;
static constexpr size_t kMaxPacketBytes = 1200;

// ── 스냅샷 컬렉션 상한 ─────────────────────────────────────────────
// 이 셋은 게임플레이 규칙이 아니라 와이어 포맷의 일부다. 인코더가 잘라내는
// 개수와 디코더가 거부하는 개수는 정의상 같은 수여야 하므로 상수는 하나씩만
// 존재하고 WriteSnapshot · ReadSnapshot 이 함께 그것을 읽는다. 값을 바꾸면
// 프로토콜이 바뀐다 — kVersion 도 함께 올려야 한다.
static constexpr size_t kSnapshotMaxTowers = 16;
static constexpr size_t kSnapshotMaxEnemies = 64;
// 발사체 상한이 20 이 아니라 18 인 이유. 합류 헤더(net::PacketHeader 8바이트)가
// 게임 헤더 뒤에 들어오면서 고정부가 8바이트 늘었고, 그대로 두면 가득 찬 스냅샷이
// 1206바이트가 되어 아래 static_assert 가 빌드를 멈춘다. 예산 1200 을 올려서
// 해결하지 않는다 — 그 상수는 UDP 경로가 정한 값이지 우리가 정한 값이 아니다.
// 세 컬렉션 중 이 행만 순수 표시용이고 매 틱 새로 생겼다 사라지므로(타워는
// 플레이어가 세운 것, 적은 권위 상태다) 한 줄 잃는 비용이 가장 싸다.
// 그 뒤 아바타 서브시뮬의 (프레임 · 체크섬) 8바이트가 더 들어오면서 19 에서
// 18 로 한 줄 더 내렸다. 체크섬만 4바이트 싣지 않는 이유는 비교가 성립하지
// 않기 때문이다 — 클라이언트가 자기 어느 프레임과 맞춰 봐야 하는지 모르면
// 그 수는 대조할 수 없는 숫자다. 프레임 번호가 그 대조의 좌표다.
static constexpr size_t kSnapshotMaxProjectiles = 18;

// ── 스냅샷 본문의 첫 바이트 ────────────────────────────────────────
// 전체인가 차분인가. 차분이면 뒤에 기준선 seq 2바이트가 더 붙는다. 이 바이트를
// 본문 맨 앞에 두는 이유는 디코더가 **무엇을 읽을지 정하기 전에** 알아야 하는
// 유일한 정보이기 때문이다.
inline constexpr uint8_t kSnapshotFull  = 0;
inline constexpr uint8_t kSnapshotDelta = 1;

// 두 보관소의 칸 수. 나란히 두는 이유가 static_assert 그 자체다 — 호스트가
// 기준선으로 가리킬 수 있는 seq 는 자기 보관소 안에 있고, 클라이언트는 그
// 전부를 복원해 들고 있어야 한다. 클라이언트 쪽이 더 좁으면 호스트가 살아
// 있다고 믿는 기준선을 클라이언트는 이미 버린 뒤이고, 그 차분은 복원 불가다.
// snippet-begin: baseline-slots
inline constexpr size_t kSnapshotBaselineSlots = 32;   // 호스트: 보낸 전체 스냅샷
inline constexpr size_t kSnapshotHeldSlots     = 64;   // 클라이언트: 복원해 둔 것
static_assert(kSnapshotHeldSlots > kSnapshotBaselineSlots,
              "클라이언트 보관 칸이 호스트 기준선 칸보다 적으면 호스트가 "
              "클라이언트에 없는 기준선을 가리킨다");
// snippet-end: baseline-slots

// 상한이 어디서 왔는지는 산수다. 가득 찬 스냅샷 한 장이 1200바이트 예산 안에
// 들어가야 하고, 각 개수는 U8 한 바이트에 실린다. 상한을 올리면 둘 중 하나가
// 먼저 깨지므로 컴파일 시간에 센다.
// snippet-begin: snapshot-budget
static constexpr size_t kSnapshotFixedBytes = 16 + 1 + 4 + 4 + 4 + 2 + 2 + 2 + 1 + 3 + 4 + 4;
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
// snippet-end: snapshot-budget

enum class PacketType : uint8_t {
    Hello = 1,
    Command = 2,
    Snapshot = 3,
    AdmissionHello = 4,
    Terrain = 5,
    Input = 6,
    QueueRequest = 7,
    QueueResponse = 8,
    AdmissionGrant = 9,
};
enum class CommandType : uint8_t { Place = 1, Upgrade = 2, Sell = 3, Target = 4, Blast = 5 };

// ── 지형 델타 ──────────────────────────────────────────────────────
// 절차 맵은 시드에서 나오므로 맵 자체는 와이어를 건너지 않는다. 하지만 발파는
// 매치 중에 그 격자를 바꾼다 — 클라이언트가 그 사실을 모르면 뚫린 벽을 계속
// 그린다. 스냅샷에 실을 수는 없다: 가득 찬 스냅샷이 이미 1198바이트라
// (kSnapshotMaxBytes) 여기에 칸 목록을 얹으면 예산 단언이 컴파일 시간에 깨진다.
// 그래서 별도 패킷으로 낸다. 내용은 **누적 전체 목록**이라 멱등이고, 유실은
// 다음 전송으로 저절로 메워진다 — 신뢰 채널이 필요 없는 이유가 그것이다.
static constexpr size_t kMaxTerrainCells = 96;

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
    // 은신 개체가 지금 드러나 있는가. **바이트를 하나도 늘리지 않는다** —
    // 적 하나가 13바이트이고 스냅샷 예산이 1200바이트인데 지금 1198바이트를
    // 쓴다(kSnapshotMaxBytes). 적마다 1바이트를 더하면 64바이트가 늘어
    // static_assert 가 컴파일 시간에 빌드를 멈춘다. 그래서 type 바이트의
    // 빈 비트를 쓴다 — EnemyType 은 여섯 값이라 하위 3비트면 충분하다.
    bool revealed = true;
};

// type 바이트의 최상위 비트가 노출 플래그다.
inline constexpr uint8_t kEnemyRevealedBit = 0x80;
inline constexpr uint8_t kEnemyTypeMask = 0x7F;

struct ProjectileState {
    uint32_t id = 0;
    math::Vec2 pos{0, 0};
};

struct Snapshot {
    uint32_t tick = 0;
    uint32_t acknowledgedCommand = 0;
    // 아바타 서브시뮬의 대조 좌표. frame 은 이 체크섬이 어느 프레임의 것인지,
    // checksum 은 그 프레임 상태의 하위 32비트다. 둘이 함께 있어야 클라이언트가
    // "내 같은 프레임"과 맞춰 볼 수 있다.
    uint32_t avatarFrame = 0;
    uint32_t avatarChecksum = 0;
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
    // 프레이밍 기본 인자는 프레이밍을 쓰지 않는 호출자(테스트의 위조 패킷 생성기
    // 등)를 위한 것이다. 기본값 seq=0/ack=0/ackBits=0 은 "아직 아무것도 못 받았다"와
    // 구분되지 않으므로 **제품 경로는 반드시 명시적으로 넘긴다**.
    explicit Writer(PacketType type, const net::PacketHeader& framing = net::PacketHeader()) {
        U32(kMagic);
        U8(kVersion);
        U8(static_cast<uint8_t>(type));
        U16(0);  // payload size, finalized by Finish()
        U16(framing.seq);
        U16(framing.ack);
        U32(framing.ackBits);
    }

    // 리틀엔디안 정규화는 platform/byteorder.hpp 하나가 소유한다. 여기 있던
    // 손으로 쓴 시프트는 "리틀엔디안 머신이라서 옳은가, 시프트로 적어서 옳은가"를
    // 구분할 수 없게 만들던 자리다 — 나가는 바이트는 하나도 변하지 않으며,
    // test_golden_packet 이 그 동치를 잠근다.
    void U8(uint8_t value) { platform::AppendLE8(bytes_, value); }
    void U16(uint16_t value) { platform::AppendLE16(bytes_, value); }
    void U32(uint32_t value) { platform::AppendLE32(bytes_, value); }
    void U64(uint64_t value) { platform::AppendLE64(bytes_, value); }
    void Raw(std::string_view value) {
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    net::Bytes Finish() {
        uint16_t payload = static_cast<uint16_t>(bytes_.size() - 16);
        platform::WriteLE16(bytes_.data() + 6, payload);
        return bytes_;
    }

private:
    net::Bytes bytes_;
};

class Reader {
public:
    explicit Reader(const net::Bytes& bytes) : bytes_(bytes) {}

    bool Header(PacketType expected, net::PacketHeader* framing = nullptr) {
        if (bytes_.size() < 16) return false;
        uint32_t magic = U32();
        uint8_t version = U8();
        uint8_t type = U8();
        uint16_t payload = U16();
        net::PacketHeader decoded;
        decoded.seq = U16();
        decoded.ack = U16();
        decoded.ackBits = U32();
        if (!ok_ || magic != kMagic || version != kVersion ||
            type != static_cast<uint8_t>(expected)) return false;
        if (payload != bytes_.size() - 16) return false;
        if (framing) *framing = decoded;
        return true;
    }

    uint8_t U8() {
        if (offset_ + 1 > bytes_.size()) { ok_ = false; return 0; }
        return bytes_[offset_++];
    }
    // U8 은 그대로 둔다 — 이미 경계 검사가 있고 바이트 하나에 엔디안이 없다.
    // 아래 셋은 읽기를 byteorder 로 옮기되 **경계 검사를 앞에 남긴다**. 검사를
    // 잃으면 잘린 패킷이 버퍼 밖을 읽는다.
    uint16_t U16() {
        if (offset_ + 2 > bytes_.size()) { ok_ = false; return 0; }
        const uint16_t v = platform::ReadLE16(bytes_.data() + offset_);
        offset_ += 2;
        return v;
    }
    uint32_t U32() {
        if (offset_ + 4 > bytes_.size()) { ok_ = false; return 0; }
        const uint32_t v = platform::ReadLE32(bytes_.data() + offset_);
        offset_ += 4;
        return v;
    }
    uint64_t U64() {
        if (offset_ + 8 > bytes_.size()) { ok_ = false; return 0; }
        const uint64_t v = platform::ReadLE64(bytes_.data() + offset_);
        offset_ += 8;
        return v;
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

inline net::Bytes WriteHello(const net::PacketHeader& framing = net::PacketHeader()) {
    Writer writer(PacketType::Hello, framing);
    return writer.Finish();
}

inline bool ReadHello(const net::Bytes& bytes, net::PacketHeader* framing = nullptr) {
    Reader reader(bytes);
    return reader.Header(PacketType::Hello, framing) && reader.Done();
}

// 입장 인사가 실어 오는 클라이언트의 정체. 토큰은 "누구인가"에 답하고, 아래
// 셋은 "무엇을 들고 있는가"에 답한다 — 콘텐츠 해시가 다른 클라이언트는 같은
// 게임을 돌고 있지 않다.
struct AdmissionHelloDetail {
    uint64_t contentHash = 0;
    uint8_t  osCode = 0;
    uint8_t  archCode = 0;
};

inline net::Bytes WriteAdmissionHello(std::string_view token,
                                      const net::PacketHeader& framing = net::PacketHeader(),
                                      uint64_t contentHash = 0,
                                      uint8_t osCode = 0, uint8_t archCode = 0) {
    if (token.empty() || token.size() > 1024) return {};
    Writer writer(PacketType::AdmissionHello, framing);
    writer.U16(static_cast<uint16_t>(token.size()));
    writer.Raw(token);
    writer.U64(contentHash);
    writer.U8(osCode);
    writer.U8(archCode);
    return writer.Finish();
}

inline bool ReadAdmissionHello(const net::Bytes& bytes, std::string& token,
                               net::PacketHeader* framing = nullptr,
                               AdmissionHelloDetail* detail = nullptr) {
    Reader reader(bytes);
    if (!reader.Header(PacketType::AdmissionHello, framing)) return false;
    const uint16_t length = reader.U16();
    if (length == 0 || length > 1024) return false;
    std::string decoded = reader.Raw(length);
    AdmissionHelloDetail out;
    out.contentHash = reader.U64();
    out.osCode = reader.U8();
    out.archCode = reader.U8();
    if (!reader.Done()) return false;
    token = std::move(decoded);
    if (detail) *detail = out;
    return true;
}

// ── 입장 승인 ──────────────────────────────────────────────────────
// 서버가 승인과 함께 내려보내는 것 넷: 콘텐츠 정체성(버전·해시), 서버 플랫폼,
// 매치 시작 시각(이벤트 판정의 유일한 입력), 확정된 플래그 배정.
//
// 시작 시각을 서버가 정하는 이유는 이벤트가 기간이기 때문이다. 두 클라이언트가
// 각자 시계를 읽으면 한쪽만 주말 이벤트 안에 있는 매치가 생긴다.
struct AdmissionGrant {
    uint32_t    contentVersion = 0;
    uint64_t    contentHash = 0;
    uint8_t     serverOs = 0;
    uint8_t     serverArch = 0;
    int64_t     matchStartUnix = 0;
    std::string flags;              // live::FlagSet::Encode()
};

// snippet-begin: grant-budget
static constexpr size_t kAdmissionGrantFixedBytes = 4 + 8 + 1 + 1 + 8 + 2;
static constexpr size_t kAdmissionGrantMaxBytes =
    16 + kAdmissionGrantFixedBytes + live::kFlagWireMax;
static_assert(kAdmissionGrantMaxBytes <= kMaxPacketBytes,
              "입장 승인 패킷이 UDP payload 예산을 넘는다");
// snippet-end: grant-budget

inline net::Bytes WriteAdmissionGrant(const AdmissionGrant& grant,
                                      const net::PacketHeader& framing = net::PacketHeader()) {
    if (grant.flags.size() > live::kFlagWireMax) return {};
    Writer writer(PacketType::AdmissionGrant, framing);
    writer.U32(grant.contentVersion);
    writer.U64(grant.contentHash);
    writer.U8(grant.serverOs);
    writer.U8(grant.serverArch);
    writer.U64(static_cast<uint64_t>(grant.matchStartUnix));
    writer.U16(static_cast<uint16_t>(grant.flags.size()));
    writer.Raw(grant.flags);
    return writer.Finish();
}

inline bool ReadAdmissionGrant(const net::Bytes& bytes, AdmissionGrant& grant,
                               net::PacketHeader* framing = nullptr) {
    Reader reader(bytes);
    if (!reader.Header(PacketType::AdmissionGrant, framing)) return false;
    AdmissionGrant decoded;
    decoded.contentVersion = reader.U32();
    decoded.contentHash = reader.U64();
    decoded.serverOs = reader.U8();
    decoded.serverArch = reader.U8();
    decoded.matchStartUnix = static_cast<int64_t>(reader.U64());
    const uint16_t flagBytes = reader.U16();
    if (flagBytes > live::kFlagWireMax) return false;
    decoded.flags = reader.Raw(flagBytes);
    if (!reader.Done()) return false;
    // 디코더가 배정 문법까지 본다. 여기서 안 보면 잘못된 문자열이 스위치 표까지
    // 흘러가고, 거기서는 "모르는 이름"과 구분되지 않는다. 빈 배정은 허용이다 —
    // 정의가 하나도 없는 서버가 정상 상태이기 때문이다.
    if (flagBytes > 0) {
        live::FlagSet parsed;
        if (!live::FlagSet::Decode(decoded.flags, parsed)) return false;
    }
    grant = std::move(decoded);
    return true;
}

inline net::Bytes WriteCommand(const Command& command,
                               const net::PacketHeader& framing = net::PacketHeader()) {
    Writer writer(PacketType::Command, framing);
    writer.U32(command.sequence);
    writer.U8(static_cast<uint8_t>(command.type));
    writer.U16(command.x);
    writer.U16(command.y);
    writer.U8(command.argument);
    return writer.Finish();
}

// snippet-begin: terrain-packet
inline net::Bytes WriteTerrain(const std::vector<uint16_t>& cells,
                               const net::PacketHeader& framing = net::PacketHeader()) {
    Writer writer(PacketType::Terrain, framing);
    const size_t count = std::min(cells.size(), kMaxTerrainCells);
    writer.U8(static_cast<uint8_t>(count));
    for (size_t i = 0; i < count; ++i) writer.U16(cells[i]);
    return writer.Finish();
}

inline bool ReadTerrain(const net::Bytes& bytes, std::vector<uint16_t>& cells,
                        net::PacketHeader* framing = nullptr) {
    Reader reader(bytes);
    if (!reader.Header(PacketType::Terrain, framing)) return false;
    const uint8_t count = reader.U8();
    if (static_cast<size_t>(count) > kMaxTerrainCells) return false;
    std::vector<uint16_t> decoded;
    decoded.reserve(count);
    for (uint8_t i = 0; i < count; ++i) decoded.push_back(reader.U16());
    if (!reader.Done()) return false;
    cells = std::move(decoded);
    return true;
}
// snippet-end: terrain-packet

// ── 레이팅 큐 ──────────────────────────────────────────────────────
// 매치에 이름이 생기기 전에는 서버가 "먼저 도착한 토큰이 말한 matchId" 를 그대로
// 믿을 수밖에 없었다. 큐 엔드포인트가 그 주체를 바꾼다: 플레이어가 자기 id 와
// 레이팅을 들고 줄을 서고, 서버가 매치를 **형성해서** 이름을 돌려준다.
static constexpr size_t kMaxPlayerIdBytes = 64;
// FormatMatchId 가 내는 폭. 정확히 이 길이만 받는다 — 길이를 느슨하게 두면
// 형성되지 않은 이름이 문법만 맞춰 들어올 통로가 생긴다.
static constexpr size_t kMatchIdBytes = 15;
static constexpr int    kMaxQueueRating = 3000;

inline net::Bytes WriteQueueRequest(std::string_view playerId, int rating,
                                    const net::PacketHeader& framing = net::PacketHeader()) {
    if (playerId.empty() || playerId.size() > kMaxPlayerIdBytes) return {};
    if (rating < 0 || rating > kMaxQueueRating) return {};
    Writer writer(PacketType::QueueRequest, framing);
    writer.U8(static_cast<uint8_t>(playerId.size()));
    writer.Raw(playerId);
    writer.U16(static_cast<uint16_t>(rating));
    return writer.Finish();
}

inline bool ReadQueueRequest(const net::Bytes& bytes, std::string& playerId, int& rating,
                             net::PacketHeader* framing = nullptr) {
    Reader reader(bytes);
    if (!reader.Header(PacketType::QueueRequest, framing)) return false;
    const uint8_t length = reader.U8();
    if (length == 0 || static_cast<size_t>(length) > kMaxPlayerIdBytes) return false;
    std::string decodedId = reader.Raw(length);
    const uint16_t decodedRating = reader.U16();
    if (!reader.Done()) return false;
    // 레이팅은 클라이언트가 말하는 값이다. 신뢰 경계 밖이므로 상한에서 자른다 —
    // 여기서 안 자르면 3000 위의 수 하나가 큐 정렬을 통째로 왜곡한다.
    if (static_cast<int>(decodedRating) > kMaxQueueRating) return false;
    playerId = std::move(decodedId);
    rating = static_cast<int>(decodedRating);
    return true;
}

inline net::Bytes WriteQueueResponse(std::string_view matchId,
                                     const net::PacketHeader& framing = net::PacketHeader()) {
    if (matchId.size() != kMatchIdBytes) return {};
    Writer writer(PacketType::QueueResponse, framing);
    writer.U8(static_cast<uint8_t>(matchId.size()));
    writer.Raw(matchId);
    return writer.Finish();
}

inline bool ReadQueueResponse(const net::Bytes& bytes, std::string& matchId,
                              net::PacketHeader* framing = nullptr) {
    Reader reader(bytes);
    if (!reader.Header(PacketType::QueueResponse, framing)) return false;
    const uint8_t length = reader.U8();
    if (static_cast<size_t>(length) != kMatchIdBytes) return false;
    std::string decoded = reader.Raw(length);
    if (!reader.Done()) return false;
    matchId = std::move(decoded);
    return true;
}

// ── 건설 사거리 규칙 ───────────────────────────────────────────────
// 규칙이 한 곳에만 있어야 하는 자리다. 로컬 제출 · 원격 명령 · 전용 서버 ·
// HUD 커서가 전부 이 둘을 부른다 — 어느 하나가 자기 사본을 쓰면 초록으로
// 칠해 놓고 안 서는 칸이 생긴다.
// snippet-begin: build-reach
inline constexpr int32_t kBuildReach = 2;   // 체비셰프 칸 거리

// 맵 좌표를 아바타 격자(16x16)의 칸 번호로. 절차 맵은 크기가 다를 수 있으므로
// 비례로 접는다 — 두 격자가 같은 칸 수라는 가정을 코드에 심으면 map=procgen
// 에서 사거리 규칙이 조용히 다른 것을 뜻하게 된다.
inline int32_t AvatarCellOfMap(const DefenseMap& map, const math::Vec2& position) {
    const DefenseMap::Cell cell = map.WorldToCell(position);
    const int columns = map.Columns() > 0 ? map.Columns() : 1;
    const int rows = map.Rows() > 0 ? map.Rows() : 1;
    const double fx = (static_cast<double>(cell.x) + 0.5) / columns;
    const double fy = (static_cast<double>(cell.y) + 0.5) / rows;
    return mp::DetState::CellIndex(static_cast<int32_t>(fx * mp::DetState::kWorld),
                                   static_cast<int32_t>(fy * mp::DetState::kWorld));
}

inline bool BuildWithinReach(const DefenseMap& map, int32_t avatarCell,
                             const math::Vec2& position) {
    return mp::DetState::CellsWithinReach(avatarCell, AvatarCellOfMap(map, position),
                                          kBuildReach);
}
// snippet-end: build-reach

// 문자열 playerId 를 매치메이커의 정수 id 로. 큐 요청과 입장 토큰이 **같은
// 함수**를 지나야 "줄 선 사람"과 "들어온 사람"이 같은 사람인지 물을 수 있다.
inline uint32_t PlayerKey(std::string_view playerId) {
    uint32_t hash = 2166136261u;
    for (char c : playerId) { hash ^= static_cast<uint8_t>(c); hash *= 16777619u; }
    return hash;
}

// ── 아바타 프레임 입력 ─────────────────────────────────────────────
// 명령과 성질이 반대다: 촘촘하고, 최신이 중요하고, 늦으면 무의미하다. 그래서
// 신뢰 채널을 **쓰지 않는다** — 손실 한 번이 뒤의 모든 입력을 막으면 아바타가
// 그 자리에 굳는다. 대신 최근 몇 프레임분을 매번 통째로 다시 싣는다.
//
// 본문은 mp::WriteInputStream 그대로다. 이 층이 더하는 것은 합류 헤더와
// 패킷 종류뿐이라, 입력 레코드의 바이트 계약은 mp/ 하나가 소유한다.
static constexpr size_t kInputPacketMaxBytes =
    16 + mp::kInputStreamHeaderBytes + mp::kMaxInputFrames * mp::kInputBytes;
static_assert(kInputPacketMaxBytes <= kMaxPacketBytes,
              "아바타 입력 패킷이 UDP payload 예산을 넘는다");

inline net::Bytes WriteInputPacket(uint32_t baseFrame, const mp::Input* inputs, size_t count,
                                   const net::PacketHeader& framing = net::PacketHeader()) {
    const std::vector<uint8_t> body = mp::WriteInputStream(baseFrame, inputs, count);
    Writer writer(PacketType::Input, framing);
    writer.Raw(std::string_view(reinterpret_cast<const char*>(body.data()), body.size()));
    return writer.Finish();
}

inline bool ReadInputPacket(const net::Bytes& bytes, uint32_t& baseFrame,
                            std::vector<mp::Input>& inputs,
                            net::PacketHeader* framing = nullptr) {
    Reader reader(bytes);
    if (!reader.Header(PacketType::Input, framing)) return false;
    if (bytes.size() < 16) return false;
    const std::vector<uint8_t> body(bytes.begin() + 16, bytes.end());
    if (!mp::ReadInputStream(body, baseFrame, inputs)) return false;
    return true;
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
        case CommandType::Blast:
        case CommandType::Upgrade:
        case CommandType::Sell:
            // 이 둘은 argument 를 읽지 않는다. 읽지 않는 필드는 0 이어야 한다 —
            // 아니면 같은 동작을 뜻하는 패킷이 256가지가 되고, 다음 버전이 이
            // 바이트에 의미를 주는 순간 옛 클라이언트의 쓰레기가 명령이 된다.
            return argument == 0;
    }
    return false;
}

inline bool ReadCommand(const net::Bytes& bytes, Command& command,
                        net::PacketHeader* framing = nullptr) {
    Reader reader(bytes);
    if (!reader.Header(PacketType::Command, framing)) return false;

    // ReadSnapshot 과 같은 규율: 지역에 풀고, 전부 검증된 뒤에만 내보낸다.
    // 출력 인자에 먼저 쓰면 거부된 패킷의 범위 밖 값이 호출자 구조체에 남는다.
    Command decoded;
    decoded.sequence = reader.U32();
    decoded.type = static_cast<CommandType>(reader.U8());
    decoded.x = reader.U16();
    decoded.y = reader.U16();
    decoded.argument = reader.U8();
    if (!reader.Done()) return false;
    if (decoded.type < CommandType::Place || decoded.type > CommandType::Blast) return false;
    if (!CommandArgumentInRange(decoded.type, decoded.argument)) return false;
    command = decoded;
    return true;
}

// truncatedOut 은 "이 스냅샷을 만들면서 엔티티를 버렸는가"를 **보낸 쪽**에 돌려준다.
// 같은 사실이 와이어 flags 로도 나가지만(받는 쪽이 읽는다), 그것만으로는 클라이언트가
// 붙어 있을 때만 관측된다. 잘림은 호스트에서 일어나는 손실이므로 호스트도 셀 수 있어야
// 한다 — 감사 #31 이 지적한 "생산만 되고 소비되지 않는 플래그"의 생산 쪽 출구다.
// 기본값 nullptr 이라 관심 없는 호출자는 그대로 둔다.
// ── 어느 64를 싣는가 ──────────────────────────────────────────────
// 스냅샷 한 장에 실을 수 있는 적은 64자리다. 규모 팔에서 살아 있는 적은 2048까지
// 오르므로 그 자리를 무엇으로 채우는가가 곧 클라이언트가 보는 세계다.
//
// 저장 행 순서로 채우면(기본 팔) 코어 앞에 도착한 적이 뒷 행이라는 이유만으로
// 화면에서 사라진다. 그리고 그 행 순서는 저장 배치를 바꾸는 A/B 팔에서 실제로
// 달라진다 — 같은 월드가 팔에 따라 다른 적을 싣는다.
//
// relevancy 팔은 전순서를 정하고 상위 64를 고른다. 순서는 네 겹이다.
//
//   1. 노출된 개체 먼저   은신 중인 개체는 클라이언트가 그리지도 못한다.
//   2. 코어에 가까운 순   격자 맨해튼 거리. 정수라 부동소수 비교가 섞이지 않는다.
//   3. 체력이 많은 순     같은 자리라면 더 오래 버티는 쪽이 더 오래 보여야 한다.
//   4. id 오름차순        **동점을 남기지 않는 마지막 겹**. 이것이 없으면 분할이
//                         동점 둘을 어느 쪽에 두는지가 행 순서에 좌우된다.
// snippet-begin: snapshot-relevance
struct EnemyPick {
    uint32_t hidden = 0;     // 0 = 노출됨
    uint32_t distance = 0;   // 코어까지 격자 맨해튼 거리
    uint32_t strength = 0;   // 인코드된 체력의 보수 — 작을수록 강하다
    uint32_t id = 0;
    size_t   row = 0;
};

inline bool MoreRelevant(const EnemyPick& a, const EnemyPick& b) {
    if (a.hidden != b.hidden) return a.hidden < b.hidden;
    if (a.distance != b.distance) return a.distance < b.distance;
    if (a.strength != b.strength) return a.strength < b.strength;
    return a.id < b.id;    // 전순서를 닫는 겹. id 는 월드 안에서 유일하다.
}

inline void CollectEnemyPicks(const GameWorld& world, std::vector<EnemyPick>& out) {
    out.clear();
    const DefenseMap* map = world.Map();
    const size_t rows = world.Enemies().Size();
    out.reserve(rows);
    for (size_t i = 0; i < rows; ++i) {
        if (!world.Enemies().Alive(i)) continue;
        EnemyPick pick;
        pick.row = i;
        pick.id = world.Enemies().Id(i);
        pick.hidden = world.Revealed(i) ? 0u : 1u;
        if (map) {
            const DefenseMap::Cell core = map->CoreCell();
            const DefenseMap::Cell at = map->WorldToCell(world.Enemies().Pos(i));
            const int dx = at.x > core.x ? at.x - core.x : core.x - at.x;
            const int dy = at.y > core.y ? at.y - core.y : core.y - at.y;
            pick.distance = static_cast<uint32_t>(dx + dy);
        }
        pick.strength = 0xFFFFu - EncodeHp(world.Enemies().Hp(i));
        out.push_back(pick);
    }
}
// snippet-end: snapshot-relevance

inline net::Bytes WriteSnapshot(const GameWorld& world, uint32_t tick,
                                uint32_t acknowledgedCommand,
                                bool* truncatedOut = nullptr,
                                const net::PacketHeader& framing = net::PacketHeader(),
                                uint32_t avatarFrame = 0,
                                uint32_t avatarChecksum = 0) {
    // 저장이 AoS 든 SoA 든 이 인코더는 같은 바이트를 낸다 — 여기서 읽는 것은
    // 배치가 아니라 행 번호별 필드다. 살아 있는 개체만 세는 것도 저장과 무관하다.
    size_t aliveEnemies = 0;
    const size_t enemyRows = world.Enemies().Size();
    for (size_t i = 0; i < enemyRows; ++i)
        if (world.Enemies().Alive(i)) ++aliveEnemies;
    const size_t liveProjectiles = world.Projectiles().Size();

    size_t towerCount = std::min(world.Towers().size(), kSnapshotMaxTowers);
    size_t enemyCount = std::min(aliveEnemies, kSnapshotMaxEnemies);
    size_t projectileCount = std::min(liveProjectiles, kSnapshotMaxProjectiles);
    bool truncated = towerCount != world.Towers().size() ||
                     enemyCount != aliveEnemies ||
                     projectileCount != liveProjectiles;
    if (truncatedOut) *truncatedOut = truncated;

    Writer writer(PacketType::Snapshot, framing);
    writer.U8(kSnapshotFull);
    writer.U32(tick);
    writer.U32(acknowledgedCommand);
    writer.U32(avatarFrame);
    writer.U32(avatarChecksum);
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
    auto writeEnemy = [&writer, &world](size_t i) {
        writer.U32(world.Enemies().Id(i));
        writer.U16(EncodePosition(world.Enemies().Pos(i).x));
        writer.U16(EncodePosition(world.Enemies().Pos(i).y));
        writer.U16(EncodeHp(world.Enemies().Hp(i)));
        writer.U16(EncodeHp(world.Enemies().MaxHp(i)));
        writer.U8(static_cast<uint8_t>(
            static_cast<uint8_t>(world.Enemies().Type(i)) |
            (world.Revealed(i) ? kEnemyRevealedBit : 0)));
    };
    size_t written = 0;
    if (world.SwitchState().snapshotPick == SnapshotPickKind::Relevance) {
        // 상위 64를 고르고(선형) 그 64만 정렬한다(k log k). 전수 정렬은 같은 답을
        // 내지만 n log n 을 치른다 — 그 차이가 이 팔의 값이다.
        //
        // 고른 뒤 다시 정렬하는 이유는 비용이 아니라 **정합성**이다. quickselect 가
        // 남기는 앞 k 개의 내부 순서는 입력 배열 순서에 좌우되므로, 정렬하지 않으면
        // 행 순서를 섞은 같은 월드가 다른 바이트를 낸다. 정렬하면 패킷이 집합의
        // 함수가 된다.
        std::vector<EnemyPick> picks;
        CollectEnemyPicks(world, picks);
        algo::SelectTopK(picks.data(), picks.size(), enemyCount, MoreRelevant);
        if (picks.size() > enemyCount) picks.resize(enemyCount);
        std::sort(picks.begin(), picks.end(), MoreRelevant);
        for (const EnemyPick& pick : picks) {
            writeEnemy(pick.row);
            ++written;
        }
    } else {
        for (size_t i = 0; i < enemyRows && written < enemyCount; ++i) {
            if (!world.Enemies().Alive(i)) continue;
            writeEnemy(i);
            ++written;
        }
    }
    written = 0;
    world.Projectiles().ForEach([&](const Projectile& projectile) {
        if (written >= projectileCount) return;
        writer.U32(projectile.id);
        writer.U16(EncodePosition(projectile.pos.x));
        writer.U16(EncodePosition(projectile.pos.y));
        ++written;
    });
    net::Bytes bytes = writer.Finish();
    // 이 비교는 방어선이 아니라 마지막 자물쇠다. 실제 상한 보장은 위의
    // `static_assert(kSnapshotMaxBytes <= kMaxPacketBytes)` 가 컴파일 시간에 한다 —
    // 세 컬렉션이 전부 상한이면 1198바이트이므로 이 분기의 else 는 구성상 도달할 수
    // 없다(감사 #31: 변이 테스트에서 이 줄을 지워도 어떤 테스트도 실패하지 않았다.
    // 그것은 테스트의 구멍이 아니라 도달 불가라는 사실의 반영이다). 상한을 올리면
    // static_assert 가 먼저 빌드를 멈추므로 런타임에 여기까지 오지 않는다.
    return bytes.size() <= kMaxPacketBytes ? bytes : net::Bytes{};
}

inline bool ReadSnapshot(const net::Bytes& bytes, Snapshot& snapshot,
                         net::PacketHeader* framing = nullptr) {
    Reader reader(bytes);
    if (!reader.Header(PacketType::Snapshot, framing)) return false;

    // 차분 패킷은 기준선 없이 해석할 수 없다. 여기서 통과시키면 뒤따르는 varint
    // 스트림이 스냅샷 필드로 읽혀 쓰레기 상태가 그려진다 — 거부가 유일하게
    // 정직한 답이고, 기준선을 아는 ReadSnapshotPacket 이 그 답을 낸다.
    if (reader.U8() != kSnapshotFull) return false;
    Snapshot decoded;
    decoded.tick = reader.U32();
    decoded.acknowledgedCommand = reader.U32();
    decoded.avatarFrame = reader.U32();
    decoded.avatarChecksum = reader.U32();
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
        const uint8_t packedType = reader.U8();
        enemy.type = static_cast<EnemyType>(packedType & kEnemyTypeMask);
        enemy.revealed = (packedType & kEnemyRevealedBit) != 0;
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

// ── 델타 스냅샷 ────────────────────────────────────────────────────
// 위의 WriteSnapshot/ReadSnapshot 은 그대로 전체 인코더·디코더로 남는다. 아래
// 둘은 그 위에 얹는 얇은 층이다: 호스트가 **클라이언트가 확실히 갖고 있는**
// 스냅샷을 기준선으로 골라 차분만 싣고, 클라이언트가 보관분으로 복원한다.
//
// 차분의 기준선은 net::BaselineStore 가 고른다(net/packet.hpp). 그 보관소에서
// 밀려난 seq 는 Baseline() 이 nullptr 을 돌려주고, 그것이 곧 "전체를 보내라"는
// 신호다 — 복원 불가 상태를 따로 감지할 필요가 없다.

// 호스트: 기준선이 있으면 차분, 없으면 전체. 반환 바이트는 어느 쪽이든 그대로
// 소켓에 실을 수 있는 완성 패킷이다.
inline net::Bytes WriteSnapshotPacket(const GameWorld& world, uint32_t tick,
                                      uint32_t acknowledgedCommand,
                                      const net::PacketHeader& framing,
                                      net::BaselineStore& baselines,
                                      uint16_t seq,
                                      bool* truncatedOut = nullptr,
                                      uint32_t avatarFrame = 0,
                                      uint32_t avatarChecksum = 0) {
    net::Bytes full = WriteSnapshot(world, tick, acknowledgedCommand, truncatedOut, framing,
                                    avatarFrame, avatarChecksum);
    if (full.empty()) return full;

    // snippet-begin: delta-write
    uint16_t baselineSeq = 0;
    const net::BaselineStore::Entry* baseline = baselines.Baseline(&baselineSeq);
    net::Bytes packet;
    if (baseline) {
        const net::Bytes delta = net::EncodeDelta(baseline->bytes, full);
        Writer writer(PacketType::Snapshot, framing);
        writer.U8(kSnapshotDelta);
        writer.U16(baselineSeq);
        writer.Raw(std::string_view(reinterpret_cast<const char*>(delta.data()), delta.size()));
        packet = writer.Finish();
        // 차분이 원본보다 큰 경우가 이론상 가능하다(바뀐 구간이 흩어져 있으면
        // 세그먼트 머리값이 절약분을 먹는다). 예산을 넘으면 전체로 되돌린다.
        if (packet.size() > kMaxPacketBytes) packet.clear();
    }
    if (packet.empty()) packet = full;
    // 보관하는 것은 언제나 **전체** 바이트다. 차분을 보관하면 다음 차분이
    // 차분의 차분이 되어 한 장만 잃어도 그 뒤가 전부 복원 불가가 된다.
    baselines.Sent(seq, tick, std::move(full));
    return packet;
    // snippet-end: delta-write
}

// 클라이언트: 플래그를 보고 필요하면 보관분으로 복원한 뒤 전체 디코더에 먹인다.
// restoredOut 은 복원된 **전체 패킷 바이트**다. 호출자는 그것을 자기 보관소에
// framing.seq 로 넣어야 다음 차분의 기준선이 된다.
// baselineMissOut 은 "스냅샷이긴 한데 기준선이 없어서 못 읽었다"를 "스냅샷이
// 아니다"와 가른다. 둘을 같은 false 로 묶으면 지형 패킷 한 장이 기준선 손실로
// 세어지고, 그 카운터로는 델타 배선의 건강을 물을 수 없다.
inline bool ReadSnapshotPacket(const net::Bytes& bytes,
                               const net::SequenceBuffer<net::Bytes>& held,
                               Snapshot& snapshot,
                               net::PacketHeader* framing = nullptr,
                               net::Bytes* restoredOut = nullptr,
                               bool* baselineMissOut = nullptr) {
    if (baselineMissOut) *baselineMissOut = false;
    net::PacketHeader decodedFraming;
    Reader reader(bytes);
    if (!reader.Header(PacketType::Snapshot, &decodedFraming)) return false;
    const uint8_t kind = reader.U8();
    if (kind == kSnapshotFull) {
        if (!ReadSnapshot(bytes, snapshot, framing)) return false;
        if (restoredOut) *restoredOut = bytes;
        return true;
    }
    if (kind != kSnapshotDelta) return false;
    const uint16_t baselineSeq = reader.U16();
    const net::Bytes* baseline = held.Find(baselineSeq);
    if (!baseline) {                          // 기준선이 없다 — 이 장은 버린다
        if (baselineMissOut) *baselineMissOut = true;
        return false;
    }
    const size_t offset = 16 + 1 + 2;
    if (bytes.size() < offset) return false;
    const net::Bytes delta(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
    net::Bytes restored;
    if (!net::ApplyDelta(*baseline, delta, restored)) return false;
    if (!ReadSnapshot(restored, snapshot, framing)) return false;
    if (restoredOut) *restoredOut = std::move(restored);
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

// 같은 보간을 id → 인덱스 맵 위에서. Lerp 는 b 의 개체 하나마다 a 를 전수
// 스캔하므로 개체 n 에서 O(n^2) 이다 — 적 64에서는 보이지 않고 2048에서는 프레임
// 예산을 넘는다. 두 팔은 **같은 스냅샷**을 내야 하고(그것이 판정 조건이다),
// 다른 것은 대응을 찾는 비용뿐이다.
//
// 맵을 멤버로 들고 있는 이유: 매 프레임 새로 만들면 그 할당이 절약분을 먹는다.
// OpenAddressingHashMap::Clear 는 용량을 유지하므로 두 번째 프레임부터 할당이 0이다.
// snippet-begin: snapshot-interpolator
class SnapshotInterpolator {
public:
    void SetMode(IdMapKind mode) { mode_ = mode; }
    IdMapKind Mode() const { return mode_; }

    Snapshot Blend(const Snapshot& a, const Snapshot& b, float t) {
        if (mode_ == IdMapKind::Linear) return Lerp(a, b, t);

        enemyIndex_.Clear();
        for (uint32_t i = 0; i < static_cast<uint32_t>(a.enemies.size()); ++i)
            enemyIndex_.Insert(a.enemies[i].id, i);
        projectileIndex_.Clear();
        for (uint32_t i = 0; i < static_cast<uint32_t>(a.projectiles.size()); ++i)
            projectileIndex_.Insert(a.projectiles[i].id, i);

        Snapshot out = b;
        out.coreHp = a.coreHp + (b.coreHp - a.coreHp) * t;
        for (EnemyState& enemy : out.enemies) {
            const uint32_t* row = enemyIndex_.Find(enemy.id);
            if (!row) continue;
            const EnemyState& previous = a.enemies[*row];
            enemy.pos = previous.pos + (enemy.pos - previous.pos) * t;
            enemy.hp = previous.hp + (enemy.hp - previous.hp) * t;
        }
        for (ProjectileState& projectile : out.projectiles) {
            const uint32_t* row = projectileIndex_.Find(projectile.id);
            if (!row) continue;
            const ProjectileState& previous = a.projectiles[*row];
            projectile.pos = previous.pos + (projectile.pos - previous.pos) * t;
        }
        return out;
    }

private:
    OpenAddressingHashMap<uint32_t, uint32_t> enemyIndex_;
    OpenAddressingHashMap<uint32_t, uint32_t> projectileIndex_;
    IdMapKind mode_ = IdMapKind::Linear;
};
// snippet-end: snapshot-interpolator

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
        case CommandType::Blast:
            return world.Blast(position) > 0;
    }
    return false;
}

}  // namespace wire
}  // namespace game
}  // namespace mega
