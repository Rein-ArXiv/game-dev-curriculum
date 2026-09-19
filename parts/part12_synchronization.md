## 들어가며

멀티스레드 프로그래밍은 현대 시스템 소프트웨어의 필수 요소다. 하지만 단순히 락을 걸고 푸는 것만으로는 부족하다. CRITICAL_SECTION이 Mutex보다 빠른 이유, 원자적 레퍼런스 카운팅이 단순 증감보다 복잡한 이유, CPU와 컴파일러가 명령어를 재배치하는 환경에서 정합성을 유지하는 방법은 모두 동기화의 깊은 곳에 자리한다.

이 글은 Windows의 사용자 모드 락 구조부터 C++ 메모리 모델까지를 한 번에 짚는다. 시작은 락 자체의 비용 분석이고, 끝은 Memory Ordering이라는 추상화가 왜 필요한지에 대한 설명이다.


---

## 목차

1. [CRITICAL_SECTION과 Mutex의 비용 차이](#1-critical_section과-mutex의-비용-차이)
2. [Lock-free 프로그래밍 개요](#2-lock-free-프로그래밍-개요)
3. [원자적 레퍼런스 카운팅](#3-원자적-레퍼런스-카운팅)
4. [Memory Ordering](#4-memory-ordering)
5. [실전 패턴과 안티패턴](#5-실전-패턴과-안티패턴)
6. [핵심 요약](#6-핵심-요약)


---

## 1. CRITICAL_SECTION과 Mutex의 비용 차이

### 비용 차이의 근원: 범위와 fast path

Windows의 `CRITICAL_SECTION`과 mutex 커널 오브젝트는 모두 상호 배제를 제공하지만 계약이 다르다. `CRITICAL_SECTION`은 한 프로세스 안의 스레드만 동기화하고, 무경합 획득은 사용자 모드의 원자 연산으로 처리할 수 있다. 이름 있는 mutex는 프로세스 사이에서 공유할 수 있고, handle 수명·보안 descriptor·abandoned 상태 같은 커널 객체 의미론을 제공한다. 그 기능에는 객체 관리자와 대기 디스패처 경로가 필요하다.

```c
HANDLE hMutex = CreateMutex(NULL, FALSE, NULL);
WaitForSingleObject(hMutex, INFINITE);
// critical section...
ReleaseMutex(hMutex);
```

중요한 차이는 "mutex 함수는 언제나 고정 횟수의 시스템 호출을 한다"는 식의 숫자가 아니라, `CRITICAL_SECTION`이 프로세스 내부 전용이라는 제한 덕분에 더 짧은 fast path를 가질 수 있다는 점이다. 정확한 진입 경로와 비용은 Windows 버전, 경합 상태, 대기 시간에 따라 달라진다.

### 개념적인 3단계 전략

`CRITICAL_SECTION`의 공개 계약은 재귀 획득 가능한 프로세스 내부 락이라는 점이다. 구조체 필드와 대기 프리미티브는 구현 세부사항이므로 직접 해석하거나 수정하면 안 된다. 비용 모델은 다음 세 경로로 이해할 수 있다.

**1단계: Fast Path (유저 모드)**

```c
// 실제 Windows 소스가 아닌 개념적 의사 코드
if (try_acquire_in_user_mode(cs)) {
    return;
}
```

경합이 없는 경로는 사용자 모드의 원자 상태 전이와 소유자·재귀 상태 갱신만으로 끝날 수 있어 대기용 커널 전환을 피한다. 공개 API는 정확히 CAS 한 번이라고 보장하지 않으므로 연산 수나 내부 필드 배치를 계약으로 가정하면 안 된다.

**2단계: Spin-Wait (여전히 유저 모드)**

스핀 카운트가 설정되고 멀티프로세서 시스템이라면 잠들기 전에 짧게 재시도할 수 있다.

```c
for (DWORD i = 0; i < configured_spin_count; ++i) {
    if (try_acquire_in_user_mode(cs)) {
        return;
    }
    cpu_relax();
}
```

크리티컬 섹션이 매우 짧고 소유자가 다른 논리 프로세서에서 실행 중이라면, 대기 등록과 수면·깨우기보다 스핀이 저렴할 수 있다. 반대로 소유자가 deschedule되었거나 임계 구역이 길면 스핀은 CPU 시간만 소비한다.

`_mm_pause` 명령어는 두 가지 역할을 한다.

- SMT 환경에서 실행 자원 경쟁을 줄이고 과도한 전력 소비를 완화
- 일부 x86 구현에서 스핀 종료 시 발생할 수 있는 memory-order violation 페널티를 줄임

**3단계: Slow Path (커널 모드)**

스핀이 끝나도 락을 획득하지 못하면 운영체제 대기 경로에 등록되어 스레드가 block될 수 있다.

```c
wait_until_signaled_by_owner(cs); // 개념적 slow path
```

slow path의 비용은 단순한 mode switch 하나가 아니다. 대기자 등록, 스케줄링, 깨우기, 다른 코어에서 돌아온 뒤의 캐시 복구가 합쳐진다. 따라서 평균 시간 하나보다 무경합 획득, 짧은 경합, 실제 수면을 각각 분리해 측정해야 한다.

| 측정 시나리오 | 드러나는 비용 |
|--------------|--------------|
| 한 스레드의 반복 lock/unlock | fast path와 컴파일러·측정 오버헤드 |
| 여러 스레드, 임계 구역 0에 가까움 | 원자 연산과 캐시 라인 소유권 이동 |
| 짧은 임계 구역 | 스핀 정책의 손익 |
| 긴 임계 구역 | block/wakeup과 scheduler tail latency |
| 프로세스 간 경합 | 커널 mutex가 제공하는 추가 의미론의 비용 |

### 스핀 카운트 최적화

스핀 카운트는 양날의 검이다. 너무 낮으면 이점을 살리지 못하고, 너무 높으면 CPU를 낭비한다.

```c
CRITICAL_SECTION cs;

// 기본 초기화
InitializeCriticalSection(&cs);

// 워크로드 측정으로 근거가 있을 때만 명시적으로 설정
InitializeCriticalSectionAndSpinCount(&cs, measured_spin_count);
```

단일 프로세서에서는 소유자가 동시에 실행될 수 없으므로 스핀 이득이 없다. 멀티프로세서에서도 보편적인 권장 횟수는 없다. CPU 주파수와 `_mm_pause` 처리량, 임계 구역 분포, oversubscription 여부가 모두 손익분기점을 바꾼다.

잘못된 예는 다음과 같다.

```c
CRITICAL_SECTION cs;
InitializeCriticalSectionAndSpinCount(&cs, 10000);

EnterCriticalSection(&cs);
PerformDatabaseQuery();  // 예: I/O로 수 ms 이상 걸릴 수 있는 작업
LeaveCriticalSection(&cs);

// 다른 스레드는 긴 I/O가 끝날 때까지 스핀 예산을 낭비할 수 있다.
```

스핀 카운트는 프로파일링을 통해 실제 워크로드에 맞게 조정해야 한다.

## 2. Lock-free 프로그래밍 개요

락을 사용하지 않고 스레드 안전성을 확보하는 방법이 Lock-free 프로그래밍이다. 여기서는 핵심 개념만 짚는다.

### 진행 보장 수준

동시성 알고리즘은 진행 보장(Progress Guarantee)에 따라 분류된다.

| 보장 수준 | 시스템 진행 | 개별 스레드 진행 | 설명 |
|-----------|-------------|------------------|------|
| Wait-free | 보장 | 보장 | 각 연산이 유한 단계 내 완료 |
| Lock-free | 보장 | 불보장 | 시스템은 진행하나 개별 스레드는 지연 가능 |
| Obstruction-free | 불보장 | 조건부 | 간섭 없을 때만 진행 |
| Blocking | 불보장 | 불보장 | 락 기반, 데드락 가능 |

많은 lock-free 알고리즘은 CAS 같은 원자적 read-modify-write와 재시도를 사용한다. lock-free 보장은 어떤 스레드 하나가 특정 CAS에서 반드시 성공한다는 뜻이 아니라, 스레드들이 충분히 실행되는 동안 시스템 전체에서 연산 완료가 계속 일어나야 한다는 진행 조건이다. 개별 스레드는 반복 실패해 기아할 수 있고, 알고리즘은 메모리 회수와 ABA까지 포함해 증명해야 한다.

### Lock-free와 Blocking의 선택 기준

Lock-free가 유리한 경우는 다음과 같다.

- 락 소유자 정지로 전체가 멈추는 것을 피해야 하는 경우
- 경쟁이 짧고 원자적 상태 전이로 명확히 표현되는 경우
- 비차단 진행 보장을 실제 스케줄러와 메모리 회수까지 검증한 경우

Blocking(락)이 나은 경우는 다음과 같다.

- 낮은 동시성 (1~4 스레드)
- 복잡한 불변식 (여러 변수 동시 업데이트)
- 디버깅 용이성 필요

구체적인 비차단 자료구조의 정확성은 CAS 성공 경로만으로 결정되지 않는다. ABA를 막는 버전 정책, 제거된 노드를 언제 재사용할 수 있는지 정하는 Hazard Pointer·epoch 같은 메모리 회수, 실패한 CAS의 재시도 순서까지 하나의 증명 대상으로 묶어야 한다.

## 3. 원자적 레퍼런스 카운팅

### 문제 분석

레퍼런스 카운팅은 메모리 관리의 기본이지만, 멀티스레드 환경에서는 함정이 많다.

```c
typedef struct Object {
    int refCount;
    char data[256];
} Object;

Object* obj = NULL;

// Thread 1: 레퍼런스 증가
void AddRef() {
    if (obj != NULL) {
        obj->refCount++;  // 위험
    }
}

// Thread 2: 객체 삭제
void Release() {
    if (obj != NULL) {
        obj->refCount--;
        if (obj->refCount == 0) {
            free(obj);
            obj = NULL;
        }
    }
}
```

두 가지 치명적인 문제가 있다.

**문제 1: 비원자적 증가**

```text
Thread 1: LOAD refCount (값: 1)
Thread 2: LOAD refCount (값: 1)
Thread 1: ADD 1 (결과: 2)
Thread 2: SUB 1 (결과: 0)
Thread 2: STORE refCount = 0 → free(obj)
Thread 1: STORE refCount = 2 → 이미 해제된 메모리에 쓰기 (Use-After-Free)
```

**문제 2: 포인터 로드와 카운트 증가의 비원자성**

```c
// Thread 1
Object* temp = obj;        // (1) temp = 0x1000, refCount = 1
// [컨텍스트 스위칭]
// Thread 2: Release() → refCount = 0 → free(obj)
temp->refCount++;          // (2) Use-After-Free
```

### shared_ptr 계열 제어 블록의 전략

C++ 표준의 `shared_ptr`는 같은 소유권을 공유하는 객체들이 제어 블록을 통해 strong/weak 수명을 관리하도록 규정한다. 아래는 Windows `Interlocked` API로 원리를 보이는 모형이지 표준 라이브러리의 필수 레이아웃이 아니다.

```c
typedef struct ControlBlock {
    volatile LONG strongRefs;   // Interlocked API가 조작하는 저장소
    volatile LONG weakRefs;     // implicit weak reference를 포함하는 모형
    void* object;               // 실제 객체 포인터
    void (*deleter)(void*);     // 커스텀 삭제자
} ControlBlock;

typedef struct SharedPtr {
    void* ptr;                  // 객체 포인터 (캐시 최적화용)
    ControlBlock* control;      // 제어 블록
} SharedPtr;
```

복사 생성자는 다음과 같다.

```c
void SharedPtr_Copy(SharedPtr* dest, const SharedPtr* src) {
    dest->ptr = src->ptr;
    dest->control = src->control;

    if (dest->control != NULL) {
        // 원자적 증가
        InterlockedIncrement(&dest->control->strongRefs);
    }
}
```

`volatile`이 원자성을 만드는 것은 아니다. 여기서는 Windows `Interlocked*` 함수의 인자 형식 때문에 붙어 있고, 실제 원자적 read-modify-write와 ordering은 그 API가 제공한다. 또한 서로 다른 `shared_ptr` 객체가 같은 제어 블록을 공유하는 것은 안전하지만, 한 `shared_ptr` 변수 자체를 다른 스레드가 동시에 `reset`하는 동안 평범한 복사 생성으로 읽으면 안전하지 않다. 그런 publication에는 mutex 또는 `std::atomic<std::shared_ptr<T>>`가 필요하다.

소멸자는 다음과 같다.

```c
void SharedPtr_Destroy(SharedPtr* sp) {
    if (sp->control == NULL)
        return;

    // 원자적 감소 + 반환값 확인
    LONG newStrongRefs = InterlockedDecrement(&sp->control->strongRefs);

    if (newStrongRefs == 0) {
        // 마지막 shared_ptr → 객체 삭제
        if (sp->control->deleter)
            sp->control->deleter(sp->ptr);
        else
            free(sp->ptr);

        // weak_ptr도 없으면 제어 블록 삭제
        LONG newWeakRefs = InterlockedDecrement(&sp->control->weakRefs);
        if (newWeakRefs == 0) {
            free(sp->control);
        }
    }

    sp->ptr = NULL;
    sp->control = NULL;
}
```

### weak_ptr 승격 문제

weak_ptr에서 shared_ptr로 승격할 때 더 복잡한 문제가 발생한다.

```c
// 잘못된 구현
SharedPtr SharedPtr_Lock(const WeakPtr* weak) {
    SharedPtr result = {NULL, NULL};

    if (weak->control == NULL)
        return result;

    LONG oldRefs = weak->control->strongRefs;  // (1)
    if (oldRefs == 0)
        return result;  // 실패

    InterlockedIncrement(&weak->control->strongRefs);  // (2)
    // 문제: (1)과 (2) 사이에 0으로 변할 수 있음
    result.ptr = weak->ptr;
    result.control = weak->control;
    return result;
}
```

올바른 해결책은 CAS 루프다.

```c
SharedPtr SharedPtr_Lock(const WeakPtr* weak) {
    SharedPtr result = {NULL, NULL};

    if (weak->control == NULL)
        return result;

    while (1) {
        LONG oldRefs = weak->control->strongRefs;

        // 이미 0이면 실패
        if (oldRefs == 0)
            return result;

        // CAS: oldRefs → oldRefs + 1
        if (InterlockedCompareExchange(
            &weak->control->strongRefs,
            oldRefs + 1,
            oldRefs) == oldRefs) {
            // 성공
            result.ptr = weak->ptr;
            result.control = weak->control;
            return result;
        }
        // 실패: 다른 스레드가 수정 → 재시도
    }
}
```

### 분리된 포인터 최적화

`ptr`과 `control`을 분리하는 이유는 캐시 효율성이다.

```c
// 방법 1: 하나의 포인터만 저장
typedef struct SharedPtr_v1 {
    ControlBlock* control;
} SharedPtr_v1;

void* Get(SharedPtr_v1* sp) {
    return sp->control->object;  // 간접 참조 2번
}

// 방법 2: 분리 저장 (실제 구현)
typedef struct SharedPtr_v2 {
    void* ptr;              // 직접 접근
    ControlBlock* control;
} SharedPtr_v2;

void* Get(SharedPtr_v2* sp) {
    return sp->ptr;  // 간접 참조 1번
}
```

분리 저장의 구조적 이득은 `operator*`와 `get()`이 control block을 경유하지 않아도 된다는 점이다. 특히 aliasing constructor는 소유하는 객체와 노출하는 포인터가 다를 수 있으므로 두 값이 의미상으로도 필요하다. 단순 반복 역참조의 고정 배수는 compiler가 루프 밖으로 load를 이동할 수 있어 대표성이 낮다. 실제 pointee 접근, shared_ptr 복사·소멸, control block cache miss를 분리해 측정한다.

## 4. Memory Ordering

### 문제의 근원: 최적화

컴파일러와 CPU는 성능을 위해 명령어 순서를 재배치한다. 이것이 멀티스레드에서는 재앙이 될 수 있다.

```c
int data = 0;
bool ready = false;

void Producer() {
    data = 42;      // (1)
    ready = true;   // (2)
}

void Consumer() {
    while (!ready);  // (3) 스핀 대기
    printf("%d\n", data);  // (4)
}
```

기대는 Consumer가 42를 출력하는 것이지만, 실제로는 0이 출력될 수 있다.

**시나리오 1: 컴파일러 재배치**

```asm
; 컴파일러가 (1)과 (2)를 재배치
Producer:
    mov [ready], 1      ; ready = true를 먼저
    mov [data], 42      ; data = 42를 나중에

; Consumer가 ready == true를 보고 진행
; 하지만 data는 아직 0 → 출력: 0
```

**시나리오 2: CPU Store Buffer**

```text
CPU1 (Producer):          CPU2 (Consumer):
mov [data], 42
  → Store Buffer에 대기
mov [ready], 1
  → 캐시에 즉시 반영    → ready == true 확인
                         → data 읽기
                         → 캐시에 아직 0 → 출력: 0
  → Store Buffer flush
```

### Sequential Consistency

`memory_order_seq_cst` 원자 연산들은 각 원자 객체의 수정 순서와 스레드 내부 순서를 보존하는 하나의 전역 총순서에 참여한다. 이는 C++ 원자 메모리 순서 중 가장 강한 기본 선택이지만, 모든 일반 메모리 연산이 한순간에 모든 코어에 보인다는 뜻은 아니다.

```c
std::atomic<int> data{0};
std::atomic<bool> ready{false};

void Producer() {
    data.store(42, std::memory_order_seq_cst);   // (1)
    ready.store(true, std::memory_order_seq_cst); // (2)
}

void Consumer() {
    while (!ready.load(std::memory_order_seq_cst));  // (3)
    printf("%d\n", data.load(std::memory_order_seq_cst));  // (4)
    // 보장: 42 출력
}
```

Consumer 루프를 빠져나온 *성공한* (3)이 (2)가 저장한 `true`를 읽었다고 하자. 그때 보장되는 관계는 다음과 같다.

- (1)은 (2)보다 sequenced-before이므로 happens-before
- (2)의 store는 그 값을 읽은 (3)의 load와 synchronizes-with
- (3)은 (4)보다 sequenced-before이므로 happens-before
- 따라서 (1) happens-before (4)

seq_cst의 비용은 연산 종류와 ISA·컴파일러에 따라 다르다. x86-64에서 seq_cst load는 보통 일반 load로 충분하지만 seq_cst store는 `xchg` 같은 locked 명령이나 store+fence로 구현될 수 있다. ARMv8은 acquire/release 명령과 배리어를 조합한다. 따라서 “모든 seq_cst 연산은 `mfence`이며 수백 cycle” 같은 고정 비용표는 틀리다. 생성된 어셈블리와 실제 경합·캐시 라인 이동을 측정해야 한다.

### Acquire-Release

Acquire-Release는 실무에서 가장 많이 사용되는 모델이다.

```c
std::atomic<bool> ready{false};
int data = 0;  // 일반 변수

void Producer() {
    data = 42;  // (1) 일반 쓰기
    ready.store(true, std::memory_order_release);  // (2) Release
    // 보장: (1)이 (2)보다 먼저 다른 CPU에 보임
}

void Consumer() {
    while (!ready.load(std::memory_order_acquire));  // (3) Acquire
    printf("%d\n", data);  // (4) 일반 읽기
    // 보장: (3)이 (4)보다 먼저 실행됨
    //       (3)에서 true를 봤다면, (1)의 결과도 봄
}
```

Synchronizes-with 관계는 다음과 같이 정리된다.

```text
Producer의 Release (2)
    ↓ synchronizes-with
Consumer의 Acquire (3)

(1) happens-before (2) synchronizes-with (3) happens-before (4)
→ (1) happens-before (4)
```

x86 구현:

```asm
; Producer
mov [data], 42          ; 일반 쓰기
mov [ready], 1          ; x86은 기본적으로 Release 시맨틱
                        ; (TSO: Total Store Order 모델)

; Consumer
.loop:
mov al, [ready]         ; x86은 기본적으로 Acquire 시맨틱
test al, al
jz .loop
mov eax, [data]         ; 42가 보장됨
```

x86에서는 추가 명령어가 거의 없어 성능 손실이 미미하다.

ARM 구현:

```asm
; Producer
mov r0, #42
str r0, [data]          ; 일반 쓰기
dmb ish                 ; Release에는 Store-Store + Load-Store 순서가 필요
                        ; (ARMv8에서는 stlr 한 명령으로 release store 가능)
mov r1, #1
str r1, [ready]

; Consumer
.loop:
ldr r2, [ready]
cmp r2, #0
beq .loop
dmb ish                 ; Acquire barrier (Load-Load, Load-Store)
ldr r3, [data]          ; 42가 보장됨
```

ARM에서는 명시적 배리어가 필요하지만 Sequential Consistency보다 저렴하다.

### Relaxed

Relaxed는 순서 보장 없이 원자성만 보장한다.

```c
std::atomic<int> counter{0};

void Increment() {
    counter.fetch_add(1, std::memory_order_relaxed);
}

// 10개 스레드가 각각 1000번 증가
// 결과: 10000 보장 (원자성)
// 하지만 중간 값은 어떤 순서로든 보일 수 있음
```

사용 사례는 단순 카운터, 통계다.

```c
std::atomic<uint64_t> bytes_sent{0};
std::atomic<uint64_t> packets_sent{0};

void SendPacket(const char* data, size_t len) {
    // ... 실제 전송 ...

    bytes_sent.fetch_add(len, std::memory_order_relaxed);
    packets_sent.fetch_add(1, std::memory_order_relaxed);

    // 순서가 중요하지 않음. 최종 합만 정확하면 됨
}
```

위험한 사용은 다음과 같다.

```c
std::atomic<int> x{0}, y{0};
int r1, r2;

// Thread 1
x.store(1, std::memory_order_relaxed);  // (1)
r1 = y.load(std::memory_order_relaxed);  // (2)

// Thread 2
y.store(1, std::memory_order_relaxed);  // (3)
r2 = x.load(std::memory_order_relaxed);  // (4)

// 가능한 결과: r1 == 0 && r2 == 0
// 이유: (1)과 (2), (3)과 (4)가 재배치됨
```

### Double-Checked Locking

Double-Checked Locking은 싱글톤 패턴의 고전적 최적화지만 함정이 많다.

잘못된 구현:

```c
Widget* widget = nullptr;
Mutex mutex;

Widget* GetInstance() {
    if (widget == nullptr) {  // (1) 첫 번째 체크 (락 없음)
        Lock lock(mutex);
        if (widget == nullptr) {  // (2) 두 번째 체크 (락 있음)
            widget = new Widget();  // (3) 생성
        }
    }
    return widget;
}
```

(3)의 실제 동작과 가능한 재배치는 다음과 같다.

```c
// (3)의 실제 동작:
Widget* temp = operator new(sizeof(Widget));  // (3a) 메모리 할당
new (temp) Widget();                          // (3b) 생성자 호출
widget = temp;                                // (3c) 포인터 할당

// 데이터 레이스를 무시하고 하드웨어 순서만 생각했을 때 흔히 드는 직관:
Widget* temp = operator new(sizeof(Widget));  // (3a)
widget = temp;                                // (3c) 먼저
new (temp) Widget();                          // (3b) 나중에

// Thread 2가 widget != nullptr를 관찰해도 생성 완료를 보장할 동기화가 없음
```

비원자 포인터를 여러 스레드가 읽고 쓰는 원래 코드는 C++ 데이터 레이스이므로 그 자체로 undefined behavior다. 따라서 실제 컴파일 결과를 위 세 줄의 특정 재배치로만 설명해서는 안 된다. 핵심은 객체 초기화와 포인터 publication 사이에 release/acquire 같은 happens-before가 없다는 것이다.

올바른 구현(C++11):

```c
std::atomic<Widget*> widget{nullptr};
Mutex mutex;

Widget* GetInstance() {
    Widget* tmp = widget.load(std::memory_order_acquire);  // (1)

    if (tmp == nullptr) {
        Lock lock(mutex);
        tmp = widget.load(std::memory_order_relaxed);  // (2) 락 안에서는 relaxed OK

        if (tmp == nullptr) {
            tmp = new Widget();
            widget.store(tmp, std::memory_order_release);  // (3)
        }
    }

    return tmp;
}
```

보장은 다음과 같다.

- (3)의 Release → (1)의 Acquire로 동기화
- `new Widget` 완료 happens-before (3)
- (3) synchronizes-with (1)
- 따라서 `new Widget` 완료 happens-before (1) 이후 코드

최신 C++는 더 간단하다.

```c
Widget& GetInstance() {
    static Widget instance;  // C++11 thread-safe 초기화
    return instance;
}
```

### Memory Ordering 선택 가이드

| 시나리오 | 권장 Ordering | 이유 |
|---------|---------------|------|
| 단순 카운터 (통계) | Relaxed | 순서 무관, 최종 값만 중요 |
| 플래그 기반 동기화 | Acquire-Release | 동기화 필요, seq_cst는 과도 |
| Spinlock | Acquire (lock), Release (unlock) | 표준 패턴 |
| 복잡한 동기화 | Seq_cst | 안전 우선 |
| 같은 mutex로 모든 접근을 보호하는 락 내부 통계 | Relaxed 또는 비원자 | 필요한 순서는 mutex가 제공 |

이 표는 출발점일 뿐이다. “복잡하면 seq_cst”는 이해하기 쉬운 기본값이지만 객체 수명과 경쟁 자체를 해결하지 않는다. “락 안에서는 relaxed”도 관련 접근이 모두 같은 락 규약을 따를 때만 성립한다.

## 5. 실전 패턴과 안티패턴

### Spinlock 구현

기본 구현은 다음과 같다.

```cpp
struct Spinlock {
    std::atomic<bool> locked{false};
};

void Lock(Spinlock* lock) {
    while (lock->locked.exchange(true, std::memory_order_acquire)) {
        // 스핀
    }
}

void Unlock(Spinlock* lock) {
    lock->locked.store(false, std::memory_order_release);
}
```

최적화된 구현은 다음과 같다.

```c
void Lock_Optimized(Spinlock* lock) {
    while (1) {
        // 1. Read-only 스핀 (캐시 친화적)
        while (lock->locked.load(std::memory_order_relaxed)) {
            _mm_pause();  // CPU에게 스핀 루프임을 알림
        }

        // 2. test-and-set 시도
        if (!lock->locked.exchange(true, std::memory_order_acquire)) {
            return;  // 성공
        }
    }
}
```

최적화의 이유는 다음과 같다.

- `exchange`는 쓰기 연산이므로 캐시 일관성 트래픽 발생
- `load` 대기 루프는 매 반복마다 쓰기 소유권을 요구하지 않는다. 다른 core의 unlock은 여전히 cache line을 invalidate한다.
- `_mm_pause`: 파이프라인 최적화, 전력 절약

### 안티패턴: Benign Race

"괜찮은 경쟁 조건"이라는 개념은 위험하다.

```c
// 잘못된 생각
bool initialized = false;
Config config;

void EnsureInit() {
    if (!initialized) {
        config = LoadConfig();
        initialized = true;  // 경쟁 조건이지만 "괜찮다"고 생각
    }
}
```

위험한 이유는 다음과 같다.

1. 데이터 레이스는 미정의 동작(Undefined Behavior)이다.
 - C++ 표준: 데이터 레이스가 있으면 프로그램 전체가 UB
 - 컴파일러가 UB를 가정하고 공격적으로 최적화
2. 허용되는 컴파일러 최적화의 예:
 ```c
 // 컴파일러가 볼 수 있는 것:
 if (!initialized) { // initialized는 항상 false로 최적화 가능
 // (다른 스레드가 쓸 수 없다고 가정)
 }
 ```
3. 가능한 결과:
 - 중복 초기화, 부분 상태 관찰, load 제거·hoisting처럼 소스 순서만으로 설명할 수 없는 실행
 - 컴파일러와 최적화 수준에 따라 증상이 달라지는 비재현성

올바른 구현은 다음과 같다.

```c
std::atomic<bool> initialized{false};
Config config;
Mutex mutex;

void EnsureInit() {
    if (!initialized.load(std::memory_order_acquire)) {
        Lock lock(mutex);
        if (!initialized.load(std::memory_order_relaxed)) {
            config = LoadConfig();
            initialized.store(true, std::memory_order_release);
        }
    }
}
```

## 6. 핵심 요약

CRITICAL_SECTION의 3단계 전략은 다음과 같다.

1. Fast Path: 사용자 모드의 원자 상태 전이
2. Spin-Wait: 설정된 예산 안에서 짧게 재시도
3. Slow Path: 운영체제 대기와 깨우기

비차단 진행 보장의 포함 관계는 `wait-free ⇒ lock-free ⇒ obstruction-free`다. Blocking은 이 위계의 마지막 단계가 아니라 락 소유자에 의존하는 별도 범주다. Tagged pointer는 태그가 래핑하지 않는 범위에서 상태 재등장을 구별하고, Hazard Pointer는 사용 중인 노드의 회수를 지연한다. 어느 하나도 모든 형태의 ABA를 자동으로 해결하지 않는다.

Memory Ordering 선택은 다음과 같이 정리된다.

- Seq_cst: 안전 우선, 단순 코드
- Acquire-Release: 실용적, 플래그 동기화
- Relaxed: 단순 카운터, 통계

마지막으로 4가지 황금률을 정리한다.

1. 측정 없는 최적화는 독이다.
2. C++에서 정의된 데이터 레이스는 UB다.
3. 락은 부끄러운 것이 아니다.
4. Lock-free가 항상 더 빠른 것은 아니다.


---

## 결론

이 글은 CRITICAL_SECTION의 경합별 경로, 원자적 레퍼런스 카운팅의 함정, Memory Ordering의 모델, 그리고 Double-Checked Locking과 Spinlock의 올바른 publication을 다뤘다. 락 비용은 단일 배수가 아니라 무경합 원자 연산, cache-line 이동, spin, block/wakeup 가운데 어떤 경로를 밟는지에 따라 달라진다. Memory Ordering의 선택은 필요한 happens-before만 표현하면서도 소유권과 수명 규약을 보존하는 문제다.

동기화 프리미티브를 고를 때는 평균 획득 시간보다 진행 보장, 소유권 범위, 임계 구역 길이, 경합 시 tail latency를 함께 보아야 한다.


## 더 읽을거리

- Jeff Preshing, "Always Use a Lightweight Mutex": https://preshing.com/20111124/always-use-a-lightweight-mutex/
- Jeff Preshing, "Memory Barriers Are Like Source Control Operations": https://preshing.com/20120710/memory-barriers-are-like-source-control-operations/
- Jeff Preshing, "Acquire and Release Semantics": https://preshing.com/20120913/acquire-and-release-semantics/
- Jeff Preshing, "An Introduction to Lock-Free Programming": https://preshing.com/20120612/an-introduction-to-lock-free-programming/
- Microsoft Learn, "CRITICAL_SECTION Objects": https://learn.microsoft.com/en-us/windows/win32/sync/critical-section-objects
- Anthony Williams, *C++ Concurrency in Action*, 2nd Edition (Chapter 5: Memory Model and Atomic Operations, Chapter 7: Lock-free Data Structures)
- Maged Michael, Michael Scott, "Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms": https://www.cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf
- Maurice Herlihy, Nir Shavit, *The Art of Multiprocessor Programming* (Chapter 3, Chapter 7)
- Herb Sutter, "atomic Weapons: The C++ Memory Model and Modern Hardware": https://herbsutter.com/2013/02/11/atomic-weapons-the-c-memory-model-and-modern-hardware/
- Intel, "Intel 64 and IA-32 Architectures Software Developer's Manual, Volume 3A" (325384-092US 기준 11장: Multiple-Processor Management)
- ARM, "ARM Barrier Litmus Tests and Cookbook": https://developer.arm.com/documentation/genc007826/latest/
- cppreference, "std::memory_order": https://en.cppreference.com/w/cpp/atomic/memory_order
