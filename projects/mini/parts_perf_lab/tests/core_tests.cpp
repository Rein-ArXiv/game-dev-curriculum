// parts_perf_lab 계산 코어 자동 검증.
//
// 이 타깃은 raylib을 링크하지 않는다. 레이아웃 구현의 Draw()는 별도 번역 단위에
// 있고 여기서는 컴파일하지 않으므로, 그래픽 스택 없이 Update/CopyState와
// 할당자·벤치마크 워크로드만 검증한다.
//
// 실행 단위는 아래 kTests 표가 정한다. 인자 없이 부르면 전부 돌고,
// 그룹 이름 하나를 주면(`parts_core_tests allocator`) 그 그룹만 돈다. CMake가
// 그룹마다 add_test 를 하나씩 만들어 두므로 ctest 쪽 이름으로도 같은 분할이 보인다.
#include "alloc/arena_allocator.h"
#include "alloc/malloc_allocator.h"
#include "alloc/pool_allocator.h"
#include "alloc/slab_allocator.h"
#include "measure/container_benchmark.h"
#include "measure/event_benchmark.h"
#include "measure/frame_timer.h"
#include "sim/isimulation.h"
#include "sim/kernel_dispatch.h"
#include "ui/hud_layout.h"
#include "ui/toggles.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;
// 지금까지 평가한 단언 수. 그룹마다 이 수가 다르므로, 실행 결과 한 줄이
// "어느 step 을 돌렸는가" 를 그대로 말해 준다.
long long checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++checks;                                                             \
        if (!(expr)) {                                                        \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':'  \
                      << __LINE__ << ")\n";                                   \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

// 조합 순회 테스트용. 어떤 조합에서 깨졌는지 라벨을 함께 찍는다.
#define CHECK_AT(expr, label)                                                 \
    do {                                                                      \
        ++checks;                                                             \
        if (!(expr)) {                                                        \
            std::cerr << "CHECK failed: " #expr << " [" << (label) << "] ("  \
                      << __FILE__ << ':' << __LINE__ << ")\n";                \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

bool IsAligned(const void* ptr, std::size_t alignment) {
    return reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0;
}

bool NearStates(const std::vector<float>& a,
                const std::vector<float>& b,
                float epsilon = 1e-5f) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::fabs(a[i] - b[i]) > epsilon) return false;
    }
    return true;
}

// ── 측정 인프라 ───────────────────────────────────────────────────────

// FrameTimer 의 슬라이딩 윈도우 계약. update/draw 를 나눠 재는 것과, 그 값이
// 600칸 링 버퍼에 어떻게 쌓이는지가 이 클래스가 하는 일 전부다.
// HUD 없이도 확인할 수 있는 순수 계산이라 여기서 고정한다.
void TestFrameTimerWindow() {
    ppl::FrameTimer timer;
    CHECK(timer.Head() == 0);
    CHECK(timer.Filled() == 0);
    CHECK(timer.LastSimDrawSubmitMs() == 0.0f);

    // steady_clock 은 뒤로 가지 않으므로 한 쌍의 Begin/End 는 음이 아닌 ms 를 남긴다.
    timer.BeginFrame();
    timer.EndFrame();
    timer.BeginUpdate();
    timer.EndUpdate();
    timer.BeginDraw();
    timer.EndDraw();
    CHECK(timer.LastFrameWallMs() >= 0.0f);
    CHECK(timer.LastUpdateMs() >= 0.0f);
    CHECK(timer.LastDrawSubmitMs() >= 0.0f);
    // LastSimDrawSubmitMs 는 두 값을 더해 돌려주는 파생 값이라 이 등식은 항상 참이다.
    // 구간이 어긋났는지가 아니라 합산 경로가 바뀌지 않았는지만 고정한다.
    CHECK(timer.LastSimDrawSubmitMs() ==
          timer.LastUpdateMs() + timer.LastDrawSubmitMs());
    // frame wall 은 sim+draw-submit 보다 크거나 같아야 한다(HUD/EndDrawing을 포함).
    CHECK(timer.LastFrameWallMs() >= timer.LastSimDrawSubmitMs());

    // CommitFrame 은 현재 값을 head 자리에 쓰고 head 를 한 칸 민다.
    const float committedFrame  = timer.LastFrameWallMs();
    const float committedUpdate = timer.LastUpdateMs();
    const float committedDraw   = timer.LastDrawSubmitMs();
    timer.CommitFrame();
    CHECK(timer.Head() == 1);
    CHECK(timer.Filled() == 1);
    CHECK(timer.FrameWallMs()[0] == committedFrame);
    CHECK(timer.UpdateMs()[0] == committedUpdate);
    CHECK(timer.DrawMs()[0] == committedDraw);

    // 용량을 다 채우면 head 가 0으로 감기고 filled 는 kCapacity 에서 멈춘다.
    // 이 포화가 없으면 그래프가 읽는 구간이 배열 밖으로 자란다.
    constexpr std::size_t kCap = ppl::FrameTimer::kCapacity;
    for (std::size_t i = 1; i < kCap; ++i) timer.CommitFrame();
    CHECK(timer.Head() == 0);
    CHECK(timer.Filled() == kCap);
    timer.CommitFrame();
    CHECK(timer.Head() == 1);
    CHECK(timer.Filled() == kCap);
}

// 토글 축이 재구성을 요구하는지의 판정. main 루프가 프레임 끝에서 시뮬·벤치마크를
// 다시 만들지 말지를 이 함수 하나로 정하므로, 어느 축이 어느 쪽인지가 계약이다.
void TestToggleRestartRules() {
    const ppl::Toggles base;
    CHECK(base.entityPresetIdx == 1);
    CHECK(base.EntityCount() == 10000);
    CHECK(ppl::kEntityCountPresetCount == 3);
    // 같은 값끼리는 재구성이 없다. 이게 깨지면 매 프레임 시뮬을 새로 만든다.
    CHECK(!ppl::RequiresRestart(base, base));

    // 데이터 구조를 다시 만들어야 하는 네 축.
    ppl::Toggles entities = base;
    entities.entityPresetIdx = 2;
    CHECK(entities.EntityCount() == 100000);
    CHECK(ppl::RequiresRestart(base, entities));
    CHECK(ppl::RequiresRestart(entities, base));  // 판정은 방향에 무관하다

    ppl::Toggles layout = base;
    layout.layout = ppl::Layout::AoS;
    CHECK(ppl::RequiresRestart(base, layout));

    ppl::Toggles allocator = base;
    allocator.allocator = ppl::Allocator::Pool;
    CHECK(ppl::RequiresRestart(base, allocator));

    ppl::Toggles container = base;
    container.container = ppl::Container::StdList;
    CHECK(ppl::RequiresRestart(base, container));

    // 경로 선택만 바꾸는 두 축은 재구성 없이 다음 프레임부터 적용된다.
    // 여기에 simd/branchless 가 끼면 토글할 때마다 화면이 깜박인다.
    ppl::Toggles simd = base;
    simd.simd = ppl::SimdWidth::AVX2;
    CHECK(!ppl::RequiresRestart(base, simd));

    ppl::Toggles branchless = base;
    branchless.branchless = true;
    CHECK(!ppl::RequiresRestart(base, branchless));

    // 콤보박스가 보여 주는 라벨과 실제 엔티티 수가 어긋나면 측정이 거짓말을 한다.
    CHECK(std::string(ppl::kEntityCountPresets[0].label) == "1k");
    CHECK(ppl::kEntityCountPresets[0].count == 1000);
    CHECK(std::string(ppl::kEntityCountPresets[1].label) == "10k");
    CHECK(ppl::kEntityCountPresets[1].count == 10000);
    CHECK(std::string(ppl::kEntityCountPresets[2].label) == "100k");
    CHECK(ppl::kEntityCountPresets[2].count == 100000);
}

void TestAlignmentHelpers() {
    CHECK(ppl::IAllocator::IsValidAlignment(1));
    CHECK(ppl::IAllocator::IsValidAlignment(64));
    CHECK(!ppl::IAllocator::IsValidAlignment(0));
    CHECK(!ppl::IAllocator::IsValidAlignment(3));
    CHECK(ppl::AlignUp(65, 64) == 128);
}

void TestMallocAndArena() {
    ppl::MallocAllocator mallocAllocator;
    void* m = mallocAllocator.Allocate(37, 64);
    CHECK(m != nullptr);
    CHECK(IsAligned(m, 64));
    CHECK(mallocAllocator.LiveBytes() == 37);
    mallocAllocator.Deallocate(m, 37);
    CHECK(mallocAllocator.LiveBytes() == 0);
    CHECK(mallocAllocator.Allocate(8, 3) == nullptr);
    CHECK(mallocAllocator.Allocate(0, 16) == nullptr);
    CHECK(mallocAllocator.Allocate(
        std::numeric_limits<std::size_t>::max(), 64) == nullptr);
    CHECK(mallocAllocator.LiveBytes() == 0);

    ppl::ArenaAllocator arena(512);
    void* a = arena.Allocate(13, 16);
    void* b = arena.Allocate(29, 64);
    CHECK(a != nullptr && IsAligned(a, 16));
    CHECK(b != nullptr && IsAligned(b, 64));
    CHECK(arena.Allocate(8, 3) == nullptr);
    CHECK(arena.Used() >= 42);
    const auto usedBeforeFailure = arena.Used();
    const auto liveBeforeFailure = arena.LiveBytes();
    CHECK(arena.Allocate(std::numeric_limits<std::size_t>::max(), 64) == nullptr);
    CHECK(arena.Allocate(0, 16) == nullptr);
    CHECK(arena.Used() == usedBeforeFailure);
    CHECK(arena.LiveBytes() == liveBeforeFailure);
    arena.Reset();
    CHECK(arena.Used() == 0);
    CHECK(arena.LiveBytes() == 0);
    CHECK(arena.Allocate(512, 1) != nullptr);
    CHECK(arena.Allocate(1, 1) == nullptr);
    CHECK(arena.Used() == 512);
}

void TestPoolAndSlab() {
    ppl::PoolAllocator pool(32, 32, 3);
    void* slots[3] = {
        pool.Allocate(16, 16),
        pool.Allocate(16, 16),
        pool.Allocate(16, 16),
    };
    for (void* p : slots) CHECK(p != nullptr && IsAligned(p, 16));
    CHECK(pool.Allocate(16, 16) == nullptr);
    CHECK(pool.Allocate(8, 3) == nullptr);
    CHECK(pool.InUse() == 3);
    pool.Deallocate(slots[1], 16);
    CHECK(pool.InUse() == 2);
    void* reused = pool.Allocate(16, 16);
    CHECK(reused == slots[1]);

    ppl::SlabAllocator slab(2);
    void* overAligned = slab.Allocate(8, 64);
    CHECK(overAligned != nullptr);
    CHECK(IsAligned(overAligned, 64));
    CHECK(slab.ClassInUse(2) == 1);
    slab.Deallocate(overAligned, 8);
    CHECK(slab.ClassInUse(2) == 0);
    CHECK(slab.LiveBytes() == 0);
    CHECK(slab.Allocate(8, 3) == nullptr);
    CHECK(slab.Allocate(4096, 16) == nullptr);
}

// Pool 고갈 계약 — 블록을 다 쓰면 nullptr, 하나 반납하면 다시 성공한다.
void TestPoolExhaustion() {
    constexpr std::size_t kBlocks = 8;
    ppl::PoolAllocator pool(32, 8, kBlocks);
    CHECK(pool.BlockCount() == kBlocks);
    CHECK(pool.Free() == kBlocks);

    void* slots[kBlocks] = {};
    for (std::size_t i = 0; i < kBlocks; ++i) {
        slots[i] = pool.Allocate(32, 8);
        CHECK(slots[i] != nullptr);
        CHECK(IsAligned(slots[i], 8));
    }
    CHECK(pool.InUse() == kBlocks);
    CHECK(pool.Free() == 0);

    // 9번째 요청은 풀 고갈이라 반드시 nullptr.
    CHECK(pool.Allocate(32, 8) == nullptr);
    CHECK(pool.InUse() == kBlocks);

    // 하나 반납하면 그 슬롯이 그대로 재사용된다(free list LIFO).
    pool.Deallocate(slots[3], 32);
    CHECK(pool.InUse() == kBlocks - 1);
    CHECK(pool.Free() == 1);
    void* again = pool.Allocate(32, 8);
    CHECK(again == slots[3]);
    CHECK(pool.Free() == 0);
    CHECK(pool.Allocate(32, 8) == nullptr);

    // 블록 크기·정렬 사양을 벗어난 요청은 풀에 여유가 있어도 거절한다.
    pool.Reset();
    CHECK(pool.Free() == kBlocks);
    CHECK(pool.Allocate(64, 8) == nullptr);
    CHECK(pool.Allocate(32, 64) == nullptr);
    CHECK(pool.Free() == kBlocks);
}

// PROJECTS_PLAN이 요구한 "자료구조 결과 동등성".
// 세 컨테이너는 같은 시드로 같은 값 집합을 채우므로 노드 수는 정확히 같고,
// 합산 결과는 순회 순서 차이(intrusive list는 역순 링크)만큼만 달라야 한다.
void TestContainerEquivalence() {
    constexpr std::size_t kNodes = 5000;
    using Node = ppl::ContainerBenchmark::Node;

    // 노드 수보다 약간 넉넉한 풀. intrusive 경로가 여기서 노드를 받는다.
    ppl::PoolAllocator pool(sizeof(Node), alignof(Node), 5016);

    float baseline = 0.0f;
    for (int t = 0; t < ppl::kContainerCount; ++t) {
        const auto type = static_cast<ppl::Container>(t);
        const char* label = ppl::ContainerName(type);

        pool.Reset();
        ppl::ContainerBenchmark bench;
        bench.Init(type, &pool, kNodes);

        CHECK_AT(bench.Type() == type, label);
        CHECK_AT(bench.NodeCount() == kNodes, label);

        bench.RunFrame();
        const float sum = bench.LastSum();

        // 값 자체가 살아 있는지: 5000개 uniform[0,100) 합은 25만 근처다.
        CHECK_AT(std::isfinite(sum), label);
        CHECK_AT(sum > 0.0f, label);

        if (t == 0) {
            baseline = sum;
        } else {
            const float rel = std::fabs(sum - baseline) / std::fabs(baseline);
            CHECK_AT(rel < 1e-4f, label);
        }

        // 같은 프레임을 다시 돌려도 합은 변하지 않는다(순회는 상태를 바꾸지 않는다).
        bench.RunFrame();
        CHECK_AT(bench.LastSum() == sum, label);

        bench.Shutdown();
        CHECK_AT(bench.NodeCount() == 0, label);
        CHECK_AT(bench.LastSum() == 0.0f, label);
        CHECK_AT(bench.Status() == ppl::BenchStatus::Skipped, label);
    }

    // intrusive 경로에 allocator를 주지 않으면 노드를 하나도 만들 수 없다.
    ppl::ContainerBenchmark orphan;
    orphan.Init(ppl::Container::IntrusiveList, nullptr, kNodes);
    CHECK(orphan.NodeCount() == 0);
    CHECK(orphan.Status() == ppl::BenchStatus::Skipped);
    orphan.RunFrame();
    CHECK(orphan.LastSum() == 0.0f);

    // 풀이 요청보다 작으면 노드 수가 풀 용량에서 멈추고, 비교 무효로 표시된다.
    ppl::PoolAllocator tiny(sizeof(Node), alignof(Node), 64);
    ppl::ContainerBenchmark starved;
    starved.Init(ppl::Container::IntrusiveList, &tiny, kNodes);
    CHECK(starved.NodeCount() == 64);
    CHECK(starved.RequestedCount() == kNodes);
    CHECK(starved.Status() == ppl::BenchStatus::InvalidCapacity);
    CHECK(tiny.InUse() == 64);
    starved.Shutdown();
    CHECK(tiny.InUse() == 0);
}

// EventBenchmark가 문서에서 "정상 신호"로 규정한 프레임 계약:
//   프레임 종료 시 LiveBytes()==0, LastFrameAllocCount()==Target().
// 4종 할당자 전부에 대해 3프레임 연속으로 확인한다.
void TestEventBenchmarkContract() {
    constexpr std::size_t kTarget   = 200;
    constexpr std::size_t kNodeSize = ppl::EventBenchmark::kNodeSize;
    constexpr std::size_t kNodeAlign = ppl::EventBenchmark::kNodeAlign;

    ppl::MallocAllocator mallocAlloc;
    // Arena 용량은 한 프레임 분에 16바이트 여유뿐이다. RunFrame 끝의 Reset()이
    // 사라지면 두 번째 프레임부터 bump pointer가 고갈된다.
    ppl::ArenaAllocator arena(kTarget * kNodeSize + 16);
    ppl::PoolAllocator  pool(kNodeSize, kNodeAlign, kTarget);
    ppl::SlabAllocator  slab(kTarget);

    ppl::IAllocator* allocators[] = {&mallocAlloc, &arena, &pool, &slab};
    static_assert(sizeof(allocators) / sizeof(allocators[0]) == ppl::kAllocatorCount,
                  "EventBenchmark contract must cover every allocator toggle");

    for (int a = 0; a < ppl::kAllocatorCount; ++a) {
        const char* label = ppl::AllocatorName(static_cast<ppl::Allocator>(a));
        ppl::IAllocator* alloc = allocators[a];

        ppl::EventBenchmark bench;
        bench.Init(kTarget);
        CHECK_AT(bench.Target() == kTarget, label);

        for (int frame = 0; frame < 3; ++frame) {
            bench.RunFrame(alloc);
            CHECK_AT(bench.LastFrameAllocCount() == kTarget, label);
            CHECK_AT(bench.LastFrameAllocBytes() == kTarget * kNodeSize, label);
            CHECK_AT(alloc->FrameAllocCount() == kTarget, label);
            CHECK_AT(alloc->FrameAllocBytes() == kTarget * kNodeSize, label);
            // 프레임 끝에서 살아 있는 바이트가 0이어야 한다.
            CHECK_AT(alloc->LiveBytes() == 0, label);
        }
        // 누적 카운터는 3프레임 분이 그대로 쌓인다.
        CHECK_AT(alloc->AllocCount() == 3 * kTarget, label);
        CHECK_AT(alloc->FreeCount() == 3 * kTarget, label);
        CHECK_AT(alloc->PeakBytes() == kTarget * kNodeSize, label);
    }

    // 일괄 회수가 실제로 일어났는지는 할당자 고유 상태로 확인한다.
    // RunFrame 끝의 Reset()이 없으면 arena.Used()가 0으로 돌아오지 않는다.
    CHECK(arena.Used() == 0);
    CHECK(pool.InUse() == 0);
    CHECK(pool.Free() == kTarget);
    CHECK(slab.ClassInUse(1) == 0);  // 24바이트 요청 -> 32바이트 클래스

    // Pool 고갈: 블록 수가 target보다 적으면 count가 blockCount에서 멈춘다.
    ppl::PoolAllocator starvedPool(kNodeSize, kNodeAlign, 64);
    ppl::EventBenchmark starved;
    starved.Init(kTarget);
    starved.RunFrame(&starvedPool);
    CHECK(starved.LastFrameAllocCount() == 64);
    CHECK(starved.LastFrameAllocBytes() == 64 * kNodeSize);
    // 요청량을 못 채웠으니 더 빠른 결과로 순위화하면 안 된다.
    CHECK(starved.Status() == ppl::BenchStatus::InvalidCapacity);
    CHECK(starved.AttemptedCount() == 65);
    CHECK(starved.FailedCount() == 1);
    CHECK(starvedPool.LiveBytes() == 0);
    CHECK(starvedPool.InUse() == 0);

    // allocator가 없으면 워크로드를 건너뛴다.
    ppl::EventBenchmark idle;
    idle.Init(kTarget);
    idle.RunFrame(nullptr);
    CHECK(idle.LastFrameAllocCount() == 0);
    CHECK(idle.LastFrameAllocBytes() == 0);
    CHECK(idle.Status() == ppl::BenchStatus::Skipped);
}

// F01/F24: 생성자 입력 거부와 0바이트·실패 카운터·공간 회계 계약.
void TestAllocatorContractGuards() {
    // Pool 생성자: 잘못된 입력을 조용히 보정하지 않고 예외로 거부한다.
    bool threw = false;
    try { ppl::PoolAllocator bad(0, 8, 4); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { ppl::PoolAllocator bad(8, 3, 4); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { ppl::PoolAllocator bad(8, 8, 0); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    // 정렬 올림·용량 곱셈 overflow는 length_error(실측 ASan heap-overflow의 원인).
    threw = false;
    try { ppl::PoolAllocator bad(std::numeric_limits<std::size_t>::max(), 8, 1); }
    catch (const std::length_error&) { threw = true; }
    CHECK(threw);
    threw = false;
    try {
        ppl::PoolAllocator bad(16, 8, std::numeric_limits<std::size_t>::max() / 16 + 2);
    } catch (const std::length_error&) { threw = true; }
    CHECK(threw);

    // Allocate(0)은 모든 backend에서 nullptr이고 어떤 카운터도 바꾸지 않는다.
    ppl::MallocAllocator m;
    ppl::ArenaAllocator a(256);
    ppl::PoolAllocator pool(32, 8, 4);
    ppl::SlabAllocator slab(4);
    ppl::IAllocator* all[] = {&m, &a, &pool, &slab};
    for (ppl::IAllocator* x : all) {
        const auto ac = x->AllocCount();
        const auto fc = x->FailureCount();
        const auto lv = x->LiveBytes();
        CHECK(x->Allocate(0, 8) == nullptr);
        CHECK(x->AllocCount() == ac);
        CHECK(x->FailureCount() == fc);
        CHECK(x->LiveBytes() == lv);
    }

    // 실패는 FailureCount만 올리고 live는 그대로다.
    const auto live = m.LiveBytes();
    const auto fail = m.FailureCount();
    CHECK(m.Allocate(8, 3) == nullptr);
    CHECK(m.FailureCount() == fail + 1);
    CHECK(m.LiveBytes() == live);

    // 공간 회계: 24B 요청은 Slab 32B class. requested(live)와 slot bytes는 다르다.
    ppl::SlabAllocator s2(4);
    CHECK(s2.ReservedBacking() ==
          4 * (16 + 32 + 64 + 128 + 256 + 512 + 1024 + 2048));
    void* p24 = s2.Allocate(24, 8);
    CHECK(p24 != nullptr);
    CHECK(s2.LiveBytes() == 24);       // requested
    CHECK(s2.SlotBytesLive() == 32);   // 32B class stride
    s2.Deallocate(p24, 24);
    CHECK(s2.SlotBytesLive() == 0);
    CHECK(s2.LiveBytes() == 0);

    ppl::PoolAllocator p3(24, 8, 10);
    CHECK(p3.ReservedBacking() == 24 * 10);
}

// F25: Owns는 슬롯 시작 주소이면서 현재 할당된 포인터만 소유로 인정한다.
void TestPoolOwnership() {
    ppl::PoolAllocator pool(32, 8, 4);
    void* a = pool.Allocate(16, 8);
    void* b = pool.Allocate(16, 8);
    CHECK(pool.Owns(a));
    CHECK(pool.Owns(b));
    CHECK(!pool.Owns(nullptr));
    // interior 포인터는 슬롯 시작이 아니다.
    CHECK(!pool.Owns(static_cast<std::uint8_t*>(a) + 1));
    pool.Deallocate(a, 16);
    CHECK(!pool.Owns(a));  // 이미 반환된 포인터는 소유가 아니다
    CHECK(pool.Owns(b));
}

// F27/F29: 벤치마크 상태와 마지막 결과 초기화.
void TestBenchmarkStatusAndReset() {
    ppl::MallocAllocator m;
    ppl::EventBenchmark e;

    e.Init(0);
    e.RunFrame(&m);
    CHECK(e.Status() == ppl::BenchStatus::Skipped);
    CHECK(e.LastFrameAllocCount() == 0);

    e.Init(100);
    e.RunFrame(&m);
    CHECK(e.Status() == ppl::BenchStatus::Valid);
    CHECK(e.AttemptedCount() == 100);
    CHECK(e.FailedCount() == 0);

    // target=0으로 되돌린 뒤 직전 결과가 남지 않는다.
    e.Init(0);
    e.RunFrame(&m);
    CHECK(e.LastFrameAllocCount() == 0);
    CHECK(e.Status() == ppl::BenchStatus::Skipped);

    // 용량 부족은 더 빠른 결과가 아니라 비교 무효다.
    ppl::PoolAllocator tiny(ppl::EventBenchmark::kNodeSize,
                            ppl::EventBenchmark::kNodeAlign, 8);
    ppl::EventBenchmark e2;
    e2.Init(100);
    e2.RunFrame(&tiny);
    CHECK(e2.Status() == ppl::BenchStatus::InvalidCapacity);
    CHECK(e2.LastFrameAllocCount() == 8);
    CHECK(e2.FailedCount() == 1);

    // ContainerBenchmark: Shutdown/Init이 마지막 값을 비운다.
    ppl::ContainerBenchmark c;
    c.Init(ppl::Container::Vector, nullptr, 10);
    c.RunFrame();
    CHECK(c.Status() == ppl::BenchStatus::Valid);
    CHECK(c.RequestedCount() == 10);
    c.Shutdown();
    CHECK(c.LastSum() == 0.0f);
    CHECK(c.LastFrameMs() == 0.0f);
    CHECK(c.NodeCount() == 0);
    CHECK(c.Status() == ppl::BenchStatus::Skipped);

    // intrusive + allocator 없음 -> 노드를 못 만들어 Skipped.
    ppl::ContainerBenchmark c2;
    c2.Init(ppl::Container::IntrusiveList, nullptr, 10);
    CHECK(c2.Status() == ppl::BenchStatus::Skipped);
    CHECK(c2.NodeCount() == 0);
}

// F09: 요청 SIMD와 실제 실행 커널의 판정.
void TestKernelResolution() {
    using ppl::KernelFallback;

    // AoS는 이 lab에 수동 SIMD가 없다.
    const auto aosAvx = ppl::ResolveKernel(ppl::Layout::AoS, ppl::SimdWidth::AVX2, true);
    CHECK(aosAvx.effective == ppl::SimdWidth::Scalar);
    CHECK(aosAvx.reason == KernelFallback::LayoutHasNoSimd);
    CHECK(aosAvx.branchlessApplied);  // scalar 경로에는 predication이 걸린다
    const auto aosScalar = ppl::ResolveKernel(ppl::Layout::AoS, ppl::SimdWidth::Scalar, false);
    CHECK(aosScalar.reason == KernelFallback::None);

    const auto soaScalar = ppl::ResolveKernel(ppl::Layout::SoA, ppl::SimdWidth::Scalar, true);
    CHECK(soaScalar.effective == ppl::SimdWidth::Scalar);
    CHECK(soaScalar.branchlessApplied);

    const auto soaSse = ppl::ResolveKernel(ppl::Layout::SoA, ppl::SimdWidth::SSE, true);
#if defined(PERF_LAB_X86_SIMD)
    CHECK(soaSse.effective == ppl::SimdWidth::SSE);
    CHECK(!soaSse.branchlessApplied);  // SIMD는 이미 마스크 select
#else
    CHECK(soaSse.effective == ppl::SimdWidth::Scalar);
    CHECK(soaSse.reason == KernelFallback::NoIntrinsics);
#endif

    const auto soaAvx = ppl::ResolveKernel(ppl::Layout::SoA, ppl::SimdWidth::AVX2, false);
#if defined(PERF_LAB_X86_SIMD) && defined(PERF_LAB_AVX2)
    CHECK(soaAvx.effective == ppl::SimdWidth::AVX2);
    CHECK(soaAvx.reason == KernelFallback::None);
#elif defined(PERF_LAB_X86_SIMD)
    CHECK(soaAvx.effective == ppl::SimdWidth::SSE);
    CHECK(soaAvx.reason == KernelFallback::Avx2NotCompiled);
#else
    CHECK(soaAvx.effective == ppl::SimdWidth::Scalar);
    CHECK(soaAvx.reason == KernelFallback::NoIntrinsics);
#endif
}

// F04/F26: 유효 좌표 계약과 비정상 입력 처리.
void TestInputContracts() {
    const ppl::Layout layouts[] = {
        ppl::Layout::SoA, ppl::Layout::AoS, ppl::Layout::Archetype};

    for (ppl::Layout l : layouts) {
        auto sim = ppl::CreateSimulation(l);
        sim->Init(64, 0, -5);  // 비정상 viewport도 계약 안(>=2)으로 올린다
        CHECK(sim->Count() == 64);

        std::vector<float> before, after;
        sim->CopyState(before);

        // 유효 상태에서 dt=0/NaN/음수는 상태를 바꾸지 않는다.
        sim->Update(0.0f, ppl::SimdWidth::Scalar, false);
        sim->CopyState(after);
        CHECK(NearStates(before, after, 0.0f));

        sim->Update(std::numeric_limits<float>::quiet_NaN(),
                    ppl::SimdWidth::Scalar, true);
        sim->CopyState(after);
        CHECK(NearStates(before, after, 0.0f));

        sim->Update(-1.0f, ppl::SimdWidth::Scalar, false);
        sim->CopyState(after);
        CHECK(NearStates(before, after, 0.0f));
    }

    // 초기 좌표가 Update 상한 안에 있다(0 <= x <= W-1, 0 <= y <= H-1).
    auto sim = ppl::CreateSimulation(ppl::Layout::SoA);
    sim->Init(100, 640, 480);
    std::vector<float> state;
    sim->CopyState(state);
    int outOfRange = 0;
    for (std::size_t i = 0; i < 100; ++i) {
        const float px = state[i * 4 + 0];
        const float py = state[i * 4 + 1];
        if (px < 0.0f || px > 639.0f || py < 0.0f || py > 479.0f) ++outOfRange;
    }
    CHECK(outOfRange == 0);
}

// ── 시뮬 레이아웃 검증 ────────────────────────────────────────────────

constexpr std::size_t kEntities = 513;  // 513%4=1 로 SIMD 잔여, 2*256+1 로 부분 chunk

struct Combo {
    ppl::Layout    layout;
    ppl::SimdWidth simd;
    bool           branchless;
};

std::string ComboName(const Combo& c) {
    std::string name = ppl::LayoutName(c.layout);
    name += '/';
    name += ppl::SimdName(c.simd);
    name += c.branchless ? "/branchless" : "/branched";
    return name;
}

// {SoA, AoS, Archetype} x {Scalar, SSE} x {branchless off, on} = 12조합.
// AVX2 빌드에서는 SimdWidth::AVX2 가 더해져 18조합이 된다.
std::vector<Combo> AllCombos() {
    static const ppl::SimdWidth kWidths[] = {
        ppl::SimdWidth::Scalar,
        ppl::SimdWidth::SSE,
#if defined(PERF_LAB_AVX2)
        ppl::SimdWidth::AVX2,
#endif
    };
    std::vector<Combo> combos;
    for (int l = 0; l < ppl::kLayoutCount; ++l) {
        for (ppl::SimdWidth w : kWidths) {
            for (int b = 0; b < 2; ++b) {
                combos.push_back(Combo{static_cast<ppl::Layout>(l), w, b != 0});
            }
        }
    }
    return combos;
}

std::unique_ptr<ppl::ISimulation> MakeAndInit(ppl::Layout layout,
                                              int w = 640, int h = 480) {
    auto sim = ppl::CreateSimulation(layout);
    sim->Init(kEntities, w, h);
    return sim;
}

// SSE 경로는 _mm_add_ps(px, _mm_mul_ps(vx, dtv)) 라 스칼라와 연산 순서·반올림이
// 같다(실측 최대 차이 0). AVX2 경로만 _mm256_fmadd_ps 로 곱셈 뒤 중간 반올림이
// 사라져 위치가 조금씩 어긋난다. 실측하면 그 차이는 발산이 아니라 프레임당
// 약 1.53e-5(=2^-16)씩 선형으로만 늘어난다 — 200프레임까지 경계 반사 판정이
// 갈리는 지점은 나타나지 않았다. 그래서 허용 오차를 프레임 수에 비례해 잡고
// 실측값의 2배를 여유로 둔다. 상수를 프레임 수와 묶어 두면 "드리프트가 선형"
// 이라는 주장 자체가 회귀 검사 대상이 된다.
constexpr float kAvx2DriftPerFrame = 1.6e-5f;

float ToleranceFor(ppl::SimdWidth simd, int frames) {
    if (simd != ppl::SimdWidth::AVX2) return 1e-4f;
    return kAvx2DriftPerFrame * static_cast<float>(frames) * 2.0f;
}

void TestLayoutEquivalence() {
    constexpr int   kFrames = 180;
    constexpr float kDt     = 1.0f / 120.0f;

    // baseline: SoA / Scalar / branched.
    std::vector<float> baselineInit, baseline;
    {
        auto sim = MakeAndInit(ppl::Layout::SoA);
        sim->CopyState(baselineInit);
        for (int i = 0; i < kFrames; ++i) {
            sim->Update(kDt, ppl::SimdWidth::Scalar, false);
        }
        sim->CopyState(baseline);
    }
    CHECK(baseline.size() == kEntities * 4);

    for (const Combo& c : AllCombos()) {
        const std::string label = ComboName(c);
        auto sim = MakeAndInit(c.layout);
        CHECK_AT(sim->Count() == kEntities, label);

        // 초기 상태는 세 레이아웃이 비트 단위로 같아야 한다(같은 시드).
        std::vector<float> state;
        sim->CopyState(state);
        CHECK_AT(NearStates(state, baselineInit, 0.0f), label);

        for (int i = 0; i < kFrames; ++i) {
            sim->Update(kDt, c.simd, c.branchless);
        }
        sim->CopyState(state);
        CHECK_AT(NearStates(state, baseline, ToleranceFor(c.simd, kFrames)), label);
    }
}

// SIMD 폭 토글이 결과를 바꾸지 않는다는 것. 폭을 바꿔 ms 를 비교하려면
// 세 폭이 *같은 계산* 이어야 하고, 그게 아니면 빨라진 게 아니라 덜 한 것이다.
void TestSimdEquivalence() {
    constexpr int   kFrames = 240;
    constexpr float kDt     = 1.0f / 144.0f;

    auto scalar = MakeAndInit(ppl::Layout::SoA);
    auto simd   = MakeAndInit(ppl::Layout::SoA);
#if defined(PERF_LAB_AVX2)
    auto wide = MakeAndInit(ppl::Layout::SoA);
#endif

    for (int i = 0; i < kFrames; ++i) {
        scalar->Update(kDt, ppl::SimdWidth::Scalar, false);
        simd->Update(kDt, ppl::SimdWidth::SSE, true);
#if defined(PERF_LAB_AVX2)
        wide->Update(kDt, ppl::SimdWidth::AVX2, true);
#endif
    }

    std::vector<float> a, c;
    scalar->CopyState(a);
    simd->CopyState(c);
    CHECK(NearStates(a, c, 1e-4f));
#if defined(PERF_LAB_AVX2)
    // AVX2는 FMA로 pos += vel*dt 의 중간 반올림이 사라져 스칼라와 비트가 다르다.
    // SSE와 같은 허용 오차를 그대로 쓰지 않고 프레임 수에 비례한 상한을 쓴다.
    //
    // "수치가 다르다"는 실제 실행 경로의 증거가 아니다(FMA를 써도 같은 값이 나올 수
    // 있고, 잘못된 계산도 다른 값을 낸다). 실행 경로는 KernelResolution 테스트의
    // ResolveKernel 판정으로 확인하고, 여기서는 정확성만 본다.
    std::vector<float> d;
    wide->CopyState(d);
    CHECK(NearStates(a, d, ToleranceFor(ppl::SimdWidth::AVX2, kFrames)));
#endif

    // AoS 는 SIMD 토글을 통째로 무시하고 언제나 scalar 경로를 돈다(stride 때문에
    // 인트린식 이득이 없다). 그래서 세 폭의 결과가 *비트까지* 같아야 한다.
    // 이 단언이 깨지면 AoS 에 조용히 SIMD 경로가 생겼거나 토글이 새고 있다는 뜻이다.
    std::vector<float> aosState[ppl::kSimdCount];
    const ppl::SimdWidth kWidths[] = {
        ppl::SimdWidth::Scalar, ppl::SimdWidth::SSE, ppl::SimdWidth::AVX2};
    for (int w = 0; w < ppl::kSimdCount; ++w) {
        auto aos = MakeAndInit(ppl::Layout::AoS);
        for (int i = 0; i < kFrames; ++i) aos->Update(kDt, kWidths[w], false);
        aos->CopyState(aosState[w]);
    }
    CHECK(NearStates(aosState[0], aosState[1], 0.0f));
    CHECK(NearStates(aosState[0], aosState[2], 0.0f));
}

// branchless 토글이 결과를 바꾸지 않는다는 것. predication 은 *같은 값* 을
// 다른 명령으로 내는 변형이라, 결과가 달라지면 그것은 최적화가 아니라 버그다.
void TestBranchlessEquivalence() {
    constexpr int   kFrames = 240;
    constexpr float kDt     = 1.0f / 144.0f;

    auto branched   = MakeAndInit(ppl::Layout::SoA);
    auto predicated = MakeAndInit(ppl::Layout::SoA);
    for (int i = 0; i < kFrames; ++i) {
        branched->Update(kDt, ppl::SimdWidth::Scalar, false);
        predicated->Update(kDt, ppl::SimdWidth::Scalar, true);
    }
    std::vector<float> a, b;
    branched->CopyState(a);
    predicated->CopyState(b);
    CHECK(NearStates(a, b));

    // 세 레이아웃 모두에서 같은 성질이 성립해야 한다. clamp 는 범위 안에서
    // 항등이므로 predication 이 무조건 clamp 를 걸어도 값이 달라지지 않는다.
    for (int l = 0; l < ppl::kLayoutCount; ++l) {
        const auto layout = static_cast<ppl::Layout>(l);
        const char* label = ppl::LayoutName(layout);

        auto off = MakeAndInit(layout);
        auto on  = MakeAndInit(layout);
        for (int i = 0; i < kFrames; ++i) {
            off->Update(kDt, ppl::SimdWidth::Scalar, false);
            on->Update(kDt, ppl::SimdWidth::Scalar, true);
        }
        std::vector<float> scalarOff, scalarOn;
        off->CopyState(scalarOff);
        on->CopyState(scalarOn);
        CHECK_AT(NearStates(scalarOff, scalarOn), label);

        // SIMD 경로는 이미 마스크 select 라 이 토글에 영향받지 않는다.
        // "체크박스를 켜도 SSE 그래프는 평평하다" 는 관측의 근거가 이 등식이다.
        // 비트까지 같아야 한다 — SoA·Archetype 은 두 경우 모두 같은 SSE 함수를 돌고,
        // AoS 는 scalar 로 폴백해 서로 다른 루프를 돌지만 위의 clamp 항등성이 값을 지킨다.
        auto simdOff = MakeAndInit(layout);
        auto simdOn  = MakeAndInit(layout);
        for (int i = 0; i < kFrames; ++i) {
            simdOff->Update(kDt, ppl::SimdWidth::SSE, false);
            simdOn->Update(kDt, ppl::SimdWidth::SSE, true);
        }
        std::vector<float> sseOff, sseOn;
        simdOff->CopyState(sseOff);
        simdOn->CopyState(sseOn);
        CHECK_AT(NearStates(sseOff, sseOn, 0.0f), label);
    }
}

// 절대 기준 불변식. 구현 간 동등성만으로는 워크로드 규칙 자체를 지워도 통과하므로,
// 문서가 규정한 "화면 경계에서 반사한다"를 직접 검사한다.
void TestBoundaryInvariants() {
    struct Setup {
        int   w;
        int   h;
        float dt;
        int   frames;
        bool  requireReflection;
    };
    // 넓은 뷰포트: 위치 범위와 |v| 보존을 확인한다.
    // 좁은 뷰포트 + 큰 dt: 모든 엔티티가 여러 번 반사하도록 강제한다.
    const Setup setups[] = {
        {1280, 580, 1.0f / 120.0f, 200, false},
        {  16,  16, 0.05f,         200, true},
    };

    for (const Setup& s : setups) {
        const float maxX = static_cast<float>(s.w) - 1.0f;
        const float maxY = static_cast<float>(s.h) - 1.0f;

        for (const Combo& c : AllCombos()) {
            const std::string label =
                ComboName(c) + " @" + std::to_string(s.w) + "x" + std::to_string(s.h);
            auto sim = MakeAndInit(c.layout, s.w, s.h);

            std::vector<float> prev, cur;
            sim->CopyState(prev);
            std::vector<float> absV0(kEntities * 2);
            for (std::size_t i = 0; i < kEntities; ++i) {
                absV0[i * 2 + 0] = std::fabs(prev[i * 4 + 2]);
                absV0[i * 2 + 1] = std::fabs(prev[i * 4 + 3]);
            }

            std::vector<char> flippedX(kEntities, 0), flippedY(kEntities, 0);
            int boundsViolations = 0;
            int speedViolations  = 0;

            for (int f = 0; f < s.frames; ++f) {
                sim->Update(s.dt, c.simd, c.branchless);
                sim->CopyState(cur);

                for (std::size_t i = 0; i < kEntities; ++i) {
                    const float px = cur[i * 4 + 0];
                    const float py = cur[i * 4 + 1];
                    const float vx = cur[i * 4 + 2];
                    const float vy = cur[i * 4 + 3];

                    // (a) 갱신 후 모든 엔티티가 뷰포트 안에 있다.
                    if (!(px >= 0.0f && px <= maxX && py >= 0.0f && py <= maxY)) {
                        ++boundsViolations;
                    }
                    // (b) 반사는 부호만 뒤집으므로 |v|는 성분별로 보존된다.
                    if (std::fabs(std::fabs(vx) - absV0[i * 2 + 0]) > 1e-4f ||
                        std::fabs(std::fabs(vy) - absV0[i * 2 + 1]) > 1e-4f) {
                        ++speedViolations;
                    }
                    // (c) 이번 프레임에 속도 부호가 뒤집혔는가.
                    if (prev[i * 4 + 2] * vx < 0.0f) flippedX[i] = 1;
                    if (prev[i * 4 + 3] * vy < 0.0f) flippedY[i] = 1;
                }
                prev.swap(cur);
            }

            CHECK_AT(boundsViolations == 0, label);
            CHECK_AT(speedViolations == 0, label);

            // |v| 멀티셋 보존(정렬 후 원소별 비교).
            std::vector<float> absVn(kEntities * 2);
            for (std::size_t i = 0; i < kEntities; ++i) {
                absVn[i * 2 + 0] = std::fabs(prev[i * 4 + 2]);
                absVn[i * 2 + 1] = std::fabs(prev[i * 4 + 3]);
            }
            std::vector<float> sorted0 = absV0, sortedN = absVn;
            std::sort(sorted0.begin(), sorted0.end());
            std::sort(sortedN.begin(), sortedN.end());
            CHECK_AT(NearStates(sorted0, sortedN, 1e-4f), label);

            if (!s.requireReflection) continue;

            const std::size_t nx = static_cast<std::size_t>(
                std::count(flippedX.begin(), flippedX.end(), char(1)));
            const std::size_t ny = static_cast<std::size_t>(
                std::count(flippedY.begin(), flippedY.end(), char(1)));

            // 벡터 본체(SIMD 주 루프)의 반사가 사라지면 여기서 걸린다. 이 설정에서
            // 실측은 nx=511, ny=510 (513개 중 속도가 거의 0인 몇 개는 200프레임
            // 안에 경계에 닿지 못한다). 주 루프가 죽으면 잔여 구간의 1개만 남아
            // nx<=1 로 떨어지므로, 임계값은 그 사이 아무 곳이나 두면 된다.
            // 환경별 미세 차이에 흔들리지 않도록 절반으로 잡는다.
            CHECK_AT(nx >= kEntities / 2, label);
            CHECK_AT(ny >= kEntities / 2, label);
            // 잔여(count%4) 처리 구간의 반사가 사라지면 여기서 걸린다.
            // 총합만 세면 주 루프 512개에 가려 드러나지 않는 지점이다.
            CHECK_AT(flippedX[kEntities - 1] == 1, label);
            CHECK_AT(flippedY[kEntities - 1] == 1, label);
        }
    }
}

// HUD 토글 패널의 기하. raygui 위젯 자체는 창·입력 컨텍스트가 있어야 돌아가서
// headless 로 클릭을 재현할 수 없지만, "펼친 목록이 아래 행 위젯을 덮는다" 는
// 순수 계산이라 여기서 고정한다. 이 겹침이 hud.cpp 가 GuiLock + 지연 그리기를
// 하는 이유 전부다. 레이아웃을 바꿔 이 CHECK 가 깨진다면 그 잠금·지연이 여전히
// 필요한지 다시 판단하고 이 테스트를 함께 고쳐야 한다.
void TestTogglePanelGeometry() {
    using ppl::DropdownOpenRect;
    using ppl::PanelColumn2X;
    using ppl::PanelDrawsWidget;
    using ppl::PanelFieldRect;
    using ppl::PanelRowY;
    using ppl::RectsOverlap;
    using ppl::UiRect;

    constexpr int x = 320;
    constexpr int y = 700;

    // 행 간격 24 > 위젯 높이 22. *접힌* 위젯끼리는 겹치지 않으므로 한 번의
    // 클릭이 콤보를 둘 동시에 열 수 없다. hud.cpp 의 두 번째 패스가
    // "열린 콤보는 최대 하나" 를 가정하는 근거다.
    CHECK(ppl::kPanelRowH == 22);
    CHECK(PanelRowY(y, 1) - PanelRowY(y, 0) == 24);
    CHECK(!RectsOverlap(PanelFieldRect(x, y, 0), PanelFieldRect(x, y, 1)));

    const UiRect entities  = PanelFieldRect(x, y, 0);
    const UiRect allocator = PanelFieldRect(x, y, 1);
    const UiRect layout    = PanelFieldRect(x, y, 2);

    // 1열 세 콤보는 x·폭이 완전히 같다. 펼치면 세로로만 겹친다.
    CHECK(entities.x == allocator.x && entities.x == layout.x);
    CHECK(entities.w == allocator.w && entities.w == layout.w);

    // raygui 4.0: boundsOpen.height = (itemCount+1)*(height + DROPDOWN_ITEMS_SPACING).
    // entities 는 "1k;10k;100k" 세 항목 → 4 * (22+2) = 96.
    const UiRect entitiesOpen = DropdownOpenRect(entities, 3);
    CHECK(entitiesOpen.h == 96);

    // 펼친 entities 목록이 allocator·layout 필드 자리를 덮는다(보고된 결함).
    CHECK(RectsOverlap(entitiesOpen, allocator));
    CHECK(RectsOverlap(entitiesOpen, layout));
    // allocator 는 네 항목이라 5*24=120, 역시 layout 을 덮는다.
    CHECK(DropdownOpenRect(allocator, 4).h == 120);
    CHECK(RectsOverlap(DropdownOpenRect(allocator, 4), layout));

    // 2열은 가로로 떨어져 있어 1열의 펼친 목록과 겹치지 않는다.
    const UiRect container = PanelFieldRect(PanelColumn2X(x), y, 0);
    CHECK(!RectsOverlap(entitiesOpen, container));
    // 2열 안에서는 다시 겹친다(container 위에 simd).
    CHECK(RectsOverlap(DropdownOpenRect(container, 3),
                       PanelFieldRect(PanelColumn2X(x), y, 1)));

    // 0 이하 크기는 면적 0이라 겹치지 않는다.
    CHECK(!RectsOverlap(UiRect{5, 5, 0, 1}, UiRect{0, 0, 10, 10}));
    CHECK(!RectsOverlap(UiRect{0, 0, 10, 10}, UiRect{5, 5, 0, 0}));
    CHECK(!RectsOverlap(UiRect{0, 0, 0, 0}, UiRect{0, 0, 0, 0}));

    // 기본 창(720)에서 패널 상단을 올려 모든 열린 콤보가 창 안에 들어온다.
    // 올리기 전 기본 배치(y=592)는 allocator 팝업이 736으로 넘친다(보고된 결함).
    CHECK(!ppl::PanelOpenFitsWindow(592, 720));
    const int panelY = ppl::PanelOriginY(592, 720);
    CHECK(panelY <= 592);
    CHECK(ppl::PanelOpenFitsWindow(panelY, 720));

    // 두 패스 그리기: 모든 위젯이 정확히 한 번 그려지고, 열린 콤보만 두 번째
    // (= 마지막) 패스에 그려진다.
    CHECK(PanelDrawsWidget(/*widgetOpen=*/false, /*openPass=*/false));
    CHECK(!PanelDrawsWidget(/*widgetOpen=*/false, /*openPass=*/true));
    CHECK(!PanelDrawsWidget(/*widgetOpen=*/true, /*openPass=*/false));
    CHECK(PanelDrawsWidget(/*widgetOpen=*/true, /*openPass=*/true));
}

// 그래프 패널이 세 라인을 한 시간축에 겹쳐 그리려면 네 채널이 *같은 인덱스* 에서
// 같은 프레임을 가리켜야 한다. alloc/container 는 벤치마크가 재서 넘겨 주는
// 외부 측정값이라 FrameTimer 는 그것을 밀어 넣기만 한다.
void TestFrameTimerChannels() {
    ppl::FrameTimer timer;
    timer.CommitFrame(0.25f, 1.5f);
    CHECK(timer.LastAllocMs() == 0.25f);
    CHECK(timer.LastContainerMs() == 1.5f);
    CHECK(timer.AllocMs()[0] == 0.25f);
    CHECK(timer.ContainerMs()[0] == 1.5f);

    // 인자를 생략한 호출은 두 채널을 0으로 되돌린다 — 워크로드가 없는 프레임이
    // 직전 값을 그대로 물려받아 그래프에 유령 라인을 남기면 안 된다.
    timer.CommitFrame();
    CHECK(timer.LastAllocMs() == 0.0f);
    CHECK(timer.LastContainerMs() == 0.0f);
    CHECK(timer.AllocMs()[1] == 0.0f);
    CHECK(timer.ContainerMs()[1] == 0.0f);
    CHECK(timer.Head() == 2);

    // 링을 한 바퀴 넘겨도 네 채널이 같은 head 로 함께 밀린다.
    constexpr std::size_t kCap = ppl::FrameTimer::kCapacity;
    ppl::FrameTimer rolled;
    for (std::size_t i = 0; i < kCap + 7; ++i) {
        rolled.CommitFrame(static_cast<float>(i), static_cast<float>(i) * 2.0f);
    }
    CHECK(rolled.Filled() == kCap);
    CHECK(rolled.Head() == 7);

    int misaligned = 0;
    for (std::size_t i = 0; i < kCap; ++i) {
        if (rolled.ContainerMs()[i] != rolled.AllocMs()[i] * 2.0f) ++misaligned;
    }
    CHECK(misaligned == 0);

    // head 는 *다음에 쓸* 자리다. 그래서 가장 최근 샘플은 head-1, 가장 오래된
    // 샘플은 head 에 있다. 그래프가 왼쪽부터 그리는 순서가 이 규칙에서 나온다.
    CHECK(rolled.AllocMs()[6] == static_cast<float>(kCap + 6));
    CHECK(rolled.AllocMs()[7] == 7.0f);
}

// ── 실행 단위 ────────────────────────────────────────────────────────
//
// group 은 그 검사를 *추가한* step 을 가리킨다. CMake 가 group 마다 add_test 를
// 하나씩 등록하므로, ctest 이름 하나가 문서 한 편에 대응한다.
struct TestCase {
    const char* group;
    const char* name;
    void (*run)();
};

const TestCase kTests[] = {
    {"skeleton",  "FrameTimerWindow",       TestFrameTimerWindow},
    {"toggles",   "ToggleRestartRules",     TestToggleRestartRules},
    {"toggles",   "TogglePanelGeometry",    TestTogglePanelGeometry},
    {"layout",    "LayoutEquivalence",      TestLayoutEquivalence},
    {"layout",    "BoundaryInvariants",     TestBoundaryInvariants},
    {"simd",      "SimdEquivalence",        TestSimdEquivalence},
    {"simd",      "KernelResolution",       TestKernelResolution},
    {"branch",    "BranchlessEquivalence",  TestBranchlessEquivalence},
    {"layout",    "InputContracts",         TestInputContracts},
    {"allocator", "AlignmentHelpers",       TestAlignmentHelpers},
    {"allocator", "MallocAndArena",         TestMallocAndArena},
    {"allocator", "PoolAndSlab",            TestPoolAndSlab},
    {"allocator", "PoolExhaustion",         TestPoolExhaustion},
    {"allocator", "AllocatorContractGuards", TestAllocatorContractGuards},
    {"allocator", "PoolOwnership",          TestPoolOwnership},
    {"allocator", "BenchmarkStatusAndReset", TestBenchmarkStatusAndReset},
    {"allocator", "EventBenchmarkContract", TestEventBenchmarkContract},
    {"container", "ContainerEquivalence",   TestContainerEquivalence},
    {"graph",     "FrameTimerChannels",     TestFrameTimerChannels},
};
constexpr int kTestCount = static_cast<int>(sizeof(kTests) / sizeof(kTests[0]));

}  // namespace

int main(int argc, char** argv) {
    const char* filter = (argc > 1) ? argv[1] : nullptr;
    const char* label  = filter ? filter : "all";

    std::cout << "parts_core_tests: group `" << label << "`\n";

    int selected = 0;
    for (const TestCase& test : kTests) {
        if (filter && std::strcmp(filter, test.group) != 0) continue;
        ++selected;

        const long long before        = checks;
        const int       beforeFailure = failures;
        test.run();

        std::cout << "  " << test.group << '/' << test.name << " ... "
                  << (checks - before) << " check(s) "
                  << (failures == beforeFailure ? "ok" : "FAILED") << '\n';
    }

    if (selected == 0) {
        std::cerr << "unknown group `" << label << "`. groups:";
        for (int i = 0; i < kTestCount; ++i) {
            if (i == 0 || std::strcmp(kTests[i].group, kTests[i - 1].group) != 0) {
                std::cerr << ' ' << kTests[i].group;
            }
        }
        std::cerr << '\n';
        return 2;
    }

    std::cout << label << ": " << selected << " test(s), " << checks
              << " check(s), " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
