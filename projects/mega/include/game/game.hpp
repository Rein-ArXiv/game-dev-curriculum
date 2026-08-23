// game — 싱글플레이 디펜스의 게임 로직. 시스템 단위로 분해한 한 스텝.
//
// 누적된 엔진 코어 위에서 디펜스의 핵심 루프가 돈다: 웨이브 스폰 → 적 진격 →
// 타워 조준·사격 → 발사체 → 충돌·보상 → 코어 방어 → 승패. 각 단계가 독립 시스템
// 이고 데이터(컴포넌트 벡터)로만 통신한다. 렌더(소프트웨어 래스터)는 이 상태를
// 읽어 그릴 뿐, 게임 로직은 렌더와 무관하게 단독으로 돌고 검증된다.
//
// 이 파일은 **규칙**만 갖는다. 적·발사체가 메모리에 어떻게 놓이는가는
// game/entity_store.hpp 가, 프레임·매치 수명 버퍼를 누가 잡는가는
// game/allocators.hpp 가, 어느 구현을 고를지는 game/switches.hpp 가 소유한다.

#pragma once

#include "game/allocators.hpp"
#include "game/boss_rig.hpp"
#include "game/debris.hpp"
#include "game/defense_map.hpp"
#include "game/enemy_brain.hpp"
#include "game/entity_store.hpp"
#include "game/nav.hpp"
#include "game/switches.hpp"
#include "game/terrain.hpp"
#include "jobs/scheduler.h"
#include "math/spatial/quadtree.hpp"
#include "math/spatial/uniform_grid.hpp"
#include "math/vec.hpp"
#include "physics/narrow_phase/collide.hpp"
#include "profiler/trace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mega {
namespace game {

// --- 타워 종류와 스펙 ---
// Frost 와 Radar 는 Sniper 뒤에 붙였다. 앞에 끼우면 앞 넷의 수가 밀려 저장된
// 리플레이와 Place 명령의 argument 바이트가 다른 타워를 뜻하게 된다.
enum class TowerType : uint8_t { Cannon, Gatling, Splash, Sniper, Frost, Radar, Count };
struct TowerSpec { float range, damage, fireInterval, splash; int cost; };
inline TowerSpec TowerSpecFor(TowerType t) {
    static constexpr std::array<TowerSpec, 6> specs{{
        {90,  25, 0.80f,  4,  70},   // Cannon: 고데미지·느림
        {75,   6, 0.12f,  2,  60},   // Gatling: 저데미지·연사
        {70,  14, 0.60f, 16,  90},   // Splash: 광역
        {180, 40, 1.40f,  2, 110},   // Sniper: 장거리·고데미지
        {85,   4, 0.90f,  6,  80},   // Frost: 피해는 명목뿐, 값은 재우는 것
        {200,  0, 1.00f,  0,  45},   // Radar: 쏘지 않는다. 은신을 드러낸다
    }};
    size_t index = static_cast<size_t>(t);
    return index < specs.size() ? specs[index] : TowerSpec{80, 10, 0.5f, 4, 60};
}
// 쏘는 타워인가. 레이더의 일은 조준이 아니라 인지다 — 조준 루프에 넣어 두면
// 데미지 0 짜리 발사체를 매 초 만들어 발사체 상한을 갉아먹는다.
inline bool TowerFires(TowerType t) { return t != TowerType::Radar; }
// 명중한 적을 재우는가. 빙결 타워 하나뿐이다.
inline float TowerStun(TowerType t) { return t == TowerType::Frost ? kFrostStun : 0.0f; }

// --- 타워 타게팅 모드 ---
// 기반 타입을 고정한다. 이 값은 와이어를 건너오고(스냅샷의 targetMode, Target
// 명령의 argument), ReadSnapshot 은 신뢰할 수 없는 바이트를 이 타입으로 캐스트한
// 뒤에 범위를 본다. 기반 타입이 없으면 열거 범위는 {0..3} 이라 200 같은 값의
// 캐스트 결과가 unspecified 가 되고, 그 뒤의 비교는 무엇도 보장하지 못한다.
// 와이어를 건너는 다른 열거형(TowerType·EnemyType·TileType·CommandType·
// PacketType)은 모두 이미 uint8_t 로 고정돼 있다 — 이것만 빠져 있었다.
enum class TargetMode : uint8_t { Nearest, First, Strongest };

struct Tower {
    math::Vec2 pos{0, 0};
    float      range = 80, damage = 5, fireInterval = 0.5f, timer = 0;
    float      splash = 6;
    // 타워도 부서진다. 15편까지 타워를 없애는 유일한 방법은 플레이어의 철거
    // 명령이었다 — 적이 건물을 건드리지 못했기 때문이다. 보스의 스톰프가 그
    // 첫 위협이고, 그래서 체력이라는 축이 여기서 처음 생긴다.
    float      hp = 0, maxHp = 0;
    TowerType  type = TowerType::Cannon;
    int        tier = 1;
    TargetMode targetMode = TargetMode::Nearest;
    int        invested = 0;      // 누적 투자 골드(판매 환불 계산)
    uint32_t   id = 0;
    bool       onVein = false;    // 광맥 칸 위에 섰는가(데미지 배율을 이미 받았다)
    // 레이더의 시야가 향하는 방향. 배치 순간 스폰 쪽으로 고정한다 — 적이 오는
    // 곳을 보는 것이 레이더의 유일하게 말이 되는 자세이고, 매 틱 다시 정하면
    // 그 방향이 적의 위치에 의존해 결정성 논증에 개체 순회 순서가 끼어든다.
    // 이 값은 시뮬 상태가 아니라 배치의 함수라 상태 해시에 넣지 않는다.
    math::Vec2 facing{1, 0};
};

// 갓 지은 타워의 체력과 강화 한 단계가 더하는 양.
inline constexpr float kTowerBaseHp = 180.0f;
inline constexpr float kTowerTierHp = 60.0f;

// 시뮬이 내보내는 소리. 상태가 아니라 **출력**이라 상태 해시에 들어가지 않는다 —
// 들어가면 표시 계층이 시뮬을 바꿀 수 있게 되고, 그 순간 결정성 논증이 무너진다.
enum class SoundKind : uint8_t { Fire, Kill, Blast, Stomp };
struct SoundEvent {
    math::Vec2 at{0, 0};
    SoundKind  kind = SoundKind::Fire;
};

// 광맥 칸의 타워가 받는 데미지 배율. 절차 지형이 심는 유일한 게임플레이 보상이다.
inline constexpr float kVeinDamageBonus = 1.25f;
// 발파 한 번의 골드 비용.
inline constexpr int kBlastCost = 40;

// 사거리 안 적 중 모드에 맞는 대상 인덱스를 고른다(-1 = 없음).
//   Nearest  = 타워에 가장 가까운 적
//   First    = 코어에 가장 가까운 적(가장 위협적)
//   Strongest= 체력이 가장 높은 적
inline int SelectTarget(const math::Vec2& towerPos, const math::Vec2& corePos,
                        const std::vector<Enemy>& enemies, float range, TargetMode mode) {
    int best = -1;
    float bestScore = 0;
    float r2 = range * range;
    for (int i = 0; i < (int)enemies.size(); ++i) {
        if (!enemies[i].alive) continue;
        float d2 = math::LengthSq(enemies[i].pos - towerPos);
        if (d2 > r2) continue;
        float score;
        switch (mode) {
            case TargetMode::Nearest:   score = d2; break;
            case TargetMode::First:
                score = enemies[i].pathIndex > 0
                    ? -static_cast<float>(enemies[i].pathIndex) * 1000000.0f
                    : math::LengthSq(enemies[i].pos - corePos);
                break;
            case TargetMode::Strongest: score = -enemies[i].hp; break;
            default:                    score = d2; break;
        }
        if (best < 0 || score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}

// 사격선 판정. geometry 가 없으면(Classic 맵) 언제나 보인다 — 스위치가 생기기
// 전의 동작이다.
// 은신 규칙이 붙는 자리이기도 하다. `revealed` 는 행 번호로 색인되는 바이트
// 배열이고, 은신 규칙이 꺼져 있으면 nullptr 이라 판정이 통째로 사라진다 —
// 스위치가 생기기 전과 같은 코드 경로다.
struct SightTest {
    // 필드 순서가 계약이다. 기존 호출부가 `SightTest{&geometry, kind}` 로 집합
    // 초기화하므로 새 필드는 **뒤에만** 붙는다.
    const StaticGeometry* geometry = nullptr;
    StaticQueryKind       kind = StaticQueryKind::Naive;
    const uint8_t*        revealed = nullptr;
    size_t                revealedCount = 0;
    bool Visible(const math::Vec2& from, const math::Vec2& to) const {
        return !geometry || !geometry->SegmentBlocked(from, to, kind);
    }
    // 이 행이 조준 후보가 되는가. 기하가 아니라 **인지**의 질문이라 위치를
    // 받지 않는다 — 답은 이미 PerceptionSystem 이 계산해 두었다.
    bool Targetable(size_t row) const {
        return !revealed || row >= revealedCount || revealed[row] != 0;
    }
};

// 조준 점수. **세 색인 팔이 공유하는 유일한 사본이다.** 저장이 둘일 때
// 전진 산술을 한 곳에 모았던 것과 같은 이유다 — 점수가 두 벌이 되는 순간
// A/B 는 비교가 아니라 서로 다른 게임이 된다.
// snippet-begin: target-score
inline float TargetScore(const EnemyStore& enemies, size_t i, const math::Vec2& towerPos,
                         const math::Vec2& corePos, TargetMode mode) {
    switch (mode) {
        case TargetMode::First:
            return enemies.PathIndex(i) > 0
                ? -static_cast<float>(enemies.PathIndex(i)) * 1000000.0f
                : math::LengthSq(enemies.Pos(i) - corePos);
        case TargetMode::Strongest: return -enemies.Hp(i);
        case TargetMode::Nearest:
        default:                    return math::LengthSq(enemies.Pos(i) - towerPos);
    }
}
// snippet-end: target-score

// 같은 규칙을 저장(EnemyStore) 위에서. 위 함수가 이것의 오라클이다 — 두 저장을
// 오가면서도 조준 결과가 흔들리지 않는다는 것을 테스트가 그 대조로 잰다.
// 전수 스캔 팔(target_index=linear)이기도 하다.
// snippet-begin: select-target-store
inline int SelectTargetIn(const math::Vec2& towerPos, const math::Vec2& corePos,
                          const EnemyStore& enemies, float range, TargetMode mode,
                          const SightTest& sight = SightTest{}) {
    int best = -1;
    float bestScore = 0;
    float r2 = range * range;
    const size_t count = enemies.Size();
    for (size_t i = 0; i < count; ++i) {
        if (!enemies.Alive(i)) continue;
        if (!sight.Targetable(i)) continue;      // 은신: 드러나지 않은 적은 후보가 아니다
        float d2 = math::LengthSq(enemies.Pos(i) - towerPos);
        if (d2 > r2) continue;
        const float score = TargetScore(enemies, i, towerPos, corePos, mode);
        if (best >= 0 && score >= bestScore) continue;
        // 가림 판정은 **이길 후보에만** 건다. 먼저 걸러도 결과는 같지만, 사격선
        // 질의는 후보 하나당 BVH 순회 한 번이라 전수로 걸면 색인의 이득을 전부
        // 되돌려준다.
        if (!sight.Visible(towerPos, enemies.Pos(i))) continue;
        bestScore = score;
        best = static_cast<int>(i);
    }
    return best;
}
// snippet-end: select-target-store

// 후보 목록에서 고르는 팔(grid·quadtree). 후보는 **행 번호 오름차순**이어야
// 한다 — 동점의 승자가 "먼저 나온 쪽"이고, 그 순서가 전수 스캔과 같아야 세 팔이
// 같은 답을 낸다.
// snippet-begin: select-target-among
inline int SelectTargetAmong(const math::Vec2& towerPos, const math::Vec2& corePos,
                             const EnemyStore& enemies, float range, TargetMode mode,
                             const uint32_t* candidates, size_t candidateCount,
                             const SightTest& sight = SightTest{}) {
    int best = -1;
    float bestScore = 0;
    const float r2 = range * range;
    for (size_t k = 0; k < candidateCount; ++k) {
        const size_t i = candidates[k];
        if (!enemies.Alive(i)) continue;
        if (!sight.Targetable(i)) continue;
        if (math::LengthSq(enemies.Pos(i) - towerPos) > r2) continue;
        const float score = TargetScore(enemies, i, towerPos, corePos, mode);
        if (best >= 0 && score >= bestScore) continue;
        if (!sight.Visible(towerPos, enemies.Pos(i))) continue;
        bestScore = score;
        best = static_cast<int>(i);
    }
    return best;
}
// snippet-end: select-target-among

// 사거리 질의 색인. 같은 질문에 세 구현이 답하고, 답이 같다는 것을 상태 해시가
// 강제한다. 격자와 쿼드트리는 매 틱 통째로 다시 짓는다 — 적이 매 틱 움직이므로
// 갱신이 재구축보다 싸지 않다.
// snippet-begin: target-index
class TargetIndex {
public:
    void Configure(TargetIndexKind kind, const math::Aabb2& bounds, float cell) {
        kind_ = kind;
        bounds_ = bounds;
        cell_ = cell > 1.0f ? cell : 1.0f;
        rebuilds_ = 0;
    }
    TargetIndexKind Kind() const { return kind_; }
    size_t Rebuilds() const { return rebuilds_; }
    size_t NodeCount() const { return tree_.NodeCount(); }

    void Rebuild(const EnemyStore& enemies) {
        if (kind_ == TargetIndexKind::Linear) return;
        ++rebuilds_;
        const size_t count = enemies.Size();
        if (kind_ == TargetIndexKind::Grid) {
            grid_.Init(bounds_, cell_);
            for (size_t i = 0; i < count; ++i)
                if (enemies.Alive(i)) grid_.Insert(enemies.Pos(i), static_cast<uint32_t>(i));
            return;
        }
        tree_.Init(bounds_, 8, 8);
        for (size_t i = 0; i < count; ++i)
            if (enemies.Alive(i)) tree_.Insert(enemies.Pos(i), static_cast<uint32_t>(i));
    }

    // 후보를 오름차순으로 out 에 채우고 개수를 돌려준다. 정렬 비용은 색인 팔이
    // 치르는 값이다 — 순서를 맞추지 않으면 동점이 갈려 비교가 성립하지 않는다.
    size_t Query(const math::Vec2& at, float radius, uint32_t* out, size_t capacity) const {
        size_t count = 0;
        auto push = [&](uint32_t row) { if (count < capacity) out[count++] = row; };
        if (kind_ == TargetIndexKind::Grid) grid_.QueryRadius(at, radius, push);
        else                                tree_.QueryRadius(at, radius, push);
        std::sort(out, out + count);
        return count;
    }

private:
    math::UniformGrid<uint32_t> grid_;
    math::Quadtree<uint32_t>    tree_;
    math::Aabb2                 bounds_;
    float                       cell_ = 32.0f;
    size_t                      rebuilds_ = 0;
    TargetIndexKind             kind_ = TargetIndexKind::Linear;
};
// snippet-end: target-index

struct WaveDef {
    int       count    = 5;
    float     hp       = 10;
    float     speed    = 20;
    int       reward   = 5;
    float     interval = 0.4f;     // 적 사이 스폰 간격(초)
    EnemyType type     = EnemyType::Normal;
};

// 엔드리스/난이도 스케일링: 웨이브 인덱스가 오를수록 어려워지는 웨이브를 생성한다.
// 적 종류는 로테이션하고, 5의 배수 웨이브마다 보스가 등장한다(tower defense 관용구).
//
// **제품은 이 함수를 부르지 않는다. 그것이 설계다** (감사 #29). 제품의 웨이브는
// 콘텐츠다 — `assets/waves.txt` 를 읽고, 없거나 검증에 실패하면 파일 전체를
// `defense::DefaultWaves()` 로 폴백한다. 난이도 곡선을 코드가 소유하면 재빌드
// 없이 밸런스를 바꾼다는 계약이 깨지므로, 코드가 웨이브를 만드는 경로는 제품에
// 두지 않는다.
//
// 그래서 이 둘의 소비자는 테스트다. 콘텐츠 파일과 무관하게 **길이와 강도를 마음대로
// 정한 웨이브 열**이 필요한 자리에서 쓴다 — `tests/test_defense_soak.cpp` 가 매 회차
// 다른 길이의 웨이브 열로 반복 매치의 유한성·ID 유일성을 재고,
// `tests/test_game_expansion.cpp` 가 곡선 자체(단조 증가·보스 주기·종류 로테이션)를
// 단언한다. 죽은 코드가 아니라 시험 장치이고, 그 사실을 여기 적어 두는 것이 등재를
// 대신한다 — `docs/LAB_ONLY.md` 는 헤더 단위 등재부라 제품 도달 헤더인 game.hpp 안의
// 함수는 담을 수 없다.
inline WaveDef GenerateWave(int index) {
    if (index < 0) index = 0;
    float scale = 1.0f + index * 0.22f;          // 난이도 곡선
    WaveDef w;
    w.count    = 8 + index;                       // 적 수 증가
    w.hp       = 10.0f * scale;                   // 체력 증가
    w.speed    = 28.0f + index * 0.7f;            // 속도 증가
    w.reward   = 4 + index / 2;                   // 보상 증가
    w.interval = 0.35f;

    if ((index + 1) % 5 == 0) {                   // 5의 배수 웨이브 = 보스
        w.type     = EnemyType::Boss;
        w.count    = 1 + index / 10;              // 후반엔 보스 여럿
        w.hp       = 45.0f * scale;
        w.interval = 1.5f;
        w.reward   = 30 + index;
    } else {
        static const EnemyType rot[4] = {EnemyType::Normal, EnemyType::Fast, EnemyType::Tank, EnemyType::Swarm};
        w.type = rot[index % 4];
        if (w.type == EnemyType::Swarm) w.count += 12;   // 무리는 더 많이
        if (w.type == EnemyType::Tank)  w.count = 3 + index / 3;
    }
    return w;
}

inline std::vector<WaveDef> GenerateWaves(int n) {
    std::vector<WaveDef> ws;
    ws.reserve(n);
    for (int i = 0; i < n; ++i) ws.push_back(GenerateWave(i));
    return ws;
}

// 엔드리스 점수: 도달 웨이브 가중 + 남은 골드.
inline int ComputeScore(int waveReached, int gold, bool cleared) {
    return waveReached * 100 + gold + (cleared ? 500 : 0);
}

class GameWorld {
public:
    // 게임플레이 상한. 한 판에 세울 수 있는 타워 수의 디자인 규칙이며 와이어
    // 포맷과는 별개다(프로토콜 쪽 상한은 wire::kSnapshotMaxTowers). 두 값은 지금
    // 같지만 이유가 다르다 — 여기는 밸런스, 저기는 패킷 예산이다. 이 값을 올릴
    // 때 스냅샷이 담을 수 있는지는 defense_protocol.hpp 의 static_assert 가
    // 빌드 시점에 막는다.
    static constexpr size_t kMaxTowers = 16;
    // 동시 생존 적 상한. small 은 지금까지의 콘텐츠가 한 번도 닿지 않는 값이고
    // (기존 실행이 그대로 재현된다), large 는 SoA·잡·아레나의 차이를 잴 수 있는
    // 규모다. 상한이 없으면 "적 2048에서 재라"는 문장 자체를 쓸 수 없다.
    static constexpr size_t kEnemyCapSmall = 64;
    static constexpr size_t kEnemyCapLarge = 2048;
    static constexpr size_t kProjectileCapSmall = 512;
    static constexpr size_t kProjectileCapLarge = 8192;
    // 청크 하나가 이보다 작으면 나눠도 동기화 비용이 이득을 넘는다.
    static constexpr size_t kParallelGrain = 256;
    // 한 프레임에 쌓아 둘 소리의 상한. 믹서의 보이스 상한과 같은 수다.
    static constexpr size_t kMaxSounds = 32;
    // 동시에 살 수 있는 정예 수. 트리도 상태 기계도 정예마다 하나이므로 이
    // 상한이 곧 그 자료구조의 개수다.
    static constexpr size_t kMaxElites = 8;
    // 조향이 한 개체당 보는 이웃의 상한. 없으면 군집 한가운데의 개체가 수백
    // 마리를 훑어 조향 비용이 개체 수의 제곱이 된다.
    static constexpr size_t kMaxNeighbors = 16;
    // 조향력 예산 = 속도 x 이 수. 속도에 비례시키는 이유는 빠른 적이 같은
    // 힘으로는 방향을 바꾸지 못하기 때문이다.
    static constexpr float kSteerForceScale = 6.0f;
    // 코어에 닿았다고 보는 거리의 제곱(조향 팔). 격자 팔은 코어 칸에 들어서는
    // 것으로 판정하므로 이 수를 읽지 않는다.
    static constexpr float kCoreReachSq = 36.0f;

    // 저장·할당자·워커를 고른다. Init 전에 불러야 한다 — Init 이 그 선택으로
    // 저장을 잡기 때문이다. 부르지 않으면 전부 기본값(스위치 이전의 동작)이다.
    void Configure(const Switches& switches, Scheduler* scheduler = nullptr) {
        switches_ = switches;
        scheduler_ = scheduler;
        const bool large = switches_.scale == ScaleKind::Large;
        enemyCap_ = large ? kEnemyCapLarge : kEnemyCapSmall;
        projectileCap_ = large ? kProjectileCapLarge : kProjectileCapSmall;
        debris_.Configure(switches_);
        navField_.Configure(switches_.pathing, enemyCap_);
        elites_.Configure(switches_.eliteBrain, kMaxElites, eliteTreeText_);
    }

    // 정예 트리의 저작 텍스트. 비면 내장 트리를 쓴다. Configure 전에 불러야
    // 하고, 제품은 assets/elite.bt 를 읽어 여기에 넣는다.
    void SetEliteTree(const std::string& text) { eliteTreeText_ = text; }
    bool ExternalEliteTree() const { return elites_.ExternalTree(); }

    const Switches& SwitchState() const { return switches_; }
    const FrameScratch& Scratch() const { return scratch_; }
    size_t EnemyCap() const { return enemyCap_; }

    // 기본 게임 구성(테스트·데모 공용). 인자로 난이도 조절.
    void Init(const math::Vec2& corePos, const math::Vec2& spawnPos,
              const std::vector<WaveDef>& waves, int startGold = 100, float coreHp = 100) {
        corePos_ = corePos; spawnPos_ = spawnPos;
        coreHp_ = coreMaxHp_ = coreHp;
        gold_ = startGold;
        waves_ = waves;
        waveIdx_ = 0; spawned_ = 0; spawnTimer_ = 0;
        enemies_.Configure(switches_.entityStore, switches_.idMap, enemyCap_);
        projs_.Configure(switches_.bodyAlloc, projectileCap_);
        // 프레임 스크래치의 최대 소요는 적 상한이 정한다(개체별 피해 배열 +
        // 스플래시 후보 배열). 여유를 두고 한 번만 잡는다 — 매치 중에는 다시
        // 잡지 않는 것이 아레나의 요점이다.
        // 색인 팔은 타워마다 후보 배열을 프레임 스크래치에서 잡는다. 기본값
        // (linear)에서는 그 배열이 없으므로 아레나 크기도 예전 그대로다.
        const size_t candidateBytes = switches_.targetIndex == TargetIndexKind::Linear
            ? 0 : kMaxTowers * enemyCap_ * sizeof(uint32_t) + 4096;
        scratch_.Init(switches_.frameAlloc, 64 * 1024 + enemyCap_ * 64 + candidateBytes);
        BlobStore::Instance().Configure(switches_.blobAlloc);
        towers_.clear();
        mapEnabled_ = false;
        path_.Clear();
        openedCells_.clear();
        debrisSettled_ = 0;
        stomps_ = 0;
        slams_ = 0;
        melee_ = 0;
        revealTicks_ = 0;
        sounds_.clear();
        gunfire_.clear();
        brains_.clear();
        revealed_.clear();
        navGrid_ = NavGrid{};
        navVersion_ = 0;
        navField_.Configure(switches_.pathing, enemyCap_);
        elites_.Configure(switches_.eliteBrain, kMaxElites, eliteTreeText_);
        won_ = lost_ = false;
        rng_ = 0x1234567u;
        nextEntityId_ = 1;
    }

    void Init(const DefenseMap& map, const std::vector<WaveDef>& waves,
              int startGold = 100, float coreHp = 100) {
        DefenseMap fresh = map;
        // 배치 판정 팔은 맵이 아니라 스위치가 소유한다. 여기서 다시 심지 않으면
        // 파일에서 온 맵(classic)이나 테스트가 손으로 지은 맵은 언제나 기본 팔로
        // 돌아, 팔을 골라도 아무 일이 일어나지 않는다.
        fresh.SetBuildCheck(switches_.buildCheck);
        // 매치가 다시 시작되면 타워도 잔해도 없다. 점유가 남아 있으면 유령 벽이
        // 경로 계산에 그대로 반영돼, 다시 시작한 판이 앞 판의 미로를 물려받는다.
        fresh.ClearOccupancy();
        fresh.RecomputeRoute();
        std::vector<math::Vec2> path = fresh.Path();
        const math::Vec2 spawn = path.empty() ? math::Vec2{0, 0} : path.front();
        const math::Vec2 core = path.empty() ? spawn : path.back();
        Init(core, spawn, waves, startGold, coreHp);
        map_ = std::move(fresh);
        path_.AssignFrom(path.data(), path.size());
        mapEnabled_ = !path_.Empty();

        // 조준 색인의 경계는 맵 전체다. 셀 크기는 가장 긴 타워 사거리의 절반쯤이
        // 되게 잡는다 — 질의가 닿는 셀이 상수 개로 남는 지점이다.
        const math::Aabb2 bounds{{0.0f, 0.0f},
                                 {static_cast<float>(map_.PixelWidth()),
                                  static_cast<float>(map_.PixelHeight())}};
        targetIndex_.Configure(switches_.targetIndex, bounds, 64.0f);

        // 절차 지형에서만 바위·잔해가 규칙에 들어온다. Classic 맵은 경로가
        // 파일이 정한 고정 회랑이라 막을 칸도, 뚫을 바위도 없다.
        if (map_.Generated()) {
            geometry_.Rebuild(map_);
            debris_.Reset(map_);
            debrisDirty_ = false;
        }

        // 통행 격자는 맵 종류와 무관하게 짓는다. 폴리라인 팔에서는 아무도 읽지
        // 않지만, 짓는 비용이 매치 시작에 한 번이라 팔마다 분기를 두는 편이
        // 오히려 비싸다 — 그리고 인지(시야 차폐)는 폴리라인 팔에서도 이 격자를 쓴다.
        navGrid_.Rebuild(map_);
        // 군집 색인의 셀은 분리 반경에 맞춘다. 조준 색인이 가장 긴 사거리의
        // 절반(64px)에 맞춰져 있는 것과 같은 규칙이고, 질의 반경이 한 자릿수
        // 다르므로 같은 인스턴스를 쓸 수 없다 — 클래스만 같다.
        const math::Aabb2 crowdBounds{{0.0f, 0.0f},
                                      {static_cast<float>(map_.PixelWidth()),
                                       static_cast<float>(map_.PixelHeight())}};
        crowdIndex_.Configure(TargetIndexKind::Grid, crowdBounds, SeparationRadius());
    }

    // 타워 배치(골드 소모). 부족하면 false.
    bool PlaceTower(const math::Vec2& pos, float range, float damage, float fireInterval, int cost) {
        if (gold_ < cost || lost_ || won_ || !CanPlaceTower(pos)) return false;
        gold_ -= cost;
        Tower t;
        t.pos = mapEnabled_ ? map_.Snap(pos) : pos;
        t.range = range;
        t.damage = damage;
        t.fireInterval = fireInterval;
        t.invested = cost;
        t.hp = t.maxHp = kTowerBaseHp;
        t.id = nextEntityId_++;
        t.facing = RadarFacing(t.pos);
        towers_.push_back(t);
        OccupyTowerCell(t.pos, true);
        return true;
    }

    // 종류별 타워 배치(스펙·비용 자동). 부족하면 false.
    bool PlaceTowerType(const math::Vec2& pos, TowerType type) {
        TowerSpec s = TowerSpecFor(type);
        if (gold_ < s.cost || lost_ || won_ || !CanPlaceTower(pos)) return false;
        gold_ -= s.cost;
        Tower t;
        t.pos = mapEnabled_ ? map_.Snap(pos) : pos;
        t.range = s.range; t.damage = s.damage; t.fireInterval = s.fireInterval;
        t.timer = 0; t.splash = s.splash; t.type = type; t.tier = 1; t.invested = s.cost;
        t.hp = t.maxHp = kTowerBaseHp;
        t.id = nextEntityId_++;
        t.facing = RadarFacing(t.pos);
        // 광맥 위에 세운 타워는 더 세게 때린다. 같은 밀도장이 지형과 보상을
        // 함께 정한다는 것이 절차 생성의 값이다.
        if (mapEnabled_ && map_.Generated()) {
            const DefenseMap::Cell cell = map_.WorldToCell(t.pos);
            if (map_.Vein(cell.x, cell.y)) { t.damage *= kVeinDamageBonus; t.onVein = true; }
        }
        towers_.push_back(t);
        OccupyTowerCell(t.pos, true);
        return true;
    }

    // snippet-begin: can-place
    bool CanPlaceTower(const math::Vec2& pos) const {
        if (towers_.size() >= kMaxTowers) return false;
        if (!mapEnabled_) return true;
        if (!map_.IsBuildable(pos)) return false;
        math::Vec2 snapped = map_.Snap(pos);
        float minDistance = map_.CellSize() * 0.75f;
        float minDistanceSq = minDistance * minDistance;
        for (const Tower& tower : towers_)
            if (math::LengthSq(tower.pos - snapped) < minDistanceSq) return false;
        // 메이즈 디펜스의 배치 규칙: 이 칸을 막아도 스폰에서 코어까지 갈 수
        // 있어야 한다. 판정은 tools::Level 의 flood fill 이고, 후보 격자
        // (이 칸이 이미 막혔다고 가정한 격자)로 한 번 돈다.
        if (map_.Generated()) {
            const DefenseMap::Cell cell = map_.WorldToCell(snapped);
            if (!map_.WouldRemainConnected(cell.x, cell.y)) return false;
        }
        return true;
    }
    // snippet-end: can-place

    int FindTowerAt(const math::Vec2& pos) const {
        float radius = mapEnabled_ ? map_.CellSize() * 0.45f : 12.0f;
        float radiusSq = radius * radius;
        for (int i = 0; i < static_cast<int>(towers_.size()); ++i)
            if (math::LengthSq(towers_[i].pos - pos) <= radiusSq) return i;
        return -1;
    }

    // 타워 판매: 누적 투자의 70% 환불 후 제거. 성공 시 환불 골드 반환(실패 0).
    int SellTower(int idx) {
        if (idx < 0 || idx >= (int)towers_.size() || won_ || lost_) return 0;
        int refund = (towers_[idx].invested * 7) / 10;
        const math::Vec2 where = towers_[idx].pos;
        gold_ += refund;
        towers_[idx] = towers_.back();   // swap-remove
        towers_.pop_back();
        // 철거는 잔해를 남긴다. 그 잔해가 어디에 눌러앉느냐에 따라 방금 비운
        // 칸이 다시 막힐 수도 있다 — 물리가 규칙의 입력이 되는 자리다.
        OccupyTowerCell(where, false);
        if (mapEnabled_ && map_.Generated()) {
            debris_.SpawnBurst(where, 6, rng_);
            debrisDirty_ = true;
        }
        return refund;
    }

    // ── 발파 ───────────────────────────────────────────────────────
    // math/sdf.hpp 의 브러시를 바위 밀도장에서 감산한다. 열린 칸 수를 돌려주고,
    // 하나도 열리지 않으면 골드를 쓰지 않는다.
    // snippet-begin: world-blast
    // 발파 비용은 이벤트가 바꾼다(live/event_schedule.hpp 의 blast_cost). 상수로
    // 두면 "발파 무료 주간" 같은 기간 한정 수정자가 표현할 것을 갖지 못한다.
    // 기본값은 kBlastCost 그대로라, 아무도 부르지 않으면 예전과 같은 게임이다.
    void SetBlastCost(int cost) { blastCost_ = cost < 0 ? 0 : cost; }
    int  BlastCost() const { return blastCost_; }

    int Blast(const math::Vec2& at) {
        if (won_ || lost_ || !mapEnabled_ || !map_.Generated()) return 0;
        if (gold_ < blastCost_) return 0;
        const math::Vec2 center = map_.Snap(at);
        const BlastBrush brush =
            BlastBrush::Sphere(center, static_cast<float>(map_.CellSize()) * 1.8f);
        blastOpened_.clear();
        const BlastResult result = ApplyBlast(map_, geometry_, brush, switches_.staticQuery,
                                              blastScratch_, &blastOpened_);
        if (result.opened == 0) return 0;
        gold_ -= blastCost_;
        // 통행 격자는 열린 칸 **전부**를 받는다. 아래 목록은 클라이언트에게
        // 보낼 예산(kMaxOpenedCells)이 있어 잘리지만, 호스트의 격자가 그 예산
        // 때문에 잘리면 발파 뒤의 항법이 사실과 다른 지형 위에서 돈다.
        for (const DefenseMap::Cell& cell : blastOpened_)
            navGrid_.SyncCell(map_, cell.x, cell.y);
        for (const DefenseMap::Cell& cell : blastOpened_) {
            if (openedCells_.size() >= kMaxOpenedCells) break;
            openedCells_.push_back(static_cast<uint16_t>(cell.y * map_.Columns() + cell.x));
        }
        map_.RecomputeRoute();
        RefreshRoute();
        debris_.SpawnBurst(center, result.opened * 2, rng_);
        debrisDirty_ = true;
        Emit(SoundKind::Blast, center);
        return result.opened;
    }
    // snippet-end: world-blast

    // 타워 타게팅 모드 설정.
    bool SetTowerTarget(int idx, TargetMode mode) {
        if (idx < 0 || idx >= (int)towers_.size()) return false;
        towers_[idx].targetMode = mode;
        return true;
    }

    // 강화 비용(현재 티어 기준). 티어가 오를수록 비싸진다.
    int UpgradeCost(int idx) const {
        if (idx < 0 || idx >= (int)towers_.size()) return 0;
        return 60 * towers_[idx].tier;
    }
    // 타워 강화: 티어↑, 데미지·사거리↑(최대 티어 3). 골드 부족·최대면 false.
    bool UpgradeTower(int idx) {
        if (idx < 0 || idx >= (int)towers_.size() || won_ || lost_) return false;
        Tower& t = towers_[idx];
        if (t.tier >= 3) return false;
        int cost = UpgradeCost(idx);
        if (gold_ < cost) return false;
        gold_ -= cost;
        ++t.tier;
        t.damage *= 1.6f;
        t.range  *= 1.12f;
        t.maxHp  += kTowerTierHp;
        t.hp      = t.maxHp;      // 강화는 수리를 겸한다
        t.invested += cost;
        return true;
    }

    // snippet-begin: world-step
    void Step(float dt) {
        if (won_ || lost_) return;
        MEGA_PROFILE_SCOPE("sim.step");
        scratch_.Begin();
        SpawnSystem(dt);
        // 인지가 전진보다 **먼저** 온다. 이번 틱에 조준 후보가 누구인가는 적이
        // 움직이기 전에 정해져야, 같은 프레임 안에서 "움직였더니 조준됐다"는
        // 순서 의존이 생기지 않는다. 청각의 입력은 직전 틱의 총성이다.
        PerceptionSystem(dt);
        NavSystem();
        EnemyAdvanceSystem(dt);
        TowerFireSystem(dt);
        ProjectileSystem(dt);
        StompSystem();
        CleanupSystem();
        DebrisSystem(dt);
        WinLoseSystem();
        scratch_.End();
    }
    // snippet-end: world-step

    // --- 조회(렌더·테스트) ---
    float CoreHp()    const { return coreHp_; }
    float CoreMaxHp() const { return coreMaxHp_; }
    int   Gold()    const { return gold_; }
    int   Wave()    const { return waveIdx_ + 1; }
    bool  Won()     const { return won_; }
    bool  Lost()    const { return lost_; }
    int   TowerCount() const { return (int)towers_.size(); }
    int   AliveEnemies() const { return enemies_.AliveCount(); }
    const EnemyStore&       Enemies()     const { return enemies_; }
    const ProjectileStore&  Projectiles() const { return projs_; }
    const std::vector<Tower>& Towers()    const { return towers_; }
    math::Vec2 CorePos() const { return corePos_; }
    math::Vec2 SpawnPos() const { return spawnPos_; }
    const DefenseMap* Map() const { return mapEnabled_ ? &map_ : nullptr; }
    const StaticGeometry& Geometry() const { return geometry_; }
    const DebrisField& Debris() const { return debris_; }
    const TargetIndex& Targets() const { return targetIndex_; }
    // ── AI 조회 ────────────────────────────────────────────────────
    const NavGrid&  Nav() const { return navGrid_; }
    const NavField& Field() const { return navField_; }
    // 개체별 AI 상태. 행 번호는 EnemyStore 와 같다(삽입 순서, 압축도 같은 규칙).
    const std::vector<BrainRow>& Brains() const { return brains_; }
    // 정예 브레인. BrainRow::elite 가 슬롯 번호이고, 그 슬롯의 문맥이 정예가
    // 지금 무엇을 하는 중인지(후퇴·회복·슬램 충전)를 든다.
    const EliteBrain& Elites() const { return elites_; }
    // 격자 항법을 쓰는가. 폴리라인 팔에서는 아래 전부가 한 줄도 돌지 않는다.
    bool GridNav() const { return switches_.pathing != PathingKind::Polyline; }
    // 이번 매치에서 잡몹이 타워를 때린 틱 수 / 정예가 슬램을 터뜨린 횟수.
    // 배선이 실제로 돌았는지를 밖에서 셀 수 있는 두 수다.
    uint32_t MeleeTicks() const { return melee_; }
    uint32_t Slams() const { return slams_; }
    // 은신 개체가 드러나 있던 (개체 x 틱) 수. 인지 배선이 실제로 돌았는지를
    // 밖에서 셀 수 있는 유일한 수다 — 0 이면 규칙이 아무것도 하지 않았다.
    uint32_t RevealTicks() const { return revealTicks_; }
    // 지금 조준 가능한 은신 개체 수. 은신 규칙이 꺼져 있으면 0 이다.
    size_t RevealedCount() const {
        size_t n = 0;
        for (size_t i = 0; i < revealed_.size(); ++i)
            if (revealed_[i] != 0 && enemies_.Alive(i) &&
                enemies_.Type(i) == EnemyType::Stalker) ++n;
        return n;
    }
    // 모르는 행은 **보인다**로 답한다. 은신 규칙이 꺼져 있으면 이 배열이 아예
    // 비어 있고, 그때 "안 보인다"를 돌려주면 스위치 이전의 그림이 달라진다.
    bool Revealed(size_t row) const { return row >= revealed_.size() || revealed_[row] != 0; }
    // 보스 리그. 기본은 내장 클립이고, 제품은 assets/boss.pose 를 읽어 갈아 끼운다.
    // 이 값이 스톰프 원점의 입력이므로 호스트와 클라이언트가 같은 것을 들어야 한다.
    const BossPoses& Poses() const { return bossPoses_; }
    void SetPoses(const BossPoses& poses) { bossPoses_ = poses; }
    // 보스 리그가 쓰는 길이 단위. 칸 하나다(맵이 없으면 기본 칸 크기).
    float BossScale() const { return mapEnabled_ ? static_cast<float>(map_.CellSize()) : 30.0f; }
    // 이번 매치에서 발이 땅에 닿은 총 횟수. 스톰프 배선이 실제로 돌았는지를
    // 밖에서 셀 수 있는 유일한 수다.
    uint32_t Stomps() const { return stomps_; }
    // 시뮬이 이번 프레임에 낸 소리. 표시 계층이 읽고 ClearSounds 로 비운다.
    const std::vector<SoundEvent>& Sounds() const { return sounds_; }
    void ClearSounds() { sounds_.clear(); }
    size_t RouteLength() const { return path_.Size(); }
    // 매치 동안 지형이 된 파편의 누적 수. DebrisField::Settled() 는 정적 기하가
    // 다시 지어질 때 0 으로 돌아가므로 그 값으로는 이 질문에 답할 수 없다.
    uint32_t DebrisSettled() const { return debrisSettled_; }
    // 발파로 열린 칸(격자 선형 색인). 클라이언트가 자기 맵 사본을 맞추는 입력이다.
    const std::vector<uint16_t>& OpenedCells() const { return openedCells_; }
    static constexpr size_t kMaxOpenedCells = 96;

    // 결정성 대조용 상태 해시. A/B 두 팔이 같은 세계를 돌았는지를 한 수로 묻는다.
    // 부동소수는 비트 그대로 섞는다 — 반올림한 값을 섞으면 갈라진 상태가 같은
    // 해시를 낼 수 있고, 그러면 이 해시는 아무것도 증명하지 못한다.
    // snippet-begin: state-hash
    uint64_t StateHash() const {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](uint64_t v) {
            h ^= v;
            h *= 1099511628211ull;
        };
        auto mixFloat = [&mix](float f) {
            uint32_t bits = 0;
            std::memcpy(&bits, &f, sizeof(bits));
            mix(bits);
        };
        mixFloat(coreHp_);
        mix(static_cast<uint64_t>(static_cast<int64_t>(gold_)));
        mix(static_cast<uint64_t>(waveIdx_));
        mix(static_cast<uint64_t>(nextEntityId_));
        mix(won_ ? 2u : lost_ ? 3u : 1u);
        const size_t enemyCount = enemies_.Size();
        mix(enemyCount);
        for (size_t i = 0; i < enemyCount; ++i) {
            mix(enemies_.Id(i));
            mixFloat(enemies_.Pos(i).x);
            mixFloat(enemies_.Pos(i).y);
            mixFloat(enemies_.Hp(i));
            mix(enemies_.PathIndex(i));
            mix(enemies_.Alive(i) ? 1u : 0u);
            // 리그 열. 보스가 아닌 행에서는 세 저장 모두 초기값을 내야 한다 —
            // ECS 팔은 컴포넌트가 아예 없고, 그 부재가 여기서 0 으로 읽힌다.
            mix(enemies_.Plants(i));
            mixFloat(enemies_.LastStomp(i).x);
            mixFloat(enemies_.LastStomp(i).y);
        }
        mix(projs_.Size());
        projs_.ForEach([&](const Projectile& p) {
            mix(p.id);
            mix(p.targetId);
            mixFloat(p.pos.x);
            mixFloat(p.pos.y);
        });
        mix(towers_.size());
        for (const Tower& t : towers_) {
            mix(t.id);
            mixFloat(t.pos.x);
            mixFloat(t.pos.y);
            mixFloat(t.timer);
            mixFloat(t.hp);
            mix(static_cast<uint64_t>(t.tier));
        }
        mix(stomps_);
        // 절차 지형은 매치 중에 변한다 — 열린 칸과 경로 길이가 그 변화의 요약이다.
        // Classic 맵에서는 이 절이 통째로 비어 해시가 예전과 비트 동일하다.
        if (mapEnabled_ && map_.Generated()) {
            mix(openedCells_.size());
            for (uint16_t cell : openedCells_) mix(cell);
            mix(path_.Size());
            mix(debris_.Live());
            mix(debrisSettled_);
        }
        // AI 상태. 결정(상태·빙결·노출·나이)만 섞고 **항법 부기(목표 칸·조향
        // 속도)는 섞지 않는다.** 부기를 섞으면 폴리라인 팔과 격자 팔이 같은
        // 세계를 돌았는지 물을 수 없게 된다 — 두 팔은 같은 위치를 지나면서
        // 서로 다른 부기를 든다. 그 대조가 이 그룹의 판정이라 여기서 지킨다.
        // 이 절은 팔과 무관하게 언제나 나온다. 팔에 따라 절을 넣었다 뺐다 하면
        // 두 팔의 해시가 **내용이 아니라 형태 때문에** 갈려, 비교 자체가 성립하지 않는다.
        mix(brains_.size());
        for (const BrainRow& brain : brains_) {
            mix(static_cast<uint64_t>(brain.state));
            mix(brain.revealed);
            mixFloat(brain.stun);
            mixFloat(brain.age);
        }
        mix(melee_);
        mix(slams_);
        return h;
    }
    // snippet-end: state-hash

private:
    float Rand01() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (rng_ >> 8) * (1.0f / 16777216.0f); }

    // 인덱스 구간을 워커에 나눠 준다. 워커가 없거나 규모가 작으면 직렬이다.
    // 결과가 워커 수와 무관해야 하므로 청크는 **인덱스로만** 나누고, 청크는
    // 자기 구간 바깥을 절대 쓰지 않는다(공유 스칼라 갱신 금지).
    // snippet-begin: run-chunks
    template <class Fn>
    void RunChunks(size_t count, Fn&& fn) {
        if (count == 0) return;
        if (!scheduler_ || scheduler_->WorkerCount() == 0 || count < kParallelGrain * 2) {
            fn(0, count);
            return;
        }
        scheduler_->ParallelFor(count, kParallelGrain, fn);
    }
    // snippet-end: run-chunks

    void SpawnSystem(float dt) {
        MEGA_PROFILE_SCOPE("sim.spawn");
        if (waveIdx_ >= (int)waves_.size()) return;
        const WaveDef& w = waves_[waveIdx_];
        spawnTimer_ -= dt;
        EnemyMod mod = EnemyModFor(w.type);
        while (spawned_ < w.count && spawnTimer_ <= 0.0f) {
            // 상한에 닿으면 이번 틱은 더 넣지 않는다. spawned_ 를 올리지 않으므로
            // 자리가 나는 다음 틱에 같은 개체를 다시 시도한다.
            if (enemies_.Full()) break;
            Enemy e;
            e.pos = spawnPos_ + math::Vec2{(Rand01() - 0.5f) * 20.0f, (Rand01() - 0.5f) * 20.0f};
            e.hp = e.maxHp = w.hp * mod.hpMul;
            e.speed  = w.speed * mod.speedMul;
            e.reward = (int)(w.reward * mod.rewardMul);
            e.damage = 10.0f * mod.damageMul;       // 기본 코어 데미지 10 × 배수
            e.type   = w.type;
            e.id     = nextEntityId_++;
            // 격자 팔에는 폴리라인 위의 자리라는 개념이 없다. 1 을 남겨 두면
            // TargetMode::First 가 전원을 같은 점수로 묶어 조준이 삽입 순서로
            // 무너지므로, 격자 팔에서는 0 으로 두어 코어 거리로 떨어지게 한다.
            e.pathIndex = (mapEnabled_ && path_.Size() > 1 && !GridNav()) ? 1 : 0;
            // 리그를 가진 개체만 초기 위상을 담는다. 0 으로 두면 스폰 첫 틱에
            // 위상 바닥값이 이미 수십이라 발을 수십 번 디딘 것으로 읽힌다.
            // 리그가 없는 개체에 값을 넣으면 안 된다 — ECS 팔에는 담을 컴포넌트가
            // 없어 그 값이 사라지고, 세 저장의 해시가 그 자리에서 갈린다.
            if (EnemyStore::HasRig(e.type)) e.plants = PlantCount(e.pos);
            enemies_.Push(e);
            // 브레인 행은 저장의 행과 **같은 순서**로 늘어난다. 그 불변식이
            // 압축에서도 유지되는 것이 이 병렬 배열이 성립하는 유일한 조건이다.
            BrainRow brain;
            brain.cell = {map_.WorldToCell(spawnPos_).x, map_.WorldToCell(spawnPos_).y};
            if (mapEnabled_) brain.cell = {map_.SpawnCell().x, map_.SpawnCell().y};
            if (IsElite(e.type) && switches_.eliteBrain != EliteBrainKind::Plain) {
                brain.elite = elites_.Acquire();
                if (brain.elite != EliteBrain::kNoSlot) {
                    EliteCtx& ctx = elites_.Context(brain.elite);
                    ctx.hp = e.hp;
                    ctx.maxHp = e.maxHp;
                }
            }
            brains_.push_back(brain);
            revealed_.push_back(1);
            ++spawned_;
            spawnTimer_ += w.interval;
        }
        // 이번 웨이브 다 스폰 + 전멸 → 다음 웨이브.
        if (spawned_ >= w.count && AliveEnemies() == 0) {
            ++waveIdx_; spawned_ = 0; spawnTimer_ = 0;
        }
    }

    // snippet-begin: enemy-advance
    void EnemyAdvanceSystem(float dt) {
        MEGA_PROFILE_SCOPE("sim.enemy_advance");
        const size_t count = enemies_.Size();
        if (count == 0) return;
        // 격자 팔은 전진이 아니라 **결정 + 전진**이다. 저장 세 팔로 나뉘지
        // 않는 이유는 그 결정이 행마다 다른 자료(브레인 행)를 읽고 쓰기
        // 때문이고, 워커에 나누지 않는 이유는 조향이 이웃을 읽기 때문이다.
        if (GridNav() && mapEnabled_) { GridAdvanceSystem(dt); return; }

        // 개체별 코어 피해를 따로 받아 둔다. 워커가 coreHp_ 를 직접 깎으면 감산
        // 순서가 실행마다 달라져 부동소수 결과가 갈린다.
        float* damage = scratch_.Alloc<float>(count);
        for (size_t i = 0; i < count; ++i) damage[i] = 0.0f;

        const math::Vec2* path = path_.Data();
        const size_t pathCount = path_.Size();
        const bool onPath = mapEnabled_ && pathCount > 0;
        const math::Vec2 core = corePos_;

        if (enemies_.Kind() == EntityStoreKind::Ecs) {
            // archetype 컬럼 순회. 워커에 나누지 않는 이유는 청크 경계가 개체
            // 수가 아니라 **조합**이 정하기 때문이다 — 보스 하나짜리 청크와
            // 2047개짜리 청크를 kParallelGrain 으로 자르면 그 자름이 조합마다
            // 달라져, "워커 수가 결과를 바꾸지 않는다"는 단언의 전제가 흔들린다.
            enemies_.ForEachCoreChunk([&](size_t chunk, EnemyCore* cells) {
                for (size_t k = 0; k < chunk; ++k) {
                    EnemyCore& row = cells[k];
                    if (!row.alive) continue;
                    if (onPath)
                        AdvanceEnemyOnPath(row.pos, row.pathIndex, row.alive, row.speed,
                                           row.damage, path, pathCount, dt, damage[row.row]);
                    else
                        AdvanceEnemyTowardCore(row.pos, row.alive, row.speed, row.damage,
                                               core, dt, damage[row.row]);
                }
            });
        } else if (enemies_.Kind() == EntityStoreKind::Soa) {
            math::Vec2*  pos   = enemies_.PosColumn();
            uint32_t*    index = enemies_.PathIndexColumn();
            uint8_t*     alive = enemies_.AliveColumn();
            const float* speed = enemies_.SpeedColumn();
            const float* hurt  = enemies_.DamageColumn();
            RunChunks(count, [=](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    if (!alive[i]) continue;
                    if (onPath)
                        AdvanceEnemyOnPath(pos[i], index[i], alive[i], speed[i], hurt[i],
                                           path, pathCount, dt, damage[i]);
                    else
                        AdvanceEnemyTowardCore(pos[i], alive[i], speed[i], hurt[i],
                                               core, dt, damage[i]);
                }
            });
        } else {
            Enemy* rows = enemies_.RowArray();
            RunChunks(count, [=](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    Enemy& e = rows[i];
                    if (!e.alive) continue;
                    if (onPath)
                        AdvanceEnemyOnPath(e.pos, e.pathIndex, e.alive, e.speed, e.damage,
                                           path, pathCount, dt, damage[i]);
                    else
                        AdvanceEnemyTowardCore(e.pos, e.alive, e.speed, e.damage,
                                               core, dt, damage[i]);
                }
            });
        }

        // 병합은 인덱스 오름차순 — 직렬 실행의 감산 순서와 정확히 같다.
        for (size_t i = 0; i < count; ++i)
            if (damage[i] != 0.0f) coreHp_ -= damage[i];
    }
    // snippet-end: enemy-advance

    // snippet-begin: tower-fire
    void TowerFireSystem(float dt) {
        MEGA_PROFILE_SCOPE("sim.tower_fire");
        // 청각의 입력이다. 표시용 sounds_ 를 쓰지 않는 것은 그 배열을 표시
        // 계층이 매 프레임 비우기 때문이다 — 시뮬 규칙이 표시 계층의 소비
        // 시점에 의존하면 헤드리스와 창 실행이 다른 게임이 된다.
        gunfire_.clear();
        const size_t towerCount = towers_.size();
        if (towerCount == 0) return;

        // 색인 팔은 틱당 한 번 통째로 다시 짓는다. 적이 매 틱 움직이므로 갱신이
        // 재구축보다 싸지 않고, 재구축은 순서가 고정돼 결정성이 유지된다.
        const bool indexed = switches_.targetIndex != TargetIndexKind::Linear;
        const size_t rows = enemies_.Size();
        uint32_t* candidates = nullptr;
        if (indexed && rows > 0) {
            MEGA_PROFILE_SCOPE("sim.target_index");
            targetIndex_.Rebuild(enemies_);
            candidates = scratch_.Alloc<uint32_t>(towerCount * rows);
        }

        // 조준 질의는 순수하다 — 적을 읽기만 하고 아무것도 바꾸지 않는다. 그래서
        // 워커에 나눌 수 있다. 바꾸는 쪽(발사체 생성·타이머)은 타워 인덱스
        // 오름차순으로 직렬 적용해 발사체 id 부여 순서를 고정한다.
        int* targets = scratch_.Alloc<int>(towerCount);
        const Tower* towers = towers_.data();
        const EnemyStore* enemies = &enemies_;
        const TargetIndex* index = &targetIndex_;
        const math::Vec2 core = corePos_;
        const SightTest sight = Sight();
        const bool useIndex = indexed && candidates != nullptr;
        RunChunks(towerCount, [=](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                const Tower& t = towers[i];
                if (!TowerFires(t.type)) { targets[i] = -1; continue; }
                if (t.timer - dt > 0.0f) { targets[i] = -1; continue; }
                if (!useIndex) {
                    targets[i] = SelectTargetIn(t.pos, core, *enemies, t.range, t.targetMode, sight);
                    continue;
                }
                uint32_t* slot = candidates + i * rows;
                const size_t found = index->Query(t.pos, t.range, slot, rows);
                targets[i] = SelectTargetAmong(t.pos, core, *enemies, t.range, t.targetMode,
                                               slot, found, sight);
            }
        });

        for (size_t i = 0; i < towerCount; ++i) {
            Tower& t = towers_[i];
            if (!TowerFires(t.type)) continue;
            t.timer -= dt;
            if (t.timer > 0.0f) continue;
            const int best = targets[i];
            if (best < 0) continue;
            Projectile projectile;
            projectile.pos = t.pos;
            projectile.target = enemies_.Pos(static_cast<size_t>(best));
            projectile.speed = 200.0f;
            projectile.damage = t.damage;
            projectile.splash = t.splash;
            projectile.stun = TowerStun(t.type);
            projectile.id = nextEntityId_++;
            projectile.targetId = enemies_.Id(static_cast<size_t>(best));
            if (!projs_.Push(projectile)) {
                --nextEntityId_;    // 상한에 막혔다 — id 를 태우지 않는다
                continue;
            }
            Emit(SoundKind::Fire, t.pos);
            if (gunfire_.size() < kMaxSounds) gunfire_.push_back(t.pos);
            t.timer = t.fireInterval;
        }
    }
    // snippet-end: tower-fire

    static float EnemyCollisionRadius(EnemyType type) {
        if (type == EnemyType::Boss) return 10.0f;
        if (type == EnemyType::Tank) return 7.0f;
        return 4.0f;
    }

    // snippet-begin: projectile-system
    void ProjectileSystem(float dt) {
        MEGA_PROFILE_SCOPE("sim.projectile");
        const size_t enemyCount = enemies_.Size();
        uint32_t* candidates = enemyCount ? scratch_.Alloc<uint32_t>(enemyCount) : nullptr;

        projs_.ForEach([&](Projectile& p) {
            int tracked = enemies_.FindRow(p.targetId);
            if (tracked >= 0 && !enemies_.Alive(static_cast<size_t>(tracked))) tracked = -1;
            if (tracked >= 0) p.target = enemies_.Pos(static_cast<size_t>(tracked));

            math::Vec2 d = p.target - p.pos;
            float dist = math::Length(d);
            float step = p.speed * dt;
            if (dist > step + 1e-3f) p.pos += d * (step / dist);
            else p.pos = p.target;

            const bool hitTracked =
                tracked >= 0 && physics::CollideCircleCircle(
                    p.pos, 2.0f, enemies_.Pos(static_cast<size_t>(tracked)),
                    EnemyCollisionRadius(enemies_.Type(static_cast<size_t>(tracked)))).hit;
            if (!hitTracked && dist > step + 1e-3f) return;

            // 여기서 죽지만 바로 다음 줄이 이 객체를 다시 읽는다. 풀 팔에서
            // 그것이 성립하는 이유는 반납이 스텝 경계로 미뤄지기 때문이다.
            projs_.Kill(p);
            const math::Vec2 impact = tracked >= 0
                ? enemies_.Pos(static_cast<size_t>(tracked)) : p.target;
            const float splash = p.splash;
            const float damage = p.damage;
            const float stun = p.stun;

            size_t hits = 0;
            for (size_t i = 0; i < enemyCount; ++i) {
                if (!enemies_.Alive(i)) continue;
                if (!physics::CollideCircleCircle(impact, splash, enemies_.Pos(i),
                                                  EnemyCollisionRadius(enemies_.Type(i))).hit)
                    continue;
                candidates[hits++] = static_cast<uint32_t>(i);
            }
            for (size_t k = 0; k < hits; ++k) {
                const size_t i = candidates[k];
                // 빙결은 **덮어쓴다**. 누적하면 빙결 타워 둘이 적을 영구히
                // 세워, 사거리 안에 들어온 순간 게임이 끝난다.
                if (stun > 0.0f && i < brains_.size() && brains_[i].stun < stun)
                    brains_[i].stun = stun;
                const float hp = enemies_.Hp(i) - damage;
                enemies_.SetHp(i, hp);
                if (hp <= 0.0f) {
                    enemies_.Kill(i);
                    gold_ += enemies_.Reward(i);
                    Emit(SoundKind::Kill, enemies_.Pos(i));
                }
            }
        });
    }
    // snippet-end: projectile-system

    // 위상의 바닥값 = 지금까지 발을 디딘 횟수. 위치의 순수 함수다.
    static uint32_t PlantCount(const math::Vec2& at) {
        const float phase = std::floor(BossPhase(at));
        return phase > 0.0f ? static_cast<uint32_t>(phase) : 0u;
    }

    // ── 스톰프 ─────────────────────────────────────────────────────
    // IK 출력이 권위 시뮬의 입력이 되는 자리. 보스가 한 걸음을 마칠 때마다
    // (걸음 위상의 바닥값이 오를 때마다) 디딘 발의 좌표를 2본 IK 로 풀고, 그
    // **발**을 원점으로 반경 안의 타워를 때린다. 몸통 중심이 아니라 발이라는
    // 것이 요점이다 — 몸통 중심이면 IK 는 그림일 뿐이고 지워도 게임이 같다.
    // snippet-begin: stomp-system
    void StompSystem() {
        const size_t count = enemies_.Size();
        if (count == 0) return;
        // 리그를 가진 개체가 하나도 없으면 이 시스템은 통째로 건너뛴다. ECS 팔은
        // 그 판정을 조합 목록(수개)에 치르고, 나머지 두 팔은 행 수(최대 2048)에
        // 치른다 — 선택적 컴포넌트가 조회에서 값을 갚는 자리다.
        if (enemies_.RigRows() == 0) return;
        MEGA_PROFILE_SCOPE("sim.stomp");
        const float scale = BossScale();
        const float radius = kStompRadiusCells * scale;
        const float radiusSq = radius * radius;
        bool destroyed = false;
        for (size_t i = 0; i < count; ++i) {
            if (!enemies_.Alive(i)) continue;
            if (!EnemyStore::HasRig(enemies_.Type(i))) continue;
            const math::Vec2 at = enemies_.Pos(i);
            const uint32_t plants = PlantCount(at);
            if (plants == enemies_.Plants(i)) continue;      // 아직 같은 걸음 안
            const math::Vec2 foot = BossPlantedFoot(bossPoses_, at, corePos_, scale);
            enemies_.SetStomp(i, plants, foot);
            ++stomps_;
            Emit(SoundKind::Stomp, foot);
            if (towers_.empty()) continue;
            for (Tower& tower : towers_) {
                if (math::LengthSq(tower.pos - foot) > radiusSq) continue;
                tower.hp -= kStompDamage;
                if (tower.hp <= 0.0f) destroyed = true;
            }
        }
        if (destroyed) RemoveDestroyedTowers();
    }
    // snippet-end: stomp-system

    // 부서진 타워를 걷어낸다. 뒤에서부터 훑는 것은 swap-remove 가 앞쪽 인덱스를
    // 건드리지 않게 하기 위해서다(SellTower 와 같은 제거 규칙).
    void RemoveDestroyedTowers() {
        for (size_t i = towers_.size(); i-- > 0;) {
            if (towers_[i].hp > 0.0f) continue;
            const math::Vec2 where = towers_[i].pos;
            towers_[i] = towers_.back();
            towers_.pop_back();
            OccupyTowerCell(where, false);
            if (mapEnabled_ && map_.Generated()) {
                debris_.SpawnBurst(where, 5, rng_);
                debrisDirty_ = true;
            }
        }
    }

    // 소리 하나. 상한을 두는 이유는 소비자가 없는 실행(테스트·서버)에서도
    // 이 목록이 자라기 때문이다 — 상한이 없으면 매치 길이만큼 늘어난다.
    void Emit(SoundKind kind, const math::Vec2& at) {
        if (sounds_.size() >= kMaxSounds) return;
        sounds_.push_back({at, kind});
    }

    // 브레인 행은 저장의 행과 짝이 맞아야 한다. 저장이 압축되기 **전에** 같은
    // 규칙(살아 있는 행을 순서 보존으로 앞으로 당긴다)으로 병렬 배열을 당긴다.
    // 압축 뒤에 하면 어느 행이 죽었는지를 물을 대상이 이미 사라져 있다.
    // snippet-begin: brain-compact
    void CleanupSystem() {
        MEGA_PROFILE_SCOPE("sim.cleanup");
        projs_.EndStep();       // 미뤄 둔 발사체 블록이 여기서만 실제로 반납된다
        const size_t count = enemies_.Size();
        size_t write = 0;
        for (size_t read = 0; read < count && read < brains_.size(); ++read) {
            if (!enemies_.Alive(read)) {
                // 정예 슬롯은 그 자리에서 돌려준다. 반납하지 않으면 정예가
                // 다섯 번 죽는 매치에서 여섯 번째 정예가 트리를 못 받는다.
                if (brains_[read].elite != EliteBrain::kNoSlot)
                    elites_.Release(brains_[read].elite);
                navField_.ClearPath(read);
                continue;
            }
            if (write != read) brains_[write] = brains_[read];
            revealed_[write] = revealed_[read];
            ++write;
        }
        brains_.resize(write);
        revealed_.resize(write);
        // 행 번호가 바뀌었으면 A* 캐시의 행 색인도 어긋난다. 경로를 통째로
        // 버리는 것이 옳다 — 남은 행의 경로를 옮겨 붙이는 것은 같은 일을 두
        // 벌 하는 것이고, 그 두 벌이 어긋나는 날 원인을 찾을 수 없다.
        if (write != count) navField_.InvalidatePaths();
        enemies_.CompactDead();
    }
    // snippet-end: brain-compact

    // 물리 결과가 AI 입력이 되는 자리. 멈춘 파편이 칸을 막고, 막힌 칸이 다음
    // 경로 계산의 입력이 된다. 코어를 봉쇄하는 안착만 지형이 되지 못한다 —
    // 그 판정도 배치와 같은 flood fill 이다.
    // snippet-begin: debris-system
    void DebrisSystem(float dt) {
        if (!mapEnabled_ || !map_.Generated()) return;
        MEGA_PROFILE_SCOPE("sim.debris");
        // 정적 기하가 바뀌었으면 다시 짓는다. 다만 날아다니는 파편이 없을 때만 —
        // PhysicsWorld 는 바디를 골라 지우지 못하므로 재구성이 곧 전량 폐기다.
        if (debrisDirty_ && debris_.Live() == 0) {
            debris_.Reset(map_);
            debrisDirty_ = false;
        }
        if (debris_.Live() == 0) return;
        debris_.Step(dt, map_, settledCells_);
        bool changed = false;
        for (const DefenseMap::Cell& cell : settledCells_) {
            if (!map_.Passable(cell.x, cell.y)) continue;
            if (!map_.WouldRemainConnected(cell.x, cell.y)) continue;
            map_.SetOccupied(cell.x, cell.y, true);
            navGrid_.SyncCell(map_, cell.x, cell.y);
            ++debrisSettled_;
            changed = true;
        }
        if (changed) {
            map_.RecomputeRoute();
            RefreshRoute();
            debrisDirty_ = true;
        }
    }
    // snippet-end: debris-system

    // 타워가 칸을 차지하거나 비운다. Classic 맵에서는 점유가 통행 판정에 들어가지
    // 않으므로 경로도 다시 계산하지 않는다(예전 동작 그대로).
    void OccupyTowerCell(const math::Vec2& pos, bool occupied) {
        if (!mapEnabled_ || !map_.Generated()) return;
        const DefenseMap::Cell cell = map_.WorldToCell(pos);
        map_.SetOccupied(cell.x, cell.y, occupied);
        // 격자 재생성이 아니라 **칸 하나** 갱신이다. 타워 하나를 세울 때마다
        // 40x40 격자를 다시 짓는 것은 바뀐 것이 한 칸이라는 사실을 버리는 일이다.
        navGrid_.SyncCell(map_, cell.x, cell.y);
        map_.RecomputeRoute();
        RefreshRoute();
        debrisDirty_ = true;
    }

    // 새 경로를 규칙이 읽는 배열로 옮기고, 살아 있는 적을 그 배열 위로 재배치한다.
    // snippet-begin: refresh-route
    void RefreshRoute() {
        const std::vector<math::Vec2> route = map_.Path();
        path_.AssignFrom(route.data(), route.size());
        // 격자 팔은 폴리라인 위의 자리를 갖지 않는다. 여기서 pathIndex 를
        // 다시 매기면 그 팔의 개체가 쓰지도 않는 값에 최근접 탐색 비용을
        // 치르고, TargetMode::First 의 점수까지 흔들린다.
        if (GridNav()) return;
        const size_t count = enemies_.Size();
        const size_t length = path_.Size();
        if (length == 0) return;
        for (size_t i = 0; i < count; ++i) {
            if (!enemies_.Alive(i)) continue;
            const math::Vec2 at = enemies_.Pos(i);
            size_t best = 0;
            float bestDistance = math::LengthSq(path_[0] - at);
            for (size_t k = 1; k < length; ++k) {
                const float d = math::LengthSq(path_[k] - at);
                if (d < bestDistance) { bestDistance = d; best = k; }   // 동점은 앞선 칸
            }
            enemies_.SetPathIndex(i, static_cast<uint32_t>(best));
        }
    }
    // snippet-end: refresh-route

    // ── 항법 ───────────────────────────────────────────────────────
    // 지형이 바뀌었을 때 두 팔이 무엇을 하는가가 여기 두 줄에 있다. 필드 팔은
    // **한 번** 다시 짓고, A* 팔은 개체 경로를 **전부** 버린다 — 버린 경로는
    // 각 개체가 자기 차례에 다시 찾으므로 비용이 개체 수에 비례해 흩어진다.
    // snippet-begin: nav-system
    void NavSystem() {
        if (!GridNav() || !mapEnabled_) return;
        MEGA_PROFILE_SCOPE("sim.nav");
        navField_.Sync(navGrid_);
        if (navVersion_ != navGrid_.Version()) {
            navVersion_ = navGrid_.Version();
            if (navField_.Kind() == PathingKind::AStar) navField_.InvalidatePaths();
        }
        if (switches_.steering) crowdIndex_.Rebuild(enemies_);
    }
    // snippet-end: nav-system

    // ── 인지 ───────────────────────────────────────────────────────
    // 은신 개체가 조준 후보가 되는 조건. 레이더의 시야 원뿔 안이면서 통행
    // 격자 위 시야선이 벽에 막히지 않거나(CanSee), 직전 틱에 쏜 타워의 청각
    // 반경 안이거나(CanHear). 둘 다 아니면 목격 기억이 나이를 먹고, forgetAfter
    // 를 넘기면 그때 사라진다 — "놓친 직후에도 잠깐 쏜다" 가 그 지연이다.
    // snippet-begin: perception-system
    void PerceptionSystem(float dt) {
        const size_t count = enemies_.Size();
        if (revealed_.size() < count) revealed_.resize(count, uint8_t{1});
        if (!switches_.stealth || !mapEnabled_) return;
        MEGA_PROFILE_SCOPE("sim.perception");
        const ai::perceive::VisionCone cone = RadarCone();
        for (size_t i = 0; i < count && i < brains_.size(); ++i) {
            if (enemies_.Type(i) != EnemyType::Stalker) {
                brains_[i].revealed = 1;
                revealed_[i] = 1;
                continue;
            }
            BrainRow& brain = brains_[i];
            const math::Vec2 at = NavSpace(map_, enemies_.Pos(i));
            bool sensed = false;
            for (const Tower& tower : towers_) {
                if (tower.type != TowerType::Radar) continue;
                if (ai::perceive::CanSee(navGrid_.SightGrid(), NavSpace(map_, tower.pos),
                                         tower.facing, cone, at)) {
                    sensed = true;
                    break;
                }
            }
            if (!sensed)
                for (const math::Vec2& shot : gunfire_)
                    if (ai::perceive::CanHear(at, NavSpace(map_, shot), kRadarHearCells)) {
                        sensed = true;
                        break;
                    }
            if (sensed) brain.memory.Observe(enemies_.Pos(i));
            else        brain.memory.Update(dt, kStalkerForget);
            brain.revealed = brain.memory.hasTarget ? uint8_t{1} : uint8_t{0};
            revealed_[i] = brain.revealed;
            if (brain.revealed) ++revealTicks_;
        }
    }
    // snippet-end: perception-system

    // ── 격자 전진 ──────────────────────────────────────────────────
    // 폴리라인 팔의 전진이 "위치를 경로의 함수로 푸는 것"이었다면, 이 팔은
    // **결정하고 나서 움직인다**. 결정은 브레인이 하고, 움직임은 격자 방향
    // 그대로거나(steering=0) 조향을 거친다(steering=1).
    // snippet-begin: grid-advance
    void GridAdvanceSystem(float dt) {
        const size_t count = enemies_.Size();
        float* damage = scratch_.Alloc<float>(count);
        for (size_t i = 0; i < count; ++i) damage[i] = 0.0f;
        const float separation = SeparationRadius();
        for (size_t i = 0; i < count && i < brains_.size(); ++i) {
            if (!enemies_.Alive(i)) continue;
            BrainRow& brain = brains_[i];
            brain.age += dt;
            brain.stun = brain.stun > dt ? brain.stun - dt : 0.0f;

            bool away = false;
            const float scale = brain.elite != EliteBrain::kNoSlot
                ? TickElite(i, brain, dt, away)
                : TickGrunt(i, brain, dt);
            if (scale <= 0.0f) continue;          // 얼었거나 때리는 중이거나 충전 중
            const float speed = enemies_.Speed(i) * scale;
            if (switches_.steering) StepSteered(i, brain, speed, dt, damage[i], away, separation);
            else                    StepGrid(i, brain, speed, dt, damage[i], away);
        }
        for (size_t i = 0; i < count; ++i)
            if (damage[i] != 0.0f) coreHp_ -= damage[i];
    }
    // snippet-end: grid-advance

    // 잡몹 한 마리의 결정. 상태 기계는 한 벌이고 상태만 개체가 든다.
    // snippet-begin: tick-grunt
    float TickGrunt(size_t i, BrainRow& brain, float dt) {
        GruntCtx ctx;
        ctx.dt = dt;
        ctx.age = brain.age;
        ctx.stun = brain.stun;
        ctx.blocked = !navField_.HasRoute(navGrid_, i, brain.cell);
        brain.state = grunts_.Tick(brain.state, ctx);
        if (ctx.attack) {
            ++melee_;
            AttackBlockingTower(i, brain, dt);
        }
        return ctx.speedScale;
    }
    // snippet-end: tick-grunt

    // 막은 것이 타워면 때리고, 타워가 아니면(바위·안착 잔해) 자기 칸에서 길을
    // 다시 찾는다. 뒤의 두 줄이 없으면 잔해에 막힌 적이 영원히 그 자리에 선다 —
    // 조향 팔의 "교착 없음" 불변식이 무너지는 첫 번째 자리다.
    void AttackBlockingTower(size_t i, BrainRow& brain, float dt) {
        const int target = NearestTowerWithin(enemies_.Pos(i), MeleeReach());
        if (target < 0) {
            brain.cell = OwnCell(i);
            brain.hasCell = false;
            return;
        }
        towers_[static_cast<size_t>(target)].hp -= kMeleeDps * dt;
        if (towers_[static_cast<size_t>(target)].hp <= 0.0f) RemoveDestroyedTowers();
    }

    // 정예 한 마리의 결정. 두 팔(fsm·bt)이 여기서 갈리고, 그 아래 행동 본체는
    // 한 벌이다.
    // snippet-begin: tick-elite
    float TickElite(size_t i, BrainRow& brain, float dt, bool& away) {
        EliteCtx& ctx = elites_.Context(brain.elite);
        ctx.dt = dt;
        ctx.hp = enemies_.Hp(i);
        ctx.maxHp = enemies_.MaxHp(i);
        ctx.coreDist = math::Length(corePos_ - enemies_.Pos(i));
        ctx.towerNear = NearestTowerWithin(enemies_.Pos(i), kEliteSlamCells * BossScale()) >= 0;
        // 쿨다운은 **브레인 밖에서** 깎는다. 두 팔 중 하나가 자기 안에서 깎으면
        // 그 팔만 한 틱 빠르거나 늦어 상태 해시 대조가 그 자리에서 갈린다.
        ctx.slamCooldown = ctx.slamCooldown > dt ? ctx.slamCooldown - dt : 0.0f;
        elites_.Tick(brain.elite);
        if (ctx.heal > 0.0f) {
            const float healed = enemies_.Hp(i) + ctx.heal;
            enemies_.SetHp(i, healed < enemies_.MaxHp(i) ? healed : enemies_.MaxHp(i));
        }
        if (ctx.slamNow) ApplySlam(enemies_.Pos(i));
        away = ctx.away;
        return ctx.speedScale;
    }
    // snippet-end: tick-elite

    void ApplySlam(const math::Vec2& at) {
        ++slams_;
        Emit(SoundKind::Stomp, at);
        const float radius = kEliteSlamCells * BossScale();
        const float radiusSq = radius * radius;
        bool destroyed = false;
        for (Tower& tower : towers_) {
            if (math::LengthSq(tower.pos - at) > radiusSq) continue;
            tower.hp -= kEliteSlamDamage;
            if (tower.hp <= 0.0f) destroyed = true;
        }
        if (destroyed) RemoveDestroyedTowers();
    }

    // 격자 방향 그대로 걷는다. **폴리라인 팔과 같은 산술**이다 — 다른 것은
    // 다음 점을 어디서 얻는가뿐이고, Classic 회랑에서 그 점이 같으므로 두 팔의
    // 위치가 비트 단위로 같아진다.
    // snippet-begin: step-grid
    void StepGrid(size_t i, BrainRow& brain, float speed, float dt, float& damageOut, bool away) {
        if (away) { StepAway(i, brain, speed, dt); return; }
        math::Vec2 pos = enemies_.Pos(i);
        float remaining = speed * dt;
        while (remaining > 0.0f) {
            if (!brain.hasCell) {
                if (brain.cell == navGrid_.Core()) {
                    damageOut += enemies_.Damage(i);
                    enemies_.Kill(i);
                    return;
                }
                NavCell next;
                if (!navField_.NextCell(navGrid_, i, brain.cell, next)) break;   // 길이 없다
                brain.cell = next;
                brain.hasCell = true;
            }
            const math::Vec2 target = map_.CellCenter(brain.cell.x, brain.cell.y);
            const math::Vec2 d = target - pos;
            const float dist = math::Length(d);
            if (dist <= remaining + 1e-4f) {
                pos = target;
                brain.hasCell = false;
                remaining -= dist;
            } else {
                pos += d * (remaining / dist);
                remaining = 0.0f;
            }
        }
        enemies_.SetPos(i, pos);
    }
    // snippet-end: step-grid

    // 후퇴. 격자 방향의 반대가 아니라 **코어의 반대**다 — 방향 필드에는 반대
    // 방향이라는 것이 없고(비용이 낮아지는 쪽만 안다), 경로 배열을 거꾸로 읽는
    // 것은 정예가 왔던 길로만 물러난다는 뜻이 돼 회피가 되지 못한다.
    void StepAway(size_t i, BrainRow& brain, float speed, float dt) {
        const math::Vec2 pos = enemies_.Pos(i);
        math::Vec2 back = pos - corePos_;
        if (math::LengthSq(back) < 1e-4f) back = {1.0f, 0.0f};
        const math::Vec2 next = SlideIntoWalkable(pos, pos + math::Normalize(back) * (speed * dt));
        enemies_.SetPos(i, next);
        brain.cell = OwnCell(i);        // 물러난 자리에서 길을 다시 찾는다
        brain.hasCell = false;
    }

    // 조향 팔. 격자 방향은 Seek 의 목표로만 쓰이고, 위치는 더 이상 경로의
    // 함수가 아니다 — 그것이 조향이 끼어들 자리를 얻는다는 말의 뜻이다.
    // snippet-begin: step-steered
    void StepSteered(size_t i, BrainRow& brain, float speed, float dt, float& damageOut,
                     bool away, float separation) {
        const math::Vec2 pos = enemies_.Pos(i);
        // 목표 칸이 **더 이상 이웃이 아니면** 그 칸으로 가는 직선이 벽을 지날 수
        // 있다. 격자 팔에서는 위치가 목표 칸의 함수라 이 상황이 생기지 않지만,
        // 조향 팔의 위치는 힘의 적분이라 한 틱에 두 칸을 지나거나 옆으로 밀려난다.
        // 그때 목표를 놓지 않으면 개체가 벽에 최고 속도로 붙어 영원히 선다 —
        // 이 세 줄이 없을 때 실제로 관측한 상태다(seed=4 에서 120마리 중 2마리).
        const DefenseMap::Cell here = map_.WorldToCell(pos);
        const bool adjacent = std::abs(here.x - brain.cell.x) <= 1 &&
                              std::abs(here.y - brain.cell.y) <= 1;
        if (!adjacent && navGrid_.Walkable(here.x, here.y)) {
            brain.cell = {here.x, here.y};
            brain.hasCell = false;
        }
        if (!brain.hasCell) {
            if (brain.cell == navGrid_.Core()) {
                damageOut += enemies_.Damage(i);
                enemies_.Kill(i);
                return;
            }
            NavCell next;
            if (!navField_.NextCell(navGrid_, i, brain.cell, next)) return;
            brain.cell = next;
            brain.hasCell = true;
        }
        ai::steer::Agent agent;
        agent.position = pos;
        agent.velocity = brain.velocity;
        agent.maxSpeed = speed;
        agent.maxForce = speed * kSteerForceScale;
        const math::Vec2 target = away ? pos + (pos - corePos_)
                                       : map_.CellCenter(brain.cell.x, brain.cell.y);

        // 우선순위가 곧 예산 배분이다. Seek 이 먼저 먹는다 — 분리에 먼저 주면
        // 빽빽한 군집에서 전진력이 남지 않아 전원이 제자리에서 서로를 민다.
        ai::steer::PriorityBlender blender(agent.maxForce);
        blender.Add(ai::steer::Seek(agent, target));
        Neighbors(i, pos, separation);
        blender.Add(ai::steer::Separation(agent, neighbors_, separation));
        const float slowRadius = BossScale() * 1.5f;
        if (math::LengthSq(pos - corePos_) < slowRadius * slowRadius)
            blender.Add(ai::steer::Arrive(agent, corePos_, slowRadius));

        agent.velocity += blender.Result() * dt;
        agent.velocity = ai::steer::Truncate(agent.velocity, agent.maxSpeed);
        brain.velocity = agent.velocity;
        const math::Vec2 next = SlideIntoWalkable(pos, pos + agent.velocity * dt);
        enemies_.SetPos(i, next);
        const DefenseMap::Cell now = map_.WorldToCell(next);
        if (now.x == brain.cell.x && now.y == brain.cell.y) brain.hasCell = false;
        if (math::LengthSq(next - corePos_) < kCoreReachSq) {
            damageOut += enemies_.Damage(i);
            enemies_.Kill(i);
        }
    }
    // snippet-end: step-steered

    // 이웃 질의. 조준 색인과 **같은 클래스**를 쓰되 인스턴스는 따로다 — 질의
    // 반경이 한 자릿수 다르므로(사거리 180px 대 분리 반경 21px) 같은 셀 크기로는
    // 한쪽이 언제나 손해를 본다.
    void Neighbors(size_t self, const math::Vec2& at, float radius) {
        neighbors_.clear();
        const size_t rows = enemies_.Size();
        if (rows == 0) return;
        crowdScratch_.resize(rows);
        const size_t found = crowdIndex_.Query(at, radius, crowdScratch_.data(), rows);
        for (size_t k = 0; k < found && neighbors_.size() < kMaxNeighbors; ++k) {
            const size_t row = crowdScratch_[k];
            if (row == self || row >= rows || !enemies_.Alive(row)) continue;
            ai::steer::Agent neighbor;
            neighbor.position = enemies_.Pos(row);
            neighbor.velocity = row < brains_.size() ? brains_[row].velocity : math::Vec2{0, 0};
            neighbors_.push_back(neighbor);
        }
    }

    bool WalkableAt(const math::Vec2& p) const {
        const DefenseMap::Cell cell = map_.WorldToCell(p);
        return navGrid_.Walkable(cell.x, cell.y);
    }
    // 벽에 부딪히면 축 하나씩 미끄러진다. 둘 다 막히면 제자리다 — 그 자리가
    // 다음 틱의 Blocked 판정으로 이어진다.
    //
    // 첫 줄이 없으면 조향 팔이 **교착한다.** 스폰 지터는 셀 하나만 한 폭이라
    // (절차 맵의 셀은 10px, 지터는 ±10px) 개체가 처음부터 통행 불가 칸에 설
    // 수 있고, 분리력도 개체를 벽 쪽으로 민다. 그 상태에서 "통행 불가로는 못
    // 간다"만 있으면 후보 셋이 전부 통행 불가라 영원히 제자리다. 이미 밖에
    // 있는 개체에게 필요한 것은 금지가 아니라 **탈출**이고, Seek 이 경로 쪽을
    // 가리키고 있으므로 한두 틱이면 돌아온다.
    // snippet-begin: slide-into-walkable
    math::Vec2 SlideIntoWalkable(const math::Vec2& from, const math::Vec2& to) const {
        if (!WalkableAt(from)) return to;        // 이미 밖이다 — 막으면 갇힌다
        if (WalkableAt(to)) return to;
        const math::Vec2 alongX{to.x, from.y};
        if (WalkableAt(alongX)) return alongX;
        const math::Vec2 alongY{from.x, to.y};
        if (WalkableAt(alongY)) return alongY;
        return from;
    }
    // snippet-end: slide-into-walkable

    NavCell OwnCell(size_t i) const {
        const DefenseMap::Cell cell = map_.WorldToCell(enemies_.Pos(i));
        return {cell.x, cell.y};
    }
    int NearestTowerWithin(const math::Vec2& at, float radius) const {
        const float radiusSq = radius * radius;
        int best = -1;
        float bestDistance = 0;
        for (size_t k = 0; k < towers_.size(); ++k) {
            const float d = math::LengthSq(towers_[k].pos - at);
            if (d > radiusSq) continue;
            if (best < 0 || d < bestDistance) { bestDistance = d; best = static_cast<int>(k); }
        }
        return best;
    }
    float MeleeReach() const {
        return mapEnabled_ ? static_cast<float>(map_.CellSize()) * 1.2f : 36.0f;
    }
    float SeparationRadius() const {
        return mapEnabled_ ? static_cast<float>(map_.CellSize()) * 0.70f : 21.0f;
    }
    static bool IsElite(EnemyType type) { return type == EnemyType::Boss; }
    math::Vec2 RadarFacing(const math::Vec2& pos) const {
        const math::Vec2 d = spawnPos_ - pos;
        return math::LengthSq(d) > 1e-4f ? math::Normalize(d) : math::Vec2{1.0f, 0.0f};
    }

    SightTest Sight() const {
        SightTest sight;
        if (mapEnabled_ && map_.Generated()) {
            sight.geometry = &geometry_;
            sight.kind = switches_.staticQuery;
        }
        if (switches_.stealth) {
            sight.revealed = revealed_.data();
            sight.revealedCount = revealed_.size();
        }
        return sight;
    }

    void WinLoseSystem() {
        MEGA_PROFILE_SCOPE("sim.winlose");
        if (coreHp_ <= 0.0f) { lost_ = true; return; }
        if (waveIdx_ >= (int)waves_.size()) won_ = true;
    }

    math::Vec2 corePos_{0, 0}, spawnPos_{0, 0};
    DefenseMap map_;
    BlobArray<math::Vec2> path_;
    bool       mapEnabled_ = false;
    float      coreHp_ = 100, coreMaxHp_ = 100;
    int        gold_ = 0;
    int blastCost_ = kBlastCost;
    std::vector<WaveDef>    waves_;
    int        waveIdx_ = 0, spawned_ = 0;
    float      spawnTimer_ = 0;
    EnemyStore      enemies_;
    ProjectileStore projs_;
    FrameScratch    scratch_;
    Switches        switches_;
    Scheduler*      scheduler_ = nullptr;
    size_t     enemyCap_ = kEnemyCapSmall;
    size_t     projectileCap_ = kProjectileCapSmall;
    std::vector<Tower>      towers_;
    StaticGeometry          geometry_;
    DebrisField             debris_;
    TargetIndex             targetIndex_;
    std::vector<uint32_t>            blastScratch_;
    std::vector<DefenseMap::Cell>    blastOpened_;
    std::vector<DefenseMap::Cell>    settledCells_;
    std::vector<uint16_t>            openedCells_;
    BossPoses               bossPoses_;
    std::vector<SoundEvent> sounds_;
    // ── AI ─────────────────────────────────────────────────────────
    NavGrid                       navGrid_;
    NavField                      navField_;
    GruntBrain                    grunts_;
    EliteBrain                    elites_;
    TargetIndex                   crowdIndex_;
    std::vector<BrainRow>         brains_;
    std::vector<uint8_t>          revealed_;
    std::vector<math::Vec2>       gunfire_;
    std::vector<ai::steer::Agent> neighbors_;
    std::vector<uint32_t>         crowdScratch_;
    std::string                   eliteTreeText_;
    uint32_t   navVersion_ = 0;
    uint32_t   melee_ = 0;
    uint32_t   slams_ = 0;
    uint32_t   revealTicks_ = 0;
    bool       debrisDirty_ = false;
    uint32_t   debrisSettled_ = 0;
    uint32_t   stomps_ = 0;
    bool       won_ = false, lost_ = false;
    uint32_t   rng_ = 0x1234567u;
    uint32_t   nextEntityId_ = 1;
};

}  // namespace game
}  // namespace mega
