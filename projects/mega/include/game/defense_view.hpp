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

#include "containers/intrusive_list.h"
#include "engine/particles.hpp"
#include "engine/resource.hpp"
#include "game/boss_rig.hpp"
#include "game/camera.hpp"
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
//
// 12편까지 이 구조체는 스프라이트 열 장을 **값으로** 들었다. 그 수명은 "시작할 때
// 한 번 읽고 프로세스가 끝날 때까지 유지"였고, 그 전제는 매치 중 리로드('L' 키와
// 비동기 리로더)가 생기는 순간 성립하지 않았다 — 리로드는 열 장을 통째로 덮어쓰고,
// 그 순간 옛 픽셀을 가리키던 참조가 있었다면 그것은 이미 사라진 메모리를 가리킨다.
// 값 소유는 그 사고를 **표현할 수단조차 주지 않는다**(옛 스프라이트에 이름이 없다).
//
// 그래서 소유를 engine/resource.hpp 의 ResourcePool + 세대 핸들로 바꾼다.
//
//   세대(generation)  슬롯이 회수·재사용되면 세대가 오른다. 옛 핸들은 인덱스가
//                     같아도 세대가 달라 IsValid 에서 걸린다.
//   레퍼런스 카운트   교체 중에도 누군가 쥐고 있으면 옛 스프라이트가 살아 있고,
//                     마지막 참조가 사라질 때만 슬롯이 회수된다.
//   상주/대기 목록    지금 그려지는 것과 이번 리로드로 들어올 것을 intrusive
//                     목록 둘로 잇는다. 커밋은 그 둘 사이의 교환 한 번이다.
struct DefenseSkin {
    static constexpr int kFileCount = 10;
    // 리로드 중에는 옛 열 장과 새 열 장이 동시에 산다. 그것이 이 상한의 이유다.
    static constexpr size_t kMaxSlots = static_cast<size_t>(kFileCount) * 2 + 2;

    int externalAssets = 0;

    // 슬롯 번호 ↔ 파일 이름은 한 곳에만 있어야 한다. 동기 로드와 비동기 리로드가
    // 각자 목록을 들면 한쪽만 파일이 늘어나 조용히 다른 스킨이 된다.
    static const char* FileName(int slot) {
        static const char* names[kFileCount] = {
            "tower_cannon.ppm", "tower_gatling.ppm", "tower_splash.ppm", "tower_sniper.ppm",
            "enemy_normal.ppm", "enemy_fast.ppm", "enemy_tank.ppm", "enemy_swarm.ppm",
            "enemy_boss.ppm", "core.ppm",
        };
        return (slot >= 0 && slot < kFileCount) ? names[slot] : "";
    }

    // ── 읽기 ───────────────────────────────────────────────────────
    static size_t TowerCount() { return 4; }
    static size_t EnemyCount() { return 5; }
    const render::Sprite& Tower(size_t index) const { return At(static_cast<int>(index)); }
    const render::Sprite& Enemy(size_t index) const { return At(static_cast<int>(index) + 4); }
    const render::Sprite& Core() const { return At(9); }

    // 슬롯이 지금 가리키는 핸들. 이것을 들고 있는 것만으로는 수명이 연장되지
    // 않는다 — 연장하려면 Acquire 로 참조를 하나 더 잡아야 한다.
    engine::Handle SlotHandle(int slot) const {
        return (slot >= 0 && slot < kFileCount) ? live_[static_cast<size_t>(slot)]
                                                : engine::Handle{};
    }
    // 참조를 하나 잡는다. 리로드가 슬롯을 갈아 끼워도 이 핸들이 가리키는 픽셀은
    // 살아 있다. Release 로 놓기 전까지 슬롯은 회수되지 않는다.
    engine::Handle Acquire(int slot) {
        const engine::Handle handle = SlotHandle(slot);
        pool_.AddRef(handle);
        return handle;
    }
    void Release(engine::Handle handle) { pool_.Release(handle); }
    const render::Sprite* Resolve(engine::Handle handle) const {
        return const_cast<engine::ResourcePool<render::Sprite>&>(pool_).Get(handle);
    }
    bool IsValid(engine::Handle handle) const { return pool_.IsValid(handle); }
    int  RefCount(engine::Handle handle) const { return pool_.RefCount(handle); }
    size_t AliveResources() const { return pool_.AliveCount(); }
    size_t ResidentCount() const { return resident_.Size(); }
    size_t PendingCount() const { return pending_.Size(); }

    // ── 쓰기 ───────────────────────────────────────────────────────
    // 리로드는 세 단계다. 스테이징이 끝나기 전에는 화면이 보는 것이 하나도
    // 바뀌지 않는다 — 절반만 갈린 스킨이 한 프레임이라도 그려지지 않게 하는 것이
    // 대기 목록의 존재 이유다.
    // snippet-begin: skin-commit
    void BeginReload() { DrainPending(); }

    void Stage(int slot, render::Sprite&& sprite) {
        if (slot < 0 || slot >= kFileCount) return;
        DropPending(slot);                       // 같은 슬롯을 두 번 스테이징
        const engine::Handle handle = pool_.Load(sprite);
        Node* node = NodeOf(handle);
        if (!node) { pool_.Release(handle); return; }
        node->handle = handle;
        node->slot = slot;
        pending_.PushBack(node);
    }

    void CommitReload() {
        while (Node* node = pending_.Front()) {
            pending_.Remove(node);
            const size_t slot = static_cast<size_t>(node->slot);
            if (Node* previous = residentOf_[slot]) {
                resident_.Remove(previous);
                pool_.Release(previous->handle);   // 마지막 참조면 여기서 회수된다
                previous->slot = -1;
            }
            live_[slot] = node->handle;
            residentOf_[slot] = node;
            resident_.PushBack(node);
        }
    }
    // snippet-end: skin-commit

    // 옛 이름. 스테이징 하나로 남겨 둔다 — 비동기 리로더가 슬롯마다 부른다.
    void Apply(int slot, render::Sprite&& sprite) {
        Stage(slot, std::move(sprite));
        ++externalAssets;
    }

    void BuildFallbacks() {
        render::Sprite hero = render::HeroMask();
        Stage(0, render::Tint(hero, {235, 150, 40, 255}));
        Stage(1, render::Tint(hero, {60, 200, 220, 255}));
        Stage(2, render::Tint(hero, {190, 90, 230, 255}));
        Stage(3, render::Tint(hero, {80, 230, 110, 255}));
        render::Sprite monster = render::MonsterMask();
        Stage(4, render::Tint(monster, {220, 75, 75, 255}));
        Stage(5, render::Tint(monster, {240, 225, 60, 255}));
        Stage(6, render::Tint(monster, {125, 95, 175, 255}));
        Stage(7, render::Tint(monster, {240, 95, 105, 255}));
        Stage(8, render::Tint(monster, {205, 55, 190, 255}));
        Stage(9, render::MakeSprite({
            "..C..", ".CCC.", "CCWCC", ".CCC.", "..C..",
        }, {{'C', {150, 210, 255, 255}}, {'W', {255, 255, 255, 255}}}));
    }

    void Load(const std::string& root) {
        BeginReload();
        BuildFallbacks();
        externalAssets = 0;
        for (int slot = 0; slot < kFileCount; ++slot) {
            render::Sprite loaded;
            if (render::LoadPpmP3(root + "/" + FileName(slot), loaded))
                Apply(slot, std::move(loaded));
        }
        CommitReload();
    }

private:
    struct Node {
        ListHook       residentHook;
        ListHook       pendingHook;
        engine::Handle handle;
        int            slot = -1;
    };

    Node* NodeOf(engine::Handle handle) {
        return handle.index < kMaxSlots ? &nodes_[handle.index] : nullptr;
    }

    // 커밋되지 않은 스테이징을 버린다. 프레임 중간에 리로드가 중단되면(파일이
    // 사라졌다든가) 이 목록이 남고, 남은 채로 다음 리로드를 시작하면 그때
    // 커밋되어 두 세대가 섞인 스킨이 된다.
    void DrainPending() {
        while (Node* node = pending_.Front()) {
            pending_.Remove(node);
            pool_.Release(node->handle);
            node->slot = -1;
        }
    }
    void DropPending(int slot) {
        for (Node* node = pending_.First(); node; node = pending_.Next(node)) {
            if (node->slot != slot) continue;
            pending_.Remove(node);
            pool_.Release(node->handle);
            node->slot = -1;
            return;
        }
    }

    const render::Sprite& At(int slot) const {
        static const render::Sprite kEmpty;
        const render::Sprite* sprite = Resolve(SlotHandle(slot));
        return sprite ? *sprite : kEmpty;
    }

    engine::ResourcePool<render::Sprite>     pool_;
    std::array<Node, kMaxSlots>              nodes_{};
    std::array<engine::Handle, kFileCount>   live_{};
    std::array<Node*, kFileCount>            residentOf_{};
    IntrusiveList<Node, &Node::residentHook> resident_;
    IntrusiveList<Node, &Node::pendingHook>  pending_;
};

// HUD 가 알아야 하는 프레임별 상태. DrawGame 의 인자를 늘리지 않으려고 묶었다.
struct HudState {
    TowerType   selectedType = TowerType::Cannon;
    uint32_t    selectedTower = 0;
    int         mouseX = -100, mouseY = -100;   // 화면 좌표
    math::Vec2  mouseWorld{-100.0f, -100.0f};   // 카메라가 푼 지면 좌표
    bool        mouseOnGround = false;
    const char* modeLabel = "SOLO";
    bool        paused = false;
    // 보스를 스프라이트로 그릴 것인가 리그로 풀 것인가. 리그 팔은 포즈 묶음을
    // 필요로 하고, 그것이 없으면 스프라이트로 떨어진다(표시가 멈추지는 않는다).
    BossRenderKind    bossRender = BossRenderKind::Sprite;
    const BossPoses*  poses = nullptr;
};

// 카메라가 주어지지 않은 호출자(옛 테스트·헤드리스 도구)를 위한 항등 카메라.
inline const DefenseCamera& IdentityCamera() {
    static const DefenseCamera camera;   // 기본 구성이 ortho2d = 항등
    return camera;
}

// 타워 하나의 표시 변환. 요는 스냅샷 안에서 가장 가까운 적을 향한다 — 권위
// 상태가 아니라 보이는 것에서 나오므로 호스트와 클라이언트가 같은 각을 얻는다.
// snippet-begin: tower-facing
inline float TowerFacing(const wire::Snapshot& view, const math::Vec2& towerPos) {
    const math::Vec2* nearest = nullptr;
    float best = 0.0f;
    for (const auto& enemy : view.enemies) {
        const float d = math::LengthSq(enemy.pos - towerPos);
        if (!nearest || d < best) { best = d; nearest = &enemy.pos; }
    }
    if (!nearest) return 0.0f;
    const math::Vec2 d = *nearest - towerPos;
    return math::Degrees(std::atan2(d.x, d.y)) * -1.0f;   // 로컬 +y 가 총구 방향
}

inline math::Transform TowerTransform(const wire::TowerState& tower,
                                      const wire::Snapshot& view, float cellSize) {
    const float scale = cellSize * (0.35f + 0.06f * static_cast<float>(tower.tier));
    return EntityTransform(tower.pos, TowerFacing(view, tower.pos), scale);
}
// snippet-end: tower-facing

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
    const EnemyStore& enemies = world.Enemies();
    const size_t enemyRows = enemies.Size();
    for (size_t i = 0; i < enemyRows; ++i) {
        if (!enemies.Alive(i)) continue;
        view.enemies.push_back({enemies.Id(i), enemies.Pos(i), enemies.Hp(i),
                                enemies.MaxHp(i), enemies.Type(i), world.Revealed(i)});
    }
    world.Projectiles().ForEach([&view](const Projectile& source) {
        view.projectiles.push_back({source.id, source.pos});
    });
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

// 카메라를 지나는 배치. 기울인 시점에서는 위치가 투영되고 크기가 거리에 따라
// 준다. ortho2d 팔에서 Project 는 항등이고 ScaleAt 은 1 이라, 위 함수를 그대로
// 부른 것과 **비트 동일한** 그림이 나온다.
// snippet-begin: blit-through-camera
inline void BlitThroughCamera(render::Framebuffer& framebuffer, const DefenseCamera& camera,
                              const render::Sprite& sprite, const math::Vec2& world, int scale) {
    bool behind = false;
    const math::Vec2 screen = camera.Project(world, &behind);
    if (behind) return;
    const float zoom = camera.ScaleAt(world);
    const int scaled = std::max(1, static_cast<int>(std::lround(scale * zoom)));
    BlitCentered(framebuffer, sprite, screen, scaled);
}
// snippet-end: blit-through-camera

// 보스 한 마리를 절차 포즈로 그린다. 스프라이트 블릿 한 줄이 하던 일을 몸통
// 삼각형 하나와 다리 여덟 선분이 대신한다.
//
// 입력이 스냅샷의 위치와 맵의 코어 위치뿐이라는 점이 중요하다 — 호스트가 그리든
// 클라이언트가 그리든 같은 다리가 나온다. 발끝 표식은 그냥 장식이 아니라 그 틱의
// **스톰프 원점**이라, 화면에서 본 자리와 피해를 받은 자리가 같다.
// snippet-begin: draw-boss-rig
inline void DrawBossRig(render::Framebuffer& framebuffer, const DefenseCamera& camera,
                        const BossFrame& frame, float scale) {
    auto screen = [&camera](const math::Vec2& world) {
        const math::Vec2 p = camera.Project(world);
        return math::Vec2{p.x, p.y};
    };
    auto ix = [](float v) { return static_cast<int>(std::lround(v)); };
    const render::Color limb{170, 60, 210, 255};
    const render::Color shell{205, 55, 190, 255};

    for (int leg = 0; leg < kBossLegCount; ++leg) {
        const math::Vec2 hip = screen(frame.legs[leg].hip);
        const math::Vec2 knee = screen(frame.legs[leg].knee);
        const math::Vec2 foot = screen(frame.legs[leg].foot);
        render::DrawLine(framebuffer, ix(hip.x), ix(hip.y), ix(knee.x), ix(knee.y), limb);
        render::DrawLine(framebuffer, ix(knee.x), ix(knee.y), ix(foot.x), ix(foot.y), limb);
        // 디딘 다리의 발만 채운다. 나머지는 떠 있는 발이라 점만 남긴다.
        const int radius = leg == frame.plantedLeg ? 3 : 1;
        render::FillCircle(framebuffer, ix(foot.x), ix(foot.y), radius,
                           frame.legs[leg].reachable ? limb : shell);
    }

    // 몸통. 진행 방향으로 뾰족한 삼각형이라 어느 쪽을 향하는지가 읽힌다.
    const math::Vec2 right{frame.forward.y, -frame.forward.x};
    const math::Vec2 nose = screen(frame.body + frame.forward * (scale * 0.55f));
    const math::Vec2 left = screen(frame.body - frame.forward * (scale * 0.35f) - right * (scale * 0.40f));
    const math::Vec2 rightCorner =
        screen(frame.body - frame.forward * (scale * 0.35f) + right * (scale * 0.40f));
    render::FillTriangle(framebuffer, ix(nose.x), ix(nose.y), ix(left.x), ix(left.y),
                         ix(rightCorner.x), ix(rightCorner.y), shell);
}
// snippet-end: draw-boss-rig

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

// 타워 선택 판정. 반지름 비교가 아니라 **표시 변환의 역**으로 로컬 공간에 내려
// 단위 상자와 비교한다. 티어가 오르면 몸통이 커지고, 커진 만큼 집기 쉬워야 한다 —
// 반지름 상수 하나로는 그 관계가 표현되지 않는다.
// snippet-begin: tower-at
inline const wire::TowerState* TowerAt(const wire::Snapshot& view,
                                       const math::Vec2& position, float radius) {
    for (const auto& tower : view.towers) {
        const math::Transform body = EntityTransform(tower.pos, 0.0f, radius);
        if (TransformContains(body, position)) return &tower;
    }
    return nullptr;
}
// snippet-end: tower-at

// 타일 하나의 색. 절차 맵에서는 밀도장이 남긴 명암이 섞이고 광맥은 금빛이 돈다.
inline render::Color TileColor(const DefenseMap& map, int x, int y) {
    render::Color color{36, 58, 48, 255};
    switch (map.Tile(x, y)) {
        case TileType::Buildable: color = ((x + y) & 1) ? render::Color{38, 65, 51, 255} : render::Color{34, 59, 47, 255}; break;
        case TileType::Path:      color = {83, 79, 72, 255}; break;
        case TileType::Spawn:     color = {90, 72, 105, 255}; break;
        case TileType::Core:      color = {58, 88, 120, 255}; break;
        case TileType::Blocked:   color = {27, 34, 37, 255}; break;
    }
    if (!map.Generated()) return color;
    auto clamp8 = [](int v) -> uint8_t { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); };
    const int shade = (static_cast<int>(map.Shade(x, y)) - 128) / 6;
    color = {clamp8(color.r + shade), clamp8(color.g + shade), clamp8(color.b + shade), color.a};
    if (map.Vein(x, y)) color = {clamp8(color.r + 55), clamp8(color.g + 40), clamp8(color.b - 10), color.a};
    if (map.Occupied(x, y)) color = {clamp8(color.r + 18), clamp8(color.g + 14), clamp8(color.b + 10), color.a};
    return color;
}

// 지면 쿼드 하나를 카메라를 지나 그린다. 기울인 시점에서 타일은 더 이상
// 축 정렬 사각형이 아니므로 FillRect 로는 그릴 수 없다 — 네 꼭짓점을 투영해
// 삼각형 둘로 채운다.
// snippet-begin: draw-tile-quad
inline void DrawTileQuad(render::Framebuffer& framebuffer, const DefenseCamera& camera,
                         const DefenseMap& map, int x, int y, render::Color color) {
    const float cell = static_cast<float>(map.CellSize());
    const float px = static_cast<float>(x) * cell;
    const float py = static_cast<float>(map.OriginY()) + static_cast<float>(y) * cell;
    bool behind = false;
    math::Vec2 corner[4] = {
        camera.Project({px, py}, &behind),
        camera.Project({px + cell, py}, &behind),
        camera.Project({px + cell, py + cell}, &behind),
        camera.Project({px, py + cell}, &behind),
    };
    if (behind) return;
    auto ix = [](float v) { return static_cast<int>(std::lround(v)); };
    render::FillTriangle(framebuffer, ix(corner[0].x), ix(corner[0].y),
                         ix(corner[1].x), ix(corner[1].y),
                         ix(corner[2].x), ix(corner[2].y), color);
    render::FillTriangle(framebuffer, ix(corner[0].x), ix(corner[0].y),
                         ix(corner[2].x), ix(corner[2].y),
                         ix(corner[3].x), ix(corner[3].y), color);
}
// snippet-end: draw-tile-quad

inline void DrawMap(render::Framebuffer& framebuffer, const DefenseMap& map,
                    const DefenseCamera& camera = IdentityCamera()) {
    framebuffer.Clear({14, 18, 23, 255});
    for (int y = 0; y < map.Rows(); ++y) {
        for (int x = 0; x < map.Columns(); ++x) {
            const render::Color color = TileColor(map, x, y);
            if (camera.Tilted()) {
                DrawTileQuad(framebuffer, camera, map, x, y, color);
                continue;
            }
            int px = x * map.CellSize();
            int py = map.OriginY() + y * map.CellSize();
            render::FillRect(framebuffer, px, py, map.CellSize(), map.CellSize(), color);
            render::DrawRectOutline(framebuffer, px, py, map.CellSize(), map.CellSize(), {22, 29, 30, 90});
        }
    }
}

// 스프라이트가 없는 두 타워. 레이더는 자기 시야 원뿔을 그린다 — 규칙이
// 화면에 없으면 플레이어는 은신 개체가 왜 조준되는지 알 수 없다. 원뿔의
// 방향은 클라이언트가 **다시 계산한다**: 배치 순간 스폰 쪽으로 고정되므로
// 맵과 타워 위치만 있으면 같은 값이 나오고, 와이어에 실을 필요가 없다.
// snippet-begin: draw-support-tower
inline void DrawSupportTower(render::Framebuffer& framebuffer, const DefenseCamera& camera,
                             const wire::TowerState& tower, const DefenseMap& map) {
    const math::Vec2 screen = camera.Project(tower.pos);
    const int cx = static_cast<int>(screen.x), cy = static_cast<int>(screen.y);
    if (tower.type == TowerType::Frost) {
        render::FillCircle(framebuffer, cx, cy, 8, {110, 200, 235, 255});
        render::DrawRectOutline(framebuffer, cx - 8, cy - 8, 17, 17, {225, 250, 255, 200});
        return;
    }
    render::FillCircle(framebuffer, cx, cy, 6, {120, 225, 150, 255});
    const DefenseMap::Cell spawnCell = map.SpawnCell();
    const math::Vec2 spawn = map.CellCenter(spawnCell.x, spawnCell.y);
    math::Vec2 facing = spawn - tower.pos;
    facing = math::LengthSq(facing) > 1e-4f ? math::Normalize(facing) : math::Vec2{1.0f, 0.0f};
    const float half = kRadarFovDegrees * 0.5f * 3.14159265f / 180.0f;
    const float reach = kRadarRangeCells * static_cast<float>(map.CellSize());
    for (int side = -1; side <= 1; side += 2) {
        const float c = std::cos(half * static_cast<float>(side));
        const float s = std::sin(half * static_cast<float>(side));
        const math::Vec2 edge{facing.x * c - facing.y * s, facing.x * s + facing.y * c};
        const math::Vec2 tip = camera.Project(tower.pos + edge * reach);
        render::DrawLine(framebuffer, cx, cy, static_cast<int>(tip.x), static_cast<int>(tip.y),
                         {120, 225, 150, 70});
    }
}
// snippet-end: draw-support-tower

inline void DrawGame(render::Framebuffer& framebuffer, const DefenseMap& map,
                     const wire::Snapshot& view, const DefenseSkin& skin,
                     const HudState& hud, bool connected,
                     const engine::ParticleSystem* particles = nullptr,
                     const DefenseCamera& camera = IdentityCamera()) {
    (void)connected;
    DrawMap(framebuffer, map, camera);

    math::Vec2 corePosition = map.Path().back();
    BlitThroughCamera(framebuffer, camera, skin.Core(), corePosition, 3);
    float coreFraction = view.coreMaxHp > 0 ? view.coreHp / view.coreMaxHp : 0;
    const math::Vec2 coreScreen = camera.Project(corePosition);
    render::DrawRectOutline(framebuffer, static_cast<int>(coreScreen.x) - 11,
                            static_cast<int>(coreScreen.y) - 11, 23, 23,
                            HpColor(coreFraction));

    for (const auto& tower : view.towers) {
        size_t index = static_cast<size_t>(tower.type);
        // 빙결·레이더에는 저작된 스프라이트가 없다. 열 장짜리 에셋 목록을
        // 늘리는 대신 절차로 그린다 — 보스 리그가 스프라이트 없이 그려지는 것과
        // 같은 선택이고, 레이더는 **시야 원뿔 자체가 그림**이라 스프라이트로는
        // 그 정보를 낼 수 없다.
        if (index >= skin.TowerCount()) {
            DrawSupportTower(framebuffer, camera, tower, map);
            continue;
        }
        BlitThroughCamera(framebuffer, camera, skin.Tower(index), tower.pos, 2);
        // 포탑은 몸통 변환의 자식이다. 부모 * 자식 합성이 총구의 월드 좌표를 내고,
        // 그 선분이 타워가 지금 어디를 겨누는지를 화면에 남긴다.
        const math::Transform body =
            TowerTransform(tower, view, static_cast<float>(map.CellSize()));
        const math::Transform muzzle = body * BarrelTransform(1.0f);
        const math::Vec2 from = camera.Project(tower.pos);
        const math::Vec2 to = camera.Project({muzzle.position.x, muzzle.position.y});
        render::DrawLine(framebuffer, static_cast<int>(from.x), static_cast<int>(from.y),
                         static_cast<int>(to.x), static_cast<int>(to.y), {225, 215, 190, 210});
        int half = 7 + tower.tier;
        if (tower.id == hud.selectedTower)
            render::DrawRectOutline(framebuffer, static_cast<int>(from.x) - half,
                                    static_cast<int>(from.y) - half,
                                    half * 2 + 1, half * 2 + 1, {255, 255, 255, 255});
    }

    for (const auto& enemy : view.enemies) {
        size_t index = static_cast<size_t>(enemy.type);
        // 은신 개체도 스프라이트가 없다. 드러났는가에 따라 색이 갈리는 것이
        // 이 개체의 전부라, 그림 자체가 규칙의 표시다.
        if (index >= skin.EnemyCount()) {
            const math::Vec2 screen = camera.Project(enemy.pos);
            const render::Color body = enemy.revealed ? render::Color{215, 120, 255, 255}
                                                      : render::Color{70, 55, 95, 255};
            render::FillCircle(framebuffer, static_cast<int>(screen.x),
                               static_cast<int>(screen.y), 4, body);
            if (enemy.revealed)
                render::DrawRectOutline(framebuffer, static_cast<int>(screen.x) - 6,
                                        static_cast<int>(screen.y) - 6, 13, 13,
                                        {255, 220, 120, 220});
            continue;
        }
        // 보스만 두 팔이 갈린다. 리그 팔에서는 스프라이트 블릿이 통째로 빠지고
        // 그 자리를 본 계층 + 클립 + IK 가 푼 자세가 대신한다.
        if (enemy.type == EnemyType::Boss && hud.bossRender == BossRenderKind::Rig &&
            hud.poses) {
            const BossFrame frame = SolveBossFrame(*hud.poses, enemy.pos, corePosition,
                                                   static_cast<float>(map.CellSize()));
            DrawBossRig(framebuffer, camera, frame, static_cast<float>(map.CellSize()));
        } else {
            int scale = enemy.type == EnemyType::Boss ? 3 : (enemy.type == EnemyType::Tank ? 2 : 1);
            BlitThroughCamera(framebuffer, camera, skin.Enemy(index), enemy.pos, scale);
        }
        if (enemy.hp < enemy.maxHp && enemy.maxHp > 0) {
            const math::Vec2 screen = camera.Project(enemy.pos);
            int width = enemy.type == EnemyType::Boss ? 24 : 12;
            int x = static_cast<int>(screen.x) - width / 2;
            int y = static_cast<int>(screen.y) - (enemy.type == EnemyType::Boss ? 12 : 7);
            render::FillRect(framebuffer, x, y, width, 2, {45, 35, 38, 255});
            render::FillRect(framebuffer, x, y,
                             static_cast<int>(width * std::clamp(enemy.hp / enemy.maxHp, 0.0f, 1.0f)),
                             2, HpColor(enemy.hp / enemy.maxHp));
        }
    }
    for (const auto& projectile : view.projectiles) {
        const math::Vec2 screen = camera.Project(projectile.pos);
        render::FillCircle(framebuffer, static_cast<int>(screen.x),
                           static_cast<int>(screen.y), 2, {255, 230, 80, 255});
    }
    if (particles) {
        for (const engine::Particle& particle : particles->Particles()) {
            const float alpha = std::clamp(engine::ParticleSystem::Alpha(particle), 0.0f, 1.0f);
            const math::Vec2 screen = camera.Project({particle.pos.x, particle.pos.y});
            render::FillCircle(
                framebuffer, static_cast<int>(screen.x), static_cast<int>(screen.y),
                std::max(1, static_cast<int>(particle.size)),
                {255, 170, 55, static_cast<uint8_t>(alpha * 220.0f)});
        }
    }

    // 프리뷰는 카메라가 푼 지면 좌표 위에 그린다. 화면 좌표를 그대로 월드로
    // 쓰면 기울인 시점에서 커서와 하이라이트가 어긋난다.
    if (hud.mouseOnGround && hud.mouseY >= map.OriginY()) {
        math::Vec2 snapped = map.Snap(hud.mouseWorld);
        bool valid = CanPlaceInView(map, view, hud.mouseWorld);
        render::Color preview = valid ? render::Color{100, 240, 155, 230}
                                      : render::Color{245, 90, 90, 230};
        const math::Vec2 screen = camera.Project(snapped);
        render::DrawRectOutline(framebuffer, static_cast<int>(screen.x) - 10,
                                static_cast<int>(screen.y) - 10, 21, 21, preview);
    }

    render::FillRect(framebuffer, 0, 0, kViewWidth, map.OriginY(), {7, 10, 14, 245});
    // 길이가 TowerType 의 값 수와 묶여 있어야 한다. 넷으로 두고 종류를 늘리면
    // HUD 가 배열 밖을 읽는다 — 그 상태를 실제로 만들었고, 제품이 그 자리에서
    // 죽었다(빙결·레이더를 고른 순간 selectedType 이 4·5).
    const char* towerNames[static_cast<size_t>(TowerType::Count)] = {
        "CANNON", "GATLING", "SPLASH", "SNIPER", "FROST", "RADAR"};
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
//
// 보관소가 std::unordered_map 이 아니라 개방 주소법 해시맵인 이유: 이 맵은 매
// 프레임 통째로 비우고 다시 채운다. 체이닝 맵은 그때마다 노드를 반납하고 다시
// 할당하지만, 개방 주소법은 한 연속 배열의 used 비트만 내렸다 올린다 — Clear 가
// 용량을 유지하므로 몇 프레임 뒤에는 재해시도 일어나지 않는다.
//
// 방출을 두 단계로 나눈 것도 의도다. 먼저 사라진 좌표를 프레임 스크래치에 모으고,
// 그 뒤에 방출한다. 순회 중에 맵을 건드리지 않으므로 Clear·재삽입이 방출 순서에
// 끼어들 수 없다.
// snippet-begin: impact-particles
inline void UpdateImpactParticles(
        const wire::Snapshot& view,
        OpenAddressingHashMap<uint32_t, math::Vec2>& previousProjectiles,
        engine::ParticleSystem& particles, float dt, FrameScratch& scratch) {
    const size_t previousCount = previousProjectiles.Size();
    math::Vec2* impacts = previousCount ? scratch.Alloc<math::Vec2>(previousCount) : nullptr;
    size_t vanished = 0;
    previousProjectiles.ForEach([&](uint32_t id, math::Vec2& position) {
        if (wire::FindById(view.projectiles, id)) return;
        impacts[vanished++] = position;
    });
    for (size_t k = 0; k < vanished; ++k) {
        for (int i = 0; i < 8; ++i) {
            const float angle = static_cast<float>(i) * 0.78539816339f;
            particles.Emit(
                {impacts[k].x, impacts[k].y, 0.0f},
                {std::cos(angle) * 35.0f, std::sin(angle) * 35.0f, 0.0f},
                0.25f, 1.5f);
        }
    }
    previousProjectiles.Clear();
    for (const auto& projectile : view.projectiles)
        previousProjectiles.Insert(projectile.id, projectile.pos);
    particles.Update(dt);
}
// snippet-end: impact-particles

}  // namespace game
}  // namespace mega
