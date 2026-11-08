## 들어가며

언어 런타임의 메모리 관리는 프로그래머의 제어와 자동 회수 사이의 트레이드오프다. C의 `malloc`/`free`, C++의 RAII, Rust의 소유권 시스템, Java/C#/Go의 가비지 컬렉션은 서로 다른 지점에 비용을 둔다. 수십 가지 GC 알고리즘이 등장했지만 어느 하나도 모든 워크로드를 만족시키지 못한다.

답은 트레이드오프다. 자유도와 안전성, 처리량과 지연 시간, 메모리 풋프린트와 회수 빈도가 서로 충돌한다. 이 글은 도달 가능성이라는 GC의 근본 정의에서 출발해 reference counting과 mark-and-sweep을 두 축으로 두고, generational, concurrent, incremental 같은 변형이 어떤 트레이드오프 위에서 만들어졌는지 본다. 후반부에서는 Unity/Unreal 같은 게임 엔진이 왜 GC를 회피하거나 별도 시스템을 만드는지, 그리고 간단한 mark-sweep을 직접 구현하는 방법을 다룬다.


---

## 목차
1. [메모리 관리의 스펙트럼](#1-메모리-관리의-스펙트럼-수동에서-자동까지)
2. [Reference Counting](#2-reference-counting-gc-스펙트럼의-한-끝)
3. [Mark-and-Sweep](#3-mark-and-sweep-도달-가능성의-추적)
4. [Generational GC](#4-generational-gc-약한-세대-가설)
5. [Concurrent/Incremental GC](#5-concurrentincremental-gc-stop-the-world-정지-축소)
6. [게임에서의 GC](#6-게임에서의-gc-unity-unreal의-선택)
7. [커스텀 GC 구현](#7-커스텀-gc-구현-간단한-mark-sweep)
8. [GC 튜닝과 메모리 압박](#8-실전-gc-튜닝과-메모리-압박)


---

## 1. 메모리 관리의 스펙트럼: 수동에서 자동까지

### 문제: 누가 메모리를 해제하는가?

```c
// 수동 관리 (C)
void* ptr = malloc(100);
// ... 복잡한 제어 흐름
free(ptr);  // 모든 경로에서 호출되어야 함
```

수동 메모리 관리의 문제는 **복잡한 제어 흐름**에서 발생한다:

```c
void process_data() {
    char* buffer = malloc(1024);

    if (condition1) {
        free(buffer);
        return;  // OK
    }

    if (condition2) {
        return;  // 메모리 누수!
    }

    // 예외가 발생하면? C++에서는 더 복잡해짐
    risky_operation();

    free(buffer);  // 여기까지 도달하지 못할 수도
}
```

Microsoft 보안 대응 센터(MSRC)의 2019년 발표에 따르면, 자사 제품 보안 취약점(CVE)의 약 **70%가 메모리 안전성 문제**다([MSRC, "A proactive approach to more secure code"](https://msrc.microsoft.com/blog/2019/07/a-proactive-approach-to-more-secure-code/); 링크는 현재 포털로 리다이렉트되므로 2019년 수치는 아카이브 원문으로 확인해야 한다). 대표적인 유형은 다음과 같다:

- Use-after-free
- Double-free
- Memory leak
- Dangling pointer

### 스펙트럼: 회수 조건과 실패 모드

메모리 관리 방식을 “성능 상·하”로 한 줄 순위에 놓으면 잘못 배운다. 같은 방식도 할당률, 객체 그래프, 스레드 간 소유권과 구현에 따라 처리량·지연·메모리가 달라진다. 비교해야 할 것은 **언제 회수할 수 있다고 판단하는가**와 **그 판단이 실패하면 무엇이 생기는가**다.

| 방식 | 회수 조건 | 강점 | 남는 실패 모드와 비용 |
|---|---|---|---|
| 명시적 `free` | 프로그래머가 수명을 끝냄 | 배치·회수 시점을 직접 설계 가능 | 누락, 이중 해제, use-after-free, allocator·OS 지연 |
| RAII/단일 소유 | 소유 객체의 스코프·수명 종료 | 예외 경로까지 결정적 정리, 자원 일반화 | 공유 수명 모델은 별도 설계, 긴 소멸자 연쇄 |
| 정적 소유권 검사 | 소유권·borrow 규칙으로 수명 검증 | 많은 댕글링·데이터 경쟁을 컴파일 시 차단 | `unsafe`, FFI, 내부 가변성, `Rc` 순환은 별도 책임 |
| Reference Counting | 강한 참조 수가 0 | 마지막 참조 해제 시점에 즉시 회수 | 순환, 증감 비용, 공유 원자 연산, destructor cascade |
| Tracing GC | root에서 도달 불가능 | 순환 그래프도 회수, 해제 호출 불필요 | 추적·barrier·여유 힙, pause, 살아 있지만 불필요한 logical leak |

명시적 관리는 **제어 수단**을 주지만 실제 실행 시간을 자동으로 예측 가능하게 만들지는 않는다. `free`가 중앙 allocator나 OS 경로를 건드릴 수 있고, 어느 코드 경로에서 마지막 소유자가 사라지는지 불명확하면 소멸 비용도 튄다. 반대로 tracing GC는 물리적인 미도달 객체를 회수하지만, 캐시나 이벤트 구독이 참조를 붙잡은 logical leak까지 알아내지는 못한다. 안전성과 예측 가능성은 언어 이름이 아니라 소유권 규약, 할당 정책, 수명 분포와 계측에서 나온다.

### McCarthy의 통찰: "살아있는" 객체의 정의

John McCarthy는 1960년 논문 "Recursive Functions of Symbolic Expressions and Their Computation by Machine"에서 LISP를 위한 최초의 자동 메모리 회수를 제시했다. 그 핵심은 이후 모든 tracing GC가 공유하는 정의로 정리된다. 객체는 root set에서 참조 사슬을 따라 도달 가능할 때 살아있다.

**도달 가능성 (Reachability)**이 GC의 근본 개념이다:

```text
Root Set (항상 살아있음):
  - 스택의 지역 변수
  - 전역 변수
  - CPU 레지스터

살아있는 객체:
  root → obj1 → obj2 → obj3  (도달 가능)

죽은 객체:
  obj4 → obj5  (root에서 도달 불가능)
```

이 단순한 정의가 모든 tracing GC의 기반이다.

## 2. Reference Counting: GC 스펙트럼의 한 끝

Reference counting은 가장 단순한 형태의 GC다. 각 객체가 자신을 가리키는 참조 개수를 추적하고, 0이 되면 즉시 해제한다.

```c
Object* retain(Object* obj)  { obj->ref_count++; return obj; }
void   release(Object* obj)  { if (--obj->ref_count == 0) free(obj); }
```

이 방식은 **즉각적 회수**와 **예측 가능한 시점**을 제공하지만, 두 가지 구조적 한계가 있다.

- 순환 참조를 자체적으로 해결하지 못한다. `a → b → a`는 외부 참조가 끊겨도 양쪽 카운트가 0이 되지 않아 영원히 해제되지 않는다. `weak_ptr` 같은 약한 참조가 보조 수단으로 필요하다.
- 포인터 대입마다 카운트 증감과 메모리 쓰기가 일어난다. 멀티스레드 환경에서는 atomic 연산이 추가되어 `shared_ptr` 복사 한 번이 수십 cycle을 잡아먹는다.

Apple Core Foundation, Python(CPython의 주 메모리 관리), C++ `std::shared_ptr`이 모두 이 방식을 사용하거나 변형한다. 이 글에서는 reference counting을 **트레이싱 GC와 대비되는 한쪽 끝**으로 기억하면 충분하다. 즉시 회수하고 카운트를 들고 다니는 대신 순환을 못 푸는 쪽이 RC, 카운트 없이 도달 가능성을 주기적으로 추적하는 대신 stop-the-world가 발생할 수 있는 쪽이 트레이싱 GC다.

## 3. Mark-and-Sweep: 도달 가능성의 추적

### 원리: 살아있는 것을 표시하고, 나머지를 청소

1960년 McCarthy의 원조 알고리즘을 후대의 tri-color 표기로 다시 쓰면:

**Phase 1: Mark (표시)**

Root set에서 시작해 도달 가능한 모든 객체를 표시한다:

```text
1. 모든 객체를 white로 초기화
2. Root set의 각 객체를 gray로 표시
3. Gray 큐가 빌 때까지:
   a. Gray 객체 하나 꺼내기
   b. Black으로 변경
   c. 이 객체가 참조하는 모든 white 객체를 gray로
```

**Phase 2: Sweep (청소)**

```text
4. 힙을 순회하며:
   - Black → White로 변경 (다음 GC 준비)
   - White → 회수 (도달 불가능했음)
```

### Tri-color Marking: White, Gray, Black

Dijkstra et al. (1978) "On-the-Fly Garbage Collection"에서 제안:

```text
White: 미방문 (GC 후보)
Gray:  방문했지만 자식 미탐색 (gray 작업; 재귀 구현은 활성 프레임, 큐 구현은 큐)
Black: 자식까지 모두 탐색 완료 (확정 생존)
```

시각화:

```text
초기 graph: Root→A; A→B; B→C; A→D; D→E
A 처리 후: A=BLACK, B=GRAY, D=GRAY; C/E=WHITE
B 처리 후: A/B=BLACK, C/D=GRAY; E=WHITE
간선 집합은 그대로 유지

Sweep:
    BLACK → 생존 (WHITE로 리셋)
    WHITE → 회수
```

### 구현: 간단한 Mark-and-Sweep GC

```c
typedef struct Object {
    bool marked;           // GC flag
    struct Object* refs[MAX_REFS];  // 참조 배열
    size_t ref_count;
    // ... 사용자 데이터
} Object;

typedef struct {
    Object* objects[MAX_OBJECTS];
    size_t count;

    Object* roots[MAX_ROOTS];
    size_t root_count;
} GC;

// Mark phase
void mark(Object* obj) {
    if (!obj || obj->marked) return;

    obj->marked = true;  // 발견/생존 표시 (자식 스캔 전이므로 tri-color black 확정은 아님)

    for (size_t i = 0; i < obj->ref_count; i++) {
        mark(obj->refs[i]);  // 재귀적으로 표시
    }
}

void mark_all(GC* gc) {
    for (size_t i = 0; i < gc->root_count; i++) {
        mark(gc->roots[i]);
    }
}

// Sweep phase
void sweep(GC* gc) {
    for (size_t i = 0; i < gc->count; ) {
        Object* obj = gc->objects[i];

        if (!obj->marked) {
            // White: 회수
            free(obj);
            gc->objects[i] = gc->objects[--gc->count];
        } else {
            // Black: 다음 GC를 위해 리셋
            obj->marked = false;
            i++;
        }
    }
}

void gc_collect(GC* gc) {
    mark_all(gc);
    sweep(gc);
}
```

재귀 mark에서 `marked=true`는 BLACK 확정이 아니라 발견/생존 표시다. 아직 자식 스캔을 마치지 않은 활성 재귀 프레임이 gray 작업을 담당하며, 자식까지 모두 스캔한 뒤에야 tri-color 의미의 black이 된다. 이 STW 구현은 그래프가 변하지 않으므로 gray를 별도 큐로 두지 않아도 된다.

### 장점: 순환 참조 해결

```c
// Reference counting으로 누수되던 케이스
Object* a = gc_alloc(gc);
Object* b = gc_alloc(gc);

a->refs[0] = b;  // A → B
b->refs[0] = a;  // B → A (순환!)

// Root에서 제거
remove_root(gc, a);
remove_root(gc, b);

gc_collect(gc);
// Mark: A와 B 모두 root에서 도달 불가능 → WHITE
// Sweep: 모두 회수! ✓
```

### 기본 구현의 단점: Stop-the-World

가장 단순한 mark-and-sweep은 root 집합과 객체 그래프가 변하지 않게 mutator를 멈춘 뒤 mark와 sweep을 수행한다:

```text
프로그램 실행:  ━━━━━╸┊┊┊┊┊┊┊┊┊┊┊┊┊╺━━━━━
GC 실행:            ┗━━━━━━━━━━┛
                     모든 스레드 정지
```

정지 시간은 전체 힙 크기 하나가 아니라 root 수, 도달 가능한 live set, 객체·포인터 밀도, 메모리 대역폭, sweep 방식에 좌우된다. 큰 live graph를 한 번에 추적하면 프레임 예산을 넘는 pause가 생길 수 있다. 다만 mark-and-sweep이라는 분류가 곧 모든 단계를 stop-the-world로 수행한다는 뜻은 아니다. concurrent·incremental collector도 같은 도달 가능성 원리를 쓰면서 일부 단계를 mutator와 겹치거나 여러 구간으로 나눈다.

### 스택 프레임과 managed heap의 역할

호출 스택의 저장 공간은 함수 진입·복귀와 ABI에 따라 관리되고, tracing GC가 개별 지역 변수를 `free`하지는 않는다. 대신 GC는 stack map이나 보수적 scan을 통해 스택·레지스터 안의 참조를 root로 찾는다.

```c
void func() {
    int x = 10;           // 스택: 자동 관리
    Object* obj = alloc();  // 힙: GC 필요

    // 함수가 끝나면 스택 프레임은 호출 규약에 따라 사라진다.
    // obj 참조가 root에서 사라져도 객체 회수 시점은 collector 정책에 달려 있다.
}
```

최적화된 native 함수는 여러 지역 변수의 공간을 함수 진입 시 한 번에 확보하고 복귀 시 한 번에 되돌릴 수 있다.

```asm
sub rsp, frame_size
; 함수 본문
add rsp, frame_size
```

그러나 “지역 변수 하나당 두 명령·1 cycle”이라는 모델도 정확하지 않다. compiler가 red zone, frame-pointer omission, register allocation, escape analysis를 적용할 수 있고 prologue/epilogue는 호출 규약과 unwind 정보에 따라 달라진다. managed heap의 bump-pointer allocation도 fast path에서는 소수 명령일 수 있지만, slow path의 refill·GC·safepoint가 tail latency를 만든다. stack과 heap의 차이는 고정 cycle 수보다 개별 해제 불필요, lifetime 규칙, escape와 collection 경로에 있다.

## 4. Generational GC: 약한 세대 가설

### 관찰: 대부분의 객체는 짧게 산다

1984년 David Ungar의 "Generation Scavenging"은 핵심 통찰을 제시했다:

> "Most objects die young."

이 관찰을 오늘날 'weak generational hypothesis(약한 세대 가설)'라 부른다. 이 명칭 자체는 Ungar의 논문 표현이 아니라 이후 GC 문헌에서 정착한 것이다. 약한 세대 가설은 많은 workload에서 관찰되는 경험 법칙이지 모든 프로그램의 고정 비율이 아니다.

```text
allocation cohort별 생존 곡선:

survival
1.0 |\
    | \____ 장수 객체의 꼬리
    |      \____
0.0 +-------------------> collection age
```

초기 collection에서 생존율이 급격히 떨어지는 workload라면 작은 young 영역을 자주 회수하는 것이 이득이다. cache·scene graph처럼 장수 객체를 대량 생성하거나 phase change에서 한꺼번에 살아남으면 곡선이 달라진다. runtime의 age table, promotion bytes와 allocation profile로 가설이 맞는지 확인한다.

**아이디어: 젊은 객체만 자주 검사하자**

### 세대 구분: Young vs Old

```text
Heap 구조:

┌─────────────────────────┐
│   Young Generation      │ ← 자주 GC (Minor GC)
│   - Eden Space          │
│   - Survivor Spaces     │
├─────────────────────────┤
│   Old Generation        │ ← 드물게 GC (Major GC)
│                         │
└─────────────────────────┘
```

### Java/C#의 Generational GC

**Minor GC (Young generation):**

```java
// 대부분의 객체는 여기서 할당됨
Object temp = new Object();
temp.process();
// temp는 곧 죽음

// Young collection
// - root와 remembered set에서 young 객체의 생존 여부 추적
// - 생존 객체를 survivor space로 복사하거나 정책에 따라 old로 승격
// - 비용은 young 크기보다 실제 생존량·root/remembered set·copy 대역폭에 크게 좌우
```

**Old/Major/Full collection:**

```java
// old 영역의 회수·압축이 필요한 cycle
// collector에 따라 concurrent mark, mixed region collection,
// stop-the-world full compaction의 의미가 서로 다르다.
```

`major GC`와 `full GC`는 런타임마다 같은 용어가 아니다. 로그의 collector phase와 cause를 확인해야 하며, “minor 10ms / full 100ms~1s” 같은 숫자는 live set·heap·하드웨어를 제시한 benchmark 결과로만 의미가 있다.

### Promotion: 세대 승격

객체가 여러 번 GC에서 살아남으면 Old generation으로 이동:

```text
Age  0 (Eden):      [obj1][obj2][obj3]
                     ↓
Minor GC
                     ↓
Age  1 (Survivor):  [obj1][obj2]  (obj3 죽음)
                     ↓
Minor GC
                     ↓
Age  2 (Survivor):  [obj1]        (obj2 죽음)
                     ↓
Minor GC
                     ↓
Age 15 (threshold 예시)
                     ↓
Old Generation:     [obj1]  (Promoted!)
```

HotSpot `MaxTenuringThreshold`의 기본값은 15지만 실제 승격 시점은 survivor 목표량과 age table 분포에 따라 collector가 동적으로 조정하며, G1의 humongous allocation처럼 Eden→Survivor→Old 흐름을 따르지 않는 예외도 있다. 모든 JVM·collector에 공통된 고정 15회 규칙이 아니다.

### 문제: Old → Young 참조

만약 Old 객체가 Young 객체를 참조하면?

```text
Old:   [A] ────→ [B] (Young)

Minor GC가 Young만 스캔한다면?
- B가 A에서 참조됨을 모름
- B를 잘못 회수할 수 있음!
```

### Write Barrier: 세대 간 참조 추적

해결책: 포인터 대입을 **가로채서** 추적한다.

**Card Table:**

```text
Old Generation을 작은 카드로 분할:

Old: [card0: 512B][card1: 512B][card2: 512B]...

Card Table: [0][0][1][0][0]...  (1 = dirty)
            ↑  ↑  ↑
            카드 단위로 Young 참조 여부 표시
```

포인터 대입 시:

```c
// obj는 Old generation의 객체
obj->field = young_obj;  // Young 객체 할당

// Write barrier (컴파일러가 자동 삽입)
if (is_old(obj) && is_young(young_obj)) {
    // slot-card 방식: 대입한 필드 slot이 놓인 card를 dirty로 표시한다.
    mark_card_dirty(&card_table, &obj->field);
}
```

Minor GC 시:

```c
// 1. Young generation의 root 스캔
// 2. Dirty cards 스캔 (Old → Young 참조)
// 3. Young generation만 마킹

for (int i = 0; i < card_table.size; i++) {
    if (card_table[i] == DIRTY) {
        scan_card(i);  // 이 card 안의 참조 slot들을 스캔 (slot-card 방식)
    }
}
```

card를 표시하는 주소와 `scan_card`가 스캔하는 범위는 같은 계약이어야 한다. 위 예시는 필드 slot이 놓인 card를 표시하고 그 slot-card를 스캔하는 방식이다. owner 객체 시작 card를 표시하는 구현이라면 `scan_card`가 그 객체 전체를 스캔해야 하며, 이 경우 owner 주소 자체는 오류가 아니다.

### Remembered Set: 대안 구현

G1 GC (Java 7+)는 카드 테이블을 write barrier의 기록 수단으로 그대로 쓰면서, 그 카드들을 region별 remembered set으로 정리해 회수 대상 region만 독립적으로 수집할 수 있게 한다:

```text
Region별로 "누가 나를 참조하는가" 리스트:

Region 1: {Region 5, Region 12}
Region 2: {Region 3}
...
```

장점:
- 참조를 담은 카드만 골라 스캔 (카드 전체 스캔 불필요)
- Region 단위 독립적 GC 가능

단점:
- 메모리 오버헤드 (포인터 리스트 저장)

### 성능 비교

GC 계열을 고정 시간과 퍼센트로 비교할 수는 없다. 같은 collector도 live set과 allocation rate가 바뀌면 결과가 역전된다.

| 구조 | 줄이려는 비용 | 새로 드는 비용 | 반드시 측정할 값 |
|---|---|---|---|
| 단순 non-generational | 구현 복잡도·barrier | 한 번에 전체 그래프를 보는 pause | live bytes, mark/sweep 시간 |
| generational | 대부분의 짧은 수명 객체를 작은 young 영역에서 회수 | remembered set, write barrier, promotion | minor/major 빈도, promotion rate |
| region 기반 | 회수 대상을 작은 region 집합으로 선택 | region 메타데이터, remembered set, evacuation 실패 | region별 live ratio, evacuation 시간 |
| concurrent | mark·relocation 일부를 mutator와 겹침 | barrier, 동시 CPU·메모리 대역폭, 여유 힙 | pause 분포, concurrent cycle 시간, allocation stall |
| incremental | 작업을 여러 slice로 분할 | slice bookkeeping, 총 작업 증가 가능 | 프레임별 slice, 완료까지 걸린 프레임 수 |

Generational GC는 약한 세대 가설이 맞는 워크로드에서 young collection의 대상 집합을 줄인다. 장수 객체가 많거나 promotion이 폭주하면 이 이점이 작아지고 old collection 비용이 드러난다. 따라서 “10배 감소”는 알고리즘의 보장이 아니라 특정 workload의 측정 결과여야 한다.

## 5. Concurrent/Incremental GC: Stop-the-World 정지 축소

### 문제: 긴 정지는 프레임 deadline을 넘긴다

```text
60 FPS 게임의 프레임 예산: 16.67ms

GC 정지 100ms는 60Hz 프레임 구간 약 6개에 해당
→ 실제 표시 결과는 엔진의 frame pacing과 스케줄링에 따라 달라지지만 큰 hitch가 된다
```

### Concurrent GC: 애플리케이션과 병렬 실행

**아이디어:** mark나 relocation의 많은 작업을 별도 스레드에서 수행해 애플리케이션과 겹친다. root 스캔, safepoint, 일부 재배치·reference 처리에는 여전히 짧은 stop-the-world 구간이 남을 수 있다.

```text
App Thread:  ━━━━━━━━━━━━━━━━━━━━━━━━━━
GC Thread:      ┗━━━━━━━━━━━┛
                동시 실행 (Concurrent)
```

**문제:** 앱이 객체를 수정하는 동안 GC가 마킹하면?

```text
GC가 A를 BLACK으로 표시:
    [A:BLACK] → [B:WHITE]

앱이 참조 변경:
    [A:BLACK] → [C:WHITE]  (B 연결 끊김)
    [B:WHITE] (이제 도달 불가능)

GC가 C를 도달 불가능으로 오인해 회수할 수 있음 → 버그!
(C는 A에서 도달 가능하지만 아직 WHITE이고 A는 이미 BLACK이라 재스캔되지 않음)
```

### Tri-color Invariant: 정확성 보장

정확성의 기준은 tri-color 불변식이다(Dijkstra et al. 1978). 강한 불변식은 black 객체가 white 객체를 직접 가리키는 상태를 허용하지 않고, 약한 불변식은 black이 가리키는 white 객체가 어떤 gray 객체에서 white 사슬을 따라 도달 가능하기만 하면 허용한다.

둘 중 하나를 유지하기 위한 두 가지 전략 — incremental update는 강한 불변식을, SATB는 약한 불변식을 보존한다:

### Incremental Update (증분 갱신)

**원칙:** Black 객체가 White를 가리키면, Black을 Gray로 되돌린다.

```c
// Write barrier
void write_barrier(Object* obj, Object* new_ref) {
    if (is_black(obj) && is_white(new_ref)) {
        set_gray(obj);  // 재스캔 대상으로 되돌림
    }
}
```

예시:

```text
대입 전: A.field = B
대입 후: A.field = C
barrier: A를 재스캔 대상으로 GRAY 처리
도식: A:GRAY ──→ C:WHITE
```

**사용:** HotSpot의 역사적 CMS가 incremental-update 계열 barrier를 사용한 대표 사례다. 현대 .NET background GC도 generation 2 작업의 상당 부분을 전용 스레드에서 수행하지만, 세대 0·1 foreground collection은 managed thread를 정지시킬 수 있다. 런타임 이름이 비슷하다고 동일한 barrier와 phase 구조라고 가정하면 안 된다.

### Snapshot-at-the-Beginning (SATB)

**원칙:** White 객체로의 참조가 끊어지면, 그 객체를 기록한다.

```c
// Write barrier
void satb_write_barrier(Object* obj, Object* old_ref) {
    if (is_white(old_ref)) {
        push_to_satb_queue(old_ref);  // "죽어도 이번 GC에선 살려둠"
    }
}
```

예시:

```text
초기:
    [A:BLACK] → [B:WHITE]

앱이 A->field = C 실행 (B 연결 끊김):

Write barrier:
    SATB Queue: [B]  (B를 보존)

GC 완료 후:
    B는 이번 GC에서 생존 (다음 GC에서 회수 가능)
```

이 그림의 `A:BLACK → B:WHITE`는 강한 불변식의 안정 상태가 아니라, SATB의 약한 불변식(추가 gray 경로/큐 불변식) 전제 아래에서 일시적으로 허용되는 장면이다.

**사용:** G1 GC (Java), Shenandoah GC

**차이:**

| 항목 | Incremental Update 계열 | SATB 계열 |
|---|---|---|
| 보존하려는 관점 | 현재 그래프의 새 간선을 놓치지 않음 | collection 시작 시점의 그래프를 보존 |
| 대표 barrier 동작 | 새 참조 대상을 mark하거나 source를 다시 scan | 덮어쓰기 전의 old reference를 기록 |
| floating garbage | 구현과 mutation 패턴에 따라 발생 | 시작 시 살아 있던 객체를 cycle 끝까지 살릴 수 있음 |

barrier 비용은 store 빈도, card/queue 구현, compiler 최적화와 mutator 패턴에 따라 달라진다. SATB가 언제나 더 싸고 incremental update가 언제나 덜 보수적인 식의 고정 순위는 없다.

**Floating garbage:** GC 도중 죽었지만 다음 GC까지 살아남는 객체

### ZGC와 Shenandoah: 낮은 pause를 목표로 한 concurrent compaction

ZGC와 Shenandoah는 mark뿐 아니라 relocation/compaction의 많은 부분을 mutator와 겹쳐, pause가 live heap 크기에 비례해 커지는 경로를 줄인다. OpenJDK 문서는 ZGC의 최대 pause 목표를 1ms 미만으로 설명하지만 이는 모든 JVM·플랫폼·워크로드에 대한 실시간 보장이 아니다. safepoint 도달, OS scheduling, root 수, reference processing과 메모리 부족 조건은 관측 pause를 늘릴 수 있다.

**기법:**

1. **Colored Pointers / Colored Object Pointers:** 포인터 또는 참조 표현의 일부 비트를 GC 상태와 주소 해석에 사용

```text
개념적 표현:
[address 또는 object offset][mark/remap/metadata bits]
```

정확한 비트 수와 인코딩은 ZGC 세대와 JVM 버전, 플랫폼의 가상 주소 폭에 따라 바뀌는 구현 세부 사항이다. 하나의 `42/4/18` 도식을 ABI처럼 외우면 안 된다.

2. **Load Barriers:** 포인터를 읽을 때 GC 상태 확인

```c
// 축소 모형: 처음 읽은 참조가 아직 slot의 현재 값일 때만 self-heal한다.
Object* load(Object** slot) {
    Object* observed = atomic_load_explicit(slot, memory_order_acquire);
    Object* moved = ResolveForwarding(observed);
    if (moved != observed) {
        Object* expected = observed;
        atomic_compare_exchange_strong_explicit(
            slot, &expected, moved,
            memory_order_acq_rel, memory_order_acquire);
    }
    return moved;
}
```

이 예시는 동시 collector의 완전 구현이 아니라 self-heal 계약을 보이기 위한 축소 모형이다. 처음 읽은 참조가 아직 slot의 현재 값일 때만 갱신하며, 새 주소 forwarding의 publication, 옛 객체의 수명/reclamation, 반환된 참조가 언제까지 유효한지, 같은 관측값이 재등장하는 ABA는 원 collector의 phase/read semantics와 함께 별도로 증명해야 한다. CAS 한 줄로 ZGC/Shenandoah 같은 실제 collector가 안전해진다고 주장하지 않는다.

3. **Concurrent Relocation:** 객체 이동을 병렬로 수행

```text
App:   [A] [B] [C] [D] [E]
        ↓
GC가 compaction:
App:   [A] [_ ] [C] [B] [E]  (B 이동)
       ↑        ↑    ↑
       forwarding pointer 업데이트
```

**결과를 읽는 법:** 큰 힙에서도 짧은 pause를 목표로 설계되지만, 낮은 지연을 위해 concurrent CPU 시간과 메모리 headroom을 더 사용한다. 평균이 아니라 p95/p99/max pause, allocation stall, concurrent cycle이 allocation rate를 따라잡는지를 같은 workload에서 측정한다.

## 6. 게임에서의 GC: Unity, Unreal의 선택

### Unity: Boehm-Demers-Weiser GC (C# Mono)

Unity는 Mono 런타임의 **Boehm GC**를 사용한다 (incremental GC 도입 이전의 기본 동작):

**특징:**

- Non-generational Mark-and-Sweep
- Conservative GC: 정확한 타입 정보 없이 "포인터 같은" 값을 모두 추적
- Stop-the-World

**문제 (실제 힙 할당이 발생하는 패턴):**

```csharp
void Update() {  // 매 프레임
    // string 연결은 새 string 객체를 힙에 만든다.
    string label = "Score: " + score;  // 힙 할당

    // boxing: int를 object로 담으면 힙 할당.
    object boxed = score;

    // LINQ는 iterator/closure 객체를 매 프레임 만든다.
    var alive = enemies.Where(e => e.hp > 0).ToList();

    // foreach가 IEnumerator를 힙에 만드는 컨테이너도 있다.
}

// 프레임 도중 GC 실행:
Frame:  ━━━━━╸┊┊┊┊┊┊┊┊╺━━━━━  (16ms)
             GC (50ms)
             ↓
결과: 프레임 드롭, 버벅임
```

`Vector3`처럼 `struct`로 정의된 값 타입은 스택에 놓이거나 다른 객체에 인라인되므로 그 자체로 GC 압력을 만들지 않는다. GC가 발생하는 건 위 예시처럼 `class` 인스턴스, `string`, `object`로의 boxing, 람다 캡처, LINQ 같은 참조 타입 할당 때문이다.

**Unity의 권장 패턴:**

1. **객체 풀링:**

```csharp
// 나쁨
void SpawnBullet() {
    GameObject bullet = Instantiate(bulletPrefab);
}

// 좋음
ObjectPool<GameObject> bulletPool = new ObjectPool<GameObject>();

void SpawnBullet() {
    GameObject bullet = bulletPool.Get();  // 재사용
}
```

2. **값 타입과 임시 할당의 위치 확인:**

```csharp
// 지역 값 타입은 JIT/IL2CPP가 레지스터나 스택에 둘 수 있다.
Vector3 position = new Vector3(1, 2, 3);

// 참조 타입 인스턴스는 일반적으로 managed heap에 놓인다.
MyClass obj = new MyClass();  // GC 압력
```

C# `struct`라고 언제나 스택에 놓이는 것은 아니다. 클래스의 필드, 배열 원소, boxing된 값은 힙 객체 안이나 별도 박스에 저장될 수 있다. 목표는 `struct`라는 키워드가 아니라 profiler에서 실제 managed allocation과 boxing을 없애는 것이다.

3. **수동 GC 트리거:**

```csharp
void OnLoadingScreen() {
    // 측정 결과 이 지점의 blocking collection이 허용될 때만 사용
    GC.Collect();
}
```

`GC.Collect()`는 full blocking collection을 강제할 수 있어 로딩 화면에서도 시간 예산을 넘길 수 있다. 호출 전후의 live set과 pause를 측정하고, 기본 정책보다 낫다는 증거가 있을 때만 제한적으로 사용한다.

### Unity 2019.1+: Incremental GC

Unity 2019.1에서 실험적으로 도입되었고 이후 기본 옵션이 되었다.

```csharp
// Project Settings
PlayerSettings.gcIncremental = true;

// 개념적 실행:
Frame N:     bounded mark slice
Frame N + 1: 다음 mark slice 또는 rescan
...
Cycle end:  sweep/finalization phase
```

incremental mode는 mark 작업을 여러 프레임의 짧은 slice로 나누어 한 번의 큰 spike를 줄인다. `50ms → 5ms × 10`처럼 정확히 균등 분할된다는 보장은 없고, reference mutation이 많으면 재스캔과 write barrier 비용이 늘거나 non-incremental collection으로 폴백할 수 있다. Unity Profiler에서 프레임별 GC slice와 `GC Alloc`, 전체 cycle 완료 시간을 함께 본다.

### Unreal Engine: UObject GC + 네이티브 수동 관리

Unreal은 메모리 관리를 두 영역으로 나눈다. UObject(게임 로직 객체)는 **mark-and-sweep GC**로 관리하고, 그 외의 C++ 네이티브 메모리와 리소스는 RAII와 수동 new/delete로 관리한다.

```cpp
// reflection/GC에 노출하는 UObject 참조는 TObjectPtr로 표현한다.
UCLASS()
class AMyActor : public AActor {
    UPROPERTY()
    TObjectPtr<UObject> ManagedObject;

    UObject* RawPointer;     // UPROPERTY/FGCObject 등으로 보고하지 않아 GC root graph에 없음
};
```

**Unreal UObject GC의 특성:**

- UObject는 mark-and-sweep 계열 reachability analysis로 관리된다. 일반 수집에는 UObject 처리 정지 구간이 생길 수 있다.
- incremental reachability analysis는 여러 프레임의 soft time limit으로 mark 작업을 나누는 별도 기능이다. 최신 공식 문서에서도 experimental이며, `TObjectPtr` 전환과 write barrier가 필요하고 지정한 시간 한도를 넘을 수 있다.
- 해당 incremental 기능은 문서상 완전한 thread safety를 보장하지 않아 worker thread에서 UObject graph를 변경하는 구성에 제약이 있다. “UE5면 기본 incremental+parallel”로 일반화할 수 없다.
- 대상은 UObject 계열에 한정. C++ 클래스(struct, 일반 class)와 동적 할당(`new`/`delete`, `TArray`, `TMap` 같은 컨테이너의 내부 버퍼 등)은 GC 대상이 아니다
- 네이티브 리소스(소켓, 파일 핸들, 그래픽 리소스)는 RAII로 관리

**최적화:**

```cpp
// 1. 객체 풀
TArray<AProjectile*> ProjectilePool;

// 2. 엔진 자체 binned allocator
FMallocBinned2

// 3. struct (GC 없음)
struct FVector {
    float X, Y, Z;
};
```

### 왜 C++ 게임 엔진은 수명 모델을 여러 개 쓰는가?

1. **프레임 지연:** 큰 stop-the-world 구간은 deadline을 넘길 수 있음
2. **메모리 오버헤드:** GC 메타데이터, 세대 간 추적 구조
3. **배치 제어:** 추적 객체 그래프는 연속 배열과 다른 접근 패턴을 만들 수 있음
4. **콘솔 제약:** 제한된 메모리에서 GC 오버헤드 부담

그렇다고 C++ 엔진이 GC를 전면 회피하는 것은 아니다. Unreal처럼 반사 가능한 gameplay object에는 tracing GC를 쓰고, 렌더·물리 데이터에는 연속 컨테이너와 arena를 쓰며, OS/GPU 자원에는 RAII를 쓰는 혼합이 일반적이다. 자료의 수명과 접근 패턴에 맞는 관리 모델을 고르는 것이 핵심이다.

Mike Acton(당시 Insomniac Games)이 CppCon 2014의 "Data-Oriented Design and C++" 발표에서 강조한 관점도 같은 맥락이다. 프로그래머의 편의를 위한 추상화 비용이 결국 플레이어의 경험으로 전가된다는 것이다.

## 7. 커스텀 GC 구현: 간단한 Mark-Sweep

이 절의 구현은 Bob Nystrom의 ["Baby's First Garbage Collector"](https://journal.stuffwithstuff.com/2013/12/08/babys-first-garbage-collector/)를 따른다. 스택 기반 VM에 mark-sweep을 붙이는 최소 구성이다.

### 완전한 구현 (C)

```c
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define MAX_OBJECTS 1000  // 힙 객체 수 상한; 값 스택 용량 256과 별개

typedef enum {
    OBJ_INT,
    OBJ_PAIR
} ObjectType;

typedef struct sObject {
    ObjectType type;
    bool marked;  // GC flag

    struct sObject* next;  // Free list

    union {
        int value;  // OBJ_INT
        struct {
            struct sObject* head;
            struct sObject* tail;
        } pair;     // OBJ_PAIR
    };
} Object;

typedef struct {
    // 값 스택 계약(depth=stack_size, capacity=256): 0 <= depth <= capacity, push: depth<capacity, pop: depth>=1, pair: depth>=2 (호출자/verifier 보장)
    Object* stack[256];
    int stack_size;

    Object* first_object;  // Linked list of all objects
    int num_objects;
    int max_objects;  // 힙 객체 수 임계값 (값 스택 용량과 별개)
} VM;

VM* new_vm() {
    VM* vm = malloc(sizeof(VM));
    vm->stack_size = 0;
    vm->first_object = NULL;
    vm->num_objects = 0;
    vm->max_objects = 8;  // 초기 threshold (힙 객체 수 기준; 값 스택 계약과 별개)
    return vm;
}

// push: stack_size < 256, pop: stack_size >= 1일 때만 유효하다.
void push(VM* vm, Object* obj) {
    vm->stack[vm->stack_size++] = obj;
}

Object* pop(VM* vm) {
    return vm->stack[--vm->stack_size];
}

// Mark phase
void mark(Object* obj) {
    if (!obj || obj->marked) return;

    obj->marked = true;

    if (obj->type == OBJ_PAIR) {
        mark(obj->pair.head);
        mark(obj->pair.tail);
    }
}

void mark_all(VM* vm) {
    // Root set: 스택의 모든 객체
    for (int i = 0; i < vm->stack_size; i++) {
        mark(vm->stack[i]);
    }
}

// Sweep phase
void sweep(VM* vm) {
    Object** obj = &vm->first_object;

    while (*obj) {
        if (!(*obj)->marked) {
            // White: 회수
            Object* unreached = *obj;
            *obj = unreached->next;
            free(unreached);
            vm->num_objects--;
        } else {
            // Black: 다음 GC를 위해 리셋
            (*obj)->marked = false;
            obj = &(*obj)->next;
        }
    }
}

void gc(VM* vm) {
    int before = vm->num_objects;

    mark_all(vm);
    sweep(vm);

    // Adaptive threshold
    vm->max_objects = vm->num_objects * 2;
    if (vm->max_objects < 8) vm->max_objects = 8;

    printf("GC: %d objects before, %d after, %d collected, next at %d\n",
           before, vm->num_objects, before - vm->num_objects, vm->max_objects);
}

Object* new_object(VM* vm, ObjectType type) {
    if (vm->num_objects >= vm->max_objects) {
        gc(vm);  // 자동 트리거
    }

    Object* obj = malloc(sizeof(Object));
    obj->type = type;
    obj->marked = false;

    // Linked list에 추가
    obj->next = vm->first_object;
    vm->first_object = obj;

    vm->num_objects++;
    return obj;
}

void push_int(VM* vm, int value) {
    Object* obj = new_object(vm, OBJ_INT);
    obj->value = value;
    push(vm, obj);
}

Object* push_pair(VM* vm) {
    // 값 스택 계약: 이 함수는 stack_size >= 2일 때만 유효하다.
    Object* obj = new_object(vm, OBJ_PAIR);
    obj->pair.tail = pop(vm);
    obj->pair.head = pop(vm);
    push(vm, obj);
    return obj;
}

// 테스트
int main() {
    VM* vm = new_vm();

    // 살아있는 객체
    push_int(vm, 1);
    push_int(vm, 2);
    push_pair(vm);  // (1, 2) 페어

    // 죽은 객체 생성 (스택에서 pop)
    push_int(vm, 3);
    push_int(vm, 4);
    push_pair(vm);
    pop(vm);  // 스택에서 제거 (도달 불가능)

    // GC 트리거
    gc(vm);  // (3, 4) 페어가 회수되어야 함

    // 순환 참조 테스트
    push_int(vm, 5);
    push_int(vm, 6);
    Object* a = push_pair(vm);  // (5, 6)

    push_int(vm, 7);
    push_int(vm, 8);
    Object* b = push_pair(vm);  // (7, 8)

    // 순환 참조 생성
    a->pair.tail = b;
    b->pair.tail = a;

    pop(vm);  // b 제거
    pop(vm);  // a 제거 (이제 둘 다 도달 불가능)

    gc(vm);  // 순환 참조도 회수되어야 함 ✓

    return 0;
}
```

### 실행 결과:

```text
GC: 6 objects before, 3 after, 3 collected, next at 8
GC: 8 objects before, 8 after, 0 collected, next at 16
GC: 9 objects before, 3 after, 6 collected, next at 8
```

### 핵심 개념:

1. **Root Set:** 스택이 유일한 root (전역 변수 없음)
2. **Linked List:** 모든 객체를 연결해 sweep 시 순회
3. **Adaptive Threshold:** GC 후 threshold를 동적 조정
4. **순환 참조 해결:** Mark-and-Sweep는 자연스럽게 처리

### 실전 개선 사항:

1. **Free List:** 회수한 메모리를 재사용 (malloc 호출 감소)

```c
Object* free_list = NULL;

void sweep(VM* vm) {
    Object** obj = &vm->first_object;
    while (*obj) {
        if (!(*obj)->marked) {
            Object* unreached = *obj;
            *obj = unreached->next;

            // Free list에 추가 (즉시 free하지 않음)
            unreached->next = free_list;
            free_list = unreached;

            vm->num_objects--;
        } else {
            (*obj)->marked = false;
            obj = &(*obj)->next;
        }
    }
}

Object* new_object(VM* vm, ObjectType type) {
    if (free_list) {
        // 재사용
        Object* obj = free_list;
        free_list = obj->next;
        obj->type = type;
        obj->marked = false;
        obj->next = vm->first_object;
        vm->first_object = obj;
        vm->num_objects++;
        return obj;
    }
    // 새로 할당
    ...
}
```

2. **Generational Extension:**

```c
typedef struct {
    Object* young_gen;
    Object* old_gen;
    int young_gc_count;
} GenerationalVM;

void minor_gc(GenerationalVM* vm) {
    mark_all_from_roots(vm);

    Object** obj = &vm->young_gen;
    while (*obj) {
        if (!(*obj)->marked) {
            // 회수
            ...
        } else if ((*obj)->age++ > PROMOTION_THRESHOLD) {
            // Old로 승격
            promote_to_old(vm, *obj);
        } else {
            // Young에 유지
            (*obj)->marked = false;
            obj = &(*obj)->next;
        }
    }
}
```

## 8. 실전: GC 튜닝과 메모리 압박

### JVM GC 튜닝: 파라미터 이해

```bash
java \
     -Xms512m \
     -Xmx4g \
     -XX:+UseG1GC \
     -XX:MaxGCPauseMillis=20 \
     -Xlog:gc*,safepoint:file=gc.log:time,uptime,level,tags \
     MyApp
```

`MaxGCPauseMillis`는 hard deadline이 아니라 collector가 region 수와 young 크기를 조절할 때 쓰는 soft goal이다. 너무 공격적으로 낮추면 GC 빈도와 concurrent 작업, 필요한 heap headroom이 늘 수 있다. G1이 자동 조절하는 generation 크기를 `NewRatio`·`SurvivorRatio`로 먼저 고정하기보다, allocation rate·live set·pause 원인을 로그에서 확인하는 것이 우선이다.

| 목표와 환경 | 후보 | 검증할 대가 |
|---|---|---|
| 작은 heap·적은 CPU·구현 오버헤드 최소 | Serial GC | 긴 단일 pause가 허용되는가 |
| 처리량 우선·pause 허용 | Parallel GC | full pause 분포 |
| 처리량과 pause의 균형 | G1 | region/remembered-set 비용, pause goal 달성률 |
| 매우 낮은 pause 우선 | ZGC | concurrent CPU, 메모리 headroom, allocation stall |
| 해당 JDK 배포판에서 지원되는 낮은 pause collector | Shenandoah | 지원 버전·플랫폼, throughput/headroom |

heap 크기 `32GB` 같은 단일 문턱으로 collector를 고를 수는 없다. 같은 live set으로 실제 트래픽을 재생해 처리량, p95/p99/max pause, RSS, CPU와 allocation stall을 함께 비교한다.

### GC 로그 분석

아래는 JDK 8 `-XX:+PrintGCDetails` 형식의 예시다. 위 명령의 JDK 9+ 통합 로깅은 줄마다 `[time][level][tags]` 장식이 붙고 phase 분해에는 `-Xlog:gc*=debug`가 필요하며 phase 이름도 다르지만, 읽는 법은 같다.

```text
[GC pause (G1 Evacuation Pause) (young), 0.0045872 secs]
   [Parallel Time: 3.8 ms, GC Workers: 4]
   [GC Worker Start (ms): Min: 1234.5, Max: 1234.6]
   [Ext Root Scanning (ms): 0.2]
   [Update RS (ms): 0.1]
   [Scan RS (ms): 0.3]
   [Code Root Scanning (ms): 0.0]
   [Object Copy (ms): 2.9]  ← 가장 큰 비용
   [Termination (ms): 0.2]
   [GC Worker End (ms): Min: 1238.2, Max: 1238.3]
[Eden: 128.0M(128.0M)->0.0B(112.0M) Survivors: 16.0M->20.0M Heap: 512.5M(1024.0M)->404.5M(1024.0M)]
 [Times: user=0.02 sys=0.00, real=0.00 secs]
```

**분석:**

- **Object Copy 2.9ms:** 이 parallel phase에서 생존 객체 복사가 큰 비용
- **Eden 128M → 0:** Young generation이 가득 참
- **Survivors 16M → 20M:** collection 뒤 survivor 사용량이 4MB 늘었음. 이 한 줄만으로 old 승격량은 알 수 없음
- **Heap 512M → 404M:** 총 108MB 회수

**최적화 방향:**

1. GC 빈도가 높으면 allocation rate와 young collection 사이의 bytes/time을 먼저 본다. heap을 늘리는 것은 pause·RSS와 맞바꾸는 선택이다.
2. copy/evacuation 비용이 크면 survivor·promotion 양과 객체 연령 분포를 확인한다. 장수 객체를 무리하게 “수명 단축”하기보다 불필요한 할당과 복사를 줄인다.
3. Full GC는 old 부족만이 아니라 allocation failure, evacuation failure, explicit GC, metaspace·class unloading 등 여러 원인이 있다. cause 필드와 직전 phase를 보고 분류한다.

### .NET GC 튜닝

```xml
<PropertyGroup>
  <!-- 다중 heap과 GC thread를 쓰는 server GC -->
<ServerGarbageCollection>true</ServerGarbageCollection>

  <!-- generation 2 작업의 상당 부분을 background로 수행 -->
<ConcurrentGarbageCollection>true</ConcurrentGarbageCollection>
</PropertyGroup>
```

```csharp
GCSettings.LatencyMode = GCLatencyMode.SustainedLowLatency;

byte[] buffer = ArrayPool<byte>.Shared.Rent(1024);
try {
    Use(buffer);
} finally {
    ArrayPool<byte>.Shared.Return(buffer);
}
```

server/workstation과 background/non-concurrent는 서로 다른 축이다. background GC 중에도 generation 0·1 foreground collection은 managed thread를 멈출 수 있고, `SustainedLowLatency`도 메모리 압박이나 명시적 collection에서 pause를 금지하지 않는다. 약 85,000바이트 이상 객체가 배치되는 LOH는 기본적으로 매 collection마다 compact하지 않지만, 런타임은 요청에 따라 LOH compaction을 수행할 수 있다. `ArrayPool`은 할당 빈도를 줄이는 대신 큰 배열을 풀에 보존하고 요청보다 큰 배열을 돌려줄 수 있으므로 RSS와 데이터 삭제 요구까지 측정한다.

### 메모리 누수 디버깅

**Java:**

```bash
# Heap dump 생성
jmap -dump:live,format=b,file=heap.bin <pid>

# Eclipse MAT로 분석
# - Leak Suspects Report
# - Dominator Tree (가장 큰 객체)
# - Path to GC Roots (왜 회수 안 되는지)
```

**C#:**

`WeakReference` 하나가 살아 있다는 사실만으로 누수를 증명할 수는 없다. JIT가 지역 변수 수명을 연장했거나 finalizer queue, static cache, event subscription이 객체를 붙잡았을 수 있다. 반대로 한 객체가 회수돼도 같은 종류의 객체가 계속 증가하는 누수는 남는다. heap snapshot 두 개의 type별 retained size를 비교하고, dominator tree와 path to GC roots로 **무엇이 보존하는지** 확인한다. PerfView, `dotnet-gcdump`, `dotnet-dump`, Visual Studio profiler 같은 도구가 이 증거를 제공한다.

### 메모리 압박 시나리오

**시나리오 1: OOM (Out Of Memory)**

```text
Heap 사용률:
100% ┤              ██████████  ← OOM!
     │          ████
     │      ████
 50% │  ████
     │██
  0% └───────────────────────
     시간 →

원인:
1. 메모리 누수
2. -Xmx가 너무 작음
3. GC가 따라잡지 못함
```

**해결:**

1. **Heap dump 분석** (메모리 누수 확인)
2. **GC 로그 확인** (Full GC가 효과 없는지)
3. **힙 크기 증가** (하드웨어 허용 시)
4. **코드 최적화** (불필요한 할당 제거)

**시나리오 2: High GC Overhead**

```text
CPU 사용률:
100% ┤  ██  ██  ██  ██  (GC에 90% 사용)
     │ █  ██  ██  ██  █
 50% │█
     │
  0% └─────────────────────
```

HotSpot의 GC overhead limit 정책(예: Parallel GC의 `-XX:+UseGCOverheadLimit`)은 GC가 CPU의 98%를 쓰고도 2% 미만 회수하면 `OutOfMemoryError: GC overhead limit exceeded`를 던진다. 이 98%/2%는 해당 collector/옵션의 진단 정책이며 모든 JVM·collector에 공통된 일반 pause 보장 규칙이 아니다.

**해결:**

```java
// GC 압력 줄이기
// 나쁨
for (int i = 0; i < 1000000; i++) {
    String s = "Item " + i;  // 매번 할당
    process(s);
}

// 좋음
StringBuilder sb = new StringBuilder();
for (int i = 0; i < 1000000; i++) {
    sb.setLength(0);  // 재사용
    sb.append("Item ").append(i);
    process(sb.toString());
}

// 같은 인덱스를 여러 번 재조회할 때만 이득 (첫 통과에서는 배열 + String 100만 개를 할당하고 전부 보존한다)
String[] cache = new String[1000000];
for (int i = 0; i < 1000000; i++) {
    if (cache[i] == null) {
        cache[i] = "Item " + i;
    }
    process(cache[i]);
}
```


---

## 결론

가비지 컬렉션은 회수 조건, 처리량, 지연과 메모리 headroom의 선택이다. Reference counting은 마지막 강한 참조가 사라질 때 회수할 수 있지만 순환과 증감·소멸 연쇄 비용이 남는다. Mark-and-sweep의 도달 가능성 정의는 순환을 자연스럽게 처리하며, 실제 collector는 generational·region·concurrent·incremental 기법으로 추적 범위와 pause를 바꾼다. 그 대가가 remembered set, write/load barrier, concurrent CPU와 추가 여유 힙이다.

*The Garbage Collection Handbook*이 일관되게 강조하는 결론도 같다. 단일한 최선의 GC 알고리즘은 없으며, 선택은 애플리케이션의 요구사항, 언어, 하드웨어에 달려 있다.

게임은 평균 처리량보다 frame deadline과 tail pause에 민감하다. 그렇다고 millisecond 단위 작업이 모두 실패인 것은 아니다. 해당 프레임의 다른 작업과 합친 총 CPU/GPU 시간, frame pacing, 수집 주기와 p99 hitch가 예산 안에 드는지 판단한다. UObject, managed gameplay object, ECS component, GPU resource처럼 데이터 종류마다 서로 다른 수명 모델을 쓰는 혼합도 자연스럽다.

## 더 읽을거리

### 기술 논문 및 서적 (필독)

- Richard Jones, Antony Hosking, Eliot Moss, ["The Garbage Collection Handbook: The Art of Automatic Memory Management"](https://gchandbook.org/), Chapman & Hall/CRC, 2011 (1판)
 - **바이블**: 모든 GC 알고리즘의 상세 분석 (사이트는 현행 2판2023 기준; 1판의 장 위치는 판본으로 확인)
- David F. Bacon, Perry Cheng, V.T. Rajan, ["A Unified Theory of Garbage Collection"](https://dl.acm.org/doi/10.1145/1028976.1028982), OOPSLA 2004
 - Tracing과 Reference Counting의 이론적 통합
- Paul R. Wilson, ["Uniprocessor Garbage Collection Techniques"](https://dl.acm.org/doi/10.1007/BFb0017182), IWMM 1992
 - GC 기법의 종합 서베이
- David Ungar, ["Generation Scavenging: A Non-disruptive High Performance Storage Reclamation Algorithm"](https://dl.acm.org/doi/10.1145/800020.808261), SIGSOFT/SIGPLAN 1984
  - 세대별 GC의 초기 실용 구현(세대 개념의 원전은 Lieberman & Hewitt, CACM 26(6), 1983)

### 역사 및 기초

- John McCarthy, ["Recursive Functions of Symbolic Expressions and Their Computation by Machine, Part I"](http://www-formal.stanford.edu/jmc/recursive.pdf), Communications of the ACM, 1960
 - 최초의 GC 알고리즘 (LISP)
- Edsger W. Dijkstra et al., ["On-the-Fly Garbage Collection: An Exercise in Cooperation"](https://dl.acm.org/doi/10.1145/359642.359655), Communications of the ACM, 1978
 - Tri-color marking과 concurrent GC의 기초

### 현대 GC 구현

- Christine H. Flood et al., ["Shenandoah: An Open-Source Concurrent Compacting Garbage Collector for OpenJDK"](https://dl.acm.org/doi/10.1145/2972206.2972210), PPPJ 2016
 - Concurrent compaction과 load-reference barrier
- Per Liden, ["ZGC: What's new in JDK 15"](https://malloc.se/blog/zgc-jdk15), 2020
 - Colored pointers와 load barriers
- [G1 GC Documentation](https://docs.oracle.com/en/java/javase/25/gctuning/garbage-first-g1-garbage-collector1.html), Oracle (JDK 25 G1 절)
 - Region-based generational GC

### 게임 엔진 및 실전

- Unity, ["Memory overview"](https://docs.unity3d.com/Manual/performance-memory-overview.html)
  - Unity GC 최적화 가이드
- Unity, ["Incremental Garbage Collection"](https://docs.unity3d.com/Manual/performance-incremental-garbage-collection.html)
 - 프레임 분산, write barrier와 fallback 조건
- Mike Acton, ["Data-Oriented Design and C++"](https://www.youtube.com/watch?v=rX0ItVEVjHc), CppCon 2014
 - 게임에서 GC를 피하는 이유
- Unreal Engine, ["Incremental Garbage Collection"](https://dev.epicgames.com/documentation/en-us/unreal-engine/incremental-garbage-collection-in-unreal-engine)
 - UObject incremental reachability의 설정과 실험 기능 제약

### JVM/CLR GC

- [HotSpot Virtual Machine Garbage Collection Tuning Guide](https://docs.oracle.com/en/java/javase/26/gctuning/), Oracle
 - JVM GC 파라미터 완전 가이드
- Microsoft, [Background garbage collection](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/background-gc)
- Microsoft, [Fundamentals of garbage collection](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/fundamentals)
 - .NET GC의 세대 구조와 회수 조건 기초
- [.NET Garbage Collection](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/), Microsoft
 - CLR GC 내부 구조
- Aleksey Shipilev, ["Shenandoah GC: The Garbage Collector That Could"](https://shipilev.net/talks/devoxx-Nov2017-shenandoah.pdf), Devoxx 2017
 - 실무 벤치마크와 튜닝

### 도구 및 프로파일링

- [JDK Flight Recorder & Mission Control](https://www.oracle.com/java/technologies/jdk-mission-control.html)
 - 프로덕션 레벨 프로파일링
- [Eclipse MAT (Memory Analyzer Tool)](https://www.eclipse.org/mat/)
 - Heap dump 분석
- [PerfView](https://github.com/microsoft/perfview)
 -.NET 메모리 프로파일러
- [dotMemory](https://www.jetbrains.com/dotmemory/)
 - C# 메모리 누수 추적

### 구현 코드

- [OpenJDK HotSpot GC Source](https://github.com/openjdk/jdk/blob/master/src/hotspot/share/gc/shared/ageTable.cpp)
  - tenuring/promotion 등 GC 구현을 읽는 출발점; 저장소 전체 GC를 검증한 것으로 표시하지 않음
- [Boehm-Demers-Weiser GC](https://github.com/ivmai/bdwgc)
 - Conservative GC (Unity에서 사용)
- [Bob Nystrom, "Crafting Interpreters"](https://craftinginterpreters.com/garbage-collection.html)
 - Mark-and-Sweep GC 구현 튜토리얼 (완전한 코드)

### 벤치마크 및 성능 분석

- Gil Tene, ["How NOT to Measure Latency"](https://www.youtube.com/watch?v=lJ8ydIuPFeU), Strange Loop 2015
 - GC 지연 시간 측정의 함정
- Aleksey Shipilev, ["The Black Magic of Java Method Dispatch"](https://shipilev.net/blog/2015/black-magic-method-dispatch/)
  - 메서드 디스패치와 JIT 인라이닝의 실측 분석 (GC 절의 직접 근거가 아니라 배경 읽을거리)
- [Renaissance Benchmark Suite](https://renaissance.dev/)
  - GC도 관측할 수 있는 현대 JVM benchmark suite (GC 전용 benchmark는 아님)

### 대안 및 최신 연구

- ["Rust Ownership System"](https://doc.rust-lang.org/book/ch04-00-understanding-ownership.html)
 - GC 없는 메모리 안전성
- Anton Lorenzen, Daan Leijen, ["Reference Counting with Frame Limited Reuse"](https://www.microsoft.com/en-us/research/publication/reference-counting-with-frame-limited-reuse/), ICFP 2022
  - Reference Counting 최적화
- ["Immix: A Mark-Region Garbage Collector"](https://dl.acm.org/doi/10.1145/1375581.1375586), PLDI 2008
 - Mark-Sweep와 Copying의 하이브리드
