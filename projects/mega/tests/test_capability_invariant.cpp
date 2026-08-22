// 티어가 달라도 권위 해시는 같다.
//
// 플랫폼 능력 프로파일은 워커 수·메모리 예산·파티클 수·오디오 보이스·렌더 LOD 를
// 바꾼다. 적 상한도, 웨이브도, 타워 사거리도 바꾸지 않는다. 그 경계가 지켜지는지를
// **실제 GameWorld 를 네 프로파일로 2000틱 돌려** 상태 해시가 전부 같은지로 묻는다.
//
// 이 단언이 공허해지지 않으려면 두 가지가 함께 있어야 한다.
//   (a) 네 프로파일이 실제로 서로 다른 예산을 준다(워커 0..16, LOD 0..2).
//   (b) 그 해시가 권위 규모에는 민감하다 — scale 을 바꾸면 갈라진다.
// (b)가 없으면 "해시가 늘 같다"는 문장은 해시가 아무것도 안 본다는 뜻일 수도 있다.
#include "game/game.hpp"
#include "jobs/scheduler.h"
#include "platform/capability.hpp"
#include "platform/detect.hpp"
#include "test_util.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace mega;
using namespace mega::game;
using mega::platform::BudgetFor;
using mega::platform::Capability;
using mega::platform::ClampWorkers;
using mega::platform::PlatformProfile;
using mega::platform::RuntimeBudget;

namespace {

// 권위 시뮬 한 판. 능력이 정하는 것은 워커 수 하나뿐이고, 그것 말고는 네 번의
// 실행이 완전히 같은 구성이다.
uint64_t RunUnderProfile(const PlatformProfile& profile, ScaleKind scale, int ticks,
                         int* workersUsed) {
    // 제품이 `--ab jobs=4` 를 받았다고 치자. 능력이 그것을 자른다(올리지는 않는다).
    const int workers = ClampWorkers(4, profile);
    if (workersUsed) *workersUsed = workers;

    Scheduler scheduler;
    if (workers > 0) {
        Scheduler::Config config;
        config.workers = workers;
        scheduler.Start(config);
        CHECK_EQ(scheduler.WorkerCount(), workers);
    }

    Switches switches;
    switches.scale = scale;
    switches.jobs = workers;
    switches.entityStore = EntityStoreKind::Soa;

    GameWorld world;
    world.Configure(switches, workers > 0 ? &scheduler : nullptr);
    const DefenseMap map = DefenseMap::CreateDefault();
    std::vector<WaveDef> heavy(4, WaveDef{9000, 900.0f, 30.0f, 1, 0.0f, EnemyType::Normal});
    world.Init(map, scale == ScaleKind::Large ? heavy : GenerateWaves(8), 4000, 1000000.0f);
    world.PlaceTowerType(map.CellCenter(6, 1), TowerType::Gatling);
    world.PlaceTowerType(map.CellCenter(8, 2), TowerType::Splash);
    world.PlaceTowerType(map.CellCenter(11, 4), TowerType::Cannon);
    world.PlaceTowerType(map.CellCenter(10, 6), TowerType::Sniper);
    for (int i = 0; i < ticks; ++i) world.Step(1.0f / 60.0f);
    return world.StateHash();
}

}  // namespace

// ── (a) 네 프로파일은 실제로 다른 예산을 준다 ──────────────────────
static void TestBudgetsDiffer() {
    const PlatformProfile pc = mega::platform::profiles::PC();
    const PlatformProfile mobile = mega::platform::profiles::Mobile();
    const PlatformProfile console = mega::platform::profiles::Console();
    const PlatformProfile web = mega::platform::profiles::Web();

    const RuntimeBudget bpc = BudgetFor(pc);
    const RuntimeBudget bmobile = BudgetFor(mobile);
    const RuntimeBudget bconsole = BudgetFor(console);
    const RuntimeBudget bweb = BudgetFor(web);

    std::printf("  프로파일별 예산 (workers/arenaKB/slabKB/particles/voices/lod)\n");
    const PlatformProfile all[] = {pc, mobile, console, web};
    for (const PlatformProfile& p : all) {
        const RuntimeBudget b = BudgetFor(p);
        std::printf("    %-8s %2d %5d %5d %5d %3d %d\n", p.name, b.workerCap,
                    b.frameArenaKB, b.blobSlabKB, b.particleCap, b.audioVoiceCap, b.renderLod);
    }

    // 스레드 능력이 없으면 워커는 0이다 — maxThreads 값이 무엇이든 능력이 이긴다.
    CHECK(!web.Has(Capability::Threads));
    CHECK_EQ(bweb.workerCap, 0);
    CHECK_EQ(ClampWorkers(8, web), 0);

    CHECK(bpc.workerCap > 0);
    CHECK(bmobile.workerCap > 0);
    CHECK(bconsole.workerCap > 0);
    // 요청보다 능력이 크면 요청이 이긴다(자르기만 한다).
    CHECK_EQ(ClampWorkers(2, pc), 2);
    CHECK_EQ(ClampWorkers(0, pc), 0);
    CHECK_EQ(ClampWorkers(-3, pc), 0);
    // 요청이 능력보다 크면 능력이 이긴다.
    CHECK_EQ(ClampWorkers(64, mobile), bmobile.workerCap);

    // 표시 예산은 티어마다 갈린다 — 갈리지 않으면 해시 비교가 공허하다.
    CHECK(bpc.particleCap > bweb.particleCap);
    CHECK(bpc.renderLod < bweb.renderLod);
    CHECK(bpc.audioVoiceCap >= bweb.audioVoiceCap);
    CHECK(bconsole.blobSlabKB >= bmobile.blobSlabKB);
}

// ── 권위 해시는 네 프로파일에서 같다 ───────────────────────────────
static void TestAuthorityHashIsProfileIndependent() {
    const PlatformProfile all[] = {
        mega::platform::profiles::PC(), mega::platform::profiles::Mobile(),
        mega::platform::profiles::Console(), mega::platform::profiles::Web()};

    uint64_t reference = 0;
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
        int workers = -1;
        const uint64_t hash = RunUnderProfile(all[i], ScaleKind::Large, 2000, &workers);
        std::printf("  %-8s workers=%2d  hash=0x%016llx\n", all[i].name, workers,
                    (unsigned long long)hash);
        if (i == 0) reference = hash;
        else CHECK_EQ(hash, reference);
    }
    CHECK(reference != 0);
}

// ── (b) 그 해시는 권위 규모에는 민감하다 ───────────────────────────
// 이 단언이 이 파일의 안전장치다. 능력이 적 상한을 건드리게 만드는 변이가
// 들어오면 위 비교가 즉시 갈라진다는 것을 여기서 보인다.
static void TestHashIsSensitiveToAuthorityScale() {
    const PlatformProfile pc = mega::platform::profiles::PC();
    const uint64_t large = RunUnderProfile(pc, ScaleKind::Large, 600, nullptr);
    const uint64_t small = RunUnderProfile(pc, ScaleKind::Small, 600, nullptr);
    std::printf("  scale=large 0x%016llx  vs  scale=small 0x%016llx\n",
                (unsigned long long)large, (unsigned long long)small);
    CHECK(large != small);
}

// ── OS → 프로파일 매핑과 플랫폼 문자열 ─────────────────────────────
static void TestProfileForOsAndLine() {
    using mega::platform::OS;
    CHECK(std::string(mega::platform::ProfileFor(OS::Windows).name) == "PC");
    CHECK(std::string(mega::platform::ProfileFor(OS::Linux).name) == "PC");
    CHECK(std::string(mega::platform::ProfileFor(OS::Web).name) == "Web");
    CHECK(std::string(mega::platform::ProfileFor(OS::Android).name) == "Mobile");

    const std::string line = mega::platform::CurrentPlatformLine();
    std::printf("  this machine: %s\n", line.c_str());
    CHECK(!line.empty());
    CHECK(line.find('/') != std::string::npos);
    CHECK(line.find("Unknown") == std::string::npos);
}

static void RunTests() {
    TestBudgetsDiffer();
    TestProfileForOsAndLine();
    TestAuthorityHashIsProfileIndependent();
    TestHashIsSensitiveToAuthorityScale();
}

TEST_MAIN("capability_invariant")
