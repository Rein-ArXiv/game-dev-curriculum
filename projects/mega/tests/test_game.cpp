// 디펜스 게임 로직 검증: 타워 사격·적 진격·경제·웨이브·승패.
#include "game/game.hpp"
#include "test_util.h"

#include <vector>

using namespace mega;
using namespace mega::game;
using math::Vec2;

static std::vector<WaveDef> SmallWaves() {
    // 약한 적 2웨이브.
    return {
        WaveDef{3, 10.0f, 25.0f, 5, 0.3f},
        WaveDef{4, 12.0f, 25.0f, 6, 0.3f},
    };
}

// 강한 타워를 스폰-코어 경로에 두면 모든 웨이브를 막고 승리한다.
static void TestWinWithTowers() {
    GameWorld g;
    g.Init(Vec2{200, 200}, Vec2{200, 40}, SmallWaves(), 200, 100);
    // 경로 중간에 강력한 타워 2기.
    CHECK(g.PlaceTower(Vec2{200, 100}, 120, 20, 0.2f, 50));
    CHECK(g.PlaceTower(Vec2{200, 140}, 120, 20, 0.2f, 50));

    for (int i = 0; i < 3600 && !g.Won() && !g.Lost(); ++i) g.Step(1.0f / 60.0f);

    CHECK(g.Won());
    CHECK(!g.Lost());
    CHECK(g.CoreHp() > 0.0f);
    CHECK(g.Gold() > 100);          // 적 처치 보상 누적
}

// 타워가 없으면 적이 코어를 부수고 패배한다.
static void TestLoseWithoutTowers() {
    GameWorld g;
    g.Init(Vec2{200, 200}, Vec2{200, 40}, SmallWaves(), 0, 50);
    for (int i = 0; i < 3600 && !g.Won() && !g.Lost(); ++i) g.Step(1.0f / 60.0f);
    CHECK(g.Lost());
    CHECK(!g.Won());
    CHECK(g.CoreHp() <= 0.0f);
}

// 경제: 골드 부족 시 배치 실패.
static void TestEconomy() {
    GameWorld g;
    g.Init(Vec2{200, 200}, Vec2{200, 40}, SmallWaves(), 60, 100);
    CHECK(g.PlaceTower(Vec2{200, 100}, 100, 10, 0.5f, 50));   // 60→10
    CHECK_EQ(g.Gold(), 10);
    CHECK(!g.PlaceTower(Vec2{200, 120}, 100, 10, 0.5f, 50));  // 10 < 50 → 실패
    CHECK_EQ(g.TowerCount(), 1);
}

// 웨이브 진행: 1웨이브 클리어 후 2웨이브로.
static void TestWaveProgression() {
    GameWorld g;
    g.Init(Vec2{200, 200}, Vec2{200, 40}, SmallWaves(), 300, 10000);  // 코어 매우 튼튼
    g.PlaceTower(Vec2{200, 100}, 150, 30, 0.15f, 50);
    g.PlaceTower(Vec2{200, 150}, 150, 30, 0.15f, 50);

    CHECK_EQ(g.Wave(), 1);
    bool reachedWave2 = false;
    for (int i = 0; i < 3600 && !g.Won(); ++i) {
        g.Step(1.0f / 60.0f);
        if (g.Wave() >= 2) reachedWave2 = true;
    }
    CHECK(reachedWave2);            // 2웨이브에 도달했었다
    CHECK(g.Won());                 // 결국 클리어
}

// 적이 실제로 코어를 향해 전진한다(타워 없이 한동안).
static void TestEnemyAdvances() {
    GameWorld g;
    g.Init(Vec2{200, 200}, Vec2{200, 40}, {WaveDef{1, 10, 20, 5, 0.1f}}, 0, 1000);
    g.Step(1.0f / 60.0f);           // 1마리 스폰
    CHECK_EQ(g.AliveEnemies(), 1);
    float y0 = g.Enemies().Pos(0).y;
    for (int i = 0; i < 60; ++i) g.Step(1.0f / 60.0f);
    // 1초 후 코어(y=200) 쪽으로 y 증가.
    CHECK(g.Enemies().Size() == 0 || g.Enemies().Pos(0).y > y0);
}

static void TestValidatedTileMapAndPlacement() {
    std::vector<std::string> rows{
        "..S.",
        "..P.",
        ".PP.",
        ".C..",
    };
    DefenseMap map;
    CHECK(DefenseMap::FromAscii(rows, 20, 10, map));
    GameWorld g;
    g.Init(map, {WaveDef{1, 10, 20, 5, 0.1f}}, 500, 100);
    CHECK(!g.PlaceTowerType(map.CellCenter(2, 1), TowerType::Cannon));
    CHECK(g.PlaceTowerType(map.CellCenter(0, 0), TowerType::Cannon));
    CHECK(!g.PlaceTowerType(map.CellCenter(0, 0), TowerType::Gatling));

    // Branching path and disconnected extra path tiles are rejected.
    CHECK(!DefenseMap::FromAscii({".P.", "SPC", "..."}, 20, 10, map));
    CHECK(!DefenseMap::FromAscii({"SPC", "##P"}, 20, 10, map));
}

static void RunTests() {
    TestWinWithTowers();
    TestLoseWithoutTowers();
    TestEconomy();
    TestWaveProgression();
    TestEnemyAdvances();
    TestValidatedTileMapAndPlacement();
}

TEST_MAIN("game")
