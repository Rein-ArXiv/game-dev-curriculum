## 들어가며

메모리 할당은 모든 프로그램의 기초다. `malloc`과 `free`는 매일 호출되지만, 그 내부에서 무엇이 일어나는지는 잘 알려져 있지 않다. `free`는 어떻게 해제할 크기를 알아내는가? 왜 `malloc`은 64비트 시스템에서 16바이트 정렬을 보장하는가? 디버그 빌드의 `0xCD`, `0xDD` 패턴은 무엇을 위한 것인가?

이 글은 그 질문들을 **할당자가 사용자에게 무엇을 약속하는가**라는 하나의 축으로 푼다. 블록 헤더와 boundary tag가 크기를 복구하는 경로, 정렬 계약과 그 계약을 넘어서는 정렬 할당의 구현, 할당 단위가 캐시 라인 공유를 결정하는 방식, 디버그 매직 넘버, 할당 지점을 기록하는 매크로, 그리고 메타데이터를 들고 다니는 자료구조의 일반적 패턴을 다룬다.


---

## 목차
1. [malloc/free의 내부 동작](#1-mallocfree의-내부-동작)
2. [메모리 정렬](#2-메모리-정렬-cpu가-정렬을-요구하는-이유)
3. [할당 단위와 캐시 라인 공유](#3-할당-단위와-캐시-라인-공유)
4. [디버그 매직 넘버](#4-디버그-매직-넘버-버그를-보이게-만들기)
5. [메모리 누수 추적](#5-메모리-누수-추적-__file__과-__line__-매크로)
6. [Arena와 String Builder의 유사성](#6-arena와-string-builder의-유사성)


---

## 1. malloc/free의 내부 동작

C 표준 라이브러리의 메모리 할당 함수를 보면 흥미로운 비대칭성이 있다:

```c
void* malloc(size_t size);  // 크기를 명시적으로 전달
void free(void* ptr);        // 크기 정보가 없다!
```

`free(ptr)`는 어떻게 해제할 메모리 블록의 크기를 알 수 있을까? 답은 **메타데이터**에 있다.

### Block Header 구조

대부분의 메모리 할당자는 사용자에게 반환하는 포인터 **직전**에 블록 헤더(block header)를 숨겨둔다. glibc의 `malloc` 구현을 간략화하면 다음과 같다:

```c
struct malloc_chunk {
    size_t      prev_size;  // 이전 청크가 free 상태일 때만 사용
    size_t      size;       // 현재 청크 크기 + 플래그 비트
    // 사용자 데이터가 여기서 시작
};
```

`malloc(100)`을 호출하면 실제로는:

```text
[Header: 16 bytes][User Data: 100+ bytes]
                  ^
                  malloc이 반환하는 포인터
```

이 구조 덕분에 `free(ptr)`는 간단한 포인터 산술로 헤더에 접근할 수 있다:

```c
void free(void* ptr) {
    if (!ptr) return;

    // 포인터를 헤더 위치로 역산
    struct malloc_chunk* chunk = (struct malloc_chunk*)ptr - 1;
    size_t size = chunk->size & ~FLAGS_MASK;  // 플래그 비트 제거

    // 메모리 해제 로직...
}
```

`(ptr - 1)`의 의미는 무엇인가? C에서 포인터 산술은 **타입의 크기만큼** 이동한다. `struct malloc_chunk*`로 캐스팅된 상태에서 `-1`은 정확히 한 구조체 크기(16바이트) 뒤로 이동한다.

### Boundary Tags: Knuth의 통찰

Donald Knuth는 "The Art of Computer Programming" 1권(1968) 2.5절 Dynamic Storage Allocation에서 **boundary tag** 기법을 제시했다. 블록의 앞뒤에 크기 정보를 중복 저장하여 인접 블록 병합(coalescing)을 $O(1)$에 수행할 수 있게 만든 것이다:

```text
[Header: size=100][User Data][Footer: size=100]
```

현대의 glibc malloc은 이를 최적화하여, **free 블록**만 footer를 유지한다. 할당된 블록은 헤더의 플래그 비트로 상태를 표시한다:

```c
#define PREV_INUSE  0x1  // 이전 청크가 사용 중
#define IS_MMAPPED  0x2  // mmap으로 할당됨
#define NON_MAIN_ARENA 0x4
```

크기 필드의 하위 3비트를 플래그로 사용할 수 있는 이유는? 메모리 정렬 때문이다.

## 2. 메모리 정렬: CPU가 정렬을 요구하는 이유

### 하드웨어 관점: 접근 경계의 제약

CPU의 load/store 명령은 1, 2, 4, 8바이트처럼 여러 크기를 다루지만, 캐시와 메모리 계층은 그보다 큰 고정 단위로 데이터를 옮긴다. 어떤 접근이 자연 정렬 경계, 캐시 라인, 페이지 경계를 가로지르면 한 번의 논리적 load가 여러 내부 접근과 재조합을 요구할 수 있다. 설명을 단순화해 8바이트 경계를 기준으로 보면 다음과 같다.

```text
Address:  0x1000 0x1001 0x1002 ... 0x1007
          [---- 8-byte word ----]
```

만약 4바이트 정수가 주소 0x1005에 저장되어 8바이트 경계를 가로지른다면?

```text
0x1000: [XX XX XX XX XX][AA BB CC]
0x1008: [DD][XX XX XX XX XX XX XX]
                         ↑
             경계를 가로지르는 int
```

CPU는 두 번의 메모리 읽기를 수행해야 한다:
1. 0x1000-0x1007 읽기 → AA, BB, CC 획득
2. 0x1008-0x100F 읽기 → DD 획득
3. 비트 시프트와 마스킹으로 재조합

x86-64의 일반 load/store와 현대 AArch64의 Normal memory 접근은 많은 비정렬 접근을 하드웨어에서 처리한다. 같은 캐시 라인 안의 접근은 추가 비용이 거의 없을 수도 있지만, 캐시 라인이나 페이지 경계를 넘으면 비용이 커질 수 있다. 반면 AArch64의 Device memory, 일부 원자적·배타적 명령, 정렬 검사가 활성화된 실행 환경처럼 비정렬 접근이 예외가 되는 경우도 있다. 따라서 "ARM은 비정렬 접근을 허용하지 않는다"가 아니라 **ISA, 명령, 메모리 속성, 운영체제 설정의 계약을 확인해야 한다**가 정확하다.

### 플랫폼별 기본 정렬

glibc malloc의 기본 정렬은 ABI와 `MALLOC_ALIGNMENT`에 의해 결정된다. 일반적인 glibc 빌드에서는 다음과 같이 동작한다:

- **32비트 시스템**: 보통 8바이트 정렬
- **64비트 시스템**: 보통 16바이트 정렬

청크 크기는 이 정렬 단위의 배수로 저장된다. 그래서 하위 비트 일부가 항상 0이고, 그 비트를 플래그로 재사용할 수 있다. glibc가 실제로 정의하는 플래그는 `PREV_INUSE`, `IS_MMAPPED`, `NON_MAIN_ARENA` 세 개라 하위 3비트를 사용한다. 64비트의 16바이트 정렬에서는 하위 4비트가 0이지만, 네 번째 비트까지 의미 있는 플래그로 쓴다는 뜻은 아니다.

Microsoft의 CRT debug heap은 사용자 블록 앞뒤에 디버그 헤더와 가드 바이트를 붙인다. 반환 포인터의 정렬은 CRT와 플랫폼의 기본 할당 정렬 정책을 따르며, 디버그 헤더 구조는 그 앞에 추가 메타데이터가 붙는다는 점을 보여준다:

```c
#define nNoMansLandSize 4

typedef struct _CrtMemBlockHeader {
    struct _CrtMemBlockHeader* pBlockHeaderNext;
    struct _CrtMemBlockHeader* pBlockHeaderPrev;
    char* szFileName;
    int nLine;
    size_t nDataSize;
    int nBlockUse;
    long lRequest;
    unsigned char gap[nNoMansLandSize];  // 0xFD 패턴
    // 사용자 데이터
    // unsigned char gap[nNoMansLandSize];  // 후행 가드
} _CrtMemBlockHeader;
```

### SIMD 정렬 요구사항

SIMD(Single Instruction Multiple Data) 명령어 중 *aligned load/store* 변종은 더 엄격한 정렬을 요구한다.

- **SSE** (`movaps`/`movapd`): 16바이트 정렬 필수
- **AVX** (`vmovaps`/`vmovapd`): 32바이트 정렬 필수
- **AVX-512**: 64바이트 정렬 필수

정렬되지 않은 데이터에 `movaps`를 사용하면 `#GP(0)` 일반 보호 fault가 발생한다(현대 OS에서는 SIGSEGV로 사용자에게 노출된다). 같은 SSE/AVX 명령군에는 *unaligned* 변종(`movups`/`vmovups`)이 별도로 있어 정렬되지 않은 주소도 동작한다. unaligned 명령은 과거에는 aligned 버전보다 느렸지만, Nehalem 이후의 인텔 CPU와 그에 대응하는 AMD CPU에서는 *주소가 실제로 정렬된 경우* 에 한해 두 명령의 비용이 거의 같다. 정렬이 깨지면 unaligned가 손해를 본다.

`malloc` 정렬의 본질은 SIMD 한 가지가 아니다. C 표준의 동적 할당은 fundamental alignment를 요구하는 객체에 적합해야 하고, 구현에서는 보통 `max_align_t`와 ABI를 기준으로 정한다. C++의 기본 `operator new`도 구현의 기본 new alignment를 만족하며, C++17의 over-aligned 타입은 정렬 인자를 받는 `operator new` 오버로드를 사용할 수 있다. 일반적인 x86-64 ABI의 16바이트 기본 정렬이 SSE 타입 요구와 일치할 수는 있지만, SIMD가 표준 할당 정렬의 보편적 원인이나 모든 확장 타입에 대한 이식 가능한 보장은 아니다.

### 워드 크기 vs malloc 정렬: 혼동하기 쉬운 개념

두 개념은 다르다:

| 개념 | 의미 | 64비트에서 |
|------|------|-----------|
| 워드 크기 | CPU 기본 연산 단위 | 8바이트 |
| malloc 정렬 | 반환 주소가 N의 배수임을 보장 | 16바이트 |

CPU는 일반 `MOV` 명령으로 8바이트 단위 접근을 자주 수행하지만, malloc이 64비트 환경에서 보통 16바이트 정렬을 제공하는 이유는 특정 SIMD 타입 하나 때문만은 아니다. C/C++ 런타임은 표준 타입과 ABI가 요구하는 기본 정렬을 만족해야 한다:

```c
__m128 simd_vec;      // SSE: 16바이트 정렬 필수
long double ld;       // 일부 플랫폼(x86-64 SysV)에서 16바이트
```

malloc은 "기본 정렬을 요구하는 타입을 안전하게 저장 가능"해야 하므로, 플랫폼 ABI의 기본 정렬 요구사항을 만족시킨다. 16바이트 정렬이면 8바이트 정렬도 자동으로 만족한다(16은 8의 배수). 더 큰 정렬이 필요한 AVX 32바이트, AVX-512 64바이트 데이터는 일반 `malloc`이 아니라 `aligned_alloc`, `posix_memalign`, `_aligned_malloc` 같은 명시적 정렬 할당을 써야 한다.

```text
16바이트 정렬: 주소가 0x10의 배수
  → 0x1000, 0x1010, 0x1020 ... ✓
  → 0x1008 ... ✗

8바이트 정렬: 주소가 0x8의 배수
  → 0x1000, 0x1008, 0x1010 ... ✓
```

### 커스텀 정렬 할당 구현: `_aligned_malloc`의 원리

만약 커스텀 할당자가 8바이트 정렬만 보장한다면 SIMD를 어떻게 쓸 수 있을까? 두 가지 방법이 있다:

1. **Unaligned 명령어 사용**: `movups`는 정렬 상관없이 동작한다. 현대 x86-64에서 성능 패널티가 거의 없다.
2. **정렬 할당 함수 별도 제공**: 표준 라이브러리도 이 방식을 쓴다.

```c
// POSIX
posix_memalign(&ptr, 16, size);
aligned_alloc(16, size);        // C11

// Windows
_aligned_malloc(size, 16);
_aligned_free(ptr);             // 반드시 짝으로!
```

Windows에서 `_aligned_malloc`으로 할당한 메모리는 반드시 `_aligned_free`로 해제해야 한다. 일반 `free`를 쓰면 힙 손상이 발생한다. 왜 그럴까?

**문제: 정렬된 주소와 실제 할당 주소가 다르다**

```c
void* raw = malloc(...);         // 실제 할당 주소: 0x1004
void* aligned = ...;             // 정렬된 주소: 0x1010

return aligned;  // 사용자는 0x1010만 안다
```

나중에 `free(0x1010)`을 호출하면? 실제로 해제해야 할 건 `0x1004`인데, 그 정보가 없으면 해제할 수 없다.

**해법: 원본 포인터를 정렬된 주소 직전에 저장**

```text
raw = 0x1004 (malloc 반환)

0x1004  [------------ 할당된 영역 ------------]
        [여유][원본 ptr][   사용자 데이터    ]
              ↓
0x1008  [0x1004]        ← raw 포인터 저장 (8바이트)
0x1010                  [데이터 시작] ← 16바이트 정렬됨
                         ↑
                    사용자에게 반환
```

"여유" 공간(0x1004~0x1007)은 정렬을 맞추다 남은 공간이다. 포인터는 `aligned - 8` 위치에 **고정**되어야 `free` 시 찾을 수 있으므로, 남는 공간이 자연스럽게 앞쪽에 생긴다.

**구현**

```c
void* my_aligned_malloc(size_t size, size_t align) {
    // 1. 넉넉하게 할당
    //    - size: 실제 데이터
    //    - align - 1: 정렬 맞추기 위한 여유분 (최악의 경우)
    //    - sizeof(void*): 원본 포인터 저장 공간
    void* raw = malloc(size + align - 1 + sizeof(void*));
    if (!raw) return NULL;

    // 2. 정렬된 주소 계산
    //    - raw + sizeof(void*): 포인터 저장 공간 확보 후
    //    - + align - 1, & ~(align - 1): 올림하여 align 배수로
    uintptr_t aligned = ((uintptr_t)raw + sizeof(void*) + align - 1)
                        & ~(align - 1);

    // 3. aligned 직전에 원본 포인터 저장
    ((void**)aligned)[-1] = raw;

    return (void*)aligned;
}

void my_aligned_free(void* aligned) {
    if (!aligned) return;
    void* raw = ((void**)aligned)[-1];  // 저장해둔 원본 꺼냄
    free(raw);
}
```

**비트 마스킹으로 정렬하기**

```text
align = 16 = 0b00010000
align - 1  = 0b00001111
~(align-1) = 0b11110000

주소 & ~(align-1) → 하위 4비트를 0으로 → 16의 배수로 내림
```

예를 들어 `raw = 0x1004`, `align = 16`일 때:

```text
(0x1004 + 8 + 15) & ~15
= 0x101B & 0xFFF0
= 0x1010  ← 16바이트 정렬된 주소
```



## 3. 할당 단위와 캐시 라인 공유

정렬 계약은 한 객체 안에서 끝나지 않는다. 할당자가 정하는 **할당 단위**가 서로 다른 객체를 같은 캐시 라인에 밀어 넣기 때문이다. 캐시와 메모리 사이의 전송 단위이자 일관성 유지 단위가 캐시 라인이고, 현대 x86-64에서 데이터 라인은 보통 64바이트다. 두 스레드가 한 라인의 서로 다른 바이트에 각각 쓰면 라인 소유권이 코어 사이를 왕복하며 처리량이 떨어진다. 두 스레드가 진짜로 같은 값을 공유하지 않는데도 비용을 내므로 false sharing이라 부른다.

이 글의 질문은 그 다음이다. **할당자는 두 객체가 같은 라인에 들어갈지를 어떻게 결정하는가.**

### 요청 크기가 아니라 할당 간격이 라인 공유를 정한다

두 개의 8바이트 카운터를 각각 `malloc(8)`로 잡는다고 하자. 사용자가 요청한 크기는 8바이트지만 실제 주소 간격은 그것이 아니다.

```c
long* c1 = malloc(sizeof(long));   // 스레드 1이 갱신
long* c2 = malloc(sizeof(long));   // 스레드 2가 갱신

printf("%td\n", (char*)c2 - (char*)c1);        // 간격
printf("%d\n", (uintptr_t)c1 / 64 == (uintptr_t)c2 / 64);  // 같은 라인인가
```

간격을 정하는 것은 두 가지다.

| 할당자 구조 | 한 객체가 차지하는 실제 간격 | 64바이트 라인에 들어가는 개수 |
|---|---|---|
| 청크 헤더 방식(glibc 계열) | 요청 크기 + 헤더 + 정렬 패딩. 64비트에서 최소 청크가 32바이트 | 2 |
| size class 방식(span을 헤더 없이 분할) | 요청 크기를 올림한 클래스 크기. 8바이트 요청은 16바이트 클래스 | 4 |

어느 쪽이든 간격이 라인보다 작으므로, **연속해서 할당한 작은 객체가 같은 라인에 놓이는 것이 기본값이다.** 두 카운터를 서로 다른 스레드에 나눠 주는 순간 그 기본값이 그대로 비용이 된다. 할당자는 "누가 이 객체를 만질 것인가"를 모르기 때문에 스스로 이 배치를 피할 수 없다.

### 계약을 라인 단위로 올리는 두 지점

첫째, 호출자가 요청 자체를 라인 단위로 만든다. 정렬만 올리면 부족하다. 크기도 함께 올려야 뒤에 오는 객체가 남은 공간을 채우지 못한다.

```c
void *c1, *c2;

// 정렬만 올린 경우: c1은 라인 시작에 놓이지만
// 그 뒤 56바이트가 비어 있어 다음 할당이 같은 라인에 들어올 수 있다
posix_memalign(&c1, 64, sizeof(long));

// 크기까지 올린 경우: 이 할당이 라인 하나를 통째로 점유한다
posix_memalign(&c2, 64, 64);
```

C11의 `aligned_alloc`은 크기가 정렬의 배수일 것을 요구하므로 위쪽 형태를 아예 표현할 수 없다. 제약이 우연히 올바른 사용을 강제하는 셈이다. C++에서는 타입 선언으로 같은 결과를 얻는다. `alignas(64)`를 붙인 타입은 크기도 정렬의 배수로 올림되므로 배열 원소가 각각 다른 라인에 놓인다.

```cpp
struct alignas(64) ThreadCounter {
    long value;
};
static_assert(sizeof(ThreadCounter) == 64);   // 정렬이 크기까지 끌어올린다

ThreadCounter counters[8];                     // 원소마다 별도 라인
```

경계값을 상수로 박는 대신 `std::hardware_destructive_interference_size`를 쓸 수도 있지만, 그 값은 구현이 제공하는 기준값이라 대상 플랫폼에서 확인이 필요하다. 할당 크기 관점에서 중요한 것은 **어떤 값을 쓰든 그 값이 곧 객체 하나당 실제로 소비되는 바이트가 된다**는 점이다.

둘째, 할당자 쪽에서 size class를 라인 크기의 배수로 잡거나 스레드별로 다른 span에서 잘라 준다. 스레드마다 다른 span을 쓰면 서로 다른 스레드의 객체가 같은 라인에 놓일 확률 자체가 사라진다. 이것이 스레드 로컬 캐시를 가진 할당자가 락 경합만이 아니라 라인 경합에서도 이득을 보는 이유다.

### 대가를 계산하고 적용한다

라인 단위 격리는 공짜가 아니다. 8바이트 카운터를 64바이트로 올리면 데이터가 8배로 늘고, 그만큼 캐시 용량과 프리페치 효율을 잃는다.

| 배치 | 카운터 8개의 크기 | 점유 라인 수 |
|---|---:|---:|
| 격리 없음 | 64바이트 | 1 |
| 라인 단위 격리 | 512바이트 | 8 |

그러므로 격리는 **서로 다른 스레드가 실제로 쓰는 객체에만** 적용한다. 한 스레드만 만지는 배열을 라인 단위로 벌리면 순회 비용만 늘어난다. 판단이 서지 않으면 할당 결과의 주소를 라인 번호로 찍어 두 객체가 실제로 같은 라인에 떨어졌는지부터 확인한다. 배치가 의도대로 나왔는지는 이 한 줄로 즉시 판별되고, 그 배치가 성능에 얼마나 나타나는지는 별개의 측정 문제다.

## 4. 디버그 매직 넘버: 버그를 보이게 만들기

Microsoft Visual C++ Runtime은 디버그 빌드에서 특정 패턴으로 메모리를 초기화한다:

### MSVC Debug Heap 패턴

| 패턴 | 의미 | 16진수 값 |
|------|------|-----------|
| Clean Memory | 새로 할당된 힙 메모리 (CRT debug heap) | `0xCD` |
| Dead Memory | 해제된 힙 메모리 (CRT debug heap) | `0xDD` |
| No Man's Land | 버퍼 오버플로우 감지용 가드 | `0xFD` |
| 초기화 안 된 스택 | `/RTC1` 옵션이 채우는 스택 메모리 | `0xCC` |
| Bad Food | 디버그 `HeapAlloc`이 초기화 안 된 힙에 채움 | `0xBAADF00D` |
| Feee Feee | 디버그 `HeapFree`가 해제된 힙에 채움 | `0xFEEEFEEE` |

`0xCD`/`0xDD`/`0xFD`는 CRT 디버그 힙(`_malloc_dbg`)의 패턴이고, `0xCC`는 컴파일러의 런타임 검사(`/RTC1`)가 스택에 채우는 값이다. `0xBAADF00D`/`0xFEEEFEEE`는 그 아래 계층인 Windows NT 디버그 힙의 패턴이다.

이 패턴들은 단순히 0으로 초기화하는 것보다 훨씬 강력하다.

### 왜 0x00이 아닌가?

1. **초기화 누락과 잘못된 제어 흐름 노출**: `0xCCCCCCCC` 같은 눈에 띄는 값은 초기화되지 않은 포인터나 함수 포인터 사용을 디버거에서 쉽게 식별하게 한다. 이 값이 모든 플랫폼에서 반드시 즉시 fault를 낸다는 보장은 없다.
2. **초기화 안 된 변수 발견**: `if (value == 0xCDCDCDCD)` 같은 조건이 우연히 참이 될 확률이 매우 낮다.
3. **버퍼 오버런 감지**: 메모리 블록 앞뒤의 `0xFD` 패턴이 변경되었는지 검사한다.

Microsoft Learn의 [CRT Debug Heap 문서](https://learn.microsoft.com/en-us/visualstudio/debugger/crt-debug-heap-details)는 새로 할당된 객체를 `0xCD`(Clean memory)로, 해제된 객체를 `0xDD`(Dead memory)로 채운다고 명시한다. 해제된 메모리를 읽는 코드는 `0xDDDDDDDD` 같은 값을 보게 되어 use-after-free가 즉시 드러난다.

실제 구현 예시:

```c
void* debug_malloc(size_t size) {
    size_t total = sizeof(Header) + size + 2 * NO_MANS_LAND;
    void* ptr = HeapAlloc(GetProcessHeap(), 0, total);
    if (!ptr) return NULL;

    Header* header = (Header*)ptr;
    header->size = size;
    header->magic = 0xABCD;

    // No man's land (전방 가드)
    memset(header + 1, 0xFD, NO_MANS_LAND);

    // 사용자 영역 (초기화 안 됨 표시)
    void* user = (char*)(header + 1) + NO_MANS_LAND;
    memset(user, 0xCD, size);

    // No man's land (후방 가드)
    memset((char*)user + size, 0xFD, NO_MANS_LAND);

    return user;
}
```

### 0xCC의 특별한 의미

x86 아키텍처에서 `0xCC`는 **INT 3** 명령어(디버거 breakpoint)다. 그래서 제어 흐름이 `0xCC`로 채워진 실행 가능한 영역으로 흘러들어가면 CPU가 그 바이트를 명령어로 인출해 즉시 디버거 트랩을 낸다. 반면 함수 포인터의 값 자체가 `0xCCCCCCCC`인 채로 호출되면 그 주소는 매핑되어 있지 않으므로 트랩이 아니라 접근 위반으로 드러난다.

## 5. 메모리 누수 추적: `__FILE__`과 `__LINE__` 매크로

### 왜 매크로인가?

메모리 누수를 추적하려면 **할당 위치**를 기록해야 한다. 함수로는 불가능하다:

```c
void* tracked_malloc(size_t size, const char* file, int line) {
    printf("Allocation at %s:%d\n", file, line);
    return malloc(size);
}

// 사용
void* p = tracked_malloc(100, __FILE__, __LINE__);  // OK, 하지만 번거로움
```

매크로를 사용하면 **호출 지점**의 정보를 자동으로 삽입할 수 있다:

```c
#define MALLOC(size) tracked_malloc(size, __FILE__, __LINE__)

void* p = MALLOC(100);  // 자동으로 파일명과 행 번호 전달
```

Microsoft CRT는 이를 더 우아하게 처리한다:

```c
#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif
```

`_CRTDBG_MAP_ALLOC`을 CRT 헤더보다 먼저 정의하면 `malloc` 계열 호출이 debug heap 변형으로 매핑된다. C++의 `new` 호출 위치까지 기록하려면 debug 빌드에서 `new(_NORMAL_BLOCK, __FILE__, __LINE__)`를 감싸는 별도 `DEBUG_NEW` 매크로를 사용한다. 표준 헤더보다 먼저 임의로 `malloc`/`free` 이름을 재정의하는 방식은 헤더와 서드파티 코드까지 오염시킬 수 있다.

### C++20 std::source_location

C++20은 이를 표준 라이브러리로 제공한다:

```cpp
#include <source_location>

void* tracked_new(
    size_t size,
    std::source_location loc = std::source_location::current()
) {
    std::cout << "Allocation at "
              << loc.file_name() << ":"
              << loc.line() << " in "
              << loc.function_name() << "\n";
    return ::operator new(size);
}

// 사용
auto p = tracked_new(100);  // 매크로 없이 위치 추적!
```

`std::source_location::current`는 **기본 인자**로 평가되므로, 호출 지점의 정보를 자동으로 캡처한다. cppreference는 이를 매크로보다 "더 나은 대안(better alternative)"이라고 설명한다.

### 실전 메모리 프로파일러

표준 `malloc`을 감싸 할당 지점을 기록하는 간단한 프로파일러:

```c
typedef struct {
    void* addr;
    size_t size;
    const char* file;
    int line;
} AllocInfo;

#define MAX_ALLOCS 10000
AllocInfo g_allocs[MAX_ALLOCS];
int g_alloc_count = 0;

void* debug_malloc(size_t size, const char* file, int line) {
    void* ptr = malloc(size);
    if (g_alloc_count < MAX_ALLOCS) {
        g_allocs[g_alloc_count++] = (AllocInfo){ptr, size, file, line};
    }
    return ptr;
}

void debug_free(void* ptr) {
    for (int i = 0; i < g_alloc_count; i++) {
        if (g_allocs[i].addr == ptr) {
            g_allocs[i] = g_allocs[--g_alloc_count];  // 제거
            break;
        }
    }
    free(ptr);
}

void print_leaks() {
    for (int i = 0; i < g_alloc_count; i++) {
        printf("Leak: %zu bytes at %s:%d\n",
               g_allocs[i].size, g_allocs[i].file, g_allocs[i].line);
    }
}
```

Valgrind와 AddressSanitizer는 이 개념을 극단까지 밀어붙여, 모든 메모리 접근을 추적하고 invalid access를 실시간으로 탐지한다.

## 6. Arena와 String Builder의 유사성

지금까지 살펴본 메모리 할당의 핵심은 "상태(메타데이터)를 어디에 저장하느냐"였다. Block header에 크기를 박고, free list 헤드를 들고 다니고, 마지막 사용 위치를 offset으로 기억한다. 같은 발상이 자료구조 전반에 반복된다.

```cpp
class Arena {
    char* buffer;
    size_t offset;     // 현재 위치를 기억해 탐색을 제거
public:
    void* allocate(size_t size) {
        void* ptr = buffer + offset;
        offset += size;
        return ptr;
    }
};

class StringBuilder {
    char* buffer;
    size_t length;     // 현재 길이를 기억해 끝 탐색을 제거
public:
    void append(const char* s, size_t len) {
        memcpy(buffer + length, s, len);
        length += len;
    }
};
```

둘 다 마지막 위치를 멤버로 들고 있다. Arena는 그것을 다음 할당의 시작 주소로, StringBuilder는 다음 문자열 추가의 시작 주소로 사용한다. C 스타일 `strcat`이 매 호출마다 `\0`을 다시 찾아 누적 $O(n^2)$가 되는 것과 같은 문제를, 같은 방식으로 푼다. 메모리 할당자가 free list 탐색을 피하기 위해 cached free pointer를 두는 것도 같은 원리다.

이 글에서 다룬 메타데이터의 위치, 정렬, 캐시 라인이라는 세 가지 축은 자료구조와 알고리즘 모두에 등장한다. 같은 압력을 만나면 같은 형태의 해법이 나오는 셈이다.


---

## 결론

메모리 할당은 단순한 API가 아니라 언어의 정렬 계약, 플랫폼 ABI, 할당자 메타데이터, 성능과 진단 정책이 얽힌 시스템이다. `free`가 크기 인자 없이 동작할 수 있는 것은 구현이 청크 메타데이터나 별도 인덱스에서 크기를 복구하기 때문이고, 일반적인 64비트 glibc의 16바이트 정렬은 ABI와 `MALLOC_ALIGNMENT` 정책의 결과다. 할당 간격이 캐시 라인보다 작다는 사실은 서로 다른 스레드의 객체를 같은 라인에 밀어 넣는 기본값을 만들고, 그것을 되돌리려면 정렬만이 아니라 크기까지 라인 단위로 올려야 한다. `0xCD`/`0xDD` 같은 디버그 패턴은 초기화 누락과 use-after-free를 가시화한다. Arena와 String Builder가 비슷한 형태를 갖는 이유는 마지막 위치를 보존해 반복 탐색을 제거하기 때문이다.

단일 객체의 할당 비용은 객체 내부에서 끝나지 않는다. 객체들이 실제 주소 공간과 캐시 라인에 어떻게 배치되는지까지 포함해야 전체 메모리 비용을 설명할 수 있다.

## 더 읽을거리

### 공식 문서
- [Arm: Armv8-A memory model guide](https://developer.arm.com/documentation/102376/latest/)
- [Microsoft Learn: CRT Debug Heap Details](https://learn.microsoft.com/en-us/visualstudio/debugger/crt-debug-heap-details)
- [Microsoft Learn: HeapAlloc function](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapalloc)
- [cppreference: std::source_location](https://en.cppreference.com/w/cpp/utility/source_location)
- [Intel 64 and IA-32 Architectures Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) — 정렬 예외(#GP)와 SIMD 명령 의미는 SDM, 비용은 함께 게재된 Optimization Reference Manual 참조.

### 기술 논문 및 서적
- Ulrich Drepper, ["What Every Programmer Should Know About Memory"](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf), 2007
- Joel Spolsky, ["Back to Basics"](https://www.joelonsoftware.com/2001/12/11/back-to-basics/), 2001
- Donald Knuth, "The Art of Computer Programming, Volume 1: Fundamental Algorithms", Section 2.5 (Dynamic Storage Allocation)
- Arjun Sreedharan, ["Memory Allocators 101 - Write a simple memory allocator"](https://arjunsreedharan.org/post/148675821737/memory-allocators-101-write-a-simple-memory)

### 소스 코드 분석
- [glibc malloc implementation](https://sourceware.org/git/?p=glibc.git;a=blob;f=malloc/malloc.c) (malloc/malloc.c)
- [jemalloc documentation](https://jemalloc.net/)
- [mimalloc by Microsoft Research](https://github.com/microsoft/mimalloc)

### 도구
- [Valgrind Memcheck](https://valgrind.org/docs/manual/mc-manual.html)
- [AddressSanitizer (ASan)](https://github.com/google/sanitizers/wiki/AddressSanitizer)
- [HeapTrack](https://github.com/KDE/heaptrack)
