// defense_view — 디펜스의 표시 계층: 스킨 로드 · 스냅샷 캡처 · 한 프레임 그리기.
//
// games/defense/main.cpp 익명 네임스페이스에 있던 코드다. 그 자리에 있는 한
// 제품만 볼 수 있고 어떤 테스트도 구동할 수 없었다 — 감사가 "--selftest 가
// 조기 반환해 에셋 로드·복제·렌더를 전부 건너뛴다"고 지적한 것의 절반이 이것이다.
// mega_core 로 옮기면 헤드리스 테스트가 창 없이 같은 그리기를 돌릴 수 있다.
//
// 여기에는 OS 호출이 없다. 프레임버퍼는 순수 소프트웨어 래스터라이저이고,
// 표시(창·GPU)는 platform/ 이 맡는다. 그래서 이 코드는 헤드리스에서 그대로 돈다.

#pragma once

#include "engine/particles.hpp"
#include "game/defense_protocol.hpp"
#include "game/game.hpp"
#include "render/framebuffer.hpp"
#include "render/raster2d.hpp"
#include "render/sprite.hpp"
#include "render/text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace mega {
namespace game {

// 기본 맵(16x15 셀 · 30px · HUD 30px)이 정확히 채우는 크기.
inline constexpr int kViewWidth = 480;
inline constexpr int kViewHeight = 480;

// 스프라이트 묶음. 외부 PPM 이 있으면 그것을, 없으면 절차 생성 폴백을 쓴다.
struct DefenseSkin {
    std::array<render::Sprite, 4> towers;
    std::array<render::Sprite, 5> enemies;
    render::Sprite core;
    int externalAssets = 0;

    void BuildFallbacks() {
        render::Sprite hero = render::HeroMask();
        towers[0] = render::Tint(hero, {235, 150, 40, 255});
        towers[1] = render::Tint(hero, {60, 200, 220, 255});
        towers[2] = render::Tint(hero, {190, 90, 230, 255});
        towers[3] = render::Tint(hero, {80, 230, 110, 255});
        render::Sprite monster = render::MonsterMask();
        enemies[0] = render::Tint(monster, {220, 75, 75, 255});
        enemies[1] = render::Tint(monster, {240, 225, 60, 255});
        enemies[2] = render::Tint(monster, {125, 95, 175, 255});
        enemies[3] = render::Tint(monster, {240, 95, 105, 255});
        enemies[4] = render::Tint(monster, {205, 55, 190, 255});
        core = render::MakeSprite({
            "..C..", ".CCC.", "CCWCC", ".CCC.", "..C..",
        }, {{'C', {150, 210, 255, 255}}, {'W', {255, 255, 255, 255}}});
    }

    void Load(const std::string& root) {
        BuildFallbacks();
        externalAssets = 0;
        const char* towerFiles[4] = {
            "tower_cannon.ppm", "tower_gatling.ppm", "tower_splash.ppm", "tower_sniper.ppm"
        };
        const char* enemyFiles[5] = {
            "enemy_normal.ppm", "enemy_fast.ppm", "enemy_tank.ppm", "enemy_swarm.ppm", "enemy_boss.ppm"
        };
        for (size_t i = 0; i < towers.size(); ++i) {
            render::Sprite loaded;
            if (render::LoadPpmP3(root + "/" + towerFiles[i], loaded)) {
                towers[i] = std::move(loaded);
                ++externalAssets;
            }
        }
        for (size_t i = 0; i < enemies.size(); ++i) {
            render::Sprite loaded;
            if (render::LoadPpmP3(root + "/" + enemyFiles[i], loaded)) {
                enemies[i] = std::move(loaded);
                ++externalAssets;
            }
        }
        render::Sprite loaded;
        if (render::LoadPpmP3(root + "/core.ppm", loaded)) {
            core = std::move(loaded);
            ++externalAssets;
        }
    }
};

// HUD 가 알아야 하는 프레임별 상태. DrawGame 의 인자를 늘리지 않으려고 묶었다.
struct HudState {
    TowerType   selectedType = TowerType::Cannon;
    uint32_t    selectedTower = 0;
    int         mouseX = -100, mouseY = -100;
    const char* modeLabel = "SOLO";
    bool        paused = false;
};

// 권위 월드 → 렌더·전송 공용 스냅샷. 호스트/솔로는 이것을 그리고, 클라이언트는
// 같은 모양의 스냅샷을 네트워크에서 받아 보간한 것을 그린다.
inline wire::Snapshot CaptureView(const GameWorld& world, uint32_t tick = 0) {
    wire::Snapshot view;
    view.tick = tick;
    view.gold = world.Gold();
    view.coreHp = world.CoreHp();
    view.coreMaxHp = world.CoreMaxHp();
    view.wave = static_cast<uint16_t>(world.Wave());
    view.won = world.Won();
    view.lost = world.Lost();
    for (const Tower& source : world.Towers()) {
        wire::TowerState tower;
        tower.id = source.id;
        tower.pos = source.pos;
        tower.type = source.type;
        tower.tier = static_cast<uint8_t>(source.tier);
        tower.targetMode = source.targetMode;
        view.towers.push_back(tower);
    }
    for (const Enemy& source : world.Enemies()) {
        if (!source.alive) continue;
        view.enemies.push_back({source.id, source.pos, source.hp, source.maxHp, source.type});
    }
    for (const Projectile& source : world.Projectiles()) {
        if (source.alive) view.projectiles.push_back({source.id, source.pos});
    }
    return view;
}

inline render::Color HpColor(float fraction) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    return {static_cast<uint8_t>(255 * (1.0f - fraction)),
            static_cast<uint8_t>(255 * fraction), 50, 255};
}

inline void BlitCentered(render::Framebuffer& framebuffer, const render::Sprite& sprite,
                         const math::Vec2& position, int scale) {
    render::BlitSprite(framebuffer, sprite,
                       static_cast<int>(position.x) - sprite.w * scale / 2,
                       static_cast<int>(position.y) - sprite.h * scale / 2, scale);
}

// 배치 가능 여부를 "보이는 것"으로만 판정한다. 클라이언트는 권위 월드를 갖지
// 않으므로 프리뷰는 반드시 스냅샷 기준이어야 한다(권위 판정은 호스트가 다시 한다).
inline bool CanPlaceInView(const DefenseMap& map, const wire::Snapshot& view,
                           const math::Vec2& position) {
    if (!map.IsBuildable(position)) return false;
    math::Vec2 snapped = map.Snap(position);
    float minimum = map.CellSize() * 0.75f;
    for (const auto& tower : view.towers)
        if (math::LengthSq(tower.pos - snapped) < minimum * minimum) return false;
    return true;
}

inline const wire::TowerState* TowerAt(const wire::Snapshot& view,
                                       const math::Vec2& position, float radius) {
    float radiusSq = radius * radius;
    for (const auto& tower : view.towers)
        if (math::LengthSq(tower.pos - position) <= radiusSq) return &tower;
    return nullptr;
}

inline void DrawMap(render::Framebuffer& framebuffer, const DefenseMap& map) {
    framebuffer.Clear({14, 18, 23, 255});
    for (int y = 0; y < map.Rows(); ++y) {
        for (int x = 0; x < map.Columns(); ++x) {
            render::Color color{36, 58, 48, 255};
            switch (map.Tile(x, y)) {
                case TileType::Buildable: color = ((x + y) & 1) ? render::Color{38, 65, 51, 255} : render::Color{34, 59, 47, 255}; break;
                case TileType::Path:      color = {83, 79, 72, 255}; break;
                case TileType::Spawn:     color = {90, 72, 105, 255}; break;
                case TileType::Core:      color = {58, 88, 120, 255}; break;
                case TileType::Blocked:   color = {27, 34, 37, 255}; break;
            }
            int px = x * map.CellSize();
            int py = map.OriginY() + y * map.CellSize();
            render::FillRect(framebuffer, px, py, map.CellSize(), map.CellSize(), color);
            render::DrawRectOutline(framebuffer, px, py, map.CellSize(), map.CellSize(), {22, 29, 30, 90});
        }
    }
}

inline void DrawGame(render::Framebuffer& framebuffer, const DefenseMap& map,
                     const wire::Snapshot& view, const DefenseSkin& skin,
                     const HudState& hud, bool connected,
                     const engine::ParticleSystem* particles = nullptr) {
    (void)connected;
    DrawMap(framebuffer, map);

    math::Vec2 corePosition = map.Path().back();
    BlitCentered(framebuffer, skin.core, corePosition, 3);
    float coreFraction = view.coreMaxHp > 0 ? view.coreHp / view.coreMaxHp : 0;
    render::DrawRectOutline(framebuffer, static_cast<int>(corePosition.x) - 11,
                            static_cast<int>(corePosition.y) - 11, 23, 23,
                            HpColor(coreFraction));

    for (const auto& tower : view.towers) {
        size_t index = static_cast<size_t>(tower.type);
        if (index >= skin.towers.size()) continue;
        BlitCentered(framebuffer, skin.towers[index], tower.pos, 2);
        int half = 7 + tower.tier;
        if (tower.id == hud.selectedTower)
            render::DrawRectOutline(framebuffer, static_cast<int>(tower.pos.x) - half,
                                    static_cast<int>(tower.pos.y) - half,
                                    half * 2 + 1, half * 2 + 1, {255, 255, 255, 255});
    }

    for (const auto& enemy : view.enemies) {
        size_t index = static_cast<size_t>(enemy.type);
        if (index >= skin.enemies.size()) continue;
        int scale = enemy.type == EnemyType::Boss ? 3 : (enemy.type == EnemyType::Tank ? 2 : 1);
        BlitCentered(framebuffer, skin.enemies[index], enemy.pos, scale);
        if (enemy.hp < enemy.maxHp && enemy.maxHp > 0) {
            int width = enemy.type == EnemyType::Boss ? 24 : 12;
            int x = static_cast<int>(enemy.pos.x) - width / 2;
            int y = static_cast<int>(enemy.pos.y) - (enemy.type == EnemyType::Boss ? 12 : 7);
            render::FillRect(framebuffer, x, y, width, 2, {45, 35, 38, 255});
            render::FillRect(framebuffer, x, y,
                             static_cast<int>(width * std::clamp(enemy.hp / enemy.maxHp, 0.0f, 1.0f)),
                             2, HpColor(enemy.hp / enemy.maxHp));
        }
    }
    for (const auto& projectile : view.projectiles)
        render::FillCircle(framebuffer, static_cast<int>(projectile.pos.x),
                           static_cast<int>(projectile.pos.y), 2, {255, 230, 80, 255});
    if (particles) {
        for (const engine::Particle& particle : particles->Particles()) {
            const float alpha = std::clamp(engine::ParticleSystem::Alpha(particle), 0.0f, 1.0f);
            render::FillCircle(
                framebuffer, static_cast<int>(particle.pos.x),
                static_cast<int>(particle.pos.y),
                std::max(1, static_cast<int>(particle.size)),
                {255, 170, 55, static_cast<uint8_t>(alpha * 220.0f)});
        }
    }

    math::Vec2 mouse{static_cast<float>(hud.mouseX), static_cast<float>(hud.mouseY)};
    if (hud.mouseY >= map.OriginY()) {
        math::Vec2 snapped = map.Snap(mouse);
        bool valid = CanPlaceInView(map, view, mouse);
        render::Color preview = valid ? render::Color{100, 240, 155, 230}
                                      : render::Color{245, 90, 90, 230};
        render::DrawRectOutline(framebuffer, static_cast<int>(snapped.x) - 10,
                                static_cast<int>(snapped.y) - 10, 21, 21, preview);
    }

    render::FillRect(framebuffer, 0, 0, kViewWidth, map.OriginY(), {7, 10, 14, 245});
    const char* towerNames[4] = {"CANNON", "GATLING", "SPLASH", "SNIPER"};
    char text[160];
    std::snprintf(text, sizeof(text), "GOLD %d  CORE %d  WAVE %u  %s  %s",
                  view.gold, static_cast<int>(view.coreHp), view.wave,
                  towerNames[static_cast<size_t>(hud.selectedType)], hud.modeLabel);
    render::DrawString(framebuffer, 5, 5, text, {230, 235, 240, 255}, 1);

    if (const auto* selected = wire::FindById(view.towers, hud.selectedTower)) {
        char detail[96];
        const char* targetNames[3] = {"NEAREST", "FIRST", "STRONGEST"};
        std::snprintf(detail, sizeof(detail), "TIER %u  TARGET %s",
                      selected->tier, targetNames[static_cast<size_t>(selected->targetMode)]);
        render::FillRect(framebuffer, 0, kViewHeight - 16, kViewWidth, 16, {7, 10, 14, 225});
        render::DrawString(framebuffer, 5, kViewHeight - 13, detail, {220, 225, 230, 255}, 1);
    }

    if (view.won || view.lost) {
        const char* result = view.won ? "CORE DEFENDED" : "CORE LOST";
        render::Color color = view.won ? render::Color{120, 240, 150, 255}
                                       : render::Color{240, 110, 110, 255};
        render::FillRect(framebuffer, 0, kViewHeight / 2 - 32, kViewWidth, 64, {5, 7, 10, 220});
        render::DrawString(framebuffer, (kViewWidth - render::TextWidth(result, 4)) / 2,
                           kViewHeight / 2 - 14, result, color, 4);
    } else if (hud.paused) {
        constexpr const char* label = "PAUSED";
        render::FillRect(framebuffer, 0, kViewHeight / 2 - 20, kViewWidth, 40,
                         {5, 7, 10, 205});
        render::DrawString(framebuffer,
                           (kViewWidth - render::TextWidth(label, 3)) / 2,
                           kViewHeight / 2 - 10, label,
                           {235, 240, 245, 255}, 3);
    }
}

// 사라진 발사체 자리에 파편을 뿌린다. 호스트든 클라이언트든 "스냅샷에서
// 없어졌다"만 보므로 두 모드에서 같은 코드가 돈다.
inline void UpdateImpactParticles(
        const wire::Snapshot& view,
        std::unordered_map<uint32_t, math::Vec2>& previousProjectiles,
        engine::ParticleSystem& particles, float dt) {
    for (const auto& [id, position] : previousProjectiles) {
        if (wire::FindById(view.projectiles, id)) continue;
        for (int i = 0; i < 8; ++i) {
            const float angle = static_cast<float>(i) * 0.78539816339f;
            particles.Emit(
                {position.x, position.y, 0.0f},
                {std::cos(angle) * 35.0f, std::sin(angle) * 35.0f, 0.0f},
                0.25f, 1.5f);
        }
    }
    previousProjectiles.clear();
    for (const auto& projectile : view.projectiles)
        previousProjectiles.emplace(projectile.id, projectile.pos);
    particles.Update(dt);
}

}  // namespace game
}  // namespace mega
