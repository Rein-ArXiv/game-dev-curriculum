// 스텝 중에 죽은 발사체를 스텝 경계까지 살려 두는 규율.
//
// ProjectileSystem 은 명중한 발사체를 죽인 **직후** 그 객체를 다시 읽는다
// (impact 좌표·splash 반경·damage). 벡터 팔에서 그것이 안전한 이유는 "스텝 중에
// push_back 이 없다"는 우연이고, 풀 팔에서는 구조다 — 블록 주소가 움직이지 않고
// 죽은 블록은 live 목록에서 deferred-free 목록으로 옮겨 갔다가 EndStep 에서만
// 실제로 반납된다.
//
// 여기서 지키는 것은 그 두 문장이다: (1) 죽인 뒤에도 스텝 경계까지 주소가 유효하다,
// (2) 경계를 지나면 풀의 Live() 가 정확히 줄어든다.

#include "game/entity_store.hpp"
#include "game/game.hpp"
#include "test_util.h"

#include <chrono>
#include <cstdio>
#include <vector>

using namespace mega;
using namespace mega::game;

namespace {

Projectile Make(uint32_t id, float x) {
    Projectile p;
    p.id = id;
    p.targetId = id + 100;
    p.pos = {x, 0};
    p.target = {x, 50};
    p.damage = 3.0f + static_cast<float>(id);
    p.splash = 5.0f;
    return p;
}

void TestKilledBlockStaysReadableUntilStepEnd() {
    ProjectileStore store;
    store.Configure(BodyAllocKind::Pool, 64);
    std::vector<Projectile*> spawned;
    for (uint32_t i = 0; i < 8; ++i) spawned.push_back(store.Push(Make(i + 1, 10.0f * i)));
    for (Projectile* p : spawned) CHECK(p != nullptr);
    CHECK_EQ(store.Size(), static_cast<size_t>(8));
    CHECK_EQ(store.PoolLive(), static_cast<size_t>(8));

    // 스텝 중: 절반을 죽인다. 죽은 블록은 live 순회에서 빠지지만 주소는 살아 있다.
    for (size_t i = 0; i < spawned.size(); i += 2) store.Kill(*spawned[i]);
    CHECK_EQ(store.Size(), static_cast<size_t>(4));
    CHECK_EQ(store.DeferredCount(), static_cast<size_t>(4));
    CHECK_EQ(store.PoolLive(), static_cast<size_t>(8));    // 아직 아무것도 반납 안 됨
    for (size_t i = 0; i < spawned.size(); i += 2) {
        CHECK(store.PoolOwns(spawned[i]));
        CHECK_EQ(spawned[i]->id, static_cast<uint32_t>(i + 1));   // 내용이 그대로다
        CHECK(spawned[i]->damage == 3.0f + static_cast<float>(i + 1));
        // pos 는 블록의 첫 8바이트다. 즉시 반납하면 풀이 그 자리에 freelist 의
        // next 포인터를 써 넣으므로 이 값이 먼저 깨진다 — ProjectileSystem 이
        // 죽인 발사체에서 다시 읽는 것이 바로 이 자리(impact 좌표)다.
        CHECK(spawned[i]->pos.x == 10.0f * static_cast<float>(i));
        CHECK(spawned[i]->target.y == 50.0f);
    }
    // 순회는 살아 있는 것만, 삽입 순서로.
    std::vector<uint32_t> live;
    store.ForEach([&live](Projectile& p) { live.push_back(p.id); });
    CHECK_EQ(live.size(), static_cast<size_t>(4));
    const uint32_t expected[4] = {2, 4, 6, 8};
    for (size_t i = 0; i < live.size(); ++i) CHECK_EQ(live[i], expected[i]);

    // 스텝 경계: 여기서만 실제로 반납된다.
    store.EndStep();
    CHECK_EQ(store.DeferredCount(), static_cast<size_t>(0));
    CHECK_EQ(store.PoolLive(), static_cast<size_t>(4));
    CHECK_EQ(store.Size(), static_cast<size_t>(4));
}

// 살아남은 블록의 주소는 새 발사체가 대량으로 생겨도 움직이지 않는다.
// 벡터 팔은 이 성질이 없다 — 그래서 벡터 팔에서는 스텝 중에 포인터를 들지 않는다.
void TestPoolAddressesAreStableAcrossSpawns() {
    ProjectileStore store;
    store.Configure(BodyAllocKind::Pool, 512);
    Projectile* first = store.Push(Make(1, 0));
    Projectile* second = store.Push(Make(2, 1));
    for (uint32_t i = 3; i < 300; ++i) CHECK(store.Push(Make(i, static_cast<float>(i))) != nullptr);
    CHECK_EQ(first->id, 1u);
    CHECK_EQ(second->id, 2u);
    CHECK(store.PoolOwns(first) && store.PoolOwns(second));
    CHECK_EQ(store.PoolPeak(), static_cast<size_t>(299));
    // 상한을 넘으면 조용히 늘어나지 않고 거절한다 — 늘어나면 주소가 움직인다.
    for (uint32_t i = 300; i <= 512; ++i) store.Push(Make(i, 0));
    CHECK_EQ(store.Size(), static_cast<size_t>(512));
    CHECK(store.Push(Make(9999, 0)) == nullptr);
    CHECK_EQ(first->id, 1u);
}

// 두 팔이 같은 게임을 돌린다. 순회 순서(삽입 순)가 같아야 발사체 id 부여와
// 스냅샷 순서가 같고, 그래야 상태 해시가 붙는다.
void TestBothArmsAgree() {
    auto run = [](BodyAllocKind kind) {
        const auto begin = std::chrono::steady_clock::now();
        Switches switches;
        switches.bodyAlloc = kind;
        GameWorld world;
        world.Configure(switches);
        const DefenseMap map = DefenseMap::CreateDefault();
        world.Init(map, GenerateWaves(6), 4000, 100000.0f);
        world.PlaceTowerType(map.CellCenter(6, 1), TowerType::Gatling);
        world.PlaceTowerType(map.CellCenter(8, 2), TowerType::Splash);
        world.PlaceTowerType(map.CellCenter(10, 6), TowerType::Sniper);
        for (int i = 0; i < 3000; ++i) world.Step(1.0f / 60.0f);
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - begin).count();
        std::printf("  %-6s ticks=3000 total=%.1fms projectile_peak=%zu\n",
                    kind == BodyAllocKind::Pool ? "pool" : "vector", ms,
                    world.Projectiles().Size());
        return world.StateHash();
    };
    std::printf("[body_alloc]\n");
    const uint64_t viaVector = run(BodyAllocKind::Vector);
    const uint64_t viaPool = run(BodyAllocKind::Pool);
    CHECK_EQ(viaVector, viaPool);
}

}  // namespace

static void RunTests() {
    TestKilledBlockStaysReadableUntilStepEnd();
    TestPoolAddressesAreStableAcrossSpawns();
    TestBothArmsAgree();
}

TEST_MAIN("deferred_free")
