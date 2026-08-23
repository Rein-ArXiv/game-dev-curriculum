// profiler/trace.h 가 제품 프레임에 실제로 박혀 있는가.
//
// live::Histogram 의 frame_ms 는 "프레임이 몇 ms 였나"까지만 답한다. 느려진 프레임
// 안에서 **무엇이** 오래 걸렸는지는 답하지 못하고, 그 질문에 답할 수 없으면 어떤
// 최적화도 근거 없이 고른 것이 된다.
//
// 그래서 이 파일은 계측 자체를 테스트한다. 제품의 진짜 프레임 루프
// (DefenseApp::Step)를 돌리고 나서 (1) 시뮬 시스템·렌더·넷 펌프의 이름이
// 전부 기록에 있는지, (2) 각 이름의 호출 수가 실제 틱 수와 맞는지, (3) 집계가
// 사람이 읽을 수 있는 형태로 나오는지를 본다.

#include "game/asset_reload.hpp"
#include "game/defense_app.hpp"
#include "profiler/trace.h"
#include "test_util.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace mega;
using namespace mega::game;

namespace {

const profiler::AggEntry* Find(const std::vector<profiler::AggEntry>& all, const char* name) {
    for (const profiler::AggEntry& entry : all)
        if (entry.name == name) return &entry;
    return nullptr;
}

void TestFrameLoopIsInstrumented() {
    profiler::Profiler::Get().Reset();

    DefenseOptions options;
    options.selftest = true;
    options.profilePath = platform::ExecutableDirectory() + "/trace_probe.wal";
    // 매 실행 같은 자리에서 출발한다 — 이 파일은 지워지지 않아 ctest 를 거듭
    // 돌리면 지갑이 누적한다(`test_profile_isolation` 이 그 누적을 잰다).
    CHECK(TruncateFile(options.profilePath));
    DefenseApp app;
    CHECK(app.Init(options));

    const int ticks = 240;
    for (int i = 0; i < ticks; ++i) app.Step(FrameInput{}, kDefenseFixedDt);

    const std::vector<profiler::AggEntry> aggregate =
        profiler::AggregateByName(profiler::Profiler::Get().Snapshot());
    CHECK(!aggregate.empty());

    // 프레임 루프의 이음매들.
    const char* frameScopes[3] = {"frame", "net.pump", "render.draw"};
    for (const char* name : frameScopes) {
        const profiler::AggEntry* entry = Find(aggregate, name);
        CHECK(entry != nullptr);
        if (entry) CHECK_EQ(entry->count, static_cast<uint64_t>(ticks));
    }

    // 시뮬 시스템들. 승패가 나면 Step 이 조기 반환하므로 틱 수 이하이되 0은 아니다.
    // sim.stomp 는 리그를 가진 개체가 있을 때만 열리므로 여기 목록에 없다.
    const char* simScopes[7] = {"sim.step",      "sim.spawn",    "sim.enemy_advance",
                                "sim.tower_fire", "sim.projectile", "sim.cleanup",
                                "sim.winlose"};
    for (const char* name : simScopes) {
        const profiler::AggEntry* entry = Find(aggregate, name);
        CHECK(entry != nullptr);
        if (entry) {
            CHECK(entry->count > 0);
            CHECK(entry->count <= static_cast<uint64_t>(ticks));
            CHECK(entry->totalMs >= 0.0);
            CHECK(entry->maxMs >= 0.0);
        }
    }

    // 링은 kCap 개까지 보존한다. 여기서 남긴 이벤트 수가 그 상한을 넘으면 앞쪽이
    // 덮여 count 단언이 성립하지 않으므로, 그 조건도 함께 지킨다.
    CHECK(profiler::Profiler::Get().TotalRecorded() <= profiler::Profiler::kCap);

    // 집계 출력은 사람이 읽는 것이다 — 헤더와 한 줄 이상이 있어야 한다.
    const std::string report = FormatTraceReport();
    CHECK(report.rfind("scope\tcount\ttotal_ms\tmax_ms\tmean_ms\n", 0) == 0);
    CHECK(report.find("sim.enemy_advance") != std::string::npos);
    CHECK(report.find("render.draw") != std::string::npos);

    app.PrintProfile();
}

// ScopeTimer 자체: 중첩된 구간이 각자 자기 이름으로 기록되고, 바깥이 안쪽을
// 포함한다.
void TestNestedScopesAreAttributedSeparately() {
    profiler::Profiler::Get().Reset();
    {
        MEGA_PROFILE_SCOPE("outer");
        for (int i = 0; i < 5; ++i) {
            MEGA_PROFILE_SCOPE("inner");
            volatile double sink = 0;
            for (int k = 0; k < 20000; ++k) sink += k * 0.5;
            (void)sink;
        }
    }
    const std::vector<profiler::AggEntry> aggregate =
        profiler::AggregateByName(profiler::Profiler::Get().Snapshot());
    const profiler::AggEntry* outer = Find(aggregate, "outer");
    const profiler::AggEntry* inner = Find(aggregate, "inner");
    CHECK(outer != nullptr);
    CHECK(inner != nullptr);
    if (outer && inner) {
        CHECK_EQ(outer->count, static_cast<uint64_t>(1));
        CHECK_EQ(inner->count, static_cast<uint64_t>(5));
        CHECK(outer->totalMs >= inner->totalMs);   // 바깥이 안쪽을 포함한다
    }
}

}  // namespace

static void RunTests() {
    TestNestedScopesAreAttributedSeparately();
    TestFrameLoopIsInstrumented();
}

TEST_MAIN("frame_trace")
