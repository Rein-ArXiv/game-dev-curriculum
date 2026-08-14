## 들어가며

UNIX의 힘은 작은 프로그램들이 서로 통신하며 조합되는 데서 나온다. W. Richard Stevens가 *UNIX Network Programming Volume 2* 전체를 IPC에 바친 이유이기도 하다.

프로세스는 운영체제의 기본 격리 단위다. 각 프로세스는 독립적인 가상 주소 공간에서 실행되며, 다른 프로세스의 메모리에 직접 접근할 수 없다. 격리는 안정성과 보안을 제공하지만, 협력이 필요한 애플리케이션에게는 도전 과제가 된다.

웹 브라우저는 보안 경계를 위해 여러 프로세스를 사용하고, 서버는 worker 프로세스나 별도 서비스 사이에서 상태를 교환하며, 셸은 파이프로 명령을 연결한다. 모든 경우에 프로세스는 데이터를 주고받아야 한다.

프로세스 간 통신(Inter-Process Communication, IPC)은 격리된 프로세스들이 협력할 수 있게 하는 메커니즘이다. UNIX/Linux는 파이프, 메시지 큐, 공유 메모리, 소켓, 세마포어 등 다양한 IPC를 제공한다. 각각은 서로 다른 trade-off와 사용 사례를 가진다. 파이프는 왜 단방향인가? 공유 메모리는 언제 복사를 줄이지만 동기화를 더 어렵게 만드는가? POSIX와 System V IPC의 차이는 무엇인가? 커널은 이를 어떻게 구현하는가?

이 글은 UNIX/Linux의 IPC 메커니즘을 다룬다. 익명 파이프부터 POSIX 공유 메모리까지, 각 메커니즘의 인터페이스, 사용 사례, 성능 특성, 그리고 커널 구현(`fs/pipe.c`, `ipc/msg.c`, `mm/shmem.c`)을 살펴본다.


---

## 목차
1. [IPC의 역사와 분류](#1-ipc의-역사와-분류)
2. [파이프: 가장 오래된 IPC](#2-파이프-가장-오래된-ipc)
3. [Named Pipe(FIFO)](#3-named-pipefifo)
4. [POSIX 메시지 큐](#4-posix-메시지-큐)
5. [공유 메모리: 데이터 경로와 동기화의 분리](#5-공유-메모리-데이터-경로와-동기화의-분리)
6. [Unix Domain Socket](#6-unix-domain-socket)
7. [세마포어: 동기화 프리미티브](#7-세마포어-동기화-프리미티브)
8. [IPC 성능 비교](#8-ipc-성능-비교)
9. [커널 내부: IPC 서브시스템](#9-커널-내부-ipc-서브시스템)
10. [고급 IPC 패턴](#10-고급-ipc-패턴)
11. [IPC 보안과 권한](#11-ipc-보안과-권한)
12. [실전 예제: 멀티프로세스 웹 서버](#12-실전-예제-멀티프로세스-웹-서버)
13. [디버깅과 모니터링](#13-디버깅과-모니터링)


---

## 1. IPC의 역사와 분류

### UNIX IPC의 진화

IPC의 역사는 UNIX의 역사와 함께한다:

**1970년대 초기 UNIX:**
- **파이프(pipe)**: Ken Thompson이 구현한 최초의 IPC
- `|` 연산자로 명령을 연결하는 쉘 파이프라인

**1980년대 System V:**
- **System V IPC**: AT&T가 추가한 세 가지 메커니즘
 - 메시지 큐 (Message Queues)
 - 공유 메모리 (Shared Memory)
 - 세마포어 (Semaphores)
- 키 기반 식별, `ipcs`/`ipcrm` 유틸리티

**1990년대 POSIX:**
- **POSIX IPC**: System V의 문제점을 해결
 - 이름 기반 식별 (`/이름` 형태). 단 어느 가상 파일시스템에 매핑되는지는 IPC 종류에 따라 다르다.
 - POSIX 공유 메모리(`shm_open`): `/dev/shm/이름`
 - POSIX 메시지 큐(`mq_open`): `/dev/mqueue/이름` (mqueue tmpfs 마운트가 필요)
 - POSIX 세마포어(`sem_open`): 보통 `/dev/shm/sem.이름`
 - 파일과 유사한 인터페이스
 - descriptor와 이름 기반 lifecycle을 사용하는 별도 POSIX API

**socket과 message bus 계열:**
- **Unix Domain Socket**: BSD socket 계열에서 발전한 로컬 양방향 통신
- **D-Bus**: service discovery와 typed message를 제공하는 사용자 공간 message bus
- **io_uring**: IPC 종류가 아니라 pipe·socket·file 연산도 제출할 수 있는 비동기 I/O 인터페이스

### IPC 메커니즘의 분류

IPC 메커니즘은 여러 기준으로 분류할 수 있다:

**데이터 전달 방식:**
- **메시지 전달(Message Passing)**: 파이프, 메시지 큐, 소켓
- **메모리 공유(Memory Sharing)**: 공유 메모리, mmap

**동기화:**
- **암묵적 동기화**: 파이프 (읽기/쓰기가 자동으로 동기화)
- **명시적 동기화 필요**: 공유 메모리 + 세마포어/뮤텍스

**네트워크 지원:**
- **로컬만**: 파이프, System V IPC, POSIX 공유 메모리
- **네트워크 가능**: 소켓

**관계 요구사항:**
- **부모-자식 관계 필요**: 익명 파이프
- **무관한 프로세스**: Named pipe, 메시지 큐, 공유 메모리, 소켓

### IPC 선택 가이드

| 메커니즘 | 용도 | 장점 | 단점 |
|---------|------|------|------|
| 익명 파이프 | 상속하거나 전달받은 fd 사이의 단순 데이터 전달 | 단순, 커널 버퍼의 blocking 규약 | 이름이 없어 fd 전달 경로 필요 |
| Named 파이프 | 무관한 프로세스 간 데이터 | 파일시스템 이름 | 단방향, 영구적 |
| 메시지 큐 | 구조화된 메시지 교환 | 우선순위, 타입별 수신 | 커널 버퍼 크기 제한 |
| 공유 메모리 | 공유 상태, 대용량 payload | 별도 커널 payload copy를 피할 수 있음 | 동기화·수명·복구를 직접 설계 |
| Unix 소켓 | 양방향 통신, 파일 전달 | 양방향, 연결 지향 | 상대적으로 무거움 |
| 세마포어 | 공유 자원 동기화 | 원자적 연산 | 데이터 전달 불가 |

## 2. 파이프: 가장 오래된 IPC

### 익명 파이프의 본질

파이프는 UNIX의 가장 우아한 발명 중 하나다. Doug McIlroy는 1964년 메모에서 이렇게 썼다:

> "We should have some ways of coupling programs like garden hose—screw in another segment when it becomes necessary to massage data in another way."

파이프는 **단방향 데이터 스트림**이다. 한쪽 끝에서 쓰고, 다른 쪽 끝에서 읽는다.

```c
#include <unistd.h>
int pipe(int pipefd[2]);
// pipefd[0]: 읽기 끝 (read end)
// pipefd[1]: 쓰기 끝 (write end)
```

**기본 사용 패턴:**

```c
int pipefd[2];
pipe(pipefd);

pid_t pid = fork();
if (pid == 0) {
    // 자식: 파이프에서 읽기
    close(pipefd[1]);  // 쓰기 끝 닫기

    char buf[100];
    ssize_t n = read(pipefd[0], buf, sizeof(buf));
    write(STDOUT_FILENO, buf, n);

    close(pipefd[0]);
    exit(0);
} else {
    // 부모: 파이프에 쓰기
    close(pipefd[0]);  // 읽기 끝 닫기

    const char *msg = "Hello from parent\n";
    write(pipefd[1], msg, strlen(msg));

    close(pipefd[1]);
    wait(NULL);
}
```

### 파이프의 특성

**1) 단방향 통신**

파이프는 한 방향으로만 데이터가 흐른다. 양방향 통신이 필요하면 두 개의 파이프를 사용한다:

```c
int parent_to_child[2], child_to_parent[2];
pipe(parent_to_child);
pipe(child_to_parent);

if (fork() == 0) {
    // 자식
    close(parent_to_child[1]);
    close(child_to_parent[0]);

    // parent_to_child[0]에서 읽기
    // child_to_parent[1]에 쓰기
} else {
    // 부모
    close(parent_to_child[0]);
    close(child_to_parent[1]);

    // parent_to_child[1]에 쓰기
    // child_to_parent[0]에서 읽기
}
```

**2) 자동 동기화**

파이프는 **생산자-소비자 문제**를 자동으로 해결한다:

- 파이프가 비어 있으면 `read`는 **블록**된다 (데이터가 올 때까지)
- 파이프가 가득 차면 `write`는 **블록**된다 (공간이 생길 때까지)
- 모든 쓰기 끝이 닫히면 `read`는 **EOF(0)**를 반환한다
- 모든 읽기 끝이 닫히면 `write`는 **SIGPIPE**를 받는다

```c
// 생산자
for (int i = 0; i < 1000; i++) {
    write(pipefd[1], &i, sizeof(i));  // 파이프 가득 차면 블록
}
close(pipefd[1]);  // EOF 신호

// 소비자
int data;
while (read(pipefd[0], &data, sizeof(data)) > 0) {  // EOF면 종료
    process(data);
}
```

**3) 원자성 보장**

POSIX는 `PIPE_BUF` 크기 이하의 쓰기는 **원자적**임을 보장한다:

```c
#include <limits.h>
// Linux: PIPE_BUF = 4096 bytes
```

Linux에서 한 번의 `write()`가 `PIPE_BUF` 이하이면 여러 writer의 데이터와 interleave되지 않는다. Linux의 `PIPE_BUF`는 4096바이트지만 portable code는 `fpathconf(fd, _PC_PIPE_BUF)`나 플랫폼 계약을 확인한다.

```c
// 여러 자식이 동시에 파이프에 쓰기
for (int i = 0; i < 10; i++) {
    if (fork() == 0) {
        char msg[100];
        snprintf(msg, sizeof(msg), "Child %d\n", i);
        write(pipefd[1], msg, strlen(msg));  // 원자적 (100 < 4096)
        exit(0);
    }
}
```

**4) 파이프 용량**

Linux의 pipe capacity는 page size, kernel version, per-user pipe page limit과 `F_SETPIPE_SZ` 설정에 따라 달라진다. 16 page가 흔한 초기 용량이지만 64KiB를 ABI 상수로 가정해서는 안 된다.

```c
#include <fcntl.h>

// 파이프 크기 확인
int size = fcntl(pipefd[0], F_GETPIPE_SZ);  // 실행 중인 pipe의 실제 용량

// 파이프 크기 변경 (권한 필요)
fcntl(pipefd[0], F_SETPIPE_SZ, 1024 * 1024);  // 1MB
```

### 쉘 파이프라인의 구현

쉘에서 `ls | grep txt | wc -l`을 실행하면 어떻게 될까?

```c
// 단순화된 쉘 파이프라인 구현
void execute_pipeline(char *cmd1[], char *cmd2[]) {
    int pipefd[2];
    pipe(pipefd);

    if (fork() == 0) {
        // 첫 번째 명령
        dup2(pipefd[1], STDOUT_FILENO);  // stdout을 파이프로 리다이렉트
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(cmd1[0], cmd1);
    }

    if (fork() == 0) {
        // 두 번째 명령
        dup2(pipefd[0], STDIN_FILENO);   // stdin을 파이프로 리다이렉트
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(cmd2[0], cmd2);
    }

    close(pipefd[0]);
    close(pipefd[1]);
    wait(NULL);
    wait(NULL);
}

// 사용
char *ls_cmd[] = {"ls", "-l", NULL};
char *grep_cmd[] = {"grep", "txt", NULL};
execute_pipeline(ls_cmd, grep_cmd);
```

### 커널 내부: 파이프의 구현

Linux에서 파이프는 **링 버퍼**로 구현된다 (`fs/pipe.c`):

```c
/* include/linux/pipe_fs_i.h */
struct pipe_inode_info {
    struct mutex mutex;         /* 동기화 */
    wait_queue_head_t rd_wait;  /* 읽기 대기 큐 */
    wait_queue_head_t wr_wait;  /* 쓰기 대기 큐 */

    unsigned int head;          /* 쓰기 위치 */
    unsigned int tail;          /* 읽기 위치 */
    unsigned int max_usage;     /* 최대 버퍼 크기 */
    unsigned int ring_size;     /* 링 버퍼 크기 (페이지 수) */

    struct pipe_buffer *bufs;   /* 버퍼 배열 */
    /* ... */
};

struct pipe_buffer {
    struct page *page;          /* 데이터 페이지 */
    unsigned int offset;        /* 페이지 내 오프셋 */
    unsigned int len;           /* 데이터 길이 */
    const struct pipe_buf_operations *ops;
    /* ... */
};
```

**파이프 읽기 구현:**

```c
/* fs/pipe.c */
static ssize_t pipe_read(struct kiocb *iocb, struct iov_iter *to)
{
    struct file *filp = iocb->ki_filp;
    struct pipe_inode_info *pipe = filp->private_data;

    mutex_lock(&pipe->mutex);

    for (;;) {
        /* 파이프에 데이터가 있는가? */
        unsigned int head = pipe->head;
        unsigned int tail = pipe->tail;
        unsigned int mask = pipe->ring_size - 1;

        if (!pipe_empty(head, tail)) {
            /* 데이터 읽기 */
            struct pipe_buffer *buf = &pipe->bufs[tail & mask];
            size_t chars = buf->len;

            /* 사용자 공간으로 복사 */
            copy_page_to_iter(buf->page, buf->offset, chars, to);

            buf->offset += chars;
            buf->len -= chars;

            /* 버퍼가 비었으면 tail 증가 */
            if (!buf->len) {
                pipe->tail = tail + 1;
            }

            /* 쓰기 대기 중인 프로세스 깨우기 */
            wake_up_interruptible(&pipe->wr_wait);

            break;
        }

        /* 파이프가 비어 있고 writer가 없으면 EOF */
        if (!pipe->writers) {
            break;  // return 0 (EOF)
        }

        /* 블록 */
        mutex_unlock(&pipe->mutex);
        wait_event_interruptible(pipe->rd_wait,
                                 !pipe_empty(pipe->head, pipe->tail));
        mutex_lock(&pipe->mutex);
    }

    mutex_unlock(&pipe->mutex);
    return ret;
}
```

**Zero-copy 최적화:**

Linux의 `splice`는 payload를 사용자 버퍼로 복사하지 않고 커널의 pipe buffer/page 참조 사이에서 이동시킬 수 있다.

```c
#include <fcntl.h>

// 파일 -> 파이프 -> 파일 (user-space payload copy 회피 가능)
int fd_in = open("input.txt", O_RDONLY);
int fd_out = open("output.txt", O_WRONLY);
int pipefd[2];
pipe(pipefd);

splice(fd_in, NULL, pipefd[1], NULL, 65536, 0);      // 파일 -> 파이프
splice(pipefd[0], NULL, fd_out, NULL, 65536, 0);     // 파이프 -> 파일

// 데이터가 커널 공간에만 머물고, 사용자 공간으로 복사되지 않음
```

장치, 파일 시스템, 정렬과 fallback 경로에 따라 내부 copy가 생길 수 있다. 여기서 zero-copy는 CPU가 어떤 바이트도 복사하지 않는다는 절대 보장이 아니라 사용자 공간 왕복 copy를 피하는 API 경로를 뜻한다. 반환값은 요청보다 짧을 수 있으므로 실제 코드는 남은 바이트를 반복 처리해야 한다.

## 3. Named Pipe (FIFO)

### FIFO의 필요성

익명 파이프는 부모-자식 관계가 필요하다. **무관한 프로세스**가 통신하려면?

**Named Pipe (FIFO)**는 파일시스템에 이름을 가진 파이프다:

```c
#include <sys/stat.h>
int mkfifo(const char *pathname, mode_t mode);
```

FIFO는 디스크에 데이터를 저장하지 않는다. 이름만 파일시스템에 있을 뿐, 실제 데이터는 커널 버퍼에 있다.

### FIFO 생성과 사용

**FIFO 생성:**

```c
// 명령행
$ mkfifo /tmp/myfifo

// C 코드
mkfifo("/tmp/myfifo", 0666);
```

**Writer 프로세스:**

```c
int fd = open("/tmp/myfifo", O_WRONLY);
if (fd == -1) {
    perror("open");
    exit(1);
}

const char *msg = "Hello from writer\n";
write(fd, msg, strlen(msg));
close(fd);
```

**Reader 프로세스:**

```c
int fd = open("/tmp/myfifo", O_RDONLY);
if (fd == -1) {
    perror("open");
    exit(1);
}

char buf[100];
ssize_t n = read(fd, buf, sizeof(buf));
if (n < 0) {
    perror("read");
} else if (n > 0) {
    write_all(STDOUT_FILENO, buf, (size_t)n);
}
close(fd);
```

### FIFO의 블로킹 동작

FIFO의 `open`은 특별한 의미를 가진다:

**기본 동작 (블로킹):**

```c
// Writer는 reader가 open()할 때까지 블록
int fd = open("/tmp/myfifo", O_WRONLY);  // 블록!

// Reader는 writer가 open()할 때까지 블록
int fd = open("/tmp/myfifo", O_RDONLY);  // 블록!
```

**논블로킹 모드:**

```c
// 즉시 반환 (상대편이 없어도)
int fd = open("/tmp/myfifo", O_WRONLY | O_NONBLOCK);
if (fd == -1 && errno == ENXIO) {
    printf("No reader yet\n");
}

int fd = open("/tmp/myfifo", O_RDONLY | O_NONBLOCK);
// 상대 writer가 없어도 peer 부재만을 이유로 ENXIO가 되지는 않는다.
// 경로 부재, 권한, fd 한도 같은 일반 open 오류는 여전히 가능하다.
```

### FIFO 활용 사례

**1) 클라이언트-서버 통신**

```c
// 서버
mkfifo("/tmp/server_fifo", 0666);

while (1) {
    int fd = open("/tmp/server_fifo", O_RDONLY);

    char request[256];
    read(fd, request, sizeof(request));

    process_request(request);
    close(fd);
}

// 클라이언트
int fd = open("/tmp/server_fifo", O_WRONLY);
write(fd, "GET /data", 9);
close(fd);
```

**2) 로그 수집기**

```c
// 로그 수집기
mkfifo("/var/log/app.fifo", 0666);

int fd = open("/var/log/app.fifo", O_RDONLY);
while (1) {
    char log[1024];
    ssize_t n = read(fd, log, sizeof(log));
    if (n > 0) {
        write_to_logfile(log, n);
    }
}

// 애플리케이션들
int fd = open("/var/log/app.fifo", O_WRONLY | O_NONBLOCK);
dprintf(fd, "[%s] Event occurred\n", timestamp());
close(fd);
```

### FIFO vs 익명 파이프

| 특성 | 익명 파이프 | FIFO |
|------|-----------|------|
| 이름 | 없음 | 파일시스템 경로 |
| 관계 | 부모-자식 필요 | 무관한 프로세스 가능 |
| 영속성 | 프로세스 종료 시 삭제 | 명시적 삭제 필요 |
| 양방향 | 두 개 필요 | 두 개 필요 (동일) |
| 성능 | 약간 빠름 | 약간 느림 (inode lookup) |

## 4. POSIX 메시지 큐

### 메시지 큐의 개념

파이프는 **바이트 스트림**이다. 메시지 경계가 없다. **메시지 큐**는 경계가 있는 **메시지**를 전송한다.

POSIX 메시지 큐는 다음 특징을 가진다:

- **메시지 단위 전송**: 각 `mq_send`는 하나의 메시지
- **우선순위**: 메시지마다 우선순위 지정 가능
- **비동기 통지**: `mq_notify`로 메시지 도착 시그널 받기
- **이름 기반**: `/name` 형식의 이름

### POSIX 메시지 큐 생성

```c
#include <mqueue.h>

mqd_t mq_open(const char *name, int oflag, mode_t mode, struct mq_attr *attr);

struct mq_attr {
    long mq_flags;      /* 플래그: O_NONBLOCK */
    long mq_maxmsg;     /* 최대 메시지 수 */
    long mq_msgsize;    /* 최대 메시지 크기 */
    long mq_curmsgs;    /* 현재 메시지 수 (read-only) */
};
```

**예시:**

```c
struct mq_attr attr = {
    .mq_flags = 0,
    .mq_maxmsg = 10,        // 최대 10개 메시지
    .mq_msgsize = 256,      // 각 메시지 최대 256바이트
    .mq_curmsgs = 0,
};

mqd_t mq = mq_open("/my_queue", O_CREAT | O_RDWR, 0644, &attr);
if (mq == (mqd_t)-1) {
    perror("mq_open");
    exit(1);
}
```

### 메시지 송수신

**송신:**

```c
int mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned int msg_prio);

// 예시
const char *msg = "Hello, Message Queue!";
if (mq_send(mq, msg, strlen(msg), 0) == -1) {
    perror("mq_send");
}
```

**수신:**

```c
ssize_t mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio);

// 예시
char buf[256];
unsigned int prio;
ssize_t n = mq_receive(mq, buf, sizeof(buf), &prio);
if (n >= 0) {
    printf("Received (prio=%u): %.*s\n", prio, (int)n, buf);
}
```

### 우선순위 메시지

우선순위가 높은 메시지가 먼저 수신된다:

```c
// 서버
mqd_t mq = mq_open("/task_queue", O_CREAT | O_RDONLY, 0644, &attr);

while (1) {
    char task[256];
    unsigned int prio;

    mq_receive(mq, task, sizeof(task), &prio);

    // 우선순위에 따라 처리
    if (prio >= 10) {
        process_urgent(task);
    } else {
        process_normal(task);
    }
}

// 클라이언트
mqd_t mq = mq_open("/task_queue", O_WRONLY);

// 일반 작업
mq_send(mq, "Normal task", 11, 5);

// 긴급 작업
mq_send(mq, "Urgent task", 11, 15);  // 먼저 수신됨
```

### 비동기 통지

`mq_notify`의 `SIGEV_SIGNAL` 통지는 일회성이다. 시그널 핸들러 안에서 `mq_receive`나 `printf`처럼 async-signal-safe가 아닌 함수를 호출하지 말고, 전용 스레드가 `sigwaitinfo`로 시그널을 동기적으로 소비하게 한다. 재등록과 수신 사이에 새 메시지가 도착해도 통지를 잃지 않도록 **먼저 재등록한 뒤**, 비차단 큐가 `EAGAIN`을 반환할 때까지 비운다.

```c
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigprocmask(SIG_BLOCK, &set, NULL);

    struct mq_attr attr = {
        .mq_maxmsg = 10,
        .mq_msgsize = 256,
    };
    mqd_t mq = mq_open("/async_queue", O_CREAT | O_RDONLY | O_NONBLOCK,
                       0644, &attr);

    struct sigevent sev = {
        .sigev_notify = SIGEV_SIGNAL,
        .sigev_signo = SIGUSR1,
        .sigev_value.sival_int = mq, // Linux에서 mqd_t는 int
    };
    if (mq_notify(mq, &sev) == -1) return 1;

    for (;;) {
        if (sigwaitinfo(&set, NULL) == -1) continue;

        // mq_notify는 한 번 통지하면 해제된다. 큐를 비우기 전에 재등록한다.
        if (mq_notify(mq, &sev) == -1) return 1;

        char buf[257];
        ssize_t n;
        while ((n = mq_receive(mq, buf, sizeof(buf) - 1, NULL)) >= 0) {
            buf[n] = '\0';
            printf("Async message: %s\n", buf);
        }
        if (errno != EAGAIN) return 1;
    }
}
```

### 커널 구현: POSIX 메시지 큐

Linux에서 POSIX 메시지 큐는 `ipc/mqueue.c`에 구현되어 있다:

```c
/* ipc/mqueue.c */
struct mqueue_inode_info {
    spinlock_t lock;
    struct inode vfs_inode;
    wait_queue_head_t wait_q;

    struct msg_msg **messages;  /* 메시지 배열 */
    struct mq_attr attr;

    struct sigevent notify;     /* 비동기 통지 */
    pid_t notify_owner;
    /* ... */
};

struct msg_msg {
    struct list_head m_list;
    long m_type;
    size_t m_ts;                /* 메시지 크기 */
    struct msg_msgseg *next;    /* 다음 세그먼트 */
    /* 메시지 데이터가 여기 뒤따름 */
};
```

**송신 구현:**

```c
static int do_mq_timedsend(mqd_t mqdes, const char *u_msg_ptr,
                           size_t msg_len, unsigned int msg_prio,
                           struct timespec *timeout)
{
    struct file *filp = mqdes_to_file(mqdes);
    struct mqueue_inode_info *info = MQUEUE_I(filp->f_inode);
    struct msg_msg *msg_ptr;

    /* 큐가 가득 찼는가? */
    while (info->attr.mq_curmsgs >= info->attr.mq_maxmsg) {
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;

        /* 블록 */
        wait_event_interruptible_timeout(info->wait_q, ...);
    }

    /* 메시지 할당 및 복사 */
    msg_ptr = load_msg(u_msg_ptr, msg_len);
    msg_ptr->m_type = msg_prio;

    /* 우선순위 순서로 삽입 */
    insert_message(info, msg_ptr);
    info->attr.mq_curmsgs++;

    /* 대기 중인 수신자 깨우기 */
    wake_up(&info->wait_q);

    /* 비동기 통지 */
    if (info->notify_owner && info->attr.mq_curmsgs == 1) {
        send_notification(info);
    }

    return 0;
}
```

### POSIX vs System V 메시지 큐

| 특성 | POSIX | System V |
|------|-------|----------|
| 식별 | 이름 (`/name`) | 키 (`key_t`) |
| 인터페이스 | 파일 디스크립터 유사 | msgget/msgsnd/msgrcv |
| 우선순위 | 지원 (POSIX 최소 32단계, Linux는 0~32767) | 타입으로 대체 |
| 비동기 통지 | mq_notify | 없음 |
| 제한 확인 | mq_getattr | msgctl |
| 삭제 | mq_unlink | msgctl(IPC_RMID) |

## 5. 공유 메모리: 데이터 경로와 동기화의 분리

### 공유 메모리의 본질

공유 메모리는 같은 물리 페이지를 여러 프로세스 주소 공간에 매핑해 프로세스 사이의 별도 커널 payload copy를 피할 수 있다. 그러나 생산자가 데이터를 처음 쓰는 메모리 트래픽, 캐시 일관성, publish/consume 동기화와 wakeup 비용은 남는다.

파이프나 메시지 큐는 다음 과정을 거친다:
1. 사용자 버퍼 → 커널 버퍼 (송신 시)
2. 커널 버퍼 → 사용자 버퍼 (수신 시)

공유 메모리는:
1. 같은 물리 메모리를 두 프로세스가 직접 접근

```text
프로세스 A 주소 공간          물리 메모리           프로세스 B 주소 공간
┌────────────────┐                               ┌────────────────┐
│                │                               │                │
│  0x7f000000    │ ──┐                      ┌──> │  0x7e000000    │
│                │   │                      │    │                │
│  [shared mem]  │   └──> [physical pages] ┘    │  [shared mem]  │
│                │                               │                │
└────────────────┘                               └────────────────┘
```

### POSIX 공유 메모리

POSIX 공유 메모리는 두 단계로 사용한다:

1. **shm_open**: 공유 메모리 객체 생성/열기
2. **mmap**: 프로세스 주소 공간에 매핑

```c
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

// 1. 단일 creator가 새 공유 메모리 객체 생성
int shm_fd = shm_open("/my_shm", O_CREAT | O_EXCL | O_RDWR, 0600);
if (shm_fd == -1) {
    perror("shm_open");
    exit(1);
}

// 2. creator만 크기 설정
size_t size = 4096;
if (ftruncate(shm_fd, size) == -1) {
    perror("ftruncate");
    close(shm_fd);
    shm_unlink("/my_shm");
    exit(1);
}

// 3. 메모리 매핑
void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
if (ptr == MAP_FAILED) {
    perror("mmap");
    exit(1);
}

// 4. 사용
strcpy(ptr, "Hello, shared memory!");

// 5. 정리
munmap(ptr, size);
close(shm_fd);
shm_unlink("/my_shm");
```

### 공유 메모리 생산자-소비자

공유 메모리는 **동기화가 필요**하다:

```c
// 공유 메모리 구조
struct shared_data {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int ready;
    char message[256];
};

// 생산자: 이 예제에서는 생산자가 유일한 creator이고 소비자는
// 별도 ready 통지를 받은 뒤에만 open한다고 가정한다.
int shm_fd = shm_open("/producer_consumer",
                      O_CREAT | O_EXCL | O_RDWR, 0600);
ftruncate(shm_fd, sizeof(struct shared_data));

struct shared_data *data = mmap(NULL, sizeof(struct shared_data),
                                 PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

// 프로세스 간 mutex/cond 초기화
pthread_mutexattr_t mutex_attr;
pthread_mutexattr_init(&mutex_attr);
pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
pthread_mutex_init(&data->mutex, &mutex_attr);

pthread_condattr_t cond_attr;
pthread_condattr_init(&cond_attr);
pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
pthread_cond_init(&data->cond, &cond_attr);

data->ready = 0;

// 메시지 작성
pthread_mutex_lock(&data->mutex);
strcpy(data->message, "Hello from producer");
data->ready = 1;
pthread_cond_signal(&data->cond);
pthread_mutex_unlock(&data->mutex);

// 소비자
int shm_fd = shm_open("/producer_consumer", O_RDWR, 0);
struct shared_data *data = mmap(NULL, sizeof(struct shared_data),
                                 PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

// 메시지 대기
pthread_mutex_lock(&data->mutex);
while (!data->ready) {
    pthread_cond_wait(&data->cond, &data->mutex);
}
printf("Received: %s\n", data->message);
pthread_mutex_unlock(&data->mutex);
```

creator가 `pthread_mutex_init`을 수행하는 동안 다른 프로세스가 객체를 사용하면 초기화 경쟁이 생긴다. `O_CREAT | O_EXCL`로 creator를 정하고, 별도의 ready header를 release/acquire로 게시하거나 bootstrap lock을 사용해야 한다. owner process가 죽을 수 있다면 robust process-shared mutex와 `EOWNERDEAD` 복구 절차도 설계한다.

`PTHREAD_PROCESS_SHARED` 지원 여부와 각 pthread 호출의 반환값도 검사해야 한다. creator는 attribute 객체를 destroy하고, 마지막 사용자가 mutex/condition variable을 destroy하기 전 다른 process가 접근하지 않는다는 종료 합의를 가져야 한다. 이름 unlink와 mapping 수명은 별개라서 creator가 일찍 `shm_unlink`해 새 참가자의 open을 막되 기존 mapping은 유지하는 패턴도 가능하다.

### System V 공유 메모리

System V 공유 메모리는 오래되었지만 여전히 사용된다:

```c
#include <sys/shm.h>

// 생성/접근
key_t key = ftok("/tmp", 'A');
int shm_id = shmget(key, 4096, IPC_CREAT | 0666);

// 연결
void *ptr = shmat(shm_id, NULL, 0);

// 사용
strcpy(ptr, "Hello from System V");

// 분리
shmdt(ptr);

// 삭제
shmctl(shm_id, IPC_RMID, NULL);
```

### mmap 기반 공유 메모리

일반 파일도 공유 메모리로 사용할 수 있다:

```c
// 프로세스 A
int fd = open("shared_file.dat", O_RDWR | O_CREAT, 0666);
ftruncate(fd, 4096);
void *ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
strcpy(ptr, "Shared via file");
munmap(ptr, 4096);
close(fd);

// 프로세스 B
int fd = open("shared_file.dat", O_RDWR);
void *ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
printf("%s\n", (char*)ptr);
munmap(ptr, 4096);
close(fd);
```

**주의:** 파일 기반 mmap은 디스크 I/O가 발생할 수 있다. 순수 메모리만 원하면 POSIX shm이나 `tmpfs`를 사용한다.

### 커널 구현: 공유 메모리

POSIX 공유 메모리는 `tmpfs` 위에 구현된다 (`mm/shmem.c`):

```c
/* mm/shmem.c */
struct shmem_inode_info {
    spinlock_t lock;
    unsigned long flags;
    unsigned long alloced;      /* 할당된 페이지 수 */
    struct shared_policy policy;
    struct list_head swaplist;
    struct inode vfs_inode;
};
```

`shm_open`은 `/dev/shm/` 아래에 파일을 생성한다:

```bash
$ ls -l /dev/shm/
-rw-r--r-- 1 user user 4096 Dec 24 10:00 my_shm
```

`mmap`이 호출되면:

1. VMA (Virtual Memory Area) 생성
2. 페이지 테이블 엔트리 설정
3. 물리 페이지 할당 (lazy - 첫 접근 시)

```c
/* mm/mmap.c */
unsigned long do_mmap(struct file *file, unsigned long addr,
                      unsigned long len, unsigned long prot,
                      unsigned long flags, unsigned long pgoff)
{
    struct mm_struct *mm = current->mm;
    struct vm_area_struct *vma;

    /* VMA 할당 */
    vma = vm_area_alloc(mm);
    vma->vm_start = addr;
    vma->vm_end = addr + len;
    vma->vm_flags = calc_vm_prot_bits(prot, flags);
    vma->vm_file = get_file(file);
    vma->vm_pgoff = pgoff;

    /* VMA를 프로세스의 주소 공간에 삽입 */
    insert_vm_struct(mm, vma);

    return addr;
}
```

여러 프로세스가 같은 파일을 `MAP_SHARED`로 매핑하면, 같은 물리 페이지를 가리키게 된다.

## 6. Unix Domain Socket

### 소켓의 장점

**Unix Domain Socket**은 다음 장점을 가진다:

- **양방향**: 한 소켓으로 송수신
- **연결 지향**: TCP처럼 신뢰성 있는 스트림
- **데이터그램 지원**: UDP처럼 메시지 경계 유지
- **파일 디스크립터 전달**: 다른 프로세스에게 fd 전달 가능
- **네트워크 소켓과 API 동일**: 코드 재사용 쉬움

### Stream 소켓 (SOCK_STREAM)

TCP와 유사한 연결 지향 통신:

```c
#include <sys/socket.h>
#include <sys/un.h>

// 서버
int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);

struct sockaddr_un addr;
memset(&addr, 0, sizeof(addr));
addr.sun_family = AF_UNIX;
strncpy(addr.sun_path, "/tmp/my_socket", sizeof(addr.sun_path) - 1);

unlink("/tmp/my_socket");  // 이전 소켓 파일 삭제
bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
listen(listen_fd, 5);

printf("Server listening on /tmp/my_socket\n");

while (1) {
    int client_fd = accept(listen_fd, NULL, NULL);

    char buf[256];
    ssize_t n = read(client_fd, buf, sizeof(buf));
    printf("Received: %.*s\n", (int)n, buf);

    write(client_fd, "ACK", 3);
    close(client_fd);
}

// 클라이언트
int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);

struct sockaddr_un addr;
memset(&addr, 0, sizeof(addr));
addr.sun_family = AF_UNIX;
strncpy(addr.sun_path, "/tmp/my_socket", sizeof(addr.sun_path) - 1);

connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr));

write(sock_fd, "Hello, Server!", 14);

char buf[256];
ssize_t n = read(sock_fd, buf, sizeof(buf));
printf("Server replied: %.*s\n", (int)n, buf);

close(sock_fd);
```

### Datagram 소켓 (SOCK_DGRAM)

UDP와 유사한 메시지 경계 유지 통신:

```c
// 서버
int sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0);

struct sockaddr_un addr;
memset(&addr, 0, sizeof(addr));
addr.sun_family = AF_UNIX;
strncpy(addr.sun_path, "/tmp/dgram_socket", sizeof(addr.sun_path) - 1);

unlink("/tmp/dgram_socket");
bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr));

while (1) {
    char buf[256];
    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);

    ssize_t n = recvfrom(sock_fd, buf, sizeof(buf), 0,
                         (struct sockaddr*)&client_addr, &client_len);

    printf("Received: %.*s\n", (int)n, buf);

    sendto(sock_fd, "ACK", 3, 0,
           (struct sockaddr*)&client_addr, client_len);
}

// 클라이언트
int sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0);

struct sockaddr_un server_addr;
memset(&server_addr, 0, sizeof(server_addr));
server_addr.sun_family = AF_UNIX;
strncpy(server_addr.sun_path, "/tmp/dgram_socket", sizeof(server_addr.sun_path) - 1);

sendto(sock_fd, "Hello", 5, 0,
       (struct sockaddr*)&server_addr, sizeof(server_addr));

char buf[256];
ssize_t n = recvfrom(sock_fd, buf, sizeof(buf), 0, NULL, NULL);
printf("Reply: %.*s\n", (int)n, buf);

close(sock_fd);
```

### 파일 디스크립터 전달

Unix 소켓의 강력한 기능: **fd 전달**

```c
#include <sys/socket.h>

// 송신자: fd를 다른 프로세스에게 전달
void send_fd(int socket, int fd_to_send) {
    struct msghdr msg = {0};
    struct iovec iov[1];
    char buf[1] = {'X'};  // 더미 데이터

    iov[0].iov_base = buf;
    iov[0].iov_len = 1;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    // 제어 메시지 (ancillary data)
    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    *(int*)CMSG_DATA(cmsg) = fd_to_send;

    sendmsg(socket, &msg, 0);
}

// 수신자: fd 받기
int receive_fd(int socket) {
    struct msghdr msg = {0};
    struct iovec iov[1];
    char buf[1];

    iov[0].iov_base = buf;
    iov[0].iov_len = 1;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    recvmsg(socket, &msg, 0);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    return *(int*)CMSG_DATA(cmsg);
}

// 사용 예시
// 프로세스 A: 파일을 열고 fd 전달
int file_fd = open("secret.txt", O_RDONLY);
send_fd(socket, file_fd);

// 프로세스 B: fd 받아서 읽기
int received_fd = receive_fd(socket);
char buf[100];
read(received_fd, buf, sizeof(buf));  // secret.txt 읽기 가능!
```

**활용 사례:**
- systemd의 socket activation
- 웹 서버의 worker 프로세스에게 클라이언트 소켓 전달
- 권한 분리 (privileged 프로세스가 fd를 열고 unprivileged에게 전달)

### Abstract Namespace

Linux는 파일시스템 없이 소켓을 만들 수 있다:

```c
#include <stddef.h>

const char *name = "my_abstract_socket";
size_t name_len = strlen(name);

struct sockaddr_un addr;
memset(&addr, 0, sizeof(addr));
addr.sun_family = AF_UNIX;
addr.sun_path[0] = '\0';  // 첫 바이트는 abstract namespace 표식
if (name_len > sizeof(addr.sun_path) - 1) {
    /* 이름이 너무 김 */
}
memcpy(addr.sun_path + 1, name, name_len);

socklen_t addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path)
                                + 1 + name_len);
bind(sock_fd, (struct sockaddr*)&addr, addr_len);

// /tmp에 파일이 생기지 않음!
// netstat -lx로 확인 가능
```

장점:
- 파일시스템 권한 문제 없음
- 자동 정리 (프로세스 종료 시)
- 이름 충돌 덜함 (namespace 분리)

## 7. 세마포어: 동기화 프리미티브

### 세마포어의 필요성

공유 자원 (공유 메모리, 파일 등)을 여러 프로세스가 접근하면 **경쟁 조건(race condition)**이 발생한다.

**세마포어(Semaphore)**는 Edsger Dijkstra가 1965년 발명한 동기화 프리미티브다:

- **카운터**: 사용 가능한 자원 수
- **P (wait, down)**: 카운터 감소, 0이면 블록
- **V (signal, up)**: 카운터 증가, 대기자 깨우기

### POSIX 세마포어

POSIX는 두 종류의 세마포어를 제공한다:

**1) Named 세마포어**

```c
#include <semaphore.h>

// 생성
sem_t *sem = sem_open("/my_semaphore", O_CREAT, 0644, 1);  // 초기값 1

// P (wait)
sem_wait(sem);

// Critical Section
shared_resource++;

// V (signal)
sem_post(sem);

// 정리
sem_close(sem);
sem_unlink("/my_semaphore");
```

**2) Unnamed 세마포어**

공유 메모리에 배치:

```c
// 공유 메모리 생성
int shm_fd = shm_open("/shared", O_CREAT | O_RDWR, 0666);
ftruncate(shm_fd, sizeof(sem_t));
sem_t *sem = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

// 프로세스 간 공유 세마포어 초기화
sem_init(sem, 1, 1);  // pshared=1, 초기값=1

// 사용
sem_wait(sem);
// ... critical section ...
sem_post(sem);

// 정리
sem_destroy(sem);
munmap(sem, sizeof(sem_t));
```

### 생산자-소비자 문제

세마포어로 경전적인 생산자-소비자 문제를 해결:

```c
#define BUFFER_SIZE 10

struct shared_buffer {
    sem_t mutex;        // 버퍼 접근 보호
    sem_t empty;        // 빈 슬롯 수
    sem_t full;         // 찬 슬롯 수
    int buffer[BUFFER_SIZE];
    int in;             // 생산자 인덱스
    int out;            // 소비자 인덱스
};

// 초기화
sem_init(&buf->mutex, 1, 1);
sem_init(&buf->empty, 1, BUFFER_SIZE);
sem_init(&buf->full, 1, 0);
buf->in = buf->out = 0;

// 생산자
void produce(struct shared_buffer *buf, int item) {
    sem_wait(&buf->empty);      // 빈 슬롯 대기
    sem_wait(&buf->mutex);      // 버퍼 잠금

    buf->buffer[buf->in] = item;
    buf->in = (buf->in + 1) % BUFFER_SIZE;

    sem_post(&buf->mutex);      // 버퍼 잠금 해제
    sem_post(&buf->full);       // 찬 슬롯 증가
}

// 소비자
int consume(struct shared_buffer *buf) {
    sem_wait(&buf->full);       // 찬 슬롯 대기
    sem_wait(&buf->mutex);      // 버퍼 잠금

    int item = buf->buffer[buf->out];
    buf->out = (buf->out + 1) % BUFFER_SIZE;

    sem_post(&buf->mutex);      // 버퍼 잠금 해제
    sem_post(&buf->empty);      // 빈 슬롯 증가

    return item;
}
```

### System V 세마포어

System V 세마포어는 **세마포어 집합**을 지원한다:

```c
#include <sys/sem.h>

// 세마포어 집합 생성 (3개)
key_t key = ftok("/tmp", 'S');
int sem_id = semget(key, 3, IPC_CREAT | 0666);

// 초기화
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

union semun arg;
arg.val = 1;
semctl(sem_id, 0, SETVAL, arg);  // 0번 세마포어를 1로

// P 연산
struct sembuf op;
op.sem_num = 0;     // 0번 세마포어
op.sem_op = -1;     // 1 감소
op.sem_flg = 0;
semop(sem_id, &op, 1);

// Critical Section

// V 연산
op.sem_op = 1;      // 1 증가
semop(sem_id, &op, 1);

// 정리
semctl(sem_id, 0, IPC_RMID);
```

**원자적 다중 연산:**

System V의 강력한 기능은 여러 세마포어를 **원자적으로** 조작하는 것이다:

```c
// 두 자원을 동시에 확보 (데드락 방지)
struct sembuf ops[2];
ops[0].sem_num = 0;
ops[0].sem_op = -1;
ops[0].sem_flg = 0;

ops[1].sem_num = 1;
ops[1].sem_op = -1;
ops[1].sem_flg = 0;

semop(sem_id, ops, 2);  // 둘 다 성공하거나 둘 다 블록
```

### 커널 구현: 세마포어

Linux의 glibc에서 POSIX 세마포어는 사용자 공간의 원자 연산과 futex 느린 경로를 결합한다. 아래는 현재 구현의 핵심 제어 흐름을 단순화한 것이다. 실제 `data`에는 값과 대기자 수가 함께 인코딩되며, 값을 얻지 못한 스레드만 `__new_sem_wait_slow64`로 들어간다.

```c
/* glibc nptl/sem_wait.c, nptl/sem_waitcommon.c의 개념 축약 */
int __new_sem_wait(sem_t *sem) {
    struct new_sem *isem = (struct new_sem *)sem;

    uint64_t d = atomic_load_relaxed(&isem->data);
    do {
        if ((d & SEM_VALUE_MASK) == 0)
            return __new_sem_wait_slow64(isem, CLOCK_REALTIME, NULL);
    } while (!atomic_compare_exchange_weak_acquire(&isem->data, &d, d - 1));

    return 0;  // fast path에서 토큰 하나를 획득
}
```

System V 세마포어는 커널에 직접 구현된다 (`ipc/sem.c`):

```c
/* ipc/sem.c */
struct sem_array {
    struct kern_ipc_perm sem_perm;
    struct sem *sems;           /* 세마포어 배열 */
    struct list_head pending_alter;
    struct list_head pending_const;
    time_t sem_otime;
    time_t sem_ctime;
    unsigned short sem_nsems;   /* 배열 크기 */
};

struct sem {
    int semval;                 /* 현재 값 */
    struct pid *sempid;         /* 마지막 조작 프로세스 */
    spinlock_t lock;
    struct list_head pending_alter;
};
```

## 8. IPC 성능 비교

### 벤치마크 설정

IPC 이름만 바꾼 단일 평균값은 공정한 비교가 아니다. 먼저 필요한 의미를 맞춘다.

| 의미 축 | 비교 전에 고정할 질문 |
|---|---|
| framing | byte stream인가, message boundary가 필요한가 |
| topology | 같은 thread, 같은 core의 두 process, 다른 core, 다른 NUMA node 중 무엇인가 |
| waiting | busy-poll, spin-then-park, blocking syscall 중 무엇인가 |
| payload | 0·64B·4KiB·1MiB 등 어느 분포인가 |
| ownership | sender buffer를 언제 재사용할 수 있는가 |
| backpressure | queue가 가득 찼을 때 block, drop, overwrite 중 무엇인가 |
| wakeup | 매 message마다 깨우는가, batch마다 깨우는가 |
| security | credential 전달, namespace, descriptor passing이 필요한가 |

최소한 두 개의 workload를 분리한다.

1. **ping-pong latency**: 요청 하나를 보내고 응답 하나를 받을 때의 RTT 분포
2. **streaming throughput**: 정해진 outstanding window와 backpressure 아래에서 전달한 bytes/s와 message/s

### 데이터 이동 비용 해석

pipe와 일반 Unix socket send/receive는 보통 sender user memory에서 kernel buffer로, 다시 receiver user memory로 payload를 복사한다. 하지만 `splice`, page reference, socket option과 kernel version에 따라 경로가 달라질 수 있으므로 무조건 “정확히 두 번”이라고 단정하지 않는다.

공유 메모리는 두 주소 공간이 같은 physical page를 보게 해 별도 kernel payload buffer를 거치지 않을 수 있다. 그래도 생산자가 원본 객체를 shared ring으로 복사하거나 소비자가 local representation으로 변환하면 application copy는 남는다. cache-line ownership transfer, memory ordering, wakeup과 reclamation 비용도 사라지지 않는다.

### 결과 기록

```text
kernel / libc:
CPU / NUMA topology:
CPU affinity / frequency policy:
IPC capacity and message limit:
payload distribution:
producer / consumer count:
waiting and wakeup policy:
batch size / outstanding window:
warm-up / duration / repetitions:
RTT p50 / p99 / p99.9:
message/s and bytes/s:
producer + consumer CPU time:
context switches / migrations / faults:
drop, retry, blocked time:
```

CPU 사용률은 wall-clock percentage 하나가 아니라 생산자·소비자·kernel worker의 총 CPU time으로 비교한다. 2개 core를 완전히 쓰면 process 합계가 200%로 보일 수 있으며, 이는 그 자체로 오류가 아니다.

### 해석 가능한 결론

- 작은 control message에서는 syscall·wakeup·scheduler 경로가 큰 비중을 차지할 수 있다.
- 큰 payload에서는 memory bandwidth와 copy policy가 중요해진다.
- 공유 메모리는 빠른 결과를 낼 가능성이 있지만 잘못된 cache-line 배치나 busy polling으로 CPU와 tail latency를 더 쓸 수도 있다.
- message queue의 priority와 kernel persistence가 필요하면 단순 byte throughput만으로 socket보다 열등하다고 판정할 수 없다.
- Unix socket은 bidirectional communication, peer credential과 descriptor passing을 하나의 kernel interface에서 제공한다.

### 선택 가이드라인

**공유 메모리를 선택하라:**
- shared state나 대용량 payload의 별도 kernel copy를 피해야 함
- process-shared synchronization, crash recovery와 schema/version을 설계할 수 있음

**파이프/소켓을 선택하라:**
- 단순한 스트림 데이터
- kernel이 제공하는 blocking과 backpressure를 원함
- 양방향 연결, peer credential, fd 전달이 필요함(Unix socket)

**메시지 큐를 선택하라:**
- 메시지 경계 중요
- 우선순위 처리 필요
- queue limit과 비동기 통지의 운영 의미가 요구사항에 맞음

## 9. 커널 내부: IPC 서브시스템

### IPC 네임스페이스

Linux는 **IPC 네임스페이스**로 IPC 객체를 격리한다:

```c
/* include/linux/ipc_namespace.h */
struct ipc_namespace {
    refcount_t count;

    struct ipc_ids ids[3];      /* 0: sem, 1: msg, 2: shm */

    int sem_ctls[4];            /* 세마포어 제한 */
    int msg_ctlmax;             /* 메시지 큐 최대 크기 */
    size_t shm_ctlmax;          /* 공유 메모리 최대 크기 */
    /* ... */
};
```

각 네임스페이스는 독립적인 System V IPC 객체 집합을 가진다:

```bash
# 새 IPC 네임스페이스에서 실행
$ unshare --ipc bash
$ ipcs  # 비어 있음

# 원래 네임스페이스의 IPC 객체는 보이지 않음
```

### IPC ID 할당

System V IPC는 namespace 안에서 할당한 **ID**로 객체를 식별한다. 커널은 slot/index와 generation 성격의 sequence를 조합해 삭제된 객체의 낡은 ID가 새 객체를 가리키는 위험을 줄인다.

```c
// 개념 의사코드
int ipc_addid(struct ipc_ids *ids, struct kern_ipc_perm *new)
{
    unsigned int index = allocate_free_index(ids);
    unsigned int sequence = next_sequence_for(index);
    new->id = encode_ipc_id(index, sequence);
    publish_object(ids, index, new);
    return new->id;
}
```

index와 sequence의 실제 bit 배치는 `IPCMNI` 설정과 커널 버전에 따라 달라질 수 있다. “항상 하위 16비트 index, 상위 16비트 sequence”를 userspace ABI로 가정하지 않는다.

### 메시지 큐 구현

System V 메시지 큐의 커널 구조 (`ipc/msg.c`):

```c
/* ipc/msg.c */
struct msg_queue {
    struct kern_ipc_perm q_perm;
    time_t q_stime;             /* 마지막 send 시각 */
    time_t q_rtime;             /* 마지막 receive 시각 */
    time_t q_ctime;             /* 마지막 change 시각 */

    unsigned long q_cbytes;     /* 현재 바이트 수 */
    unsigned long q_qnum;       /* 현재 메시지 수 */
    unsigned long q_qbytes;     /* 최대 바이트 수 */

    pid_t q_lspid;              /* 마지막 send PID */
    pid_t q_lrpid;              /* 마지막 receive PID */

    struct list_head q_messages;    /* 메시지 리스트 */
    struct list_head q_receivers;   /* 블록된 수신자들 */
    struct list_head q_senders;     /* 블록된 송신자들 */
};

struct msg_msg {
    struct list_head m_list;
    long m_type;                /* 메시지 타입 */
    size_t m_ts;                /* 메시지 크기 */
    struct msg_msgseg *next;    /* 큰 메시지 체인 */
    /* 메시지 데이터 */
};
```

**송신 로직:**

```c
static long do_msgsnd(int msqid, long mtype, void __user *mtext,
                      size_t msgsz, int msgflg)
{
    struct msg_queue *msq;
    struct msg_msg *msg;

    /* 메시지 큐 찾기 */
    msq = msq_obtain_object_check(ns, msqid);

    /* 메시지 할당 및 복사 */
    msg = load_msg(mtext, msgsz);
    msg->m_type = mtype;

    /* 공간 확인 */
    while (msq->q_cbytes + msgsz > msq->q_qbytes) {
        if (msgflg & IPC_NOWAIT)
            return -EAGAIN;

        /* 블록 */
        schedule();
    }

    /* 메시지 삽입 */
    list_add_tail(&msg->m_list, &msq->q_messages);
    msq->q_cbytes += msgsz;
    msq->q_qnum++;

    /* 대기 중인 수신자 깨우기 */
    wake_up_q(&wake_q);

    return 0;
}
```

### 공유 메모리 페이지 폴트

공유 메모리 접근 시 페이지 폴트가 발생하면 tmpfs 기반 매핑은 `shmem_fault`(mm/shmem.c)를 거쳐 페이지 캐시에서 페이지를 찾거나 할당한다. 아래는 이해를 돕기 위한 익명 페이지 폴트 경로의 단순화된 형태다(공유 매핑은 같은 골격에서 페이지 출처만 페이지 캐시로 바뀐다):

```c
/* mm/memory.c (단순화) */
vm_fault_t do_anonymous_page(struct vm_fault *vmf)
{
    struct vm_area_struct *vma = vmf->vma;
    struct page *page;

    /* 페이지 할당 */
    page = alloc_zeroed_user_highpage_movable(vma, vmf->address);

    /* 페이지 테이블 엔트리 설정 */
    entry = mk_pte(page, vma->vm_page_prot);
    entry = pte_mkwrite(pte_mkdirty(entry));

    set_pte_at(vma->vm_mm, vmf->address, vmf->pte, entry);

    /* 다른 프로세스도 같은 물리 페이지를 매핑 */
    // MAP_SHARED 플래그로 인해 페이지 캐시에서 공유

    return 0;
}
```

### Unix 소켓 버퍼

Unix 소켓은 `sk_buff` 구조체로 데이터를 관리한다:

```c
/* include/linux/skbuff.h */
struct sk_buff {
    struct sk_buff *next;
    struct sk_buff *prev;

    ktime_t tstamp;
    struct sock *sk;

    unsigned int len;           /* 데이터 길이 */
    unsigned int data_len;      /* 조각화된 데이터 길이 */

    unsigned char *head;        /* 버퍼 시작 */
    unsigned char *data;        /* 데이터 시작 */
    unsigned char *tail;        /* 데이터 끝 */
    unsigned char *end;         /* 버퍼 끝 */
    /* ... */
};
```

**송신:**

```c
/* net/unix/af_unix.c */
static int unix_stream_sendmsg(struct socket *sock, struct msghdr *msg, size_t len)
{
    struct sock *sk = sock->sk;
    struct sock *other = unix_peer(sk);
    struct sk_buff *skb;

    /* sk_buff 할당 */
    skb = sock_alloc_send_skb(sk, len, msg->msg_flags & MSG_DONTWAIT, &err);

    /* 사용자 데이터 복사 */
    if (copy_from_iter(skb_put(skb, len), len, &msg->msg_iter) != len) {
        kfree_skb(skb);
        return -EFAULT;
    }

    /* 수신 소켓의 큐에 추가 */
    skb_queue_tail(&other->sk_receive_queue, skb);

    /* 수신자 깨우기 */
    other->sk_data_ready(other);

    return len;
}
```

## 10. 고급 IPC 패턴

### 요청-응답 패턴

클라이언트-서버에서 흔한 패턴:

```c
// 서버
struct request {
    int request_id;
    char reply_queue[64];
    char data[256];
};

struct response {
    int request_id;
    int status;
    char result[256];
};

/*
 * 아래 구조체를 그대로 mq_send 하는 방식은 동일한 ABI, 정렬, 엔디언,
 * 타입 크기를 사용하는 로컬 프로세스끼리만 쓸 수 있는 예제다.
 * 버전이 다른 프로그램이나 다른 머신과 통신한다면 고정 폭 정수와
 * 명시적 바이트 순서를 가진 직렬화 포맷을 사용해야 한다.
 */

// 요청 큐: 같은 서비스 계정의 프로세스만 사용한다고 가정
mqd_t req_mq = mq_open("/request_queue",
                       O_CREAT | O_EXCL | O_RDONLY, 0600, &attr);
if (req_mq == (mqd_t)-1)
    fail("mq_open request queue");

while (1) {
    struct request req;
    ssize_t received = mq_receive(req_mq, (char*)&req, sizeof(req), NULL);
    if (received == -1)
        continue;  // 실제 코드는 EINTR과 종료 조건을 구분
    if ((size_t)received != sizeof(req))
        continue;

    // 요청 처리
    struct response resp;
    resp.request_id = req.request_id;
    resp.status = process_request(&req, resp.result);

    // 응답 전송 (클라이언트가 요청에 넣은 예측 불가능한 큐 이름)
    char resp_name[64];
    memcpy(resp_name, req.reply_queue, sizeof(resp_name));
    resp_name[sizeof(resp_name) - 1] = '\0';
    mqd_t resp_mq = mq_open(resp_name, O_WRONLY);
    if (resp_mq != (mqd_t)-1) {
        mq_send(resp_mq, (char*)&resp, sizeof(resp), 0);
        mq_close(resp_mq);
    }
}

// 클라이언트
// CSPRNG로 충분히 긴 nonce를 만들고 응답 큐를 원자적으로 생성한다.
// random_nonce_hex()는 운영체제 CSPRNG를 사용한다고 가정한다.
char resp_name[64];
char nonce[33];
random_nonce_hex(nonce, sizeof(nonce));
snprintf(resp_name, sizeof(resp_name), "/response_%s", nonce);
mqd_t resp_mq = mq_open(resp_name, O_CREAT | O_EXCL | O_RDONLY, 0600, &attr);
if (resp_mq == (mqd_t)-1)
    fail("mq_open response queue");

// 요청 전송
mqd_t req_mq = mq_open("/request_queue", O_WRONLY);
struct request req = {.request_id = 1};
strncpy(req.reply_queue, resp_name, sizeof(req.reply_queue) - 1);
strcpy(req.data, "GET /data");
mq_send(req_mq, (char*)&req, sizeof(req), 0);

// 응답 대기
struct response resp;
mq_receive(resp_mq, (char*)&resp, sizeof(resp), NULL);
printf("Status: %d, Result: %s\n", resp.status, resp.result);

mq_close(resp_mq);
mq_unlink(resp_name);
```

PID와 시각으로 만든 이름은 PID 재사용과 선점 생성에 취약하다. 공격자가 먼저 같은 큐를 만들면 응답을 가로채거나 서버를 잘못된 큐에 연결할 수 있다. 예측 불가능한 nonce, `O_CREAT | O_EXCL`, 소유자 전용 권한을 함께 써야 이름이 일회성 capability에 가까워진다. 보안 경계가 중요한 서비스라면 응답 큐 이름을 요청 본문에서 신뢰하기보다 피어 자격 증명을 확인할 수 있는 Unix domain socket 연결이나 인증된 프로토콜을 사용한다.

### 발행-구독 패턴

```c
// 초기화 담당 프로세스가 mutex/condition variable을 process-shared로
// 구성해 둔 공유 메모리를 발행자와 구독자가 연다.
int shm_fd = shm_open("/pubsub", O_RDWR, 0);

struct pubsub {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    uint64_t version;   // unsigned wrap은 정의되지만 wrap 정책은 별도로 필요
    char data[1024];
};

struct pubsub *ps = mmap(...);

// 발행
pthread_mutex_lock(&ps->mutex);
strcpy(ps->data, "New event occurred");
ps->version++;
pthread_cond_broadcast(&ps->cond);  // 모든 구독자 깨우기
pthread_mutex_unlock(&ps->mutex);

// 구독자 1
uint64_t last_version = 0;
while (1) {
    pthread_mutex_lock(&ps->mutex);
    while (ps->version == last_version) {
        pthread_cond_wait(&ps->cond, &ps->mutex);
    }
    printf("Subscriber 1: %s (v%" PRIu64 ")\n", ps->data, ps->version);
    last_version = ps->version;
    pthread_mutex_unlock(&ps->mutex);
}

// 구독자 2 (동일한 로직)
```

이 구조는 **이벤트 로그가 아니라 최신 상태 알림**이다. 발행자가 구독자보다 빠르면 구독자는 버전이 여러 번 증가했다는 사실만 보고 중간 값을 잃는다. 모든 이벤트를 보존해야 한다면 고정 크기 링 버퍼에 레코드 순번과 구독자별 읽기 커서를 두고, 생산자가 읽지 않은 항목을 덮었을 때 overrun을 검출해야 한다. 메시지 큐나 로그 기반 IPC를 선택할 수도 있다. 버전 래핑을 정의하고, 소유 프로세스가 잠금을 보유한 채 종료되는 경우에는 robust process-shared mutex의 복구 절차도 필요하다.

### 풀링 패턴

여러 워커가 작업 큐에서 가져가기:

```c
// 작업 큐
mqd_t task_mq = mq_open("/task_queue", O_CREAT | O_RDWR, 0644, &attr);

// 마스터: 작업 추가
for (int i = 0; i < 100; i++) {
    char task[64];
    snprintf(task, sizeof(task), "Task %d", i);
    mq_send(task_mq, task, strlen(task), 0);
}

// 워커들 (여러 프로세스)
while (1) {
    char task[64];
    ssize_t n = mq_receive(task_mq, task, sizeof(task), NULL);
    if (n < 0) break;

    printf("Worker %d processing: %.*s\n", getpid(), (int)n, task);
    process_task(task);
}
```

### immutable snapshot과 grace period

atomic index와 슬롯 두 개만 둔 double buffer는 RCU가 아니다. reader가 이전 슬롯을 복사하는 동안 writer가 두 번째 update에서 그 슬롯을 재사용하면 C memory model의 data race와 torn snapshot이 생긴다. release/acquire로 index만 보호해도 객체의 재사용 시점을 해결하지 못한다.

RCU 계열의 핵심 계약은 다음과 같다.

```c
// 개념 의사코드다. cross-process grace-period 구현이 필요하다.
void publish(struct shared_state *s, uint64_t next_offset)
{
    uint64_t old_offset =
        atomic_exchange_explicit(&s->current_offset, next_offset,
                                 memory_order_release);

    wait_for_all_preexisting_readers();  // grace period
    reclaim_slot(old_offset);
}

void read_snapshot(struct shared_state *s, void *dst)
{
    reader_enter();
    uint64_t offset =
        atomic_load_explicit(&s->current_offset, memory_order_acquire);
    const struct snapshot *p = snapshot_at_local_mapping(offset);
    copy_immutable_snapshot(dst, p);
    reader_exit();
}
```

공유 mapping은 프로세스마다 다른 virtual address에 놓일 수 있으므로 shared metadata에는 raw pointer 대신 offset이나 stable index를 저장한다. 공유 메모리에 둔 C atomic이 구현 내부의 process-private lock에 의존하지 않는지도 `atomic_is_lock_free`와 대상 ABI로 확인한다. `reader_enter()`가 시작된 reader를 writer가 관찰할 수 있어야 하고, process crash가 reader state를 영원히 남겼을 때의 복구 정책도 필요하다. 일반 POSIX 프로그램에는 kernel RCU API가 그대로 제공되지 않는다. 단순성과 crash 복구가 더 중요하면 `PTHREAD_PROCESS_SHARED` rwlock이나 mutex를 사용하고, lock-free read가 실제 요구사항일 때 epoch, QSBR, hazard pointer 같은 reclamation protocol을 별도로 구현·검증한다.

## 11. IPC 보안과 권한

### Unix 권한 모델

**파이프/소켓:**
- 파일 디스크립터를 가진 프로세스만 접근
- `fork` 후 자식이 상속
- `SCM_RIGHTS`로 다른 프로세스에 전달 가능

**FIFO:**
- 파일시스템 권한 (`chmod`, `chown`)
- `/tmp/myfifo` → `rw-rw-r--`

**POSIX IPC:**
- 이름 기반 객체. 종류별로 가상 파일시스템 위치가 다르다.
 - 공유 메모리/세마포어: `/dev/shm/`
 - 메시지 큐: `/dev/mqueue/`
- 파일시스템 권한 적용
- `shm_open(..., 0600)` → owner만 접근

**System V IPC:**
- `struct ipc_perm`에 권한 저장
- `ipcs -m`으로 확인

```c
struct ipc_perm {
    key_t key;
    uid_t uid;      /* owner UID */
    gid_t gid;      /* owner GID */
    uid_t cuid;     /* creator UID */
    gid_t cgid;     /* creator GID */
    mode_t mode;    /* 권한 비트 */
    /* ... */
};
```

### 권한 체크

커널은 IPC 접근 시 권한을 확인한다:

```c
/* ipc/util.c */
int ipcperms(struct ipc_namespace *ns, struct kern_ipc_perm *ipcp, short flag)
{
    kuid_t euid = current_euid();
    int requested = (flag >> 6) | (flag >> 3) | flag;
    int granted = ipcp->mode;

    if (uid_eq(euid, ipcp->cuid) || uid_eq(euid, ipcp->uid))
        granted >>= 6;
    else if (in_group_p(ipcp->cgid) || in_group_p(ipcp->gid))
        granted >>= 3;

    if ((requested & ~granted & 0007) != 0 &&
        !ns_capable(ns->user_ns, CAP_IPC_OWNER))
        return -1;

    return security_ipc_permission(ipcp, flag); // LSM 최종 검사
}
```

`flag`는 호출 위치에 따라 소유자·그룹·기타 자리에 권한 비트를 담을 수 있으므로 먼저 하위 3비트로 접는다. DAC 비트가 부족해도 해당 IPC namespace를 소유한 user namespace에서 `CAP_IPC_OWNER`가 있으면 통과할 수 있으며, 마지막에는 LSM 훅이 별도의 보안 정책을 적용한다.

### 네임스페이스 격리

컨테이너는 IPC 네임스페이스로 격리된다:

```bash
# 새 IPC 네임스페이스 생성
$ unshare --ipc bash

# 이 네임스페이스에서 공유 메모리 생성
$ ipcmk -M 1024
Shared memory id: 0

# 호스트 네임스페이스에서는 보이지 않음
```

Docker 컨테이너는 기본적으로 독립적인 IPC 네임스페이스를 가진다:

```bash
$ docker run --ipc=host ...  # 호스트 IPC 공유
$ docker run --ipc=container:other ...  # 다른 컨테이너와 공유
```

### 보안 모범 사례

**1) 최소 권한 원칙**

```c
// 나쁜 예: 모두에게 읽기/쓰기
shm_open("/data", O_CREAT | O_RDWR, 0666);

// 좋은 예: owner만 접근
shm_open("/data", O_CREAT | O_RDWR, 0600);
```

**2) 이름 충돌 방지**

```c
// 나쁜 예: 예측 가능한 이름
shm_open("/shared", ...);

// 좋은 예: 예측 불가능한 이름을 배타적으로 생성
char name[64];
char nonce[33];
random_nonce_hex(nonce, sizeof(nonce));  // 운영체제 CSPRNG 사용
snprintf(name, sizeof(name), "/app_%s", nonce);
int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
```

이름이 고유해 보인다는 사실만으로 접근 권한이 생기지는 않는다. 예측 불가능한 이름은 추측 비용을 높이는 보조 capability일 뿐이고, 파일 모드, 사용자·IPC 네임스페이스, 피어 자격 증명과 프로토콜 인증이 실제 보안 경계를 만든다.

**3) 정리**

```c
// 새 참가자가 더 이상 이름으로 열 필요가 없는 합의된 시점에 unlink
shm_unlink("/myshm");
mq_unlink("/myqueue");
sem_unlink("/mysem");
unlink("/tmp/myfifo");
```

POSIX shared memory와 named semaphore/message queue의 `unlink`는 이름을 제거한다. 이미 열린 핸들은 계속 사용할 수 있으므로 creator가 생성 직후 이름을 지우는 패턴도 가능하지만, 이후 프로세스가 이름으로 참가해야 하는 설계에는 맞지 않는다. 종료 때 무조건 지운다는 규칙보다 소유권과 참가 시점을 프로토콜로 정해야 한다.

**4) 입력 검증**

```c
// 공유 메모리에서 읽은 데이터는 신뢰하지 말 것
struct shared_data *data = mmap(...);

// 나쁜 예: 검증 없이 사용
strcpy(local_buf, data->message);  // 버퍼 오버플로우 가능!

// 좋은 예: writer와 같은 process-shared mutex 아래에서 길이와 본문을 스냅샷
pthread_mutex_lock(&data->mutex);
size_t len = data->msg_len;
if (len < sizeof(data->message) && len < sizeof(local_buf)) {
    memcpy(local_buf, data->message, len);
    local_buf[len] = '\0';
} else {
    // 잘못된 레코드 거부
}
pthread_mutex_unlock(&data->mutex);
```

길이를 한 번 검사한 뒤 잠금 없이 본문을 복사하면 writer가 그 사이에 길이와 내용을 바꾸는 TOCTOU 경쟁이 생긴다. 길이와 payload를 같은 동기화 구간에서 스냅샷하고, 원본과 목적지 양쪽 경계를 검사해야 한다. 프로세스가 잠금 보유 중 죽을 수 있다면 robust mutex 복구 또는 commit marker와 세대 번호를 가진 lock-free 프로토콜이 필요하다.

## 12. 실전 예제: 멀티프로세스 웹 서버

### 아키텍처

```text
Master Process
    │
    ├── Worker 1 ──┐
    ├── Worker 2   │
    ├── Worker 3   ├──> Unix Socket (클라이언트 연결)
    └── Worker 4 ──┘
         │
         └──> Shared Memory (통계, 캐시)
```

### 구현

```c
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NUM_WORKERS 4

// 공유 메모리 구조
struct server_stats {
    pthread_mutex_t mutex;
    uint64_t total_requests;
    uint64_t worker_requests[NUM_WORKERS];
};

static void worker_main(int worker_id, int listen_fd,
                        struct server_stats *stats);
static void handle_request(int client_fd);

// 마스터 프로세스
int main() {
    // 공유 메모리 생성
    int shm_fd = shm_open("/webserver_stats", O_CREAT | O_RDWR, 0600);
    ftruncate(shm_fd, sizeof(struct server_stats));
    struct server_stats *stats = mmap(NULL, sizeof(struct server_stats),
                                       PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    // 뮤텍스 초기화 (프로세스 간 공유)
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&stats->mutex, &attr);

    stats->total_requests = 0;

    // 리스닝 소켓 생성
    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strcpy(addr.sun_path, "/tmp/webserver.sock");
    unlink(addr.sun_path);
    bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, 128);

    // 워커 프로세스 생성
    for (int i = 0; i < NUM_WORKERS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            worker_main(i, listen_fd, stats);
            exit(0);
        }
    }

    // 통계 모니터링
    while (1) {
        sleep(10);

        pthread_mutex_lock(&stats->mutex);
        printf("Total requests: %" PRIu64 "\n", stats->total_requests);
        for (int i = 0; i < NUM_WORKERS; i++) {
            printf("  Worker %d: %" PRIu64 "\n",
                   i, stats->worker_requests[i]);
        }
        pthread_mutex_unlock(&stats->mutex);
    }
}

// 워커 프로세스
static void worker_main(int worker_id, int listen_fd,
                        struct server_stats *stats) {
    while (1) {
        // accept()는 여러 프로세스가 동시에 호출 가능
        // 커널의 exclusive wakeup이 불필요한 다중 깨우기를 줄이지만,
        // 여러 worker 사이의 accept 경쟁 비용까지 없애지는 않는다.
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) continue;

        // 요청 처리
        handle_request(client_fd);
        close(client_fd);

        // 통계 업데이트
        pthread_mutex_lock(&stats->mutex);
        stats->total_requests++;
        stats->worker_requests[worker_id]++;
        pthread_mutex_unlock(&stats->mutex);
    }
}

static void handle_request(int client_fd) {
    char buf[4096];
    ssize_t n = read(client_fd, buf, sizeof(buf));

    // 간단한 HTTP 응답
    const char *response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "Hello, World!";

    write(client_fd, response, strlen(response));
}
```

### 성능 최적화

**1) Zero-copy 전송**

```c
// sendfile()로 사용자 공간 payload copy를 피하는 전송 경로 요청
int file_fd = open("static.html", O_RDONLY);
struct stat st;
fstat(file_fd, &st);

// 헤더 전송
dprintf(client_fd, "HTTP/1.1 200 OK\r\nContent-Length: %" PRIdMAX "\r\n\r\n",
        (intmax_t)st.st_size);

// 파일 내용 전송 (커널에서 직접)
sendfile(client_fd, file_fd, NULL, st.st_size);

close(file_fd);
```

**2) 연결 풀 (connection pooling)**

```c
// 공유 메모리에 연결 풀
struct connection_pool {
    pthread_mutex_t mutex;
    int available[MAX_CONNECTIONS];
    int count;
};

int acquire_connection(struct connection_pool *pool) {
    pthread_mutex_lock(&pool->mutex);
    while (pool->count == 0) {
        pthread_mutex_unlock(&pool->mutex);
        usleep(1000);
        pthread_mutex_lock(&pool->mutex);
    }
    int fd = pool->available[--pool->count];
    pthread_mutex_unlock(&pool->mutex);
    return fd;
}
```

## 13. 디버깅과 모니터링

### IPC 객체 확인

**System V IPC:**

```bash
# 모든 IPC 객체 나열
$ ipcs

# 공유 메모리만
$ ipcs -m
------ Shared Memory Segments --------
key        shmid      owner      perms      bytes      nattch
0x00000000 0          user       600        4096       2

# 메시지 큐
$ ipcs -q

# 세마포어
$ ipcs -s
```

**POSIX IPC:**

```bash
# 공유 메모리
$ ls -l /dev/shm/
-rw------- 1 user user 4096 Dec 24 10:00 my_shm

# 메시지 큐
$ ls -l /dev/mqueue/
-rw-r--r-- 1 user user 80 Dec 24 10:00 my_queue
```

### IPC 통계

**파이프/FIFO:**

```bash
# lsof로 열린 파일 확인
$ lsof -p 1234 | grep FIFO
myapp  1234 user  3r  FIFO  0,12  0t0  /tmp/myfifo

# strace로 IPC 호출 추적
$ strace -e trace=read,write,pipe,mq_send,mq_receive -p 1234
```

**공유 메모리:**

```bash
# /proc로 매핑 확인
$ cat /proc/1234/maps | grep shm
7f1234000000-7f1234001000 rw-s 00000000 00:01 12345 /dev/shm/my_shm
```

### 성능 프로파일링

```c
// client 관점의 ping-pong RTT 한 샘플
#include <time.h>

struct timespec start, end;
char reply[256];

clock_gettime(CLOCK_MONOTONIC, &start);

mq_send(mq, msg, len, 0);
mq_receive(reply_mq, reply, sizeof(reply), NULL);

clock_gettime(CLOCK_MONOTONIC, &end);

long latency_ns = (end.tv_sec - start.tv_sec) * 1000000000L +
                  (end.tv_nsec - start.tv_nsec);

printf("Latency: %ld ns\n", latency_ns);
```

한 번의 호출은 timer overhead, warm-up과 scheduler noise를 포함한다. 충분한 반복에서 raw sample을 보관해 percentile을 계산하고, empty-loop·clock 비용, timeout, error와 coordinated omission을 처리한다. producer 시각과 consumer 시각을 직접 빼는 one-way 측정은 clock source와 timestamp 지점의 계약을 먼저 맞춰야 한다.

**perf로 시스템 콜 추적:**

```bash
$ perf record -e 'syscalls:sys_enter_mq_*' -p 1234
$ perf report
```

### 메모리 릭 탐지

```bash
# valgrind로 IPC 메모리 릭 확인
$ valgrind --leak-check=full --trace-children=yes ./myapp

# 공유 메모리 정리 확인
$ ipcs -m | grep user
# 프로그램 종료 후에도 남아 있으면 릭

# 정리
$ ipcrm -m <shmid>
$ ipcrm -M <key>
```


---

## 결론

### IPC의 철학

UNIX의 IPC는 "mechanism, not policy" 철학을 따른다:

- **메커니즘**: 파이프, 공유 메모리, 소켓 등 다양한 도구 제공
- **정책**: 애플리케이션이 적절한 도구를 선택

각 IPC는 서로 다른 trade-off를 가진다:

| 특성 | 파이프 | 메시지 큐 | 공유 메모리 | 소켓 |
|------|--------|----------|-----------|------|
| 단순성 | 최고 | 중간 | 낮음 | 중간 |
| 성능 | 중간 | 낮음 | 최고 | 중간 |
| 유연성 | 낮음 | 중간 | 중간 | 최고 |
| 안전성 | 높음(자동 동기화) | 높음 | 낮음(직접 동기화) | 높음 |

### 현대 시스템에서의 IPC

**전통적 IPC의 한계:**
- 모두 로컬 머신에 제한
- 복잡한 동기화 (공유 메모리)
- 제한된 성능 (메시지 큐)

**현대적 대안:**
- **gRPC**: 언어 중립적 RPC, 네트워크 투명성
- **ZeroMQ**: 고성능 메시지 패턴
- **io_uring**: 최신 Linux 비동기 I/O
- **eBPF**: 커널-사용자 공간 통신

하지만 전통적 IPC는 여전히 중요하다:
- **간단함**: `pipe`는 여전히 가장 쉬운 IPC
- **표준화**: POSIX는 이식성 보장
- **대용량 데이터 경로**: 공유 메모리는 payload copy를 줄일 수 있음
- **통합**: 기존 시스템과의 호환성

### 핵심 교훈

IPC를 사용할 때 기억해야 할 것들:

1. **적절한 도구 선택**
 - 단순 → 파이프
 - 구조화 → 메시지 큐
 - 대용량·고처리량 + 별도 동기화 설계 가능 → 공유 메모리
 - 양방향/네트워크 → 소켓

2. **동기화 필수**
 - 공유 메모리는 세마포어/뮤텍스 필요
 - 파이프는 자동 동기화

3. **정리는 책임**
 - 생성한 IPC 객체는 명시적으로 삭제
 - `*_unlink`, `IPC_RMID`

4. **보안 고려**
 - 최소 권한 원칙
 - 입력 검증
 - 네임스페이스 격리

5. **성능 측정**
 - 추측하지 말고 측정
 - 벤치마크로 적절한 IPC 선택

## 더 읽을거리

- POSIX.1-2017 Base Specifications: Pipes and FIFOs, Message Queues, Semaphores, Shared Memory Objects (https://pubs.opengroup.org/onlinepubs/9699919799/)
- Linux 커널 소스(v5.10): `fs/pipe.c`, `ipc/msg.c`, `ipc/shm.c`, `ipc/sem.c`, `ipc/mqueue.c`, `mm/shmem.c`, `net/unix/af_unix.c`
- W. Richard Stevens, *UNIX Network Programming Volume 2: Interprocess Communications*, 2nd Edition
- Michael Kerrisk, *The Linux Programming Interface* 43~48장(IPC Overview, Pipes/FIFOs, System V IPC), 51~54장(POSIX IPC)
- Maurice Bach, *The Design of the UNIX Operating System* 5장, 11장
- Linux man pages: `pipe(2)`, `pipe(7)`, `fifo(7)`, `mq_overview(7)`, `shm_overview(7)`, `sem_overview(7)`, `sysvipc(7)`, `unix(7)`
- Dennis M. Ritchie and Ken Thompson, "The UNIX Time-Sharing System", CACM 1974
- Linux 커널 문서: `Documentation/filesystems/tmpfs.rst`, `Documentation/admin-guide/sysctl/kernel.rst`
