## 들어가며

일반 목적 할당자인 `malloc`과 `free`는 범용성을 위해 설계되었다. 어떤 크기든, 어떤 순서로든 할당과 해제가 가능하다. 그 범용성의 대가는 락 경합, bin 탐색, 메타데이터 관리, 인접 청크 병합이다. 게임 엔진이 매 프레임 수천 개의 파티클을 생성하고 삭제하거나, 실시간 시스템이 밀리초 단위의 지터도 허용하지 않을 때 이 오버헤드는 무시할 수 없는 병목이다.

이 글은 도메인 특화 할당자의 다섯 가지 핵심 패턴(Arena, Pool, Stack, Buddy, Slab)을 구현 수준에서 다루고, C++17 `std::pmr`로 표준 컨테이너에 통합하는 방법, allocator-aware 컨테이너의 동작 원리, 하이브리드 메모리 풀, 실측 벤치마크, 게임 엔진 적용 사례, 디버깅 도구까지 한 흐름으로 본다. 각 패턴은 특정 사용 패턴을 가정하고, 그 제약을 자원 삼아 극적인 성능 향상을 만든다.


---

## 목차
1. [Arena Allocator](#1-arena-allocator-bump-allocation)
2. [Pool Allocator](#2-pool-allocator-고정-크기-객체의-재사용)
3. [Stack Allocator](#3-stack-allocator-lifo-제약의-활용)
4. [Buddy Allocator](#4-buddy-allocator-2의-거듭제곱-분할)
5. [Slab Allocator](#5-slab-allocator-객체-캐싱)
6. [C++17 std::pmr 통합](#6-c17-stdpmr-통합)
7. [Allocator-aware 컨테이너](#7-allocator-aware-컨테이너)
8. [하이브리드 패턴: Arena+Slab+Free List 결합](#8-하이브리드-패턴-arenaslabfree-list-결합)
9. [성능 벤치마크](#9-성능-벤치마크)
10. [게임 엔진 적용 사례](#10-게임-엔진-적용-사례)
11. [디버깅 도구](#11-디버깅-도구)
12. [할당자 선택 가이드](#12-할당자-선택-가이드)


---

## 1. Arena Allocator: bump allocation

### 왜 malloc은 느린가

`malloc`이 내부적으로 하는 일을 단순화하면 다음과 같다.

```c
/* glibc malloc의 단순화된 흐름 */
void* malloc(size_t size) {
    /* 1. Arena 선택: 멀티스레드 환경에서 경합 최소화 */
    mstate ar_ptr = arena_get();

    /* 2. Arena 뮤텍스 획득: 커널 컨텍스트 스위치 가능 */
    __libc_lock_lock(ar_ptr->mutex);

    /* 3. 적합한 bin 탐색 (fastbin / smallbin / largebin / unsorted) */
    void* ptr = _int_malloc(ar_ptr, size);

    /* 4. 뮤텍스 해제 */
    __libc_lock_unlock(ar_ptr->mutex);

    return ptr;
}
```

이 과정에서 발생하는 비용은 다음과 같다.

- Mutex 락: 경합이 길어져 스레드가 잠들면 스케줄러 경로까지 확장
- Bin 탐색: 분기 예측 실패와 캐시 미스
- 메타데이터 관리: 청크 헤더와 free chunk의 boundary tag 등 갱신
- 병합(coalescing): 인접 free 청크 병합 오버헤드

`malloc` 비용은 로컬 캐시 hit와 OS 매핑·page fault 경로 사이에서 큰 분포를 가진다. 호출 횟수만으로 프레임 비용을 계산하지 말고 대상 할당자의 크기별 지연, refill 횟수와 p99를 측정해야 한다.

### 동작 원리: bump pointer

Arena allocator(또는 linear allocator)는 가장 단순하면서도 강력한 전략이다. 핵심은 **포인터를 증가시키기만 하는 것**이다.

```c
typedef struct Arena {
    uint8_t*  base;       /* 메모리 블록 시작 주소 */
    size_t    size;       /* 전체 크기 */
    size_t    offset;     /* 현재 사용 오프셋 (bump pointer) */
} Arena;

void* arena_alloc_aligned(Arena* a, size_t size, size_t align);

void* arena_alloc(Arena* a, size_t alloc_size, size_t align) {
    return arena_alloc_aligned(a, alloc_size, align);
}
```

**성능 특성:**
- 할당: $O(1)$ - 정렬 padding 계산과 범위 검사, offset 갱신
- 해제: 개별 해제 불가능
- 전체 리셋: $O(1)$ - 오프셋을 0으로 설정

이것이 얼마나 빠른지 측정해보겠다.

```c
/* 벤치마크: 백만 개 할당 */
#include <time.h>

void benchmark_malloc() {
    clock_t start = clock();
    void* ptrs[1000000];

    for (int i = 0; i < 1000000; i++) {
        ptrs[i] = malloc(64);
    }

    clock_t end = clock();
    printf("malloc: %.3f ms\n",
           (double)(end - start) * 1000 / CLOCKS_PER_SEC);

    for (int i = 0; i < 1000000; i++) {
        free(ptrs[i]);
    }
}

void benchmark_arena() {
    Arena* arena = arena_create(64 * 1000000);
    clock_t start = clock();

    for (int i = 0; i < 1000000; i++) {
        arena_alloc_aligned(arena, 64, 16);
    }

    clock_t end = clock();
    printf("arena: %.3f ms\n",
           (double)(end - start) * 1000 / CLOCKS_PER_SEC);

    arena_destroy(arena);
}
```

이 코드는 측정 골격일 뿐 결과표가 아니다. `clock()`은 프로세스 CPU 시간을 측정하고 컴파일러가 사용되지 않는 결과를 제거할 수도 있으므로, 실제 비교에서는 반환 포인터를 관찰 가능한 방식으로 사용하고 warm-up·반복·분위수를 기록해야 한다. Arena의 fast path는 정렬 계산, 범위 검사와 offset 갱신으로 짧지만 arena 자체의 메타데이터와 backing storage 확보 비용은 존재한다.

### 완전한 구현

아래는 고정 backing block을 사용하는 단일 스레드 구현이다. 정렬 값 검증, 절대 주소 기준 padding, 산술 overflow와 수명 계약을 포함한다.

```c
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define DEFAULT_ALIGNMENT 16

static bool is_power_of_two_size(size_t x) {
    return x != 0 && (x & (x - 1)) == 0;
}

/* Arena 생성 */
Arena* arena_create(size_t capacity) {
    Arena* a = malloc(sizeof(Arena));
    if (!a) return NULL;

    a->base = malloc(capacity);
    if (!a->base) {
        free(a);
        return NULL;
    }

    a->size = capacity;
    a->offset = 0;
    return a;
}

/* Arena 해제 */
void arena_destroy(Arena* a) {
    if (!a) return;
    free(a->base);
    free(a);
}

/* 전체 리셋: 모든 할당을 한 번에 해제 */
void arena_reset(Arena* a) {
    /*
     * reset은 C++ destructor나 외부 자원 정리를 대신하지 않는다.
     * 살아 있는 비-trivial 객체가 없다는 것이 호출 전제다.
     */
    /* 디버그 모드: 메모리 초기화로 use-after-free 감지
       (offset을 0으로 만들기 전에 채워야 한다) */
    #ifdef DEBUG
    memset(a->base, 0xCD, a->offset);
    #endif

    a->offset = 0;
}

/* 정렬된 할당 */
void* arena_alloc_aligned(Arena* a, size_t size, size_t align) {
    if (a == NULL || !is_power_of_two_size(align))
        return NULL;
    if (a->offset > a->size)
        return NULL;

    uintptr_t current = (uintptr_t)(a->base + a->offset);
    size_t padding = (align - (current & (align - 1))) & (align - 1);
    size_t remaining = a->size - a->offset;

    /* padding + size를 먼저 계산하지 않아 size_t wrap을 피한다. */
    if (padding > remaining || size > remaining - padding) {
        return NULL;
    }

    size_t aligned_offset = a->offset + padding;
    void* ptr = a->base + aligned_offset;
    a->offset = aligned_offset + size;

    return ptr;
}

/* 편의 함수: 기본 정렬 */
#define arena_alloc(a, size) arena_alloc_aligned(a, size, DEFAULT_ALIGNMENT)
```

### Temporary Arena Memory Pattern

Arena의 진정한 힘은 **중첩된 스코프 관리**에서 나타난다. Savepoint 패턴을 사용하면 부분적인 해제가 가능하다.

```c
typedef struct Temp_Arena_Memory {
    Arena*  arena;
    size_t  saved_offset;
} Temp_Arena_Memory;

Temp_Arena_Memory temp_arena_begin(Arena* a) {
    Temp_Arena_Memory temp;
    temp.arena = a;
    temp.saved_offset = a->offset;
    return temp;
}

void temp_arena_end(Temp_Arena_Memory temp) {
    temp.arena->offset = temp.saved_offset;
}
```

Savepoint는 같은 arena에서 **엄격한 LIFO 순서**로 끝나야 한다. Savepoint 뒤의 allocation을 다른 스레드나 더 오래 사는 객체가 보관하고 있으면 offset 복원이 살아 있는 storage를 재사용한다. Arena 자체도 동기화되지 않았으므로 한 스레드가 소유하거나 호출자가 잠금·thread-local 분리를 제공해야 한다.

이 패턴의 강력함을 실제 예제로 확인해보자.

```c
/* 프레임 단위 할당: 게임 엔진의 전형적인 패턴 */
void game_frame_update(Arena* frame_arena, Arena* permanent_arena) {
    /* 프레임 시작: 이전 프레임 데이터 전체 삭제 */
    arena_reset(frame_arena);

    /* 임시 계산용 메모리 */
    Temp_Arena_Memory temp = temp_arena_begin(frame_arena);

    /* View-Projection 행렬 계산 */
    float* view_matrix = arena_alloc(frame_arena, 16 * sizeof(float));
    float* proj_matrix = arena_alloc(frame_arena, 16 * sizeof(float));
    calculate_matrices(view_matrix, proj_matrix);

    /* 컬링용 frustum */
    Frustum* frustum = arena_alloc(frame_arena, sizeof(Frustum));
    build_frustum(view_matrix, proj_matrix, frustum);

    /* 보이는 객체만 렌더링 리스트에 추가 */
    RenderCommand* commands =
        arena_alloc(permanent_arena, MAX_OBJECTS * sizeof(RenderCommand));
    int command_count = cull_and_build_commands(frustum, commands);

    /* 계산 완료: frustum, 행렬 메모리 즉시 해제 */
    temp_arena_end(temp);

    /* 렌더링 커맨드는 permanent에 있으므로 살아있음 */
    submit_render_commands(commands, command_count);
}
```

이 패턴을 파싱에 적용하면 더욱 명확해진다.

```c
/* 컴파일러 프론트엔드 예제 */
AST* parse_file(const char* filename, Arena* arena) {
    Temp_Arena_Memory temp = temp_arena_begin(arena);

    /* 토큰 버퍼: 파싱 중에만 필요 */
    Token* tokens = arena_alloc(arena, MAX_TOKENS * sizeof(Token));
    int token_count = tokenize(filename, tokens);

    /* AST 노드들: 파싱 후에도 유지되어야 함 */
    AST* ast = parse_tokens(tokens, token_count, arena);

    /* 토큰 버퍼만 해제, AST는 유지 */
    temp_arena_end(temp);

    return ast;
}
```

**핵심 통찰**: `temp_arena_end`는 offset을 복원한다. temp 이전에 할당된 AST는 영향을 받지 않고, temp 이후에 할당된 tokens만 해제된다.

### 언제 Arena를 사용할까?

| 시나리오 | 왜 적합한가 |
|---------|-----------|
| **프레임 데이터** | 60fps에서 매 16ms마다 생성/삭제 |
| **파싱** | 토큰, AST 노드 등 단계별 수명 |
| **레벨 로딩** | 레벨 전환 시 전체 데이터 폐기 |
| **문자열 빌더** | JSON 생성, 로그 메시지 조합 |
| **임시 계산** | 행렬 연산, 중간 버퍼 |

**장점:**
- fast path가 상수 시간이며 범위 검사와 bump만 수행
- 순차적인 배치로 locality를 얻기 쉬움
- 개별 free hole이 생기지 않아 외부 단편화가 없음
- arena 수명 단위로 raw storage 회수가 단순함

**단점:**
- 개별 해제 불가능 (전체 또는 savepoint 단위)
- 정렬 padding과 마지막 미사용 영역의 내부 낭비
- reset이 C++ 소멸자나 파일·소켓·GPU handle 정리를 대신하지 않음
- 최대 사용량 기준으로 메모리 예약 필요

## 2. Pool Allocator: 고정 크기 객체의 재사용

Pool allocator는 **동일한 크기의 객체**를 빠르게 할당하고 해제한다. 게임 엔진의 파티클 시스템, ECS 아키텍처의 컴포넌트, 네트워크 패킷 버퍼 등에 이상적이다.

### Free List 기반 설계

Pool의 핵심은 **인플레이스 free list**이다. Free 블록은 사용되지 않으므로, 블록 자체를 linked list 노드로 활용한다.

```c
typedef struct Pool {
    uint8_t*  memory;        /* 전체 메모리 블록 */
    void*     free_list;     /* 다음 free 블록 포인터 */
    size_t    chunk_size;    /* 각 블록 크기 */
    size_t    chunk_count;   /* 전체 블록 수 */
    size_t    alignment;
} Pool;

Pool* pool_create(size_t object_size, size_t alignment, size_t chunk_count) {
    if (object_size == 0 || chunk_count == 0 ||
        !is_power_of_two_size(alignment))
        return NULL;

    if (alignment < _Alignof(void*))
        alignment = _Alignof(void*);

    /* 최소 크기 보장: 포인터를 저장할 공간 필요 */
    size_t chunk_size = object_size < sizeof(void*) ? sizeof(void*) : object_size;

    if (chunk_size > SIZE_MAX - (alignment - 1))
        return NULL;
    chunk_size = (chunk_size + alignment - 1) & ~(alignment - 1);
    if (chunk_size > SIZE_MAX / chunk_count)
        return NULL;

    Pool* pool = calloc(1, sizeof(*pool));
    if (pool == NULL)
        return NULL;

    size_t total = chunk_size * chunk_count;
    pool->memory = aligned_alloc(alignment, total);
    if (pool->memory == NULL) {
        free(pool);
        return NULL;
    }

    pool->chunk_size = chunk_size;
    pool->chunk_count = chunk_count;
    pool->alignment = alignment;

    /* Free list 초기화: 각 블록을 linked list로 연결 */
    pool->free_list = NULL;
    for (size_t i = chunk_count; i-- > 0;) {
        void* chunk = pool->memory + i * chunk_size;
        *(void**)chunk = pool->free_list;
        pool->free_list = chunk;
    }

    return pool;
}
```

메모리 레이아웃을 시각화하면:

**초기 상태 (모두 free):**

| free_list → | Block 0 → | Block 1 → | Block 2 |
|:-----------:|:---------:|:---------:|:-------:|
| ↓ | `[ptr]` | `[ptr]` | `[NULL]` |

**Block 0 할당 후:** `free_list` → Block 1 → Block 2 → NULL

**Block 1 할당 후:** `free_list` → Block 2 → NULL

**Block 0 해제 후 (LIFO):** `free_list` → Block 0 → Block 2 → NULL

### 할당과 해제

```c
void* pool_alloc(Pool* pool) {
    if (pool->free_list == NULL) {
        return NULL;  /* Pool 고갈 */
    }

    /* Free list 헤드 꺼내기 */
    void* ptr = pool->free_list;
    pool->free_list = *(void**)ptr;

    return ptr;
}

void pool_free(Pool* pool, void* ptr) {
    if (ptr == NULL) return;

    /*
     * 전제: ptr은 이 pool이 반환한 chunk 시작 주소이고 아직 free가 아니다.
     * C++ 객체라면 destructor가 이미 실행되어 lifetime이 끝나 있어야 한다.
     */
    /* Free list 헤드에 추가 (LIFO) */
    *(void**)ptr = pool->free_list;
    pool->free_list = ptr;
}

void pool_destroy(Pool* pool) {
    if (!pool) return;
    free(pool->memory);
    free(pool);
}
```

fast path는 빈 목록 검사와 몇 번의 포인터 접근으로 상수 시간에 끝나며 자체 lock은 없다. 따라서 같은 Pool을 여러 스레드가 동시에 호출하면 data race가 발생한다. Release 구현은 잘못된 pool의 pointer와 double free를 검사하지 않으므로 외부 소유권 계약이 필요하고, debug build에서는 slot bitmap과 generation을 두어 검증한다. `aligned_alloc`을 지원하지 않는 플랫폼에서는 같은 alignment·size 계약을 가진 platform helper와 대응되는 free 함수를 사용한다.

### 실전 예제: 파티클 시스템

```c
typedef struct Particle {
    float position[3];
    float velocity[3];
    float color[4];
    float lifetime;
    float size;
} Particle;

/* 파티클 풀 생성: 최대 10,000개 */
Pool* particle_pool =
    pool_create(sizeof(Particle), _Alignof(Particle), 10000);

/* 파티클 발생 */
void emit_particle(float x, float y, float z) {
    Particle* p = pool_alloc(particle_pool);
    if (!p) {
        /* Pool 고갈: 가장 오래된 파티클 재사용 등의 전략 */
        return;
    }

    /* Free 상태에서는 첫 바이트가 next pointer였으므로 모든 필드를 초기화한다. */
    memset(p, 0, sizeof(*p));
    p->position[0] = x;
    p->position[1] = y;
    p->position[2] = z;
    p->lifetime = 5.0f;
    p->size = 1.0f;
}

/* 파티클 업데이트 */
void update_particles(Particle** active_particles, int* count, float dt) {
    for (int i = 0; i < *count; i++) {
        Particle* p = active_particles[i];
        p->lifetime -= dt;

        if (p->lifetime <= 0.0f) {
            /* 수명 종료: Pool에 반환 */
            pool_free(particle_pool, p);

            /* 배열에서 제거 (swap-and-pop) */
            active_particles[i] = active_particles[*count - 1];
            (*count)--;
            i--;
        } else {
            /* 위치 업데이트 */
            p->position[0] += p->velocity[0] * dt;
            p->position[1] += p->velocity[1] * dt;
            p->position[2] += p->velocity[2] * dt;
        }
    }
}
```

### 연속 backing storage와 dense iteration은 다르다

Pool의 slot은 하나의 bounded memory region에 모여 있어 개별 `malloc`보다 주소 범위와 page 수를 통제하기 쉽다. 그러나 free-list 순서로 checkout한 살아 있는 객체가 slot 번호순으로 조밀하다는 보장은 없다.

```c
/* malloc 버전: 포인터가 힙 전체에 흩어짐 */
Particle** particles_malloc = malloc(10000 * sizeof(Particle*));
for (int i = 0; i < 10000; i++) {
    particles_malloc[i] = malloc(sizeof(Particle));  /* 임의 위치 */
}

/* 업데이트: 캐시 미스 빈번 */
for (int i = 0; i < 10000; i++) {
    update_particle(particles_malloc[i]);  /* 각 접근마다 캐시 미스 가능 */
}

/* Free-list Pool: backing region은 연속이지만 live 순회는 포인터 목록 */
for (int i = 0; i < active_count; ++i) {
    update_particle(active_particles[i]);
}

/* Dense storage: live 객체를 [0, live_count)에 유지하고 swap-remove */
for (size_t i = 0; i < live_count; ++i) {
    update_particle(&dense_particles[i]);
}
```

Free slot까지 포함한 `pool->memory`를 `Particle[]`처럼 순회하면 죽은 객체를 업데이트하고, `chunk_size != sizeof(Particle)`일 때 stride도 틀린다. 매 프레임 모든 live 객체를 순회하는 시스템은 dense vector, sparse set, archetype chunk처럼 live range를 조밀하게 유지하는 자료구조가 더 직접적이다. Pool은 주소 안정성·고정 용량·빠른 개별 재사용을 소유하고, dense container는 순회 locality를 소유한다.

비교할 때는 할당 방식과 업데이트 레이아웃을 분리한다. 개별 할당 객체의 포인터 배열과 dense 배열을 비교하면 locality뿐 아니라 간접 참조와 삭제 정책도 함께 달라진다. `perf stat`의 cache miss와 cycles를 기록하되 "pool이라서 몇 배"라는 상수보다 같은 live-set과 같은 iteration order에서 배치만 바꾸어 측정한다.

### 언제 Pool을 사용할까?

| 시나리오 | 왜 적합한가 |
|---------|-----------|
| **파티클 시스템** | 수천 개의 동일 구조체 생성/소멸 |
| **ECS 컴포넌트** | TransformComponent, RenderComponent 등 |
| **네트워크 패킷** | 송수신 버퍼 재사용 |
| **이벤트 큐** | Event 객체 할당 |
| **물리 시뮬레이션** | RigidBody, Collider 객체 |

**장점:**
- $O(1)$ 할당/해제
- Pool 내부에 가변 크기 hole이 생기지 않아 외부 단편화가 없음
- backing address 범위와 page 수를 통제하기 쉬움
- 메모리 버짓 명확 (chunk_size × chunk_count)

**단점:**
- 크기 고정
- 정렬 padding과 비어 있는 slot의 내부 낭비
- free-list Pool만으로 live 객체의 dense iteration을 보장하지 않음
- Pool 고갈 시 할당 실패

## 3. Stack Allocator: LIFO 제약의 활용

Stack allocator는 **LIFO(Last-In-First-Out) 순서로만 해제**할 수 있다. 이 제약이 강해 보이지만, 많은 알고리즘이 자연스럽게 LIFO 패턴을 따른다.

### 마커 기반 롤백

가장 실용적인 Stack allocator는 **마커(savepoint)**를 사용한다.

```c
typedef struct Stack_Allocator {
    uint8_t*  base;
    size_t    size;
    size_t    offset;
    size_t    top_header;    /* 최근 header offset, 없으면 STACK_NONE */
} Stack_Allocator;

#define STACK_NONE SIZE_MAX

typedef struct Stack_Marker {
    size_t offset;
    size_t top_header;
} Stack_Marker;

/* 마커 저장 */
Stack_Marker stack_get_marker(Stack_Allocator* sa) {
    return (Stack_Marker){
        .offset = sa->offset,
        .top_header = sa->top_header
    };
}

/* 마커로 롤백 */
bool stack_free_to_marker(Stack_Allocator* sa, Stack_Marker marker) {
    if (sa == NULL || marker.offset > sa->offset)
        return false;

    /* marker 뒤에 생성한 비-trivial 객체와 외부 자원은 이미 정리되어야 한다. */
    sa->offset = marker.offset;
    sa->top_header = marker.top_header;
    return true;
}

/* 할당 */
void* stack_alloc(Stack_Allocator* sa, size_t size, size_t align) {
    if (sa == NULL || !is_power_of_two_size(align) ||
        sa->offset > sa->size)
        return NULL;

    uintptr_t current = (uintptr_t)(sa->base + sa->offset);
    size_t padding = (align - (current & (align - 1))) & (align - 1);
    size_t remaining = sa->size - sa->offset;
    if (padding > remaining || size > remaining - padding)
        return NULL;

    size_t aligned_offset = sa->offset + padding;
    void* ptr = sa->base + aligned_offset;
    sa->offset = aligned_offset + size;

    return ptr;
}
```

### 실전 예제: A* 경로 탐색

```c
/* A* 알고리즘의 임시 메모리 할당 */
static bool checked_mul_size(size_t a, size_t b, size_t* out) {
    if (a != 0 && b > SIZE_MAX / a)
        return false;
    *out = a * b;
    return true;
}

Path* find_path(Grid* grid, Point start, Point goal,
                Stack_Allocator* stack) {
    if (grid == NULL || stack == NULL ||
        grid->width <= 0 || grid->height <= 0)
        return NULL;

    size_t cell_count;
    size_t node_bytes;
    size_t closed_bytes;
    if (!checked_mul_size((size_t)grid->width,
                          (size_t)grid->height, &cell_count) ||
        !checked_mul_size(cell_count, sizeof(AStarNode), &node_bytes) ||
        !checked_mul_size(cell_count, sizeof(bool), &closed_bytes))
        return NULL;

    Stack_Marker marker = stack_get_marker(stack);

    /* Open/Closed set: 탐색 중에만 필요 */
    AStarNode* open_set = stack_alloc(stack, node_bytes, 16);
    bool* closed_set = stack_alloc(stack, closed_bytes, 16);
    if (open_set == NULL || closed_set == NULL) {
        stack_free_to_marker(stack, marker);
        return NULL;
    }

    /* A* 알고리즘 실행 */
    int path_length = astar_search(grid, start, goal,
                                   open_set, closed_set);
    if (path_length < 0) {
        stack_free_to_marker(stack, marker);
        return NULL;
    }

    /* 경로 결과: 영구 메모리에 복사 */
    size_t point_bytes;
    if (!checked_mul_size((size_t)path_length,
                          sizeof(Point), &point_bytes) ||
        point_bytes > SIZE_MAX - sizeof(Path)) {
        stack_free_to_marker(stack, marker);
        return NULL;
    }

    Path* result = malloc(sizeof(Path) + point_bytes);
    if (result != NULL) {
        result->length = path_length;
        reconstruct_path(result->points, path_length, open_set);
    }

    /* 임시 메모리 전체 해제 */
    stack_free_to_marker(stack, marker);

    return result;
}
```

### 헤더 기반 LIFO 검증

순서 위반을 감지하려면 각 할당마다 헤더를 추가한다.

```c
typedef struct Alloc_Header {
    size_t previous_offset;
    size_t previous_header;
    size_t allocation_end;
} Alloc_Header;

void* stack_alloc_with_header(Stack_Allocator* sa, size_t size, size_t align) {
    if (sa == NULL || !is_power_of_two_size(align) ||
        sa->offset > sa->size)
        return NULL;

    size_t effective_align =
        align < _Alignof(Alloc_Header) ? _Alignof(Alloc_Header) : align;
    size_t remaining = sa->size - sa->offset;
    if (sizeof(Alloc_Header) > remaining)
        return NULL;

    uintptr_t after_header =
        (uintptr_t)(sa->base + sa->offset + sizeof(Alloc_Header));
    size_t padding =
        (effective_align - (after_header & (effective_align - 1))) &
        (effective_align - 1);
    remaining -= sizeof(Alloc_Header);
    if (padding > remaining || size > remaining - padding)
        return NULL;

    size_t payload_offset =
        sa->offset + sizeof(Alloc_Header) + padding;
    size_t header_offset = payload_offset - sizeof(Alloc_Header);
    size_t allocation_end = payload_offset + size;

    /* Header는 payload 바로 앞에 있고 자체 alignment도 만족한다. */
    Alloc_Header* header =
        (Alloc_Header*)(sa->base + header_offset);
    header->previous_offset = sa->offset;
    header->previous_header = sa->top_header;
    header->allocation_end = allocation_end;

    sa->top_header = header_offset;
    sa->offset = allocation_end;

    return sa->base + payload_offset;
}

bool stack_free_with_check(Stack_Allocator* sa, void* ptr) {
    if (sa == NULL || ptr == NULL || sa->top_header == STACK_NONE)
        return false;

    Alloc_Header* header =
        (Alloc_Header*)(sa->base + sa->top_header);
    void* expected_ptr = (uint8_t*)header + sizeof(*header);

    if (ptr != expected_ptr || sa->offset != header->allocation_end)
        return false;  /* 최근 allocation이 아니거나 allocator 상태 손상 */

    sa->offset = header->previous_offset;
    sa->top_header = header->previous_header;
    return true;
}
```

초기화할 때 `offset = 0`, `top_header = STACK_NONE`으로 둔다. Marker와 개별 free를 섞더라도 모두 LIFO여야 하며, 오래된 marker를 reset 뒤 재사용하지 않는다. 이 구현도 단일 스레드 소유를 전제로 한다.

### 언제 Stack을 사용할까?

- **함수 스코프 할당**: 함수 진입 시 마커, 종료 시 롤백
- **재귀 알고리즘**: DFS, 백트래킹 등
- **레벨 로딩**: 섹션별 할당/해제
- **AI 경로 탐색**: Open/Closed set, 임시 계산

## 4. Buddy Allocator: 2의 거듭제곱 분할

Buddy allocator는 **외부 단편화를 줄이면서 다양한 크기를 지원**한다. Linux 커널의 물리 페이지 할당자가 대표적인 예이다.

### 분할과 병합 메커니즘

메모리를 2의 거듭제곱 크기로 재귀적으로 분할한다.

```text
초기: 1MB 블록
[                    1MB                    ]

64KB 할당 요청:
1MB -> 512KB + 512KB
512KB -> 256KB + 256KB
256KB -> 128KB + 128KB
128KB -> 64KB(할당) + 64KB(free)

결과:
[64KB][64KB][128KB][256KB][512KB]
 사용  free   free   free   free
```

### 구조체 정의

```c
#define MIN_ORDER 4   /* 최소 16B: free-list pointer와 기본 정렬 수용 */
#define MAX_ORDER 20  /* root block은 2^20 = 1MiB */

_Static_assert((1ULL << MIN_ORDER) >= sizeof(void*),
               "minimum buddy block must hold a free-list pointer");

typedef struct Buddy_Allocator {
    uint8_t* base;
    size_t   size;
    void*    free_lists[MAX_ORDER + 1];  /* 각 order별 free list */
} Buddy_Allocator;

bool buddy_init(Buddy_Allocator* ba) {
    if (ba == NULL)
        return false;

    memset(ba, 0, sizeof(*ba));
    ba->size = 1ULL << MAX_ORDER;
    /*
     * Root를 root size에 맞추면 모든 하위 buddy도 자기 block size에
     * 자연 정렬된다. Extended alignment를 지원하는 platform helper 필요.
     */
    ba->base = aligned_alloc(ba->size, ba->size);
    if (ba->base == NULL)
        return false;

    /* 처음에는 root block 하나만 free다. */
    *(void**)ba->base = NULL;
    ba->free_lists[MAX_ORDER] = ba->base;
    return true;
}

void buddy_destroy(Buddy_Allocator* ba) {
    if (ba == NULL)
        return;
    free(ba->base);
    memset(ba, 0, sizeof(*ba));
}

/* 크기를 order로 변환 */
static int size_to_order(size_t size) {
    if (size == 0)
        return -1;

    int order = MIN_ORDER;
    size_t block_size = 1ULL << MIN_ORDER;
    while (block_size < size && order < MAX_ORDER) {
        block_size <<= 1;
        ++order;
    }

    return block_size >= size ? order : -1;
}

/* Buddy 주소 계산: XOR 트릭 */
static void* get_buddy(Buddy_Allocator* ba, void* block, int order) {
    size_t offset = (uint8_t*)block - ba->base;
    size_t buddy_offset = offset ^ (1ULL << order);
    return buddy_offset < ba->size ? ba->base + buddy_offset : NULL;
}
```

**핵심 통찰**: Buddy 주소는 XOR로 계산된다. 블록 A와 그 buddy B는 order 비트만 다르다.

```text
Example: order=3 (8바이트 블록)
Block A offset: 0b00010000 (16)
Buddy B offset: 0b00011000 (24)  <- 3번째 비트만 flip
XOR: 16 ^ 8 = 24
```

### 할당 알고리즘

```c
void* buddy_alloc(Buddy_Allocator* ba, size_t size) {
    int order = size_to_order(size);
    if (ba == NULL || order < 0)
        return NULL;

    /* 적합한 블록 찾기 */
    int current_order = order;
    while (current_order <= MAX_ORDER && ba->free_lists[current_order] == NULL) {
        current_order++;
    }

    if (current_order > MAX_ORDER) {
        return NULL;  /* Out of memory */
    }

    /* 블록 꺼내기 */
    void* block = ba->free_lists[current_order];
    ba->free_lists[current_order] = *(void**)block;

    /* 분할 (splitting) */
    while (current_order > order) {
        current_order--;
        size_t buddy_size = 1ULL << current_order;
        void* buddy = (uint8_t*)block + buddy_size;

        /* Buddy를 free list에 추가 */
        *(void**)buddy = ba->free_lists[current_order];
        ba->free_lists[current_order] = buddy;
    }

    return block;
}
```

### 병합(Coalescing) 메커니즘

```c
bool buddy_free(Buddy_Allocator* ba, void* ptr, size_t size) {
    if (ba == NULL || ptr == NULL || ba->base == NULL)
        return false;

    int order = size_to_order(size);
    if (order < 0)
        return false;

    uintptr_t base_addr = (uintptr_t)ba->base;
    uintptr_t ptr_addr = (uintptr_t)ptr;
    if (ptr_addr < base_addr || ptr_addr - base_addr >= ba->size)
        return false;

    size_t offset = (size_t)(ptr_addr - base_addr);
    size_t block_size = 1ULL << order;
    if (offset % block_size != 0)
        return false;  /* block 시작이 아니거나 size/order가 맞지 않음 */

    /* Buddy 병합 시도 */
    while (order < MAX_ORDER) {
        void* buddy = get_buddy(ba, ptr, order);
        if (buddy == NULL)
            return false;

        /* Free list에서 buddy 찾기 */
        void** prev = &ba->free_lists[order];
        void* current = *prev;
        bool found = false;

        while (current != NULL) {
            if (current == buddy) {
                /* Buddy 제거 */
                *prev = *(void**)current;
                found = true;
                break;
            }
            prev = (void**)current;
            current = *(void**)current;
        }

        if (!found) break;  /* Buddy가 사용 중 */

        /* 병합: 낮은 주소를 유지 */
        if ((uintptr_t)buddy < (uintptr_t)ptr)
            ptr = buddy;
        order++;
    }

    /* Free list에 추가 */
    *(void**)ptr = ba->free_lists[order];
    ba->free_lists[order] = ptr;
    return true;
}
```

이 구현은 분할·XOR buddy·병합이라는 핵심만 보이는 **단일 스레드 교육용 구현**이다. `buddy_destroy` 전에 모든 allocation의 수명이 끝나 있어야 한다. `buddy_free`의 `size`는 allocation 때 반올림된 order와 정확히 같아야 하며, 잘못된 크기나 double free가 우연히 정렬 검사를 통과할 수 있다. production 구현은 allocation order와 상태를 out-of-band bitmap·page descriptor·handle metadata에 기록해 free 시 대조하고, free-list 변조 방어와 동기화를 추가한다. Free buddy를 연결 리스트에서 선형 탐색하므로 병합은 목록 길이에 비례할 수 있다. order별 bitmap이나 주소 색인을 사용하면 buddy의 free 여부를 더 직접 확인할 수 있다.

### Linux 커널의 Buddy System

Linux는 물리 페이지 할당에 Buddy system을 사용한다.

```c
/* include/linux/mmzone.h */
struct free_area {
    struct list_head  free_list[MIGRATE_TYPES];
    unsigned long     nr_free;
};

struct zone {
    struct free_area  free_area[MAX_ORDER];
    /* ... */
};

/* mm/page_alloc.c */
struct page *__alloc_pages(gfp_t gfp_mask, unsigned int order,
                          int preferred_nid, nodemask_t *nodemask);
```

**주요 특징:**
- Order 0~10 (4KB ~ 4MB)
- MIGRATE_TYPES: 단편화 방지 전략
 - UNMOVABLE: 커널 데이터
 - MOVABLE: 사용자 페이지
 - RECLAIMABLE: 페이지 캐시
- Compaction: 물리 메모리 조각 모음

Buddy allocator는 **외부 단편화**를 제한된 order의 병합 가능 block으로 바꾸는 대신 내부 단편화를 감수한다. 요청을 바로 위 2의 거듭제곱으로 올리는 단순 buddy에서 요청 크기가 block 경계를 조금 넘으면 내부 낭비율은 할당 block의 50%에 가까워질 수 있다. 평균 낭비율은 요청 크기 분포와 최소 order에 따라 달라지므로 Wilson 등의 allocator survey가 하나의 보편적인 25~30% 평균을 보장하는 것으로 읽으면 안 된다.

## 5. Slab Allocator: 객체 캐싱

Slab allocator는 같은 크기와 정렬의 슬롯을 여러 개 묶고, 비어 있는 슬롯을 재사용한다. 기본적으로 절약하는 비용은 범용 allocator의 크기 분류, 메타데이터 탐색, 중앙 free list 접근과 backing allocation이다. **C++ 생성자와 소멸자 생략은 slab의 자동 보장이 아니다.**

### 객체 캐싱 원리

raw storage pool의 수명은 다음과 같다.

```text
slot 획득 -> 객체 생성/초기화 -> 사용 -> 객체 소멸/정리 -> slot 반환
```

생성 비용까지 줄이는 object cache는 별도의 수명 정책이다.

```text
[초기화]
모든 cache entry를 구성

[런타임]
checkout -> 사용 상태로 reset -> 사용 -> 외부 자원 해제와 idle 상태로 reset -> return

[종료]
남아 있는 모든 entry를 소멸
```

두 모델을 섞으면 안 된다. free slot의 첫 바이트를 next pointer로 덮는 intrusive free list는 그 슬롯에 살아 있는 C++ 객체가 없을 때만 안전하다. 반대로 free 상태에서도 객체를 constructed 상태로 유지하려면 free-list metadata를 객체 밖에 두고, checkout/return 때 모든 가변 상태와 외부 자원을 명시적으로 reset해야 한다.

### Raw slot slab 구현

```c
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define SLAB_NONE SIZE_MAX

typedef struct Slab {
    unsigned char *storage;   /* alignment에 맞춘 raw storage */
    size_t        *next;      /* 객체 바깥의 free-list metadata */
    unsigned char *in_use;    /* double free 검출용 */
    size_t         object_size;
    size_t         stride;
    size_t         alignment;
    size_t         object_count;
    size_t         free_head;
} Slab;

static bool is_power_of_two(size_t x) {
    return x != 0 && (x & (x - 1)) == 0;
}

Slab* slab_create(size_t object_size, size_t alignment, size_t count) {
    if (object_size == 0 || count == 0 || !is_power_of_two(alignment))
        return NULL;
    if (object_size > SIZE_MAX - (alignment - 1))
        return NULL;

    size_t stride = (object_size + alignment - 1) & ~(alignment - 1);
    if (stride > SIZE_MAX / count ||
        count > SIZE_MAX / sizeof(size_t))
        return NULL;

    size_t storage_bytes = stride * count;
    Slab* slab = calloc(1, sizeof(*slab));
    if (slab == NULL)
        return NULL;

    /*
     * C11 aligned_alloc은 size가 alignment의 배수여야 한다.
     * stride가 alignment의 배수이므로 storage_bytes도 조건을 만족한다.
     * 목표 플랫폼이 aligned_alloc을 지원하지 않으면
     * posix_memalign/_aligned_malloc을 감싼 platform helper를 사용한다.
     */
    slab->storage = aligned_alloc(alignment, storage_bytes);
    slab->next = malloc(sizeof(size_t) * count);
    slab->in_use = calloc(count, sizeof(unsigned char));
    if (slab->storage == NULL || slab->next == NULL || slab->in_use == NULL) {
        free(slab->storage);
        free(slab->next);
        free(slab->in_use);
        free(slab);
        return NULL;
    }

    slab->object_size = object_size;
    slab->stride = stride;
    slab->alignment = alignment;
    slab->object_count = count;
    slab->free_head = 0;

    for (size_t i = 0; i < count; ++i)
        slab->next[i] = (i + 1 < count) ? i + 1 : SLAB_NONE;

    return slab;
}

void* slab_alloc(Slab* slab) {
    if (slab == NULL || slab->free_head == SLAB_NONE)
        return NULL;

    size_t index = slab->free_head;
    slab->free_head = slab->next[index];
    slab->in_use[index] = 1;

    /* 이 시점에는 raw storage일 뿐이다. 호출자가 객체 수명을 시작한다. */
    return slab->storage + index * slab->stride;
}

bool slab_free(Slab* slab, void* ptr) {
    if (slab == NULL || ptr == NULL)
        return false;

    uintptr_t base = (uintptr_t)slab->storage;
    uintptr_t address = (uintptr_t)ptr;
    size_t storage_bytes = slab->stride * slab->object_count;

    if (address < base || address - base >= storage_bytes)
        return false;

    size_t offset = (size_t)(address - base);
    if (offset % slab->stride != 0)
        return false;

    size_t index = offset / slab->stride;
    if (!slab->in_use[index])
        return false;  /* double free */

    /*
     * 계약: C++ 객체라면 호출자가 이미 destructor를 실행했고,
     * C 객체라면 소유한 외부 자원을 정리했다.
     */
    slab->in_use[index] = 0;
    slab->next[index] = slab->free_head;
    slab->free_head = index;
    return true;
}

bool slab_destroy(Slab* slab) {
    if (slab == NULL)
        return true;

    for (size_t i = 0; i < slab->object_count; ++i) {
        if (slab->in_use[i])
            return false;  /* 살아 있는 객체의 storage를 해제하지 않는다. */
    }

    free(slab->storage);
    free(slab->next);
    free(slab->in_use);
    free(slab);
    return true;
}
```

`uintptr_t` 기반 범위 검사는 일반적인 flat-address 플랫폼의 진단 장치다. `slab_free`의 근본 계약은 여전히 “이 slab이 반환한 slot 시작 주소만 넘긴다”이다. 공격자가 임의 pointer를 전달하는 보안 경계라면 pointer 검증만으로 allocator metadata를 보호하려 하지 말고 handle과 generation을 사용한다.

C++에서는 `slab_alloc`이 반환한 raw storage에 `std::construct_at` 또는 placement `new`로 객체 수명을 시작하고, `std::destroy_at` 뒤 `slab_free`를 호출한다. 생성자가 예외를 던지면 slot을 즉시 반환해야 한다. 이 계층은 backing allocation을 재사용하지만 생성자·소멸자 의미를 생략하지 않는다.

### 실전 예제: SSL 연결 풀

```c
typedef struct Connection {
    int socket_fd;
    SSL* ssl;
    char buffer[4096];
} Connection;

/* SSL_CTX는 서버 설정 단위로 한 번 만들고 연결들이 공유한다. */
SSL_CTX* server_ctx = NULL;
Slab* conn_slab = NULL;

bool connection_open(Connection* conn, int client_fd) {
    conn->socket_fd = client_fd;
    conn->ssl = SSL_new(server_ctx);       /* 연결별 TLS 상태 */
    if (conn->ssl == NULL) return false;
    if (SSL_set_fd(conn->ssl, client_fd) != 1) {
        SSL_free(conn->ssl);
        conn->ssl = NULL;
        return false;
    }
    memset(conn->buffer, 0, sizeof(conn->buffer));
    return true;
}

void connection_close(Connection* conn) {
    if (conn->ssl != NULL) {
        SSL_free(conn->ssl);
        conn->ssl = NULL;
    }
    if (conn->socket_fd >= 0) {
        close(conn->socket_fd);
        conn->socket_fd = -1;
    }
}

/* 연결 처리 */
void handle_client(int client_fd) {
    Connection* conn = slab_alloc(conn_slab);
    if (conn == NULL) {
        close(client_fd);
        return;
    }
    if (!connection_open(conn, client_fd)) {
        close(client_fd);
        slab_free(conn_slab, conn);
        return;
    }

    /* SSL 핸드셰이크 및 데이터 처리 */
    process_connection(conn);

    /* 외부 자원 수명을 끝낸 뒤 메모리 슬롯만 반환한다. */
    connection_close(conn);
    slab_free(conn_slab, conn);
}
```

`SSL_CTX`를 연결마다 만들었다가 없애는 구현은 allocator 비교 기준이 아니라 수명 설계 버그다. 서버 설정 객체는 공유하고 각 연결의 `SSL` 상태는 API 계약에 따라 생성·초기화·정리한다. 객체 풀이 연결 객체를 재사용하려면 인증 상태, 버퍼, 오류 큐와 소켓 소유권을 완전히 reset할 수 있어야 한다. raw slab은 메모리 위치만 재사용할 뿐 C++ 생성자나 외부 라이브러리의 수명 규칙을 자동으로 만족시키지 않는다.

측정은 `SSL_CTX` 공용화가 끝난 동일한 기준에서 일반 할당과 pool의 alloc/free 지연, peak connection 수, resident memory, reset 비용을 비교한다. TLS handshake와 암호 연산이 지배적이면 allocator 차이는 전체 요청 지연에서 작을 수 있다.

### Linux kmem_cache

Linux 커널의 slab allocator는 inode, dentry, task_struct 등 커널 객체에 사용된다.

```c
/* include/linux/slab.h */

struct kmem_cache *kmem_cache_create(
    const char *name,
    unsigned int size,
    unsigned int align,
    slab_flags_t flags,
    void (*ctor)(void *)  /* 생성자 */
);

void *kmem_cache_alloc(struct kmem_cache *cachep, gfp_t flags);
void kmem_cache_free(struct kmem_cache *cachep, void *objp);
void kmem_cache_destroy(struct kmem_cache *cachep);
```

**Linux inode cache의 개념적 형태**:

```c
/* 커널 버전마다 함수 이름과 플래그는 달라질 수 있다. */
static struct kmem_cache *inode_cachep;

void __init inode_init(void) {
    inode_cachep = kmem_cache_create(
        "inode_cache",
        sizeof(struct inode),
        0,
        SLAB_RECLAIM_ACCOUNT | SLAB_PANIC | SLAB_ACCOUNT,
        init_once
    );
}

struct inode *alloc_inode(struct super_block *sb) {
    return kmem_cache_alloc(inode_cachep, GFP_KERNEL);
}
```

이 코드는 `kmem_cache_create`로 동일 크기·정렬·초기화 정책을 가진 객체 캐시를 만들고 `kmem_cache_alloc`으로 객체를 꺼낸다는 구조를 설명하기 위한 축약이다. 실제 Linux 소스의 inode 생성은 파일 시스템별 `alloc_inode` 연산과 보안·계정 초기화를 포함하므로, 특정 버전의 함수 본문을 그대로 옮긴 ABI 예제로 읽어서는 안 된다.

## 6. C++17 std::pmr 통합

게임용 컨테이너가 요구한 allocator 인스턴스, 명시적 정렬, 메모리 태그, 고정 용량 컨테이너는 EASTL의 N2271에도 정리되어 있다. 이 문서는 당시 게임 개발의 요구를 보여 주는 중요한 사례지만, 이후의 모든 표준 allocator 기능이 N2271에서 직접 유래했다고 볼 근거는 없다. `std::pmr`은 `memory_resource`를 통한 런타임 자원 선택이라는 별도의 표준화 계보를 가지며, EASTL과는 같은 문제군에 서로 다른 인터페이스로 답한다.

### Polymorphic Memory Resource

C++17은 `<memory_resource>` 헤더에서 표준 할당자 인터페이스를 제공한다. 이 인터페이스를 구현하면 표준 컨테이너와 호환된다.

```cpp
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory_resource>
#include <new>
#include <scoped_allocator>

// 표준 인터페이스의 핵심 모양을 보이는 축약본이다.
// 실제 프로그램에서 namespace std의 클래스를 재정의하지 않는다.
namespace std::pmr {
    class memory_resource {
    public:
        virtual ~memory_resource() = default;

        void* allocate(size_t bytes,
                       size_t alignment = alignof(max_align_t)) {
            return do_allocate(bytes, alignment);
        }

        void deallocate(void* p, size_t bytes,
                        size_t alignment = alignof(max_align_t)) {
            do_deallocate(p, bytes, alignment);
        }

    private:
        virtual void* do_allocate(size_t, size_t) = 0;
        virtual void do_deallocate(void*, size_t, size_t) = 0;
        virtual bool do_is_equal(const memory_resource&)
            const noexcept = 0;
    };
}
```

이 인터페이스를 구현한 객체를 컨테이너 생성자에 넘기면 된다.

```cpp
std::pmr::memory_resource* resource = get_game_resource();
std::pmr::vector<int> vec{resource};
std::pmr::string str{resource};
std::pmr::unordered_map<int, Foo> map{resource};
```

### Arena를 pmr로 구현하기

```cpp
class ArenaResource : public std::pmr::memory_resource {
    std::byte* buffer_;
    size_t capacity_;
    size_t offset_ = 0;

    void* do_allocate(size_t bytes, size_t alignment) override {
        if (offset_ > capacity_ || alignment == 0 ||
            (alignment & (alignment - 1)) != 0)
            throw std::bad_alloc();

        uintptr_t current =
            reinterpret_cast<uintptr_t>(buffer_ + offset_);
        size_t padding =
            (alignment - (current & (alignment - 1))) &
            (alignment - 1);
        size_t remaining = capacity_ - offset_;
        if (padding > remaining || bytes > remaining - padding)
            throw std::bad_alloc();

        size_t aligned_offset = offset_ + padding;
        offset_ = aligned_offset + bytes;
        return buffer_ + aligned_offset;
    }

    void do_deallocate(void*, size_t, size_t) override {
        // Arena는 개별 해제를 지원하지 않는다
    }

    bool do_is_equal(const memory_resource& o) const noexcept override {
        return this == &o;
    }

public:
    explicit ArenaResource(size_t capacity)
        : capacity_(capacity) {
        buffer_ = static_cast<std::byte*>(std::malloc(capacity));
        if (!buffer_) throw std::bad_alloc();
    }

    ArenaResource(const ArenaResource&) = delete;
    ArenaResource& operator=(const ArenaResource&) = delete;
    ArenaResource(ArenaResource&&) = delete;
    ArenaResource& operator=(ArenaResource&&) = delete;

    ~ArenaResource() override { std::free(buffer_); }

    void reset() { offset_ = 0; }
};

void process_frame(const Scene& scene) {
    ArenaResource arena(16 * 1024 * 1024);   // 16MB

    std::pmr::vector<glm::mat4> matrices(&arena);
    std::pmr::vector<RenderCommand> commands(&arena);

    for (const auto& obj : scene) {
        matrices.push_back(obj.transform);
        commands.push_back(build_command(obj));
    }

    render(matrices, commands);
}                                             // arena 소멸: 한 번에 회수
```

이 resource는 동기화되지 않으며 `reset()`은 이 resource에서 받은 storage를 참조하는 객체가 모두 파괴된 뒤에만 호출한다. `deallocate`가 no-op이어도 컨테이너 destructor는 원소 destructor를 실행한다. Resource 객체와 backing storage는 자신을 사용하는 모든 PMR container보다 오래 살아야 한다.

### 표준 제공 리소스

표준 라이브러리는 자주 쓰이는 리소스를 미리 제공한다.

| 리소스 | 용도 |
|--------|------|
| `std::pmr::monotonic_buffer_resource` | bump pointer 기반 arena. 해제는 무시 |
| `std::pmr::unsynchronized_pool_resource` | 크기별 풀, 단일 스레드 |
| `std::pmr::synchronized_pool_resource` | 크기별 풀, 락 보호 |
| `std::pmr::new_delete_resource` | allocation을 `::operator new`/`delete`에 위임 |
| `std::pmr::null_memory_resource` | `allocate`는 `bad_alloc`, `deallocate`는 효과 없음. 폴백 차단용 |

```cpp
void example() {
    std::byte buffer[1024];
    std::pmr::monotonic_buffer_resource arena{buffer, sizeof(buffer)};
    std::pmr::vector<int> vec(&arena);
    vec.reserve(100);   // buffer 안에서 할당, 고갈 시 new로 폴백
}
```

`unsynchronized_pool_resource`는 여러 block size의 pool을 관리하고, pool option의 largest block을 넘거나 맞는 pool로 처리하지 않는 요청은 upstream resource에 위임할 수 있다. 구체적인 size-class 사다리가 8, 16, 32, 64바이트의 거듭제곱이라는 보장은 표준에 없으며 구현 세부사항이다.


## 7. Allocator-aware 컨테이너

`std::pmr::vector`처럼 allocator를 받을 수 있는 컨테이너를 **allocator-aware container**라고 부른다. 표준 컨테이너 대부분이 이미 allocator-aware다. 다만 동작 규칙이 비직관적이라 몇 가지를 짚어둘 필요가 있다.

### polymorphic_allocator의 동작

`std::pmr::polymorphic_allocator<T>`는 내부적으로 `memory_resource*` 하나만 들고 다닌다. 컨테이너가 이 allocator를 복사해도 같은 resource를 가리킨다.

```cpp
template<class T>
class polymorphic_allocator {
    memory_resource* mr_;
public:
    polymorphic_allocator() noexcept
        : mr_(std::pmr::get_default_resource()) {}
    polymorphic_allocator(memory_resource* mr) noexcept : mr_(mr) {}

    T* allocate(size_t n) {
        if (n > std::numeric_limits<size_t>::max() / sizeof(T))
            throw std::bad_array_new_length();
        return static_cast<T*>(mr_->allocate(n * sizeof(T), alignof(T)));
    }
    void deallocate(T* p, size_t n) {
        mr_->deallocate(p, n * sizeof(T), alignof(T));
    }
    // ...
};
```

이 덕분에 서로 다른 resource를 사용하는 두 `std::pmr::vector<int>`는 **같은 컨테이너 타입**이다. 표준 `std::vector<int, MyAlloc>`은 allocator의 정적 타입에 따라 컨테이너 타입도 달라지지만, PMR 변형은 자원 선택을 런타임 포인터로 옮겨 이 결합을 끊는다. 물론 `vector`와 `list`처럼 컨테이너 종류 자체가 다르면 서로 다른 타입이다.

### propagate_on_container_*

allocator는 컨테이너의 복사/이동/swap에서 함께 따라갈지 여부를 트레이트로 노출한다.

| 트레이트 | 의미 |
|---------|------|
| `propagate_on_container_copy_assignment` | 복사 대입 시 src의 allocator를 dst로 복사 |
| `propagate_on_container_move_assignment` | 이동 대입 시 src의 allocator를 dst로 이동 |
| `propagate_on_container_swap` | swap 시 allocator도 swap |
| `is_always_equal` | 두 allocator 인스턴스가 항상 같다고 봐도 되는가 |

```cpp
struct MyAlloc {
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap            = std::true_type;
    using is_always_equal                        = std::false_type;
    // ...
};
```

allocator가 trait를 직접 정의하지 않으면 세 propagation trait는 `false_type`이고, `allocator_traits<Alloc>::is_always_equal`은 기본적으로 `is_empty<Alloc>::type`이다. 즉 빈 stateless allocator는 모든 instance가 동등하다고 추론될 수 있다.

`propagate_on_container_swap`이 `false`이면 allocator는 swap되지 않는다. 이때 두 allocator가 `operator==`로 같지 않으면 표준 container의 swap은 undefined behavior다. `is_always_equal == false`는 “반드시 다르다”가 아니라 instance별 비교가 필요하다는 뜻이므로, 실제 `get_allocator() == other.get_allocator()` 조건을 확인해야 한다.

`std::pmr::polymorphic_allocator`는 propagation trait와 `is_always_equal`이 `false_type`이다. 복사 대입은 대상 resource를 유지하고, allocator가 다른 move assignment는 원소별 이동이 필요할 수 있다. 서로 동등하지 않은 resource를 사용하는 두 PMR container를 그대로 swap하는 것은 허용되지 않는다. resource lifetime과 equality가 컨테이너 연산의 전제조건이 된다.

### scoped_allocator_adaptor

컨테이너의 컨테이너에서 자주 발생하는 문제는 다음과 같다.

```cpp
ArenaResource arena(1 << 20);
std::pmr::vector<std::pmr::string> strings(&arena);

strings.emplace_back("hello");
```

바깥 vector는 arena에서 allocator를 받지만, 안쪽 string은 어디에서 할당하는가? 만약 allocator가 element로 전파되지 않는다면 안쪽 string은 기본값(`get_default_resource`)을 쓰게 되어, vector의 노드는 arena에 있는데 string의 char 버퍼는 글로벌 힙에 놓이는 어긋남이 생긴다.

`std::pmr::polymorphic_allocator`는 **uses-allocator construction**을 자동으로 처리해 이 문제를 해결한다. 컨테이너가 element를 생성할 때, 만약 element가 allocator를 받을 수 있다면 자기 자신의 allocator를 전파한다. 그래서 위 코드에서 안쪽 string도 같은 arena를 사용한다.

표준 allocator로 같은 효과를 얻으려면 `std::scoped_allocator_adaptor`를 명시적으로 써야 한다.

```cpp
using InnerAlloc = MyAlloc<char>;
using InnerString =
    std::basic_string<char, std::char_traits<char>, InnerAlloc>;
using OuterBase = MyAlloc<InnerString>;
using OuterAlloc =
    std::scoped_allocator_adaptor<OuterBase, InnerAlloc>;

OuterAlloc outer_alloc{OuterBase{}, InnerAlloc{}};
std::vector<InnerString, OuterAlloc> v{outer_alloc};
v.emplace_back("hello");   // string도 inner_alloc으로 할당
```

중요한 점은 element 타입 자체가 `InnerAlloc`을 allocator 타입으로 받아야 한다는 것이다. `std::string`은 `std::allocator<char>`가 타입에 고정된 별칭이므로 임의의 `MyAlloc<char>`를 주입할 수 없다. 위 예제는 allocator 타입까지 포함한 `InnerString`을 정의한다.

`scoped_allocator_adaptor<Outer, InnerN...>`는 컨테이너 자신은 Outer를 쓰고, element의 allocator로 Inner를 흘려보낸다. 중첩이 깊어지면 Inner 자리에 또 다른 `scoped_allocator_adaptor`를 둔다.

`std::pmr` 시리즈는 이 메커니즘을 내장하고 있어, 대부분의 경우 사용자가 `scoped_allocator_adaptor`를 직접 손댈 필요가 없다.


## 8. 하이브리드 패턴: Arena+Slab+Free List 결합

레거시 게임 엔진이나 고성능 시스템 코드베이스에서는 단일 할당자가 아닌 복합적인 메모리 관리 구조를 자주 마주한다. 하나의 할당자만으로는 모든 요구를 충족하지 못하기 때문이다.

### 단일 할당자의 한계

| 할당자 | 강점 | 약점 |
|--------|------|------|
| Arena | $O(1)$ 할당, 캐시 지역성 | 개별 해제 불가 |
| Pool | $O(1)$ 할당/해제, 재사용 | 크기 고정 |
| Free List | 개별 해제 가능 | 탐색 비용, 단편화 |

2000년대 초반 게임 엔진들은 이 셋을 계층적으로 결합했다.

| 계층 | 담당 | 특징 |
|------|------|------|
| Arena | OS로부터 대량 확보 | VirtualAlloc/mmap으로 시스템 콜 최소화 |
| Size-class Pool | 크기별 블록 관리 | 16B/32B/64B/128B... 각각의 풀 |
| Free List | 해제된 블록 재사용 | 블록 내부에 next 포인터, $O(1)$ 재할당 |

### 블록 헤더와 union

할당된 블록과 해제된 블록은 상호 배타적이다. 같은 메모리 공간을 두 용도로 쓸 수 있다.

```c
typedef struct BlockHeader {
    uint32_t generation;
    uint16_t class_index;
    uint16_t state;
    union {
        struct BlockHeader* next_free;   // Free 상태
        size_t              requested_size; // Active 상태
    };
} BlockHeader;

enum {
    BLOCK_FREE = 0,
    BLOCK_ACTIVE = 1
};

#define SMALL_ALIGNMENT 16
#define HEADER_BYTES \
    ((sizeof(BlockHeader) + SMALL_ALIGNMENT - 1) & ~(SMALL_ALIGNMENT - 1))
#define BLOCK_SIZE   64
#define PAYLOAD_SIZE (BLOCK_SIZE - HEADER_BYTES)
```

| 상태 | union 멤버 | 의미 |
|------|------------|------|
| 사용 중 | requested_size | 호출자가 요청한 payload 크기 |
| Free | next_free | 다음 free 블록 포인터 |

### 풀 내부 동작

```c
typedef struct SizeClassPool {
    uint8_t*     memory;
    BlockHeader* free_head;
    size_t       block_size;
    size_t       block_count;
    uint16_t     class_index;
} SizeClassPool;

void* size_class_alloc(SizeClassPool* pool, size_t requested_size) {
    if (pool == NULL || pool->free_head == NULL) return NULL;
    if (requested_size > pool->block_size - HEADER_BYTES)
        return NULL;

    BlockHeader* block = pool->free_head;
    pool->free_head = block->next_free;

    block->class_index = pool->class_index;
    block->state = BLOCK_ACTIVE;
    block->requested_size = requested_size;  // Free → Active
    return (uint8_t*)block + HEADER_BYTES;
}

bool size_class_free(SizeClassPool* pool, void* ptr) {
    if (!pool || !ptr) return false;

    uintptr_t base = (uintptr_t)pool->memory;
    uintptr_t address = (uintptr_t)ptr;
    size_t total = pool->block_size * pool->block_count;
    if (address < base + HEADER_BYTES ||
        address - base >= total)
        return false;

    size_t payload_offset = (size_t)(address - base);
    size_t block_offset = payload_offset - HEADER_BYTES;
    if (block_offset % pool->block_size != 0)
        return false;

    BlockHeader* block =
        (BlockHeader*)(pool->memory + block_offset);
    if (block->state != BLOCK_ACTIVE ||
        block->class_index != pool->class_index)
        return false;

    block->generation++;
    block->state = BLOCK_FREE;
    block->next_free = pool->free_head;   // Active → Free
    pool->free_head = block;
    return true;
}
```

Pool 초기화는 `block_size * block_count`의 overflow를 검사하고, 각 header의 `generation = 0`, `state = BLOCK_FREE`, `class_index`와 `next_free`를 설정해야 한다. Free-list push/pop 자체는 상수 시간이지만 고갈 검사, debug 상태 검증과 class routing 분기는 남는다. 예제는 단일 스레드 전제다. `generation`은 stale handle 검증에 사용할 수 있지만 raw pointer만 외부에 노출하면 세대 정보를 대조할 방법이 없다.

### 참조 카운팅과 allocator 상태를 분리한다

Allocator의 Active/Free 상태와 객체의 shared ownership은 다른 불변식이다. Reference count에는 원자적 갱신, strong/weak count 관계, 마지막 owner가 수행할 destructor, overflow와 resurrection 정책이 필요하다. 이를 free-list union의 정수 하나로 대신하면 다른 스레드가 참조를 늘리는 순간 block이 재사용되는 경쟁을 막을 수 없다.

공유 소유권이 필요하면 객체나 별도 control block이 strong/weak count를 관리한다. Strong count가 0이 된 스레드가 객체 수명을 끝내고, weak 관찰자까지 control block을 더 이상 사용하지 않을 때 allocator에 storage를 반환한다. 그 뒤에만 같은 union storage를 `next_free`로 재해석할 수 있다.

### 크기 클래스 라우팅

```c
#define SIZE_CLASS_COUNT 8

static const size_t SIZE_CLASSES[SIZE_CLASS_COUNT] = {
    16, 32, 64, 128, 256, 512, 1024, 2048
};

typedef struct LargeAllocator LargeAllocator;

typedef struct Allocator {
    SizeClassPool pools[SIZE_CLASS_COUNT];
    LargeAllocator* large;
} Allocator;

static int size_to_class(size_t size, size_t alignment) {
    if (!is_power_of_two_size(alignment) ||
        alignment > SMALL_ALIGNMENT)
        return -1;

    for (int i = 0; i < SIZE_CLASS_COUNT; i++)
        if (size <= SIZE_CLASSES[i])
            return i;
    return -1;   // 너무 크거나 over-aligned: large allocation 경로
}

void* allocator_alloc(Allocator* a, size_t size, size_t alignment) {
    int idx = size_to_class(size, alignment);
    if (idx < 0)
        return large_alloc(a->large, size, alignment);
    return size_class_alloc(&a->pools[idx], size);
}
```

| 요청 크기 | 라우팅 | 풀 |
|:---------:|--------|-----|
| 1~16 B | class 0 | Pool 16B |
| 17~32 B | class 1 | Pool 32B |
| 33~64 B | class 2 | Pool 64B |
|... |... |... |
| > 2,048 B 또는 class보다 큰 정렬 | -1 | 개별 해제가 가능한 large allocation |

`SIZE_CLASSES`는 payload 용량이다. 각 pool의 실제 block stride는 `HEADER_BYTES + payload capacity`를 `SMALL_ALIGNMENT` 배수로 올리고, backing storage도 같은 정렬로 확보해야 한다. 이 예제의 small path는 최대 16바이트 정렬만 지원하며 더 큰 정렬은 large path로 보낸다.

해제 시 호출자가 `size`만 다시 계산해 class를 고르면 최초 요청의 alignment와 라우팅 정보를 잃을 수 있다. Header의 `class_index`에 small/large 구분까지 기록하고, `allocator_free(ptr)`가 그 metadata를 검증해 정확한 pool 또는 large-allocation 해제 경로로 보내야 한다. C++ allocator처럼 deallocation API가 size와 alignment를 함께 받는 경우에도 debug header와 일치하는지 대조하면 잘못된 호출을 조기에 찾을 수 있다.

### 캐시 지역성

free list 순회가 필요 없다는 점이 중요하다. alloc은 head를 꺼내고 free는 head에 붙이므로 연산 복잡도는 $O(1)$이다. 그러나 head가 가리키는 블록이 캐시에 있다는 보장은 없으므로 캐시 미스가 사라지는 것은 아니다. LIFO는 최근 해제된 블록을 먼저 재사용해 시간 지역성을 얻을 가능성을 높인다.

C++17 `std::pmr`은 이런 자원 조합을 런타임 interface로 표현한다. `std::pmr::synchronized_pool_resource`는 여러 size class의 pool을 제공하고, `monotonic_buffer_resource`는 upstream resource에서 큰 block을 받아 bump allocation한다. 어느 resource를 upstream에 둘지는 수명과 deallocation 계약에 따라 결정하며, 두 클래스를 연결했다고 임의의 계층형 allocator와 같은 동작이 자동으로 생기는 것은 아니다.


## 9. 성능 벤치마크

### 검증 가능한 비교 설계

할당자 비교는 하나의 64바이트 반복문으로 끝나지 않는다.

| 실험 | 고정할 조건 | 기록할 값 |
|------|------------|----------|
| 단순 할당/해제 | 크기, 총 연산 수, 결과 사용 방식 | ops/s, median, p95/p99 |
| 수명 혼합 | 프레임·세션·영구 수명의 비율 | peak RSS, fragmentation |
| locality | 같은 객체 배열과 같은 update 함수 | cache miss, IPC, bandwidth |
| cross-thread free | 생산자·소비자 배치 | CAS 실패, remote free 지연 |
| 확장성 | CPU pinning, thread/CPU 비율 | thread 수별 처리량 곡선 |
| reset/purge | reset 주기와 working set | frame max, page fault, RSS 회수 |

Pool이 객체를 반드시 한 캐시 라인에 여러 개 넣는 것은 아니다. 객체 크기와 정렬이 64바이트 이상이면 한 객체가 여러 라인을 차지할 수 있고, free-list 포인터나 디버그 패딩도 레이아웃에 영향을 준다. 실제 `sizeof`, alignment와 stride를 출력해 확인해야 한다. Thread-local arena도 backing store 보충, NUMA 배치와 전체 메모리 대역폭에서 포화되므로 완전한 선형 확장성을 보장하지 않는다.


## 10. 게임 엔진 적용 사례

### 메모리 아키텍처

전형적인 게임 엔진은 영구 메모리, 프레임 임시 메모리, 객체 풀, 스택을 분리해서 관리한다.

```cpp
class GameMemorySystem {
    ArenaAllocator permanent_arena_{512u << 20};   // 512MB
    ArenaAllocator frame_arena_{128u << 20};       // 128MB

    Pool<Entity, 100'000>    entity_pool_;
    Pool<Transform, 100'000> transform_pool_;
    Pool<RenderMesh, 50'000> mesh_pool_;
    Pool<Particle, 1'000'000> particle_pool_;

    ArenaAllocator physics_arena_{64u << 20};      // Bullet Physics 전용
    Pool<AudioBuffer, 1'000>  audio_pool_;
    Pool<NetPacket, 10'000>   packet_pool_;

public:
    void frame_begin() { frame_arena_.reset(); }   // O(1) 회수

    void on_level_change() {
        permanent_arena_.reset();   // 이전 레벨 데이터 일괄 삭제
        // 풀은 유지: 엔티티 슬롯 재사용
    }

    void* alloc_frame(size_t size, size_t align) {
        return frame_arena_.allocate(size, align);
    }

    Entity*  spawn_entity()        { return entity_pool_.allocate(); }
    void     destroy_entity(Entity* e) { entity_pool_.deallocate(e); }
};
```

### 프레임 루프

```cpp
void GameEngine::update(float dt) {
    memory_.frame_begin();   // 임시 메모리 일괄 회수

    // 카메라 (frame arena)
    auto* view = memory_.alloc_frame<glm::mat4>();
    auto* proj = memory_.alloc_frame<glm::mat4>();
    calculate_view_projection(camera_, view, proj);

    // 컬링
    auto* frustum = memory_.alloc_frame<Frustum>();
    extract_frustum(view, proj, frustum);

    std::pmr::vector<Entity*> visible(&frame_arena_);
    cull_entities(frustum, visible);

    // 파티클 (pool)
    update_particle_system(dt);

    // 렌더 커맨드 (frame arena)
    std::pmr::vector<RenderCommand> commands(&frame_arena_);
    for (Entity* e : visible)
        commands.push_back(build_render_command(e));

    renderer_.submit(commands);
}   // 다음 프레임 시작 시 frame_arena 리셋
```

### 레벨 로딩

```c
void load_level(const char* path, GameMemorySystem& mem) {
    arena_reset(&mem.permanent_arena);   // 이전 레벨 데이터 삭제

    auto temp = temp_arena_begin(&mem.permanent_arena);

    char* json = arena_alloc(&mem.permanent_arena, 10u << 20);
    load_file(path, json);
    JSON* parsed = parse_json(json, &mem.permanent_arena);

    for (int i = 0; i < parsed->mesh_count; i++) {
        Mesh* mesh = arena_alloc(&mem.permanent_arena, sizeof(Mesh));
        load_mesh(parsed->meshes[i], mesh, &mem.permanent_arena);
    }

    temp_arena_end(temp);   // JSON 파싱 데이터 회수, 메시는 유지
}
```

`temp_arena_begin`/`end`는 arena의 현재 offset을 저장했다가 복원하는 마커 패턴이다. 일시적인 작업 메모리를 깔끔하게 회수할 수 있다.

### 임베디드: 고정 메모리 버짓

```cpp
class EmbeddedMemorySystem {
    static constexpr size_t PERMANENT_SIZE = 32u << 20;
    static constexpr size_t FRAME_SIZE     = 8u  << 20;

    alignas(64) uint8_t permanent_buffer_[PERMANENT_SIZE];
    alignas(64) uint8_t frame_buffer_[FRAME_SIZE];

    BumpAllocator permanent_;
    BumpAllocator frame_;
public:
    EmbeddedMemorySystem()
        : permanent_(permanent_buffer_, PERMANENT_SIZE),
          frame_(frame_buffer_, FRAME_SIZE) {}

    void* operator new(size_t) = delete;   // malloc 사용 금지
};
```

64MB RAM 임베디드 환경에서는 `malloc`을 아예 봉인한다. 모든 메모리가 정적 버퍼에 잡히므로 사용량을 컴파일 타임에 검증할 수 있다.

### Unreal Engine 스타일 통합

```cpp
class FMemoryManager {
public:
    FMallocBinned2 binned_malloc;             // jemalloc 스타일
    thread_local FMemStack thread_stack;      // 스레드 임시
    TObjectPool<AActor>    actor_pool;
    TObjectPool<UComponent> component_pool;
    FMallocAnsi platform_malloc;              // 플랫폼 C allocator 래퍼

    void* Malloc(size_t size, uint32 align) {
        if (size > LARGE_ALLOC_THRESHOLD)
            return platform_malloc.Malloc(size, align);
        return binned_malloc.Malloc(size, align);
    }
};
```

| 카테고리 | 전략 |
|---------|------|
| 일반 할당 | jemalloc 스타일 binned allocator |
| 스레드 임시 | TLS stack |
| 핫 타입 | 전용 pool |
| 대용량 (>1MB) | 플랫폼 allocator로 위임하는 예시. 실제 OS 매핑 여부는 그 구현과 임계값에 달림 |


## 11. 디버깅 도구

### Allocation Tracker

```cpp
struct AllocationRecord {
    void* ptr;
    size_t size;
    const char* file;
    int line;
    std::chrono::steady_clock::time_point timestamp;
};

class AllocationTracker {
    std::vector<AllocationRecord> records_;
    std::mutex mutex_;
public:
    void record(void* ptr, size_t size, const char* file, int line) {
        std::lock_guard lock(mutex_);
        records_.push_back({ptr, size, file, line,
                            std::chrono::steady_clock::now()});
    }

    void remove(void* ptr) {
        std::lock_guard lock(mutex_);
        auto it = std::find_if(records_.begin(), records_.end(),
            [ptr](const auto& r) { return r.ptr == ptr; });
        if (it != records_.end()) records_.erase(it);
    }

    void dump_report() {
        std::lock_guard lock(mutex_);
        if (records_.empty()) {
            printf("No leaks detected.\n");
            return;
        }
        std::map<std::string, std::vector<AllocationRecord>> by_file;
        for (const auto& r : records_) by_file[r.file].push_back(r);

        printf("=== Memory Leak Report (%zu) ===\n", records_.size());
        for (const auto& [file, recs] : by_file) {
            size_t total = 0;
            for (const auto& r : recs) total += r.size;
            printf("%s: %zu bytes / %zu allocs\n",
                   file.c_str(), total, recs.size());
        }
    }
};

template <class Allocator>
void* tracked_alloc(Allocator& allocator, size_t size,
                    const char* file, int line) {
    void* p = allocator.allocate(size);
    g_tracker.record(p, size, file, line);
    return p;
}

#define TRACKED_ALLOC(allocator, size) \
    tracked_alloc((allocator), (size), __FILE__, __LINE__)
```

원래 GNU statement expression `({ ... })`을 쓰면 GCC·Clang 확장에 의존한다. 함수 템플릿이 실제 작업을 맡고 매크로는 호출 위치만 전달하게 하면 MSVC를 포함한 표준 C++ 구현에서 같은 인터페이스를 사용할 수 있다.

### Heap Visualizer

```cpp
class HeapVisualizer {
    struct Block {
        size_t offset, size;
        const char* label;
        bool is_free;
    };
    std::vector<Block> blocks_;
public:
    void add_block(size_t off, size_t size, const char* label, bool free) {
        blocks_.push_back({off, size, label, free});
    }

    void render_ascii(size_t total_capacity) {
        constexpr int WIDTH = 80;
        std::string line(WIDTH, '.');
        for (const auto& b : blocks_) {
            int s = (b.offset * WIDTH) / total_capacity;
            int e = ((b.offset + b.size) * WIDTH) / total_capacity;
            char ch = b.is_free ? '.' : '#';
            for (int i = s; i < e && i < WIDTH; i++) line[i] = ch;
        }
        printf("[%s]\n", line.c_str());
    }
};
```

출력 예시:

```text
Frame Arena (128 MB):
[################################...........................]
Total: 134,217,728 bytes, Used: 54,832,128 bytes (40.9%)

Breakdown:
  Matrices: 16,384 bytes
  Frustum: 128 bytes
  VisibleEntities: 4,096 bytes
  RenderCommands: 54,811,520 bytes
```

### Debug 패턴 채우기

해제된 메모리를 0xDD로, 가드 영역을 0xFD로, 새로 할당된 영역을 0xCD로 채우면 use-after-free와 버퍼 오버런을 즉시 잡을 수 있다.

```c
#ifdef DEBUG
void pool_free_debug(Pool* pool, void* ptr) {
    BlockHeader* h = get_header(ptr);
    if (is_in_free_list(pool, h)) {
        fprintf(stderr, "ERROR: Double free at %p\n", ptr);
        abort();
    }
    memset(ptr, 0xDD, PAYLOAD_SIZE);   // dead pattern
    h->next_free = pool->free_head;
    pool->free_head = h;
}
#endif
```

### 통계와 Watermark

```c
typedef struct PoolDebug {
    Pool   base;
    size_t total_allocations;
    size_t current_allocations;
    size_t peak_allocations;
} PoolDebug;

void* pool_alloc_debug(PoolDebug* p) {
    void* ptr = pool_alloc(&p->base);
    if (ptr) {
        p->total_allocations++;
        if (++p->current_allocations > p->peak_allocations)
            p->peak_allocations = p->current_allocations;
    }
    return ptr;
}
```

프레임 종료 시 통계를 출력해 메모리 사용 추세를 추적한다.

```text
=== Memory Statistics ===
Permanent: 187,432,960 / 268,435,456 (69.8%)
Transient: 8,388,608 peak / 134,217,728 (6.2%)
Entity pool: 3,456 / 100,000 used
Particle pool: 12,789 / 1,000,000 used
```


## 12. 할당자 선택 가이드

| 사용 패턴 | 권장 할당자 | 시간 복잡도 | 공간 오버헤드 |
|----------|------------|-----------|-------------|
| 프레임 단위 생성/삭제 | Arena | $O(1)$ 할당, $O(1)$ 리셋 | 정렬 패딩과 미사용 꼬리 공간 |
| 같은 크기 빈번한 재사용 | Pool | 보통 $O(1)$ 할당/해제 | free-list 표현과 페이지 단위 여유 공간 |
| 스코프 기반 LIFO | Stack | $O(1)$ 할당/롤백 | 정렬 패딩과 선택적 마커 |
| 다양한 크기, 병합 필요 | Buddy | 보통 $O(\log n)$ 또는 크기 클래스 수에 비례 | 요청을 다음 블록 크기로 올리는 내부 단편화 |
| 동일 크기 객체의 raw storage 재사용 | Slab | 보통 $O(1)$ fast path | slab 메타데이터와 비어 있는 슬롯 |
| 예측하기 어려운 범용 패턴 | 범용 힙 / PMR 자원 | 구현과 경합에 따라 달라짐 | 구현별 청크 메타데이터와 크기 클래스 여유 |

| 할당자 | 제약 | 보상 |
|--------|------|------|
| Arena | 개별 해제 불가 | 짧고 예측 가능한 할당 경로 |
| Pool | 크기 고정 | 단편화 제거, 캐시 효율 |
| Stack | LIFO 순서 | 스코프 관리, 누수 방지 |
| Buddy | 2의 거듭제곱 | 병합 가능, 외부 단편화 감소 |
| Slab | 크기·정렬 고정 | backing allocation과 크기 분류 비용 회피 |


---

## 결론

커스텀 할당자는 **제약을 활용해 비용과 지터를 줄이는 시스템 프로그래밍 기법**이다. 범용 힙은 다양한 크기·수명·스레드의 요청을 처리해야 한다. 사용 패턴이 알려져 있다면 락과 크기 탐색을 줄이고, 메타데이터를 별도 관리하며, 회수를 일괄 처리할 수 있다. 개선 폭은 객체 크기, 스레드 수, 메모리 접근, 범용 힙 구현에 따라 달라지므로 배수 자체를 일반화할 수 없다. 채택 여부는 동일한 워크로드의 처리량뿐 아니라 상위 지연, 단편화, 최대 상주 메모리까지 함께 측정해 결정한다.

C++17 `std::pmr`은 이 전통을 표준 인터페이스로 옮겨놓았다. arena, pool, monotonic buffer를 표준 컨테이너에 그대로 꽂아 쓸 수 있고, allocator-aware 컨테이너의 propagate 트레이트와 `polymorphic_allocator`의 uses-allocator construction이 중첩 컨테이너의 메모리 소속까지 자동으로 묶어준다. 직접 구현한 할당자라도 `memory_resource`를 상속하기만 하면 표준 컨테이너 생태계에 편입된다.

## 더 읽을거리

- Ginger Bill, "Memory Allocation Strategies" 시리즈: [Part 2 Linear/Arena](https://www.gingerbill.org/article/2019/02/08/memory-allocation-strategies-002/), [Part 3 Free Lists and Buddy](https://www.gingerbill.org/article/2019/02/15/memory-allocation-strategies-003/)
- Jason Gregory, *Game Engine Architecture* 3rd Ed., Chapter 5.2 Memory Management
- Bonwick, J. (1994), "The Slab Allocator: An Object-Caching Kernel Memory Allocator", USENIX Summer
- Wilson et al. (1995), "Dynamic Storage Allocation: A Survey and Critical Review"
- N3916 Polymorphic Memory Resources 제안서: <http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n3916.pdf>
- Paul Pedriana, [N2271: EASTL](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2271.html)
- cppreference, [`std::pmr::memory_resource`](https://en.cppreference.com/w/cpp/memory/memory_resource)
- cppreference, [`std::scoped_allocator_adaptor`](https://en.cppreference.com/w/cpp/memory/scoped_allocator_adaptor)
- LWN, [The SLUB allocator](https://lwn.net/Articles/229984/)
- jemalloc design: <https://github.com/jemalloc/jemalloc/wiki/Background>
- mimalloc design: <https://github.com/microsoft/mimalloc>
- EASTL source and documentation: <https://github.com/electronicarts/EASTL>
- LLVM `BumpPtrAllocatorImpl`: <https://llvm.org/doxygen/classllvm_1_1BumpPtrAllocatorImpl.html>
- Facebook Folly Arena: <https://github.com/facebook/folly/blob/main/folly/memory/Arena.h>
- Emery Berger et al., "Hoard: A Scalable Memory Allocator", ASPLOS 2000
