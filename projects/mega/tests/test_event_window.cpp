// 기간 한정 이벤트의 경계 시각과 수정자 합성.
//
// 이 파일이 잠그는 것은 세 가지다.
//   1. [start, end) — 시작 포함, 종료 제외. 연속된 두 이벤트가 한 순간도
//      겹치지 않는다는 뜻이고, 겹치면 수정자가 두 번 곱해진다.
//   2. 겹치는 이벤트의 합성 순서는 (우선순위 내림차순, id 오름차순)로 고정.
//      곱셈만 있으면 순서가 안 보이지만 더하기와 불리언이 섞이면 보인다.
//   3. 같은 시각을 주입한 두 관측자(호스트·서버)는 같은 집합을 본다 — 시계를
//      안에서 읽지 않기 때문이다.
//
// 이 파일이 **묻지 않는 것**: 합성된 수정자가 밸런스와 웨이브 표에 실제로
// 곱해지는가. 여기 있는 것은 `live::EventSchedule` 하나이고,
// `defense::ApplyEventModifiers` 는 한 줄도 지나지 않는다. 그 배선을 통째로
// 건너뛰어도 이 파일은 전부 통과한다 — `tests/test_live_ops_wiring.cpp` 가
// 보스 러시 창의 시각을 제품에 주입해 그 뒷단을 잇는다.
#include "live/event_schedule.hpp"
#include "test_util.h"

#include <cmath>
#include <string>

using namespace mega::live;

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

// ── 1. 경계 ────────────────────────────────────────────────────────
static void TestHalfOpenWindow() {
    Event e;
    e.id = 1; e.start = 100; e.end = 200;
    CHECK(!e.ActiveAt(99));
    CHECK(e.ActiveAt(100));     // start 포함
    CHECK(e.ActiveAt(199));
    CHECK(!e.ActiveAt(200));    // end 미포함 — 여기가 변이 지점이다
    CHECK(!e.ActiveAt(201));

    // 맞닿은 두 이벤트는 어느 시각에도 동시에 활성이 아니다.
    EventSchedule schedule;
    CHECK(schedule.Add(Event{1, 100, 200, 0, 0, "early", {}}));
    CHECK(schedule.Add(Event{2, 200, 300, 0, 0, "late", {}}));
    for (int64_t t = 95; t <= 305; ++t)
        CHECK(schedule.ActiveAt(t).size() <= 1);
    CHECK_EQ(schedule.ActiveAt(199).size(), (size_t)1);
    CHECK_EQ(schedule.ActiveAt(199)[0], 1u);
    CHECK_EQ(schedule.ActiveAt(200)[0], 2u);

    // 길이가 0 이하인 창은 언제나 비활성(정의상 존재하지 않는 이벤트).
    Event degenerate;
    degenerate.start = 100; degenerate.end = 100;
    CHECK(!degenerate.ActiveAt(100));
}

static void TestRecurringBoundary() {
    Event weekly;
    weekly.id = 7; weekly.start = 1000; weekly.end = 1100; weekly.period = 1000;
    CHECK(!weekly.ActiveAt(999));
    CHECK(weekly.ActiveAt(1000));
    CHECK(weekly.ActiveAt(1099));
    CHECK(!weekly.ActiveAt(1100));    // 첫 주기 끝
    CHECK(!weekly.ActiveAt(1999));
    CHECK(weekly.ActiveAt(2000));     // 두 번째 주기 시작
    CHECK(weekly.ActiveAt(2099));
    CHECK(!weekly.ActiveAt(2100));
    CHECK(weekly.ActiveAt(9000));     // 먼 미래 주기
}

// ── 2. 합성 ────────────────────────────────────────────────────────
static void TestModifierComposition() {
    EventSchedule schedule;
    EventModifiers weekend;
    weekend.scrapMultiplier = 2.0f;
    weekend.bonusStartGold = 100;
    EventModifiers rush;
    rush.enemyHpMultiplier = 1.5f;
    rush.bonusStartGold = 50;
    rush.bossRush = true;
    rush.blastCostDelta = -30;

    CHECK(schedule.Add(Event{10, 0, 1000, 0, /*prio*/ 1, "weekend_scrap", weekend}));
    CHECK(schedule.Add(Event{20, 500, 1500, 0, /*prio*/ 5, "boss_rush", rush}));

    // 한쪽만 활성인 구간.
    const EventModifiers early = schedule.ModifiersAt(100);
    CHECK(Near(early.scrapMultiplier, 2.0f));
    CHECK(Near(early.enemyHpMultiplier, 1.0f));
    CHECK_EQ(early.bonusStartGold, 100);
    CHECK(!early.bossRush);

    // 겹치는 구간 — 곱은 곱해지고 더하기는 더해지며 불리언은 OR 된다.
    const EventModifiers both = schedule.ModifiersAt(700);
    CHECK(Near(both.scrapMultiplier, 2.0f));
    CHECK(Near(both.enemyHpMultiplier, 1.5f));
    CHECK_EQ(both.bonusStartGold, 150);
    CHECK_EQ(both.blastCostDelta, -30);
    CHECK(both.bossRush);

    // 순서는 우선순위 내림차순, 동률은 id 오름차순으로 **고정**이다.
    const std::vector<uint32_t> order = schedule.ActiveAt(700);
    CHECK_EQ(order.size(), (size_t)2);
    CHECK_EQ(order[0], 20u);   // prio 5
    CHECK_EQ(order[1], 10u);   // prio 1

    // 아무 이벤트도 없는 시각은 항등 수정자 — 이벤트 없음이 정상 경로다.
    const EventModifiers none = schedule.ModifiersAt(2000);
    CHECK(none.IsIdentity());

    // 겹침이 아무리 쌓여도 최종 범위는 잘린다.
    EventSchedule stacked;
    EventModifiers heavy;
    heavy.scrapMultiplier = 4.0f;
    for (uint32_t id = 1; id <= 4; ++id)
        CHECK(stacked.Add(Event{id, 0, 1000, 0, 0, "heavy", heavy}));
    const EventModifiers clamped = stacked.ModifiersAt(10);
    CHECK(Near(clamped.scrapMultiplier, 8.0f));   // 4^4 = 256 이 아니라 상한 8
}

// ── 3. 주입한 시각에 대해 두 관측자가 같은 집합 ────────────────────
static void TestInjectedClockIsDeterministic() {
    const char* text =
        "# 운영자 파일\n"
        "event 1 weekend_scrap 1000 2000 0 10 scrap=2.0\n"
        "event 2 boss_rush 1500 2500 0 5 enemy_hp=1.5 boss_rush=1\n"
        "event 3 free_blast 0 100000 0 -1 blast_cost=-40\n";
    EventSchedule host, server;
    std::string error;
    CHECK(ParseEventSchedule(text, host, &error));
    CHECK(error.empty());
    CHECK(ParseEventSchedule(text, server, &error));
    CHECK_EQ(host.Count(), (size_t)3);

    for (int64_t t = 900; t <= 2600; t += 37) {
        const std::vector<uint32_t> a = host.ActiveAt(t);
        const std::vector<uint32_t> b = server.ActiveAt(t);
        CHECK_EQ(a.size(), b.size());
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) CHECK_EQ(a[i], b[i]);
        const EventModifiers ma = host.ModifiersAt(t);
        const EventModifiers mb = server.ModifiersAt(t);
        CHECK(Near(ma.scrapMultiplier, mb.scrapMultiplier));
        CHECK_EQ(ma.blastCostDelta, mb.blastCostDelta);
        CHECK_EQ(ma.bossRush, mb.bossRush);
    }

    const EventModifiers peak = host.ModifiersAt(1800);
    std::printf("  t=1800 scrap=%.2f enemy_hp=%.2f blast=%d boss_rush=%d active=%zu\n",
                (double)peak.scrapMultiplier, (double)peak.enemyHpMultiplier,
                peak.blastCostDelta, peak.bossRush ? 1 : 0, host.ActiveAt(1800).size());
    CHECK_EQ(host.ActiveAt(1800).size(), (size_t)3);
    CHECK(Near(peak.scrapMultiplier, 2.0f));
    CHECK(peak.bossRush);
    CHECK_EQ(peak.blastCostDelta, -40);
}

// ── 파일의 신뢰 경계 ───────────────────────────────────────────────
static void TestScheduleFileFallback() {
    EventSchedule keep;
    CHECK(keep.Add(Event{99, 0, 10, 0, 0, "sentinel", {}}));
    const size_t before = keep.Count();
    std::string error;

    const char* rejected[] = {
        "event 1 x 200 100 0 0\n",              // end <= start
        "event 1 x 100 200 50 0\n",             // period < duration
        "event 1 x 100 200 0 0 scrap=9\n",      // 범위 밖 수정자
        "event 1 x 100 200 0 0 unknown=1\n",    // 모르는 키
        "event 1 x 100 200 0 0 scrap\n",        // key=value 아님
        "event 0 x 100 200 0 0\n",              // id 0
        "event 1 x 100 200 0 0\nevent 1 y 300 400 0 0\n",   // id 중복
        "season 1 x 100 200 0 0\n",             // 모르는 첫 낱말
        "event 1 x 100\n",                      // 낱말 부족
        "event 1 x abc 200 0 0\n",              // 수가 아님
    };
    for (const char* text : rejected) {
        CHECK(!ParseEventSchedule(text, keep, &error));
        CHECK(!error.empty());
        CHECK_EQ(keep.Count(), before);         // 실패는 out 을 건드리지 않는다
        CHECK(keep.Find(99) != nullptr);
    }

    // 빈 파일·주석만 있는 파일은 정상이고 결과는 이벤트 0개다.
    EventSchedule empty;
    CHECK(ParseEventSchedule("", empty, &error));
    CHECK(ParseEventSchedule("# nothing\n\n", empty, &error));
    CHECK_EQ(empty.Count(), (size_t)0);
    CHECK(empty.ModifiersAt(12345).IsIdentity());

    // 상한을 넘는 이벤트 수는 거부된다.
    std::string many;
    for (int i = 1; i <= (int)kMaxScheduledEvents + 1; ++i)
        many += "event " + std::to_string(i) + " e 0 100 0 0\n";
    EventSchedule overflow;
    CHECK(!ParseEventSchedule(many, overflow, &error));
}

static void RunTests() {
    TestHalfOpenWindow();
    TestRecurringBoundary();
    TestModifierComposition();
    TestInjectedClockIsDeterministic();
    TestScheduleFileFallback();
}

TEST_MAIN("event_window")
