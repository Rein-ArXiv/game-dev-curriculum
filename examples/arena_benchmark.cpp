// arena_benchmark.cpp
// Visual Studio에서 실행: Ctrl+F5 (디버그 없이 시작)
// 또는 콘솔에서: cl /O2 arena_benchmark.cpp && arena_benchmark.exe

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <chrono>

// ============================================================================
// Arena Allocator 구현
// ============================================================================

#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))

// 최적화 방지 싱크: 할당 결과를 관측 가능하게 만들어
// 컴파일러(/O2, -O2)가 할당 루프를 통째로 제거하는 것을 막는다.
// (이게 없으면 arena 루프는 부작용이 offset 증가뿐이라 루프 전체가
//  상수 대입으로 접혀 0.000 ms로 측정된다 — 죽은 코드 제거)
volatile uint64_t g_sink = 0;

struct Arena {
    uint8_t* base;      // 메모리 블록 시작 주소
    size_t   size;      // 전체 크기
    size_t   offset;    // 현재 사용 오프셋 (bump pointer)
};

// Arena 생성
Arena arena_create(size_t capacity) {
    Arena a;
    a.base = (uint8_t*)malloc(capacity);
    a.size = capacity;
    a.offset = 0;
    return a;
}

// 정렬된 메모리 할당
void* arena_alloc(Arena* a, size_t alloc_size, size_t align) {
    // 정렬 조정
    size_t aligned_offset = ALIGN_UP(a->offset, align);

    // 공간 확인
    if (aligned_offset + alloc_size > a->size) {
        return nullptr;  // 공간 부족
    }

    void* ptr = a->base + aligned_offset;
    a->offset = aligned_offset + alloc_size;
    return ptr;
}

// Arena 전체 리셋 (메모리 해제 없이 재사용)
void arena_reset(Arena* a) {
    a->offset = 0;
}

// Arena 소멸
void arena_destroy(Arena* a) {
    free(a->base);
    a->base = nullptr;
    a->size = 0;
    a->offset = 0;
}

// ============================================================================
// 벤치마크
// ============================================================================

void benchmark_malloc() {
    printf("=== malloc 벤치마크 ===\n");

    const int COUNT = 1000000;
    void** ptrs = (void**)malloc(COUNT * sizeof(void*));

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < COUNT; i++) {
        ptrs[i] = malloc(64);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto alloc_time = std::chrono::duration<double, std::milli>(end - start).count();

    start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < COUNT; i++) {
        free(ptrs[i]);
    }

    end = std::chrono::high_resolution_clock::now();
    auto free_time = std::chrono::duration<double, std::milli>(end - start).count();

    printf("  할당 (%d회 x 64B): %.3f ms\n", COUNT, alloc_time);
    printf("  해제: %.3f ms\n", free_time);
    printf("  총합: %.3f ms\n", alloc_time + free_time);
    printf("  평균: %.1f ns/alloc\n\n", (alloc_time * 1000000.0) / COUNT);

    free(ptrs);
}

void benchmark_arena() {
    printf("=== Arena 벤치마크 ===\n");

    const int COUNT = 1000000;

    // Arena 생성 (64,000,000 B ≈ 61MiB - 100만개 x 64B)
    Arena arena = arena_create(64 * COUNT);

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < COUNT; i++) {
        g_sink += (uint64_t)(uintptr_t)arena_alloc(&arena, 64, 16);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto alloc_time = std::chrono::duration<double, std::milli>(end - start).count();

    // Arena 리셋 (한 번에 "해제")
    start = std::chrono::high_resolution_clock::now();
    arena_reset(&arena);
    end = std::chrono::high_resolution_clock::now();
    auto reset_time = std::chrono::duration<double, std::milli>(end - start).count();

    printf("  할당 (%d회 x 64B): %.3f ms\n", COUNT, alloc_time);
    printf("  리셋 (전체 해제): %.3f ms\n", reset_time);
    printf("  총합: %.3f ms\n", alloc_time + reset_time);
    printf("  평균: %.1f ns/alloc\n\n", (alloc_time * 1000000.0) / COUNT);

    arena_destroy(&arena);
}

void benchmark_arena_reuse() {
    printf("=== Arena 재사용 벤치마크 (10회 반복) ===\n");

    const int COUNT = 1000000;
    const int ITERATIONS = 10;

    Arena arena = arena_create(64 * COUNT);

    auto start = std::chrono::high_resolution_clock::now();

    for (int iter = 0; iter < ITERATIONS; iter++) {
        for (int i = 0; i < COUNT; i++) {
            g_sink += (uint64_t)(uintptr_t)arena_alloc(&arena, 64, 16);
        }
        arena_reset(&arena);  // 프레임 끝에서 리셋
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto total_time = std::chrono::duration<double, std::milli>(end - start).count();

    printf("  총 할당: %d회\n", COUNT * ITERATIONS);
    printf("  총 시간: %.3f ms\n", total_time);
    printf("  평균: %.1f ns/alloc\n\n", (total_time * 1000000.0) / (COUNT * ITERATIONS));

    arena_destroy(&arena);
}

// ============================================================================
// 메인
// ============================================================================

int main() {
    printf("========================================\n");
    printf("  Arena Allocator 벤치마크\n");
    printf("  각 테스트: 100만 회 x 64바이트 할당\n");
    printf("========================================\n\n");

    // 워밍업
    printf("워밍업 중...\n\n");
    {
        Arena warmup = arena_create(1024 * 1024);
        for (int i = 0; i < 10000; i++) {
            g_sink += (uint64_t)(uintptr_t)arena_alloc(&warmup, 64, 16);
        }
        arena_destroy(&warmup);
    }

    // 벤치마크 실행
    benchmark_malloc();
    benchmark_arena();
    benchmark_arena_reuse();

    // 비교 요약
    printf("========================================\n");
    printf("  결론:\n");
    printf("  - Arena는 malloc보다 수십 배 빠름\n");
    printf("  - 해제가 O(1) (리셋 한 번)\n");
    printf("  - 게임 프레임 단위 할당에 이상적\n");
    printf("========================================\n");

    printf("\n아무 키나 누르면 종료...\n");
    getchar();

    return 0;
}
