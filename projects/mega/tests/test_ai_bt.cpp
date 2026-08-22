// 행동 트리: Blackboard · Running · Composite 정지 규칙 · Builder 짝맞춤.
//
// 이 파일이 지키는 경계 하나: **Running 이 여러 틱에 걸친 행동을 표현한다.**
// Running 이 Success 로 새면 후퇴·회복·충전이 전부 한 틱 만에 끝나고, 그때
// 행동 트리는 조건문 더미와 구분되지 않는다.
#include "ai/bt/builder.hpp"
#include "ai/bt/node.hpp"
#include "ai/bt/tick.hpp"
#include "test_util.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace mega::ai;
using bt::Status;
using mega::math::Vec2;

// ── Blackboard ─────────────────────────────────────────────────────
static void TestBlackboardRoundTrip() {
    bt::Blackboard bb;
    CHECK(!bb.Has("hp_frac"));
    bb.SetFloat("hp_frac", 0.25f);
    bb.SetInt("retreats_left", 2);
    bb.SetBool("slam_ready", true);
    bb.SetVec2("nearest_tower", Vec2{12.0f, -3.0f});
    CHECK(bb.Has("hp_frac"));
    CHECK_EQ(bb.GetFloat("hp_frac", 1.0f), 0.25f);
    CHECK_EQ(bb.GetInt("retreats_left", 0), 2);
    CHECK(bb.GetBool("slam_ready", false));
    CHECK_EQ(bb.GetVec2("nearest_tower").x, 12.0f);
    CHECK_EQ(bb.GetVec2("nearest_tower").y, -3.0f);

    // 미설정 키는 **호출자가 준 기본값**을 낸다. 마지막 값을 흘리면 정예가
    // 한 틱 전의 세계를 보고 결정한다.
    CHECK_EQ(bb.GetFloat("no_such_key", 7.5f), 7.5f);
    CHECK_EQ(bb.GetInt("no_such_key", -4), -4);
    CHECK(!bb.GetBool("no_such_key", false));
    CHECK(bb.GetBool("no_such_key", true));
    CHECK_EQ(bb.GetVec2("no_such_key").x, 0.0f);
    CHECK_EQ(bb.GetVec2("no_such_key", Vec2{9, 9}).y, 9.0f);

    // 타입은 서로 다른 칸에 산다. 같은 이름의 float 와 int 가 섞이지 않는다.
    bb.SetFloat("shared", 1.5f);
    bb.SetInt("shared", 3);
    CHECK_EQ(bb.GetFloat("shared", 0.0f), 1.5f);
    CHECK_EQ(bb.GetInt("shared", 0), 3);
}

// ── Running ────────────────────────────────────────────────────────
// Running 을 낸 노드는 다음 틱에 **같은 지점에서 재개**한다. 앞 자식은 다시
// 돌지 않는다 — 그것이 memory Composite 의 정의다.
static void TestRunningResumesAtSameNode() {
    int firstCalls = 0, secondCalls = 0;
    int budget = 3;
    bt::NodePtr tree = bt::Builder()
        .Sequence()
            .Action([&firstCalls](bt::Blackboard&) { ++firstCalls; return Status::Success; })
            .Action([&secondCalls, &budget](bt::Blackboard&) {
                ++secondCalls;
                return (--budget > 0) ? Status::Running : Status::Success;
            })
        .End()
        .Build();
    bt::Blackboard bb;
    CHECK(tree->Tick(bb) == Status::Running);
    CHECK(tree->Tick(bb) == Status::Running);
    CHECK(tree->Tick(bb) == Status::Success);
    CHECK_EQ(firstCalls, 1);       // 앞 자식은 딱 한 번
    CHECK_EQ(secondCalls, 3);      // 뒤 자식이 세 틱에 걸쳐 돌았다
}

// Sequence 는 첫 Failure 에서, Selector 는 첫 Success 에서 멈춘다.
static void TestCompositeStopRules() {
    std::vector<int> visited;
    bt::NodePtr sequence = bt::Builder()
        .Sequence()
            .Action([&visited](bt::Blackboard&) { visited.push_back(0); return Status::Success; })
            .Action([&visited](bt::Blackboard&) { visited.push_back(1); return Status::Failure; })
            .Action([&visited](bt::Blackboard&) { visited.push_back(2); return Status::Success; })
        .End()
        .Build();
    bt::Blackboard bb;
    CHECK(sequence->Tick(bb) == Status::Failure);
    CHECK_EQ(visited.size(), size_t{2});
    CHECK_EQ(visited[1], 1);

    visited.clear();
    bt::NodePtr selector = bt::Builder()
        .Selector()
            .Action([&visited](bt::Blackboard&) { visited.push_back(0); return Status::Failure; })
            .Action([&visited](bt::Blackboard&) { visited.push_back(1); return Status::Success; })
            .Action([&visited](bt::Blackboard&) { visited.push_back(2); return Status::Success; })
        .End()
        .Build();
    CHECK(selector->Tick(bb) == Status::Success);
    CHECK_EQ(visited.size(), size_t{2});
    CHECK_EQ(visited[1], 1);
}

// Parallel 의 두 정책. requireAll 이면 전부 Success 라야 Success 이고,
// 아니면 하나라도 Success 면 Success 다. 어느 쪽이든 **모든 자식을 매 틱 돈다**.
static void TestParallelPolicies() {
    bt::Blackboard bb;
    int ticks = 0;
    {
        auto all = std::make_unique<bt::Parallel>(true);
        all->Add(std::make_unique<bt::Action>([&ticks](bt::Blackboard&) { ++ticks; return Status::Success; }));
        all->Add(std::make_unique<bt::Action>([&ticks](bt::Blackboard&) { ++ticks; return Status::Running; }));
        CHECK(all->Tick(bb) == Status::Running);
        CHECK_EQ(ticks, 2);                       // 하나가 Running 이어도 둘 다 돈다
    }
    {
        auto all = std::make_unique<bt::Parallel>(true);
        all->Add(std::make_unique<bt::Action>([](bt::Blackboard&) { return Status::Success; }));
        all->Add(std::make_unique<bt::Action>([](bt::Blackboard&) { return Status::Failure; }));
        CHECK(all->Tick(bb) == Status::Failure);
    }
    {
        auto any = std::make_unique<bt::Parallel>(false);
        any->Add(std::make_unique<bt::Action>([](bt::Blackboard&) { return Status::Failure; }));
        any->Add(std::make_unique<bt::Action>([](bt::Blackboard&) { return Status::Success; }));
        CHECK(any->Tick(bb) == Status::Success);
    }
    {
        auto any = std::make_unique<bt::Parallel>(false);
        any->Add(std::make_unique<bt::Action>([](bt::Blackboard&) { return Status::Failure; }));
        any->Add(std::make_unique<bt::Action>([](bt::Blackboard&) { return Status::Failure; }));
        CHECK(any->Tick(bb) == Status::Failure);
    }
}

// Repeater 는 정확히 count 회 돌린 뒤 Success 를 낸다.
static void TestRepeaterCount() {
    int runs = 0;
    bt::NodePtr tree = bt::Builder()
        .Repeater(4)
            .Action([&runs](bt::Blackboard&) { ++runs; return Status::Success; })
        .Build();
    bt::Blackboard bb;
    CHECK(tree->Tick(bb) == Status::Running);
    CHECK(tree->Tick(bb) == Status::Running);
    CHECK(tree->Tick(bb) == Status::Running);
    CHECK(tree->Tick(bb) == Status::Success);
    CHECK_EQ(runs, 4);
}

// ── Builder ────────────────────────────────────────────────────────
// End() 가 스택을 팝하지 않으면 형제가 자식이 된다 — 트리 **모양**이 달라지고
// 결과가 갈린다. 그 사실을 여기서 두 모양의 결과 차이로 고정한다.
static void TestEndClosesComposite() {
    std::vector<int> flat;
    bt::NodePtr correct = bt::Builder()
        .Selector()
            .Sequence()
                .Action([&flat](bt::Blackboard&) { flat.push_back(0); return Status::Failure; })
                .Action([&flat](bt::Blackboard&) { flat.push_back(1); return Status::Success; })
            .End()
            .Action([&flat](bt::Blackboard&) { flat.push_back(2); return Status::Success; })
        .End()
        .Build();
    bt::Blackboard bb;
    CHECK(correct->Tick(bb) == Status::Success);
    // Sequence 가 첫 자식에서 실패 → Selector 가 폴백 행동으로 넘어간다.
    CHECK_EQ(flat.size(), size_t{2});
    CHECK_EQ(flat[0], 0);
    CHECK_EQ(flat[1], 2);

    // End() 를 빠뜨리면 폴백 행동이 Sequence 의 **셋째 자식**이 되어 절대 돌지
    // 않는다. 같은 노드 목록, 다른 모양, 다른 결과.
    std::vector<int> nested;
    bt::NodePtr broken = bt::Builder()
        .Selector()
            .Sequence()
                .Action([&nested](bt::Blackboard&) { nested.push_back(0); return Status::Failure; })
                .Action([&nested](bt::Blackboard&) { nested.push_back(1); return Status::Success; })
                .Action([&nested](bt::Blackboard&) { nested.push_back(2); return Status::Success; })
        .Build();
    CHECK(broken->Tick(bb) == Status::Failure);
    CHECK_EQ(nested.size(), size_t{1});
}

// Decorator 는 자식 하나를 붙이면 자동으로 닫힌다.
static void TestDecoratorAutoCloses() {
    int patrols = 0;
    bt::NodePtr tree = bt::Builder()
        .Sequence()
            .Inverter()
                .Condition([](bt::Blackboard& bb) { return bb.GetBool("visible"); })
            .Action([&patrols](bt::Blackboard&) { ++patrols; return Status::Success; })
        .End()
        .Build();
    bt::Blackboard bb;
    bb.SetBool("visible", false);
    CHECK(tree->Tick(bb) == Status::Success);
    CHECK_EQ(patrols, 1);          // Inverter 뒤의 행동이 형제로 붙었다
    tree->Reset();
    bb.SetBool("visible", true);
    CHECK(tree->Tick(bb) == Status::Failure);
    CHECK_EQ(patrols, 1);          // Inverter 가 실패시켜 형제가 돌지 않는다
}

static void RunTests() {
    TestBlackboardRoundTrip();
    TestRunningResumesAtSameNode();
    TestCompositeStopRules();
    TestParallelPolicies();
    TestRepeaterCount();
    TestEndClosesComposite();
    TestDecoratorAutoCloses();
}

TEST_MAIN("ai_bt")
