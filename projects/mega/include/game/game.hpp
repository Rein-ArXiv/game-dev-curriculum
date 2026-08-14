// game — 싱글플레이 디펜스의 게임 로직. 시스템 단위로 분해한 한 스텝.
//
// 누적된 엔진 코어 위에서 디펜스의 핵심 루프가 돈다: 웨이브 스폰 → 적 진격 →
// 타워 조준·사격 → 발사체 → 충돌·보상 → 코어 방어 → 승패. 각 단계가 독립 시스템
// 이고 데이터(컴포넌트 벡터)로만 통신한다. 렌더(소프트웨어 래스터)는 이 상태를
// 읽어 그릴 뿐, 게임 로직은 렌더와 무관하게 단독으로 돌고 검증된다.

#pragma once

#include "math/vec.hpp"
#include "physics/narrow_phase/collide.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace mega {
namespace game {

// --- 타워 종류와 스펙 ---
enum class TowerType : uint8_t { Cannon, Gatling, Splash, Sniper, Count };
struct TowerSpec { float range, damage, fireInterval, splash; int cost; };
inline TowerSpec TowerSpecFor(TowerType t) {
    static constexpr std::array<TowerSpec, 4> specs{{
        {90,  25, 0.80f,  4,  70},   // Cannon: 고데미지·느림
        {75,   6, 0.12f,  2,  60},   // Gatling: 저데미지·연사
        {70,  14, 0.60f, 16,  90},   // Splash: 광역
        {180, 40, 1.40f,  2, 110},   // Sniper: 장거리·고데미지
    }};
    size_t index = static_cast<size_t>(t);
    return index < specs.size() ? specs[index] : TowerSpec{80, 10, 0.5f, 4, 60};
}

// --- 적 종류와 스탯 배수 ---
enum class EnemyType : uint8_t { Normal, Fast, Tank, Swarm, Boss, Count };
struct EnemyMod { float hpMul, speedMul, damageMul; int rewardMul; };
inline EnemyMod EnemyModFor(EnemyType t) {
    static constexpr std::array<EnemyMod, 5> mods{{
        {1.0f, 1.0f, 1.0f, 1},
        {0.6f, 1.8f, 1.0f, 1},    // Fast: 빠르고 약함
        {3.0f, 0.6f, 2.0f, 3},    // Tank: 튼튼·느림·강타·고보상
        {0.4f, 1.2f, 0.5f, 1},    // Swarm: 다수 약체
        {12.0f, 0.4f, 5.0f, 10},  // Boss: 거대 체력·매우 느림·치명타·대량 보상
    }};
    size_t index = static_cast<size_t>(t);
    return index < mods.size() ? mods[index] : EnemyMod{1, 1, 1, 1};
}

// --- 타워 타게팅 모드 ---
enum class TargetMode { Nearest, First, Strongest };

enum class TileType : uint8_t { Buildable, Path, Spawn, Core, Blocked };

// 게임 규칙이 소유하는 격자 맵. 렌더러는 이 데이터를 그릴 뿐이며, 배치 명령은
// 항상 이 맵으로 다시 검증된다. 기본 맵은 한 셀 폭의 굴곡 경로를 갖는다.
class DefenseMap {
public:
    DefenseMap(int columns = 16, int rows = 15, int cellSize = 30, int originY = 30)
        : columns_(columns), rows_(rows), cellSize_(cellSize), originY_(originY),
          tiles_(static_cast<size_t>(columns * rows), TileType::Buildable) {}

    static DefenseMap CreateDefault() {
        DefenseMap map;
        auto append = [&map](int x, int y) {
            if (!map.pathCells_.empty()) {
                const Cell& last = map.pathCells_.back();
                if (last.x == x && last.y == y) return;
            }
            map.pathCells_.push_back({x, y});
            map.SetTile(x, y, TileType::Path);
        };
        auto segment = [&append](int x0, int y0, int x1, int y1) {
            int dx = (x1 > x0) - (x1 < x0);
            int dy = (y1 > y0) - (y1 < y0);
            for (;;) {
                append(x0, y0);
                if (x0 == x1 && y0 == y1) break;
                x0 += dx;
                y0 += dy;
            }
        };

        segment(7, 0, 7, 3);
        segment(7, 3, 12, 3);
        segment(12, 3, 12, 7);
        segment(12, 7, 4, 7);
        segment(4, 7, 4, 11);
        segment(4, 11, 8, 11);
        segment(8, 11, 8, 14);
        map.SetTile(7, 0, TileType::Spawn);
        map.SetTile(8, 14, TileType::Core);

        // 가장자리 일부는 장식/지형용 비배치 셀이다.
        for (Cell c : std::array<Cell, 8>{{{0, 0}, {1, 0}, {14, 0}, {15, 0},
                                           {0, 14}, {1, 14}, {14, 14}, {15, 14}}})
            map.SetTile(c.x, c.y, TileType::Blocked);
        return map;
    }

    // '.', '#', 'P', 'S', 'C' rows -> validated map. The path must be one
    // continuous, non-branching chain from S to C and consume every path tile.
    static bool FromAscii(const std::vector<std::string>& rows, int cellSize,
                          int originY, DefenseMap& out) {
        if (rows.empty() || rows.size() > 64 || rows.front().empty() || rows.front().size() > 64)
            return false;
        size_t width = rows.front().size();
        for (const std::string& row : rows) if (row.size() != width) return false;

        DefenseMap map(static_cast<int>(width), static_cast<int>(rows.size()), cellSize, originY);
        Cell spawn{-1, -1}, core{-1, -1};
        int pathTileCount = 0;
        for (int y = 0; y < map.rows_; ++y) {
            for (int x = 0; x < map.columns_; ++x) {
                char tile = rows[static_cast<size_t>(y)][static_cast<size_t>(x)];
                switch (tile) {
                    case '.': map.SetTile(x, y, TileType::Buildable); break;
                    case '#': map.SetTile(x, y, TileType::Blocked); break;
                    case 'P': map.SetTile(x, y, TileType::Path); ++pathTileCount; break;
                    case 'S':
                        if (spawn.x >= 0) return false;
                        spawn = {x, y}; map.SetTile(x, y, TileType::Spawn); ++pathTileCount; break;
                    case 'C':
                        if (core.x >= 0) return false;
                        core = {x, y}; map.SetTile(x, y, TileType::Core); ++pathTileCount; break;
                    default: return false;
                }
            }
        }
        if (spawn.x < 0 || core.x < 0 || pathTileCount < 2) return false;

        std::vector<uint8_t> visited(static_cast<size_t>(map.columns_ * map.rows_), 0);
        Cell current = spawn;
        map.pathCells_.push_back(current);
        visited[static_cast<size_t>(current.y * map.columns_ + current.x)] = 1;
        const int directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        while (!(current.x == core.x && current.y == core.y)) {
            std::array<Cell, 4> candidates{};
            int candidateCount = 0;
            for (const auto& direction : directions) {
                int nx = current.x + direction[0];
                int ny = current.y + direction[1];
                if (!map.InBounds(nx, ny)) continue;
                TileType tile = map.Tile(nx, ny);
                bool isPath = tile == TileType::Path || tile == TileType::Spawn || tile == TileType::Core;
                size_t index = static_cast<size_t>(ny * map.columns_ + nx);
                if (isPath && !visited[index]) candidates[static_cast<size_t>(candidateCount++)] = {nx, ny};
            }
            if (candidateCount != 1) return false;
            current = candidates[0];
            visited[static_cast<size_t>(current.y * map.columns_ + current.x)] = 1;
            map.pathCells_.push_back(current);
        }
        if (static_cast<int>(map.pathCells_.size()) != pathTileCount) return false;
        out = std::move(map);
        return true;
    }

    struct Cell { int x = 0, y = 0; };

    int Columns() const { return columns_; }
    int Rows() const { return rows_; }
    int CellSize() const { return cellSize_; }
    int OriginY() const { return originY_; }
    int PixelWidth() const { return columns_ * cellSize_; }
    int PixelHeight() const { return originY_ + rows_ * cellSize_; }

    bool InBounds(int x, int y) const {
        return x >= 0 && y >= 0 && x < columns_ && y < rows_;
    }
    TileType Tile(int x, int y) const {
        return InBounds(x, y) ? tiles_[static_cast<size_t>(y * columns_ + x)] : TileType::Blocked;
    }
    Cell WorldToCell(const math::Vec2& p) const {
        return {static_cast<int>(std::floor(p.x / static_cast<float>(cellSize_))),
                static_cast<int>(std::floor((p.y - originY_) / static_cast<float>(cellSize_)))};
    }
    math::Vec2 CellCenter(int x, int y) const {
        return {x * static_cast<float>(cellSize_) + cellSize_ * 0.5f,
                originY_ + y * static_cast<float>(cellSize_) + cellSize_ * 0.5f};
    }
    math::Vec2 Snap(const math::Vec2& p) const {
        Cell c = WorldToCell(p);
        return CellCenter(c.x, c.y);
    }
    bool IsBuildable(const math::Vec2& p) const {
        Cell c = WorldToCell(p);
        return InBounds(c.x, c.y) && Tile(c.x, c.y) == TileType::Buildable;
    }
    std::vector<math::Vec2> Path() const {
        std::vector<math::Vec2> out;
        out.reserve(pathCells_.size());
        for (Cell c : pathCells_) out.push_back(CellCenter(c.x, c.y));
        return out;
    }

private:
    void SetTile(int x, int y, TileType type) {
        if (InBounds(x, y)) tiles_[static_cast<size_t>(y * columns_ + x)] = type;
    }

    int columns_ = 0, rows_ = 0, cellSize_ = 0, originY_ = 0;
    std::vector<TileType> tiles_;
    std::vector<Cell> pathCells_;
};

struct Enemy {
    math::Vec2 pos{0, 0};
    float      hp = 10, maxHp = 10, speed = 20;
    int        reward = 5;
    float      damage = 10;     // 코어에 닿을 때
    bool       alive = true;
    EnemyType  type = EnemyType::Normal;
    uint32_t   id = 0;
    size_t     pathIndex = 0;
};

struct Tower {
    math::Vec2 pos{0, 0};
    float      range = 80, damage = 5, fireInterval = 0.5f, timer = 0;
    float      splash = 6;
    TowerType  type = TowerType::Cannon;
    int        tier = 1;
    TargetMode targetMode = TargetMode::Nearest;
    int        invested = 0;      // 누적 투자 골드(판매 환불 계산)
    uint32_t   id = 0;
};

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

struct Projectile {
    math::Vec2 pos{0, 0}, target{0, 0};
    float      speed = 200, damage = 5, splash = 6;
    bool       alive = true;
    uint32_t   id = 0;
    uint32_t   targetId = 0;
};

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
    static constexpr size_t kMaxTowers = 16;
    // 기본 게임 구성(테스트·데모 공용). 인자로 난이도 조절.
    void Init(const math::Vec2& corePos, const math::Vec2& spawnPos,
              const std::vector<WaveDef>& waves, int startGold = 100, float coreHp = 100) {
        corePos_ = corePos; spawnPos_ = spawnPos;
        coreHp_ = coreMaxHp_ = coreHp;
        gold_ = startGold;
        waves_ = waves;
        waveIdx_ = 0; spawned_ = 0; spawnTimer_ = 0;
        enemies_.clear(); towers_.clear(); projs_.clear();
        mapEnabled_ = false;
        path_.clear();
        won_ = lost_ = false;
        rng_ = 0x1234567u;
        nextEntityId_ = 1;
    }

    void Init(const DefenseMap& map, const std::vector<WaveDef>& waves,
              int startGold = 100, float coreHp = 100) {
        map_ = map;
        path_ = map_.Path();
        const math::Vec2 spawn = path_.empty() ? math::Vec2{0, 0} : path_.front();
        const math::Vec2 core = path_.empty() ? spawn : path_.back();
        Init(core, spawn, waves, startGold, coreHp);
        map_ = map;
        path_ = map_.Path();
        mapEnabled_ = !path_.empty();
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
        t.id = nextEntityId_++;
        towers_.push_back(t);
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
        t.id = nextEntityId_++;
        towers_.push_back(t);
        return true;
    }

    bool CanPlaceTower(const math::Vec2& pos) const {
        if (towers_.size() >= kMaxTowers) return false;
        if (!mapEnabled_) return true;
        if (!map_.IsBuildable(pos)) return false;
        math::Vec2 snapped = map_.Snap(pos);
        float minDistance = map_.CellSize() * 0.75f;
        float minDistanceSq = minDistance * minDistance;
        for (const Tower& tower : towers_)
            if (math::LengthSq(tower.pos - snapped) < minDistanceSq) return false;
        return true;
    }

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
        gold_ += refund;
        towers_[idx] = towers_.back();   // swap-remove
        towers_.pop_back();
        return refund;
    }

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
        t.invested += cost;
        return true;
    }

    void Step(float dt) {
        if (won_ || lost_) return;
        SpawnSystem(dt);
        EnemyAdvanceSystem(dt);
        TowerFireSystem(dt);
        ProjectileSystem(dt);
        CleanupSystem();
        WinLoseSystem();
    }

    // --- 조회(렌더·테스트) ---
    float CoreHp()    const { return coreHp_; }
    float CoreMaxHp() const { return coreMaxHp_; }
    int   Gold()    const { return gold_; }
    int   Wave()    const { return waveIdx_ + 1; }
    bool  Won()     const { return won_; }
    bool  Lost()    const { return lost_; }
    int   TowerCount() const { return (int)towers_.size(); }
    int   AliveEnemies() const {
        int n = 0; for (const auto& e : enemies_) if (e.alive) ++n; return n;
    }
    const std::vector<Enemy>&      Enemies()     const { return enemies_; }
    const std::vector<Tower>&      Towers()      const { return towers_; }
    const std::vector<Projectile>& Projectiles() const { return projs_; }
    math::Vec2 CorePos() const { return corePos_; }
    math::Vec2 SpawnPos() const { return spawnPos_; }
    const DefenseMap* Map() const { return mapEnabled_ ? &map_ : nullptr; }

private:
    float Rand01() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (rng_ >> 8) * (1.0f / 16777216.0f); }

    void SpawnSystem(float dt) {
        if (waveIdx_ >= (int)waves_.size()) return;
        const WaveDef& w = waves_[waveIdx_];
        spawnTimer_ -= dt;
        EnemyMod mod = EnemyModFor(w.type);
        while (spawned_ < w.count && spawnTimer_ <= 0.0f) {
            Enemy e;
            e.pos = spawnPos_ + math::Vec2{(Rand01() - 0.5f) * 20.0f, (Rand01() - 0.5f) * 20.0f};
            e.hp = e.maxHp = w.hp * mod.hpMul;
            e.speed  = w.speed * mod.speedMul;
            e.reward = (int)(w.reward * mod.rewardMul);
            e.damage = 10.0f * mod.damageMul;       // 기본 코어 데미지 10 × 배수
            e.type   = w.type;
            e.id     = nextEntityId_++;
            e.pathIndex = mapEnabled_ && path_.size() > 1 ? 1 : 0;
            enemies_.push_back(e);
            ++spawned_;
            spawnTimer_ += w.interval;
        }
        // 이번 웨이브 다 스폰 + 전멸 → 다음 웨이브.
        if (spawned_ >= w.count && AliveEnemies() == 0) {
            ++waveIdx_; spawned_ = 0; spawnTimer_ = 0;
        }
    }

    void EnemyAdvanceSystem(float dt) {
        for (auto& e : enemies_) {
            if (!e.alive) continue;
            if (mapEnabled_) {
                float remaining = e.speed * dt;
                while (remaining > 0.0f && e.alive) {
                    if (e.pathIndex >= path_.size()) {
                        coreHp_ -= e.damage;
                        e.alive = false;
                        break;
                    }
                    math::Vec2 d = path_[e.pathIndex] - e.pos;
                    float dist = math::Length(d);
                    if (dist <= remaining + 1e-4f) {
                        e.pos = path_[e.pathIndex++];
                        remaining -= dist;
                    } else {
                        e.pos += d * (remaining / dist);
                        remaining = 0.0f;
                    }
                }
                continue;
            }
            math::Vec2 d = corePos_ - e.pos;
            float dist = math::Length(d);
            if (dist < 6.0f) {                       // 코어 도달
                coreHp_ -= e.damage;
                e.alive = false;
                continue;
            }
            e.pos += d * (e.speed * dt / dist);      // 코어로 직진
        }
    }

    void TowerFireSystem(float dt) {
        for (auto& t : towers_) {
            t.timer -= dt;
            if (t.timer > 0.0f) continue;
            // 사거리 안 적을 타워 모드에 맞춰 조준.
            int best = SelectTarget(t.pos, corePos_, enemies_, t.range, t.targetMode);
            if (best >= 0) {
                Projectile projectile;
                projectile.pos = t.pos;
                projectile.target = enemies_[best].pos;
                projectile.speed = 200.0f;
                projectile.damage = t.damage;
                projectile.splash = t.splash;
                projectile.id = nextEntityId_++;
                projectile.targetId = enemies_[best].id;
                projs_.push_back(projectile);
                t.timer = t.fireInterval;
            }
        }
    }

    static float EnemyCollisionRadius(EnemyType type) {
        if (type == EnemyType::Boss) return 10.0f;
        if (type == EnemyType::Tank) return 7.0f;
        return 4.0f;
    }

    void ProjectileSystem(float dt) {
        for (auto& p : projs_) {
            if (!p.alive) continue;
            const Enemy* tracked = nullptr;
            for (const Enemy& enemy : enemies_) {
                if (enemy.alive && enemy.id == p.targetId) {
                    tracked = &enemy;
                    p.target = enemy.pos;
                    break;
                }
            }
            math::Vec2 d = p.target - p.pos;
            float dist = math::Length(d);
            float step = p.speed * dt;
            if (dist > step + 1e-3f) p.pos += d * (step / dist);
            else p.pos = p.target;

            const bool hitTracked =
                tracked && physics::CollideCircleCircle(
                    p.pos, 2.0f, tracked->pos,
                    EnemyCollisionRadius(tracked->type)).hit;
            if (hitTracked || dist <= step + 1e-3f) {
                p.alive = false;
                const math::Vec2 impact = tracked ? tracked->pos : p.target;
                for (Enemy& enemy : enemies_) {
                    if (!enemy.alive) continue;
                    if (!physics::CollideCircleCircle(
                            impact, p.splash, enemy.pos,
                            EnemyCollisionRadius(enemy.type)).hit)
                        continue;
                    enemy.hp -= p.damage;
                    if (enemy.hp <= 0.0f) {
                        enemy.alive = false;
                        gold_ += enemy.reward;
                    }
                }
            }
        }
    }

    void CleanupSystem() {
        // 죽은 발사체 제거.
        projs_.erase(std::remove_if(projs_.begin(), projs_.end(),
                                    [](const Projectile& p) { return !p.alive; }),
                     projs_.end());
        // 적은 웨이브 전환 시 전멸하므로 누적이 제한적 — 죽은 것 압축.
        enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
                                      [](const Enemy& e) { return !e.alive; }),
                       enemies_.end());
    }

    void WinLoseSystem() {
        if (coreHp_ <= 0.0f) { lost_ = true; return; }
        if (waveIdx_ >= (int)waves_.size()) won_ = true;
    }

    math::Vec2 corePos_{0, 0}, spawnPos_{0, 0};
    DefenseMap map_;
    std::vector<math::Vec2> path_;
    bool       mapEnabled_ = false;
    float      coreHp_ = 100, coreMaxHp_ = 100;
    int        gold_ = 0;
    std::vector<WaveDef>    waves_;
    int        waveIdx_ = 0, spawned_ = 0;
    float      spawnTimer_ = 0;
    std::vector<Enemy>      enemies_;
    std::vector<Tower>      towers_;
    std::vector<Projectile> projs_;
    bool       won_ = false, lost_ = false;
    uint32_t   rng_ = 0x1234567u;
    uint32_t   nextEntityId_ = 1;
};

}  // namespace game
}  // namespace mega
