// Long deterministic runs: finite state, entity caps, stable IDs, wire bounds.
#include "game/defense_protocol.hpp"
#include "game/game.hpp"
#include "test_util.h"

#include <cmath>
#include <iterator>
#include <unordered_set>

using namespace mega;
using namespace mega::game;
namespace wire = mega::game::wire;

namespace {

bool Finite(const math::Vec2& value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

void CheckWorldInvariants(const GameWorld& world, uint32_t tick) {
    CHECK(std::isfinite(world.CoreHp()));
    CHECK(std::isfinite(world.CoreMaxHp()));
    CHECK(!(world.Won() && world.Lost()));
    CHECK(world.Gold() >= 0);
    // 구현이 쓰는 상수가 아니라 리터럴과 비교한다. GameWorld::kMaxTowers 와
    // 비교하면 상수가 무엇으로 바뀌든 항상 참인 항등식이 된다.
    CHECK(world.Towers().size() <= 16);

    std::unordered_set<uint32_t> ids;
    for (const Tower& tower : world.Towers()) {
        CHECK(ids.insert(tower.id).second);
        CHECK(Finite(tower.pos));
        CHECK(tower.tier >= 1 && tower.tier <= 3);
    }
    const EnemyStore& enemies = world.Enemies();
    for (size_t i = 0; i < enemies.Size(); ++i) {
        CHECK(ids.insert(enemies.Id(i)).second);
        CHECK(Finite(enemies.Pos(i)));
        CHECK(std::isfinite(enemies.Hp(i)) && std::isfinite(enemies.MaxHp(i)));
        CHECK(enemies.Hp(i) > 0.0f && enemies.Hp(i) <= enemies.MaxHp(i));
    }
    world.Projectiles().ForEach([&](const Projectile& projectile) {
        CHECK(ids.insert(projectile.id).second);
        CHECK(Finite(projectile.pos));
        CHECK(Finite(projectile.target));
    });

    const net::Bytes packet = wire::WriteSnapshot(world, tick, 0);
    CHECK(!packet.empty());
    CHECK(packet.size() <= wire::kMaxPacketBytes);
    wire::Snapshot decoded;
    CHECK(wire::ReadSnapshot(packet, decoded));
}

void TestRepeatedMatches() {
    const DefenseMap map = DefenseMap::CreateDefault();
    const DefenseMap::Cell cells[] = {
        {6, 1}, {8, 2}, {10, 2}, {11, 4}, {13, 5}, {10, 6},
        {8, 8}, {5, 8}, {3, 9}, {5, 10}, {6, 12}, {9, 13},
    };

    for (int run = 0; run < 24; ++run) {
        GameWorld world;
        world.Init(map, GenerateWaves(10 + run % 6), 1800, 300);
        for (size_t i = 0; i < std::size(cells); ++i) {
            world.PlaceTowerType(
                map.CellCenter(cells[i].x, cells[i].y),
                static_cast<TowerType>((i + run) %
                                       static_cast<size_t>(TowerType::Count)));
        }

        uint32_t tick = 0;
        for (; tick < 30000 && !world.Won() && !world.Lost(); ++tick) {
            world.Step(1.0f / 60.0f);
            if (tick % 300 == 0) CheckWorldInvariants(world, tick);
        }
        CHECK(world.Won() || world.Lost());
        CheckWorldInvariants(world, tick);
    }
}

}  // namespace

static void RunTests() {
    TestRepeatedMatches();
}

TEST_MAIN("defense_soak")
