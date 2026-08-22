// 군집 조향: 겹침 · 교착 없음 · 재현성.
//
// 이 파일이 지키는 경계 둘.
//   1. **아무도 겹쳐 서지 않는다.** 격자 방향을 그대로 따르면 수백 마리가 한 줄로
//      포개지므로, 조향을 켰을 때 개체 간 최소 거리가 실제로 벌어져야 한다.
//   2. **아무도 갇히지 않는다.** 분리력이 전진력을 삼키면 군집이 제자리에서 서로를
//      밀며 영원히 서 있는다. 그 상태는 실패가 아니라 **끝나지 않음**으로 나타나므로,
//      "유한 시간 안에 전원이 코어에 닿는다"를 명시적으로 단언한다.
#include "game/defense_content.hpp"
#include "game/game.hpp"
#include "game/map_gen.hpp"
#include "test_util.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace mega;
using namespace mega::game;

static DefenseMap MakeMap(uint32_t seed) {
    mapgen::TerrainParams params;
    params.seed = seed;
    DefenseMap map;
    CHECK(mapgen::Generate(params, map));
    return map;
}

// 한 웨이브를 통째로 흘려 보낸다. 타워는 세우지 않는다 — 조향만 재려면 적이
// 죽지 않아야 하고, 죽는 순간 "몇 마리가 도착했는가"가 조향이 아니라 사격의 함수가 된다.
struct CrowdRun {
    int    arrivedTick = -1;     // 전원이 코어에 닿은 틱
    float  minSeparation = 1e9f; // 스폰이 끝난 뒤 관측한 개체 간 최소 거리
    int    overlaps = 0;         // 분리 반경의 1/4 보다 가까웠던 짝의 수
    int    peakAlive = 0;
    uint64_t hash = 0;
};

// 스폰이 끝나기 전에는 거리를 재지 않는다. 스폰은 한 칸에서 일어나므로 그
// 순간의 최소 거리는 조향이 아니라 지터의 함수이고, 두 팔에서 같은 값이 나온다.
static constexpr int kSettleTicks = 400;

static CrowdRun RunCrowd(bool steering, int count, int maxTicks, uint32_t seed) {
    Switches switches;
    switches.map = MapSource::Procgen;
    switches.mapSeed = static_cast<int>(seed);
    switches.pathing = PathingKind::FlowField;
    switches.steering = steering;
    switches.scale = ScaleKind::Large;

    GameWorld world;
    world.Configure(switches);
    std::vector<WaveDef> waves{{count, 40.0f, 40.0f, 1, 0.03f, EnemyType::Swarm}};
    world.Init(MakeMap(seed), waves, 0, 1e9f);

    CrowdRun run;
    for (int tick = 0; tick < maxTicks; ++tick) {
        world.Step(1.0f / 60.0f);
        const EnemyStore& enemies = world.Enemies();
        const size_t rows = enemies.Size();
        int alive = 0;
        for (size_t i = 0; i < rows; ++i) if (enemies.Alive(i)) ++alive;
        if (alive > run.peakAlive) run.peakAlive = alive;
        // 최소 거리는 개체가 여럿일 때만 뜻이 있다. 전수 짝짓기는 비싸므로
        // 규모가 큰 틱에서만 재고, 그마저 앞쪽 64행으로 자른다.
        if (alive >= 8 && tick >= kSettleTicks) {
            const size_t limit = rows < 64 ? rows : 64;
            const float tooClose = world.Map()
                ? static_cast<float>(world.Map()->CellSize()) * 0.70f * 0.25f : 5.0f;
            for (size_t a = 0; a < limit; ++a) {
                if (!enemies.Alive(a)) continue;
                for (size_t b = a + 1; b < limit; ++b) {
                    if (!enemies.Alive(b)) continue;
                    const float d = math::Length(enemies.Pos(a) - enemies.Pos(b));
                    if (d < run.minSeparation) run.minSeparation = d;
                    if (d < tooClose) ++run.overlaps;
                }
            }
        }
        if (alive == 0 && world.Wave() > 1) { run.arrivedTick = tick; break; }
    }
    run.hash = world.StateHash();
    return run;
}

// 조향을 켜면 개체가 벌어진다. 끄면 격자 방향이 같은 개체가 한 점으로 모인다.
static void TestSeparation() {
    const CrowdRun plain = RunCrowd(false, 120, 6000, 4);
    const CrowdRun steered = RunCrowd(true, 120, 6000, 4);
    std::printf("  격자 방향  최소거리=%.3fpx 겹침짝=%d 도착틱=%d 최대생존=%d\n",
                static_cast<double>(plain.minSeparation), plain.overlaps,
                plain.arrivedTick, plain.peakAlive);
    std::printf("  조향       최소거리=%.3fpx 겹침짝=%d 도착틱=%d 최대생존=%d\n",
                static_cast<double>(steered.minSeparation), steered.overlaps,
                steered.arrivedTick, steered.peakAlive);
    CHECK(plain.peakAlive >= 8);
    CHECK(steered.peakAlive >= 8);
    // 격자 방향 팔은 같은 칸을 향하는 개체가 정확히 같은 점을 밟는다.
    CHECK(plain.minSeparation < 1.0f);
    CHECK(plain.overlaps > 0);
    // 조향 팔은 그 겹침을 실제로 걷어낸다. 최소 거리 하나가 아니라 **겹친 짝의
    // 수**로 재는 이유는, 조향을 켜도 두 개체가 한순간 스칠 수는 있기 때문이다 —
    // 한 번의 스침으로 무너지는 단언은 조향의 값을 재지 못하고 운을 잰다.
    CHECK(steered.minSeparation > plain.minSeparation);
    CHECK(steered.overlaps * 4 < plain.overlaps);
}

// 교착 없음: 조향을 켜도 전원이 유한 시간 안에 코어에 닿는다.
static void TestNoDeadlock() {
    for (uint32_t seed : {2u, 4u, 6u}) {
        const CrowdRun steered = RunCrowd(true, 200, 9000, seed);
        std::printf("  seed=%u 조향 도착틱=%d\n", seed, steered.arrivedTick);
        CHECK(steered.arrivedTick > 0);       // -1 = 9000틱 안에 못 끝냈다
    }
}

// 같은 시드 · 같은 팔 = 같은 세계. 조향은 이웃 질의를 지나므로 색인의 순회
// 순서가 새는 순간 이 단언이 먼저 죽는다.
static void TestDeterminism() {
    const CrowdRun a = RunCrowd(true, 120, 4000, 5);
    const CrowdRun b = RunCrowd(true, 120, 4000, 5);
    CHECK_EQ(a.hash, b.hash);
    CHECK_EQ(a.arrivedTick, b.arrivedTick);
}

static void RunTests() {
    TestSeparation();
    TestNoDeadlock();
    TestDeterminism();
}

TEST_MAIN("crowd")
