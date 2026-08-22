// 게임 확장 검증: 타워 4종·적 4종·강화 티어.
#include "game/game.hpp"
#include "test_util.h"

#include <cmath>
#include <vector>

using namespace mega;
using namespace mega::game;
using math::Vec2;

static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

static void TestTowerSpecs() {
    TowerSpec cannon = TowerSpecFor(TowerType::Cannon);
    TowerSpec gat    = TowerSpecFor(TowerType::Gatling);
    TowerSpec splash = TowerSpecFor(TowerType::Splash);
    TowerSpec sniper = TowerSpecFor(TowerType::Sniper);

    CHECK(cannon.damage > gat.damage);          // 캐논 고데미지
    CHECK(gat.fireInterval < cannon.fireInterval); // 개틀링 연사
    CHECK(sniper.range > cannon.range);         // 스나이퍼 장거리
    CHECK(splash.splash > cannon.splash);       // 스플래시 광역
    CHECK(sniper.damage > splash.damage);
}

static void TestEnemyMods() {
    EnemyMod fast = EnemyModFor(EnemyType::Fast);
    EnemyMod tank = EnemyModFor(EnemyType::Tank);
    EnemyMod swarm = EnemyModFor(EnemyType::Swarm);

    CHECK(fast.speedMul > 1.0f); CHECK(fast.hpMul < 1.0f);    // 빠르고 약함
    CHECK(tank.hpMul > 2.0f);    CHECK(tank.speedMul < 1.0f); // 튼튼·느림
    CHECK(tank.rewardMul > 1);                                // 고보상
    CHECK(swarm.hpMul < 1.0f);                                // 약체
}

static void TestPlaceTowerType() {
    GameWorld g;
    g.Init(Vec2{200, 200}, Vec2{200, 40}, {WaveDef{1, 10, 20, 5, 0.5f}}, 300, 100);
    CHECK(g.PlaceTowerType(Vec2{200, 120}, TowerType::Sniper));   // 비용 110
    CHECK_EQ(g.Gold(), 190);
    CHECK_EQ(g.TowerCount(), 1);
    const Tower& t = g.Towers()[0];
    CHECK(t.type == TowerType::Sniper);
    CHECK(Near(t.range, 180));
    CHECK_EQ(t.tier, 1);

    // 골드 부족 시 실패.
    GameWorld g2;
    g2.Init(Vec2{200, 200}, Vec2{200, 40}, {WaveDef{1, 10, 20, 5, 0.5f}}, 50, 100);
    CHECK(!g2.PlaceTowerType(Vec2{200, 120}, TowerType::Sniper));  // 50 < 110
    CHECK_EQ(g2.TowerCount(), 0);
}

static void TestUpgrade() {
    GameWorld g;
    g.Init(Vec2{200, 200}, Vec2{200, 40}, {WaveDef{1, 10, 20, 5, 0.5f}}, 500, 100);
    g.PlaceTowerType(Vec2{200, 120}, TowerType::Cannon);   // 70 → 430
    float dmg0 = g.Towers()[0].damage;
    float rng0 = g.Towers()[0].range;

    CHECK(g.UpgradeTower(0));                  // 비용 60(tier1)
    CHECK_EQ(g.Towers()[0].tier, 2);
    CHECK(g.Towers()[0].damage > dmg0);        // 데미지↑
    CHECK(g.Towers()[0].range > rng0);         // 사거리↑
    int goldAfter1 = g.Gold();

    CHECK(g.UpgradeTower(0));                  // tier 2→3, 비용 120
    CHECK_EQ(g.Towers()[0].tier, 3);
    CHECK(g.Gold() == goldAfter1 - 120);

    CHECK(!g.UpgradeTower(0));                 // 최대 티어 3 → 실패
    CHECK_EQ(g.Towers()[0].tier, 3);
}

static void TestTankWaveStats() {
    GameWorld g;
    g.Init(Vec2{200, 200}, Vec2{200, 40},
           {WaveDef{1, 20.0f, 30.0f, 5, 0.1f, EnemyType::Tank}}, 0, 100000);
    g.Step(1.0f / 60.0f);                      // 1마리 스폰
    CHECK_EQ(g.AliveEnemies(), 1);
    const Enemy e = g.Enemies().Row(0);
    CHECK(e.type == EnemyType::Tank);
    CHECK(Near(e.maxHp, 60.0f, 0.5f));         // 20 * 3.0 = 60
    CHECK(e.speed < 30.0f);                    // 느림(0.6배)
}

// 스플래시 타워가 다수 약체(Swarm) 웨이브를 막아 승리한다(광역 피해 통합).
static void TestSplashClearsSwarm() {
    GameWorld g;
    g.Init(Vec2{200, 300}, Vec2{200, 40},
           {WaveDef{20, 8.0f, 35.0f, 3, 0.15f, EnemyType::Swarm}}, 300, 150);
    g.PlaceTowerType(Vec2{200, 150}, TowerType::Splash);
    g.PlaceTowerType(Vec2{170, 200}, TowerType::Splash);
    for (int i = 0; i < 3600 && !g.Won() && !g.Lost(); ++i) g.Step(1.0f / 60.0f);
    CHECK(g.Won());                            // 광역으로 떼를 정리
    CHECK(g.CoreHp() > 0);
}

static void TestBossEnemy() {
    EnemyMod boss = EnemyModFor(EnemyType::Boss);
    CHECK(boss.hpMul >= 10.0f);              // 거대 체력
    CHECK(boss.speedMul < 0.5f);             // 매우 느림
    CHECK(boss.damageMul >= 5.0f);           // 치명타
    CHECK(boss.rewardMul >= 10);             // 대량 보상

    GameWorld g;
    g.Init(Vec2{200, 200}, Vec2{200, 40},
           {WaveDef{1, 50.0f, 30.0f, 20, 0.1f, EnemyType::Boss}}, 0, 100000);
    g.Step(1.0f / 60.0f);
    CHECK_EQ(g.AliveEnemies(), 1);
    const Enemy e = g.Enemies().Row(0);
    CHECK(e.type == EnemyType::Boss);
    CHECK(Near(e.maxHp, 600.0f, 1.0f));      // 50 * 12 = 600
}

static void TestTargetModes() {
    Vec2 tower{0, 0}, core{0, 100};
    std::vector<Enemy> enemies(3);
    enemies[0].pos = {5, 0};   enemies[0].hp = 10; enemies[0].alive = true;    // 타워 최근접
    enemies[1].pos = {0, 80};  enemies[1].hp = 50; enemies[1].alive = true;    // 코어 최근접 + 최강
    enemies[2].pos = {20, 20}; enemies[2].hp = 30; enemies[2].alive = true;

    CHECK_EQ(SelectTarget(tower, core, enemies, 1000, TargetMode::Nearest), 0);   // A
    CHECK_EQ(SelectTarget(tower, core, enemies, 1000, TargetMode::First), 1);     // B(코어 근처)
    CHECK_EQ(SelectTarget(tower, core, enemies, 1000, TargetMode::Strongest), 1); // B(최강)

    CHECK_EQ(SelectTarget(tower, core, enemies, 10, TargetMode::Nearest), 0);     // 사거리 10 안엔 A만
    std::vector<Enemy> farOnly(1);
    farOnly[0].pos = {100, 0}; farOnly[0].alive = true;
    CHECK_EQ(SelectTarget(tower, core, farOnly, 10, TargetMode::Nearest), -1);    // 없음

    enemies[0].alive = false;
    CHECK_EQ(SelectTarget(tower, core, enemies, 1000, TargetMode::Nearest), 2);   // A 죽음 → C

    GameWorld g;
    g.Init(Vec2{200, 200}, Vec2{200, 40}, {WaveDef{1, 10, 20, 5, 0.5f}}, 300, 100);
    g.PlaceTowerType(Vec2{200, 120}, TowerType::Sniper);
    CHECK(g.SetTowerTarget(0, TargetMode::First));
    CHECK(g.Towers()[0].targetMode == TargetMode::First);
}

static void TestSellTower() {
    GameWorld g;
    g.Init(Vec2{200, 200}, Vec2{200, 40}, {WaveDef{1, 10, 20, 5, 0.5f}}, 300, 100);
    g.PlaceTowerType(Vec2{200, 120}, TowerType::Cannon);   // 70 → 230
    CHECK_EQ(g.Gold(), 230);
    CHECK_EQ(g.TowerCount(), 1);

    // 판매: 투자 70 의 70% = 49 환불.
    int refund = g.SellTower(0);
    CHECK_EQ(refund, 49);
    CHECK_EQ(g.Gold(), 279);                  // 230 + 49
    CHECK_EQ(g.TowerCount(), 0);

    // 강화 후 판매는 누적 투자 기준.
    g.PlaceTowerType(Vec2{200, 120}, TowerType::Cannon);   // 70 → 209
    g.UpgradeTower(0);                         // +60 → 149, invested 130
    int refund2 = g.SellTower(0);
    CHECK_EQ(refund2, (130 * 7) / 10);         // 91
    CHECK_EQ(g.TowerCount(), 0);

    // 잘못된 인덱스는 0.
    CHECK_EQ(g.SellTower(5), 0);
}

static void TestEndlessScaling() {
    // 후반 웨이브가 전반보다 어렵다(체력·적 수 증가).
    WaveDef early = GenerateWave(1);
    WaveDef late  = GenerateWave(11);
    CHECK(late.hp > early.hp);                 // 체력 증가
    CHECK(late.count >= early.count);          // 적 수 증가(대체로)
    CHECK(late.reward >= early.reward);        // 보상 증가

    // 5의 배수 웨이브(인덱스 4,9,14)는 보스.
    CHECK(GenerateWave(4).type == EnemyType::Boss);
    CHECK(GenerateWave(9).type == EnemyType::Boss);
    CHECK(GenerateWave(2).type != EnemyType::Boss);

    // 적 종류 로테이션(보스 아닌 웨이브).
    CHECK(GenerateWave(0).type == EnemyType::Normal);
    CHECK(GenerateWave(1).type == EnemyType::Fast);

    // GenerateWaves 길이.
    auto ws = GenerateWaves(20);
    CHECK_EQ((int)ws.size(), 20);

    // 점수: 도달 웨이브 가중 + 골드 + 클리어 보너스.
    CHECK_EQ(ComputeScore(5, 200, false), 700);
    CHECK_EQ(ComputeScore(5, 200, true), 1200);
    CHECK(ComputeScore(10, 0, false) > ComputeScore(5, 300, false));  // 멀리 갈수록 고득점
}

// 생성된 웨이브로 실제 한 판이 끝까지 굴러간다(엔드리스 콘텐츠 동작).
static void TestGeneratedRunPlays() {
    GameWorld g;
    g.Init(Vec2{240, 380}, Vec2{240, 40}, GenerateWaves(8), 600, 300);
    const TowerType layout[] = {TowerType::Sniper, TowerType::Splash, TowerType::Cannon, TowerType::Gatling};
    int slot = 0;
    for (int y = 120; y <= 320; y += 60) {
        g.PlaceTowerType(Vec2{210, (float)y}, layout[slot++ % 4]);
        g.PlaceTowerType(Vec2{270, (float)y + 30}, layout[slot++ % 4]);
    }
    int frames = 0;
    for (; frames < 12000 && !g.Won() && !g.Lost(); ++frames) g.Step(1.0f / 60.0f);
    CHECK(g.Won() || g.Lost());                // 결말에 도달(무한루프 아님)
    CHECK(g.Wave() >= 1);
}

static void RunTests() {
    TestBossEnemy();
    TestTargetModes();
    TestSellTower();
    TestEndlessScaling();
    TestGeneratedRunPlays();
    TestTowerSpecs();
    TestEnemyMods();
    TestPlaceTowerType();
    TestUpgrade();
    TestTankWaveStats();
    TestSplashClearsSwarm();
}

TEST_MAIN("game_expansion")
