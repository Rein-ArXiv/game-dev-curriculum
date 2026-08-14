## 들어가며

멀티코어 시스템에서 동기화는 피할 수 없는 문제다. 두 코어가 같은 변수를 동시에 갱신하면 어떤 결과가 남는지 알 수 없다. 한 코어가 자료구조를 수정하는 도중 다른 코어가 그 자료구조를 읽으면 일관성이 깨진다. 그래서 락이 필요하다.

문제는 락이 한 가지 종류만 있는 게 아니라는 점이다. 인터럽트 컨텍스트에서는 슬립할 수 없으니 스핀락이 필요하고, 사용자 공간에서 빈번하게 잠그는 자원에는 시스템 콜 없이 동작하는 futex가 필요하다. 읽기가 압도적으로 많은 자료구조에는 RCU 같은 락프리에 가까운 방식이 적합하다. 하드웨어의 캐시 코히런시 프로토콜과 메모리 모델을 이해하지 못하면 락의 비용을 가늠할 수 없다.

이 글은 Linux 커널의 동기화 메커니즘을 하드웨어 레벨부터 파헤친다. 스핀락의 캐시 효과, 뮤텍스의 빠른 경로, 세마포어와 완료 변수, RW 락, futex의 사용자 공간/커널 공간 분할, RCU의 grace period, per-CPU 변수, seqlock까지 차례로 본다. 마지막에는 어떤 상황에 어떤 프리미티브를 써야 하는지 선택 기준을 정리한다.


---

## 목차
1. [스핀락: 바쁜 대기의 구현](#1-스핀락-바쁜-대기의-구현)
2. [뮤텍스: 커널 뮤텍스의 내부](#2-뮤텍스-커널-뮤텍스의-내부)
3. [세마포어와 완료 변수](#3-세마포어와-완료-변수)
4. [Reader-Writer 락](#4-reader-writer-락)
5. [Futex: 사용자 공간 동기화의 핵심](#5-futex-사용자-공간-동기화의-핵심)
6. [RCU: Read-Copy-Update의 혁명](#6-rcu-read-copy-update의-혁명)
7. [Per-CPU 변수: 동기화 없는 동기화](#7-per-cpu-변수-동기화-없는-동기화)
8. [시퀀스 락 Seqlock: 쓰기 우선 동기화](#8-시퀀스-락-seqlock-쓰기-우선-동기화)


---

## 1. 스핀락: 바쁜 대기의 구현

### 왜 스핀락인가?

커널 공간에서는 일반적인 뮤텍스 사용이 제한적이다. 특히 인터럽트 핸들러나 크리티컬 섹션에서는 **슬립할 수 없다**. 이때 필요한 것이 스핀락이다.

```c
typedef struct {
    volatile unsigned int lock;
} spinlock_t;
```

가장 단순한 스핀락 구현을 보자:

```c
void spin_lock(spinlock_t *lock) {
    while (__sync_lock_test_and_set(&lock->lock, 1)) {
        while (lock->lock)  // 읽기만 수행 (캐시 친화적)
            cpu_relax();     // PAUSE 명령어
    }
}

void spin_unlock(spinlock_t *lock) {
    __sync_lock_release(&lock->lock);
}
```

이 코드의 핵심은 **이중 루프** 구조다. 외부 루프는 Test-And-Set으로 락 획득을 시도하고, 내부 루프는 단순 읽기만 수행한다. 왜 이렇게 나눴을까?

### 캐시 코히런시와 MESI 프로토콜

Test-And-Set은 **쓰기 연산**이다. x86-64의 MESI 캐시 코히런시 프로토콜에서 쓰기는:

1. 다른 코어의 캐시 라인을 **Invalidate** 시킨다
2. 버스에 **RFO(Request For Ownership)** 메시지를 전송한다
3. 메모리 버스에 엄청난 트래픽을 발생시킨다

만약 모든 대기 스레드가 동시에 Test-And-Set을 반복하면?

```text
Core 0: Test-And-Set -> 버스 점유
Core 1: Test-And-Set -> 버스 점유
Core 2: Test-And-Set -> 버스 점유
...
-> 버스 포화 (Bus Saturation)
```

내부 루프의 단순 읽기는 **Shared 상태**의 캐시 라인에서 수행되므로 버스 트래픽이 없다. `cpu_relax`는 x86에서 `PAUSE` 명령어로 컴파일되는데, 이는:

- 파이프라인 플러시를 방지 (스핀 루프 최적화)
- 전력 소비 감소
- 하이퍼스레딩 환경에서 다른 하드웨어 스레드에 양보

### Ticket Spinlock과 그 한계: 공정성에서 확장성으로

기본 스핀락은 **starvation** 문제가 있다. 어떤 스레드는 영원히 락을 획득하지 못할 수 있다. 2008~2014년 무렵 x86 리눅스 커널은 Ticket Spinlock으로 이를 해결했다.

```c
typedef struct {
    u16 owner;
    u16 next;
} arch_spinlock_t;

void arch_spin_lock(arch_spinlock_t *lock) {
    u16 ticket = atomic_fetch_add(&lock->next, 1);

    while (READ_ONCE(lock->owner) != ticket)
        cpu_relax();
}

void arch_spin_unlock(arch_spinlock_t *lock) {
    lock->owner++;
}
```

은행 번호표 시스템과 동일하다. `next`는 번호표를 발급하고, `owner`는 현재 서비스 중인 번호를 나타낸다. waiter가 취소되지 않고 modulo ticket 순서의 비교가 유효한 이 단순 모델에서는 ticket 순서로 획득한다.

그러나 ticket spinlock은 코어 수가 늘어날수록 모든 대기 코어가 같은 `owner` cache line을 읽고, unlock 때 그 line의 새 값을 관찰해야 한다. Linux x86의 qspinlock은 `locked`, `pending`, queue tail을 32-bit word에 압축하고 slow path에서 per-CPU MCS node를 이용해 대기열을 만든다. 모든 경합이 완전히 독립 cache line spinning으로 바뀐다고 단순화할 수는 없으며 pending path와 lock word 공유가 남는다. 무경합 fast path는 atomic try-lock으로 짧게 끝날 수 있다.

```c
// kernel/locking/qspinlock.c (간략화, 5.15 기준)
typedef struct qspinlock {
    union {
        atomic_t val;
        struct { u8 locked, pending; u16 tail; };
    };
} arch_spinlock_t;

void queued_spin_lock(struct qspinlock *lock) {
    if (likely(atomic_try_cmpxchg_acquire(&lock->val, &(u32){0}, _Q_LOCKED_VAL)))
        return;                  // 무경합 fast path
    queued_spin_lock_slowpath(lock, /* val */);
}
```

따라서 현재 x86 리눅스의 `spin_lock`을 설명할 때 ticket spinlock을 그대로 쓰는 것은 오래된 모델이다. 본문의 ticket 예제는 "공정성 도입의 역사적 단계"로 보고, 실제 동작은 qspinlock 기준으로 이해한다.

### MCS Lock: NUMA 최적화

Ticket Lock의 문제는? 모든 대기 스레드가 **같은 캐시 라인**(`owner`)을 폴링한다. NUMA 시스템에서 이는 재앙이다.

MCS(Mellor-Crummey and Scott) Lock은 각 스레드가 **자신의 로컬 변수**를 폴링하게 만든다:

```c
struct mcs_lock {
    struct mcs_lock *next;
    int locked;  // 각 스레드의 로컬 플래그
};

void mcs_lock(struct mcs_lock **lock, struct mcs_lock *node) {
    node->next = NULL;
    node->locked = 1;

    struct mcs_lock *prev = xchg(lock, node);

    if (prev != NULL) {
        prev->next = node;
        // 자신의 로컬 플래그를 대기
        while (node->locked)
            cpu_relax();
    }
}

void mcs_unlock(struct mcs_lock **lock, struct mcs_lock *node) {
    if (node->next == NULL) {
        if (cmpxchg(lock, node, NULL) == node)
            return;
        while (node->next == NULL)
            cpu_relax();
    }
    node->next->locked = 0;  // 다음 스레드 깨우기
}
```

각 스레드는 대기 큐에서 **자신의 노드**를 주로 폴링한다. 모든 waiter가 하나의 lock word를 읽고 쓰는 형태보다 공유 캐시 라인 경쟁을 분산하지만, queue tail 교환, predecessor의 `next` 연결과 successor 깨우기에는 여전히 coherence traffic이 생긴다. MCS lock의 장점은 이를 완전히 없애는 것이 아니라 대기자 수가 늘 때 hot spot이 한 캐시 라인에 집중되는 현상을 줄이는 데 있다.

## 2. 뮤텍스: 커널 뮤텍스의 내부

### 스핀락과 뮤텍스의 차이

스핀락은 빠르지만 CPU를 낭비한다. 락이 오래 유지되면? 뮤텍스가 필요하다.

```c
struct mutex {
    atomic_long_t   owner;       // 현재 소유자
    spinlock_t      wait_lock;   // 대기 큐 보호
    struct list_head wait_list;  // 대기 스레드 목록
};
```

리눅스 커널의 뮤텍스는 **하이브리드 접근**을 사용한다:

```c
void mutex_lock(struct mutex *lock) {
    // Fast Path: 락이 비어있으면 즉시 획득
    if (atomic_long_try_cmpxchg_acquire(&lock->owner, 0, current))
        return;

    // Slow Path: 슬립
    __mutex_lock_slowpath(lock);
}

static void __mutex_lock_slowpath(struct mutex *lock) {
    struct mutex_waiter waiter;

    spin_lock(&lock->wait_lock);
    list_add_tail(&waiter.list, &lock->wait_list);
    spin_unlock(&lock->wait_lock);

    for (;;) {
        set_current_state(TASK_UNINTERRUPTIBLE);

        if (atomic_long_try_cmpxchg_acquire(&lock->owner, 0, current))
            break;

        schedule();  // 스케줄러에게 CPU 양보
    }

    set_current_state(TASK_RUNNING);
}
```

Fast Path는 **언락 비용 없는** 원자적 연산이다. 경합이 없으면 단일 CAS로 끝난다. Slow Path에서는:

1. 대기 큐에 자신을 추가
2. `TASK_UNINTERRUPTIBLE` 상태로 전환
3. `schedule` 호출하여 CPU 양보
4. 깨어나면 다시 락 획득 시도

### Optimistic Spinning

커널 뮤텍스는 2.6.30(2009)부터 소유자가 실행 중이면 잠시 스핀하는 adaptive spinning을 도입했고, 3.15(2014)부터는 스피너들을 OSQ(MCS 계열 큐)로 줄 세우는 **MCS 기반 낙관적 스피닝**으로 발전했다:

```c
static bool mutex_optimistic_spin(struct mutex *lock) {
    if (!mutex_can_spin_on_owner(lock))
        return false;

    // MCS 락으로 스피너 큐 구성
    if (!osq_lock(&lock->osq))
        return false;

    while (true) {
        struct task_struct *owner = READ_ONCE(lock->owner);

        // 소유자가 여전히 실행 중이면 스핀
        if (owner && !owner_on_cpu(owner))
            break;

        if (atomic_long_try_cmpxchg_acquire(&lock->owner, 0, current)) {
            osq_unlock(&lock->osq);
            return true;
        }

        cpu_relax();
    }

    osq_unlock(&lock->osq);
    return false;
}
```

핵심 아이디어: 락 소유자가 **현재 실행 중**이라면, 곧 락을 풀 것이라 예상하고 스핀한다. 컨텍스트 스위칭 비용을 피할 수 있다.

## 3. 세마포어와 완료 변수

### 세마포어: 카운팅 동기화

세마포어는 리소스 풀을 관리할 때 유용하다:

```c
struct semaphore {
    raw_spinlock_t  lock;
    unsigned int    count;
    struct list_head wait_list;
};

void down(struct semaphore *sem) {
    unsigned long flags;

    raw_spin_lock_irqsave(&sem->lock, flags);
    if (likely(sem->count > 0)) {
        sem->count--;
        raw_spin_unlock_irqrestore(&sem->lock, flags);
    } else {
        __down(sem);  // 대기 큐에 추가 및 슬립
        raw_spin_unlock_irqrestore(&sem->lock, flags);
    }
}

void up(struct semaphore *sem) {
    unsigned long flags;

    raw_spin_lock_irqsave(&sem->lock, flags);
    if (likely(list_empty(&sem->wait_list)))
        sem->count++;
    else
        __up(sem);  // 대기 스레드 깨우기
    raw_spin_unlock_irqrestore(&sem->lock, flags);
}
```

뮤텍스와의 차이:
- 뮤텍스: 소유권 개념 (획득한 스레드만 해제 가능)
- 세마포어: 카운터 기반 (아무나 up/down 가능)

### 완료 변수 (Completion)

커널에서 자주 등장하는 패턴: "초기화 작업이 끝날 때까지 대기"

```c
struct completion {
    unsigned int done;
    struct swait_queue_head wait;
};

void wait_for_completion(struct completion *x) {
    __wait_for_common(x, MAX_SCHEDULE_TIMEOUT, TASK_UNINTERRUPTIBLE);
}

void complete(struct completion *x) {
    unsigned long flags;

    spin_lock_irqsave(&x->wait.lock, flags);
    x->done++;
    __wake_up_locked(&x->wait, TASK_NORMAL, 1);
    spin_unlock_irqrestore(&x->wait.lock, flags);
}
```

사용 예시:

```c
DECLARE_COMPLETION_ONSTACK(done);

// 스레드 A
kthread_run(worker_fn, &done, "worker");
wait_for_completion(&done);

// 스레드 B (worker_fn)
static int worker_fn(void *arg) {
    struct completion *done = arg;

    // 초기화 작업...
    complete(done);  // 스레드 A 깨우기

    // 나머지 작업...
    return 0;
}
```

## 4. Reader-Writer 락

### 읽기-쓰기 패턴의 최적화

읽기 작업이 쓰기보다 훨씬 많은 경우, 단순 뮤텍스는 비효율적이다. Reader-Writer Lock은:

- 여러 리더가 동시 접근 가능
- 라이터는 배타적 접근

```c
typedef struct {
    atomic_t        cnt;     // 리더 카운트 (음수면 라이터)
    spinlock_t      lock;
    struct list_head wait_list;
} rwlock_t;

void read_lock(rwlock_t *rw) {
    for (;;) {
        int cnt = atomic_read(&rw->cnt);
        if (cnt >= 0 && atomic_cmpxchg(&rw->cnt, cnt, cnt + 1) == cnt)
            break;
        cpu_relax();
    }
}

void read_unlock(rwlock_t *rw) {
    atomic_dec(&rw->cnt);
}

void write_lock(rwlock_t *rw) {
    spin_lock(&rw->lock);  // 라이터 간 배타적 접근

    // cmpxchg는 이전 값을 반환한다. 0이었을 때만 성공이므로
    // 0이 아닌 값이 반환되는 동안(리더가 남아 있는 동안) 재시도한다.
    while (atomic_cmpxchg(&rw->cnt, 0, -1) != 0)
        cpu_relax();  // 모든 리더가 나갈 때까지 대기
}

void write_unlock(rwlock_t *rw) {
    atomic_set(&rw->cnt, 0);
    spin_unlock(&rw->lock);
}
```

하지만 **Writer Starvation** 문제가 있다. 계속해서 리더가 들어오면 라이터는 영원히 대기할 수 있다.

### rwsem: 대기열과 기아 정책을 가진 읽기-쓰기 세마포어

```c
struct rw_semaphore {
    atomic_long_t   count;
    struct list_head wait_list;
    raw_spinlock_t  wait_lock;
};

// count 필드의 비트 레이아웃 (kernel/locking/rwsem.c, 5.x 기준):
// [62:8] 리더 카운트
// [2]    핸드오프(handoff) 비트
// [1]    대기 큐에 태스크 존재
// [0]    라이터 락 획득
```

rwsem은 waiter queue와 handoff·optimistic spinning 정책으로 writer starvation을 줄이려 하지만, 엄격한 FIFO 공정성을 userspace 계약처럼 보장한다고 단정할 수 없다. kernel version, architecture와 owner spinning 조건에 따라 새 reader의 fast path와 waiter handoff가 달라진다.

## 5. Futex: 사용자 공간 동기화의 핵심

### Fast Userspace Mutex의 철학

전통적인 뮤텍스 문제: 시스템 콜 오버헤드. Futex는 **하이브리드 접근**을 사용한다:

- **경합 없음**: 완전히 유저스페이스에서 처리
- **경합 발생**: 커널에 요청

```c
// 유저스페이스 (GCC/Clang __atomic builtin 기준)
// 0: unlocked, 1: locked (경합 없음), 2: locked (대기자 있음)
int futex_lock(int *futex_addr) {
    int expected = 0;
    if (__atomic_compare_exchange_n(futex_addr, &expected, 1,
                                    false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return 0;  // 무경합 fast path

    // 경합 발생: 상태를 2(대기자 있음)로 올리고 커널에 wait 요청
    int c = expected;
    do {
        if (c == 2 ||
            __atomic_compare_exchange_n(futex_addr, &(int){1}, 2,
                                        false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            syscall(SYS_futex, futex_addr, FUTEX_WAIT, 2, NULL, NULL, 0);

        c = 0;
    } while (!__atomic_compare_exchange_n(futex_addr, &c, 2,
                                          false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED));
    return 0;
}

void futex_unlock(int *futex_addr) {
    if (__atomic_fetch_sub(futex_addr, 1, __ATOMIC_RELEASE) != 1) {
        // 1이 아니었다면 대기자가 있는 상태(2)였다는 뜻이므로 한 명을 깨운다
        __atomic_store_n(futex_addr, 0, __ATOMIC_RELEASE);
        syscall(SYS_futex, futex_addr, FUTEX_WAKE, 1, NULL, NULL, 0);
    }
}
```

(`atomic_dec_and_test`는 커널 `atomic_t` API다. 사용자 공간에서는 `__atomic_*` builtin이나 C11 `<stdatomic.h>`로 통일하는 편이 의미 차이를 줄인다.)

Futex 값의 의미:
- 0: 언락 상태
- 1: 락 획득, 대기자 없음
- 2: 락 획득, 대기자 있음 (커널 개입 필요)

### 커널 Futex 구현

커널은 **해시 테이블**로 대기 큐를 관리한다:

```c
struct futex_hash_bucket {
    spinlock_t lock;
    struct plist_head chain;  // 우선순위 리스트
};

static struct futex_hash_bucket futex_queues[1 << FUTEX_HASHBITS];

long do_futex(u32 __user *uaddr, int op, u32 val) {
    union futex_key key;
    struct futex_hash_bucket *hb;
    struct futex_q *q;

    // 가상 주소 -> 물리 페이지 매핑
    get_futex_key(uaddr, &key);

    // 해시 버킷 선택
    hb = hash_futex(&key);

    switch (op) {
    case FUTEX_WAIT:
        // 대기 큐에 추가
        spin_lock(&hb->lock);
        if (*uaddr == val) {  // 재검증 (race 방지)
            plist_add(&q->list, &hb->chain);
            spin_unlock(&hb->lock);
            schedule();  // 슬립
        } else {
            spin_unlock(&hb->lock);
        }
        break;

    case FUTEX_WAKE:
        // 대기 스레드 깨우기
        spin_lock(&hb->lock);
        plist_for_each_entry_safe(q, hb->chain) {
            if (match_futex(&q->key, &key)) {
                wake_up_process(q->task);
                if (--val == 0)
                    break;
            }
        }
        spin_unlock(&hb->lock);
        break;
    }
}
```

핵심은 **가상 주소의 물리 매핑**이다. 서로 다른 프로세스가 공유 메모리를 통해 같은 futex를 사용할 수 있다.

### Futex의 미묘한 버그들

**ABA 문제**:

```c
// Thread A
if (*futex == 2)  // (1)
    futex_wait(futex, 2);  // (2)

// Thread B
*futex = 0;  // (1)과 (2) 사이에 실행
*futex = 2;  // 다시 2로 설정
```

Thread A는 `futex_wait`에서 영원히 대기할 수 있다. 커널의 재검증 로직이 이를 방지한다.

**우선순위 역전**:

```text
High Priority Task -> Futex -> Low Priority Task (락 소유)
                               Medium Priority Task (락과 무관)
```

Medium이 Low를 선점하면? High는 무한 대기. 해결책: **우선순위 상속 (PI Futex)**

```c
syscall(SYS_futex, futex_addr, FUTEX_LOCK_PI, 0);
```

커널은 락 소유자의 우선순위를 일시적으로 상승시킨다.

## 6. RCU: Read-Copy-Update의 혁명

### "읽기는 무료다"

RCU는 Paul McKenney의 천재적 통찰에서 시작되었다: 대부분의 데이터 구조는 **읽기가 압도적으로 많다**. 락 없이 읽을 수 있다면?

RCU의 핵심 아이디어:

1. **읽기**: 락 없이 직접 접근
2. **쓰기**: 복사본을 만들어 수정
3. **업데이트**: 포인터를 원자적으로 교체
4. **해제**: 모든 리더가 끝날 때까지 대기 후 구 데이터 삭제

```c
struct foo {
    int a;
    struct foo __rcu *next;
};

// 읽기 (락 불필요!)
rcu_read_lock();
struct foo *p = rcu_dereference(global_ptr);
do_something_with(p->a);
rcu_read_unlock();

// 쓰기
struct foo *new_fp = kmalloc(sizeof(*new_fp), GFP_KERNEL);
new_fp->a = new_value;
new_fp->next = old_fp->next;

rcu_assign_pointer(global_ptr, new_fp);  // 원자적 포인터 교체

synchronize_rcu();  // 모든 리더 종료 대기
kfree(old_fp);      // 안전하게 해제
```

### Grace Period: 어떻게 모든 리더가 끝났는지 아는가?

`synchronize_rcu`의 구현은 복잡하지만, 개념은 간단하다:

```c
void synchronize_rcu(void) {
    int cpu;

    // 모든 CPU가 최소 한 번의 quiescent state를 거치길 대기
    for_each_online_cpu(cpu) {
        while (!cpu_passed_quiescent_state(cpu))
            schedule_timeout(1);
    }
}
```

**Quiescent State**: RCU 읽기 섹션 밖에 있는 상태. 다음 중 하나:

- 유저 모드 실행
- 아이들 루프
- `rcu_read_unlock` 통과

리눅스 커널은 **계층적 비트맵**으로 수천 개의 CPU를 효율적으로 추적한다:

```c
struct rcu_node {
    spinlock_t lock;
    unsigned long qsmask;  // CPU 비트마스크
    struct rcu_node *parent;
};

// 3-레벨 트리 구조
//        Root
//       /    \
//    Node   Node
//   /  \    /  \
// CPU CPU CPU CPU
```

### RCU 링크드 리스트 순회

전통적인 리스트 삭제:

```c
// 위험한 코드!
list_del(node);
kfree(node);  // 다른 스레드가 아직 읽고 있을 수 있다!
```

RCU 리스트:

```c
list_del_rcu(node);
synchronize_rcu();  // Grace Period 대기
kfree(node);        // 이제 안전
```

`list_del_rcu`는 포인터를 원자적으로 업데이트하지만, 노드는 유지한다:

```c
static inline void list_del_rcu(struct list_head *entry) {
    __list_del_entry(entry);
    entry->prev = LIST_POISON2;  // 디버깅 목적
}
```

읽는 쪽:

```c
rcu_read_lock();
list_for_each_entry_rcu(pos, head, member) {
    // pos->member 접근
}
rcu_read_unlock();
```

`rcu_dereference`는 포인터 load와 그 포인터를 통한 후속 접근 사이의 dependency ordering을 보존한다. 아래처럼 제거된 내부 매크로를 직접 복제하지 말고 커널이 제공하는 RCU API를 사용해야 한다.

```c
struct item *p = rcu_dereference(global_item);
if (p)
    consume(p->value);
```

과거 구현에는 `smp_read_barrier_depends()`가 노출되었지만 Linux 4.15부터 필요한 처리가 `READ_ONCE()` 경로에 흡수되었고 5.9에서 매크로 자체가 제거되었다. `rcu_dereference()`는 컴파일러 최적화와 Alpha 같은 약한 메모리 모델까지 포함한 커널의 계약이다.

### RCU의 변종들

**SRCU (Sleepable RCU)**:

일반 RCU는 `rcu_read_lock` 안에서 슬립 불가. SRCU는 허용한다:

```c
int idx = srcu_read_lock(&my_srcu);
// 여기서 슬립 가능!
mutex_lock(&some_mutex);
srcu_read_unlock(&my_srcu, idx);
```

대신 Grace Period가 길어질 수 있다.

**RCU-Tasks**:

모든 태스크가 최소 한 번 **자발적으로 스케줄**되면 Grace Period 종료. 코드 패칭에 사용된다.

**RCU-Preempt**:

선점 커널에서 RCU 읽기 섹션이 선점되면? 복잡한 상태 추적이 필요하다. `CONFIG_PREEMPT_RCU`로 활성화.

## 7. Per-CPU 변수: 동기화 없는 동기화

### 캐시 라인 경합 제거

가장 빠른 락은 **락이 없는 것**이다. Per-CPU 변수는 각 CPU마다 독립적인 복사본을 유지한다:

```c
DEFINE_PER_CPU(int, my_counter);

// 접근
get_cpu();  // 선점 비활성화 + CPU ID 획득
int *ptr = this_cpu_ptr(&my_counter);
(*ptr)++;
put_cpu();  // 선점 재활성화
```

왜 `get_cpu`가 필요한가? 선점되면 다른 CPU로 이동할 수 있다:

```c
// 위험한 코드!
int *ptr = this_cpu_ptr(&my_counter);  // CPU 0에서 실행
// <-- 여기서 선점되어 CPU 1로 이동
(*ptr)++;  // CPU 0의 변수를 CPU 1에서 수정!
```

### Per-CPU 메모리 레이아웃

컴파일 시:

```c
DEFINE_PER_CPU(int, var);

// 실제로는:
__attribute__((section(".data..percpu"))) int var;
```

부팅 시 커널은 각 CPU마다 `.data..percpu` 섹션을 복사한다:

```text
CPU 0: [var copy 0][other vars...]
CPU 1: [var copy 1][other vars...]
CPU 2: [var copy 2][other vars...]
```

`this_cpu_ptr`의 구현:

```c
#define this_cpu_ptr(ptr) \
    SHIFT_PERCPU_PTR((ptr), per_cpu_offset(raw_smp_processor_id()))

static inline unsigned long per_cpu_offset(int cpu) {
    return __per_cpu_offset[cpu];
}
```

`__per_cpu_offset[cpu]`는 각 CPU의 베이스 오프셋을 저장한다.

### Per-CPU Atomic Operations

x86-64는 특수 명령어를 제공한다:

```c
this_cpu_add(var, 1);  // 원자적 증가

// 어셈블리:
// add %gs:var, 1
```

`%gs` 세그먼트 레지스터는 현재 CPU의 Per-CPU 영역을 가리킨다. 단일 명령어로 완료되므로 **락 없이 원자적**이다 (같은 CPU 내에서).

### Per-CPU 카운터 집계

모든 CPU의 값을 합산하려면?

```c
int total = 0;
for_each_possible_cpu(cpu)
    total += per_cpu(my_counter, cpu);
```

이는 **일관성이 보장되지 않는다**. 집계 중에 다른 CPU가 값을 변경할 수 있다. 로컬 IRQ와 CPU hotplug만 막아서는 다른 CPU의 갱신을 멈추지 못하므로 정확한 동시 snapshot이 되지 않는다.

```c
// 선형화된 총합이 계약이면 update 경로에서 atomic counter를 갱신한다.
atomic64_add(delta, &global_total);

// 통계용 근사 snapshot이면 합산 중 변화 가능성을 API에 명시한다.
for_each_possible_cpu(cpu)
    total += per_cpu(my_counter, cpu);
```

여러 per-CPU 필드의 일관된 snapshot이 필요하면 각 CPU의 seqcount로 retry하거나, update 경로가 참여하는 별도 lock·generation protocol을 설계한다. 모든 CPU를 멈추는 방법은 매우 비싸므로 제한된 진단 용도에만 고려한다.

## 8. 시퀀스 락 (Seqlock): lockless reader와 직렬화된 writer

### Reader-Writer Lock의 반대

Seqlock은 reader가 writer를 막지 않고 일관되지 않은 읽기를 재시도하게 한다.

- 라이터: 내부 spinlock으로 다른 writer와 직렬화되며 reader 완료를 기다리지는 않음
- 리더: 재시도 가능해야 함

```c
typedef struct {
    unsigned sequence;
    spinlock_t lock;
} seqlock_t;

// 쓰기
write_seqlock(&sl);
// 크리티컬 섹션
write_sequnlock(&sl);

// 읽기
unsigned seq;
do {
    seq = read_seqbegin(&sl);
    // 데이터 읽기 (일관성 보장 안 됨)
} while (read_seqretry(&sl, seq));
```

구현:

```c
static inline unsigned read_seqbegin(seqlock_t *sl) {
    unsigned ret;
repeat:
    ret = READ_ONCE(sl->sequence);
    if (unlikely(ret & 1)) {  // 홀수면 쓰기 중
        cpu_relax();
        goto repeat;
    }
    smp_rmb();  // 메모리 베리어
    return ret;
}

static inline int read_seqretry(seqlock_t *sl, unsigned start) {
    smp_rmb();
    return unlikely(sl->sequence != start);
}

void write_seqlock(seqlock_t *sl) {
    spin_lock(&sl->lock);
    ++sl->sequence;  // 홀수로 만들기 (쓰기 중 표시)
    smp_wmb();
}

void write_sequnlock(seqlock_t *sl) {
    smp_wmb();
    ++sl->sequence;  // 짝수로 만들기 (쓰기 완료)
    spin_unlock(&sl->lock);
}
```

시퀀스 번호의 역할:

- **짝수**: 일관된 상태
- **홀수**: 쓰기 진행 중

리더의 재시도 로직:

1. 시작 시퀀스 번호 읽기 (짝수 확인)
2. 데이터 읽기
3. 종료 시퀀스 번호 읽기
4. 시작과 종료가 다르면? 쓰기가 개입했으므로 재시도

### 사용 사례: 시스템 시간

커널의 `gettimeofday`는 seqlock을 사용한다:

```c
struct timekeeper {
    seqlock_t lock;
    struct timespec64 xtime;
    // ...
};

void ktime_get_real_ts64(struct timespec64 *ts) {
    unsigned int seq;

    do {
        seq = read_seqbegin(&tk_core.seq);
        *ts = tk_core.timekeeper.xtime;
    } while (read_seqretry(&tk_core.seq, seq));
}
```

시간 데이터는 읽기가 매우 빈번하고 짧은 writer update 동안 reader가 retry할 수 있어 seqcount 계열과 잘 맞는다. writer도 내부 lock 경쟁과 선점·IRQ 정책의 영향을 받으므로 “절대 지연되지 않는다”는 보장은 아니다. 실제 timekeeping 구현의 자료구조와 API는 kernel version에 따라 확인한다.

### Seqlock의 제약

리더가 **재시도 가능**해야 한다:

```c
// 위험한 사용!
do {
    seq = read_seqbegin(&sl);
    kmalloc(...);  // 재시도 시 메모리 누수!
} while (read_seqretry(&sl, seq));
```

부작용 없는 순수 읽기 작업에만 사용해야 한다.


---

## 결론

어떤 동기화를 써야 하는지는 상황에 따라 다르다. 표로 정리하면 다음과 같다.

| 상황 | 추천 |
|---|---|
| 짧은 크리티컬 섹션, 인터럽트 컨텍스트 | 스핀락 |
| 긴 크리티컬 섹션, 슬립 필요 | 뮤텍스 |
| 읽기 >> 쓰기, 쓰기 지연 가능 | RCU |
| 읽기 >> 쓰기, reader가 짧은 snapshot을 재시도 가능 | Seqcount/Seqlock |
| 읽기 >> 쓰기, 일반적 | RW Semaphore |
| 리소스 카운팅 | 세마포어 |
| 일회성 이벤트 대기 | Completion |
| 사용자 공간 뮤텍스 | Futex |
| CPU별 독립 데이터 | Per-CPU 변수 |

성능 특성은 다음과 같다.

- 스핀락: 대기 중 CPU 소모, 컨텍스트 스위칭 없음
- 뮤텍스: 대기 중 슬립, 컨텍스트 스위칭 비용
- RCU: 읽기 경로의 동기화 비용이 매우 작고, 갱신·grace period·회수가 비쌈
- Seqlock: reader가 writer를 막지 않음, writer끼리는 직렬화되고 reader는 재시도
- Per-CPU: 락 오버헤드 없음, 집계 비용

메모리 배리어도 빠뜨릴 수 없다.

```c
// 컴파일러 배리어 (재배치 방지)
barrier();

// 읽기 메모리 배리어
smp_rmb();

// 쓰기 메모리 배리어
smp_wmb();

// 전체 메모리 배리어
smp_mb();
```

약한 메모리 모델(ARM, POWER)에서는 배리어가 실제 하드웨어 명령어로 변환된다. x86-64는 강한 메모리 모델이라 대부분 컴파일러 배리어로 충분하다.

동기화는 항상 트레이드오프다. 정확성과 성능, 단순성과 확장성, 공정성과 처리량 사이에서 균형을 잡아야 한다. Futex와 RCU는 그 정점에 있는, 수십 년의 연구와 최적화가 녹아든 결과물이다.

## 더 읽을거리

- Robert Love, *Linux Kernel Development*, Chapter 9-10
- Paul E. McKenney, *Is Parallel Programming Hard, And, If So, What Can You Do About It?*
- 커널 소스: `kernel/locking/`, `include/linux/spinlock.h`, `include/linux/mutex.h`, `kernel/rcu/`
- LWN: ["The design of preemptible read-copy-update"](https://lwn.net/Articles/253651/)
- Ulrich Drepper, ["Futexes Are Tricky"](https://www.akkadia.org/drepper/futex.pdf)
- Linux kernel, [Futex Requeue PI](https://docs.kernel.org/locking/futex-requeue-pi.html)
