// entity_store — 적과 발사체의 **저장**. 규칙이 아니라 배치를 소유한다.
//
// 게임 규칙(game/game.hpp)은 "이 적을 얼마나 전진시키는가"를 안다. 그 적이
// 메모리에 어떻게 놓이는가는 다른 질문이고, 답이 하나가 아니다. 그래서 여기서
// 저장만 떼어 내고 두 답을 나란히 둔다.
//
//   EnemyStore       AoS(std::vector<Enemy>) ↔ SoA(필드별 열 배열)
//                    ↔ ECS(ecs/world.h 의 archetype 컬럼)
//   ProjectileStore  std::vector + 압축 ↔ 고정 블록 풀 + intrusive 목록
//
// 세 저장은 **같은 결과**를 내야 한다. 그래서 산술은 이 헤더의 자유 함수
// (AdvanceEnemyOnPath · AdvanceEnemyTowardCore)에만 있고 세 팔이 같은 함수를
// 부른다. 저장이 다르다는 이유로 수식이 두 벌이 되면 그 순간 A/B 는 비교가
// 아니라 두 게임이 된다.
//
// 세 번째 팔이 생긴 이유는 **선택적 컴포넌트**다. 보스만 리그(EnemyRig)를 갖는데,
// 고정 열 SoA 는 그 열을 적 상한만큼 잡는다 — large 규모에서 12바이트 x 2048행
// = 24KB 가 한두 행을 위해 놓인다. archetype 저장은 그 조합을 가진 엔티티만
// 컬럼을 갖는다. 대신 컴포넌트가 붙고 떨어질 때마다 행이 archetype 사이를
// 이동하고(memcpy), 행 번호라는 개념이 저장 안에 남지 않는다 — 그 둘이 값이다.
//
// 순서 규칙: 어느 저장이든 살아 있는 행의 상대 순서는 **삽입 순서**다. 조준
// 동점(같은 거리)의 승자와 스냅샷에 실리는 순서가 그 순서로 결정되므로, 순서를
// 흐트러뜨리는 압축(swap-remove)은 쓰지 않는다.

#pragma once

#include "containers/hash_map.h"
#include "containers/intrusive_list.h"
#include "containers/soa_vector.h"
#include "ecs/world.h"
#include "game/switches.hpp"
#include "math/vec.hpp"
#include "memory/pool.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

namespace mega {
namespace game {

// --- 적 종류와 스탯 배수 ---
// Stalker 는 Boss 뒤에 붙였다. 앞에 끼워 넣으면 Normal..Boss 의 수가 밀려
// 저장된 리플레이·와이어 바이트의 뜻이 통째로 바뀐다 — 열거 값이 프로토콜을
// 건너가는 순간 그 수는 이름이 아니라 **약속**이 된다.
enum class EnemyType : uint8_t { Normal, Fast, Tank, Swarm, Boss, Stalker, Count };
struct EnemyMod { float hpMul, speedMul, damageMul; int rewardMul; };
inline EnemyMod EnemyModFor(EnemyType t) {
    static constexpr EnemyMod mods[6] = {
        {1.0f, 1.0f, 1.0f, 1},
        {0.6f, 1.8f, 1.0f, 1},    // Fast: 빠르고 약함
        {3.0f, 0.6f, 2.0f, 3},    // Tank: 튼튼·느림·강타·고보상
        {0.4f, 1.2f, 0.5f, 1},    // Swarm: 다수 약체
        {12.0f, 0.4f, 5.0f, 10},  // Boss: 거대 체력·매우 느림·치명타·대량 보상
        {0.7f, 1.5f, 1.0f, 2},    // Stalker: 은신·빠름. 조준되지 않는 것이 방어력이다
    };
    size_t index = static_cast<size_t>(t);
    return index < 6 ? mods[index] : EnemyMod{1, 1, 1, 1};
}

// alive 가 bool 이 아니라 uint8_t 인 이유: SoA 열은 std::vector<bool> 이 될 수
// 없다(비트 프록시라 원소 참조를 만들 수 없고, 두 저장이 같은 전진 함수를 공유할
// 수 없게 된다). pathIndex 가 size_t 가 아닌 이유도 같은 층위다 — 열 하나가
// 8바이트면 2048행에서 16KB 가 되고, 그 열은 전진 루프가 매 틱 전수로 읽는다.
struct Enemy {
    math::Vec2 pos{0, 0};
    float      hp = 10, maxHp = 10, speed = 20;
    int        reward = 5;
    float      damage = 10;     // 코어에 닿을 때
    uint8_t    alive = 1;
    EnemyType  type = EnemyType::Normal;
    uint32_t   id = 0;
    uint32_t   pathIndex = 0;
    // 리그 상태 — **보스만** 쓴다. 다른 네 종류에서는 영원히 초기값이고,
    // AoS·SoA 팔은 그 초기값을 행마다(열마다) 그대로 들고 다닌다.
    //   plants     지금까지 발을 디딘 횟수. 걸음 위상의 바닥값과 같다.
    //   lastStomp  마지막으로 디딘 발의 월드 좌표(스톰프 원점).
    math::Vec2 lastStomp{0, 0};
    uint32_t   plants = 0;
};

// ── ECS 팔의 컴포넌트 ───────────────────────────────────────────────
// 셋으로 나눈 기준은 "누가 읽는가"다. 전진 루프는 EnemyCore 만 읽고, 피해
// 처리는 EnemyVitals 를, 스톰프는 EnemyRig 를 읽는다. row 가 EnemyCore 안에
// 있는 이유는 archetype 저장이 **행 번호를 표현하지 못하기 때문**이다 —
// 컴포넌트가 붙고 떨어질 때마다 행이 다른 archetype 으로 옮겨 가면서 그
// archetype 안의 자리가 바뀐다. 삽입 순서라는 공개 계약은 EnemyStore 가
// 따로 든 order_ 배열이 소유하고, row 는 그 배열로 되돌아가는 표다.
// snippet-begin: ecs-components
struct EnemyCore {
    math::Vec2 pos{0, 0};
    float      speed = 20;
    float      damage = 10;
    uint32_t   pathIndex = 0;
    uint32_t   id = 0;
    uint32_t   row = 0;
    EnemyType  type = EnemyType::Normal;
    uint8_t    alive = 1;
};
struct EnemyVitals {
    float hp = 10, maxHp = 10;
    int   reward = 5;
};
struct EnemyRig {
    math::Vec2 lastStomp{0, 0};
    uint32_t   plants = 0;
};
// snippet-end: ecs-components

struct Projectile {
    math::Vec2 pos{0, 0}, target{0, 0};
    float      speed = 200, damage = 5, splash = 6;
    // 명중한 적을 재우는 시간. 0 이면 아무것도 하지 않는다 — 빙결 타워가
    // 생기기 전의 모든 발사체가 그 값이다. 발사체에 **종류**를 두지 않은 이유는
    // 이 하나뿐이기 때문이다. 종류가 둘이 되는 날 열거형이 이 자리에 온다.
    float      stun = 0;
    uint8_t    alive = 1;
    uint32_t   id = 0;
    uint32_t   targetId = 0;
};

// ── 전진 산술: 두 저장이 공유하는 유일한 사본 ──────────────────────
// 경로를 따라 dt 만큼 나아간다. 경로 끝을 지나면 코어에 damage 를 남기고 죽는다.
// 코어 체력을 직접 깎지 않고 damageOut 으로 돌려주는 것은 병렬화 때문이다 —
// 워커가 공유 스칼라를 갱신하면 감산 순서가 실행마다 달라져 부동소수 결과가
// 갈린다. 개체별로 받아 두고 인덱스 오름차순으로 합치면 직렬과 비트 동일하다.
// snippet-begin: advance-math
inline void AdvanceEnemyOnPath(math::Vec2& pos, uint32_t& pathIndex, uint8_t& alive,
                               float speed, float damage,
                               const math::Vec2* path, size_t pathCount,
                               float dt, float& damageOut) {
    float remaining = speed * dt;
    while (remaining > 0.0f && alive) {
        if (pathIndex >= pathCount) {
            damageOut += damage;
            alive = 0;
            break;
        }
        math::Vec2 d = path[pathIndex] - pos;
        float dist = math::Length(d);
        if (dist <= remaining + 1e-4f) {
            pos = path[pathIndex++];
            remaining -= dist;
        } else {
            pos += d * (remaining / dist);
            remaining = 0.0f;
        }
    }
}

inline void AdvanceEnemyTowardCore(math::Vec2& pos, uint8_t& alive, float speed,
                                   float damage, const math::Vec2& corePos, float dt,
                                   float& damageOut) {
    math::Vec2 d = corePos - pos;
    float dist = math::Length(d);
    if (dist < 6.0f) {                     // 코어 도달
        damageOut += damage;
        alive = 0;
        return;
    }
    pos += d * (speed * dt / dist);        // 코어로 직진
}
// snippet-end: advance-math

// ── 적 저장 ────────────────────────────────────────────────────────
class EnemyStore {
public:
    // 열 순서. 전진 루프가 읽는 것은 0·3·4·7·9 다섯 열뿐이고, hp·maxHp·reward·
    // type 은 캐시 라인에 끌려오지 않는다 — SoA 팔의 존재 이유가 이 문장이다.
    using Columns = SoAVector<math::Vec2,  // 0 pos
                             float,        // 1 hp
                             float,        // 2 maxHp
                             float,        // 3 speed
                             float,        // 4 damage
                             int,          // 5 reward
                             uint32_t,     // 6 id
                             uint32_t,     // 7 pathIndex
                             EnemyType,    // 8 type
                             uint8_t,      // 9 alive
                             math::Vec2,   // 10 lastStomp  ← 보스만 쓴다
                             uint32_t>;    // 11 plants     ← 보스만 쓴다

    void Configure(EntityStoreKind kind, IdMapKind idMap, size_t capacity) {
        kind_ = kind;
        idMap_ = idMap;
        capacity_ = capacity ? capacity : 1;
        Clear();
        if (kind_ == EntityStoreKind::Aos) aos_.reserve(capacity_);
        else if (kind_ == EntityStoreKind::Soa) soa_.Reserve(capacity_);
        else order_.reserve(capacity_);
    }

    EntityStoreKind Kind() const { return kind_; }
    IdMapKind       IdMap() const { return idMap_; }
    size_t          Capacity() const { return capacity_; }

    void Clear() {
        aos_.clear();
        soa_.Clear();
        ecs_ = World();
        order_.clear();
        index_.Clear();
    }

    size_t Size() const {
        if (kind_ == EntityStoreKind::Aos) return aos_.size();
        if (kind_ == EntityStoreKind::Soa) return soa_.Size();
        return order_.size();
    }
    bool Full() const { return Size() >= capacity_; }

    // ECS 팔에서 리그를 갖는 조건. 보스만이다 — 그리고 그 사실이 이 팔의 전부다.
    static bool HasRig(EnemyType type) { return type == EnemyType::Boss; }

    bool Push(const Enemy& e) {
        if (Full()) return false;
        if (kind_ == EntityStoreKind::Aos) {
            aos_.push_back(e);
        } else if (kind_ == EntityStoreKind::Soa) {
            soa_.PushBack(e.pos, e.hp, e.maxHp, e.speed, e.damage, e.reward, e.id,
                          e.pathIndex, e.type, e.alive, e.lastStomp, e.plants);
        } else {
            const EntityId entity = ecs_.Create();
            EnemyCore core;
            core.pos = e.pos;
            core.speed = e.speed;
            core.damage = e.damage;
            core.pathIndex = e.pathIndex;
            core.id = e.id;
            core.row = static_cast<uint32_t>(order_.size());
            core.type = e.type;
            core.alive = e.alive;
            ecs_.Add(entity, core);
            ecs_.Add(entity, EnemyVitals{e.hp, e.maxHp, e.reward});
            if (HasRig(e.type)) ecs_.Add(entity, EnemyRig{e.lastStomp, e.plants});
            order_.push_back(entity);
        }
        if (idMap_ == IdMapKind::Hash)
            index_.Insert(e.id, static_cast<uint32_t>(Size() - 1));
        return true;
    }

    // 행 하나를 값으로 뽑는다. 스냅샷·테스트처럼 전 필드를 한 번에 읽는 자리용.
    Enemy Row(size_t i) const {
        if (kind_ == EntityStoreKind::Aos) return aos_[i];
        Enemy e;
        if (kind_ == EntityStoreKind::Ecs) {
            const EnemyCore& c = Core(i);
            const EnemyVitals& v = Vitals(i);
            e.pos = c.pos; e.speed = c.speed; e.damage = c.damage;
            e.pathIndex = c.pathIndex; e.id = c.id; e.type = c.type; e.alive = c.alive;
            e.hp = v.hp; e.maxHp = v.maxHp; e.reward = v.reward;
            if (const EnemyRig* rig = RigOf(i)) { e.lastStomp = rig->lastStomp; e.plants = rig->plants; }
            return e;
        }
        e.pos = soa_.Column<0>()[i];
        e.hp = soa_.Column<1>()[i];
        e.maxHp = soa_.Column<2>()[i];
        e.speed = soa_.Column<3>()[i];
        e.damage = soa_.Column<4>()[i];
        e.reward = soa_.Column<5>()[i];
        e.id = soa_.Column<6>()[i];
        e.pathIndex = soa_.Column<7>()[i];
        e.type = soa_.Column<8>()[i];
        e.alive = soa_.Column<9>()[i];
        e.lastStomp = soa_.Column<10>()[i];
        e.plants = soa_.Column<11>()[i];
        return e;
    }

    const math::Vec2& Pos(size_t i) const  { if (kind_ == EntityStoreKind::Ecs) return Core(i).pos;       return kind_ == EntityStoreKind::Aos ? aos_[i].pos : soa_.Column<0>()[i]; }
    float Hp(size_t i) const               { if (kind_ == EntityStoreKind::Ecs) return Vitals(i).hp;      return kind_ == EntityStoreKind::Aos ? aos_[i].hp : soa_.Column<1>()[i]; }
    float MaxHp(size_t i) const            { if (kind_ == EntityStoreKind::Ecs) return Vitals(i).maxHp;   return kind_ == EntityStoreKind::Aos ? aos_[i].maxHp : soa_.Column<2>()[i]; }
    float Speed(size_t i) const            { if (kind_ == EntityStoreKind::Ecs) return Core(i).speed;     return kind_ == EntityStoreKind::Aos ? aos_[i].speed : soa_.Column<3>()[i]; }
    float Damage(size_t i) const           { if (kind_ == EntityStoreKind::Ecs) return Core(i).damage;    return kind_ == EntityStoreKind::Aos ? aos_[i].damage : soa_.Column<4>()[i]; }
    int   Reward(size_t i) const           { if (kind_ == EntityStoreKind::Ecs) return Vitals(i).reward;  return kind_ == EntityStoreKind::Aos ? aos_[i].reward : soa_.Column<5>()[i]; }
    uint32_t Id(size_t i) const            { if (kind_ == EntityStoreKind::Ecs) return Core(i).id;        return kind_ == EntityStoreKind::Aos ? aos_[i].id : soa_.Column<6>()[i]; }
    uint32_t PathIndex(size_t i) const     { if (kind_ == EntityStoreKind::Ecs) return Core(i).pathIndex; return kind_ == EntityStoreKind::Aos ? aos_[i].pathIndex : soa_.Column<7>()[i]; }
    EnemyType Type(size_t i) const         { if (kind_ == EntityStoreKind::Ecs) return Core(i).type;      return kind_ == EntityStoreKind::Aos ? aos_[i].type : soa_.Column<8>()[i]; }
    bool  Alive(size_t i) const            { if (kind_ == EntityStoreKind::Ecs) return Core(i).alive != 0; return (kind_ == EntityStoreKind::Aos ? aos_[i].alive : soa_.Column<9>()[i]) != 0; }

    // ── 리그 열 ────────────────────────────────────────────────────
    // AoS·SoA 는 행마다 자리를 갖고, ECS 는 그 컴포넌트를 가진 archetype 만
    // 갖는다. 리그가 없는 행을 물으면 세 팔 모두 초기값을 낸다.
    uint32_t Plants(size_t i) const {
        if (kind_ == EntityStoreKind::Ecs) { const EnemyRig* r = RigOf(i); return r ? r->plants : 0u; }
        return kind_ == EntityStoreKind::Aos ? aos_[i].plants : soa_.Column<11>()[i];
    }
    math::Vec2 LastStomp(size_t i) const {
        if (kind_ == EntityStoreKind::Ecs) { const EnemyRig* r = RigOf(i); return r ? r->lastStomp : math::Vec2{0, 0}; }
        return kind_ == EntityStoreKind::Aos ? aos_[i].lastStomp : soa_.Column<10>()[i];
    }
    void SetStomp(size_t i, uint32_t plants, const math::Vec2& at) {
        if (kind_ == EntityStoreKind::Aos) { aos_[i].plants = plants; aos_[i].lastStomp = at; return; }
        if (kind_ == EntityStoreKind::Soa) { soa_.Column<11>()[i] = plants; soa_.Column<10>()[i] = at; return; }
        if (EnemyRig* r = RigOf(i)) { r->plants = plants; r->lastStomp = at; }
    }

    // 격자 항법과 조향이 위치를 직접 쓴다. 폴리라인 팔에서는 전진 함수가 열을
    // 참조로 받아 그 자리에서 고쳤지만, 격자 팔의 이동은 브레인 결정과 벽
    // 미끄러짐을 거쳐 **한 번에** 결정되므로 쓰기 지점이 하나여야 한다.
    void SetPos(size_t i, const math::Vec2& v) {
        if (kind_ == EntityStoreKind::Aos) aos_[i].pos = v;
        else if (kind_ == EntityStoreKind::Soa) soa_.Column<0>()[i] = v;
        else Core(i).pos = v;
    }

    void SetHp(size_t i, float v) {
        if (kind_ == EntityStoreKind::Aos) aos_[i].hp = v;
        else if (kind_ == EntityStoreKind::Soa) soa_.Column<1>()[i] = v;
        else Vitals(i).hp = v;
    }
    void Kill(size_t i) {
        if (kind_ == EntityStoreKind::Aos) aos_[i].alive = 0;
        else if (kind_ == EntityStoreKind::Soa) soa_.Column<9>()[i] = 0;
        else Core(i).alive = 0;
    }
    // 경로가 다시 계산되면 살아 있는 적의 진행 위치도 새 배열 기준으로 옮겨야
    // 한다. 그 재배치가 부르는 유일한 쓰기 지점이다.
    void SetPathIndex(size_t i, uint32_t v) {
        if (kind_ == EntityStoreKind::Aos) aos_[i].pathIndex = v;
        else if (kind_ == EntityStoreKind::Soa) soa_.Column<7>()[i] = v;
        else Core(i).pathIndex = v;
    }

    int AliveCount() const {
        int n = 0;
        const size_t count = Size();
        for (size_t i = 0; i < count; ++i) if (Alive(i)) ++n;
        return n;
    }

    // 전진 루프가 쓰는 쓰기 가능 열. AoS 팔에는 열이 없으므로 nullptr 이고,
    // 호출자는 그때 행 단위 경로를 탄다.
    // ECS 팔의 전진 경로. 컬럼을 하나씩 넘겨받는 대신 **조합별 청크**를 받는다.
    // 순회는 조합 마스크 오름차순으로 고정된 래퍼를 통해서만 한다 —
    // archetype 배열은 어느 조합이 먼저 나타났는가 순으로 늘어나기 때문이다.
    // snippet-begin: ecs-chunks
    template <class Fn>
    void ForEachCoreChunk(Fn&& fn) {
        ecs_.ForEachChunkOrdered<EnemyCore>(
            [&fn](size_t count, EnemyCore* core) { fn(count, core); });
    }
    // snippet-end: ecs-chunks

    // 리그를 가진 조합만 훑는다. 보스가 하나도 없으면 청크가 0개라 이 순회는
    // 아무 행도 만지지 않는다 — 전수 스캔 팔이 매 틱 2048행을 훑어 type 을
    // 비교하는 것과 갈리는 지점이다.
    template <class Fn>
    void ForEachRigChunk(Fn&& fn) const {
        ecs_.ForEachChunkOrdered<EnemyCore, EnemyRig>(
            [&fn](size_t count, EnemyCore* core, EnemyRig* rig) { fn(count, core, rig); });
    }

    // 리그를 가진 행이 몇인가. 세 팔의 비용이 갈리는 가장 짧은 질문이다 —
    // ECS 는 조합 목록(수개)만 보고 답하고, 나머지 둘은 행을 전수로 훑는다.
    // snippet-begin: rig-rows
    size_t RigRows() const {
        if (kind_ == EntityStoreKind::Ecs) {
            size_t rows = 0;
            ForEachRigChunk([&rows](size_t count, EnemyCore*, EnemyRig*) { rows += count; });
            return rows;
        }
        size_t rows = 0;
        const size_t count = Size();
        for (size_t i = 0; i < count; ++i) if (HasRig(Type(i))) ++rows;
        return rows;
    }
    // snippet-end: rig-rows

    size_t ArchetypeCount() const { return ecs_.ArchetypeCount(); }
    // 리그 열이 차지하는 바이트. SoA 는 상한만큼, ECS 는 실제 보스 수만큼.
    size_t RigBytes() const {
        if (kind_ == EntityStoreKind::Soa) return capacity_ * (sizeof(math::Vec2) + sizeof(uint32_t));
        if (kind_ == EntityStoreKind::Aos) return aos_.capacity() * (sizeof(math::Vec2) + sizeof(uint32_t));
        size_t rigs = 0;
        for (size_t i = 0; i < order_.size(); ++i) if (RigOf(i)) ++rigs;
        return rigs * sizeof(EnemyRig);
    }

    math::Vec2* PosColumn()      { return kind_ == EntityStoreKind::Soa ? soa_.Data<0>() : nullptr; }
    const float* SpeedColumn() const { return kind_ == EntityStoreKind::Soa ? soa_.Data<3>() : nullptr; }
    const float* DamageColumn() const { return kind_ == EntityStoreKind::Soa ? soa_.Data<4>() : nullptr; }
    uint32_t* PathIndexColumn()  { return kind_ == EntityStoreKind::Soa ? soa_.Data<7>() : nullptr; }
    uint8_t*  AliveColumn()      { return kind_ == EntityStoreKind::Soa ? soa_.Data<9>() : nullptr; }
    Enemy*    RowArray()         { return kind_ == EntityStoreKind::Aos ? aos_.data() : nullptr; }

    // id → 행. linear 팔은 전수 스캔, hash 팔은 개방 주소법 조회. 둘은 같은
    // 행 번호를 돌려주어야 한다(id 는 유일하다).
    // snippet-begin: find-row
    int FindRow(uint32_t id) const {
        if (idMap_ == IdMapKind::Hash) {
            const uint32_t* row = index_.Find(id);
            if (!row) return -1;
            return static_cast<int>(*row);
        }
        const size_t count = Size();
        for (size_t i = 0; i < count; ++i)
            if (Id(i) == id) return static_cast<int>(i);
        return -1;
    }
    // snippet-end: find-row

    // 죽은 행을 걷어내되 **순서를 보존**한다. swap-remove 는 O(1) 이지만 남은
    // 행의 순서를 흐트러뜨려 AoS 팔(std::remove_if, 순서 보존)과 다른 세계를
    // 만든다. 두 팔이 같은 결과를 내야 한다는 요구가 여기서는 O(1) 보다 세다.
    // snippet-begin: compact-dead
    void CompactDead() {
        if (kind_ == EntityStoreKind::Ecs) {
            // 저장은 순서를 모른다. 순서를 아는 것은 order_ 뿐이고, 압축은
            // 그 배열을 앞으로 당기면서 살아남은 행의 row 를 다시 매기는 일이다.
            // 엔티티를 지우는 것은 archetype 안의 swap-remove 라 남은 엔티티의
            // 자리는 흐트러지지만, order_ 가 든 EntityId 는 그대로 유효하다.
            size_t write = 0;
            for (size_t read = 0; read < order_.size(); ++read) {
                const EntityId entity = order_[read];
                EnemyCore* core = ecs_.Get<EnemyCore>(entity);
                if (!core || !core->alive) { ecs_.Destroy(entity); continue; }
                order_[write] = entity;
                core->row = static_cast<uint32_t>(write);
                ++write;
            }
            order_.resize(write);
            RebuildIndex();
            return;
        }
        if (kind_ == EntityStoreKind::Aos) {
            aos_.erase(std::remove_if(aos_.begin(), aos_.end(),
                                      [](const Enemy& e) { return !e.alive; }),
                       aos_.end());
        } else {
            const std::vector<uint8_t>& alive = soa_.Column<9>();
            size_t write = 0;
            for (size_t read = 0; read < alive.size(); ++read) {
                if (!alive[read]) continue;
                if (write != read)
                    soa_.ForEachColumn([&](auto& column) { column[write] = column[read]; });
                ++write;
            }
            soa_.ForEachColumn([&](auto& column) { column.resize(write); });
        }
        RebuildIndex();
    }
    // snippet-end: compact-dead

private:
    void RebuildIndex() {
        if (idMap_ != IdMapKind::Hash) return;
        index_.Clear();
        const size_t count = Size();
        for (size_t i = 0; i < count; ++i) index_.Insert(Id(i), static_cast<uint32_t>(i));
    }

    // 행 번호 → 컴포넌트. const 접근자도 같은 자리를 쓰므로 저장은 mutable 이다.
    EnemyCore&   Core(size_t i) const   { return *ecs_.Get<EnemyCore>(order_[i]); }
    EnemyVitals& Vitals(size_t i) const { return *ecs_.Get<EnemyVitals>(order_[i]); }
    EnemyRig*    RigOf(size_t i) const  { return ecs_.Get<EnemyRig>(order_[i]); }

    std::vector<Enemy>                     aos_;
    Columns                                soa_;
    mutable World                          ecs_;
    std::vector<EntityId>                  order_;   // 행 번호 → 엔티티(삽입 순서)
    OpenAddressingHashMap<uint32_t, uint32_t> index_;
    size_t                                 capacity_ = 64;
    EntityStoreKind                        kind_ = EntityStoreKind::Aos;
    IdMapKind                              idMap_ = IdMapKind::Linear;
};

// ── 발사체 저장 ────────────────────────────────────────────────────
// 풀 팔이 존재하는 이유는 속도 이전에 **주소 안정성**이다. 스텝 한 번 안에서
// 발사체가 죽는데(명중) 그 뒤로도 같은 스텝이 그 객체를 읽는다. 벡터 팔은
// 스텝이 끝난 뒤에 한 번 압축하므로 지금은 안전하지만, 그 안전은 "스텝 중에
// push_back 이 없다"는 우연에 기대고 있다. 풀 팔에서는 우연이 아니라 구조다 —
// 블록 주소는 절대 움직이지 않고, 죽은 블록은 live 목록에서 빠져 deferred-free
// 목록으로 옮겨 갔다가 **스텝 경계에서만** 실제로 반납된다.
class ProjectileStore {
public:
    struct Node {
        Projectile p;
        ListHook   liveHook;
        ListHook   deadHook;
    };

    void Configure(BodyAllocKind kind, size_t capacity) {
        Clear();
        kind_ = kind;
        capacity_ = capacity ? capacity : 1;
        if (kind_ == BodyAllocKind::Pool) pool_.Init(sizeof(Node), capacity_);
        else vec_.reserve(capacity_);
    }

    BodyAllocKind Kind() const { return kind_; }
    size_t Capacity() const { return capacity_; }

    void Clear() {
        vec_.clear();
        while (Node* n = live_.Front()) { live_.Remove(n); pool_.Release(n); }
        while (Node* n = dead_.Front()) { dead_.Remove(n); pool_.Release(n); }
        liveCount_ = 0;
    }

    size_t Size() const { return kind_ == BodyAllocKind::Pool ? liveCount_ : vec_.size(); }

    Projectile* Push(const Projectile& p) {
        if (kind_ == BodyAllocKind::Vector) {
            if (vec_.size() >= capacity_) return nullptr;
            vec_.push_back(p);
            return &vec_.back();
        }
        if (liveCount_ >= capacity_) return nullptr;
        void* raw = pool_.Acquire();
        if (!raw) return nullptr;
        Node* node = new (raw) Node{};
        node->p = p;
        live_.PushBack(node);
        ++liveCount_;
        return &node->p;
    }

    // 삽입 순으로 살아 있는 발사체를 훑는다. fn(Projectile&).
    template <class Fn>
    void ForEach(Fn&& fn) {
        if (kind_ == BodyAllocKind::Vector) {
            for (Projectile& p : vec_) if (p.alive) fn(p);
            return;
        }
        for (Node* n = live_.First(); n; ) {
            Node* next = live_.Next(n);   // fn 이 n 을 죽여 목록에서 뺄 수 있다
            if (n->p.alive) fn(n->p);
            n = next;
        }
    }
    template <class Fn>
    void ForEach(Fn&& fn) const {
        const_cast<ProjectileStore*>(this)->ForEach(
            [&fn](Projectile& p) { fn(const_cast<const Projectile&>(p)); });
    }

    // 죽인다. 풀 팔에서는 이 순간 live 에서 빠지지만 **주소는 유효**하다 —
    // 호출자가 곧바로 뒤이어 p.target 을 읽는다.
    // snippet-begin: deferred-kill
    void Kill(Projectile& p) {
        p.alive = 0;
        if (kind_ == BodyAllocKind::Vector) return;
        Node* node = NodeOf(p);
        live_.Remove(node);
        dead_.PushBack(node);
        --liveCount_;
    }

    // 스텝 경계. 여기서만 메모리가 실제로 돌아간다.
    void EndStep() {
        if (kind_ == BodyAllocKind::Vector) {
            vec_.erase(std::remove_if(vec_.begin(), vec_.end(),
                                      [](const Projectile& p) { return !p.alive; }),
                       vec_.end());
            return;
        }
        while (Node* node = dead_.Front()) {
            dead_.Remove(node);
            pool_.Release(node);
        }
    }
    // snippet-end: deferred-kill

    size_t DeferredCount() const { return dead_.Size(); }
    size_t PoolLive() const { return pool_.Live(); }
    size_t PoolPeak() const { return pool_.Peak(); }
    bool   PoolOwns(const void* p) const { return pool_.Owns(p); }

private:
    static Node* NodeOf(Projectile& p) {
        // p 는 Node 의 첫 멤버다(표준 레이아웃). 첫 멤버의 주소가 곧 객체 주소다.
        return reinterpret_cast<Node*>(reinterpret_cast<char*>(&p) - offsetof(Node, p));
    }

    std::vector<Projectile>              vec_;
    Pool                                 pool_;
    IntrusiveList<Node, &Node::liveHook> live_;
    IntrusiveList<Node, &Node::deadHook> dead_;
    size_t                               liveCount_ = 0;
    size_t                               capacity_ = 512;
    BodyAllocKind                        kind_ = BodyAllocKind::Vector;
};

}  // namespace game
}  // namespace mega
