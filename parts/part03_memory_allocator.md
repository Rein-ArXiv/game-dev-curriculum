## 들어가며

현대 소프트웨어에서 메모리 할당은 가장 빈번하게 호출되는 연산 중 하나다. 게임 엔진은 매 프레임 수천 개의 객체를 생성하고 소멸시킨다. 웹 서버는 수만 개의 동시 요청을 처리하며 메모리를 할당하고 해제한다. 이런 환경에서 메모리 할당자의 성능은 전체 시스템의 병목이 된다.

malloc이라는 단순한 인터페이스 뒤에는 정교한 설계가 숨어 있다. Windows의 VirtualAlloc, glibc malloc, jemalloc, Google의 TCMalloc, Microsoft Research에서 시작된 mimalloc은 각각 다른 문제와 워크로드를 겨냥한다.

이 글은 OS 가상 메모리 API에서 시작해 표준 malloc 구현체들의 내부 구조까지 따라간다. 각 할당자가 어떤 트레이드오프를 선택했는지, 어떤 워크로드에 적합한지를 정량적으로 비교한다.


---

## 목차

1. [메모리 할당의 계층 구조](#1-메모리-할당의-계층-구조)
2. [glibc malloc(ptmalloc2): 멀티스레딩의 도전](#2-glibc-mallocptmalloc2-멀티스레딩의-도전)
3. [jemalloc: 단편화와 확장성의 균형](#3-jemalloc-단편화와-확장성의-균형)
4. [tcmalloc: Front/Middle/Back-End](#4-tcmalloc-frontmiddleback-end)
5. [mimalloc: Free List Sharding 혁명](#5-mimalloc-free-list-sharding-혁명)
6. [가상 메모리 최적화: Large Page와 Memory Mapping](#6-가상-메모리-최적화-large-page와-memory-mapping)
7. [할당자 선택 가이드](#7-할당자-선택-가이드)


---

## 1. 메모리 할당의 계층 구조

현대 운영체제에서 메모리 할당은 여러 계층으로 이루어져 있다. 각 계층은 서로 다른 추상화 수준과 성능 특성을 가지며, 위로 갈수록 더 세밀한 제어를, 아래로 갈수록 더 큰 단위의 할당을 담당한다.

### Windows 메모리 할당 스택

```c
malloc/new               <- 애플리케이션 레벨 (바이트 단위)
    ↓
UCRT malloc/new          <- C/C++ 런타임 API와 디버그 계층
    ↓
HeapAlloc/HeapFree       <- NT Heap/LFH (서브페이지 블록 관리)
    ↓
VirtualAlloc/VirtualFree <- 가상 메모리 API (페이지 단위)
    ↓
NtAllocateVirtualMemory  <- 커널 모드 (시스템 콜)
    ↓
Memory Manager           <- Page Frame Database (물리 메모리)
```

이 계층 구조가 존재하는 이유는 간단하다. 운영체제는 페이지 단위(보통 4KB)로만 메모리를 관리하는데, 애플리케이션은 16바이트나 64바이트 같은 작은 크기를 요청하기 때문이다.

### VirtualAlloc: 가상 메모리의 출발점

Windows의 가장 저수준 메모리 할당 API인 VirtualAlloc을 이해하는 것이 메모리 관리의 핵심이다.

```c
LPVOID VirtualAlloc(
    LPVOID lpAddress,        // NULL이면 시스템이 위치 결정
    SIZE_T dwSize,           // 바이트 단위 크기
    DWORD  flAllocationType, // MEM_RESERVE | MEM_COMMIT
    DWORD  flProtect         // PAGE_READWRITE 등
);
```

VirtualAlloc의 핵심은 **예약(Reserve)**과 **커밋(Commit)**의 분리이다. 이 두 개념의 차이를 이해하면 메모리 관리의 많은 부분이 명확해진다.

**MEM_RESERVE**는 프로세스 가상 주소 범위를 확보하지만 commit charge와 데이터 페이지를 제공하지 않는다. OS의 주소 공간 관리 메타데이터는 필요할 수 있다. commit하지 않은 페이지에 접근하면 access violation이 발생한다.

**MEM_COMMIT**은 페이지가 필요할 때 backing store를 제공할 수 있도록 시스템 commit charge를 확보하고 접근 가능한 상태로 만든다. 각 데이터 페이지의 물리 프레임은 보통 최초 접근의 demand-zero fault에서 연결되며, commit과 현재 resident RAM은 같은 개념이 아니다.

```c
// 1단계: 1GB 주소 공간 예약 (물리 메모리 0바이트)
LPVOID pReserved = VirtualAlloc(
    NULL,
    1024 * 1024 * 1024,  // 1GB
    MEM_RESERVE,
    PAGE_NOACCESS
);

// 2단계: 필요할 때만 4KB 커밋
LPVOID pCommitted = VirtualAlloc(
    pReserved,
    4096,
    MEM_COMMIT,
    PAGE_READWRITE
);

// 실제 물리 메모리 사용량: 4KB
// 예약된 주소 공간: 1GB
```

왜 이렇게 복잡한 구조일까? 몇 가지 강력한 활용 사례가 있다:

1. **희소 배열(Sparse Array)**: 1TB 주소 공간을 예약하고 실제로는 1GB만 사용하는 경우
2. **동적 확장**: 배열 재할당 없이 커밋 영역만 늘려서 크기를 증가
3. **메모리 압박 완화**: 필요한 시점에만 물리 메모리를 소비

64비트 시스템에서는 주소 공간이 사실상 무제한(이론적으로 128TB+)이므로, 예약은 매우 저렴한 연산이다.

### 페이지 크기와 서브페이지 할당의 필요성

VirtualAlloc의 가장 큰 제약은 페이지 단위로만 동작한다는 점이다.

```c
// 16바이트만 필요한데...
char* small = (char*)VirtualAlloc(
    NULL, 16, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
// 요청 크기는 페이지 경계로 반올림된다.
```

일반적인 Windows x86/x64 구성에서 데이터 페이지는 4KiB이고, `lpAddress == NULL`인 reserve는 시스템 allocation granularity 경계에 맞춰 주소 영역을 잡는다. 16바이트 요청 하나를 OS VM API에 직접 전달하면 적어도 한 데이터 페이지와 주소 공간 관리 비용을 소비하므로 작은 객체마다 호출하기에는 비효율적이다. large page 크기와 지원 조건은 시스템에서 조회해야 한다.

이것이 바로 **HeapAlloc**과 **malloc**이 필요한 이유이다. 이들은 VirtualAlloc으로 큰 페이지를 받아온 뒤, 내부적으로 작은 블록들로 분할해서 관리한다.

```c
// Windows Heap API
HANDLE hHeap = GetProcessHeap();
void* p = HeapAlloc(hHeap, 0, 16);  // 16바이트만 할당

// 내부 구조 (개념적):
// VirtualAlloc(4KB) → [16B][16B][32B][64B][...] (Bucket 구조)
```

HeapAlloc은 다음과 같은 일을 한다:
- VirtualAlloc으로 큰 페이지들을 미리 할당
- 내부적으로 작은 블록들로 분할 관리
- Free 시 즉시 VirtualFree를 호출하지 않고 재사용 풀에 보관
- 메타데이터로 할당 상태 추적

## 2. glibc malloc(ptmalloc2): 멀티스레딩의 도전

glibc의 기본 할당자인 ptmalloc2(pthreads malloc version 2)는 Wolfram Gloger가 개발했으며, 대부분의 Linux 시스템에서 사용된다. ptmalloc2는 Doug Lea의 dlmalloc에 멀티스레드 지원을 추가한 것이다.

### Chunk: 메모리 블록의 기본 단위

ptmalloc2의 핵심 데이터 구조는 **chunk**이다.

```c
struct malloc_chunk {
    size_t      prev_size;  // 이전 청크가 free면 그 크기
    size_t      size;       // 현재 청크 크기 + 플래그 (하위 3비트)

    // 아래 필드는 free 청크만 사용
    // 할당된 청크는 이 공간을 유저 데이터로 사용
    struct malloc_chunk* fd;  // Forward: 다음 free 청크
    struct malloc_chunk* bk;  // Backward: 이전 free 청크

    // Large 청크만 사용
    struct malloc_chunk* fd_nextsize;
    struct malloc_chunk* bk_nextsize;
};
```

여기서 놀라운 최적화가 하나 숨어있다. size 필드의 하위 3비트는 플래그로 사용된다:

- **P (PREV_INUSE, 0x01)**: 이전 청크가 사용 중
- **M (IS_MMAPPED, 0x02)**: mmap으로 할당됨
- **A (NON_MAIN_ARENA, 0x04)**: main arena가 아님

왜 하위 3비트를 플래그로 쓸 수 있을까? 청크 크기는 `MALLOC_ALIGNMENT`의 배수로 저장된다. 일반적인 64비트 glibc에서는 이 정렬이 16바이트라 하위 4비트가 0이고, 32비트 환경에서는 보통 8바이트 정렬이라 하위 3비트가 0이다. glibc의 `size` 필드는 그중 세 비트만 `P/M/A` 플래그로 정의한다. 최소 청크 크기가 32바이트라는 사실은 "모든 청크 크기가 32바이트 배수"라는 뜻이 아니므로, 하위 5비트 이상을 플래그로 자유롭게 쓸 수 있다는 의미가 아니다.

더 흥미로운 최적화는 **메모리 재사용**이다. 할당된 청크는 fd/bk 포인터를 유저 데이터 영역으로 덮어쓸 수 있다:

```text
Free Chunk (최소 32바이트):          Allocated Chunk (최소 32바이트, 유저 24B):
+------------------+                +------------------+
| prev_size (8B)   |                | prev_size (8B)   |
| size | P|M|A (8B)|                | size | P|M|A (8B)|
| fd (8B)          |                | user data        |
| bk (8B)          |                | user data        |
| user data...     |                | user data...     |
+------------------+                +------------------+
```

할당 시에는 fd/bk가 필요 없으므로 이 공간을 유저에게 제공하고, 해제 시에는 유저 데이터를 덮어쓰고 fd/bk를 설정한다. 이로써 메타데이터 오버헤드를 최소화한다.

### Arena: 멀티스레드 경합 해결

초기 malloc 구현은 단일 전역 힙을 사용했다. 이는 멀티스레드 환경에서 심각한 병목이 된다:

```c
// 모든 스레드가 하나의 락을 놓고 경쟁
Thread 1: malloc() → 락 대기...
Thread 2: malloc() → 락 획득 → 할당 → 락 해제
Thread 3: malloc() → 락 대기...
Thread 4: free() → 락 대기...
```

ptmalloc2는 **Arena**라는 개념으로 이를 해결한다.

```c
struct malloc_state {
    mutex_t mutex;  // 아레나별 락

    // Fastbins: 작은 청크 (요청 <= M_MXFAST, 64비트 기본 128B), LIFO
    mfastbinptr fastbinsY[NFASTBINS];

    // Unsorted bin: 최근 free된 청크들의 캐시
    mchunkptr bins[1];

    // Small bins: 청크 1024B 미만, 16B 간격 (62개, 64비트 기준)
    // Large bins: 청크 1024B 이상 (63개)
    mchunkptr bins[2..126];

    // Top chunk: 가장 큰 free 청크
    mchunkptr top;

    size_t system_mem;
    size_t max_system_mem;
};
```

각 스레드는 자신만의 arena를 가질 수 있다:

```text
Thread 1 → Arena 0 (main)
Thread 2 → Arena 1
Thread 3 → Arena 2
Thread 4 → Arena 0 (재사용, 일부 경합 발생)
```

- **Main Arena**: 프로세스의 힙 영역(brk/sbrk 사용)
- **Thread Arena**: mmap으로 별도 할당
- **최대 개수**: CPU 코어 수 * 2~8

Arena가 충분하면 각 스레드가 독립적으로 메모리를 할당할 수 있어 경합이 줄어든다. 하지만 스레드 수가 많으면 여전히 여러 스레드가 하나의 arena를 공유하게 된다.

### Bin 체계: 크기별 분류와 최적화

ptmalloc2는 free된 청크들을 크기에 따라 여러 **bin**으로 분류한다. 각 bin은 서로 다른 할당 전략과 성능 특성을 가진다.

#### Fast Bins: 속도 최우선

Fast bins는 작은 freed chunk를 크기 클래스별 단일 연결 리스트에 보관한다. 경계는 `global_max_fast`, 정렬, chunk header와 glibc 버전에 의해 결정되므로 “사용자 요청 128바이트 이하”를 고정 ABI로 보면 안 된다. `M_MXFAST`도 사용자 요청 크기와 내부 chunk 크기를 같은 숫자로 읽지 말고 대상 glibc의 `request2size`와 runtime 통계를 함께 확인한다.

```c
// Fast bin 구조 (단일 연결 리스트, LIFO)
// 64비트에서 청크 크기 32B부터 16B 간격
fastbins[0] (32B): -> [32B] -> [32B] -> [32B] -> NULL
fastbins[1] (48B): -> [48B] -> [48B] -> NULL
fastbins[2] (64B): -> [64B] -> [64B] -> [64B] -> [64B] -> NULL
...
fastbins[6] (128B): -> [128B] -> NULL
```

Fast bins의 핵심 특징:
1. **LIFO (Last In First Out)**: 가장 최근에 해제된 청크를 먼저 재사용 (캐시 지역성)
2. **단일 연결 리스트**: fd 포인터만 사용, bk 불필요 (메모리 절약)
3. **병합 안함**: 인접한 free 청크를 병합하지 않음 (속도 우선)

왜 병합하지 않을까? 작은 객체는 생성-소멸이 매우 빠르게 반복된다. 게임 엔진의 임시 벡터, 웹 서버의 요청 객체 등이 그 예이다. 이런 경우 병합 비용이 단편화 비용보다 크다.

```c
// 예: 게임 엔진의 매 프레임마다
for (int frame = 0; frame < 1000000; frame++) {
    Vec3* temp = malloc(24);  // Fast bin에서 즉시 할당
    // ... 계산 ...
    free(temp);                // Fast bin에 즉시 반환
}
// 병합했다면 매번 인접 청크 확인 필요 (비싼 연산)
```

Fast bins는 주기적으로 consolidation(병합) 과정을 거쳐 단편화를 관리한다.

#### Tcache: Thread Local Cache

glibc 2.26부터 도입된 **tcache**는 혁명적인 개선이다.

```c
typedef struct tcache_perthread_struct {
    uint16_t counts[TCACHE_MAX_BINS];        // 각 bin의 청크 개수
    tcache_entry *entries[TCACHE_MAX_BINS];  // LIFO 스택
} tcache_perthread_struct;
```

Tcache의 특성:
- **Thread-local**: 각 스레드마다 독립적 (TLS 사용)
- **락 없음**: 경합 제로
- **64개 bin**: 요청 크기 최대 1032바이트까지 커버 (16바이트 간격)
- **bin당 7개**: 최대 7개 청크까지 캐싱 (`glibc.malloc.tcache_count`로 조정)

두 경로의 차이는 다음 의사 코드로 드러난다.

```c
// Tcache 없음 (glibc 2.25)
malloc(32):
  1. Arena 락 획득
  2. Fastbin 검색
  3. 청크 반환
  4. Arena 락 해제
// 공유 arena 상태와 락을 건드리는 경로

// Tcache 있음 (glibc 2.26+)
malloc(32):
  1. TLS에서 tcache 획득 (락 없음)
  2. tcache->entries[bin] 검색
  3. 청크 반환
// 로컬 free-list를 갱신하는 짧은 경로
```

tcache hit는 arena 락을 피하므로 작은 할당의 경합을 줄인다. 그러나 스레드 수에 따라 캐시된 메모리와 RSS가 늘고, cache refill, 큰 객체, cross-thread free, arena 부족 경로는 여전히 공유 상태를 건드린다. 따라서 전체 처리량이 스레드 수에 선형 비례한다고 보장할 수 없다.

#### Unsorted Bin: 재사용 최적화

Unsorted bin은 독특한 역할을 한다. 최근에 free된 청크들(fast bin과 tcache에 들어가지 못한)을 임시로 보관하는 "캐시"이다.

```c
// free(ptr) 호출 시
if (tcache가 가득 참 && fastbin 크기 아님) {
    unsorted_bin에 삽입
}

// 다음 malloc() 시
1. Unsorted bin 순회
2. 요청 크기와 정확히 맞으면 즉시 반환
3. 맞지 않으면 적절한 small/large bin으로 정렬
```

이는 "방금 free한 메모리를 곧 다시 쓸 확률이 높다"는 시간적 지역성을 활용한 것이다. 특히 반복문 내에서 같은 크기의 객체를 할당-해제-재할당하는 패턴에서 효과적이다.

#### Small Bins과 Large Bins

Small bins는 1024바이트 미만의 청크(64비트 기준)를 관리하며, 각 bin은 정확한 크기를 담당한다:

```cpp
bins[2] (32B): <-> [32B] <-> [32B] <-> [32B]
bins[3] (48B): <-> [48B] <-> [48B]
bins[4] (64B): <-> [64B] <-> [64B] <-> [64B]
...
```

- 이중 연결 리스트 (FIFO)
- 정확한 크기 매칭
- 인접 청크와 병합 수행

Large bins는 small-bin 경계 이상의 청크를 점차 넓어지는 크기 범위로 그룹화한다. 정확한 bin index와 경계는 word size와 glibc 버전의 `largebin_index` 매크로에 달려 있으므로 다음처럼 고정된 ABI 표로 쓰지 않는다.

```cpp
small bin: 한 bin이 한 chunk size를 담당
large bin: 한 bin이 여러 chunk size 범위를 담당
           작은 구간은 촘촘하고 큰 구간은 점차 넓어짐
```

bin 내부에서는 크기 순으로 정렬되며(fd_nextsize/bk_nextsize 사용), **best-fit** 전략으로 가장 적합한 크기를 찾는다.

### malloc 할당 흐름

이제 실제 malloc 호출 시 어떤 일이 일어나는지 전체 흐름을 살펴보자:

```bash
malloc(size) 호출
    ↓
[1] Tcache 확인 (기본 설정의 작은 크기 클래스 && tcache 활성화)
    └→ Hit: arena 락 없이 로컬 엔트리 반환
    ↓
[2] Arena 락 획득
    ↓
[3] Fastbin 확인 (정규화된 chunk size가 현재 global_max_fast 범위인지 검사)
    └→ Hit: 반환 후 락 해제
    ↓
[4] Small bin 확인 (청크 < 1024B)
    └→ 정확한 크기 있으면 반환
    ↓
[5] Unsorted bin 처리
    └→ 순회하면서:
       - 맞는 크기 발견 → 즉시 반환
       - 안 맞으면 적절한 bin으로 정렬
    ↓
[6] Large bin 확인 (청크 >= 1024B)
    └→ Best-fit 검색
    ↓
[7] Top chunk 분할
    └→ top이 충분하면 분할해서 반환
    ↓
[8] 시스템 메모리 요청
    └→ brk() 또는 mmap() 호출
```

작은 객체가 반복 재사용되는 워크로드에서는 1단계나 3단계의 비중이 높다. 평균 지연은 glibc 버전, hardening 옵션, 크기 분포, 캐시 refill 빈도와 경합에 따라 달라지므로 경로별 hit 비율과 지연 분포를 함께 측정해야 한다.

### 멀티스레딩 한계

ptmalloc2의 arena 시스템은 개선이지만 완벽하지 않다. 스레드 수가 arena 수를 초과하면 경합이 발생한다:

```text
// 32코어 서버, 300개 스레드
// 최대 Arena: 32 * 8 = 256개

Thread 1-256: 각자 arena를 차지 (락 경합 없음)
Thread 257: Arena 0 재사용 → Thread 1과 경합!
Thread 258: Arena 1 재사용 → Thread 2와 경합!
```

스레드 수가 max arena 한도를 넘어서면 그 시점부터 동일 arena를 두 스레드 이상이 공유하게 되고, tcache가 비는 순간 arena 락에서 경합이 발생한다. arena 한도 자체는 환경 변수 `MALLOC_ARENA_MAX`로 조정할 수 있지만 너무 키우면 메모리 단편화가 늘어난다.

Tcache가 대부분의 경우를 커버하지만, tcache 미스 시에는 여전히 arena 락이 병목이 된다. 이것이 jemalloc과 tcmalloc 같은 대안 할당자가 등장한 배경이다.

## 3. jemalloc: 단편화와 확장성의 균형

**jemalloc**은 Jason Evans가 FreeBSD를 위해 개발한 범용 allocator다. 여러 운영체제와 장기 실행 서비스에서 채택된 사례가 있지만, 특정 제품이나 언어 런타임의 현재 기본 allocator 여부는 버전·플랫폼·빌드 설정에 따라 달라진다.

### 설계 철학

jemalloc의 핵심 목표는 두 가지이다:
1. **메모리 단편화 최소화**: 장기 실행 서버에서 메모리 누수 방지
2. **멀티스레드 확장성**: 스레드 수가 증가해도 성능 유지

이를 위해 세 가지 핵심 아이디어를 사용한다:
- **크기 클래스 기반 할당**: 정교한 크기 분류로 내부 단편화 최소화
- **Thread-local 캐싱**: 대부분의 할당을 락 없이 처리
- **Active Memory 추적**: 메모리 사용량을 능동적으로 관리

### 계층 구조: Tcache, Arena, Extent

**Application** → 할당 요청

| 계층 | 구성 요소 | 특징 |
|:----:|----------|------|
| **1. Tcache** | Thread Local 캐시 | 락 없음, 가장 빠름 |
| | - 크기 클래스별 캐시 | 대부분의 할당 여기서 처리 |
| | - 최대 200개 객체 | |
| ↓ | *(캐시 미스)* | |
| **2. Arena** | per-thread/shared | 최소한의 경합 |
| | - Bins (크기 클래스별) | |
| | - Extents (메모리 영역) | |
| ↓ | *(메모리 부족)* | |
| **3. Base Allocator** | OS 메모리 할당 | 큰 단위 할당 |
| | - `mmap` / `VirtualAlloc` | |
| | - 2MB aligned extents | |

#### Extent: 메모리 영역 관리자

Extent는 jemalloc의 독특한 개념이다. 큰 메모리 블록(보통 2MB)을 관리하는 단위이다.

```c
struct extent_t {
    void*        e_addr;        // 시작 주소 (2MB 정렬)
    size_t       e_size;        // 크기 (페이지 배수)
    arena_t*     e_arena;       // 소속 arena

    // Slab 정보 (작은 객체용)
    slab_data_t  e_slab_data;

    // 상태 플래그
    bool         e_committed;   // 물리 메모리 커밋 여부
    bool         e_zeroed;      // 0 초기화 여부

    // Red-Black Tree 노드 (빠른 검색)
    rb_node_t    e_size_node;   // 크기 기준 트리
    rb_node_t    e_addr_node;   // 주소 기준 트리
};
```

Extent는 다음 두 가지 용도로 사용된다:
1. **Slab으로 분할**: 작은 객체들을 담는 컨테이너
2. **직접 할당**: 큰 객체(1MB+)는 extent를 통째로 사용

#### Slab: 동일 크기 객체 컨테이너

Slab은 하나의 크기 클래스 객체들만 담는다. 예를 들어 64바이트 slab은 64바이트 객체만 포함한다.

**[2MiB extent를 64B 크기 클래스로 분할한 단순화 예]**

| 영역 | 크기 | 설명 |
|------|------|------|
| **Bitmap** | 4,088B (32,704비트) | 각 비트 = 객체 할당 상태 (0=free, 1=allocated) |
| **Data** | 2,093,056B (약 1.996MiB) | `[64B][64B][64B]...` 객체 32,704개 |

`32,704 × 64B = 2,093,056B`이고 bitmap은 `32,704 / 8 = 4,088B`다. 둘의 합은 2,097,144B로 2MiB보다 8B 작다. 실제 jemalloc의 extent·slab 크기와 metadata 배치는 버전, page 크기와 size class에 따라 달라지므로 이 표는 bitmap 산술을 보이는 독립 예이지 jemalloc 내부 구조의 고정 ABI가 아니다.

Bitmap을 사용하면 free slot을 매우 빠르게 찾을 수 있다:

```c
// Free slot 찾기 (CTZ: Count Trailing Zeros)
unsigned long bitmap = slab->bitmap[idx];
int free_bit = __builtin_ctzl(~bitmap);  // 하드웨어 명령어, O(1)
void* ptr = slab->base + free_bit * size_class;
```

CTZ 계열 연산은 지원 ISA에서 고정된 수의 명령으로 구현할 수 있어 전체 free list를 선형 탐색하는 것보다 유리하다. 정확한 latency와 throughput은 명령 선택, 입력 의존성, 마이크로아키텍처에 따라 달라지므로 "항상 한 사이클"로 고정해서는 안 된다. 또한 비트맵 word가 모두 사용 중일 때 `ctz(0)`을 호출하지 않도록 상위 수준에서 빈 word를 판별해야 한다.

#### Tcache: Thread Local Cache

jemalloc의 tcache는 ptmalloc2보다 더 정교하다.

```c
struct tcache_t {
    struct {
        void*    stack[TCACHE_NSLOTS];  // LIFO 스택 (보통 20개)
        uint16_t ncached;               // 현재 캐시된 개수
        uint16_t low_water;             // GC 임계값
    } bins[NBINS];  // 크기 클래스별

    uint64_t prof_accum;  // 프로파일링 카운터
};
```

동작 방식:

```c
void* malloc(size_t size) {
    size_t binind = size_to_bin(size);
    tcache_bin_t* bin = &tcache->bins[binind];

    if (bin->ncached > 0) {
        // Fast path: O(1), 락 없음
        return bin->stack[--bin->ncached];
    }

    // Slow path: arena에서 배치로 리필 (20개 정도)
    arena_batch_fill(bin, binind);
    return bin->stack[--bin->ncached];
}

void free(void* ptr) {
    size_t binind = ptr_to_bin(ptr);
    tcache_bin_t* bin = &tcache->bins[binind];

    if (bin->ncached < TCACHE_NSLOTS) {
        // Fast path: O(1), 락 없음
        bin->stack[bin->ncached++] = ptr;
        return;
    }

    // 캐시 가득 차면 arena로 플러시
    arena_batch_flush(bin, binind);
}
```

배치 전송(batch transfer)이 핵심이다. 한 번에 여러 객체를 이동함으로써 arena 락 획득 횟수를 1/N로 줄인다.

### 232개 크기 클래스: 단편화 최소화

jemalloc은 232개의 정교한 크기 클래스를 사용한다. 이는 무작위로 정한 것이 아니라, 내부 단편화를 최소화하도록 설계되었다.

#### Small 크기 클래스 (8B ~ 14KB)

```text
8B 단독:           8
16B 간격:          16, 32, 48, 64, 80, 96, 112, 128
32B 간격:          160, 192, 224, 256
64B 간격:          320, 384, 448, 512
128B 간격:         640, 768, 896, 1024
...
```

2의 거듭제곱 구간마다 4개의 클래스를 배치하므로, 크기가 두 배가 될 때마다 간격도 두 배가 된다. 이 설계의 내부 단편화 상한은 다음과 같다:

$$
\text{내부 단편화} = \frac{\text{할당 크기} - \text{요청 크기}}{\text{할당 크기}} < 20\%
$$

최악의 경우는 구간 경계 직후다. 예를 들어 129바이트를 요청하면 160바이트 클래스가 할당되어 낭비가 31바이트(19.4%)다. 반면 100바이트 요청은 112바이트 클래스에 들어가 낭비가 12바이트(10.7%)에 그친다.

#### Large allocation과 extent

jemalloc은 작은 요청을 size class와 slab로 처리하고, 더 큰 요청은 페이지 단위 extent에서 다룬다. 과거 버전의 문서에는 `large`와 `huge`라는 별도 경계가 등장하지만, 경계값과 내부 경로는 버전, 페이지 크기, 빌드 설정에 따라 달라졌다. 따라서 "4MiB 이상이면 항상 arena를 거치지 않고 `mmap`을 직접 호출한다" 같은 고정 규칙으로 이해하면 안 된다. 큰 요청도 arena의 extent 관리와 캐시·purge 정책에 참여할 수 있으며, OS 매핑을 새로 요청할지는 재사용 가능한 extent의 존재와 설정에 달려 있다.

### Active Memory 추적과 Decay-Based Purging

jemalloc의 가장 독특한 기능은 **메모리 상태 추적**이다.

```c
struct arena_t {
    size_t nactive;  // 사용 중인 페이지

    // 4단계 메모리 상태
    extent_tree_t extents_dirty;     // 해제됨, 빠른 재사용
    extent_tree_t extents_muzzy;     // 부분 해제
    extent_tree_t extents_retained;  // 완전 해제
};
```

메모리는 다음 4단계 상태를 거친다:

1. **Active**: 현재 사용 중
2. **Dirty**: 해제되었지만 물리 메모리는 유지 (빠른 재할당 가능)
3. **Muzzy**: MADV_FREE 적용, OS가 메모리 압박 시 회수 가능
4. **Retained**: 완전 해제, 주소 공간만 유지

**Decay-based purging**은 시간 기반으로 메모리를 단계적으로 해제한다:

```text
// 설정 예
decay_dirty_ms: 10000    // dirty → muzzy: 10초 후
decay_muzzy_ms: 10000    // muzzy → retained: 10초 후
```

이는 다음과 같은 효과를 낸다:
- 단기 메모리 스파이크를 dirty로 처리 (빠른 재사용)
- 장기 미사용 메모리는 자동으로 OS에 반환
- RSS(Resident Set Size) 지속적 증가 방지

장기 실행 서버(Redis, Cassandra 등)에서 jemalloc을 선호하는 이유가 바로 이 기능 때문이다.

## 4. tcmalloc: Front/Middle/Back-End

**TCMalloc**(Thread-Caching Malloc)은 Google이 개발한 allocator다. 여기서는 변하기 쉬운 제품 채택 목록이 아니라 공개 설계 문서가 설명하는 front end, middle end, back end의 책임을 기준으로 구조를 분석한다.

### 3층 아키텍처: Front/Middle/Back-End

tcmalloc은 명확한 3층 구조를 가진다:

| 계층 | 역할 | 구성 요소 | 주된 비용 |
|:----:|------|----------|-----------|
| **Front-End** | 빠른 할당 | ThreadCache 또는 Per-CPU Cache | 로컬 크기 클래스 접근 |
| ↓ | *(캐시 미스)* | | |
| **Middle-End** | 배치 전송 | TransferCache, CentralFreeList | 동기화와 객체 묶음 이동 |
| ↓ | *(객체 부족)* | | |
| **Back-End** | 페이지 관리 | PageHeap, HugePageAwareAllocator | span 분할·병합과 OS 메모리 |

각 층은 서로 다른 책임을 가지며, 대부분의 할당은 Front-End에서 처리된다.

### Front-End: Per-CPU Cache의 혁신

#### ThreadCache (전통적 방식)

```c
class ThreadCache {
private:
    FreeList list_[kNumClasses];  // 86개 크기 클래스
    size_t size_;                  // 현재 캐시 총 크기
    size_t max_size_;              // 동적 조절되는 최대 크기

public:
    void* Allocate(size_t cl) {
        FreeList* list = &list_[cl];
        if (!list->empty()) {
            return list->Pop();  // O(1)
        }
        return FetchFromCentral(cl);  // 중앙에서 배치로 가져옴
    }
};
```

ThreadCache는 TLS(Thread-Local Storage)에 저장되어 락 없이 접근 가능하다. 하지만 한 가지 문제가 있다:

```text
ThreadCache:
  - 스레드 수에 비례해 로컬 캐시와 메타데이터가 늘어난다.
  - idle 스레드의 캐시가 객체를 오래 보유할 수 있다.
  - 다른 스레드가 해제한 객체를 소유자 쪽으로 돌려보내는 비용이 생긴다.
```

#### Per-CPU Cache

현대 TCMalloc은 Linux의 restartable sequences(`rseq`)를 이용하는 Per-CPU front end를 제공한다. 2021년 OSDI 논문은 이 캐시 자체가 아니라 TCMalloc back end의 **Temeraire hugepage-aware allocator**를 다룬다. 두 설계를 같은 출처로 묶으면 안 된다.

```c
class PerCPUCache {
private:
    struct Slab {
        void* objects[64];   // 객체 포인터 배열
        uint8_t begin;       // 시작 인덱스
        uint8_t end;         // 끝 인덱스
    };

    // CPU 개수 * 크기 클래스 개수
    Slab slabs_[kNumCPUs][kNumClasses];

public:
    void* Allocate(size_t cl) {
        int cpu = GetCurrentCPU();  // rseq로 빠르게 획득
        Slab* slab = &slabs_[cpu][cl];

        if (slab->begin != slab->end) {
            return slab->objects[slab->begin++];  // O(1), 락 없음
        }

        return SlowPath(cl);
    }
};
```

핵심 기술은 **rseq**(restartable sequences)다. rseq는 등록한 userspace critical section이 선점, signal delivery나 CPU migration과 충돌하면 commit 전 시작 지점으로 되돌릴 수 있게 한다. 이것은 임의 메모리 연산에 원자성을 부여하는 기능이 아니며, 해당 per-CPU 자료구조의 단일 commit point와 slow path가 올바르게 설계되어야 한다:

```text
// rseq 개념 (의사코드)
do {
    cpu = current_cpu();
    // 여기서 CPU 바뀌면 자동으로 재시작
    result = slabs_[cpu][cl].pop();
} while (cpu_changed);
```

Per-CPU 방식은 활성 CPU 수를 기준으로 캐시 예산을 제한하고, 현재 CPU가 자주 접근하는 메타데이터를 같은 소유 영역에 모은다. 이 장점은 thread 수가 CPU 수보다 훨씬 많거나 스레드 생성·소멸이 잦을 때 커질 수 있다. 반대로 `rseq` 지원, CPU hotplug, 선점과 마이그레이션 처리라는 복잡성이 추가된다. ThreadCache와의 실제 차이는 할당 크기 분포와 원격 해제 패턴을 포함해 측정해야 한다.

### Middle-End: 배치 전송 최적화

#### TransferCache: 락 획득 최소화

TransferCache는 Front-End와 Back-End 사이에서 배치 전송을 담당한다.

```c
class TransferCache {
private:
    static constexpr int kMaxCapacity = 64;

    struct Entry {
        void* objects[kMaxCapacity];
        int count;
    };

    Entry slots_[kNumSlots];
    std::atomic<int> used_slots_;
    SpinLock lock_;

public:
    int RemoveRange(void** batch, int n) {
        SpinLockHolder h(&lock_);  // 한 번만 락 획득

        int total = 0;
        for (int i = 0; i < used_slots_ && total < n; ++i) {
            Entry& e = slots_[i];
            int to_move = std::min(e.count, n - total);
            memcpy(&batch[total], e.objects, to_move * sizeof(void*));
            total += to_move;
            e.count -= to_move;
        }
        return total;
    }
};
```

ThreadCache가 비면 TransferCache에서 한 번에 N개(보통 32~64개)를 가져온다. 이로써:
- 락 획득 횟수: N번 → 1번
- 캐시 지역성 향상: 연속적 메모리 접근
- Contention 감소: 락 보유 시간 단축

#### CentralFreeList: 글로벌 풀

CentralFreeList는 각 크기 클래스별로 존재하며, 실제 메모리 풀을 관리한다.

```c
class CentralFreeList {
private:
    SpinLock lock_;

    Span* nonempty_;  // 사용 가능한 객체 있는 span
    Span* empty_;     // 모든 객체 할당된 span

    size_t size_class_;
    size_t object_size_;
};
```

Span은 여러 페이지의 집합이며, 동일 크기 객체들을 담는다:

**Span (8 pages = 32KB, 64바이트 객체용):**

| 내용 | 설명 |
|------|------|
| `[64B][64B][64B]...[64B]` | 512개 객체 |
| FreeList | 빈 슬롯 추적 |

### Back-End: PageHeap와 HugePage

#### PageHeap: Span 관리자

PageHeap는 페이지 단위로 메모리를 관리한다.

```c
class PageHeap {
private:
    // 1~255 페이지: 배열 인덱싱 (O(1))
    SpanList free_[kMaxPages];

    // 256+ 페이지: 트리 구조 (O(log n))
    std::set<Span*, SpanSizeOrder> large_;

    HugePageAwareAllocator huge_allocator_;

public:
    Span* New(Length n) {
        // 1. 정확한 크기 검색
        if (n < kMaxPages && !free_[n].empty()) {
            return free_[n].Pop();
        }

        // 2. 더 큰 span 분할
        for (Length s = n + 1; s < kMaxPages; ++s) {
            if (!free_[s].empty()) {
                Span* span = free_[s].Pop();
                return Carve(span, n);  // n페이지만 떼어내고 나머지 반환
            }
        }

        // 3. Large span 검색
        auto it = large_.lower_bound(n);
        if (it != large_.end()) {
            return Carve(*it, n);
        }

        // 4. 시스템에서 새로 할당
        return AllocLarge(n);
    }

    void Delete(Span* span) {
        // 인접 span과 병합 (coalescing)
        Span* prev = GetDescriptor(span->first_page - 1);
        if (prev && prev->IsFree()) {
            span = Merge(prev, span);
        }

        Span* next = GetDescriptor(span->first_page + span->num_pages);
        if (next && next->IsFree()) {
            span = Merge(span, next);
        }

        PrependToFreeList(span);
    }
};
```

병합(coalescing)은 외부 단편화를 방지한다. 인접한 free span들을 하나로 합쳐서 큰 할당에 대응한다.

#### HugePageAwareAllocator: TLB 최적화

tcmalloc의 최신 버전은 **HugePage**(2MB 페이지)를 적극 활용한다.

TLB(Translation Lookaside Buffer)는 가상 주소를 물리 주소로 변환하는 캐시이다. TLB 미스는 100+ 사이클의 페이지 테이블 워킹을 발생시킨다.

TLB의 엔트리 수와 계층은 마이크로아키텍처마다 다르므로 “Intel x64” 하나의 고정 표로 설명할 수 없다. 동일한 엔트리 수라면 2MiB 페이지 하나가 4KiB 페이지보다 512배 넓은 가상 주소 범위를 매핑한다는 기하만 보편적이다. 실제 성능은 working set, 접근 패턴, page walk cache, NUMA 배치에 달려 있으며 huge page는 내부 단편화, 승격·분할 비용, 메모리 회수 지연을 늘릴 수 있다.

tcmalloc 계열은 큰 span을 받아 size class별 작은 객체로 나누고, 최신 구현은 huge page 친화적인 page heap을 제공할 수 있다. 그러나 allocator의 2MiB 정렬 span이 운영체제의 2MiB huge page로 실제 매핑되었다는 뜻은 아니다. THP 정책, `madvise`, 명시적 huge page 설정과 런타임 통계를 따로 확인해야 한다.

### 성능 벤치마크

할당자 순위는 size 분포, 생존 시간, 원격 해제 비율, thread 수, 메모리 상한과 반환 정책이 바뀌면 뒤집힌다. 단일 `malloc/free` 평균 ns 하나로는 게임 워크로드를 대표할 수 없다.

| 측정 축 | 기록할 값 |
|---|---|
| 지연 분포 | alloc/free의 p50·p95·p99, refill이 발생한 slow path |
| 처리량 | thread 수별 ops/s와 포화 지점 |
| 메모리 | live bytes, allocated bytes, resident/committed bytes, peak |
| 단편화 | `resident / live`, size class별 빈 span, 페이지 반환 지연 |
| 교차 스레드 | 할당 스레드와 해제 스레드가 다른 비율 |
| locality | NUMA 노드, CPU pinning, page fault·TLB miss |

동일한 미리 생성된 요청 trace를 각 할당자에 재생하고 warm-up, 샘플 수, CPU·OS·할당자 버전과 설정을 결과에 붙여야 비교가 재현된다.

tcmalloc의 Per-CPU mode는 front-end 캐시 수를 활성 스레드 수가 아니라 CPU 수에 묶어 메모리 증폭과 공용 메타데이터 경합을 줄이려는 설계다. 처리량이 스레드 수의 영향을 받지 않는다는 보장은 없으며, middle-end refill, remote free, 메모리 대역폭과 oversubscription에서 포화될 수 있다.

## 5. mimalloc: Free List Sharding 혁명

**mimalloc**은 Microsoft Research의 Daan Leijen과 Ben Zorn이 2019년 발표한 할당자이다. 기존 할당자와 완전히 다른 접근 방식을 사용한다.

### 핵심 아이디어: Page-Local Free List

전통적인 할당자(tcmalloc, jemalloc)는 크기 클래스별로 전역 free list를 유지한다:

```cpp
전역 Free List (64바이트):
[obj1@page1] -> [obj2@page3] -> [obj3@page1] -> [obj4@page5] -> ...
```

이 구조의 문제점:
1. **False Sharing**: 여러 스레드가 같은 캐시 라인을 수정
2. **캐시 지역성 나쁨**: 객체들이 여러 페이지에 분산
3. **크로스 스레드 해제**: 다른 스레드가 해제 시 락 필요

mimalloc의 해결책은 **각 페이지가 자신만의 free list를 소유**하는 것이다:

```c
Page 1 (스레드 A 소유):
  local:  [obj1] -> [obj2] -> [obj3] -> NULL
  thread: [obj7] (다른 스레드가 free한 것)

Page 2 (스레드 B 소유):
  local:  [obj4] -> [obj5] -> NULL
  thread: NULL
```

### Page 구조

```cpp
// 실제 필드명·폭은 mimalloc 버전에 따라 달라지는 구조 개요
struct mi_page_outline {
    size_t block_size;
    uint16_t capacity;      // 현재 사용할 수 있도록 초기화한 slot 수
    uint16_t reserved;      // page가 최대로 수용할 수 있는 slot 수
    uint16_t used;          // 사용 중인 slot 수

    mi_block_t* free;       // 즉시 allocation에 쓰는 owner-local list
    mi_block_t* local_free; // owner가 해제해 나중에 free로 병합할 list
    std::atomic<uintptr_t> xthread_free; // remote free pointer + 상태 비트

    mi_heap_t* heap;        // owner heap
    bool is_zero;
};
```

free list를 owner 경로와 remote 경로로 분리하는 것이 핵심이다.
- **free / local_free**: 페이지 소유 스레드가 조작하고 필요할 때 병합
- **xthread_free**: 다른 스레드가 CAS로 게시하며 owner가 나중에 수거

### 할당 흐름

```c
void* mi_malloc(size_t size) {
    // 1. 현재 스레드 힙 (TLS)
    mi_heap_t* heap = mi_heap_get_default();

    // 2. 크기 클래스
    size_t bin = mi_bin_index(size);

    // 3. 현재 페이지
    mi_page_t* page = heap->pages[bin];

    // 4. Local free list (락 없음, 빠름!)
    if (page->local_free != NULL) {
        mi_block_t* block = page->local_free;
        page->local_free = block->next;
        return block;
    }

    // 5. Thread free list 수확
    if (page->thread_free != NULL) {
        mi_page_collect_free(page);  // thread_free → local_free 이동
        // 재시도
        if (page->local_free != NULL) {
            mi_block_t* block = page->local_free;
            page->local_free = block->next;
            return block;
        }
    }

    // 6. 새 페이지 필요
    return mi_malloc_generic(heap, size);
}
```

작은 객체가 같은 소유 스레드에서 반복 사용되면 4단계의 local free-list 경로가 높은 비중을 차지한다. 평균 지연은 fast-path hit 비율과 페이지 보충 빈도에 따라 달라진다.

### 크로스 스레드 해제: 원격 free-list 게시

mimalloc은 다른 스레드가 소유한 페이지의 블록을 소유자 전용 리스트에 직접 넣지 않고 원자적인 remote free-list에 게시한다.

```c
void mi_free(void* ptr) {
    // 1. 포인터 → 페이지 변환 (O(1), 비트 마스킹)
    mi_page_t* page = mi_ptr_page(ptr);

    // 2. 소유 스레드 확인
    mi_heap_t* heap = page->heap;

    if (mi_heap_is_mine(heap)) {
        // Case 1: 같은 스레드 (락 없음)
        mi_block_t* block = (mi_block_t*)ptr;
        block->next = page->local_free;
        page->local_free = block;
    } else {
        // Case 2: 다른 스레드 (CAS 루프로 remote list에 게시)
        mi_block_t* block = (mi_block_t*)ptr;
        uintptr_t tfree;

        do {
            tfree = mi_atomic_load_relaxed(&page->thread_freed);
            block->next = (mi_block_t*)(tfree & ~0x3);
        } while (!mi_atomic_cas_weak_release(
            &page->thread_freed,
            tfree,
            (uintptr_t)block
        ));
    }
}
```

CAS는 경쟁이 없으면 첫 시도에 성공할 수 있지만 동시 원격 해제가 몰리면 재시도한다. 이 설계의 핵심은 전역 락 하나로 모든 원격 해제를 직렬화하지 않고 페이지 단위로 경합을 분산하는 데 있다. 다른 할당자도 버전과 설정에 따라 thread cache, transfer cache, 원격 목록을 사용하므로 "다른 할당자는 항상 글로벌 락 세 번"이라고 일반화할 수 없다.

### 포인터에서 페이지 찾기: O(1) 매직

mimalloc이 빠른 또 다른 이유는 포인터로부터 페이지 메타데이터를 O(1)에 찾는 기법이다.

```c
// 세그먼트(기본 4MiB)는 4MiB 경계에 정렬되어 있다
static inline mi_segment_t* mi_ptr_segment(const void* p) {
    return (mi_segment_t*)((uintptr_t)p & ~MI_SEGMENT_MASK);
}

static inline mi_page_t* mi_ptr_page(void* p) {
    mi_segment_t* segment = mi_ptr_segment(p);
    // 세그먼트 내 오프셋으로 페이지 인덱스 계산
    ptrdiff_t diff = (uint8_t*)p - (uint8_t*)segment;
    size_t idx = (size_t)diff >> segment->page_shift;
    return &segment->pages[idx];  // 메타데이터는 세그먼트 헤더에 모여 있다
}
```

페이지(작은 객체용 64KiB 단위)들은 4MiB 정렬된 세그먼트 안에 담기고, 페이지 메타데이터는 세그먼트 헤더의 배열에 모여 있다. 포인터에서 비트 마스킹 한 번으로 세그먼트 헤더를 찾고, 오프셋 시프트로 페이지 인덱스를 얻는다. 이는 다음과 같은 장점이 있다:
- 별도 객체별 해시 테이블 불필요. jemalloc도 arbitrary pointer의 extent metadata를 찾을 때 radix tree 계열 `rtree`를 사용하며 단순한 "extent 트리 선형 검색"은 아니다.
- 캐시 미스 최소화
- 예측 가능한 성능

### 캐시 지역성과 False Sharing 제거

Page-local free list의 가장 큰 이점은 **캐시 지역성**이다.

```cpp
전통적 할당자:
  Thread A malloc:
    [obj1@page1] -> [obj5@page3] -> [obj2@page1] -> [obj9@page5]
  → 페이지 점프, 캐시 미스 많음

mimalloc:
  Thread A malloc:
    [obj1@page1] -> [obj2@page1] -> [obj3@page1] -> [obj4@page1]
  → 같은 페이지, 캐시 히트!
```

또한 페이지 소유권은 서로 다른 스레드가 같은 할당자 메타데이터 캐시 라인을 수정할 가능성을 줄인다.

```text
전통적 할당자:
  [캐시 라인: obj1 | obj2]
  Thread A: obj1 수정 → 캐시 무효화
  Thread B: obj2 수정 → 캐시 무효화
  → 핑퐁 효과

mimalloc의 이상적인 소유 패턴:
  Page 1 (Thread A 전용): [obj1 | obj2]
  Page 2 (Thread B 전용): [obj3 | obj4]
  → 메타데이터와 객체 소유권 경합 감소
```

### 논문 결과를 읽는 법

할당자 논문의 결과는 해당 논문의 버전, 비교 대상 설정, 하드웨어와 benchmark suite에서 성립한다. 단일 `ns/op` 순위로 일반화하면 다음 변수를 잃는다.

| 축 | 반드시 분리할 조건 |
|----|-------------------|
| 객체 크기 | 고정 크기, 혼합 분포, 큰 객체 |
| 수명 | 즉시 free, 프레임 수명, 장기 생존 |
| 소유권 | 같은 스레드 free, cross-thread free |
| 동시성 | thread 수, CPU 수, oversubscription |
| 메모리 | peak RSS, fragmentation, 반환 속도 |
| 지연 | 중앙값, p95/p99, 최악 프레임 |

mimalloc의 free-list sharding은 특히 cross-thread free와 공유 메타데이터 경합을 줄이려는 설계다. 그것이 특정 게임 워크로드에서 우세한지는 동일한 컴파일 옵션과 allocator 설정으로 위 축을 재현해 판단해야 한다.

## 6. 가상 메모리 최적화: Large Page와 Memory Mapping

할당자 알고리즘도 중요하지만, 운영체제의 가상 메모리 기능을 어떻게 활용하느냐도 성능에 큰 영향을 미친다.

### Large Page: TLB 효율의 극대화

#### TLB의 역할

가상 메모리 시스템에서 모든 메모리 접근은 가상 주소를 물리 주소로 변환해야 한다. 이 변환 정보는 **TLB(Translation Lookaside Buffer)**에 캐시된다.

```text
메모리 접근 프로세스:
  1. TLB 검색
     → Hit: 캐시와 병행 가능한 빠른 변환 경로
     → Miss: 여러 단계의 Page Table Walk
  2. 물리 주소로 접근
```

예시적인 TLB reach 계산:
```text
4KB 변환을 N개 보관한다면:
  이론적 reach = N * 4KB

2MB 변환을 M개 보관한다면:
  이론적 reach = M * 2MB
```

`N`, `M`, associativity와 페이지 크기별 공유 방식은 CPU마다 다르다. 실제 reach는 충돌과 코드·데이터 변환 경쟁 때문에 이론값보다 작을 수 있다.

#### Large Page의 효과

1GiB 범위를 한 번 순차 순회한다고 하자.

```text
4KB pages:
  서로 다른 page 수: 1GiB / 4KiB = 262,144

2MB pages:
  서로 다른 page 수: 1GiB / 2MiB = 512
```

이 숫자는 *page 수*이지 TLB miss rate가 아니다. 각 4KiB page에서 8바이트 원소 512개를 연속 접근한다면 cold 순회의 강제 miss 하한은 대략 page당 한 번, 즉 원소 접근의 약 `1/512`이다. 2MiB page에서는 대략 `1/262,144`까지 낮아질 수 있다. 실제 miss 수는 TLB 계층, hardware prefetch와 page-walk cache, 반복 순회, associativity와 다른 mapping의 경쟁에 따라 달라진다. `필요 page 수 - TLB entry 수`를 접근 횟수로 나눈 값은 miss rate가 아니다.

Large Page는 다음과 같은 경우 효과적이다:
- 큰 데이터 구조 (데이터베이스 버퍼 풀, 메모리 캐시)
- 순차 접근 패턴
- 장기간 유지되는 메모리

#### Windows에서 Large Page 사용

```c
// 1. Large Page 크기 확인
SIZE_T largePageSize = GetLargePageMinimum();  // 보통 2MB

// 2. 권한 필요 (SeLockMemoryPrivilege)
// 관리 도구 → 로컬 보안 정책 → "Lock pages in memory"

// 3. 할당
void* ptr = VirtualAlloc(
    NULL,
    100 * 1024 * 1024,  // 100MB (2MB 배수로 자동 라운딩)
    MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES,
    PAGE_READWRITE
);

if (ptr == NULL) {
    // 실패 가능 (권한 없음, 메모리 단편화 등)
    DWORD error = GetLastError();
}
```

제약사항:
- 관리자 권한 필요 (보안 정책 설정)
- 2MB 단위로만 할당
- Swap 불가 (항상 물리 메모리에 상주)
- 메모리 단편화 시 할당 실패 가능

성능 비교는 같은 데이터와 NUMA 배치에서 page 크기만 바꾸고 `dTLB-load-misses`, page-walk cycles, 처리량과 p95 지연을 함께 기록한다. TLB가 병목이 아니면 large page가 처리량을 거의 바꾸지 않을 수 있고, 내부 단편화·메모리 compaction·할당 실패 비용 때문에 오히려 불리할 수도 있다.

#### Linux Transparent Huge Pages

Linux는 더 편리한 방법을 제공한다:

```bash
# 커널이 자동으로 4KB → 2MB 병합
echo always > /sys/kernel/mm/transparent_hugepage/enabled

# 또는 애플리케이션에서 힌트
madvise(ptr, size, MADV_HUGEPAGE);
```

애플리케이션 코드 변경 없이 커널이 자동으로 최적화한다. 다만 다음 주의사항이 있다:
- 메모리 압축(compaction) 오버헤드
- latency spike 가능성
- Redis 등 일부 애플리케이션은 비활성화 권장

### Memory Mapped File: 별도 사용자 버퍼 복사 회피

#### 전통적 파일 I/O의 비효율

```c
// 전통적 방식: 데이터가 2번 복사됨
char buffer[4096];
read(fd, buffer, 4096);    // 커널 버퍼 → 유저 버퍼
process(buffer);
write(fd2, buffer, 4096);  // 유저 버퍼 → 커널 버퍼
```

각 read/write는 다음을 수반한다:
1. 시스템 콜 오버헤드
2. 메모리 복사
3. 커널 진입과 반환

#### Memory Mapped I/O

```c
// 파일 → 가상 메모리 직접 매핑
void* ptr = mmap(NULL, file_size, PROT_READ | PROT_WRITE,
                 MAP_SHARED, fd, 0);

// 메모리처럼 접근
struct Record {
    int id;
    char name[256];
};

Record* records = (Record*)ptr;
records[100].id = 999;  // dirty page가 됨. 영속 시점은 별도 동기화 계약이 필요

munmap(ptr, file_size);
```

장점:
- **별도 사용자 버퍼 복사 회피**: page cache 페이지를 주소 공간에 직접 매핑
- **Lazy Loading**: 접근하는 페이지만 로드 (페이지 폴트 기반)
- **간단한 접근 모델**: 파일을 배열처럼 사용

`MAP_SHARED` 쓰기가 최종 저장장치에 즉시 도달하거나 crash 뒤 보존된다는 보장은 없다. 필요한 범위에 `msync`를 사용하고 파일 metadata 내구성까지 필요하면 `fsync` 계열과 파일시스템의 crash-consistency 계약을 확인해야 한다.

#### Windows 구현

```c
// 1. 파일 열기
HANDLE hFile = CreateFileW(
    L"data.bin",
    GENERIC_READ | GENERIC_WRITE,
    0, NULL, OPEN_ALWAYS,
    FILE_ATTRIBUTE_NORMAL, NULL
);

// 2. 파일 매핑 객체 생성
HANDLE hMapFile = CreateFileMappingW(
    hFile, NULL,
    PAGE_READWRITE,
    0, 1ULL << 30,  // 1GB (파일보다 크면 확장)
    NULL
);

// 3. 가상 주소 공간에 매핑
void* pView = MapViewOfFile(
    hMapFile,
    FILE_MAP_ALL_ACCESS,
    0, 0, 0  // 전체 매핑
);

// 4. 사용
Data* data = (Data*)pView;
data[1000000].value = 42;

// 5. 수정된 page를 파일로 write-back하도록 요청
FlushViewOfFile(data, sizeof(Data) * 1000000);

// 전원 손실까지 고려한 durable commit 계약이 필요하면 파일 핸들도 flush한다.
// FlushViewOfFile만으로 디스크 장치까지의 영속성이 보장되지는 않는다.
FlushFileBuffers(hFile);

// 6. 정리
UnmapViewOfFile(pView);
CloseHandle(hMapFile);
CloseHandle(hFile);
```

#### Copy-on-Write 매핑

Private 매핑은 흥미로운 활용이 가능하다:

```c
void* pPrivate = MapViewOfFile(
    hMapFile,
    FILE_MAP_COPY,  // COW 매핑
    0, 0, 0
);

// 읽기: 파일 내용 직접 접근
int x = ((int*)pPrivate)[0];

// 쓰기: 페이지 복사본 생성, 파일은 그대로
((int*)pPrivate)[0] = 999;
```

사용 사례:
- **프로세스 Fork**: 부모 프로세스 메모리를 자식에게 COW로 공유
- **실행 파일 로딩**: DLL/EXE를 여러 프로세스가 공유, 수정 시 복사
- **스냅샷**: 원본 파일을 건드리지 않고 수정 버전 생성

실제 데이터베이스 활용:
- **LMDB**: 전체 데이터베이스를 mmap, 매우 빠른 읽기
- **SQLite**: `PRAGMA mmap_size`로 mmap I/O 옵션 제공
- **MongoDB**: 구형 MMAPv1 스토리지 엔진이 mmap 기반이었다 (현재 기본인 WiredTiger는 자체 캐시 사용)

### Commit/Decommit 패턴: 동적 메모리 관리

#### 동적 배열 구현

```c
class DynamicArray {
private:
    void* base_;
    size_t reserved_;   // 예약된 크기 (주소 공간)
    size_t committed_;  // 커밋된 크기 (물리 메모리)
    size_t used_;       // 실제 사용 크기

public:
    DynamicArray(size_t max_size) {
        // 1GB 주소 공간 예약 (물리 메모리 0)
        base_ = VirtualAlloc(
            NULL, max_size,
            MEM_RESERVE,
            PAGE_NOACCESS
        );
        reserved_ = max_size;
        committed_ = 0;
        used_ = 0;
    }

    void Grow(size_t new_size) {
        if (new_size > committed_) {
            // 64KB 단위로 커밋 확장
            size_t new_commit = RoundUp(new_size, 64 * 1024);

            VirtualAlloc(
                (char*)base_ + committed_,
                new_commit - committed_,
                MEM_COMMIT,
                PAGE_READWRITE
            );

            committed_ = new_commit;
        }
        used_ = new_size;
    }

    void Shrink() {
        // 사용률 25% 이하면 절반 decommit
        if (used_ < committed_ / 4) {
            size_t new_commit = committed_ / 2;

            VirtualFree(
                (char*)base_ + new_commit,
                committed_ - new_commit,
                MEM_DECOMMIT
            );

            committed_ = new_commit;
        }
    }

    ~DynamicArray() {
        VirtualFree(base_, 0, MEM_RELEASE);
    }
};
```

#### Reserve granularity와 commit 단위

Windows에서 `VirtualAlloc`의 *예약 시작 주소*는 allocation granularity에 맞춰 내려가며, 전형적인 x86-64 Windows에서 이 값은 64KiB다. 반면 commit과 decommit의 실질 단위는 `dwPageSize`이며 보통 4KiB다. 두 값을 `GetSystemInfo`로 따로 읽어야 한다.

```c
SYSTEM_INFO si;
GetSystemInfo(&si);
size_t reserveAlignment = si.dwAllocationGranularity;
size_t pageSize = si.dwPageSize;
```

애플리케이션은 반드시 64KiB씩 commit해야 하는 것이 아니다. 성장 chunk는 시스템 호출 빈도와 미사용 committed page의 균형으로 정한다. 작은 chunk는:
- 시스템 콜 횟수 증가
- commit bookkeeping과 page fault가 더 자주 발생할 수 있음

큰 chunk는:
- 아직 쓰지 않을 주소까지 commit charge와 page-table 비용을 앞당길 수 있음

64KiB는 가능한 정책 예일 뿐 보편적인 최적값이 아니다. reserve 정렬값과 우연히 같더라도 근거는 워크로드의 성장 단위와 측정이어야 한다.

#### Decommit 전략

```c
// Immediate Decommit (메모리 압박 시)
void FreeBlock(void* ptr, size_t size) {
    if (IsMemoryPressure()) {
        VirtualFree(ptr, size, MEM_DECOMMIT);
    }
}

// Delayed Decommit (성능 우선)
class DelayedDecommit {
private:
    struct Entry {
        void* ptr;
        size_t size;
        std::chrono::time_point<steady_clock> time;
    };

    std::vector<Entry> pending_;

public:
    void Add(void* ptr, size_t size) {
        pending_.push_back({ptr, size, now()});

        // 10초 이상 된 것들만 decommit
        auto cutoff = now() - std::chrono::seconds(10);

        for (auto it = pending_.begin(); it != pending_.end();) {
            if (it->time < cutoff) {
                VirtualFree(it->ptr, it->size, MEM_DECOMMIT);
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }
};
```

비교할 때는 reserve 호출 시간만 보지 말고 최초 touch, zero-fill fault, decommit 뒤 재접근, peak working set을 따로 측정한다. `MEM_COMMIT`이 모든 물리 페이지를 즉시 fault-in했다는 뜻은 아니며, 커밋된 demand-zero 페이지도 최초 접근 비용을 가질 수 있다. 반대로 decommit하지 않으면 재사용 지연은 줄 수 있지만 resident memory 압력과 다른 시스템의 page reclaim 비용을 키울 수 있다.

권장 전략:
1. **예측 가능한 크기**: Reserve/Commit, decommit 최소화
2. **변동 심한 크기**: Delayed decommit (10초 타임아웃)
3. **메모리 제약**: Immediate decommit

## 7. 할당자 선택 가이드

### 특성 비교표

| 평가 축 | 확인할 질문 |
|---------|-------------|
| 처리량 | 실제 크기·수명 분포에서 초당 몇 회를 처리하는가 |
| tail latency | refill, page fault, purge가 p99 프레임에 어떻게 나타나는가 |
| 확장성 | thread 수와 CPU 수가 늘 때 처리량이 어디서 포화되는가 |
| 원격 해제 | 생산자와 소비자가 다른 경우 어떤 메타데이터가 경합하는가 |
| 메모리 증폭 | requested bytes 대비 active, resident, retained bytes는 얼마인가 |
| 반환 정책 | dirty/unused 페이지를 OS에 언제 어떤 방식으로 돌려주는가 |
| 관측성 | heap profile, size-class 통계, 샘플링을 제공하는가 |
| 배포 조건 | 대상 OS·libc, 정적/동적 링크, 라이선스와 지원 정책은 맞는가 |

### 사용 권장

**ptmalloc2 (glibc 기본)**
```text
적합한 경우:
  - 범용 애플리케이션
  - 이식성 중요
  - 특별한 최적화 불필요

주의사항:
  - 기본 구성으로 운영 단순화
  - 대체 할당자의 이득이 측정되지 않은 경우
```

**jemalloc**
```text
적합한 경우:
  - 장기 실행 서버 (Redis, Cassandra)
  - 메모리 단편화 민감
  - RSS 제어 중요
  - 실제 배포 환경에서 jemalloc의 decay·통계·단편화 특성이 유리하다고 측정됨

사용 예:
  export LD_PRELOAD=/usr/lib/libjemalloc.so
  ./my_server
```

**tcmalloc**
```text
적합한 경우:
  - 고성능 웹 서버
  - 활성 CPU 수를 기준으로 front-end cache를 운용하려는 경우
  - 큰 페이지와 central cache 통계를 활용할 수 있는 경우

사용 예:
  export LD_PRELOAD=/usr/lib/libtcmalloc.so
  # 실제 옵션은 사용하는 TCMalloc 배포판과 버전 문서에서 확인한다.
```

**mimalloc**
```text
적합한 경우:
  - 최신 멀티코어 시스템
  - page-local sharding을 평가하려는 경우
  - 크로스 스레드 해제 빈번
  - 게임 엔진, 실시간 시스템

사용 예:
  // CMake
  find_package(mimalloc REQUIRED)
  target_link_libraries(myapp mimalloc)
```

### 실전 벤치마킹

실제 워크로드에서 테스트하는 것이 중요하다:

```c
// 간단한 벤치마크 프레임워크
#include <chrono>

void benchmark_allocator(int num_threads, int iterations) {
    auto start = std::chrono::high_resolution_clock::now();

    #pragma omp parallel for num_threads(num_threads)
    for (int t = 0; t < num_threads; t++) {
        for (int i = 0; i < iterations; i++) {
            void* ptr = malloc(64);
            // ... 사용 ...
            free(ptr);
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    std::cout << "Total: " << duration.count() << " ns\n";
    std::cout << "Per op: " << duration.count() / (num_threads * iterations) << " ns\n";
}
```

각 할당자로 컴파일해서 비교:

```bash
# ptmalloc2 (기본)
g++ bench.cpp -fopenmp -o bench_glibc
./bench_glibc

# jemalloc
g++ bench.cpp -fopenmp -ljemalloc -o bench_jemalloc
./bench_jemalloc

# tcmalloc
g++ bench.cpp -fopenmp -ltcmalloc -o bench_tcmalloc
./bench_tcmalloc

# mimalloc
g++ bench.cpp -fopenmp -lmimalloc -o bench_mimalloc
./bench_mimalloc
```


---

## 결론

메모리 할당은 단순해 보이지만 내부는 수십 년의 연구와 최적화가 집약된 정교한 시스템이다. VirtualAlloc, HeapAlloc, malloc은 서로 다른 추상화 수준을 제공한다. ptmalloc은 arena, bin, tcache를 조합하고, jemalloc은 size class, extent, decay 정책을 제공한다. tcmalloc은 front/middle/back-end와 per-CPU cache를 사용하며, mimalloc은 page-local free-list와 원격 해제 sharding을 강조한다.

현대 할당자들의 공통 전략은 네 가지다. Thread-local 캐싱으로 락을 피한다. 크기 클래스로 단편화를 최소화한다. 배치 전송으로 경합을 줄인다. Large Page로 TLB 효율을 끌어올린다.

선택은 제품 이름별 고정 순위가 아니라 워크로드와 운영 정책의 트레이드오프다. 동일한 trace에서 처리량, tail latency, peak RSS, 원격 해제와 OS 반환 정책을 함께 측정해야 한다.

## 더 읽을거리

### 공식 문서

1. **Microsoft Learn - VirtualAlloc**
 https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc
 Windows 가상 메모리 API 공식 문서

2. **glibc malloc 소스 코드**
 https://sourceware.org/git/?p=glibc.git;a=blob;f=malloc/malloc.c
 ptmalloc2 구현 전체 (14,000+ 라인)

3. **glibc Malloc Internals Wiki**
 https://sourceware.org/glibc/wiki/MallocInternals
 Arena, bin, chunk 구조 상세 설명

### 학술 논문

4. **Jason Evans (2006). "A Scalable Concurrent malloc(3) Implementation for FreeBSD"**
 BSDCan 2006
 https://people.freebsd.org/~jasone/jemalloc/bsdcan2006/jemalloc.pdf
 jemalloc 초기 설계 논문

5. **Emery D. Berger, et al. (2000). "Hoard: A Scalable Memory Allocator for Multithreaded Applications"**
 ASPLOS 2000
 False sharing, 캐시 지역성 문제 분석

6. **Google (2021). "Beyond malloc efficiency to fleet efficiency: a hugepage-aware memory allocator"**
 OSDI 2021
 https://research.google/pubs/pub50370/
 TCMalloc의 Temeraire hugepage-aware page allocator

7. **Daan Leijen, Benjamin Zorn, Leonardo de Moura (2019). "Mimalloc: Free List Sharding in Action"**
 MSR-TR-2019-18
 https://www.microsoft.com/en-us/research/publication/mimalloc-free-list-sharding-in-action/
 mimalloc 설계 철학과 벤치마크

### 온라인 리소스

8. **tcmalloc Design Documentation**
 https://google.github.io/tcmalloc/design.html
 Front/Middle/Back-end 아키텍처 상세

9. **TCMalloc Per-CPU Caches Using Linux RSEQ**
 https://google.github.io/tcmalloc/rseq.html
 restartable sequences 기반 Per-CPU fast path

10. **jemalloc Documentation**
 https://jemalloc.net/
 크기 클래스, extent, slab 설명

11. **mimalloc GitHub Repository**
 https://github.com/microsoft/mimalloc
 벤치마크 결과, API 문서, 최신 업데이트

### 추가 읽을거리

12. **Ulrich Drepper (2007). "What Every Programmer Should Know About Memory"**
 https://people.freebsd.org/~lstewart/articles/cpumemory.pdf
 캐시, TLB, 가상 메모리 심층 분석 (필독!)

13. **Intel (2023). "Intel 64 and IA-32 Architectures Optimization Reference Manual"**
 https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
 Large Page, 캐시 최적화 기법

14. **Linux Kernel Documentation - Transparent Huge Pages**
 https://www.kernel.org/doc/html/latest/admin-guide/mm/transhuge.html
 THP 동작 원리와 설정

15. **Windows Internals, 7th Edition (Part 1)**
 Mark Russinovich, et al.
 Chapter 5: Memory Management
 VirtualAlloc, Heap Manager 내부 구조

**키워드**: 메모리 할당자, malloc, VirtualAlloc, ptmalloc2, jemalloc, tcmalloc, mimalloc, 시스템 프로그래밍, 멀티스레딩, TLB 최적화
