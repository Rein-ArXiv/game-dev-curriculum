## 들어가며

> **핵심 질문**: epoll만으로는 왜 부족한가?


전통적인 Linux I/O 인터페이스는 역할이 나뉘어 있다. blocking descriptor의 `read`/`write`는 호출 스레드를 기다리게 할 수 있고, `epoll`은 지원하는 descriptor의 readiness를 알린 뒤 실제 I/O를 별도 호출로 수행한다. POSIX AIO의 glibc 구현은 사용자 공간 worker thread를 사용할 수 있으며, Linux native AIO는 지원 작업과 파일 형식에 제약이 있었다. 시스템 콜마다 사용자 모드와 커널 모드 사이의 전환 비용이 들지만, 이를 곧바로 스레드 context switch나 모든 cache 무효화와 동일시해서는 안 된다.

2019년 Linux 5.1에 io_uring이 등장했다. Jens Axboe가 설계한 이 인터페이스는 공유 SQ/CQ로 여러 작업을 배치하고 completion을 수집한다. 일반 모드의 제출·대기에는 `io_uring_enter`가 필요할 수 있고, SQPOLL 조건에서는 커널 poll thread가 SQ를 소비해 일부 제출 호출을 생략할 수 있다. 핵심은 “항상 시스템 콜 0회”가 아니라 작업별 호출을 batch당 호출로 amortize하고 파일·소켓·타이머 작업을 공통 completion 모델로 표현하는 데 있다.

이 글은 io_uring의 아키텍처, 커널과 사용자 공간 사이의 메모리 순서, 요청·버퍼 수명, 오류와 취소, 등록 자원과 폴링의 트레이드오프를 다룬다.


---

## 목차
1. [기존 I/O 모델의 한계](#1-기존-io-모델의-한계)
2. [io_uring 아키텍처](#2-io_uring-아키텍처)
3. [커널 구현 상세](#3-커널-구현-상세)
4. [epoll, AIO와의 비교](#4-epoll-aio와의-비교)
5. [liburing 사용법](#5-liburing-사용법)
6. [고급 기능](#6-고급-기능)
7. [io_uring_setup 시스템 콜 내부](#7-io_uring_setup-시스템-콜-내부)
8. [성능 벤치마크](#8-성능-벤치마크)
9. [보안 고려사항](#9-보안-고려사항)
10. [고급 주제](#10-고급-주제)


---

## 1. 기존 I/O 모델의 한계

### 동기 I/O: read/write

전형적인 서버 코드의 모습이다.

```c
int fd = open("file.txt", O_RDONLY);
char buf[4096];
ssize_t n = read(fd, buf, sizeof(buf));  // 블로킹
close(fd);
```

문제점은 다음과 같다.

1. 블로킹: I/O 완료까지 스레드가 대기한다.
2. 시스템 콜 오버헤드: 각 작업마다 user ↔ kernel 전환이 발생한다.
3. 배치 부재: 평범한 `read` 호출만 사용하면 1,000개 독립 읽기를 제출하는 데 호출도 1,000번 필요하다.

### 이벤트 기반: epoll

개선된 모델은 다음과 같다.

```c
int epfd = epoll_create1(0);
struct epoll_event ev;
ev.events = EPOLLIN;
ev.data.fd = sockfd;
epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);

struct epoll_event events[MAX_EVENTS];
int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);

for (int i = 0; i < nfds; i++) {
    if (events[i].events & EPOLLIN) {
        read(events[i].data.fd, buf, sizeof(buf));  // 여전히 시스템 콜
    }
}
```

남아있는 문제는 다음과 같다.

- Readiness notification only: "읽을 수 있다"는 알려주지만 실제 읽기는 별도 시스템 콜이다.
- regular file에는 readiness 의미가 유용하지 않다. `epoll`은 socket뿐 아니라 pipe, eventfd, timerfd, signalfd 등 여러 poll 가능한 descriptor를 지원하지만 일반 파일 비동기 완료 API는 아니다.
- 분산된 API: epoll, aio, sync_file_range 등 파편화되어 있다.

### POSIX AIO: 미완의 표준

```c
struct aiocb cb;
memset(&cb, 0, sizeof(cb));
cb.aio_fildes = fd;
cb.aio_buf = buf;
cb.aio_nbytes = sizeof(buf);

aio_read(&cb);
aio_suspend(&cb, 1, NULL);
```

Linux에서 고려할 제약은 다음과 같다.

1. glibc 구현은 커널이 아닌 유저스페이스 스레드 풀을 사용한다.
2. POSIX AIO와 Linux native AIO는 서로 다른 인터페이스이며, native AIO의 전통적인 강점은 direct I/O였다. 커널 버전과 파일 시스템에 따라 buffered I/O 지원 범위를 고정 문장으로 일반화하면 안 된다.
3. 파일·소켓·타이머를 한 completion API로 구성하기 어렵고 오류·취소·버퍼 수명 규약이 분산된다.

### 요구사항 정리

이상적인 I/O 인터페이스는 다음을 만족해야 한다.

- 비동기 완료: 지원 opcode가 호출 스레드를 장시간 점유하지 않도록 실행 경로를 선택
- 시스템 콜 최소화: 배치 제출/수집
- 링 메타데이터 공유: 요청 전달 자체의 복사를 줄이되 payload zero-copy와는 구분
- 폴링 지원: 지연시간 극소화
- 유연성: 파일, 소켓, 타이머 등 통합

io_uring은 이 요구를 넓게 다루지만 모든 opcode가 같은 방식으로 비동기 실행되거나 zero-copy가 되는 것은 아니다. 즉시 완료, nonblocking 시도, io-wq worker fallback, 장치 비동기 경로가 작업별로 다르다.

## 2. io_uring 아키텍처

### 핵심 개념: 공유 링 버퍼

전통적 방식(시스템 콜 기반)에서는 다음 흐름이 일어난다.

```text
User Space              Kernel Space
   read(fd, buf, len)
   ──────────────────→  [커널 처리]
   ←──────────────────  반환
   (사용자/커널 모드 전환과 호출 비용)
```

io_uring 방식은 다음과 같다.

```text
User Space              Kernel Space
   [SQ Ring]
      ↓ (공유 메모리)
   ──────────────────→  [SQ 읽기]
                        [I/O 처리]
   [CQ Ring]            [CQ 쓰기]
      ↑ (공유 메모리)
   ←──────────────────
   (CQ에 이미 완료가 있으면 수집은 사용자 공간,
    제출·대기는 일반적으로 io_uring_enter가 필요할 수 있음)
```

### 두 개의 링 버퍼

**Submission Queue(SQ)**: User → Kernel

사용자가 I/O 요청을 추가하는 큐다.

```text
┌─────┬─────┬─────┬─────┬─────┐
│ SQE │ SQE │ SQE │     │     │  SQE: Submission Queue Entry
└─────┴─────┴─────┴─────┴─────┘
  ↑head        ↑tail
```

**Completion Queue(CQ)**: Kernel → User

커널이 완료 결과를 추가하는 큐다.

```text
┌─────┬─────┬─────┬─────┬─────┐
│ CQE │ CQE │     │     │     │  CQE: Completion Queue Entry
└─────┴─────┴─────┴─────┴─────┘
  ↑head        ↑tail
```

### 작동 흐름

전체 라이프사이클은 다음과 같다.

```text
1. 초기화
   io_uring_setup() → ring 생성, mmap으로 공유 메모리 매핑

2. I/O 제출
   [User] SQE 작성 (opcode, fd, addr, len, ...)
   [User] SQ tail 증가
   [User] 일반 모드는 io_uring_enter로 제출
          SQPOLL은 poll thread가 SQ를 소비하며 필요 시 wakeup 호출

3. 커널 처리
   [Kernel] SQ head ~ tail 읽기
   [Kernel] 각 SQE에 대해 I/O 작업 수행
   [Kernel] 완료 시 CQE 작성

4. 결과 수집
   [User] CQ head ~ tail 읽기
   [User] 결과 확인 (user_data로 요청 매칭)
   [User] CQ head 증가
```

### 주요 데이터 구조

**Submission Queue Entry**

```c
// include/uapi/linux/io_uring.h
struct io_uring_sqe {
    __u8    opcode;         // IORING_OP_READ, WRITE, ...
    __u8    flags;          // IOSQE_FIXED_FILE, ASYNC, ...
    __u16   ioprio;         // I/O 우선순위
    __s32   fd;             // 파일 디스크립터
    union {
        __u64   off;        // 파일 오프셋
        __u64   addr2;
    };
    union {
        __u64   addr;       // 버퍼 주소
        __u64   splice_off_in;
    };
    __u32   len;            // 길이
    union {
        __kernel_rwf_t  rw_flags;
        __u32           fsync_flags;
        __u16           poll_events;
        __u32           sync_range_flags;
        __u32           msg_flags;
        __u32           timeout_flags;
        __u32           accept_flags;
        __u32           cancel_flags;
        __u32           open_flags;
        __u32           statx_flags;
        __u32           fadvise_advice;
        __u32           splice_flags;
    };
    __u64   user_data;      // 사용자 정의 ID (완료 시 그대로 반환)
    union {
        struct {
            __u16   buf_index;
            __u16   personality;
            __s32   splice_fd_in;
        };
        __u64   __pad2[3];
    };
};
```

**Completion Queue Entry**

```c
struct io_uring_cqe {
    __u64   user_data;      // SQE의 user_data 복사
    __s32   res;            // 결과 (성공 시 바이트 수, 실패 시 -errno)
    __u32   flags;          // 완료 플래그
};
```

## 3. 커널 구현 상세

### io_uring_setup: 초기화

시스템 콜 인터페이스다.

```c
int ring_fd = syscall(__NR_io_uring_setup, 128, &params);
```

커널 경로는 다음과 같다.

```c
// fs/io_uring.c
SYSCALL_DEFINE2(io_uring_setup, u32, entries,
                struct io_uring_params __user *, params)
{
    struct io_ring_ctx *ctx;
    struct io_uring_params p;

    // 1. 파라미터 검증
    if (copy_from_user(&p, params, sizeof(p)))
        return -EFAULT;
    if (entries > IORING_MAX_ENTRIES)
        return -EINVAL;

    // 2. ring context 할당
    ctx = io_ring_ctx_alloc(&p);
    if (!ctx)
        return -ENOMEM;

    // 3. SQ/CQ 링 버퍼 생성
    ret = io_allocate_scq_urings(ctx, &p);
    if (ret)
        goto err;

    // 4. worker thread pool 생성 (SQPOLL 모드 시)
    if (p.flags & IORING_SETUP_SQPOLL) {
        ret = io_sq_offload_create(ctx, &p);
        if (ret)
            goto err;
    }

    // 5. eventfd (완료 통지용, 선택적)
    if (ctx->eventfd_async) {
        ret = io_eventfd_register(ctx, ...);
    }

    // 6. 파일 디스크립터 할당
    ret = io_uring_install_fd(ctx);

    return ret;
}
```

핵심 구조체는 다음과 같다.

```c
struct io_ring_ctx {
    struct io_rings         *rings;     // 공유 메모리 영역

    // Submission Queue
    struct io_uring_sqe     *sq_sqes;
    unsigned                sq_entries;

    // Completion Queue
    unsigned                cq_entries;

    // 작업 큐
    struct io_wq            *io_wq;
    struct task_struct      *sqo_task;

    // 파일/버퍼 등록
    struct fixed_file_table *file_table;
    struct io_rsrc_data     *buf_data;

    // 동기화
    struct mutex            uring_lock;
    wait_queue_head_t       cq_wait;

    // 통계
    struct io_uring_cqe     *cq_overflow;
    atomic_t                cq_overflow_nr;
};
```

### 메모리 매핑

사용자 공간에서는 다음과 같이 매핑한다.

```c
// 1. SQ ring
void *sq_ring = mmap(0, sq_ring_sz, PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_POPULATE,
                     ring_fd, IORING_OFF_SQ_RING);

// 2. CQ ring
void *cq_ring = mmap(0, cq_ring_sz, PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_POPULATE,
                     ring_fd, IORING_OFF_CQ_RING);

// 3. SQE 배열
void *sqes = mmap(0, entries * sizeof(struct io_uring_sqe),
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_POPULATE,
                  ring_fd, IORING_OFF_SQES);
```

커널 mmap 핸들러는 다음과 같다.

```c
// fs/io_uring.c
static int io_uring_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct io_ring_ctx *ctx = file->private_data;
    loff_t offset = (loff_t)vma->vm_pgoff << PAGE_SHIFT;
    struct page *page;
    void *ptr;

    switch (offset) {
    case IORING_OFF_SQ_RING:
    case IORING_OFF_CQ_RING:
        ptr = ctx->rings;
        break;
    case IORING_OFF_SQES:
        ptr = ctx->sq_sqes;
        break;
    default:
        return -EINVAL;
    }

    page = virt_to_head_page(ptr);
    return vm_insert_page(vma, vma->vm_start, page);
}
```

### I/O 제출과 처리

io_uring_enter 시스템 콜이다.

```c
SYSCALL_DEFINE6(io_uring_enter, unsigned int, fd,
                u32, to_submit, u32, min_complete,
                u32, flags, const sigset_t __user *, sig,
                size_t, sigsz)
{
    struct io_ring_ctx *ctx;
    struct file *file;
    long ret = -EBADF;

    file = fget(fd);
    if (!file || file->f_op != &io_uring_fops)
        goto out_fput;

    ctx = file->private_data;

    if (to_submit) {
        ret = io_submit_sqes(ctx, to_submit);
        if (ret != to_submit)
            goto out;
    }

    if (min_complete) {
        ret = io_cqring_wait(ctx, min_complete, sig, sigsz);
    }

out:
    return ret;
}
```

실제 I/O 수행은 다음과 같다.

```c
static int io_submit_sqes(struct io_ring_ctx *ctx, unsigned int nr)
{
    struct io_submit_state state;
    struct io_kiocb *req;
    int submitted = 0;

    io_submit_state_start(&state, ctx, nr);

    for (int i = 0; i < nr; i++) {
        struct io_uring_sqe *sqe;

        sqe = io_get_sqe(ctx);
        if (!sqe)
            break;

        req = io_alloc_req(ctx);
        if (!req) {
            io_put_sqe(sqe);
            break;
        }

        if (io_init_req(ctx, req, sqe, &state)) {
            io_req_complete(req, -EINVAL);
            continue;
        }

        io_queue_sqe(ctx, req);
        submitted++;
    }

    io_submit_state_end(&state);
    return submitted;
}
```

opcode별 처리는 다음과 같다.

```c
static int io_issue_sqe(struct io_kiocb *req, unsigned int issue_flags)
{
    const struct io_op_def *def = &io_op_defs[req->opcode];

    switch (req->opcode) {
    case IORING_OP_READ:
        return io_read(req, issue_flags);
    case IORING_OP_WRITE:
        return io_write(req, issue_flags);
    case IORING_OP_FSYNC:
        return io_fsync(req, issue_flags);
    case IORING_OP_POLL_ADD:
        return io_poll_add(req, issue_flags);
    case IORING_OP_ACCEPT:
        return io_accept(req, issue_flags);
    case IORING_OP_CONNECT:
        return io_connect(req, issue_flags);
    case IORING_OP_SEND:
        return io_send(req, issue_flags);
    case IORING_OP_RECV:
        return io_recv(req, issue_flags);
    case IORING_OP_TIMEOUT:
        return io_timeout(req, issue_flags);
    case IORING_OP_STATX:
        return io_statx(req, issue_flags);
    case IORING_OP_OPENAT:
        return io_openat(req, issue_flags);
    case IORING_OP_CLOSE:
        return io_close(req, issue_flags);
    // ... 80+ operations
    default:
        return -EINVAL;
    }
}
```

비동기 처리(io_read 예시)는 다음과 같다.

```c
static int io_read(struct io_kiocb *req, unsigned int issue_flags)
{
    struct io_rw *rw = &req->rw;
    struct iovec *iov;
    ssize_t ret;

    struct kiocb *kiocb = &req->rw.kiocb;
    kiocb->ki_pos = rw->offset;
    kiocb->ki_flags = req->file->f_flags;
    kiocb->ki_hint = ki_hint_validate(file_write_hint(kiocb->ki_filp));
    kiocb->ki_complete = io_complete_rw;

    ret = io_import_iovec(READ, req, &iov, ...);
    if (ret < 0)
        return ret;

    ret = call_read_iter(req->file, kiocb, &rw->iter);

    if (ret == -EIOCBQUEUED) {
        return 0;
    } else {
        io_req_complete(req, ret);
        return 0;
    }
}
```

### 완료 처리

CQE 추가는 다음과 같다.

```c
static bool io_cqring_event_overflow(struct io_ring_ctx *ctx, u64 user_data,
                                     s32 res, u32 cflags)
{
    struct io_uring_cqe *cqe;

    if (io_cqring_events(ctx) < ctx->cq_entries) {
        cqe = io_get_cqe(ctx);
        if (likely(cqe)) {
            cqe->user_data = user_data;
            cqe->res = res;
            cqe->flags = cflags;
            return true;
        }
    }

    return io_cqring_event_overflow_add(ctx, user_data, res, cflags);
}

static void io_req_complete_post(struct io_kiocb *req, s32 res, u32 cflags)
{
    struct io_ring_ctx *ctx = req->ctx;

    spin_lock(&ctx->completion_lock);

    io_cqring_event_overflow(ctx, req->user_data, res, cflags);

    if (waitqueue_active(&ctx->cq_wait))
        wake_up(&ctx->cq_wait);

    if (ctx->eventfd_async)
        io_eventfd_signal(ctx);

    spin_unlock(&ctx->completion_lock);

    io_put_req(req);
}
```

## 4. epoll, AIO와의 비교

### 비교할 모델

`epoll`은 descriptor의 **readiness**를 알리고, `io_uring`은 제출한 작업의 **completion**을 돌려준다. 동일한 IOPS 표에 이름만 나열하기 전에 워크로드와 의미를 맞춰야 한다.

| 방식 | 잘 맞는 대상 | 핵심 비용 |
|------|-------------|----------|
| blocking I/O + thread | 단순한 낮은 동시성 | thread stack, scheduling |
| `epoll` + nonblocking I/O | socket readiness | readiness event 뒤 실제 read/write |
| Linux AIO | 주로 direct file I/O | 제한된 opcode와 제출 ABI |
| `io_uring` | file·socket 작업의 배치 completion | ring 관리, worker fallback, 등록 자원 수명 |
| SQPOLL/IOPOLL | 조건을 만족하는 고부하·저지연 경로 | 전용 CPU 시간과 장치·파일 제약 |

### 시스템 콜 횟수 비교

일반 파일은 readiness 모델의 적절한 `epoll` 비교 대상이 아니다. 디스크에 아직 없는 데이터도 descriptor 관점에서는 읽기 가능 상태로 취급되며, 일반 파일을 `epoll_ctl`에 등록하면 `EPERM`이 날 수 있다. socket 서버라면 `epoll_wait`가 준비된 fd를 알려준 뒤 각 fd에 `recv`/`send`를 호출한다.

```c
while (running) {
    int n = epoll_wait(epfd, events, MAX_EVENTS, timeout);
    for (int i = 0; i < n; ++i) {
        while (recv_nonblocking(events[i].data.fd, buffer)) {
            consume(buffer);
        }
    }
}
```

`io_uring`은 여러 file read나 socket receive를 SQ에 적고 한 `io_uring_enter`로 배치 제출할 수 있다.

```c
int ring_fd = io_uring_setup(1024, &params);    // 1

for (int i = 0; i < 1000; i++) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read(sqe, fds[i], bufs[i], 4096, 0);
    sqe->user_data = i;
}

io_uring_submit_and_wait(&ring, 1000);

unsigned head;
io_uring_for_each_cqe(&ring, head, cqe) {
    // 처리
}
```

정확한 시스템 콜 수는 SQ 크기, 제출 batch, completion 도착 시점, `SQPOLL`의 wakeup 필요 여부와 ring이 가득 찼는지에 따라 달라진다. 핵심 보장은 "항상 두 번"이 아니라 제출과 완료를 여러 작업 단위로 amortize할 수 있다는 것이다.

### 기능 비교표

| 기능 | epoll | POSIX AIO | io_uring |
|------|-------|-----------|----------|
| 파일 I/O | 미지원 | 제한적 | 지원 |
| 소켓 I/O | 지원 | 미지원 | 지원 |
| 일반 파일 I/O의 비동기 실행 경로 | readiness API 아님 | 구현·작업별 제한 | opcode·파일·fallback 경로에 따라 지원 |
| 배치 제출 | 미지원 | 미지원 | 지원 |
| payload copy 회피 | `sendfile` 등 별도 API | 일반 계약 아님 | 전용 ZC opcode·등록 방식·장치 조건에서 가능 |
| 폴링 모드 | 미지원 | 미지원 | 지원 |
| 버퍼 링 지원 | 미지원 | 미지원 | 지원 |
| 고정 파일 | 미지원 | 미지원 | 지원 |
| 체인 작업 | 미지원 | 미지원 | 지원 |
| 타임아웃 | 지원 | 지원 | 지원 |
| signal handler에서 직접 사용 | 일반 계약 아님 | completion notification만 신호 사용 가능 | liburing 호출을 async-signal-safe로 간주할 수 없음 |
| 커널 인터페이스 성격 | readiness | 요청 제출 | 공유 ring 기반 요청·완료 |

## 5. liburing 사용법

### 설치

```bash
# 소스 컴파일
git clone https://github.com/axboe/liburing.git
cd liburing
./configure
make
sudo make install

# 패키지 설치 (Ubuntu 20.04+)
sudo apt install liburing-dev
```

### 기본 사용법

헤더와 초기화는 다음과 같다.

```c
#include <liburing.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

int main() {
    struct io_uring ring;
    int ret;

    ret = io_uring_queue_init(128, &ring, 0);
    if (ret < 0) {
        fprintf(stderr, "queue_init: %s\n", strerror(-ret));
        return 1;
    }

    // ... I/O 작업 ...

    io_uring_queue_exit(&ring);
    return 0;
}
```

### 파일 읽기

```c
void read_file_io_uring(const char *filename)
{
    struct io_uring ring;
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;
    struct iovec iov;
    char buffer[4096];
    int fd, ret;
    bool ring_ready = false;

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return;
    }

    ret = io_uring_queue_init(1, &ring, 0);
    if (ret < 0) {
        fprintf(stderr, "queue_init: %s\n", strerror(-ret));
        goto cleanup;
    }
    ring_ready = true;

    sqe = io_uring_get_sqe(&ring);
    if (!sqe) {
        fprintf(stderr, "get sqe failed\n");
        goto cleanup;
    }

    iov.iov_base = buffer;
    iov.iov_len = sizeof(buffer);
    io_uring_prep_readv(sqe, fd, &iov, 1, 0);
    io_uring_sqe_set_data(sqe, &iov);

    ret = io_uring_submit(&ring);
    if (ret < 0) {
        fprintf(stderr, "submit: %s\n", strerror(-ret));
        goto cleanup;
    }
    if (ret != 1) {
        fprintf(stderr, "submit accepted %d requests, expected 1\n", ret);
        goto cleanup;
    }

    ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret < 0) {
        fprintf(stderr, "wait: %s\n", strerror(-ret));
        goto cleanup;
    }

    if (cqe->res < 0) {
        fprintf(stderr, "read failed: %s\n", strerror(-cqe->res));
    } else {
        printf("Read %d bytes\n", cqe->res);
        printf("Content: %.*s\n", cqe->res, buffer);
    }

    io_uring_cqe_seen(&ring, cqe);

cleanup:
    close(fd);
    if (ring_ready)
        io_uring_queue_exit(&ring);
}
```

SQE가 제출된 뒤에는 CQE를 회수하거나 요청을 취소하고 그 완료를 회수하기 전까지 `buffer`, `iov`, `fd`, ring을 파괴하면 안 된다. 위 함수는 제출 성공 뒤 반드시 하나의 CQE를 기다리므로 stack buffer 수명이 유지된다. timeout이나 조기 반환을 추가한다면 취소 요청의 CQE와 원래 요청의 CQE를 모두 구분해 drain하는 규약이 필요하다.

### 복수 파일 병렬 읽기

```c
#define NUM_FILES 10

void read_multiple_files(const char *filenames[], int count)
{
    struct io_uring ring;
    struct file_data {
        int fd;
        struct iovec iov;
        char buffer[4096];
    } files[NUM_FILES];
    int submitted = 0;
    int completed = 0;

    if (count < 0 || count > NUM_FILES) {
        fprintf(stderr, "invalid file count\n");
        return;
    }
    int ret = io_uring_queue_init(128, &ring, 0);
    if (ret < 0) {
        fprintf(stderr, "queue_init: %s\n", strerror(-ret));
        return;
    }
    for (int i = 0; i < count; ++i)
        files[i].fd = -1;

    for (int i = 0; i < count; i++) {
        files[i].fd = open(filenames[i], O_RDONLY);
        if (files[i].fd < 0) {
            fprintf(stderr, "open %s: %s\n", filenames[i], strerror(errno));
            continue;
        }
        files[i].iov.iov_base = files[i].buffer;
        files[i].iov.iov_len = sizeof(files[i].buffer);

        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        if (!sqe) {
            close(files[i].fd);
            files[i].fd = -1;
            break;
        }
        io_uring_prep_readv(sqe, files[i].fd, &files[i].iov, 1, 0);
        io_uring_sqe_set_data(sqe, &files[i]);
        submitted++;
    }

    ret = io_uring_submit(&ring);
    if (ret < 0) {
        fprintf(stderr, "submit: %s\n", strerror(-ret));
        submitted = 0;  // 어떤 SQE도 커널에 제출되지 않음
    } else {
        submitted = ret;
    }

    while (completed < submitted) {
        struct io_uring_cqe *cqe;
        ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0) {
            fprintf(stderr, "wait_cqe: %s\n", strerror(-ret));
            break;
        }

        struct file_data *fdata = io_uring_cqe_get_data(cqe);
        if (cqe->res < 0)
            fprintf(stderr, "fd=%d: %s\n",
                    fdata->fd, strerror(-cqe->res));
        else
            printf("File fd=%d: read %d bytes\n", fdata->fd, cqe->res);

        io_uring_cqe_seen(&ring, cqe);
        completed++;
    }

    for (int i = 0; i < count; i++) {
        if (files[i].fd >= 0)
            close(files[i].fd);
    }
    io_uring_queue_exit(&ring);
}
```

간결성을 위해 이 예제는 `io_uring_submit`이 준비한 SQE보다 적은 수만 제출했을 때 남은 SQE를 다시 제출하는 production loop를 생략했다. 중요한 불변식은 **실제로 제출된 요청 수만 기다리고**, 각 CQE의 `res`를 별도 결과로 처리하며, 마지막 CQE 전에는 `file_data`, `iovec`, buffer와 fd를 파괴하지 않는 것이다.

### Echo 서버

Echo server의 핵심은 API 호출 수가 아니라 **요청과 connection의 수명 상태 기계**다. 아래 코드는 setup과 SQE 확보 helper를 분리한 completion 처리 골격이다.

```c
#define BUFSIZE 4096

enum operation { OP_ACCEPT, OP_RECV, OP_SEND };

struct connection {
    int fd;
    char buffer[BUFSIZE];
    size_t echo_size;
    size_t sent;
};

struct request {
    enum operation op;
    struct connection *conn;  // OP_ACCEPT에서는 NULL
};

// 각 submit_* helper의 계약:
// 1. SQE 부족 시 submit 후 다시 확보하거나 명시적 오류를 반환한다.
// 2. 제출 성공 뒤 request와 connection 수명은 CQE handler가 소유한다.
// 3. 실패 시 넘겨받은 객체를 누가 해제하는지 반환 계약에 고정한다.
bool submit_accept(struct io_uring *ring, int listen_fd);
bool submit_recv(struct io_uring *ring, struct connection *conn);
bool submit_send(struct io_uring *ring, struct connection *conn);

static void close_connection(struct connection *conn)
{
    close(conn->fd);
    free(conn);
}

static void handle_completion(struct io_uring *ring, int listen_fd,
                              struct request *req, int result)
{
    switch (req->op) {
    case OP_ACCEPT: {
        // accept 오류가 나도 listener를 계속 운영하려면 먼저 다시 arm한다.
        bool rearmed = submit_accept(ring, listen_fd);
        if (!rearmed)
            request_controlled_shutdown();

        if (result >= 0) {
            struct connection *conn = calloc(1, sizeof(*conn));
            if (!conn) {
                close(result);
            } else {
                conn->fd = result;
                if (!submit_recv(ring, conn))
                    close_connection(conn);
            }
        } else if (result != -ECANCELED) {
            log_io_error("accept", -result);
        }
        break;
    }

    case OP_RECV: {
        struct connection *conn = req->conn;
        if (result <= 0) {
            if (result < 0 && result != -ECANCELED)
                log_io_error("recv", -result);
            close_connection(conn);  // result == 0은 EOF
            break;
        }

        conn->echo_size = (size_t)result;
        conn->sent = 0;
        if (!submit_send(ring, conn))
            close_connection(conn);
        break;
    }

    case OP_SEND: {
        struct connection *conn = req->conn;
        if (result <= 0) {
            if (result < 0 && result != -ECANCELED)
                log_io_error("send", -result);
            close_connection(conn);
            break;
        }

        conn->sent += (size_t)result;
        if (conn->sent < conn->echo_size) {
            // stream send는 short completion이 가능하다.
            if (!submit_send(ring, conn))
                close_connection(conn);
        } else if (!submit_recv(ring, conn)) {
            close_connection(conn);
        }
        break;
    }
    }
    free(req);
}

void completion_loop(struct io_uring *ring, int listen_fd)
{
    for (;;) {
        struct io_uring_cqe *cqe;
        int rc = io_uring_wait_cqe(ring, &cqe);
        if (rc < 0) {
            log_io_error("wait_cqe", -rc);
            break;
        }

        struct request *req = io_uring_cqe_get_data(cqe);
        int result = cqe->res;
        io_uring_cqe_seen(ring, cqe);
        handle_completion(ring, listen_fd, req, result);
    }
}
```

`submit_send`는 `buffer + sent`, `echo_size - sent` 범위를 제출해야 한다. 한 connection에는 동시에 하나의 recv 또는 send만 outstanding하게 두었으므로 completion 순서와 buffer 소유권이 단순해진다. 실제 서버는 listener와 ring 초기화의 모든 반환값, signal 기반 종료, 연결 수 상한, backpressure를 처리해야 한다. 종료 시 ring을 바로 파괴하지 말고 outstanding 요청을 취소한 뒤 원래 요청과 취소 요청의 CQE를 모두 drain해야 한다.

## 6. 고급 기능

### Fixed Files (고정 파일 디스크립터)

매 I/O마다 `fget(fd)`로 파일 객체를 조회하면 오버헤드가 있다. 파일 디스크립터를 사전 등록하여 조회를 생략할 수 있다.

```c
#define NR_FILES 1000

void use_fixed_files()
{
    struct io_uring ring;
    int fds[NR_FILES];

    for (int i = 0; i < NR_FILES; i++) {
        char filename[64];
        sprintf(filename, "file%d.dat", i);
        fds[i] = open(filename, O_RDONLY);
    }

    io_uring_queue_init(128, &ring, 0);

    int ret = io_uring_register_files(&ring, fds, NR_FILES);
    if (ret) {
        fprintf(stderr, "register_files: %s\n", strerror(-ret));
        return;
    }

    for (int i = 0; i < NR_FILES; i++) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        char *buf = malloc(4096);

        io_uring_prep_read(sqe, i, buf, 4096, 0);  // i는 등록된 인덱스
        sqe->flags |= IOSQE_FIXED_FILE;
        io_uring_sqe_set_data(sqe, buf);
    }

    io_uring_submit(&ring);

    for (int i = 0; i < NR_FILES; i++) {
        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);
        void *buf = io_uring_cqe_get_data(cqe);
        free(buf);
        io_uring_cqe_seen(&ring, cqe);
    }

    io_uring_unregister_files(&ring);
    for (int i = 0; i < NR_FILES; i++) {
        close(fds[i]);
    }
    io_uring_queue_exit(&ring);
}
```

커널 구현은 다음과 같다.

```c
static int io_sqe_files_register(struct io_ring_ctx *ctx, void __user *arg,
                                  unsigned nr_args)
{
    struct file **files;
    int fd, ret;

    ctx->file_table = kzalloc(sizeof(*ctx->file_table), GFP_KERNEL);
    files = kcalloc(nr_args, sizeof(struct file *), GFP_KERNEL);

    for (int i = 0; i < nr_args; i++) {
        if (copy_from_user(&fd, &arg[i], sizeof(fd)))
            return -EFAULT;

        files[i] = fget(fd);
        if (!files[i])
            return -EBADF;
    }

    ctx->file_table->files = files;
    ctx->file_table->nr_files = nr_args;
    return 0;
}

static struct file *io_file_get(struct io_ring_ctx *ctx,
                                 struct io_kiocb *req, int fd)
{
    if (req->flags & REQ_F_FIXED_FILE) {
        if (unlikely(fd >= ctx->file_table->nr_files))
            return NULL;
        return ctx->file_table->files[fd];
    } else {
        return fget(fd);
    }
}
```

일반 fd 조회는 현재 프로세스의 fdtable array에서 `struct file *`을 안전하게 얻고 참조 수명을 확보한다. hash table lookup이 아니다. 등록 파일은 이 해석과 참조 획득을 등록 시점으로 옮기고 SQE에서 작은 slot index를 사용한다. hot fdtable에서는 차이가 작을 수 있고, 등록·갱신·해제 비용과 slot 수명 관리가 새로 생기므로 고정 배율로 표현할 수 없다.

### Registered Buffers (등록 버퍼)

일부 비동기·direct I/O 경로는 사용자 페이지를 I/O 동안 pin하고 매핑해야 한다. 등록 버퍼는 이 준비를 등록 시점으로 옮겨 반복 비용을 줄일 수 있다. 이것이 buffered write의 사용자 데이터를 장치까지 무조건 무복사로 만든다는 뜻은 아니다.

```c
#define NR_BUFS 128
#define BUF_SIZE 4096

void use_registered_buffers()
{
    struct io_uring ring;
    struct iovec iovecs[NR_BUFS];
    void *buffers[NR_BUFS];

    for (int i = 0; i < NR_BUFS; i++) {
        buffers[i] = aligned_alloc(4096, BUF_SIZE);
        iovecs[i].iov_base = buffers[i];
        iovecs[i].iov_len = BUF_SIZE;
    }

    io_uring_queue_init(128, &ring, 0);
    io_uring_register_buffers(&ring, iovecs, NR_BUFS);

    int fd = open("test.dat", O_RDONLY);

    for (int i = 0; i < NR_BUFS; i++) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);

        io_uring_prep_read_fixed(sqe, fd, buffers[i], BUF_SIZE,
                                  i * BUF_SIZE, i);
        sqe->user_data = i;
    }

    io_uring_submit(&ring);

    for (int i = 0; i < NR_BUFS; i++) {
        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);
        int idx = cqe->user_data;
        printf("Buffer %d: read %d bytes\n", idx, cqe->res);
        io_uring_cqe_seen(&ring, cqe);
    }

    io_uring_unregister_buffers(&ring);
    close(fd);
    io_uring_queue_exit(&ring);

    for (int i = 0; i < NR_BUFS; i++) {
        free(buffers[i]);
    }
}
```

장점은 다음과 같다.

- 페이지 고정 1회: 등록 시에만 pin하며 I/O 때마다 반복하지 않는다.
- 반복 pin·mapping 비용을 줄일 수 있다. 실제 DMA 경로와 IOMMU mapping은 장치·opcode에 달려 있다.
- 등록 버퍼 자체는 payload copy 제거를 보장하지 않는다. zero-copy send/receive 전용 기능과 장치 조건을 별도로 확인한다.

### Multishot Operations

accept, recv 같은 작업은 한 번만 완료되고 재제출이 필요하다. Multishot 플래그로 자동 재발행을 설정할 수 있다.

**Multishot Accept**

```c
void multishot_accept_server(int port)
{
    struct io_uring ring;
    struct sockaddr_in addr;
    int server_fd;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 128);

    struct io_uring_params params = {0};
    params.flags = IORING_SETUP_SUBMIT_ALL;
    io_uring_queue_init_params(256, &ring, &params);

    // multishot accept는 sqe->flags가 아니라 전용 helper로 만든다.
    // (io_uring_prep_multishot_accept가 내부에서 IORING_OP_ACCEPT +
    //  IORING_ACCEPT_MULTISHOT을 설정한다.)
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_multishot_accept(sqe, server_fd, NULL, NULL, 0);
    io_uring_sqe_set_data64(sqe, 0xAcc);
    io_uring_submit(&ring);

    while (1) {
        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);

        if (cqe->user_data == 0xAcc) {
            int client_fd = cqe->res;
            // 동일 SQE가 살아 있으므로 다음 accept도 자동으로 진행된다
        }

        if (!(cqe->flags & IORING_CQE_F_MORE)) {
            // 더 이상 후속 CQE가 오지 않으면 multishot이 끝난 것
            fprintf(stderr, "Multishot ended\n");
            io_uring_cqe_seen(&ring, cqe);
            break;
        }

        io_uring_cqe_seen(&ring, cqe);
    }

    io_uring_queue_exit(&ring);
    close(server_fd);
}
```

**Multishot Recv (5.19+)**

```c
void multishot_recv(int client_fd)
{
    struct io_uring ring;
    io_uring_queue_init(128, &ring, 0);

    // 모든 buffer를 같은 group(BG_ID)에 등록한다. recv는 이 group에서 buffer를 고른다.
    enum { BG_ID = 1 };
    char buffers[16][4096];
    for (int i = 0; i < 16; i++) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        // (sqe, addr, len, nr, bgid, bid): 버퍼 16개를 BG_ID 그룹에 i번 인덱스로 등록.
        io_uring_prep_provide_buffers(sqe, buffers[i], 4096, 1, BG_ID, i);
    }
    io_uring_submit(&ring);

    // multishot recv도 전용 helper를 쓴다.
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_recv_multishot(sqe, client_fd, NULL, 0, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = BG_ID;       // 위에서 등록한 그룹과 일치시켜야 한다
    io_uring_submit(&ring);

    while (1) {
        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);

        // 1) 오류 CQE 검사 (실패 CQE에는 buffer id가 없을 수 있다)
        if (cqe->res < 0) {
            fprintf(stderr, "CQE error: %s\n", strerror(-cqe->res));
            io_uring_cqe_seen(&ring, cqe);
            break;
        }

        // 2) buffer가 실제로 선택되어 응답되었는지 확인
        if (!(cqe->flags & IORING_CQE_F_BUFFER)) {
            // buffer가 비치지 않은 응답 — 별도 처리
            io_uring_cqe_seen(&ring, cqe);
            continue;
        }

        int buf_id = cqe->flags >> IORING_CQE_BUFFER_SHIFT;

        sqe = io_uring_get_sqe(&ring);
        // 사용한 buffer를 같은 그룹에 다시 등록한다.
        io_uring_prep_provide_buffers(sqe, buffers[buf_id], 4096, 1, BG_ID, buf_id);
        io_uring_submit(&ring);

        if (!(cqe->flags & IORING_CQE_F_MORE)) {
            io_uring_cqe_seen(&ring, cqe);
            break;
        }

        io_uring_cqe_seen(&ring, cqe);
    }

    io_uring_queue_exit(&ring);
}
```

### SQPOLL (Kernel-side Submission)

`io_uring_enter`로 SQE 제출을 알리려면 시스템 콜이 1회 필요하다. 커널 스레드가 SQ를 폴링하면 이 시스템 콜도 제거할 수 있다.

```c
void sqpoll_mode()
{
    struct io_uring ring;
    struct io_uring_params params = {0};

    params.flags = IORING_SETUP_SQPOLL;
    params.sq_thread_idle = 2000;  // 2초 후 sleep

    io_uring_queue_init_params(128, &ring, &params);

    int fd = open("test.dat", O_RDONLY);
    char buf[4096];

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read(sqe, fd, buf, sizeof(buf), 0);
    sqe->user_data = 1;

    io_uring_sqring_wait(&ring);

    struct io_uring_cqe *cqe;
    io_uring_wait_cqe(&ring, &cqe);
    io_uring_cqe_seen(&ring, cqe);

    close(fd);
    io_uring_queue_exit(&ring);
}
```

커널 SQPOLL 스레드는 다음과 같다.

```c
static int io_sq_thread(void *data)
{
    struct io_ring_ctx *ctx = data;
    unsigned long timeout = ctx->sq_thread_idle;

    while (!kthread_should_stop()) {
        unsigned int to_submit;

        to_submit = io_sqring_entries(ctx);

        if (to_submit) {
            mutex_lock(&ctx->uring_lock);
            io_submit_sqes(ctx, to_submit);
            mutex_unlock(&ctx->uring_lock);

            timeout = jiffies + ctx->sq_thread_idle;
        }

        if (!to_submit) {
            if (time_after(jiffies, timeout)) {
                set_current_state(TASK_INTERRUPTIBLE);
                schedule_timeout(HZ);
            } else {
                cpu_relax();
            }
        }
    }

    return 0;
}
```

SQ thread가 깨어 있고 ring 상태가 허용하면 userspace의 tail 갱신만으로 제출을 발견할 수 있어 submit syscall을 줄일 수 있다. thread가 idle로 잠들었거나 `IORING_SQ_NEED_WAKEUP`이 표시되면 `io_uring_enter`로 깨워야 한다. 대가는 polling CPU time과 다른 workload에 대한 간섭이다. SQPOLL 자체의 privilege 규칙은 커널 버전별로 변했으므로 현재 `io_uring_setup(2)`와 host 정책을 확인한다.

### IOPOLL (Device Polling)

NVMe 같은 고속 디바이스는 인터럽트 오버헤드가 병목이다. 디바이스 완료 큐를 폴링하면 지연을 줄일 수 있다.

```c
void iopoll_mode(const char *filename)
{
    struct io_uring ring;
    struct io_uring_params params = {0};

    params.flags = IORING_SETUP_IOPOLL;
    io_uring_queue_init_params(128, &ring, &params);

    int fd = open(filename, O_RDONLY | O_DIRECT);
    void *buf = aligned_alloc(4096, 4096);

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read(sqe, fd, buf, 4096, 0);
    io_uring_submit(&ring);

    struct io_uring_cqe *cqe;
    io_uring_wait_cqe(&ring, &cqe);

    io_uring_cqe_seen(&ring, cqe);
    free(buf);
    close(fd);
    io_uring_queue_exit(&ring);
}
```

커널 폴링 루프는 다음과 같다.

```c
static int io_iopoll_check(struct io_ring_ctx *ctx, unsigned int *nr_events,
                           long min)
{
    struct io_kiocb *req, *tmp;
    unsigned int nr = 0;

    list_for_each_entry_safe(req, tmp, &ctx->iopoll_list, inflight_entry) {
        struct kiocb *kiocb = &req->rw.kiocb;
        int ret;

        ret = kiocb->ki_filp->f_op->iopoll(kiocb, true);

        if (ret > 0) {
            list_del(&req->inflight_entry);
            io_req_complete(req, ret);
            nr++;

            if (nr >= min)
                return nr;
        }
    }

    return nr;
}
```

IOPOLL은 interrupt delivery를 피할 수 있지만 polling CPU를 계속 소비하고, poll을 지원하는 block device와 direct I/O 같은 제약을 요구한다. 이득은 queue depth, block size, device firmware, CPU affinity와 power state에 따라 달라진다. interrupt 모드와 비교할 때는 IOPS뿐 아니라 p50/p99 completion latency, polling CPU time, energy, 다른 workload에 미친 간섭을 함께 기록한다.

### Linked Operations

순차적 작업(open → read → close)을 각각 제출/대기하면 비효율적이다. SQE를 체인으로 연결하여 일괄 처리할 수 있다.

```c
void linked_operations(const char *filename)
{
    struct io_uring ring;
    io_uring_queue_init(8, &ring, 0);

    char buf[4096];
    struct io_uring_sqe *sqe;
    int fd = -1;  // 링크 안에서 sqe 사이로 fd가 자동 전달되지 않는다.

    // io_uring 링크는 SQE 사이에 결과 값을 자동으로 주입하지 않는다.
    // open의 결과 fd를 다음 SQE의 fd로 쓰려면, 호출자가 open이 끝난 뒤
    // 그 fd를 별도로 들고 read/close SQE를 다시 만들어야 한다.
    // 따라서 "open → read → close"를 하나의 링크로 묶어 fd를 자동 전달하는
    // 방식은 io_uring 의미와 다르다. 다음 두 가지 방식 중 하나를 쓴다.
    //
    // (A) 두 단계 제출: open 완료를 기다린 뒤 read/close를 새로 묶어 제출
    // (B) IORING_OP_OPENAT2 + 등록된 fd 슬롯(IORING_RSRC_REGISTER) 활용
    //
    // 아래는 (A)의 단순 형태다.

    // 1. open만 먼저 제출하고 결과를 받는다.
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_openat(sqe, AT_FDCWD, filename, O_RDONLY, 0);
    io_uring_sqe_set_data64(sqe, 1);
    io_uring_submit(&ring);

    struct io_uring_cqe *cqe;
    io_uring_wait_cqe(&ring, &cqe);
    fd = cqe->res;
    io_uring_cqe_seen(&ring, cqe);
    if (fd < 0) { io_uring_queue_exit(&ring); return; }

    // 2. fd를 알고 나서 read와 close를 링크로 묶어 제출한다.
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read(sqe, fd, buf, sizeof(buf), 0);
    sqe->flags |= IOSQE_IO_LINK;
    io_uring_sqe_set_data64(sqe, 2);

    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_close(sqe, fd);
    io_uring_sqe_set_data64(sqe, 3);

    io_uring_submit(&ring);
    for (int i = 0; i < 2; i++) {
        io_uring_wait_cqe(&ring, &cqe);
        io_uring_cqe_seen(&ring, cqe);
    }

    io_uring_queue_exit(&ring);
}
```

`IOSQE_IO_LINK`는 앞 SQE가 실패하면 뒤 SQE를 취소한다는 의미일 뿐, 결과 값을 다음 SQE의 인자(예: fd)로 자동 주입하지 않는다. `IOSQE_IO_HARDLINK`는 실패해도 뒤 SQE 실행을 막지 않는다는 차이만 있다. fd 같은 결과를 후속 작업에 사용하려면 호출자가 CQE를 받아 직접 전달하거나, 등록된 fd/buffer 슬롯과 같은 별도의 자원 ID를 활용해야 한다.

## 7. io_uring_setup 시스템 콜 내부

### 시스템 콜 흐름

```c
// arch/x86/entry/syscalls/syscall_64.tbl
425    common  io_uring_setup    sys_io_uring_setup

SYSCALL_DEFINE2(io_uring_setup, u32, entries,
                struct io_uring_params __user *, params)
{
    struct io_uring_params p;
    int ret;

    if (copy_from_user(&p, params, sizeof(p)))
        return -EFAULT;

    if (entries > IORING_MAX_ENTRIES || entries == 0)
        return -EINVAL;

    entries = roundup_pow_of_two(entries);

    if (p.flags & ~(IORING_SETUP_IOPOLL |
                    IORING_SETUP_SQPOLL |
                    IORING_SETUP_SQ_AFF |
                    IORING_SETUP_CQSIZE |
                    IORING_SETUP_CLAMP |
                    IORING_SETUP_ATTACH_WQ |
                    IORING_SETUP_R_DISABLED |
                    IORING_SETUP_SUBMIT_ALL))
        return -EINVAL;

    ret = io_uring_create(entries, &p);
    if (ret < 0)
        return ret;

    if (copy_to_user(params, &p, sizeof(p))) {
        io_uring_ctx_free(ctx);
        return -EFAULT;
    }

    return ret;
}
```

### 링 버퍼 할당

```c
static int io_allocate_scq_urings(struct io_ring_ctx *ctx,
                                   struct io_uring_params *p)
{
    struct io_rings *rings;
    size_t size;

    ctx->sq_entries = p->sq_entries;

    if (p->flags & IORING_SETUP_CQSIZE) {
        if (p->cq_entries > IORING_MAX_CQ_ENTRIES)
            return -EINVAL;
        ctx->cq_entries = p->cq_entries;
    } else {
        ctx->cq_entries = 2 * p->sq_entries;
    }

    size = sizeof(struct io_rings);
    size += ctx->sq_entries * sizeof(u32);
    size += ctx->cq_entries * sizeof(struct io_uring_cqe);

    rings = io_mem_alloc(size);
    if (!rings)
        return -ENOMEM;

    ctx->rings = rings;
    rings->sq_ring_mask = ctx->sq_entries - 1;
    rings->cq_ring_mask = ctx->cq_entries - 1;
    rings->sq_ring_entries = ctx->sq_entries;
    rings->cq_ring_entries = ctx->cq_entries;

    size = ctx->sq_entries * sizeof(struct io_uring_sqe);
    ctx->sq_sqes = io_mem_alloc(size);
    if (!ctx->sq_sqes) {
        io_mem_free(ctx->rings);
        return -ENOMEM;
    }

    return 0;
}
```

### 공유 메모리 구조

```c
struct io_rings {
    // SQ 메타데이터
    u32 sq_head;
    u32 sq_tail;
    u32 sq_ring_mask;
    u32 sq_ring_entries;
    u32 sq_flags;
    u32 sq_dropped;

    // CQ 메타데이터
    u32 cq_head;
    u32 cq_tail;
    u32 cq_ring_mask;
    u32 cq_ring_entries;
    u32 cq_overflow;
    u32 cq_flags;

    // SQ 인덱스 배열
    u32 sq_array[];
};
```

메모리 레이아웃 예시(SQ=128, CQ=256)는 다음과 같다.

```text
┌─────────────────────────────────────┐ 0x0000
│ struct io_rings (메타데이터)         │
├─────────────────────────────────────┤ 0x0040
│ sq_array[128]                       │ SQ 인덱스 (u32 * 128 = 512B)
├─────────────────────────────────────┤ 0x0240
│ cqes[256]                           │ CQE 배열 (16B * 256 = 4KB)
└─────────────────────────────────────┘ 0x1240

별도 매핑:
┌─────────────────────────────────────┐
│ sqes[128]                           │ SQE 배열 (64B * 128 = 8KB)
└─────────────────────────────────────┘
```

### SQPOLL 스레드 생성

```c
static int io_sq_offload_create(struct io_ring_ctx *ctx,
                                 struct io_uring_params *p)
{
    int ret;

    if (p->flags & IORING_SETUP_SQ_AFF) {
        int cpu = p->sq_thread_cpu;

        if (cpu >= nr_cpu_ids || !cpu_online(cpu))
            return -EINVAL;

        ctx->sq_cpu = cpu;
    } else {
        ctx->sq_cpu = -1;
    }

    ctx->sq_thread_idle = msecs_to_jiffies(p->sq_thread_idle);
    if (ctx->sq_thread_idle == 0)
        ctx->sq_thread_idle = HZ;

    ctx->sqo_task = kthread_create(io_sq_thread, ctx, "io_uring-sq");
    if (IS_ERR(ctx->sqo_task)) {
        ret = PTR_ERR(ctx->sqo_task);
        ctx->sqo_task = NULL;
        return ret;
    }

    if (ctx->sq_cpu != -1) {
        struct cpumask mask;
        cpumask_clear(&mask);
        cpumask_set_cpu(ctx->sq_cpu, &mask);
        kthread_bind_mask(ctx->sqo_task, &mask);
    }

    wake_up_process(ctx->sqo_task);

    return 0;
}
```

이 코드는 SQ thread의 life cycle을 설명하는 의사코드다. thread 생성 API, idle 처리와 scheduling policy를 현재 커널 소스의 그대로인 ABI로 해석하지 않는다.

## 8. 성능 벤치마크

### 마이크로벤치마크

**시스템 콜 오버헤드**

```c
#include <unistd.h>
#include <time.h>

#define ITERATIONS 1000000

void bench_getpid() {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < ITERATIONS; i++) {
        getpid();
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    long ns = (end.tv_sec - start.tv_sec) * 1e9 +
              (end.tv_nsec - start.tv_nsec);
    printf("getpid: %ld ns/call\n", ns / ITERATIONS);
}

    // 결과는 CPU·커널·보안 완화 설정과 측정 방법에 따라 기록한다.
```

**NOP 비교**

```c
void bench_nop_io_uring() {
    struct io_uring ring;
    io_uring_queue_init(128, &ring, 0);

    for (int i = 0; i < ITERATIONS; i++) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_nop(sqe);
        io_uring_submit(&ring);

        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);
        io_uring_cqe_seen(&ring, cqe);
    }
    // submit/wait를 매 연산마다 수행한 기준 경로
}

void bench_nop_io_uring_sqpoll() {
    struct io_uring_params params = {0};
    params.flags = IORING_SETUP_SQPOLL;

    struct io_uring ring;
    io_uring_queue_init_params(128, &ring, &params);
    // SQPOLL thread의 CPU 사용량과 NEED_WAKEUP 경로까지 함께 측정
}
```

NOP는 저장장치나 네트워크 처리량을 측정하지 않고 ring bookkeeping의 하한만 본다. `getpid`와도 의미가 다른 작업이다. 측정에서는 batch size, SQ/CQ 크기, ring fd 옵션, CPU pinning, SQPOLL thread의 CPU 시간과 p50/p99를 기록해야 한다. `SQPOLL`도 polling thread가 잠들었거나 ring 상태가 요구하면 `io_uring_enter`로 깨워야 할 수 있다.

### 실전 워크로드

파일 I/O 비교는 먼저 page cache와 device I/O를 분리해야 한다.

| 축 | 반드시 고정하거나 기록할 조건 |
|---|---|
| 데이터 경로 | buffered I/O 또는 `O_DIRECT`, cold cache 또는 warm cache |
| 요청 형태 | sequential/random, read/write 비율, block size |
| 동시성 | queue depth, worker 수, ring 수, batch 크기 |
| 자원 등록 | fixed file·registered buffer 사용 여부 |
| polling | SQPOLL/IOPOLL, polling CPU affinity와 idle timeout |
| 장치 | 모델, firmware, filesystem, mount option, I/O scheduler |
| 결과 | IOPS·bytes/s, p50/p99/p99.9, user/sys CPU, context switch, energy |

동일한 4KiB 파일을 여러 번 읽으면 대부분 page cache와 메모리 복사 경로를 측정할 수 있다. 저장장치 성능을 비교하려면 working set이 메모리보다 큰지, direct I/O의 alignment를 지켰는지, run 사이 cache 상태가 같은지를 명시한다. 반대로 실제 서비스가 warm page cache를 사용한다면 cache를 강제로 비운 결과만 제시해서도 안 된다.

네트워크 비교에서는 `epoll` 서버와 `io_uring` 서버가 같은 protocol parser, response body, connection policy와 backpressure를 사용해야 한다. multishot accept/recv, buffer selection, zero-copy send 같은 기능을 한쪽에만 적용했다면 “API 차이”와 “구현 최적화 차이”를 분리해 결과를 제시한다.

```text
kernel:
cpu / NUMA topology:
NIC or block device:
filesystem / mount:
request size and mix:
concurrency / queue depth:
ring entries / batch:
registered files and buffers:
SQPOLL / IOPOLL:
warm-up and measurement duration:
throughput:
latency histogram:
user / system / polling CPU:
context switches / faults:
```

### perf와 trace 해석

`perf record`, `perf stat`, `strace -c`, io_uring tracepoint를 함께 사용하면 syscall 횟수, submission batch, worker fallback과 completion 지연을 구분할 수 있다.

```bash
perf stat -e cycles,instructions,context-switches,cpu-migrations \
    ./server

perf record -g ./server

sudo bpftrace -l 'tracepoint:io_uring:*'
```

`io_uring_get_sqe()`처럼 대부분 userspace에서 실행되는 helper가 profile에 보인다고 해서 커널 비용이라고 해석하면 안 된다. 반대로 syscall 수가 줄었다고 전체 지연이 반드시 줄어드는 것도 아니다. 장치 service time, queueing, copy, application logic과 polling CPU가 병목일 수 있다.

### 레이턴시 분포

평균만으로 tail latency를 판단하지 않는다. coordinated omission을 피하는 load generator를 사용하고, overload 구간에서는 offered load와 completed throughput을 모두 기록한다. 같은 throughput에서 비교하거나 saturation curve 전체를 그려야 높은 처리량 때문에 queue가 길어진 결과와 API 자체의 비용을 구분할 수 있다.

## 9. 보안 고려사항

### 권한 분리

SQ/CQ는 userspace와 커널이 공유하지만 userspace가 임의의 커널 포인터를 직접 쓰는 구조는 아니다. 커널은 ring index, opcode, user pointer와 등록 자원 수명을 검증해야 한다. 넓은 opcode 집합과 비동기 수명 관리가 커널 attack surface를 늘리므로 배포 환경에서는 필요한 기능과 syscall만 허용한다.

**Capability 요구**

```c
// 개념 검사다. 실제 권한 규칙은 대상 커널의 io_uring_setup(2)을 확인한다.
if (requested_feature_needs_privilege_or_policy_check(params))
    return -EPERM;
```

초기 커널에서는 SQPOLL에 특권이 필요했지만 Linux 5.11부터는 CAP_SYS_NICE capability가 있으면 비root로 사용할 수 있게 되었고, Linux 5.13 이후에는 일반적으로 SQPOLL 자체에 특별한 capability가 필요하지 않다. 다만 SQ thread CPU affinity 같은 옵션, resource limit, sysctl과 배포판 보안 정책은 별도 제약을 둘 수 있다. IOPOLL은 direct I/O와 polling을 지원하는 파일·장치 등 데이터 경로 제약을 먼저 만족해야 한다.

**Credential 체크**

```c
const struct cred *old_cred = override_creds(ctx->submitter_cred);
ret = io_issue_sqe(req, issue_flags);
revert_creds(old_cred);
```

**Per-operation 권한**

```c
if (req->opcode == IORING_OP_OPENAT) {
    ret = do_sys_openat2(req->open.dfd, req->open.filename,
                         &req->open.how);
    // 내부적으로 current->cred 검증
}
```

### 리소스 제한

**메모리 제한**

```c
// 개념 모델
validate_ring_entry_count(entries);
account_pinned_or_registered_resources(user, resources);
enforce_rlimit_and_cgroup_policy(user);
```

**파일 디스크립터 제한**

```c
validate_registered_file_count(nr_files);
for_each_registered_file(file)
    hold_reference_until_unregistered(file);
```

### 취약점 사례

io_uring은 커널에 빠르게 추가된 대규모 코드라 보안 취약점도 자주 발견되어 왔다. 대표 사례는 다음과 같다.

- **CVE-2020-29373 (샌드박스 탈출)**: 커널 스레드로 오프로드된 I/O 작업이 요청을 제출한 프로세스의 root 디렉토리(chroot)가 아닌 초기 네임스페이스의 root 기준으로 경로를 탐색해, chroot 격리 밖의 파일에 접근할 수 있었다. `fs_struct`를 async offload context에 잡아 두는 수정은 Linux 5.6에 포함되었고, 영향 범위는 5.6 이전 계열이다.
- **CVE-2021-41073 (권한 상승)**: `loop_rw_iter`가 사용자 등록 버퍼를 처리할 때 커널 포인터와 사용자 포인터를 혼동하는 type confusion이 있어, 로컬 공격자가 권한 상승에 활용할 수 있었다. 5.14.7에서 수정되었다.
- Google Project Zero는 2023년 분석("io_uring: New code, new bugs")에서 io_uring이 단기간에 가장 많은 커널 취약점이 보고된 서브시스템 중 하나임을 지적했고, 이 때문에 ChromeOS와 Android는 io_uring 사용을 제한했다.

### 안전한 사용 가이드

**입력 검증**

```c
if (sqe->off + sqe->len < sqe->off)  // 정수 오버플로우
    return -EINVAL;

if (sqe->buf_index >= ctx->nr_user_bufs)  // 범위 초과
    return -EINVAL;
```

**최소 권한 원칙**

```c
struct io_uring_params params = {0};
// polling이 필요하다는 측정 근거가 없으면 일반 모드에서 시작한다.
params.flags = 0;

io_uring_queue_init_params(128, &ring, &params);
```

**Seccomp 필터링**

```c
#include <seccomp.h>

scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL);
seccomp_rule_add(ctx, SCMP_ACT_ALLOW, __NR_io_uring_setup, 0);
seccomp_rule_add(ctx, SCMP_ACT_ALLOW, __NR_io_uring_enter, 0);
seccomp_rule_add(ctx, SCMP_ACT_ALLOW, __NR_io_uring_register, 0);
seccomp_load(ctx);
```

**Namespace 격리**

```bash
# host 전역 정책 예시. 값의 의미는 대상 커널 문서를 확인한다.
sysctl kernel.io_uring_disabled
```

`kernel.io_uring_disabled`는 일반적인 per-container switch가 아니라 host의 커널 정책이다. cgroup에 프로세스를 넣는 것만으로 io_uring opcode가 차단되지 않는다. 컨테이너별 제한은 seccomp, LSM, container runtime 정책과 커널 제공 제한을 조합한다.

## 10. 고급 주제

### Provided Buffers (사용자 제공 버퍼 선택)

애플리케이션은 buffer group에 여러 userspace buffer를 등록하거나 제공하고, recv SQE는 특정 고정 주소 대신 그 group에서 사용할 buffer를 커널이 선택하게 할 수 있다. 커널이 payload buffer를 필요할 때 임의로 할당해 주는 기능은 아니다. CQE의 buffer ID를 확인해 처리한 뒤 버퍼를 적절한 시점에 다시 제공해야 한다.

```c
#define BGID 0  // Buffer Group ID
#define NR_BUFS 128

void provided_buffers_example()
{
    struct io_uring ring;
    io_uring_queue_init(256, &ring, 0);

    char buffers[NR_BUFS][4096];

    for (int i = 0; i < NR_BUFS; i++) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_provide_buffers(sqe, buffers[i], 4096, 1, BGID, i);
    }
    io_uring_submit(&ring);

    int sockfd = /* ... */;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_recv(sqe, sockfd, NULL, 0, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = BGID;
    io_uring_submit(&ring);

    struct io_uring_cqe *cqe;
    io_uring_wait_cqe(&ring, &cqe);

    // CQE가 오류이거나 buffer가 선택되지 않았는지 먼저 검사
    if (cqe->res < 0 || !(cqe->flags & IORING_CQE_F_BUFFER)) {
        fprintf(stderr, "recv failed or no buffer (res=%d, flags=0x%x)\n",
                cqe->res, cqe->flags);
        io_uring_cqe_seen(&ring, cqe);
        io_uring_queue_exit(&ring);
        return;
    }

    int buf_id = cqe->flags >> IORING_CQE_BUFFER_SHIFT;

    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_provide_buffers(sqe, buffers[buf_id], 4096, 1, BGID, buf_id);
    io_uring_submit(&ring);

    io_uring_cqe_seen(&ring, cqe);
    io_uring_queue_exit(&ring);
}
```

### File Update

고정 파일 테이블을 동적으로 업데이트할 수 있다.

```c
void dynamic_file_update()
{
    struct io_uring ring;
    int fds[16];
    for (size_t i = 0; i < 16; ++i) fds[i] = -1;

    io_uring_queue_init(32, &ring, 0);
    io_uring_register_files(&ring, fds, 16);

    int new_fd = open("newfile.txt", O_RDONLY);
    io_uring_register_files_update(&ring, 0, &new_fd, 1);

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    char buf[4096];
    io_uring_prep_read(sqe, 0, buf, sizeof(buf), 0);
    sqe->flags |= IOSQE_FIXED_FILE;
    io_uring_submit(&ring);

    int dummy = -1;
    io_uring_register_files_update(&ring, 0, &dummy, 1);

    close(new_fd);
    io_uring_queue_exit(&ring);
}
```

### Timeout Operations

**상대 타임아웃과 절대 타임아웃**

`io_uring_prep_timeout`의 마지막 flag가 0이면 상대 타임아웃이다. 절대 타임아웃을 원하면 `IORING_TIMEOUT_ABS`를 명시해야 한다(현재 시각 + ts가 아니라 ts 자체를 만기 시각으로 해석).

```c
void relative_timeout(void)  // 5초 상대 타임아웃
{
    struct io_uring ring;
    io_uring_queue_init(8, &ring, 0);

    struct __kernel_timespec ts = { .tv_sec = 5, .tv_nsec = 0 };

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_timeout(sqe, &ts, 0, /*flags=*/0);  // 상대
    sqe->user_data = 0x71E0;
    io_uring_submit(&ring);

    struct io_uring_cqe *cqe;
    io_uring_wait_cqe(&ring, &cqe);
    if (cqe->res == -ETIME) printf("Timeout expired\n");
    io_uring_cqe_seen(&ring, cqe);
    io_uring_queue_exit(&ring);
}

void absolute_timeout(void)  // 절대 시각까지 대기
{
    struct io_uring ring;
    io_uring_queue_init(8, &ring, 0);

    struct __kernel_timespec ts;
    /* 예: 지금부터 5초 후의 절대 시각으로 만든 다음 ABS 플래그를 준다 */
    /* (실제로는 clock_gettime(CLOCK_MONOTONIC, ...) 등으로 채운다) */

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_timeout(sqe, &ts, 0, IORING_TIMEOUT_ABS);
    sqe->user_data = 0x71E1;
    io_uring_submit(&ring);

    struct io_uring_cqe *cqe;
    io_uring_wait_cqe(&ring, &cqe);
    io_uring_cqe_seen(&ring, cqe);
    io_uring_queue_exit(&ring);
}
```

**연결된 타임아웃**

```c
void linked_timeout()
{
    struct io_uring ring;
    io_uring_queue_init(8, &ring, 0);

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    char buf[4096];
    io_uring_prep_read(sqe, fd, buf, sizeof(buf), 0);
    sqe->flags |= IOSQE_IO_LINK;
    sqe->user_data = 1;

    struct __kernel_timespec ts = { .tv_sec = 1 };
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_link_timeout(sqe, &ts, 0);
    sqe->user_data = 2;

    io_uring_submit(&ring);

    struct io_uring_cqe *cqe;
    io_uring_wait_cqe(&ring, &cqe);

    if (cqe->user_data == 1 && cqe->res == -ECANCELED) {
        printf("Read timed out\n");
    }

    io_uring_cqe_seen(&ring, cqe);
    io_uring_queue_exit(&ring);
}
```

### Cancel Operations

```c
void cancel_operation()
{
    struct io_uring ring;
    io_uring_queue_init(16, &ring, 0);

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read(sqe, slow_fd, buf, sizeof(buf), 0);
    sqe->user_data = 0xEAD;
    io_uring_submit(&ring);

    sleep(1);

    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_cancel(sqe, 0xEAD, 0);
    sqe->user_data = 0xCACE;
    io_uring_submit(&ring);

    for (int i = 0; i < 2; i++) {
        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);
        io_uring_cqe_seen(&ring, cqe);
    }

    io_uring_queue_exit(&ring);
}
```

### Zero-Copy Send: IORING_OP_SEND_ZC

io_uring의 zero-copy 전송은 `MSG_ZEROCOPY` 플래그 단독으로 의미가 잡히지 않는다. 일반 socket의 `MSG_ZEROCOPY`는 `errqueue`에서 별도 비동기 알림(`SO_EE_CODE_ZEROCOPY_COPIED` 등)을 받아야 송신 버퍼 재사용 시점을 알 수 있다. io_uring은 이 알림을 CQE 두 개(송신 완료 + 알림)로 정규화한 전용 opcode `IORING_OP_SEND_ZC`(또는 sendmsg 버전 `IORING_OP_SENDMSG_ZC`)와 liburing helper(`io_uring_prep_send_zc`)를 제공한다. 이 helper를 쓰는 것이 정확한 형태다.

```c
#include <liburing.h>

void zerocopy_send(int sockfd, const void *buf, size_t len)
{
    struct io_uring ring;
    io_uring_queue_init(32, &ring, 0);

    // io_uring 전용 zero-copy send (IORING_OP_SEND_ZC).
    // 첫 CQE: 송신 자체에 대한 결과(res = 보낸 바이트 수)
    // 두 번째 CQE: IORING_CQE_F_NOTIF 플래그가 켜진 알림.
    //              버퍼 재사용 가능을 의미한다.
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_send_zc(sqe, sockfd, buf, len, /*flags=*/0, /*zc_flags=*/0);
    io_uring_sqe_set_data64(sqe, 1);
    io_uring_submit(&ring);

    // 첫 CQE의 F_MORE가 켜졌을 때만 notification을 더 기다린다.
    bool waiting = true;
    while (waiting) {
        struct io_uring_cqe *cqe;
        int ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0) {
            fprintf(stderr, "wait_cqe: %s\n", strerror(-ret));
            break;
        }

        if (cqe->flags & IORING_CQE_F_NOTIF) {
            printf("Buffer freed, safe to reuse\n");
            waiting = false;
        } else {
            if (cqe->res < 0)
                fprintf(stderr, "send: %s\n", strerror(-cqe->res));
            else
                printf("Send completion: %d bytes\n", cqe->res);

            if (cqe->flags & IORING_CQE_F_MORE) {
                printf("Buffer notification will follow\n");
            } else {
                // 별도 notification이 없으므로 이 요청이 더는 버퍼를 보유하지 않는다.
                waiting = false;
            }
        }
        io_uring_cqe_seen(&ring, cqe);
    }

    io_uring_queue_exit(&ring);
}
```

Zero-copy send는 보통 두 CQE를 만들지만 항상 두 개라고 가정하면 첫 CQE에 `IORING_CQE_F_MORE`가 없는 fallback·오류 경로에서 영원히 기다릴 수 있다. `F_MORE`가 켜졌을 때만 같은 `user_data`의 `IORING_CQE_F_NOTIF`를 기다리고, 그 전에는 buffer를 수정하지 않는다. notification은 커널이 buffer 수명을 더 붙잡지 않는다는 뜻이지 peer가 데이터를 받았거나 TCP ACK가 도착했다는 전달 보장이 아니다. 일반 `IORING_OP_SEND` + `MSG_ZEROCOPY` 조합은 errqueue를 별도로 처리해야 한다.


---

## 결론

io_uring은 리눅스 I/O의 패러다임을 바꾸었다. 핵심 정리는 다음 여섯 가지다.

1. 시스템 콜 최소화: 공유 메모리 링 버퍼로 배치 처리한다.
2. completion 모델: 파일과 소켓을 포함한 여러 opcode를 한 완료 모델로 다룬다. 작업에 따라 nonblocking 실행, inline 완료, io-wq worker fallback이 달라진다.
3. 등록 자원: 고정 파일과 등록·제공 버퍼로 반복 조회·pin·buffer 선택 비용을 줄인다. 이들 자체가 모두 zero-copy를 보장하지는 않는다.
4. 폴링 모드: SQPOLL과 IOPOLL은 조건이 맞을 때 syscall·interrupt 비용을 CPU polling과 교환한다.
5. 고급 기능: 멀티샷, 링크, 타임아웃, 취소를 지원한다.
6. 확장성: 커널 통합으로 지속적 기능 추가가 가능하다.

`epoll`보다 항상 빠른 것이 아니라 readiness와 completion 중 어느 모델이 workload에 맞는지가 먼저다. 작은 socket readiness loop에서는 `epoll`이 충분히 단순하고 효율적일 수 있고, 많은 파일 작업과 배치·등록 자원을 활용할 때 `io_uring`의 이점이 커질 수 있다. 커널과 liburing 버전별 opcode·flag 지원, 권한과 공격 표면도 배포 조건으로 확인해야 한다.

## 더 읽을거리

### 공식 문서
- Linux kernel documentation: `Documentation/io_uring.rst`
- Kernel source: `fs/io_uring.c`, `include/uapi/linux/io_uring.h`
- liburing repository: https://github.com/axboe/liburing
- liburing documentation: `man io_uring_setup`, `man io_uring_enter`, `man io_uring_register`

### 논문 및 발표
- Axboe, J. (2019). *Efficient IO with io_uring*. https://kernel.dk/io_uring.pdf
- Axboe, J. (2020). *io_uring and BPF*. Linux Plumbers Conference.
- Corbet, J. (2019). *Ringing in a new asynchronous I/O API*. LWN.net. https://lwn.net/Articles/776703/
- Corbet, J. (2020). *The rapid growth of io_uring*. LWN.net. https://lwn.net/Articles/810414/

### 서적
- Robert Love, *Linux System Programming* (2nd ed., O'Reilly, 2013)
- Michael Kerrisk, *The Linux Programming Interface* (No Starch Press, 2010)
- Brendan Gregg, *Systems Performance* (2nd ed., Addison-Wesley, 2020)

### 벤치마크 및 분석
- ScyllaDB, *How io_uring and eBPF Will Revolutionize Programming in Linux* (2020): https://www.scylladb.com/2020/05/05/how-io_uring-and-ebpf-will-revolutionize-programming-in-linux/
- Google Project Zero, *io_uring: New code, new bugs* (2023): https://googleprojectzero.blogspot.com/2023/06/io_uring-new-code-new-bugs.html

### 커널 소스 주요 파일
- `fs/io_uring.c` 핵심 구현
- `io_uring/io_uring.c` (5.19+ 분리된 구조)
- `io_uring/sqpoll.c` SQPOLL 구현
- `io_uring/opdef.c` 작업 정의
- `include/uapi/linux/io_uring.h` 사용자 API

### 추가 리소스
- io_uring by Example: https://github.com/frevib/io_uring-echo-server
- Lord of the io_uring 시리즈: https://unixism.net/loti/
- Awesome io_uring: https://github.com/espoal/awesome-iouring
