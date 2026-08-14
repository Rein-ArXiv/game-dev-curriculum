// 의사결정 레이어 검증: 행동 트리(BT) + 유한 상태 기계(FSM).
#include "ai/bt/builder.hpp"
#include "ai/fsm/state_machine.hpp"
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

static void RunTests() {
    TestSelectorSequence();
    TestInverter();
    TestSequenceMemory();
    TestRepeater();
    TestFsm();
}

TEST_MAIN("ai_decision")
