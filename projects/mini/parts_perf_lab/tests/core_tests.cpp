// parts_perf_lab 계산 코어 자동 검증.
//
// 이 타깃은 raylib을 링크하지 않는다. 레이아웃 구현의 Draw()는 별도 번역 단위에
// 있고 여기서는 컴파일하지 않으므로, 그래픽 스택 없이 Update/CopyState와
// 할당자·벤치마크 워크로드만 검증한다.
#include "alloc/arena_allocator.h"
#include "alloc/malloc_allocator.h"
#include "alloc/pool_allocator.h"
#include "alloc/slab_allocator.h"
#include "measure/container_benchmark.h"
#include "measure/event_benchmark.h"
#include "sim/isimulation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':'  \
                      << __LINE__ << ")\n";                                   \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

// 조합 순회 테스트용. 어떤 조합에서 깨졌는지 라벨을 함께 찍는다.
#define CHECK_AT(expr, label)                                                 \
    do {                                                                      \
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

    ppl::ArenaAllocator arena(512);
    void* a = arena.Allocate(13, 16);
    void* b = arena.Allocate(29, 64);
    CHECK(a != nullptr && IsAligned(a, 16));
    CHECK(b != nullptr && IsAligned(b, 64));
    CHECK(arena.Allocate(8, 3) == nullptr);
    CHECK(arena.Used() >= 42);
    arena.Reset();
    CHECK(arena.Used() == 0);
    CHECK(arena.LiveBytes() == 0);
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
    }

    // intrusive 경로에 allocator를 주지 않으면 노드를 하나도 만들 수 없다.
    ppl::ContainerBenchmark orphan;
    orphan.Init(ppl::Container::IntrusiveList, nullptr, kNodes);
    CHECK(orphan.NodeCount() == 0);
    orphan.RunFrame();
    CHECK(orphan.LastSum() == 0.0f);

    // 풀이 요청보다 작으면 노드 수가 풀 용량에서 멈춘다.
    ppl::PoolAllocator tiny(sizeof(Node), alignof(Node), 64);
    ppl::ContainerBenchmark starved;
    starved.Init(ppl::Container::IntrusiveList, &tiny, kNodes);
    CHECK(starved.NodeCount() == 64);
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
    CHECK(starvedPool.LiveBytes() == 0);
    CHECK(starvedPool.InUse() == 0);

    // allocator가 없으면 워크로드를 건너뛴다.
    ppl::EventBenchmark idle;
    idle.Init(kTarget);
    idle.RunFrame(nullptr);
    CHECK(idle.LastFrameAllocCount() == 0);
    CHECK(idle.LastFrameAllocBytes() == 0);
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

void TestBranchAndSimdEquivalence() {
    constexpr int kFrames = 240;

    auto branched   = MakeAndInit(ppl::Layout::SoA);
    auto predicated = MakeAndInit(ppl::Layout::SoA);
    auto simd       = MakeAndInit(ppl::Layout::SoA);
#if defined(PERF_LAB_AVX2)
    auto wide = MakeAndInit(ppl::Layout::SoA);
#endif

    for (int i = 0; i < kFrames; ++i) {
        constexpr float dt = 1.0f / 144.0f;
        branched->Update(dt, ppl::SimdWidth::Scalar, false);
        predicated->Update(dt, ppl::SimdWidth::Scalar, true);
        simd->Update(dt, ppl::SimdWidth::SSE, true);
#if defined(PERF_LAB_AVX2)
        wide->Update(dt, ppl::SimdWidth::AVX2, true);
#endif
    }

    std::vector<float> a, b, c;
    branched->CopyState(a);
    predicated->CopyState(b);
    simd->CopyState(c);
    CHECK(NearStates(a, b));
    CHECK(NearStates(a, c, 1e-4f));
#if defined(PERF_LAB_AVX2)
    // AVX2는 FMA로 pos += vel*dt 의 중간 반올림이 사라져 스칼라와 비트가 다르다.
    // SSE와 같은 허용 오차를 그대로 쓰지 않고 프레임 수에 비례한 상한을 쓴다.
    std::vector<float> d;
    wide->CopyState(d);
    CHECK(NearStates(a, d, ToleranceFor(ppl::SimdWidth::AVX2, kFrames)));
    // 그래도 SSE 만큼 가깝지는 않다는 것까지 못 박아 둔다. 이 단언이 깨지면
    // AVX2 토글이 실제로는 SSE/스칼라로 폴백하고 있다는 뜻이다.
    CHECK(!NearStates(a, d, 1e-6f));
#endif
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

}  // namespace

int main() {
    TestAlignmentHelpers();
    TestMallocAndArena();
    TestPoolAndSlab();
    TestPoolExhaustion();
    TestContainerEquivalence();
    TestEventBenchmarkContract();
    TestLayoutEquivalence();
    TestBranchAndSimdEquivalence();
    TestBoundaryInvariants();
    if (failures != 0) {
        std::cerr << failures << " parts core test(s) failed\n";
        return 1;
    }
    std::cout << "parts core tests passed\n";
    return 0;
}
