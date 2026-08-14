// Profiler 검증: 기록 정확성·집계·중첩·링 오버플로·멀티스레드 동시 기록.
#include "profiler/trace.h"
#include "test_util.h"

#include <atomic>
#include <thread>
#include <vector>

using namespace mega::profiler;

// 측정 가능한 일을 시키는 바쁜 루프(sleep 의존 회피 — 결정성·비플레이키).
static volatile uint64_t g_sink = 0;
static void BusyWork(int iters) {
    uint64_t acc = 1;
    for (int i = 0; i < iters; ++i) acc = acc * 6364136223846793005ull + 1442695040888963407ull;
    g_sink += acc;
}

static void RunTests() {
    Profiler& prof = Profiler::Get();

    // --- 기본 기록 수 + 음수 아님 ---
    {
        prof.Reset();
        for (int i = 0; i < 5; ++i) { MEGA_PROFILE_SCOPE("loop"); BusyWork(2000); }
        CHECK_EQ(prof.TotalRecorded(), (uint64_t)5);
        auto snap = prof.Snapshot();
        CHECK_EQ(snap.size(), (size_t)5);
        bool nonneg = true;
        for (auto& e : snap) if (e.durMs < 0) nonneg = false;
        CHECK(nonneg);
    }

    // --- 이름별 집계 ---
    {
        prof.Reset();
        for (int i = 0; i < 3; ++i) { MEGA_PROFILE_SCOPE("A"); BusyWork(1000); }
        for (int i = 0; i < 2; ++i) { MEGA_PROFILE_SCOPE("B"); BusyWork(1000); }
        auto agg = AggregateByName(prof.Snapshot());
        CHECK_EQ(agg.size(), (size_t)2);
        uint64_t ca = 0, cb = 0;
        for (auto& a : agg) {
            if (a.name == "A") ca = a.count;
            if (a.name == "B") cb = a.count;
            CHECK(a.totalMs >= 0);
            CHECK(a.maxMs <= a.totalMs + 1e-9);   // max ≤ total
        }
        CHECK_EQ(ca, (uint64_t)3);
        CHECK_EQ(cb, (uint64_t)2);
    }

    // --- 중첩: 바깥 스코프 지속 ≥ 안쪽 스코프 지속 ---
    {
        prof.Reset();
        {
            MEGA_PROFILE_SCOPE("outer");
            BusyWork(500);
            { MEGA_PROFILE_SCOPE("inner"); BusyWork(3000); }
            BusyWork(500);
        }
        auto snap = prof.Snapshot();
        CHECK_EQ(snap.size(), (size_t)2);
        // 소멸 순서상 inner 가 먼저 기록(index 0), outer 가 나중(index 1).
        double inner = 0, outer = 0;
        for (auto& e : snap) {
            if (std::strcmp(e.name, "inner") == 0) inner = e.durMs;
            if (std::strcmp(e.name, "outer") == 0) outer = e.durMs;
        }
        CHECK(outer >= inner);    // 바깥이 안쪽을 포함
    }

    // --- 링 오버플로: 보존은 kCap, 총계는 전부 카운트 ---
    {
        prof.Reset();
        const uint64_t extra = 100;
        for (uint64_t i = 0; i < Profiler::kCap + extra; ++i)
            prof.Record(TraceEvent{"flood", 0, (double)i, 0.0});
        CHECK_EQ(prof.TotalRecorded(), Profiler::kCap + extra);
        auto snap = prof.Snapshot();
        CHECK_EQ(snap.size(), (size_t)Profiler::kCap);
        // 가장 오래된 'extra'개는 덮였으므로, 보존된 첫 이벤트의 startMs == extra.
        CHECK_EQ(snap.front().startMs, (double)extra);
        CHECK_EQ(snap.back().startMs, (double)(Profiler::kCap + extra - 1));
    }

    // --- 멀티스레드 동시 기록: 락 없이 전부 카운트 ---
    {
        prof.Reset();
        const int T = 8, M = 5000;
        std::vector<std::thread> ts;
        for (int t = 0; t < T; ++t)
            ts.emplace_back([&] {
                for (int i = 0; i < M; ++i) prof.Record(TraceEvent{"mt", ThreadTag(), 0, 0});
            });
        for (auto& th : ts) th.join();
        CHECK_EQ(prof.TotalRecorded(), (uint64_t)T * M);   // 유실 없는 원자 증가
    }
}

TEST_MAIN("profiler")
