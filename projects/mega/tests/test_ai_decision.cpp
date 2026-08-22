// 의사결정 레이어 검증: 행동 트리(BT) + 유한 상태 기계(FSM).
#include "ai/bt/builder.hpp"
#include "ai/fsm/state_machine.hpp"
#include "game/enemy_brain.hpp"
#include "test_util.h"

using namespace mega::ai;
using bt::Status;

static void TestSelectorSequence() {
    // Selector{ Sequence{ Cond(enemyVisible), Action(attack) }, Action(patrol) }
    bt::NodePtr tree = bt::Builder()
        .Selector()
            .Sequence()
                .Condition([](bt::Blackboard& bb) { return bb.GetBool("enemyVisible"); })
                .Action([](bt::Blackboard& bb) { bb.SetBool("attacked", true); return Status::Success; })
            .End()
            .Action([](bt::Blackboard& bb) { bb.SetBool("patrolled", true); return Status::Success; })
        .End()
        .Build();

    // 적이 보이면 공격 분기.
    bt::Blackboard bb1;
    bb1.SetBool("enemyVisible", true);
    CHECK(tree->Tick(bb1) == Status::Success);
    CHECK(bb1.GetBool("attacked"));
    CHECK(!bb1.GetBool("patrolled"));

    // 적이 안 보이면 순찰 폴백.
    tree->Reset();
    bt::Blackboard bb2;
    bb2.SetBool("enemyVisible", false);
    CHECK(tree->Tick(bb2) == Status::Success);
    CHECK(!bb2.GetBool("attacked"));
    CHECK(bb2.GetBool("patrolled"));
}

static void TestInverter() {
    bt::NodePtr t = bt::Builder()
        .Inverter()
            .Condition([](bt::Blackboard& bb) { return bb.GetBool("flag"); })
        .Build();
    bt::Blackboard bb;
    bb.SetBool("flag", false);
    CHECK(t->Tick(bb) == Status::Success);     // false 를 뒤집어 Success
    bb.SetBool("flag", true);
    CHECK(t->Tick(bb) == Status::Failure);
}

static void TestSequenceMemory() {
    // 첫 tick 에 Running, 둘째에 Success 인 행동을 Sequence 가 이어가야 한다.
    int calls = 0;
    bt::NodePtr t = bt::Builder()
        .Sequence()
            .Action([](bt::Blackboard&) { return Status::Success; })
            .Action([&calls](bt::Blackboard&) {
                return (++calls < 2) ? Status::Running : Status::Success;
            })
        .End()
        .Build();
    bt::Blackboard bb;
    CHECK(t->Tick(bb) == Status::Running);     // 둘째 자식이 Running
    CHECK(t->Tick(bb) == Status::Success);     // 이어서 Success
    CHECK_EQ(calls, 2);
}

static void TestRepeater() {
    int runs = 0;
    bt::NodePtr t = bt::Builder()
        .Repeater(3)
            .Action([&runs](bt::Blackboard&) { ++runs; return Status::Success; })
        .Build();
    bt::Blackboard bb;
    // 3회 반복: 두 번 Running, 세 번째 Success.
    CHECK(t->Tick(bb) == Status::Running);
    CHECK(t->Tick(bb) == Status::Running);
    CHECK(t->Tick(bb) == Status::Success);
    CHECK_EQ(runs, 3);
}

// --- FSM ---
struct EnemyCtx {
    float distance = 100.0f;
    int   enterChase = 0, enterAttack = 0, exitChase = 0;
};

static void TestFsm() {
    fsm::StateMachine<EnemyCtx> sm;
    int patrol = sm.AddState("Patrol");
    int chase  = sm.AddState("Chase",
                             [](EnemyCtx& c) { ++c.enterChase; }, {},
                             [](EnemyCtx& c) { ++c.exitChase; });
    int attack = sm.AddState("Attack", [](EnemyCtx& c) { ++c.enterAttack; });

    sm.AddTransition(patrol, [](const EnemyCtx& c) { return c.distance < 10.0f; }, chase);
    sm.AddTransition(chase,  [](const EnemyCtx& c) { return c.distance < 2.0f; },  attack);
    sm.AddTransition(chase,  [](const EnemyCtx& c) { return c.distance > 15.0f; }, patrol);
    sm.AddTransition(attack, [](const EnemyCtx& c) { return c.distance > 2.0f; },  chase);

    EnemyCtx ctx;
    sm.Start(patrol, ctx);
    CHECK_EQ(sm.Current(), patrol);

    ctx.distance = 8.0f;  sm.Update(ctx);   // Patrol → Chase
    CHECK_EQ(sm.Current(), chase);
    CHECK_EQ(ctx.enterChase, 1);

    ctx.distance = 1.5f;  sm.Update(ctx);   // Chase → Attack
    CHECK_EQ(sm.Current(), attack);
    CHECK_EQ(ctx.enterAttack, 1);
    CHECK_EQ(ctx.exitChase, 1);

    ctx.distance = 5.0f;  sm.Update(ctx);   // Attack → Chase
    CHECK_EQ(sm.Current(), chase);
    CHECK_EQ(ctx.enterChase, 2);

    ctx.distance = 20.0f; sm.Update(ctx);   // Chase → Patrol
    CHECK_EQ(sm.Current(), patrol);

    // 멀리 있으면 Patrol 유지(전이 없음).
    ctx.distance = 50.0f; sm.Update(ctx);
    CHECK_EQ(sm.Current(), patrol);
}

// ── 잡몹 상태 기계(game/enemy_brain.hpp) ───────────────────────────
// 기계는 **한 벌**이고 상태만 개체가 든다. Resume 이 onEnter 를 부르지 않는
// 것이 그 구조의 전제라, 여기서 그 사실 자체를 단언한다.
static void TestGruntResumeDoesNotReenter() {
    mega::game::GruntBrain brain;
    mega::game::GruntCtx ctx;
    ctx.dt = 1.0f / 60.0f;
    // 전이가 없는 틱을 열 번 돌려도 진입/이탈이 한 번도 울리지 않는다.
    mega::game::GruntState state = mega::game::GruntState::Advance;
    for (int i = 0; i < 10; ++i) {
        ctx.enters = ctx.exits = 0;
        state = brain.Tick(state, ctx);
        CHECK_EQ(ctx.enters, 0u);
        CHECK_EQ(ctx.exits, 0u);
        CHECK(state == mega::game::GruntState::Advance);
    }
}

// 앞 칸이 막히면 Blocked, 풀리면 Advance. 전이마다 onExit/onEnter 가 정확히 한 번.
static void TestGruntBlocked() {
    mega::game::GruntBrain brain;
    mega::game::GruntCtx ctx;
    ctx.dt = 1.0f / 60.0f;
    mega::game::GruntState state = mega::game::GruntState::Advance;

    ctx.blocked = true;
    ctx.enters = ctx.exits = 0;
    state = brain.Tick(state, ctx);
    CHECK(state == mega::game::GruntState::Blocked);
    CHECK_EQ(ctx.enters, 1u);
    CHECK_EQ(ctx.exits, 1u);
    CHECK(ctx.attack);                    // 막힌 잡몹은 앞의 타워를 때린다
    CHECK_EQ(ctx.speedScale, 0.0f);       // 그리고 움직이지 않는다

    // 막힌 채로 더 돌아도 전이는 없다.
    ctx.enters = ctx.exits = 0;
    ctx.attack = false;
    state = brain.Tick(state, ctx);
    CHECK(state == mega::game::GruntState::Blocked);
    CHECK_EQ(ctx.enters, 0u);
    CHECK(ctx.attack);

    ctx.blocked = false;
    ctx.enters = ctx.exits = 0;
    ctx.attack = false;
    state = brain.Tick(state, ctx);
    CHECK(state == mega::game::GruntState::Advance);
    CHECK_EQ(ctx.enters, 1u);
    CHECK_EQ(ctx.exits, 1u);
    CHECK(!ctx.attack);
    CHECK_EQ(ctx.speedScale, 1.0f);
}

// **전이 순서가 규칙이다.** 빙결과 봉쇄가 동시에 참이면 빙결이 이긴다 —
// 얼어붙은 적이 타워를 때리면 빙결 타워는 아무 일도 하지 않은 것이 된다.
static void TestGruntStunBeatsBlocked() {
    mega::game::GruntBrain brain;
    mega::game::GruntCtx ctx;
    ctx.dt = 1.0f / 60.0f;
    ctx.blocked = true;
    ctx.stun = mega::game::kFrostStun;
    mega::game::GruntState state = brain.Tick(mega::game::GruntState::Advance, ctx);
    CHECK(state == mega::game::GruntState::Stunned);
    CHECK(!ctx.attack);
    CHECK_EQ(ctx.speedScale, 0.0f);

    // 빙결이 풀리면 Advance 로 돌아온다(막혀 있어도 그 다음 틱에 Blocked 로 간다).
    ctx.stun = 0.0f;
    ctx.attack = false;
    state = brain.Tick(state, ctx);
    CHECK(state == mega::game::GruntState::Advance);
    state = brain.Tick(state, ctx);
    CHECK(state == mega::game::GruntState::Blocked);
}

// 오래 살아남으면 가속한다. 그 상태에서 막히면 Blocked 로 가고, 풀리면 다시
// Enraged 로 돌아온다 — Advance 가 아니다.
static void TestGruntEnrage() {
    mega::game::GruntBrain brain;
    mega::game::GruntCtx ctx;
    ctx.dt = 1.0f / 60.0f;
    ctx.age = mega::game::kEnrageAfter - 0.01f;
    mega::game::GruntState state = brain.Tick(mega::game::GruntState::Advance, ctx);
    CHECK(state == mega::game::GruntState::Advance);

    ctx.age = mega::game::kEnrageAfter;
    state = brain.Tick(state, ctx);
    CHECK(state == mega::game::GruntState::Enraged);
    CHECK_EQ(ctx.speedScale, mega::game::kEnrageSpeed);

    ctx.blocked = true;
    state = brain.Tick(state, ctx);
    CHECK(state == mega::game::GruntState::Blocked);
    ctx.blocked = false;
    state = brain.Tick(state, ctx);
    CHECK(state == mega::game::GruntState::Enraged);
}

static void RunTests() {
    TestSelectorSequence();
    TestGruntResumeDoesNotReenter();
    TestGruntBlocked();
    TestGruntStunBeatsBlocked();
    TestGruntEnrage();
    TestInverter();
    TestSequenceMemory();
    TestRepeater();
    TestFsm();
}

TEST_MAIN("ai_decision")
