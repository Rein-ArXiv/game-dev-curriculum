// parts_perf_lab — 1만 점 ECS 시뮬레이션의 저레벨 성능 측정 도구.
//
// 현재 활성 토글:
//   - entities (1k / 10k / 100k)
//   - layout (SoA / AoS / Archetype)
//   - simd (Scalar / SSE / AVX2 — SoA/Archetype만, AoS는 scalar 폴백)
//   - branchless (Scalar 경로의 predication 토글; SIMD 경로는 이미 마스크라 무관)
//   - allocator (malloc / Arena / Pool / Slab; EventBenchmark 워크로드에 적용)
//   - container (vector / std::list / Intrusive; ContainerBenchmark 워크로드에 적용)

#include "raylib.h"

#include "alloc/arena_allocator.h"
#include "alloc/iallocator.h"
#include "alloc/malloc_allocator.h"
#include "alloc/pool_allocator.h"
#include "alloc/slab_allocator.h"
#include "measure/container_benchmark.h"
#include "measure/event_benchmark.h"
#include "measure/frame_timer.h"
#include "sim/isimulation.h"
#include "ui/hud.h"
#include "ui/toggles.h"

#include <cstdio>
#include <memory>

namespace {

constexpr int kWindowW = 1280;
constexpr int kWindowH = 720;
constexpr int kHudH    = 140;
constexpr int kSimH    = kWindowH - kHudH;

// EventBenchmark가 매 프레임 alloc/free할 노드 수.
// 작은 블록(24 byte) × 2000개 = 48KB가 매 프레임 할당 후 즉시 회수된다.
constexpr std::size_t kEventCount = 2000;

// ContainerBenchmark가 보유할 노드 수 (시작 시 한 번 채우고 매 프레임 순회).
constexpr std::size_t kContainerCount = 5000;

// allocator별 백킹 용량.
// Pool/Slab은 한 프레임의 노드 수 + 여유를 미리 잡아 둔다.
// Arena는 노드 다발 한 번에 충분한 단일 버퍼.
constexpr std::size_t kArenaBytes  = 256 * 1024;   // 256 KB
constexpr std::size_t kPoolBlocks  = 4096;         // 2000 + 여유

std::unique_ptr<ppl::IAllocator> CreateAllocator(ppl::Allocator type) {
    using namespace ppl;
    switch (type) {
        case Allocator::Malloc:
            return std::make_unique<MallocAllocator>();
        case Allocator::Arena:
            return std::make_unique<ArenaAllocator>(kArenaBytes);
        case Allocator::Pool:
            return std::make_unique<PoolAllocator>(
                EventBenchmark::kNodeSize,
                EventBenchmark::kNodeAlign,
                kPoolBlocks);
        case Allocator::Slab:
            return std::make_unique<SlabAllocator>(kPoolBlocks);
    }
    return std::make_unique<MallocAllocator>();
}

}  // namespace

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(kWindowW, kWindowH, "parts_perf_lab");
    SetTargetFPS(0);

    ppl::Toggles  toggles;
    ppl::Toggles  prevToggles = toggles;

    auto sim = ppl::CreateSimulation(toggles.layout);
    sim->Init(toggles.EntityCount(), kWindowW, kSimH);

    auto alloc = CreateAllocator(toggles.allocator);

    ppl::EventBenchmark bench;
    bench.Init(kEventCount);

    // ContainerBenchmark용 별도 Pool. EventBenchmark의 allocator와 분리해
    // container 토글이 allocator 토글에 영향받지 않게 한다.
    auto containerPool = std::make_unique<ppl::PoolAllocator>(
        sizeof(ppl::ContainerBenchmark::Node),
        alignof(ppl::ContainerBenchmark::Node),
        kContainerCount + 16);

    ppl::ContainerBenchmark cbench;
    cbench.Init(toggles.container, containerPool.get(), kContainerCount);

    ppl::FrameTimer timer;

    ppl::Hud hud;
    hud.Init(kWindowW, kWindowH, kHudH);

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();

        timer.BeginUpdate();
        sim->Update(dt, toggles.simd, toggles.branchless);
        timer.EndUpdate();

        bench.RunFrame(alloc.get());
        cbench.RunFrame();

        timer.BeginDraw();
        BeginDrawing();
        ClearBackground(Color{10, 10, 14, 255});
        sim->Draw();
        timer.EndDraw();
        timer.CommitFrame(bench.LastFrameMs(), cbench.LastFrameMs());
        hud.Draw(toggles, timer, sim->Count(), bench, *alloc, cbench);
        EndDrawing();

        // 토글 변경 처리.
        // - layout 변경: 시뮬 인스턴스 자체를 교체.
        // - entityCount 변경: 시뮬 재초기화.
        // - allocator 변경: EventBenchmark용 allocator 인스턴스를 새로 만든다.
        // - container 변경: ContainerBenchmark 재초기화.
        if (ppl::RequiresRestart(prevToggles, toggles)) {
            const bool layoutChanged    = (prevToggles.layout    != toggles.layout);
            const bool entityChanged    = (prevToggles.entityPresetIdx != toggles.entityPresetIdx);
            const bool allocatorChanged = (prevToggles.allocator != toggles.allocator);
            const bool containerChanged = (prevToggles.container != toggles.container);

            if (layoutChanged || entityChanged) {
                sim->Shutdown();
                if (layoutChanged) {
                    sim = ppl::CreateSimulation(toggles.layout);
                }
                sim->Init(toggles.EntityCount(), kWindowW, kSimH);
            }

            if (allocatorChanged) {
                alloc = CreateAllocator(toggles.allocator);
            }

            if (containerChanged) {
                cbench.Init(toggles.container, containerPool.get(),
                            kContainerCount);
            }
        }
        prevToggles = toggles;
    }

    sim->Shutdown();
    cbench.Shutdown();
    CloseWindow();
    return 0;
}
