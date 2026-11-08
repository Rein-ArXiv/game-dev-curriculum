## 들어가며

> "Signals are software interrupts that provide a way to handle asynchronous events."
> — W. Richard Stevens, *Advanced Programming in the UNIX Environment*


터미널에서 `Ctrl+C`를 누르면 프로그램이 종료된다. 백그라운드 프로세스가 비정상적으로 메모리에 접근하면 즉시 종료된다. 타이머가 만료되면 특정 함수가 호출된다. 이 모든 일은 어떻게 가능할까?

프로세스는 대부분의 시간을 자신의 코드를 순차적으로 실행하며 보낸다. 하지만 외부에서 또는 자기 자신이 발생시킨 **비동기적 이벤트**가 발생하면, 프로세스는 현재 실행을 중단하고 미리 등록된 핸들러를 실행해야 한다. 이것이 바로 **시그널(signal)**의 본질이다.

하지만 시그널은 단순하지 않다. 시그널 핸들러 안에서 `printf`를 호출하면 왜 안전하지 않은가? 여러 시그널이 동시에 발생하면 어떻게 되는가? 시그널은 어떻게 스레드와 상호작용하는가? 그리고 왜 `signal`보다 `sigaction`을 사용해야 하는가?

이 글은 UNIX/Linux의 시그널 메커니즘을 깊이 탐구한다. POSIX 시그널 명세부터 Linux 커널의 시그널 전달 메커니즘(`kernel/signal.c`), 시그널 안전성, 실시간 시그널, 멀티스레드 환경에서의 시그널 처리까지, 비동기 통지의 전체 여정을 따라간다.


---

## 목차
1. [시그널이란 무엇인가: 소프트웨어 인터럽트](#1-시그널이란-무엇인가-소프트웨어-인터럽트)
2. [시그널의 종류](#2-시그널의-종류)
3. [시그널 전송: 비동기 통지의 시작](#3-시그널-전송-비동기-통지의-시작)
4. [시그널 핸들링: 비동기 이벤트의 처리](#4-시그널-핸들링-비동기-이벤트의-처리)
5. [시그널 마스크와 Pending 시그널](#5-시그널-마스크와-pending-시그널)
6. [시그널 안전 함수: Async-Signal-Safe](#6-시그널-안전-함수-async-signal-safe)
7. [실시간 시그널: 신뢰성과 큐잉](#7-실시간-시그널-신뢰성과-큐잉)
8. [시그널과 스레드: 복잡한 상호작용](#8-시그널과-스레드-복잡한-상호작용)
9. [실전 예제: 견고한 시그널 처리](#9-실전-예제-견고한-시그널-처리)
10. [시그널의 한계와 대안](#10-시그널의-한계와-대안)
11. [터미널 작업 제어와 PTY](#11-터미널-작업-제어와-pty)


---

## 1. 시그널이란 무엇인가: 소프트웨어 인터럽트

### 시그널의 본질

**시그널(Signal)**은 프로세스에게 **비동기 이벤트가 발생했음을 알리는 소프트웨어 메커니즘**이다. 하드웨어 인터럽트가 CPU를 중단시키듯, 시그널은 프로세스의 정상 실행 흐름을 중단시킨다.

시그널은 다음과 같은 특징을 갖는다:

1. **비동기성(Asynchronous)**: 언제든지 발생할 수 있다
2. **단순성(Simplicity)**: 단일 정수 값만 전달한다 (확장 시그널 제외)
3. **우선순위**: 프로세스의 정상 코드보다 우선 실행된다
4. **제한된 컨텍스트**: 핸들러에서 사용할 수 있는 함수가 제한된다

### 시그널의 역사

시그널은 UNIX 초기부터 존재했다. 1973년의 4th Edition UNIX는 열두 개의 시그널을 번호로만 지원했고, `SIGHUP` 같은 이름 있는 매크로는 1979년 7th Edition의 `signal.h`에서 처음 정의됐다. 그 헤더가 정의한 열다섯 개 중 앞부분은 다음과 같다:

```c
/* 7th Edition UNIX signals (1979) */
#define SIGHUP  1   /* hangup */
#define SIGINT  2   /* interrupt */
#define SIGQUIT 3   /* quit */
#define SIGILL  4   /* illegal instruction */
#define SIGTRAP 5   /* trace trap */
#define SIGIOT  6   /* IOT instruction */
#define SIGEMT  7   /* EMT instruction */
#define SIGFPE  8   /* floating point exception */
```

이후 시그널은 계속 확장되었다. System V와 BSD는 서로 다른 방향으로 발전했고, POSIX.1-1990이 이들을 통합하려 했다. 현대 Linux는 64개의 시그널을 지원한다:

- **표준 시그널 (1-31)**: POSIX에 정의된 전통적 시그널
- **실시간 시그널 (32-64)**: POSIX.1b에서 추가된 신뢰성 있는 시그널

### 시그널 vs 인터럽트

시그널은 "소프트웨어 인터럽트"라고 불리지만, 하드웨어 인터럽트와는 다르다:

| 특성 | 하드웨어 인터럽트 | 시그널 |
|------|------------------|--------|
| 대상 | CPU | 프로세스 또는 특정 스레드 |
| 발생 위치 | 하드웨어 장치 | 커널 또는 다른 프로세스 |
| 핸들러 위치 | 커널 모드 | 사용자 모드 |
| 전달 시점 | CPU/커널이 정한 인터럽트 경계 | 동기 fault 지점 또는 대상 스레드가 전달 가능한 경계에 도달할 때 |
| 컨텍스트 | 커널의 interrupt context | 선택된 사용자 스레드의 문맥 |

## 2. 시그널의 종류

### 표준 시그널

Linux에서 `kill -l` 명령으로 모든 시그널을 확인할 수 있다:

```bash
$ kill -l
 1) SIGHUP       2) SIGINT       3) SIGQUIT      4) SIGILL
 5) SIGTRAP      6) SIGABRT      7) SIGBUS       8) SIGFPE
 9) SIGKILL     10) SIGUSR1     11) SIGSEGV     12) SIGUSR2
13) SIGPIPE     14) SIGALRM     15) SIGTERM     16) SIGSTKFLT
17) SIGCHLD     18) SIGCONT     19) SIGSTOP     20) SIGTSTP
...
```

위 번호는 흔한 Linux x86 계열 출력이다. POSIX는 시그널의 이름과 의미를 주로 규정하며 숫자는 아키텍처와 운영체제에 따라 달라질 수 있다. 파일 형식이나 네트워크 프로토콜에 raw signal 번호를 저장하지 않는다.

주요 시그널을 살펴보자:

#### 프로세스 제어 시그널

**SIGKILL (9번)**
- 프로세스를 즉시 종료한다
- **잡을 수 없고(uncatchable), 무시할 수 없다(unignorable)**
- 핸들러를 등록할 수 없다
- 커널이 강제로 프로세스를 종료한다

```c
/* kernel/signal.c */
int do_sigaction(int sig, struct k_sigaction *act, struct k_sigaction *oact)
{
    /* SIGKILL과 SIGSTOP은 disposition 변경 자체가 거부된다 */
    if (!valid_signal(sig) || sig < 1 || (act && sig_kernel_only(sig)))
        return -EINVAL;
    /* ... */
}
```

**SIGTERM (15번)**
- 프로세스에게 종료를 요청한다
- **잡을 수 있다(catchable)**
- 핸들러에서 정리 작업을 수행하고 종료할 수 있다
- `kill` 명령의 기본 시그널이다

```c
volatile sig_atomic_t terminate_requested = 0;

void sigterm_handler(int sig) {
    terminate_requested = 1;
}

int main() {
    struct sigaction sa = {0};
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);

    while (!terminate_requested)
        run_one_iteration();

    // 일반 실행 문맥에서 정리한다.
    cleanup_resources();
}
```

**SIGINT (2번)**
- 터미널에서 `Ctrl+C`를 누를 때 발생한다
- 사용자가 프로그램을 중단하려는 의도를 나타낸다
- 잡을 수 있다

**SIGQUIT (3번)**
- 터미널에서 `Ctrl+\`를 누를 때 발생한다
- SIGINT보다 강력하다
- 기본 동작은 코어 덤프를 생성하고 종료한다

**SIGSTOP (19번)**
- 프로세스를 일시 정지한다
- SIGKILL처럼 **잡을 수 없고, 무시할 수 없다**
- `fg` 명령으로 재개할 수 있다

**SIGCONT (18번)**
- SIGSTOP으로 정지된 프로세스를 재개한다

#### 오류 시그널

**SIGSEGV (11번)**
- Segmentation Fault
- 잘못된 메모리 접근 시 발생한다
- 페이지 테이블에 없는 주소, 권한 위반 등

```c
int *ptr = NULL;
*ptr = 42;  // SIGSEGV 발생
```

**SIGBUS (7번)**
- Bus Error
- 정렬되지 않은 메모리 접근, 존재하지 않는 물리 주소 접근 등

```c
// 4바이트 정렬이 필요한 아키텍처에서
char buf[10];
int *ptr = (int*)(buf + 1);  // 비정렬 주소
*ptr = 42;  // SIGBUS 발생 가능
```

**SIGFPE (8번)**
- Floating Point Exception
- CPU 산술 trap을 운영체제가 변환한 시그널이다. 정수 나눗셈 0, 명시적으로 활성화한 부동소수점 trap 등이 원인이 될 수 있다.

```c
// C에서 0으로 나누는 식 자체는 undefined behavior다.
// 컴파일러가 실제 divide 명령을 생성하거나 SIGFPE를 보장하지 않는다.
volatile int zero = 0;
volatile int x = 1 / zero;  // Linux/x86 관찰 예시일 뿐 이식 가능한 발생법이 아님
```

IEEE-754 부동소수점 나눗셈은 기본 환경에서 infinity와 상태 flag를 만들고 계속 실행할 수 있으므로 `1.0 / 0.0`이 곧바로 `SIGFPE`를 보낸다고 가정해서도 안 된다.

**SIGILL (4번)**
- Illegal Instruction
- 잘못된 기계어 명령어를 실행하려 할 때

#### 타이머 시그널

**SIGALRM (14번)**
- `alarm` 함수로 설정한 타이머가 만료될 때 발생한다

```c
volatile sig_atomic_t alarm_fired = 0;

void alarm_handler(int sig) {
    static const char msg[] = "Time's up!\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    alarm_fired = 1;
}

int main() {
    struct sigaction sa = {0};
    sigset_t block, old;

    /* 대상 시그널을 먼저 차단해 handler 실행과 대기 사이의 창을 없앤다 */
    sigemptyset(&block);
    sigaddset(&block, SIGALRM);
    sigprocmask(SIG_BLOCK, &block, &old);

    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

    alarm(5);  // 5초 후 SIGALRM 발생

    /* 대기 mask에서 SIGALRM을 해제한 채 원자적으로 기다리고, 깨어나면 조건을 재검사한다 */
    while (!alarm_fired)
        sigsuspend(&old);

    sigprocmask(SIG_SETMASK, &old, NULL);
}
```

#### 자식 프로세스 시그널

**SIGCHLD (17번)**
- 자식 프로세스가 종료되거나 정지될 때 부모에게 전달된다
- `wait`/`waitpid`와 함께 사용된다

```c
volatile sig_atomic_t child_changed = 0;

void sigchld_handler(int sig) {
    int saved_errno = errno;
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;  // 서식화와 로깅은 메인 루프에 별도 통지해서 수행
    child_changed = 1;
    errno = saved_errno;
}

int main() {
    struct sigaction sa = {0};
    sigset_t block, old;

    /* fork 이전에 SIGCHLD를 차단해, 자식이 먼저 종료해도 대기에서 놓치지 않게 한다 */
    sigemptyset(&block);
    sigaddset(&block, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block, &old);

    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);

    if (fork() == 0) {
        _exit(0);  // 자식 종료 -> 부모에게 SIGCHLD
    }

    while (!child_changed)   // 이미 통지됐으면 즉시 통과
        sigsuspend(&old);    // 그 사이 도착한 시그널도 놓치지 않는다

    sigprocmask(SIG_SETMASK, &old, NULL);
}
```

`pause()`로 조건을 기다리면 시그널이 `pause()` 호출 전에 처리됐을 때 lost-wakeup이 생긴다 — 위처럼 대상 시그널을 먼저 차단하고 조건을 검사한 뒤 `sigsuspend`로 원자적으로 기다리면 그 창이 사라진다.

#### 사용자 정의 시그널

**SIGUSR1 (10번), SIGUSR2 (12번)**
- 애플리케이션이 자유롭게 사용할 수 있는 시그널
- 프로세스 간 통신, 설정 재로드 등에 활용된다

```c
volatile sig_atomic_t reload_requested = 0;

void sigusr1_handler(int sig) {
    reload_requested = 1;  // 실제 파일 I/O와 파싱은 메인 루프에서 수행
}

int main() {
    signal(SIGUSR1, sigusr1_handler);
    // ...
}
```

### 실시간 시그널

POSIX.1b는 **실시간 시그널(Real-time signals)**을 도입했다:

```c
int first_rt_signal = SIGRTMIN;
int last_rt_signal = SIGRTMAX;
```

`SIGRTMIN`과 `SIGRTMAX`는 POSIX가 고정 숫자로 정하지 않는다. glibc에서는 threading 구현이 일부 실시간 시그널을 예약할 수 있어 `SIGRTMIN`이 함수형 매크로일 수도 있으므로, 32와 64를 portable ABI로 저장하면 안 된다.

실시간 시그널의 특징:

1. **큐잉(Queuing)**: 같은 번호의 여러 instance를 표준 시그널처럼 한 비트로 합치지 않고 자원 한도까지 큐에 저장한다
2. **순서 보장**: 같은 실시간 시그널의 instance는 생성 순서를 보존하고, 서로 다른 번호가 pending이면 POSIX 우선순위 규칙을 적용한다
3. **추가 데이터**: `sigqueue`로 정수나 포인터를 함께 전달할 수 있다
4. **우선순위**: 번호가 낮을수록 우선순위가 높다

```c
// 실시간 시그널 전송
union sigval value;
value.sival_int = 42;
sigqueue(pid, SIGRTMIN, value);

// 실시간 시그널 수신
volatile sig_atomic_t last_value = 0;
volatile sig_atomic_t value_ready = 0;

void rt_handler(int sig, siginfo_t *info, void *ucontext) {
    last_value = info->si_value.sival_int;  // sig_atomic_t 범위의 값만 기록
    value_ready = 1;
}

struct sigaction sa;
sa.sa_sigaction = rt_handler;
sa.sa_flags = SA_SIGINFO;
sigaction(SIGRTMIN, &sa, NULL);
```

## 3. 시그널 전송: 비동기 통지의 시작

### kill: 시그널의 기본 전송

`kill` 시스템 콜은 프로세스에게 시그널을 전송한다:

```c
#include <signal.h>
int kill(pid_t pid, int sig);
```

**pid 값의 의미:**

```c
kill(1234, SIGTERM);    // PID 1234 프로세스에게 전송
kill(0, SIGTERM);       // 같은 프로세스 그룹의 모든 프로세스에게
kill(-1, SIGTERM);      // 권한이 있는 모든 프로세스에게 (init 제외)
kill(-5678, SIGTERM);   // 프로세스 그룹 5678의 모든 프로세스에게
```

**예시:**

```c
// 자식 프로세스 종료
pid_t child_pid = fork();
if (child_pid < 0) {
    perror("fork");   // pid<0을 그대로 kill(-1, ...)로 넘기지 않는다
    return 1;
} else if (child_pid == 0) {
    // 자식: 무한 루프
    while (1) sleep(1);
} else {
    // 부모: 5초 후 자식 종료
    sleep(5);
    kill(child_pid, SIGKILL);
}
```

### raise: 자기 자신에게 시그널 전송

```c
#include <signal.h>
int raise(int sig);
```

`raise(sig)`의 의미는 단일 스레드 프로세스와 멀티스레드 프로세스에서 다르다.

- 단일 스레드 프로세스: `kill(getpid, sig)`와 같다(프로세스 단위 시그널, 전달 대상 스레드는 커널이 선택).
- 멀티스레드 프로세스: `pthread_kill(pthread_self, sig)`와 같다(시그널은 **호출 스레드 자신**에게 동기적으로 전달된다).

`raise`가 만든 시그널은 호출 스레드를 대상으로 한다. 다만 그 스레드가 시그널을 차단했다면 즉시 handler를 실행하지 않고 thread-pending 상태로 남을 수 있다. `kill(pid, sig)`는 임의의 **특정 스레드**를 고르는 API가 아니라 process-directed signal을 보낸다. 같은 프로세스의 특정 POSIX thread를 대상으로 하려면 `pthread_kill(thread, sig)`를 사용한다.

```c
// 자기 자신(=호출 스레드)에 시그널을 발생
raise(SIGTERM);

// 디버거 중단점
raise(SIGTRAP);

// 같은 의미를 명시적으로 쓴 형태 (멀티스레드 기준)
pthread_kill(pthread_self(), SIGTERM);
```

### sigqueue: 데이터와 함께 전송

실시간 시그널의 장점을 활용하려면 `sigqueue`를 사용한다:

```c
#include <signal.h>
int sigqueue(pid_t pid, int sig, const union sigval value);

union sigval {
    int   sival_int;
    void *sival_ptr;
};
```

**예시: 작업 완료 통지**

```c
// 서버 프로세스
volatile sig_atomic_t completed_job_id = 0;
volatile sig_atomic_t completion_pending = 0;

void job_done_handler(int sig, siginfo_t *info, void *ucontext) {
    completed_job_id = info->si_value.sival_int;
    completion_pending = 1;
}

struct sigaction sa;
sa.sa_sigaction = job_done_handler;
sa.sa_flags = SA_SIGINFO;
sigaction(SIGRTMIN, &sa, NULL);

// 워커 프로세스
void notify_completion(pid_t server_pid, int job_id) {
    union sigval val;
    val.sival_int = job_id;
    sigqueue(server_pid, SIGRTMIN, val);
}
```

### 커널 내부: 시그널 전송 메커니즘

Linux 커널에서 시그널 전송은 `kernel/signal.c`의 `__send_signal_locked`(옛 이름 `__send_signal`)로 구현된다:

```c
/* kernel/signal.c */
static int __send_signal_locked(int sig, struct kernel_siginfo *info,
                         struct task_struct *t, enum pid_type type, bool force)
{
    struct sigpending *pending;
    struct sigqueue *q;

    /* 시그널이 무시되는지 확인 */
    if (sig_ignored(t, sig, force))
        return 0;

    /* 실시간 시그널이 아니고 이미 pending이면 무시 */
    if (legacy_queue(pending, sig))
        goto out;

    /* sigqueue 구조체 할당 */
    q = __sigqueue_alloc(sig, t, GFP_ATOMIC, ...);
    if (q) {
        list_add_tail(&q->list, &pending->list);
        copy_siginfo(&q->info, info);
    }

    /* pending 비트 설정 */
    sigaddset(&pending->signal, sig);

    /* 프로세스 깨우기 */
    complete_signal(sig, t, type);

out:
    return 0;
}
```

**핵심 자료구조:**

```c
/* include/linux/signal_types.h */
struct sigpending {
    struct list_head list;      /* sigqueue의 링크드 리스트 */
    sigset_t signal;            /* pending 시그널 비트셋 */
};

struct sigqueue {
    struct list_head list;
    int flags;
    kernel_siginfo_t info;      /* 시그널 정보 */
    struct ucounts *ucounts;
};
```

**전송 과정:**

1. 시그널이 차단되었거나 무시되는지 확인
2. 실시간 시그널이 아니면 이미 pending인지 확인
3. `sigqueue` 구조체를 할당하고 pending 리스트에 추가
4. `sigpending.signal` 비트셋에 시그널 번호 설정
5. `complete_signal`로 프로세스를 깨우거나 시그널 처리 예약

## 4. 시그널 핸들링: 비동기 이벤트의 처리

### signal: 전통적인 방식

`signal` 함수는 가장 오래된 시그널 핸들러 등록 방법이다:

```c
#include <signal.h>
typedef void (*sighandler_t)(int);
sighandler_t signal(int signum, sighandler_t handler);
```

**예시:**

```c
void handler(int sig) {
    static const char msg[] = "Caught signal\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
}

int main() {
    signal(SIGINT, handler);

    while (1) {
        // 작업 수행
        sleep(1);
    }
}
```

**signal을 새 코드에서 쓰지 않는 이유:**

1. **이식성 부족**: 역사적으로 System V는 핸들러 실행 시 자동 리셋(`oneshot`)이 기본이었고, BSD는 그렇지 않았다. glibc의 `signal`은 현대 Linux에서 sigaction 기반의 BSD 의미론을 제공하지만, 다른 libc/운영체제에서는 여전히 차이가 있다.
2. **명세가 약함**: 시그널 마스크, 재시작 의미(`SA_RESTART`), siginfo 전달 같은 동작을 명시적으로 지정할 수 없다.
3. **정보 부족**: `siginfo_t` 없이 시그널 번호만 받는다. `si_code`, `si_addr`, `si_pid` 같은 컨텍스트가 필요하면 사용할 수 없다.
4. **스레드 의미론 모호**: 멀티스레드 환경에서 어떤 스레드가 핸들러를 받는지 명시적으로 제어하기 어렵다.

따라서 정확한 동작을 명시적으로 지정하고 이식 가능한 코드를 작성하려면 `sigaction`을 사용한다. glibc `signal`이 내부적으로 `sigaction`을 호출하더라도, 코드가 의존하는 의미를 명시적으로 드러낸다는 점에서 권장 인터페이스는 `sigaction`이다.

### sigaction: POSIX 표준 방식

`sigaction`은 disposition, mask, restart 정책과 확장 정보를 명시할 수 있다. 어느 스레드가 process-directed signal을 받을지, handler의 재진입 안전성, 공유 상태 동기화까지 자동으로 해결하는 함수는 아니다.

```c
#include <signal.h>
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);

struct sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, siginfo_t *, void *);
    };                                            // 같은 저장소를 공유
    sigset_t sa_mask;                             // 핸들러 실행 중 차단할 시그널
    int sa_flags;                                 // 플래그
    void (*sa_restorer)(void);                    // 사용 안 함 (glibc 내부용)
};
```

**기본 사용법:**

```c
void handler(int sig) {
    write(STDOUT_FILENO, "Caught SIGINT\n", 14);
}

int main() {
    struct sigaction sa;

    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);

    while (1) pause();
}
```

`pause()`는 이미 처리된 시그널을 다시 통지하지 않는다. 인터럽트로 깨어나 조건을 확인하는 대기라면 위 SIGCHLD/SIGALRM 예처럼 대상 시그널을 차단한 뒤 `sigsuspend`로 기다려 lost-wakeup을 피한다. 이 예는 사용자 입력으로 SIGINT가 비교적 늦게 오는 대화형 데모라 `pause()`로도 동작한다.

### sa_flags: 핸들러 동작 제어

`sa_flags`는 핸들러의 동작을 세밀하게 제어한다:

**SA_SIGINFO**
- `sa_sigaction` 핸들러를 사용한다
- 추가 정보를 받을 수 있다

```c
void extended_handler(int sig, siginfo_t *info, void *ucontext) {
    // 필요한 scalar 정보를 고정 크기 record에 복사한 뒤
    // nonblocking self-pipe로 메인 루프에 전달한다.
    struct signal_record record = {
        .sig = sig,
        .sender_pid = info->si_pid,
        .sender_uid = info->si_uid,
        .code = info->si_code,
    };
    notify_main_loop(&record);  // async-signal-safe하도록 별도 구현
}

struct sigaction sa;
sa.sa_sigaction = extended_handler;
sa.sa_flags = SA_SIGINFO;
sigaction(SIGINT, &sa, NULL);
```

**SA_RESTART**
- Linux/POSIX가 restart 대상으로 정한 일부 blocking 인터페이스를 자동 재시작할 수 있다

```c
struct sigaction sa;
sa.sa_handler = handler;
sa.sa_flags = SA_RESTART;
sigaction(SIGINT, &sa, NULL);

// descriptor 종류와 전송 여부에 따라 재시작되거나 EINTR/부분 길이를 반환할 수 있다.
ssize_t n = read(fd, buf, sizeof(buf));
```

모든 시스템 콜이 재시작되는 것은 아니다. timeout이 설정된 socket, signal wait, 여러 multiplexing API 등은 `EINTR`을 반환할 수 있고, 일부 바이트를 이미 전송했다면 성공한 짧은 길이를 반환할 수 있다. 호출별 명세와 Linux `signal(7)`의 restart 목록을 확인해야 한다.

**SA_NODEFER**
- 핸들러 실행 중에도 같은 시그널을 다시 받을 수 있다 (기본적으로 차단됨)

**SA_RESETHAND**
- 핸들러가 한 번 실행되면 기본 동작으로 리셋된다 (`signal`과 유사)

**SA_NOCLDSTOP**
- 자식이 정지될 때 SIGCHLD를 보내지 않는다

**SA_NOCLDWAIT**
- 자식이 종료될 때 wait 가능한 zombie를 남기지 않도록 요청한다

```c
struct sigaction sa;
sa.sa_handler = SIG_DFL;
sa.sa_flags = SA_NOCLDWAIT;  // 좀비 프로세스 자동 정리
sigaction(SIGCHLD, &sa, NULL);
```

### siginfo_t: 시그널 상세 정보

`SA_SIGINFO` 플래그를 사용하면 `siginfo_t` 구조체로 상세 정보를 받는다:

```c
/* include/uapi/asm-generic/siginfo.h */
typedef struct siginfo {
    int si_signo;    /* 시그널 번호 */
    int si_errno;    /* errno 값 */
    int si_code;     /* 시그널이 발생한 원인 */

    union {
        /* SIGKILL, SIGTERM 등 */
        struct {
            pid_t si_pid;   /* 보낸 프로세스 */
            uid_t si_uid;   /* 보낸 사용자 */
        } _kill;

        /* SIGCHLD */
        struct {
            pid_t si_pid;
            uid_t si_uid;
            int si_status;  /* exit 코드 */
            clock_t si_utime;
            clock_t si_stime;
        } _sigchld;

        /* SIGSEGV, SIGBUS */
        struct {
            void *si_addr;  /* 잘못된 주소 */
        } _sigfault;

        /* 실시간 시그널 */
        struct {
            pid_t si_pid;
            uid_t si_uid;
            sigval_t si_value;  /* 전달된 데이터 */
        } _rt;
    } _sifields;
} siginfo_t;
```

**si_code 값의 의미:**

```c
/* SIGSEGV */
si_code == SEGV_MAPERR  // 주소가 매핑되지 않음
si_code == SEGV_ACCERR  // 권한 위반

/* SIGCHLD */
si_code == CLD_EXITED   // 자식이 exit()으로 종료
si_code == CLD_KILLED   // 자식이 시그널로 종료
si_code == CLD_STOPPED  // 자식이 정지됨

/* 일반 */
si_code == SI_USER      // kill()로 전송됨
si_code == SI_QUEUE     // sigqueue()로 전송됨
si_code == SI_TIMER     // 타이머 만료
si_code == SI_KERNEL    // 커널이 전송
```

**활용 예시:**

```c
void segv_handler(int sig, siginfo_t *info, void *ucontext) {
    // 미리 연 crash fd에 고정 크기 binary record를 write할 수 있다.
    // printf, malloc, symbolization, exit는 handler 안에서 호출하지 않는다.
    write_crash_record_async_safe(sig, info->si_code, info->si_addr);
    _exit(128 + sig);
}

struct sigaction sa;
sa.sa_sigaction = segv_handler;
sa.sa_flags = SA_SIGINFO;
sigaction(SIGSEGV, &sa, NULL);
```

`SIGSEGV` 뒤에는 일반 프로그램 불변식이 이미 깨졌을 수 있다. handler에서 복구를 계속하거나 복잡한 crash report를 만들지 말고, 필요한 최소 정보를 async-signal-safe 경로로 남긴 뒤 종료하는 것이 기본이다. 작은 일반 스택에서 overflow가 발생한 경우를 기록하려면 `sigaltstack`과 `SA_ONSTACK`도 필요하다.

## 5. 시그널 마스크와 Pending 시그널

### 시그널 차단: sigprocmask

프로세스는 특정 시그널을 일시적으로 **차단(block)**할 수 있다:

```c
#include <signal.h>
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
```

**how 값:**

```c
SIG_BLOCK    // set의 시그널들을 차단 목록에 추가
SIG_UNBLOCK  // set의 시그널들을 차단 목록에서 제거
SIG_SETMASK  // 차단 목록을 set으로 교체
```

**sigset_t 조작 함수:**

```c
int sigemptyset(sigset_t *set);           // 빈 집합
int sigfillset(sigset_t *set);            // 모든 시그널
int sigaddset(sigset_t *set, int sig);    // 시그널 추가
int sigdelset(sigset_t *set, int sig);    // 시그널 제거
int sigismember(const sigset_t *set, int sig);  // 멤버십 테스트
```

**예시: critical section 보호**

```c
sigset_t newmask, oldmask;

// SIGINT 차단
sigemptyset(&newmask);
sigaddset(&newmask, SIGINT);
sigprocmask(SIG_BLOCK, &newmask, &oldmask);

// Critical section: SIGINT가 전달되지 않음
update_shared_data();

// 원래 마스크 복원
sigprocmask(SIG_SETMASK, &oldmask, NULL);
```

### Pending 시그널: sigpending

차단된 시그널은 **pending 상태**가 된다. `sigpending`으로 확인할 수 있다:

```c
#include <signal.h>
int sigpending(sigset_t *set);
```

**예시:**

```c
void check_pending() {
    sigset_t pending;
    sigpending(&pending);

    if (sigismember(&pending, SIGINT)) {
        printf("SIGINT is pending\n");
    }
}

int main() {
    sigset_t mask;

    // SIGINT 차단
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    // thread 생성 전이라면 sigprocmask도 가능하지만, thread 프로그램은
    // pthread_sigmask로 모든 thread가 상속할 mask를 먼저 설정한다.
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    printf("Press Ctrl+C...\n");
    sleep(5);

    check_pending();  // SIGINT is pending

    // 차단 해제 -> pending SIGINT가 즉시 전달된다(핸들러 미등록 시 기본 동작인 종료)
    pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
}
```

### 핸들러 실행 중 자동 차단

`sigaction`으로 핸들러를 등록하면, 핸들러 실행 중에는 **같은 시그널이 자동으로 차단**된다:

```c
void handler(int sig) {
    static const char msg[] = "handler\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    // handler가 실행 중인 동안 같은 번호가 생성되면 pending 상태가 된다.
}

struct sigaction sa;
sa.sa_handler = handler;
sigemptyset(&sa.sa_mask);
sa.sa_flags = 0;  // SA_NODEFER가 없으면 자동 차단
sigaction(SIGINT, &sa, NULL);
```

`sa_mask`를 사용하면 추가 시그널도 차단할 수 있다:

```c
struct sigaction sa;
sa.sa_handler = handler;
sigemptyset(&sa.sa_mask);
sigaddset(&sa.sa_mask, SIGQUIT);  // 핸들러 실행 중 SIGQUIT도 차단
sa.sa_flags = 0;
sigaction(SIGINT, &sa, NULL);
```

### 커널 내부: 시그널 마스크

`task_struct`는 차단된 시그널과 pending 시그널을 추적한다:

```c
/* include/linux/sched.h */
struct task_struct {
    /* ... */
    sigset_t blocked;              /* 차단된 시그널들 */
    sigset_t real_blocked;         /* sigtimedwait() 임시 복원용 */
    struct sigpending pending;     /* 이 스레드 전용 pending 시그널 */
    /* ... */
};

/* include/linux/sched/signal.h */
struct signal_struct {
    /* ... */
    struct sigpending shared_pending;  /* 스레드 그룹 공유 pending */
    /* ... */
};
```

프로세스 대상 시그널의 수신 스레드를 고를 때는 차단 마스크만 보는 것이 아니다. 커널의 `wants_signal()`은 종료 중인지, stop/trace 상태인지, 현재 CPU에서 실행 중인지, 이미 처리할 pending 시그널이 있는지도 함께 고려한다. 따라서 "차단하지 않은 첫 스레드"처럼 단순화한 규칙으로 전달 대상을 예측해서는 안 된다.

## 6. 시그널 안전 함수: Async-Signal-Safe

### 재진입성의 문제

시그널 핸들러는 **언제든지** 실행될 수 있다. 만약 프로세스가 `malloc`을 실행하는 도중에 시그널이 발생하면?

```c
void handler(int sig) {
    char *p = malloc(100);  // 위험!
    // malloc()이 이미 실행 중이면 데이터 구조 손상
}

int main() {
    signal(SIGINT, handler);

    char *data = malloc(1000);  // malloc() 실행 중...
    // 이 순간 SIGINT 발생 -> handler 실행 -> malloc() 재진입!
}
```

대부분의 함수는 **재진입 불가능(non-reentrant)**이다:

- **전역 변수 사용**: `errno`, `strtok`의 내부 상태 등
- **내부 잠금**: `malloc`, `printf`는 내부적으로 mutex를 사용
- **정적 버퍼**: `getpwnam`, `strerror` 등

### Async-Signal-Safe 함수

POSIX는 시그널 핸들러에서 호출해도 되는 **async-signal-safe** 함수 목록을 버전별로 정의한다. 대표적인 최소 집합은 `_exit`, `write`, `read`, `close`, `kill`, `sigaction`, 여러 signal-set 조작 함수다. 전체 목록은 대상으로 삼는 POSIX 판의 명세를 확인해야 한다.

POSIX.1-2024에서는 `fork()`가 이 목록에서 빠지고 `_Fork()`가 추가되었다. `fork()`는 `pthread_atfork` handler와 다른 라이브러리 상태를 건드릴 수 있기 때문이다. 오래된 POSIX 목록을 복사해 현재의 portable 계약으로 사용하면 안 된다. lock-free atomic 객체에 대한 C `<stdatomic.h>` 연산은 POSIX.1-2024가 별도 조건 아래 async-signal-safe로 규정한다.

**주목할 점:**

- `printf`, `sprintf` **없음** → `write` 사용
- `malloc`, `free` **없음**
- `pthread_mutex_lock` **없음**

### 안전한 핸들러 작성

**나쁜 예:**

```c
void bad_handler(int sig) {
    printf("Caught signal %d\n", sig);  // 위험!
    free(global_ptr);                    // 위험!
}
```

**좋은 예:**

```c
volatile sig_atomic_t got_signal = 0;

void safe_handler(int sig) {
    got_signal = 1;  // 플래그만 설정
}

int main() {
    struct sigaction sa = {0};
    sa.sa_handler = safe_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    while (!got_signal) {
        // 메인 루프
    }

    // 여기서 정리 작업 (안전)
    printf("Signal received, cleaning up\n");
    cleanup();
}
```

**write를 사용한 출력:**

```c
void handler(int sig) {
    const char msg[] = "Caught SIGINT\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
}
```

### sig_atomic_t

시그널 핸들러와 메인 코드 간 통신에는 `sig_atomic_t`를 사용한다:

```c
#include <signal.h>
typedef /* 구현 정의 */ sig_atomic_t;
```

- **단순 접근 보장**: handler가 읽거나 쓸 때 찢어진 중간 표현을 관찰하지 않는 구현 정의 정수형
- 폭과 부호는 구현 정의이며 `int`와 같다고 가정하지 않는다
- `volatile`과 함께 사용한다

```c
volatile sig_atomic_t flag = 0;

void handler(int sig) {
    flag = 1;  // 원자적 쓰기
}

int main() {
    struct sigaction sa = {0};
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    while (flag == 0) {  // 원자적 읽기
        // 작업
    }
}
```

`volatile sig_atomic_t`는 정상 실행과 그 실행을 interrupt한 handler 사이의 작은 flag에 맞는 최소 도구다. 증가 같은 read-modify-write 전체가 원자적이라는 뜻이 아니며, 여러 worker thread 사이의 C/C++ 메모리 순서나 포인터·구조체 publication을 대신하지 않는다.

**왜 volatile인가?**

컴파일러가 최적화로 `flag` 읽기를 제거하는 것을 방지한다:

```c
// 컴파일러가 이렇게 최적화하면 안 됨:
// if (flag == 0) {
//     while (1) { /* ... */ }  // flag를 다시 읽지 않음
// }
```

## 7. 실시간 시그널: 신뢰성과 큐잉

### 표준 시그널의 한계

실시간 범위가 아닌 표준 시그널은 같은 번호가 여러 번 pending되어도 보통 하나의 pending 상태로 합쳐진다. 번호 범위 자체는 플랫폼마다 다르므로 `1-31`을 portable 구분으로 쓰지 않는다.

```c
// 같은 시그널을 여러 번 전송
for (int i = 0; i < 5; i++) {
    kill(pid, SIGUSR1);
}

// 하지만 핸들러는 1번만 실행될 수 있다!
// 이미 pending이면 추가 instance가 별도 개수로 보존되지 않음
```

커널 구현:

```c
/* kernel/signal.c */
static bool legacy_queue(struct sigpending *signals, int sig)
{
    /* 표준 시그널이 이미 pending이면 큐에 추가하지 않음 */
    return (sig < SIGRTMIN) && sigismember(&signals->signal, sig);
}
```

### 실시간 시그널의 장점

실시간 시그널(`SIGRTMIN`부터 `SIGRTMAX`)은 자원 한도 안에서 instance를 큐잉한다.

1. **큐잉**: 성공적으로 생성된 각 instance가 별도 pending 항목을 가진다
2. **순서 보장**: 같은 번호의 instance는 생성 순서를 보존한다
3. **데이터 전달**: `sigqueue`로 정수/포인터 전달
4. **우선순위**: 번호가 낮을수록 우선순위가 높다

```c
// 5개 시그널 모두 전달됨
for (int i = 0; i < 5; i++) {
    union sigval val;
    val.sival_int = i;
    sigqueue(pid, SIGRTMIN, val);
}

// 다섯 sigqueue가 모두 성공했다면 다섯 instance를 받을 수 있음
void rt_handler(int sig, siginfo_t *info, void *ucontext) {
    enqueue_signal_value_async_safe(info->si_value.sival_int);
}
```

### 실시간 시그널 큐의 크기

실시간 시그널 큐의 크기는 제한되어 있다:

```bash
$ ulimit -i
127529
```

```c
/* include/uapi/asm-generic/resource.h */
#define RLIMIT_SIGPENDING  11  /* max number of pending signals */
```

큐가 가득 차면 `sigqueue`는 실패한다:

```c
union sigval val;
val.sival_int = 42;

if (sigqueue(pid, SIGRTMIN, val) == -1) {
    if (errno == EAGAIN) {
        printf("Signal queue is full\n");
    }
}
```

### 실시간 시그널 활용 예시

**타이머 만료 통지:**

```c
timer_t timerid;
struct sigevent sev;
struct itimerspec its;

// 타이머 만료 시 SIGRTMIN 전송
sev.sigev_notify = SIGEV_SIGNAL;
sev.sigev_signo = SIGRTMIN;
sev.sigev_value.sival_ptr = &timerid;

timer_create(CLOCK_REALTIME, &sev, &timerid);

// 1초 후 만료
its.it_value.tv_sec = 1;
its.it_value.tv_nsec = 0;
its.it_interval.tv_sec = 0;
its.it_interval.tv_nsec = 0;

timer_settime(timerid, 0, &its, NULL);
```

**AIO 완료 통지:**

```c
struct aiocb cb;
memset(&cb, 0, sizeof(cb));
cb.aio_fildes = fd;
cb.aio_buf = buffer;
cb.aio_nbytes = size;

// AIO 완료 시 SIGRTMIN+1 전송
cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
cb.aio_sigevent.sigev_signo = SIGRTMIN + 1;
cb.aio_sigevent.sigev_value.sival_ptr = &cb;

aio_read(&cb);
```

## 8. 시그널과 스레드: 복잡한 상호작용

### 스레드별 vs 프로세스 전역 시그널

멀티스레드 환경에서 시그널은 두 가지로 나뉜다:

**1) 스레드 지향 시그널(Thread-directed)**
- 특정 스레드에게만 전달된다
- `pthread_kill`, `raise`로 전송
- SIGSEGV, SIGFPE, SIGILL 등 (스레드가 직접 발생)

**2) 프로세스 지향 시그널(Process-directed)**
- 프로세스의 아무 스레드에게나 전달될 수 있다
- `kill`로 전송
- SIGINT, SIGTERM, SIGUSR1 등

### 스레드별 시그널 마스크

각 스레드는 **독립적인 시그널 마스크**를 가진다:

```c
int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset);
```

`sigprocmask`와 동일하지만, 멀티스레드 환경에서는 `pthread_sigmask`를 사용해야 한다.

**예시:**

```c
void *thread_func(void *arg) {
    sigset_t mask;

    // 이 스레드는 SIGINT를 차단
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    while (1) {
        // SIGINT가 이 스레드에게는 전달되지 않음
        work();
    }
}

int main() {
    pthread_t signal_tid;
    pthread_t workers[4];
    pthread_create(&signal_tid, NULL, thread_func, NULL);

    // 메인 스레드는 SIGINT를 받을 수 있음
    pause();
}
```

### 프로세스 시그널의 전달 규칙

프로세스 시그널은 다음 규칙으로 스레드를 선택한다:

1. **차단하지 않은 스레드** 중에서 선택
2. 여러 스레드가 가능하면 **임의로** 선택

```c
/* kernel/signal.c — complete_signal()이 스레드 선택에 사용하는 판정 함수 */
static inline bool wants_signal(int sig, struct task_struct *p)
{
    if (sigismember(&p->blocked, sig))
        return false;            /* 차단한 스레드는 제외 */

    if (p->flags & PF_EXITING)
        return false;            /* 종료 중인 스레드는 제외 */

    if (sig == SIGKILL)
        return true;             /* SIGKILL은 무조건 전달 */

    if (task_is_stopped_or_traced(p))
        return false;

    /* 현재 실행 중이거나 pending 시그널이 없는 스레드를 선호 */
    return task_curr(p) || !task_sigpending(p);
}
```

**문제 상황:**

```c
// 모든 스레드가 SIGINT를 차단
pthread_sigmask(SIG_BLOCK, &mask, NULL);

// 메인에서 SIGINT 전송
kill(getpid(), SIGINT);

// 어느 스레드도 받지 못함! pending 상태로 남음
```

### 시그널 전용 스레드

일반적인 패턴은 **시그널 전용 스레드**를 만드는 것이다:

```c
void *signal_thread(void *arg) {
    const sigset_t *set = arg;
    int sig;

    while (1) {
        int rc = sigwait(set, &sig);
        if (rc != 0) {
            log_pthread_error("sigwait", rc);
            continue;
        }

        // 시그널 처리
        switch (sig) {
            case SIGINT:
                printf("Handling SIGINT\n");
                break;
            case SIGTERM:
                printf("Handling SIGTERM\n");
                cleanup_and_exit();
                break;
        }
    }
}

int main() {
    pthread_t signal_tid;
    pthread_t workers[4];
    sigset_t set;

    // 비동기로 처리할 process-control signal만 차단한다.
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    // 시그널 전용 스레드 생성 (차단된 마스크 상속)
    pthread_create(&signal_tid, NULL, signal_thread, &set);

    // 워커 스레드들 생성
    for (int i = 0; i < 4; i++) {
        pthread_create(&workers[i], NULL, worker_thread, NULL);
    }

    pthread_join(signal_tid, NULL);
    for (int i = 0; i < 4; ++i)
        pthread_join(workers[i], NULL);
}
```

`SIGSEGV`, `SIGBUS`, `SIGFPE`처럼 특정 명령 실행에서 생기는 synchronous fault까지 `sigfillset`으로 전용 스레드에 모으려 해서는 안 된다. faulting thread의 실행 문맥과 연결된 thread-directed signal이며, `SIGKILL`과 `SIGSTOP`은 애초에 차단할 수 없다. `set`은 signal thread가 끝날 때까지 살아 있어야 한다.

### pthread_kill: 특정 스레드에게 전송

```c
#include <signal.h>
int pthread_kill(pthread_t thread, int sig);
```

**예시:**

```c
pthread_t worker_tid;
volatile sig_atomic_t stop_requested = 0;

void usr1_handler(int sig) {
    stop_requested = 1;
}

void *worker(void *arg) {
    /* 이 thread는 SIGUSR1을 차단하지 않으므로 handler를 실행할 수 있다. */
    while (!stop_requested) {
        work();
    }
    return NULL;
}

int main() {
    struct sigaction sa = {0};

    /* process-wide disposition을 thread 생성 전에 등록한다.
       등록하지 않으면 SIGUSR1의 기본 동작이 프로세스 전체를 종료한다. */
    sa.sa_handler = usr1_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    pthread_create(&worker_tid, NULL, worker, NULL);

    sleep(5);

    /* 대상 thread가 SIGUSR1을 차단하지 않았는지 mask 계약을 확인한 뒤 보낸다. */
    pthread_kill(worker_tid, SIGUSR1);

    pthread_join(worker_tid, NULL);
}
```

**주의:** 종료되어 수명이 끝난 `pthread_t`를 다시 사용하면 유효하지 않다. 구현은 `ESRCH`을 반환할 수 있지만, 식별자가 재사용되거나 잘못된 handle 접근이 생길 수 있으므로 join/detach와 별도의 수명 동기화로 대상 thread가 살아 있음을 보장해야 한다.

### sigwait: 동기적 시그널 처리

시그널 핸들러 대신 **동기적으로** 시그널을 기다릴 수 있다:

```c
#include <signal.h>
int sigwait(const sigset_t *set, int *sig);
```

`sigwait`는 다음을 수행한다:

1. `set`의 시그널 중 하나가 pending될 때까지 **차단**한다
2. pending 시그널을 제거하고 번호를 `*sig`에 저장한다
3. **핸들러를 실행하지 않는다**

**예시:**

```c
void *signal_handler_thread(void *arg) {
    sigset_t set;
    int sig;

    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);

    while (1) {
        int rc = sigwait(&set, &sig);  // set은 모든 관련 스레드에서 block되어 있어야 함
        if (rc != 0) {
            log_pthread_error("sigwait", rc);
            continue;
        }

        printf("Caught signal %d in thread\n", sig);

        if (sig == SIGTERM) {
            break;
        }
    }
}

int main() {
    pthread_t tid;
    sigset_t set;

    // 메인 스레드에서 시그널 차단
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    // 시그널 처리 스레드 생성 (차단된 마스크 상속)
    pthread_create(&tid, NULL, signal_handler_thread, NULL);

    // 실제 프로그램은 worker를 생성하고 종료 상태를 공유한 뒤 join한다.
    pthread_join(tid, NULL);
}
```

이 방식의 장점:

- **재진입 문제 없음**: 일반 함수처럼 작성할 수 있다
- **동기적**: 원하는 시점에 시그널을 처리한다
- **일반 스레드 문맥**: async-signal-safe 제약 없이 잠금, `malloc`, `printf`를 사용할 수 있다. 평범한 thread-safety 규약은 여전히 지켜야 한다.

## 9. 실전 예제: 견고한 시그널 처리

### 우아한 종료 (Graceful Shutdown)

```c
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

volatile sig_atomic_t shutdown_requested = 0;

void shutdown_handler(int sig) {
    shutdown_requested = 1;
}

int main() {
    struct sigaction sa;

    // SIGINT, SIGTERM 핸들러 등록
    sa.sa_handler = shutdown_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("Server started. Press Ctrl+C to shutdown gracefully.\n");

    while (!shutdown_requested) {
        // 서버 작업
        printf("Working...\n");
        sleep(1);
    }

    printf("Shutdown requested. Cleaning up...\n");

    // 정리 작업
    close_connections();
    save_state();

    printf("Shutdown complete.\n");
    return 0;
}
```

### 자식 프로세스 정리 (Zombie Reaping)

```c
#include <sys/wait.h>

void sigchld_handler(int sig) {
    int saved_errno = errno;  // errno 보존

    // 모든 종료된 자식 프로세스를 정리
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;

    errno = saved_errno;  // errno 복원
}

int main() {
    struct sigaction sa;

    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    sigaction(SIGCHLD, &sa, NULL);

    // 자식 프로세스 생성
    for (int i = 0; i < 10; i++) {
        if (fork() == 0) {
            // 자식: 작업 후 종료
            do_work();
            _exit(0);
        }
    }

    // 부모: 다른 작업
    while (1) {
        sleep(1);
    }
}
```

### 타임아웃 구현

```c
ssize_t read_with_timeout(int fd, void *buf, size_t count, int timeout_ms) {
    struct pollfd pfd = {
        .fd = fd,
        .events = POLLIN,
    };

    for (;;) {
        int rc = poll(&pfd, 1, timeout_ms);
        if (rc == 0) {
            errno = ETIMEDOUT;
            return -1;
        }
        if (rc < 0) {
            if (errno == EINTR)
                continue;  // 정책에 따라 남은 deadline을 다시 계산해야 함
            return -1;
        }
        return read(fd, buf, count);
    }
}
```

이 축약 코드는 `EINTR` 때 원래 `timeout_ms`를 다시 사용하므로 시그널이 반복되면 전체 deadline을 초과할 수 있다. 실제 구현은 monotonic clock으로 절대 deadline을 잡고 남은 시간을 계산한다. Linux 이벤트 루프라면 `timerfd`를 같은 `epoll` 집합에 넣는 방법도 있다.

시그널 handler에서 `longjmp`로 임의의 라이브러리 호출을 빠져나오면 mutex와 객체 수명, libc 내부 상태를 건너뛸 수 있다. `sigsetjmp`/`siglongjmp`가 signal mask 복원을 돕더라도, unsafe 함수가 중단된 뒤 unsafe 코드를 실행하는 위험까지 없애지는 않는다.

### 멀티스레드 서버의 시그널 처리

```c
#include <pthread.h>

#define NUM_WORKERS 4

void *worker_thread(void *arg) {
    while (1) {
        handle_client();
    }
}

void *signal_thread(void *arg) {
    sigset_t set;
    int sig;

    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGUSR1);
    sigaddset(&set, SIGUSR2);

    while (1) {
        int rc = sigwait(&set, &sig);
        if (rc != 0) {
            log_pthread_error("sigwait", rc);
            continue;
        }

        switch (sig) {
            case SIGINT:
            case SIGTERM:
                printf("Shutdown signal received\n");
                shutdown_workers();
                pthread_exit(NULL);

            case SIGUSR1:
                printf("Reload configuration\n");
                reload_config();
                break;

            case SIGUSR2:
                print_statistics();
                break;
        }
    }
}

int main() {
    pthread_t signal_tid;
    pthread_t workers[NUM_WORKERS];
    sigset_t set;

    // 전용 스레드가 동기적으로 소비할 signal만 생성 전에 차단한다.
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGUSR1);
    sigaddset(&set, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    // 시그널 처리 스레드
    pthread_create(&signal_tid, NULL, signal_thread, NULL);

    // 워커 스레드들
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_create(&workers[i], NULL, worker_thread, NULL);
    }

    pthread_join(signal_tid, NULL);
    for (int i = 0; i < NUM_WORKERS; ++i)
        pthread_join(workers[i], NULL);

    return 0;
}
```

## 10. 시그널의 한계와 대안

### 시그널의 근본적 한계

시그널은 강력하지만 많은 한계가 있다:

1. **제한된 정보**: 표준 시그널은 번호만 전달
2. **신뢰성 부족**: 표준 시그널은 손실될 수 있음
3. **복잡한 핸들러 제약**: async-signal-safe 함수만 사용 가능
4. **레이스 컨디션**: 핸들러와 메인 코드 간 동기화 어려움
5. **스레드와의 복잡한 상호작용**

### 대안 1: signalfd

Linux는 시그널을 **파일 디스크립터**로 읽을 수 있게 한다:

```c
#include <sys/signalfd.h>

int signalfd(int fd, const sigset_t *mask, int flags);
```

**예시:**

```c
int main() {
    sigset_t mask;
    int sfd;

    // SIGINT, SIGTERM 차단
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    // signalfd 생성
    sfd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sfd == -1) {
        perror("signalfd");
        return 1;
    }

    while (1) {
        struct signalfd_siginfo fdsi;
        ssize_t s = read(sfd, &fdsi, sizeof(fdsi));

        if (s != sizeof(fdsi)) {
            perror("read");
            exit(1);
        }

        printf("Got signal %d\n", fdsi.ssi_signo);

        if (fdsi.ssi_signo == SIGTERM) {
            break;
        }
    }

    close(sfd);
}
```

**장점:**

- `select`, `poll`, `epoll`과 통합 가능
- 핸들러 불필요 → 재진입 문제 없음
- 일반 함수 사용 가능

### 대안 2: 파이프를 이용한 Self-Pipe Trick

```c
static int sig_pipe[2];

void handler(int sig) {
    unsigned char byte = (unsigned char)sig;
    ssize_t ignored = write(sig_pipe[1], &byte, 1);
    (void)ignored;  // EAGAIN이면 이미 깨어날 이벤트가 있다고 보고 coalesce
}

int main() {
    if (pipe2(sig_pipe, O_NONBLOCK | O_CLOEXEC) == -1)
        return 1;

    struct sigaction sa = {0};
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) == -1)
        return 1;

    struct pollfd pfd;
    pfd.fd = sig_pipe[0];
    pfd.events = POLLIN;

    while (1) {
        poll(&pfd, 1, -1);

        if (pfd.revents & POLLIN) {
            unsigned char byte;
            read(sig_pipe[0], &byte, 1);
            printf("Got signal %u\n", (unsigned)byte);
        }
    }
}
```

handler의 `write`가 pipe가 가득 찼을 때 block하면 원래 스레드와 교착할 수 있으므로 write end는 nonblocking이어야 한다. 한 바이트에 signal 번호를 넣는 방식은 `NSIG <= UCHAR_MAX`인 대상 플랫폼 전제도 필요하다. 더 일반적인 self-pipe는 “상태가 바뀌었다”는 wakeup byte만 보내고 실제 상태는 `sig_atomic_t` flag에서 읽는다. `EAGAIN`은 wakeup이 이미 쌓였다는 의미로 coalesce할 수 있지만, instance별 전달이 필요한 데이터 큐로 사용해서는 안 된다.

### 대안 3: 이벤트 기반 라이브러리

현대적인 접근은 이벤트 기반 라이브러리를 사용하는 것이다:

- **libevent**: 시그널을 이벤트로 통합
- **libev**: 시그널 핸들러를 이벤트 루프에 통합
- **libuv**: Node.js가 사용하는 이벤트 루프

**libevent 예시:**

```c
#include <event2/event.h>

void signal_cb(evutil_socket_t sig, short events, void *user_data) {
    printf("Caught signal %d\n", sig);
    struct event_base *base = user_data;
    event_base_loopbreak(base);
}

int main() {
    struct event_base *base = event_base_new();
    struct event *signal_event;

    signal_event = evsignal_new(base, SIGINT, signal_cb, base);
    event_add(signal_event, NULL);

    event_base_dispatch(base);

    event_free(signal_event);
    event_base_free(base);
}
```

## 11. 터미널 작업 제어와 PTY

### Ctrl+C는 키보드가 직접 프로세스를 죽이는 것이 아니다

터미널 장치의 line discipline은 설정된 제어 문자를 해석한다. 기본 canonical 모드에서 `VINTR`에 대응하는 문자는 보통 `Ctrl+C`이고, line discipline은 foreground process group에 `SIGINT`를 보낸다.

| 제어 문자 | termios 항목 | 기본 시그널 |
|---|---|---|
| `Ctrl+C` | `VINTR` | `SIGINT` |
| `Ctrl+\` | `VQUIT` | `SIGQUIT` |
| `Ctrl+Z` | `VSUSP` | `SIGTSTP` |

`Ctrl+D`는 시그널이 아니다. Canonical 입력에서 현재 line buffer를 읽을 수 있게 만드는 EOF 의미의 제어 문자다. 버퍼가 비어 있으면 `read`가 0을 반환하고, 데이터가 있으면 그 데이터를 먼저 반환한다.

### 세션, 프로세스 그룹, foreground

작업 제어는 세 계층의 ID를 사용한다.

```text
session SID
  shell process group PGID=100
  pipeline process group PGID=200
    process 201
    process 202
```

- 프로세스 그룹은 pipeline처럼 함께 시그널을 받을 작업 단위다.
- 세션은 여러 프로세스 그룹과 선택적인 controlling terminal을 묶는다.
- 터미널은 한 시점에 foreground process group ID를 가진다.

Shell은 pipeline의 자식들을 같은 PGID로 묶고 `tcsetpgrp`로 그 그룹을 foreground에 둔다. 터미널이 만든 `SIGINT`·`SIGTSTP`는 개별 PID가 아니라 이 foreground PGID로 전달된다.

Background group이 controlling terminal에서 읽으려 하면 보통 `SIGTTIN`을 받아 멈춘다. `TOSTOP`이 설정된 터미널에 background group이 쓰면 `SIGTTOU`를 받을 수 있다. Terminal hangup은 session leader와 작업에 `SIGHUP`을 발생시킬 수 있다.

### Canonical과 raw mode

Canonical 모드에서는 line discipline이 줄 단위 입력, erase, kill, EOF, signal 문자를 처리한다. Raw에 가까운 모드에서는 이 처리를 끄고 byte를 애플리케이션에 전달한다.

```c
struct termios raw;
tcgetattr(fd, &raw);
cfmakeraw(&raw);
tcsetattr(fd, TCSAFLUSH, &raw);
```

`cfmakeraw`는 여러 flag를 한꺼번에 바꾸는 편의 함수다. 프로그램은 종료뿐 아니라 시그널·오류 경로에서도 원래 termios를 복구해야 한다. 복구하지 못한 shell은 echo가 꺼지거나 입력 모드가 깨진 채 남을 수 있다.

### PTY

Pseudo-terminal은 master/slave 파일 디스크립터 쌍이다.

```text
terminal emulator / SSH server / tmux
              |
          PTY master
              |
      line discipline + TTY state
              |
          PTY slave
              |
         shell and child jobs
```

Slave는 프로그램에서 실제 terminal처럼 보인다. Master에 쓴 byte는 slave의 입력이 되고, slave 출력은 master에서 읽힌다. Terminal emulator는 master 쪽에서 escape sequence를 해석해 화면을 그린다. SSH server는 네트워크와 PTY master 사이를 중계한다.

PTY가 terminal 화면 자체를 구현하는 것은 아니다. 커널은 byte stream, line discipline, window size, foreground PGID 같은 상태를 제공하고 색·커서·글리프 렌더링은 사용자 공간 terminal emulator가 담당한다.

### Shell의 경합

Job-control shell은 `fork`, `setpgid`, `tcsetpgrp`, `waitpid` 사이의 경합을 막아야 한다.

1. `SIGCHLD`를 잠시 block한다.
2. child와 parent 양쪽에서 `setpgid`를 시도해 누가 먼저 실행돼도 그룹을 만든다.
3. foreground 작업이면 terminal foreground PGID를 child group으로 바꾼다.
4. `waitpid(..., WUNTRACED | WCONTINUED)`로 종료·정지·재개를 추적한다.
5. 작업이 정지하거나 끝나면 terminal foreground를 shell PGID로 되돌린다.
6. terminal mode를 복원하고 `SIGCHLD` mask를 되돌린다.

이 순서가 없으면 child가 그룹에 들어가기 전에 terminal-generated signal을 받거나, shell 자신이 background group이 되어 `SIGTTOU`로 멈출 수 있다.


---

## 결론

시그널은 UNIX의 단순함의 철학을 체현한다. 정수 하나로 이벤트를 알리고, 언제든 발생할 수 있으며, 핸들러로 임의 동작을 구현할 수 있다. 이 단순함 뒤에는 복잡성이 숨어 있다. 재진입성, 레이스 컨디션, 스레드와의 상호작용 등 많은 함정이 있다.

현대 코드에서 시그널은 프로세스 생명주기 관리(SIGTERM, SIGKILL), 오류 처리(SIGSEGV, SIGBUS), 자식 프로세스 관리(SIGCHLD)에서 여전히 핵심이다. Linux에서 process-directed signal을 이벤트 루프에 통합할 때는 `signalfd`, portable한 형태에는 self-pipe를 고려할 수 있다. `eventfd`는 애플리케이션이 직접 counter를 쓰는 thread/process 통지 수단이지 OS signal을 자동으로 소비하는 대체 API는 아니다.

기억해야 할 사용 원칙은 다음과 같다. `signal` 대신 `sigaction`을 쓴다. 핸들러는 최소한으로 짜고 플래그만 설정한 뒤 빠져나온다. async-signal-safe 함수만 호출한다. `sig_atomic_t`와 `volatile`을 적절히 사용한다. 멀티스레드 환경이라면 시그널 전용 스레드를 두는 패턴을 고려한다. 가능하면 signalfd나 이벤트 루프 같은 현대적 대안을 함께 검토한다.

터미널 작업 제어에서는 foreground process group이 terminal-generated signal의 수신 단위다. PTY는 이 계약을 물리 terminal 없이 재현하며, shell은 process group 생성과 terminal foreground 이전을 경합 없이 수행해야 한다.

## 더 읽을거리

- POSIX.1-2024 Base Specifications, Signal Concepts — https://pubs.opengroup.org/onlinepubs/9799919799/
- POSIX.1-2017 Base Specifications, Signal Concepts — https://pubs.opengroup.org/onlinepubs/9699919799/
- 커널 소스: `kernel/signal.c`, `arch/x86/kernel/signal.c`, `include/linux/sched/signal.h`
- W. Richard Stevens, Stephen A. Rago, *Advanced Programming in the UNIX Environment*, 3rd ed., Chapter 10
- Michael Kerrisk, *The Linux Programming Interface*, Chapter 20-22
- Intel 64 and IA-32 Architectures Software Developer's Manual, Volume 3A
- Linux man pages: `signal(7)`, `signal-safety(7)`, `sigaction(2)`, `sigprocmask(2)`, `pthread_sigmask(3)`
- POSIX.1 General Terminal Interface: <https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap11.html>
- Linux man pages: `termios(3)`, `pty(7)`, `credentials(7)`, `tcsetpgrp(3)`
- Maurice Bach, *The Design of the UNIX Operating System*, Chapter 7
