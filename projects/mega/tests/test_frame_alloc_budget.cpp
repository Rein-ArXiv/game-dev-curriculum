// frame_alloc=malloc|arena — 프레임당 힙 할당 횟수를 정수로 센다.
//
// "아레나가 빠르다"는 주장은 재기 어렵다(타이밍은 머신·부하마다 흔들린다). 대신
// **셀 수 있는 것**을 센다: 한 프레임이 전역 operator new 를 몇 번 부르는가.
// 아레나 팔은 프레임 시작에 Mark, 끝에 Rewind 한 번이므로 스크래치에서 오는
// 할당이 0이어야 하고, malloc 팔은 요청 하나당 한 번이어야 한다.
//
// 전역 operator new 를 이 번역 단위에서 바꿔치기한다. 정적 링크된 mega_core 를
// 포함해 프로그램 전체의 할당이 이 카운터를 지난다.

#include "game/game.hpp"
#include "test_util.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

namespace {
std::atomic<uint64_t> g_newCalls{0};
std::atomic<bool>     g_counting{false};
}

void* operator new(size_t size) {
    if (g_counting.load(std::memory_order_relaxed))
        g_newCalls.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](size_t size) { return operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

using namespace mega;
using namespace mega::game;

namespace {

struct Measurement {
    uint64_t allocations = 0;
    int      ticks = 0;
    uint64_t hash = 0;
    size_t   overflows = 0;
    size_t   arenaPeak = 0;
    double   perTick = 0;
};

Measurement Measure(FrameAllocKind kind, ScaleKind scale, int ticks) {
    Switches switches;
    switches.frameAlloc = kind;
    switches.scale = scale;

    // 스크래치 소요는 동시 생존 개체 수가 정한다. 상한까지 밀어 넣는 웨이브를
    // 써야 small(64) 과 large(2048) 의 차이가 바이트로 나타난다 — 콘텐츠
    // 웨이브(최대 26마리)로는 두 팔이 같은 크기를 잡아 비교가 성립하지 않는다.
    std::vector<WaveDef> waves;
    for (int i = 0; i < 6; ++i)
        waves.push_back(WaveDef{4000, 400.0f, 5.0f, 1, 0.0f, EnemyType::Normal});

    GameWorld world;
    world.Configure(switches);
    const DefenseMap map = DefenseMap::CreateDefault();
    world.Init(map, waves, 4000, 1000000.0f);
    world.PlaceTowerType(map.CellCenter(6, 1), TowerType::Gatling);
    world.PlaceTowerType(map.CellCenter(8, 2), TowerType::Splash);
    world.PlaceTowerType(map.CellCenter(11, 4), TowerType::Cannon);
    world.PlaceTowerType(map.CellCenter(10, 6), TowerType::Sniper);
    // 워밍업: 컨테이너 용량이 자라는 동안의 할당은 프레임 비용이 아니다.
    for (int i = 0; i < 300; ++i) world.Step(1.0f / 60.0f);

    Measurement out;
    g_newCalls.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);
    for (int i = 0; i < ticks; ++i) world.Step(1.0f / 60.0f);
    g_counting.store(false, std::memory_order_relaxed);

    out.allocations = g_newCalls.load(std::memory_order_relaxed);
    out.ticks = ticks;
    out.hash = world.StateHash();
    out.overflows = world.Scratch().Overflows();
    out.arenaPeak = world.Scratch().ArenaPeak();
    out.perTick = static_cast<double>(out.allocations) / static_cast<double>(ticks);
    return out;
}

void Report(const char* label, const Measurement& m) {
    std::printf("  %-22s allocations=%llu ticks=%d per_tick=%.3f arena_peak=%zuB overflow=%zu\n",
                label, static_cast<unsigned long long>(m.allocations), m.ticks,
                m.perTick, m.arenaPeak, m.overflows);
}

void TestScratchAllocationsDisappear(ScaleKind scale, const char* label) {
    const int ticks = 300;
    const Measurement heap = Measure(FrameAllocKind::Malloc, scale, ticks);
    const Measurement arena = Measure(FrameAllocKind::Arena, scale, ticks);
    std::printf("[frame_alloc %s]\n", label);
    Report("malloc", heap);
    Report("arena", arena);

    // (a) 같은 결과. 이것이 없으면 (b)는 "덜 해서 빨라졌다"와 구분되지 않는다.
    CHECK_EQ(heap.hash, arena.hash);
    // (b) 아레나 팔에서 스크래치 할당이 사라진다. 시뮬 한 스텝은 스크래치를 셋
    //     잡는다(개체별 피해 · 조준 결과 · 스플래시 후보).
    CHECK(heap.allocations > arena.allocations);
    CHECK(heap.allocations - arena.allocations >= static_cast<uint64_t>(ticks) * 2);
    // (c) 아레나가 한 번도 넘치지 않았다 — 넘쳤다면 (b)의 0은 거짓이다.
    CHECK_EQ(arena.overflows, static_cast<size_t>(0));
    CHECK(arena.arenaPeak > 0);
    CHECK_EQ(heap.arenaPeak, static_cast<size_t>(0));   // malloc 팔은 아레나를 안 잡는다
}

}  // namespace

static void RunTests() {
    TestScratchAllocationsDisappear(ScaleKind::Small, "scale=small");
    TestScratchAllocationsDisappear(ScaleKind::Large, "scale=large");
}

TEST_MAIN("frame_alloc_budget")
