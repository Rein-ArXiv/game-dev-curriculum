// 분석(카운터·게이지·히스토그램) + 이벤트 스케줄 검증.
#include "live/analytics.hpp"
#include "live/event_schedule.hpp"
#include "test_util.h"

#include <cmath>

using namespace mega::live;

static void TestCounterGauge() {
    Counter c;
    c.Add();
    c.Add(5);
    CHECK_EQ(c.Value(), (uint64_t)6);
    c.Reset();
    CHECK_EQ(c.Value(), (uint64_t)0);

    Gauge g;
    g.Set(100);
    g.Add(-30);
    CHECK(std::fabs(g.Value() - 70.0) < 1e-9);
}

static void TestHistogram() {
    Histogram h = Histogram::Linear(/*max*/ 100.0, /*buckets*/ 100);
    for (int i = 0; i < 100; ++i) h.Record((double)i);   // 0..99 균등
    CHECK_EQ(h.Count(), (uint64_t)100);
    CHECK(std::fabs(h.Min() - 0.0) < 1e-9);
    CHECK(std::fabs(h.Max() - 99.0) < 1e-9);
    CHECK(std::fabs(h.Mean() - 49.5) < 1e-6);
    // 퍼센타일(버킷 해상도 1.0).
    CHECK(std::fabs(h.Percentile(0.5) - 50.0) < 2.0);
    CHECK(std::fabs(h.Percentile(0.95) - 95.0) < 2.0);
    CHECK(std::fabs(h.Percentile(0.99) - 99.0) < 2.0);
}

static void TestRegistry() {
    Registry reg;
    reg.GetCounter("matches").Add(3);
    reg.GetCounter("matches").Add(2);
    CHECK_EQ(reg.GetCounter("matches").Value(), (uint64_t)5);
    reg.GetGauge("ccu").Set(1280);
    CHECK(std::fabs(reg.GetGauge("ccu").Value() - 1280.0) < 1e-9);
    Histogram& lat = reg.GetHistogram("latency_ms", 200.0, 200);
    lat.Record(20); lat.Record(40); lat.Record(35);
    CHECK_EQ(lat.Count(), (uint64_t)3);
    CHECK(reg.MetricCount() >= 3);
}

static void TestOneShotEvent() {
    EventSchedule sch;
    sch.Add({1, /*start*/ 100, /*end*/ 200, /*period*/ 0, /*prio*/ 0});
    CHECK(!sch.IsActive(1, 50));         // 시작 전
    CHECK(sch.IsActive(1, 100));         // 시작(포함)
    CHECK(sch.IsActive(1, 150));         // 중간
    CHECK(!sch.IsActive(1, 200));        // 종료(미포함)
    CHECK(!sch.IsActive(1, 300));        // 종료 후
}

static void TestRecurringEvent() {
    // 매 1000마다 [0,100) 윈도우로 반복(주말 이벤트 느낌).
    EventSchedule sch;
    sch.Add({2, /*start*/ 0, /*end*/ 100, /*period*/ 1000, /*prio*/ 0});
    CHECK(sch.IsActive(2, 50));          // 1주기 윈도우 안
    CHECK(!sch.IsActive(2, 150));        // 윈도우 밖
    CHECK(sch.IsActive(2, 1050));        // 2주기 윈도우 안
    CHECK(!sch.IsActive(2, 1150));       // 밖
    CHECK(sch.IsActive(2, 5050));        // 먼 미래 주기에도
}

static void TestActivePriority() {
    EventSchedule sch;
    sch.Add({10, 0, 1000, 0, /*prio*/ 1});
    sch.Add({20, 0, 1000, 0, /*prio*/ 5});   // 더 높은 우선
    sch.Add({30, 0, 1000, 0, /*prio*/ 5});   // 동률 → id 순
    sch.Add({40, 2000, 3000, 0, 9});         // 이 시각엔 비활성
    auto active = sch.ActiveAt(500);
    CHECK_EQ(active.size(), (size_t)3);
    CHECK_EQ(active[0], 20u);            // prio 5, id 20
    CHECK_EQ(active[1], 30u);            // prio 5, id 30
    CHECK_EQ(active[2], 10u);            // prio 1
}

static void RunTests() {
    TestCounterGauge();
    TestHistogram();
    TestRegistry();
    TestOneShotEvent();
    TestRecurringEvent();
    TestActivePriority();
}

TEST_MAIN("live_metrics")
