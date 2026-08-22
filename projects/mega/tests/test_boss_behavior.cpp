// 정예의 행동을 **제품 안에서** 묻는다.
//
// tests/test_ai_bt.cpp 는 노드의 계약을(Running · 정지 규칙 · 짝맞춤),
// tests/test_elite_content.cpp 는 조립기의 계약을(자료 = 코드, 거부는 파일 단위)
// 지킨다. 둘 다 통과하면서도 게임은 아무것도 달라지지 않을 수 있다 — 트리가
// 돌기는 하는데 그 출력이 세계에 닿지 않으면 그렇다. 그 구멍을 이 파일이 막는다.
//
// 이 파일이 지키는 경계 넷.
//
//   1. **켜야 생긴다.** elite_brain=plain 에서는 후퇴도 회복도 슬램도 한 번도
//      일어나지 않는다. 그러지 않으면 "새 기능이 생겼다"가 거짓이다.
//   2. **후퇴는 여러 틱에 걸친다.** 체력이 임계 아래로 떨어지면 후퇴가 한 틱이
//      아니라 kEliteRetreatTime 만큼 이어져야 한다. Running 전파가 무너지면
//      이 길이가 1로 주저앉는다.
//   3. **후퇴는 무한하지 않다.** 상한이 없으면 정예는 30%에서 물러나 55%까지
//      회복하는 고리를 끝내지 않는다 — 즉 죽지 않는다.
//   4. **두 표현이 같은 게임이다.** elite_brain=fsm 과 =bt 는 같은 행동을 상태
//      기계로 쓴 것과 트리로 쓴 것이라, 매치의 위치·체력 열이 비트 단위로 같아야
//      한다. 다르면 A/B 는 비교가 아니라 서로 다른 두 게임이다.
//
// 후퇴를 위치의 변화로 되짚지 않는 이유가 하나 있다. 굽은 회랑에서는 정상 전진
// 중에도 코어와의 직선 거리가 늘어나는 구간이 있어, "멀어졌다"와 "물러난다"가
// 구분되지 않는다. 그래서 정예 문맥(EliteCtx)을 직접 읽는다.

#include "game/game.hpp"
#include "test_util.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace mega;
using namespace mega::game;

namespace {

// 보스가 지나가는 길목의 사격 자리(셀프테스트 스크립트가 쓰는 열두 칸 중 앞의 열).
const DefenseMap::Cell kPlan[10] = {
    {6, 1}, {8, 2}, {10, 2}, {11, 4}, {13, 5}, {10, 6}, {8, 8}, {5, 8}, {3, 9}, {5, 10},
};

constexpr float kDt = 1.0f / 60.0f;
// 보스 한 마리의 기본 체력. 정예 규칙을 관측하려면 보스가 임계 아래로
// **내려가되 즉사하지는 않아야** 하고, 그 둘을 동시에 만족시키는 손잡이는
// 체력뿐이다. EnemyModFor(Boss) 가 12배를 곱하므로 실제 최대 체력은 이것의 12배다.
constexpr float kBossHp = 90.0f;
constexpr int   kTicks  = 9000;

// 한 틱의 관측. 두 팔이 같은 게임인지는 이 넷으로 판정한다.
struct Frame {
    float x = 0, y = 0, hp = 0;
    int   alive = 0;
    // 진단용. 판정에는 쓰지 않는다 — 두 팔의 내부 표현이 아니라 **세계**가
    // 같은지를 묻는 것이 이 비교의 목적이다.
    float speed = 0, retreat = 0, slam = 0, cooldown = 0, heal = 0;
    int   away = 0, retreats = 0;

    bool operator==(const Frame& o) const {
        return x == o.x && y == o.y && hp == o.hp && alive == o.alive;
    }
};

struct Run {
    std::vector<Frame> frames;
    uint32_t slams = 0;
    float    maxHp = 0;
    float    lowestHp = 0;
    int      awayTicks = 0;       // 정예가 물러나던 틱
    int      longestAwayRun = 0;  // 그중 가장 긴 연속 구간
    int      healTicks = 0;       // 정예가 회복하던 틱
    int      healEpisodes = 0;    // 회복 구간의 수 = 후퇴 횟수
    int      retreats = 0;        // 문맥이 센 후퇴 횟수(마지막 관측값)
    bool     bossGone = false;
};

int BossRow(const GameWorld& world) {
    const EnemyStore& enemies = world.Enemies();
    for (size_t i = 0; i < enemies.Size(); ++i)
        if (enemies.Type(i) == EnemyType::Boss) return static_cast<int>(i);
    return -1;
}

// 한 매치를 돌려 보스의 궤적과 정예 문맥을 뜬다. 팔만 다르고 나머지는 전부 같다.
Run Play(EliteBrainKind brain, int ticks) {
    GameWorld  world;
    DefenseMap map = DefenseMap::CreateDefault();
    Switches   switches;
    // 정예 브레인은 격자 항법 위에서만 돈다(폴리라인 팔에는 "앞 칸"이 없다).
    switches.pathing = PathingKind::AStar;
    switches.eliteBrain = brain;
    world.Configure(switches);
    world.Init(map, {WaveDef{1, kBossHp, 130.0f, 5, 0.6f, EnemyType::Boss}}, 4000, 1.0e9f);
    for (const DefenseMap::Cell& cell : kPlan)
        world.PlaceTowerType(map.CellCenter(cell.x, cell.y), TowerType::Gatling);

    Run  run;
    int  awayRun = 0;
    bool healing = false;
    for (int t = 0; t < ticks; ++t) {
        world.Step(kDt);
        const int row = BossRow(world);
        Frame frame;
        if (row >= 0) {
            const size_t i = static_cast<size_t>(row);
            frame.x = world.Enemies().Pos(i).x;
            frame.y = world.Enemies().Pos(i).y;
            frame.hp = world.Enemies().Hp(i);
            frame.alive = world.Enemies().Alive(i) ? 1 : 0;
            if (run.maxHp <= 0.0f) {
                run.maxHp = world.Enemies().MaxHp(i);
                run.lowestHp = run.maxHp;
            }
            if (frame.alive && frame.hp < run.lowestHp) run.lowestHp = frame.hp;

            const std::vector<BrainRow>& brains = world.Brains();
            if (i < brains.size() && brains[i].elite != EliteBrain::kNoSlot) {
                const EliteCtx& ctx = world.Elites().Context(brains[i].elite);
                frame.speed = ctx.speedScale;
                frame.retreat = ctx.retreat;
                frame.slam = ctx.slam;
                frame.cooldown = ctx.slamCooldown;
                frame.heal = ctx.heal;
                frame.away = ctx.away ? 1 : 0;
                frame.retreats = ctx.retreats;
                if (ctx.away) {
                    ++run.awayTicks;
                    ++awayRun;
                    if (awayRun > run.longestAwayRun) run.longestAwayRun = awayRun;
                } else {
                    awayRun = 0;
                }
                const bool nowHealing = ctx.heal > 0.0f;
                if (nowHealing) {
                    ++run.healTicks;
                    if (!healing) ++run.healEpisodes;
                }
                healing = nowHealing;
                run.retreats = ctx.retreats;
            }
        } else {
            run.bossGone = true;
            awayRun = 0;
            healing = false;
        }
        run.frames.push_back(frame);
        if (world.Won() || world.Lost()) break;
    }
    run.slams = world.Slams();
    return run;
}

// ── 1. 켜야 생긴다 ──────────────────────────────────────────────────
void TestPlainArmHasNoEliteBehaviour() {
    const Run plain = Play(EliteBrainKind::Plain, kTicks);
    CHECK(plain.maxHp > 0.0f);
    CHECK_EQ(plain.slams, 0u);
    CHECK_EQ(plain.awayTicks, 0);
    CHECK_EQ(plain.healTicks, 0);
    CHECK_EQ(plain.retreats, 0);
    // 그리고 실제로 한 판을 돌았는가. 0틱짜리 매치는 무엇이든 0 으로 통과한다.
    CHECK(plain.frames.size() > 600);
    std::printf("  plain: %zu틱 · 슬램 %u · 후퇴 %d틱 · 회복 %d틱\n",
                plain.frames.size(), plain.slams, plain.awayTicks, plain.healTicks);
}

// ── 2. 후퇴는 여러 틱에 걸친다 ──────────────────────────────────────
// kEliteRetreatTime(1.2초) = 72틱. Running 이 Success 로 새면 후퇴가 한 틱 만에
// 끝나고, 이 연속 구간이 그 자리에서 무너진다.
void TestRetreatSpansManyTicks() {
    const Run bt = Play(EliteBrainKind::Bt, kTicks);
    CHECK(bt.maxHp > 0.0f);
    // 체력이 실제로 임계 아래로 내려갔는가. 내려가지 않았다면 아래 단언들은
    // 아무것도 관측하지 못한 채 통과한다.
    CHECK(bt.lowestHp < bt.maxHp * kEliteHurtFraction);

    const int expected = static_cast<int>(kEliteRetreatTime / kDt);   // 72
    CHECK(bt.longestAwayRun >= expected - 4);
    CHECK(bt.longestAwayRun <= expected + 4);
    CHECK(bt.healTicks > 0);
    CHECK(bt.slams > 0u);
    std::printf("  bt: 최저 체력 %.1f/%.1f · 후퇴 연속 %d틱(기대 %d) · 회복 %d틱 · 슬램 %u\n",
                bt.lowestHp, bt.maxHp, bt.longestAwayRun, expected, bt.healTicks, bt.slams);
}

// ── 3. 후퇴는 무한하지 않다 ─────────────────────────────────────────
void TestRetreatIsBounded() {
    const Run bt = Play(EliteBrainKind::Bt, kTicks);
    CHECK(bt.healEpisodes >= 1);
    CHECK(bt.healEpisodes <= kEliteMaxRetreats);
    CHECK(bt.retreats <= kEliteMaxRetreats);
    // 상한이 실제로 걸렸다면 보스는 결국 사라진다(죽거나 코어에 닿거나).
    // 걸리지 않으면 매치는 실패하는 게 아니라 **끝나지 않는다**.
    CHECK(bt.bossGone);
    std::printf("  bt: 회복 구간 %d회 · 문맥 후퇴 %d회(상한 %d) · 보스 소멸 %s · %zu틱\n",
                bt.healEpisodes, bt.retreats, kEliteMaxRetreats,
                bt.bossGone ? "예" : "아니오", bt.frames.size());
}

// ── 4. 두 표현이 같은 게임이다 ──────────────────────────────────────
void TestFsmAndBtAgreeTickForTick() {
    const Run fsm = Play(EliteBrainKind::Fsm, kTicks);
    const Run bt  = Play(EliteBrainKind::Bt, kTicks);

    CHECK_EQ(fsm.frames.size(), bt.frames.size());
    size_t firstDiff = fsm.frames.size();
    for (size_t i = 0; i < fsm.frames.size() && i < bt.frames.size(); ++i)
        if (!(fsm.frames[i] == bt.frames[i])) { firstDiff = i; break; }
    CHECK_EQ(firstDiff, fsm.frames.size());
    if (firstDiff != fsm.frames.size()) {
        std::printf("  fsm/bt 가 틱 %zu 에서 갈렸다\n", firstDiff);
        const size_t from = firstDiff > 6 ? firstDiff - 6 : 0;
        for (size_t i = from; i <= firstDiff + 2 && i < fsm.frames.size() &&
                              i < bt.frames.size(); ++i) {
            const Frame& f = fsm.frames[i];
            const Frame& b = bt.frames[i];
            std::printf("   %5zu fsm hp=%9.4f sp=%.2f away=%d ret=%.3f slam=%.3f cd=%.3f "
                        "heal=%.3f n=%d | bt hp=%9.4f sp=%.2f away=%d ret=%.3f slam=%.3f "
                        "cd=%.3f heal=%.3f n=%d\n",
                        i, f.hp, f.speed, f.away, f.retreat, f.slam, f.cooldown, f.heal,
                        f.retreats, b.hp, b.speed, b.away, b.retreat, b.slam, b.cooldown,
                        b.heal, b.retreats);
        }
    }

    CHECK_EQ(fsm.slams, bt.slams);
    CHECK_EQ(fsm.healTicks, bt.healTicks);
    CHECK_EQ(fsm.awayTicks, bt.awayTicks);
    CHECK_EQ(fsm.longestAwayRun, bt.longestAwayRun);
    CHECK_EQ(fsm.healEpisodes, bt.healEpisodes);

    // 그리고 둘 다 plain 과는 **달라야** 한다. 같으면 위 비교는 "두 팔 모두
    // 아무것도 하지 않았다"를 확인한 것이다.
    const Run plain = Play(EliteBrainKind::Plain, kTicks);
    CHECK(bt.slams != plain.slams);
    CHECK(bt.healTicks != plain.healTicks);
    CHECK(bt.frames.size() != plain.frames.size() ||
          !(bt.frames.back() == plain.frames.back()));
    std::printf("  fsm=bt 일치 %zu틱 · 슬램 %u · 회복 %d틱 (plain 은 %u / %d)\n",
                fsm.frames.size(), bt.slams, bt.healTicks, plain.slams, plain.healTicks);
}

// ── 5. 슬램은 타워를 실제로 깎는다 ──────────────────────────────────
// 슬램이 세계에 닿지 않으면 위의 모든 단언은 "카운터가 올랐다"만 말한다.
void TestSlamDamagesTowers() {
    GameWorld  world;
    DefenseMap map = DefenseMap::CreateDefault();
    Switches   switches;
    switches.pathing = PathingKind::AStar;
    switches.eliteBrain = EliteBrainKind::Bt;
    world.Configure(switches);
    // 죽지 않는 보스. 스톰프가 아니라 **슬램**만 재려면 보스가 살아 있어야 한다.
    world.Init(map, {WaveDef{1, 400000.0f, 130.0f, 5, 0.6f, EnemyType::Boss}}, 4000, 1.0e9f);
    for (const DefenseMap::Cell& cell : kPlan)
        world.PlaceTowerType(map.CellCenter(cell.x, cell.y), TowerType::Gatling);
    const int placed = world.TowerCount();
    CHECK(placed > 0);

    float    lowest = kTowerBaseHp;
    uint32_t slams = 0;
    for (int t = 0; t < kTicks; ++t) {
        world.Step(kDt);
        slams = world.Slams();
        for (const Tower& tower : world.Towers())
            if (tower.hp < lowest) lowest = tower.hp;
        if (world.Won() || world.Lost()) break;
    }
    CHECK(slams > 0u);
    CHECK(lowest < kTowerBaseHp);      // 누군가 실제로 깎였다
    std::printf("  슬램 %u회 · 최저 타워 체력 %.1f/%.1f · 남은 타워 %d/%d\n",
                slams, lowest, kTowerBaseHp, world.TowerCount(), placed);
}

void RunTests() {
    TestPlainArmHasNoEliteBehaviour();
    TestRetreatSpansManyTicks();
    TestRetreatIsBounded();
    TestFsmAndBtAgreeTickForTick();
    TestSlamDamagesTowers();
}

}  // namespace

TEST_MAIN("boss_behavior")
