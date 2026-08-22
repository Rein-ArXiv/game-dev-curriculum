// jobs=0|1|2|4|8 — 워커 수가 결과를 바꾸지 않는가.
//
// 병렬 시뮬의 유일한 어려운 부분은 속도가 아니라 **재현성**이다. 워커가 공유
// 스칼라를 갱신하거나 완료 순서대로 결과를 이어 붙이면, 같은 입력에 워커 수마다
// 다른 세계가 나온다. 그러면 골든 해시도, 리플레이도, 협동 권위도 성립하지 않는다.
//
// 규칙은 둘이다.
//   1. 청크는 인덱스로만 나누고, 청크는 자기 구간 바깥을 쓰지 않는다.
//   2. 합치는 것은 **인덱스 오름차순**으로 한다(완료 순서가 아니라).
// 이 파일은 그 둘이 지켜졌는지를 다섯 워커 수의 상태 해시가 전부 같은지로 묻는다.

#include "game/game.hpp"
#include "jobs/scheduler.h"
#include "profiler/trace.h"
#include "test_util.h"

#include <chrono>
#include <cstdio>
#include <vector>

using namespace mega;
using namespace mega::game;

namespace {

uint64_t RunMatch(int workers, WorkerLock lock, ScaleKind scale, int ticks) {
    Scheduler scheduler;
    if (workers > 0) {
        Scheduler::Config config;
        config.workers = workers;
        config.lock = lock;
        scheduler.Start(config);
        CHECK_EQ(scheduler.WorkerCount(), workers);
    }

    Switches switches;
    switches.scale = scale;
    switches.jobs = workers;
    switches.entityStore = EntityStoreKind::Soa;   // 병렬 경로가 실제로 도는 배치
    GameWorld world;
    world.Configure(switches, workers > 0 ? &scheduler : nullptr);
    const DefenseMap map = DefenseMap::CreateDefault();
    // 상한을 꽉 채우는 웨이브라야 청크 분할이 실제로 일어난다. 콘텐츠 웨이브
    // (최대 26마리)로 재면 병렬 경로는 한 번도 돌지 않고, 그 상태에서 "워커 수와
    // 무관하다"는 단언은 아무것도 증명하지 못한다.
    std::vector<WaveDef> waves(4, WaveDef{9000, 900.0f, 30.0f, 1, 0.0f, EnemyType::Normal});
    world.Init(map, scale == ScaleKind::Large ? waves : GenerateWaves(8), 4000, 1000000.0f);
    world.PlaceTowerType(map.CellCenter(6, 1), TowerType::Gatling);
    world.PlaceTowerType(map.CellCenter(8, 2), TowerType::Splash);
    world.PlaceTowerType(map.CellCenter(11, 4), TowerType::Cannon);
    world.PlaceTowerType(map.CellCenter(10, 6), TowerType::Sniper);
    for (int i = 0; i < ticks; ++i) world.Step(1.0f / 60.0f);
    // 이 단언이 없으면 위의 해시 비교가 "둘 다 직렬로 돌았다"를 증명하는 것으로
    // 조용히 퇴화한다.
    if (scale == ScaleKind::Large) CHECK(world.Enemies().Size() >= 512);
    return world.StateHash();
}

// 다섯 워커 수가 같은 세계를 낸다. scale=large 라야 청크 분할이 실제로 일어난다
// (kParallelGrain 의 두 배를 넘어야 나눈다).
void TestWorkerCountDoesNotChangeTheWorld() {
    const int ticks = 900;
    const uint64_t serial = RunMatch(0, WorkerLock::Std, ScaleKind::Large, ticks);
    std::printf("[jobs] serial hash=%016llx\n", static_cast<unsigned long long>(serial));
    for (int workers : {1, 2, 4, 8}) {
        const uint64_t parallel = RunMatch(workers, WorkerLock::Std, ScaleKind::Large, ticks);
        std::printf("[jobs] workers=%d hash=%016llx\n", workers,
                    static_cast<unsigned long long>(parallel));
        CHECK_EQ(serial, parallel);
    }
    // 잠금 구현을 바꿔도 같은 세계다 — 잠금은 순서를 바꾸는 장치가 아니다.
    CHECK_EQ(serial, RunMatch(4, WorkerLock::Futex, ScaleKind::Large, ticks));
}

// 작은 규모(직렬로 떨어지는 경로)에서도 워커가 있고 없고가 같아야 한다.
void TestSmallScaleFallsBackToSerial() {
    const uint64_t serial = RunMatch(0, WorkerLock::Std, ScaleKind::Small, 1200);
    CHECK_EQ(serial, RunMatch(4, WorkerLock::Std, ScaleKind::Small, 1200));
}

// ParallelFor 자체: grain 을 1까지 줄여도 각 인덱스가 정확히 한 번 처리된다.
void TestParallelForCoversEveryIndexOnce() {
    Scheduler scheduler;
    Scheduler::Config config;
    config.workers = 4;
    scheduler.Start(config);
    for (size_t grain : {size_t(1), size_t(7), size_t(64), size_t(4096)}) {
        std::vector<int> touched(5000, 0);
        scheduler.ParallelFor(touched.size(), grain, [&touched](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) ++touched[i];
        });
        for (size_t i = 0; i < touched.size(); ++i) CHECK_EQ(touched[i], 1);
    }
}

// 워커 수를 늘렸을 때의 비용. 단언하지 않고 출력만 한다 — 이 수는 머신의 코어
// 수·부하에 달렸고, 게이트는 위의 해시 동일성이 이미 맡고 있다.
//
// 나뉘는 것은 적 전진과 조준 질의 두 시스템뿐이다. 다른 시스템(스폰·발사체·
// 압축)은 직렬로 남으므로 암달의 법칙이 그대로 보인다.
void ReportJobCost() {
    auto run = [](int workers, int ticks) {
        Scheduler scheduler;
        if (workers > 0) {
            Scheduler::Config config;
            config.workers = workers;
            scheduler.Start(config);
        }
        Switches switches;
        switches.scale = ScaleKind::Large;
        switches.jobs = workers;
        switches.entityStore = EntityStoreKind::Soa;
        GameWorld world;
        world.Configure(switches, workers > 0 ? &scheduler : nullptr);
        const DefenseMap map = DefenseMap::CreateDefault();
        std::vector<WaveDef> waves(4, WaveDef{9000, 900.0f, 30.0f, 1, 0.0f, EnemyType::Normal});
        world.Init(map, waves, 4000, 1e9f);
        const DefenseMap::Cell cells[16] = {
            {6, 1}, {8, 1}, {6, 2}, {8, 2}, {6, 3}, {8, 4}, {11, 2}, {13, 3},
            {11, 4}, {13, 5}, {11, 6}, {10, 8}, {8, 8}, {5, 8}, {3, 9}, {5, 10}};
        for (const DefenseMap::Cell& cell : cells)
            world.PlaceTowerType(map.CellCenter(cell.x, cell.y), TowerType::Gatling);
        for (int i = 0; i < 600; ++i) world.Step(1.0f / 60.0f);
        profiler::Profiler::Get().Reset();
        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < ticks; ++i) world.Step(1.0f / 60.0f);
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - begin).count();
        double advance = 0, fire = 0;
        for (const profiler::AggEntry& e :
             profiler::AggregateByName(profiler::Profiler::Get().Snapshot())) {
            if (e.name == "sim.enemy_advance") advance = e.totalMs;
            if (e.name == "sim.tower_fire") fire = e.totalMs;
        }
        std::printf("  jobs=%d enemies=%zu ticks=%d total=%.1fms advance=%.2fms fire=%.2fms\n",
                    workers, world.Enemies().Size(), ticks, ms, advance, fire);
        return world.StateHash();
    };
    const int ticks = 600;
    std::printf("[jobs scale=large towers=16]\n");
    const uint64_t serial = run(0, ticks);
    for (int workers : {1, 2, 4, 8}) CHECK_EQ(serial, run(workers, ticks));
}

}  // namespace

static void RunTests() {
    TestParallelForCoversEveryIndexOnce();
    TestSmallScaleFallsBackToSerial();
    TestWorkerCountDoesNotChangeTheWorld();
    ReportJobCost();
}

TEST_MAIN("jobs_determinism")
