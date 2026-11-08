## 들어가며

> **핵심 질문**: "Namespaces와 Cgroups의 마법"


Docker 컨테이너를 실행하면 독립된 가상 머신처럼 보인다. 자신만의 파일 시스템, 네트워크 스택, 프로세스 공간을 가진다. 하지만 VM과 달리 커널은 공유한다. 어떻게 가능한가?

답은 Linux Namespaces와 Cgroups에 있다. 컨테이너는 하이퍼바이저가 아니라 격리된 프로세스다.

이 글은 컨테이너가 커널 레벨에서 어떻게 작동하는지, namespace 8종(MNT, PID, NET, IPC, UTS, USER, CGROUP, TIME)과 cgroup 리소스 제어, OverlayFS, OCI 표준과 runc, 그리고 Docker 없이 직접 컨테이너를 만드는 방법까지 다룬다.


---

## 목차
1. [컨테이너란: 프로세스 격리의 집합](#1-컨테이너란-프로세스-격리의-집합)
2. [Namespaces: 독립된 세계 만들기](#2-namespaces-독립된-세계-만들기)
3. [Cgroups: 리소스 제한](#3-cgroups-리소스-제한)
4. [Union Filesystem: OverlayFS](#4-union-filesystem-overlayfs)
5. [Docker의 내부 동작](#5-docker의-내부-동작)
6. [OCI 표준과 runc](#6-oci-표준과-runc)
7. [보안 고려사항](#7-보안-고려사항)
8. [직접 컨테이너 만들기](#8-직접-컨테이너-만들기)


---

## 1. 컨테이너란: 프로세스 격리의 집합

### VM vs Container

**가상 머신:**
```text
[App A] [App B]
[Guest OS] [Guest OS]
[Hypervisor]
[Host OS]
[Hardware]
```

각 VM은 완전한 OS를 실행한다. 부팅 시간, 메모리 오버헤드가 크다.

**컨테이너:**
```text
[App A] [App B]
[Container Runtime]
[Host OS (shared kernel)]
[Hardware]
```

모든 컨테이너가 호스트 커널을 공유한다. 프로세스 격리만으로 고립을 달성.

### 격리의 본질

```bash
# 호스트에서 컨테이너 프로세스 확인
$ docker run -d nginx
$ ps aux | grep nginx
root     12345  ... nginx: master process
```

컨테이너 "내부"의 nginx는 호스트에서 보면 **그냥 프로세스**다.

**차이점:**
- 컨테이너는 자신이 PID 1이라고 믿는다
- 호스트 파일 시스템을 볼 수 없다
- 독립된 네트워크 인터페이스를 가진다

이 모든 것이 **커널의 착시(illusion)** 다.

### 핵심 기술 스택

```text
┌─────────────────────────────────┐
│   Docker / Podman / containerd  │  ← 고수준 런타임
├─────────────────────────────────┤
│   runc / crun                   │  ← OCI 런타임
├─────────────────────────────────┤
│   Namespaces + Cgroups          │  ← 커널 기능
└─────────────────────────────────┘
```

모든 컨테이너 기술의 기반은 **Linux 커널**이다.

## 2. Namespaces: 독립된 세계 만들기

### Namespace 개념

Namespace는 **전역 자원의 격리된 뷰**를 제공한다.

```c
// include/linux/nsproxy.h (현대 커널의 핵심 필드, 단순화)
struct nsproxy {
    atomic_t count;
    struct uts_namespace *uts_ns;       // 호스트명, 도메인명
    struct ipc_namespace *ipc_ns;       // System V IPC, POSIX MQ
    struct mnt_namespace *mnt_ns;       // 파일 시스템 마운트 트리
    struct pid_namespace *pid_ns_for_children;
    struct net *net_ns;                 // 네트워크 스택
    struct cgroup_namespace *cgroup_ns; // /proc/self/cgroup 뷰
    struct time_namespace *time_ns;     // CLOCK_MONOTONIC/BOOTTIME 오프셋
    struct time_namespace *time_ns_for_children;
};

// user namespace는 nsproxy가 아니라 credentials에 속한다.
struct cred {
    struct user_namespace *user_ns;
    // uid/gid, capability sets, ...
};
```

Linux가 제공하는 namespace 종류는 MNT, PID, NET, IPC, UTS, USER, CGROUP, TIME 여덟 축이지만 모두 `nsproxy` 한 구조체에 들어가지는 않는다. 태스크는 `nsproxy`와 `cred->user_ns`를 함께 통해 namespace 상태에 연결되고, 현재 PID namespace는 `task_active_pid_ns()`로 조회한다. `pid_ns_for_children`은 이후 생성할 자식이 들어갈 namespace다.

### PID Namespace

**문제**: 두 컨테이너가 모두 PID 1을 가질 수 있나?

```bash
# 컨테이너 A
$ docker exec container-a ps aux
PID   USER     COMMAND
1     root     /bin/sh

# 컨테이너 B
$ docker exec container-b ps aux
PID   USER     COMMAND
1     root     nginx
```

둘 다 PID 1이다. 하지만 충돌하지 않는다!

**해법: PID Namespace 계층 구조**

```text
Host PID Namespace
├─ PID 1234 (container A)
│  └─ Child PID Namespace
│     └─ PID 1 (container A 내부)
│
└─ PID 5678 (container B)
   └─ Child PID Namespace
      └─ PID 1 (container B 내부)
```

PID namespace는 **계층적**이다. 자식 namespace의 프로세스는 부모에서도 보이지만, 역은 불가능.

**직접 생성:**
```c
#define _GNU_SOURCE
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

int child_func(void *arg) {
    printf("Child PID: %d\n", getpid());  // 1 출력!
    return 0;
}

int main() {
    char stack[4096];

    // 새 PID namespace에서 자식 생성
    clone(child_func, stack + 4096,
          CLONE_NEWPID | SIGCHLD, NULL);

    sleep(1);
    return 0;
}
```

`CLONE_NEWPID`로 생성된 프로세스는 자신이 PID 1이라고 믿는다.

### Mount Namespace

**문제**: 컨테이너마다 다른 루트 파일 시스템?

```bash
# 호스트
$ ls /
bin  boot  dev  etc  home  ...

# 컨테이너 (Ubuntu 이미지)
$ docker exec ubuntu ls /
bin  boot  dev  etc  home  ...  # 다른 내용!
```

**해법: `pivot_root`와 Mount Namespace**

```c
// fs/mount.h
struct mnt_namespace {
    atomic_t count;
    struct mount *root;  // 이 namespace의 루트 마운트
    struct list_head list;
};
```

각 mount namespace는 독립된 마운트 트리를 가진다.

**chroot vs pivot_root:**

| 기능 | chroot | pivot_root |
|------|--------|------------|
| 격리 성질 | pathname root만 변경하며 열린 fd·mount 권한을 별도로 통제해야 함 | mount namespace의 root mount를 교체하지만 그 자체만으로 완전한 보안 경계는 아님 |
| 이전 루트 | 여전히 접근 가능 | 새 root로 교체되고 이전 root는 지정 경로에 남아 분리 대상이 됨(데이터 삭제가 아님) |
| 사용 | 개발 환경 | 프로덕션 컨테이너 |

**pivot_root 사용:**
```c
#include <sys/mount.h>
#include <unistd.h>

int setup_rootfs(const char *new_root) {
    // 1. 호스트의 shared mount propagation으로 변경이 역전파되지 않게 차단
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1)
        return -1;

    // 2. new_root를 마운트 포인트로
    mount(new_root, new_root, NULL, MS_BIND | MS_REC, NULL);

    // 3. 작업 디렉토리 변경
    chdir(new_root);

    // 4. pivot_root 실행
    mkdir(".old_root", 0700);
    syscall(SYS_pivot_root, ".", ".old_root");

    // 5. 이전 루트를 mount 분리(데이터 삭제가 아님)
    chdir("/");
    umount2("/.old_root", MNT_DETACH);
    rmdir("/.old_root");

    return 0;
}
```

`pivot_root`는 root mount의 역할을 교체하고 이전 root를 지정한 경로로 옮기는 연산이지, 옛 filesystem의 내용을 삭제하는 연산이 아니다. 뒤의 `umount2(..., MNT_DETACH)`도 mount를 분리할 뿐이다.

### Network Namespace

**문제**: 각 컨테이너가 자신만의 IP 주소?

```bash
# 컨테이너 A
$ docker exec container-a ip addr
1: lo: ...
    inet 127.0.0.1/8
2: eth0: ...
    inet 172.17.0.2/16

# 컨테이너 B
$ docker exec container-b ip addr
1: lo: ...
    inet 127.0.0.1/8
2: eth0: ...
    inet 172.17.0.3/16
```

각자 독립된 `lo`, `eth0`를 가진다!

**해법: veth pair + bridge**

```text
Host Network NS
┌─────────────────────────────────────┐
│  docker0 bridge (172.17.0.1)        │
│    │                                 │
│    ├─ veth123a ←→ veth123b ────┐    │
│    │                            │    │
│    ├─ veth456a ←→ veth456b ────┼─┐  │
└────│────────────────────────────│─│──┘
     │ Container A Net NS         │ │
     │ ┌──────────────────────┐   │ │
     │ │ eth0 (veth123b)      │   │ │
     │ │ 172.17.0.2           │   │ │
     │ └──────────────────────┘   │ │
     └────────────────────────────┘ │
       Container B Net NS           │
       ┌──────────────────────┐     │
       │ eth0 (veth456b)      │     │
       │ 172.17.0.3           │     │
       └──────────────────────┘     │
       ──────────────────────────────┘
```

**veth pair 생성:**
```bash
# 네트워크 namespace 생성
ip netns add container1

# veth pair 생성
ip link add veth0 type veth peer name veth1

# veth1을 container1로 이동
ip link set veth1 netns container1

# 설정
ip addr add 172.17.0.1/24 dev veth0
ip link set veth0 up

ip netns exec container1 ip addr add 172.17.0.2/24 dev veth1
ip netns exec container1 ip link set veth1 up
ip netns exec container1 ip link set lo up
```

### UTS Namespace

**Hostname 격리:**
```bash
# 호스트
$ hostname
my-host

# 컨테이너
$ docker run --hostname web-server ubuntu hostname
web-server
```

**구현:**
```c
#define _GNU_SOURCE
#include <sched.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

int child_func(void *arg) {
    sethostname("container", 9);
    execlp("sh", "sh", NULL);
    return 0;
}

int main() {
    char stack[4096];
    clone(child_func, stack + 4096,
          CLONE_NEWUTS | SIGCHLD, NULL);
    wait(NULL);
    return 0;
}
```

### IPC Namespace

**System V IPC 격리:**
- Message queues (`msgget`)
- Semaphores (`semget`)
- Shared memory (`shmget`)

```bash
# 호스트
$ ipcmk -M 1024
Shared memory id: 0

$ ipcs -m
------ Shared Memory Segments --------
key        shmid      owner      bytes
0x1234     0          root       1024

# 컨테이너 (다른 IPC namespace)
$ docker run -it ubuntu ipcs -m
------ Shared Memory Segments --------
(empty)
```

### User Namespace

**가장 강력한 격리: UID 재매핑**

```bash
# 컨테이너 내부
$ whoami
root

$ id
uid=0(root) gid=0(root)

# 호스트에서 보면
$ ps aux | grep container-process
100000   12345  ...
```

컨테이너는 자신이 root라고 믿지만, 호스트에서는 비특권 UID 100000!

**UID 매핑:**
```text
Container UID  →  Host UID
0 (root)       →  100000
1              →  100001
...
65535          →  165535
```

**설정:**
```bash
# /etc/subuid
user:100000:65536

# /etc/subgid
user:100000:65536

# 컨테이너 실행
unshare --user --map-auto --setuid 0 --setgid 0
```

**보안 이점:**
- 컨테이너 탈출 시에도 호스트에서 권한 없음
- rootless container 가능

### Cgroup Namespace

**Cgroup 경로 가상화:**

```bash
# 호스트
$ cat /proc/self/cgroup
0::/user.slice/user-1000.slice

# 컨테이너 (cgroup v2)
$ cat /proc/self/cgroup
0::/
```

컨테이너는 자신이 cgroup 루트에 있다고 믿는다.

## 3. Cgroups: 리소스 제한

### Cgroup의 필요성

**문제**: 컨테이너가 호스트 자원을 모두 소비?

```bash
# fork bomb
$ docker run ubuntu bash -c ':(){ :|:& };:'
```

Namespace만으로는 자원 사용량을 제한할 수 없다.

**해법: Cgroups (Control Groups)**

### Cgroup v1 vs v2

**Cgroup v1 (Legacy):**
```text
/sys/fs/cgroup/
├── cpu/
│   ├── docker/
│   │   └── <container-id>/
│   │       ├── cpu.shares
│   │       └── cpu.cfs_quota_us
├── memory/
│   ├── docker/
│   │   └── <container-id>/
│   │       ├── memory.limit_in_bytes
│   │       └── memory.usage_in_bytes
└── blkio/
    └── ...
```

각 서브시스템(cpu, memory,...)이 독립적 계층.

**문제점:**
- 복잡한 관리 (각 컨트롤러마다 다른 경로)
- 일관성 없는 인터페이스
- 계층 충돌 가능

**Cgroup v2 (Unified Hierarchy):**
```text
/sys/fs/cgroup/
└── system.slice/
    └── docker-<id>.scope/
        ├── cgroup.controllers  (cpu memory io pids)
        ├── cpu.max
        ├── memory.max
        └── io.max
```

**개선점:**
- 단일 계층 구조
- 통일된 인터페이스
- PSI (Pressure Stall Information)
- 메모리 보호 (memory.low, memory.min)

### CPU 제한

**Cgroup v1:**
```bash
# CPU 0.5개에 해당하는 시간 예산 제한
echo 50000 > /sys/fs/cgroup/cpu/mycontainer/cpu.cfs_quota_us
echo 100000 > /sys/fs/cgroup/cpu/mycontainer/cpu.cfs_period_us
# quota/period=0.5 → 전체 장비의 50%가 아니라 aggregate로 CPU 0.5개에 해당
```

**Cgroup v2:**
```bash
# CPU 0.5개에 해당하는 시간 예산 제한
echo "50000 100000" > /sys/fs/cgroup/mycontainer/cpu.max
# <quota> <period>; quota/period가 CPU 개수에 해당하는 시간 예산
```

**Docker 예시:**
```bash
# 1 CPU 전체 사용 가능
docker run --cpus=1 ubuntu stress --cpu 8

# CPU shares (상대적 가중치)
docker run --cpu-shares=512 ubuntu  # 기본은 1024
```

**커널 구현:**
```c
// kernel/sched/sched.h
struct cfs_bandwidth {
    ktime_t period;         // 구성된 bandwidth period
    u64 quota;              // 사용 가능 시간
    u64 runtime;            // 남은 시간
    // ...
};

// kernel/sched/fair.c — 스케줄러가 주기적으로 체크
if (cfs_rq->runtime_remaining <= 0) {
    throttle_cfs_rq(cfs_rq);  // CPU 할당 중단
}
```

### Memory 제한

**OOM (Out of Memory) 처리:**

```bash
# Cgroup v2
echo "512M" > /sys/fs/cgroup/mycontainer/memory.max
echo "256M" > /sys/fs/cgroup/mycontainer/memory.high  # soft limit
```

**메모리 계층:**
```text
memory.max        ← Hard limit (초과 시 OOM kill)
memory.high       ← Soft limit (초과 시 reclaim 압력)
memory.low        ← Best-effort 보호
memory.min        ← 절대 보호
```

**Docker 예시:**
```bash
docker run -m 512m --memory-reservation 256m ubuntu
```

**OOM 동작:**
```c
// mm/oom_kill.c
void out_of_memory(struct oom_control *oc) {
    struct task_struct *victim = select_bad_process(oc);
    if (victim)
        oom_kill_process(oc, victim, ...);
}
```

실제 희생자 점수 평가는 `select_bad_process()`/`oom_evaluate_task()` 경로가 담당하고, `oom_kill_process()`는 이미 선택된 희생자를 처리한다. cgroup OOM과 system-wide OOM은 후보 집합과 정책이 다르므로 “가장 많이 쓴 프로세스 하나”로 단순화하지 않는다.

**메모리 계정:**
```bash
# 현재 사용량
cat /sys/fs/cgroup/mycontainer/memory.current

# 상세 통계
cat /sys/fs/cgroup/mycontainer/memory.stat
anon 134217728
file 67108864
shmem 0
...
```

### I/O 제한

**Block I/O throttling:**

```bash
# Cgroup v2
# 디바이스 8:0에 대해 초당 10MB로 제한
echo "8:0 rbps=10485760 wbps=10485760" > /sys/fs/cgroup/mycontainer/io.max
```

**Docker:**
```bash
docker run --device-read-bps /dev/sda:1mb \
           --device-write-bps /dev/sda:1mb ubuntu
```

**I/O Weight (proportional):**
```bash
# 100 = 기본값, 범위는 1-10000
echo "8:0 100" > /sys/fs/cgroup/mycontainer/io.weight
```

### PID 제한

**Fork bomb 방어:**

```bash
# 최대 task 수 제한(thread도 각각 하나로 센다)
echo 100 > /sys/fs/cgroup/mycontainer/pids.max

# 현재 task 수
cat /sys/fs/cgroup/mycontainer/pids.current
```

**Docker:**
```bash
# thread를 포함한 task/TID 수 기준
docker run --pids-limit 100 ubuntu
```

### Network 대역폭 제어

Cgroup v2에는 v1의 `net_cls`·`net_prio`와 같은 전용 controller가 없다. v1의 두 controller도 패킷 분류·우선순위를 표시할 뿐 자체적으로 대역폭을 제한하지 않는다. 실제 rate limiting은 **tc** 또는 cgroup hook에 붙인 eBPF와 조합한다:

```bash
# 컨테이너 veth에 대역폭 제한
tc qdisc add dev veth123 root tbf rate 1mbit burst 32768 latency 400ms
```

또는 **eBPF + cgroup** (modern):
```c
SEC("cgroup/skb")
int limit_egress(struct __sk_buff *skb) {
    // 대역폭 제한 로직
    return 1;
}
```

## 4. Union Filesystem: OverlayFS

### 계층형 이미지의 필요성

**문제**: Ubuntu 이미지 1GB를 100개 컨테이너가 사용하면 100GB?

**해법: 레이어 공유**

```text
Container A          Container B
[App Layer A]       [App Layer B]
      ↓                    ↓
    [Node.js Layer] ← 공유!
      ↓
    [Ubuntu Base] ← 공유!
```

### OverlayFS 구조

**3개 레이어:**
```text
┌─────────────────────────┐
│   merged (컨테이너 뷰)    │
└───────────┬─────────────┘
            │
    ┌───────┴────────┐
    │                │
[lower (RO)]    [upper (RW)]
  이미지          쓰기 레이어
                     │
                [work dir]
```

**예시:**
```bash
# 디렉토리 준비
mkdir -p lower upper work merged

# lower에 파일 생성
echo "base" > lower/file.txt

# overlayfs 마운트
mount -t overlay overlay \
  -o lowerdir=lower,upperdir=upper,workdir=work \
  merged

# merged에서 파일 수정
echo "modified" > merged/file.txt

# 실제로는 upper에 저장됨
cat upper/file.txt  # "modified"
cat lower/file.txt  # "base" (변경 안 됨)
```

### Copy-on-Write (CoW)

**파일 수정 과정:**
```text
1. 읽기: lower에서 읽음 (O)
2. 쓰기 발생
   → lower 파일을 upper로 복사
   → upper에서 수정
3. 이후 읽기: upper에서 읽음
```

**삭제 처리:**
```bash
# merged에서 파일 삭제
rm merged/file.txt

# 실제로는 "whiteout" 파일 생성
ls -la upper/
c--------- 1 root root 0, 0 Jan  1 00:00 file.txt  # character device 0:0
```

**커널 구현:**
```c
// fs/overlayfs/copy_up.c
int ovl_copy_up_one(struct dentry *parent, struct dentry *dentry) {
    // lower 파일 열기
    old_file = ovl_path_open(&lowerpath, O_RDONLY);

    // upper에 새 파일 생성
    new_file = ovl_path_open(&upperpath, O_WRONLY | O_CREAT);

    // 데이터 복사: reflink(extent 공유) 시도 후, 안 되면 splice로 1 MiB 청크 복사
    loff_t len = i_size_read(d_inode(dentry));
    cloned = vfs_clone_file_range(old_file, 0, new_file, 0, len, 0);
    if (cloned != len) {
        loff_t old_pos = 0, new_pos = 0;
        while (len) {
            size_t this_len = min_t(size_t, len, OVL_COPY_UP_CHUNK_SIZE);  // 1 << 20
            bytes = do_splice_direct(old_file, &old_pos, new_file, &new_pos,
                                     this_len, SPLICE_F_MOVE);
            if (bytes <= 0)
                break;
            len -= bytes;
        }
    }

    // 메타데이터 복사 (권한, 소유자, ...)
    ovl_copy_xattr(old_file, new_file);

    return 0;
}
```

### Docker Image Layers

**이미지 구조:**
```bash
$ docker image inspect ubuntu:latest
"RootFS": {
    "Layers": [
        "sha256:abc123...",  # base layer
        "sha256:def456...",  # package updates
        "sha256:ghi789..."   # application
    ]
}
```

**Dockerfile과 레이어:**
```dockerfile
FROM ubuntu:20.04           # 기존 base image를 선택
RUN apt-get update          # filesystem layer 생성
RUN apt-get install nginx   # filesystem layer 생성
COPY index.html /var/www/   # filesystem layer 생성
```

모든 Dockerfile 명령이 filesystem layer를 만드는 것은 아니다. 일반적으로 `RUN`, `COPY`, `ADD`가 root filesystem diff를 만들고, `ENV`, `CMD`, `LABEL` 등은 image configuration metadata를 바꾼다. `FROM`은 기존 base image의 layer chain을 출발점으로 선택한다.

**레이어 재사용:**
```text
Image A: [Ubuntu] → [apt update] → [nginx]
                                     ↑
Image B: [Ubuntu] → [apt update] ──┘
         └──────────┬──────────┘
              공유 레이어
```

### 저장소 드라이버

현대 Docker Engine에서 선택 가능한 대표 storage driver는 다음과 같다. 정확한 지원 조합은 Engine 버전과 배포판을 확인한다:

| Driver | Backing FS | CoW | Performance |
|--------|-----------|-----|-------------|
| overlay2 | ext4/xfs | Yes | |
| btrfs | btrfs | Yes | |
| zfs | zfs | Yes | |
| vfs | 모든 일반 FS | layer 전체 복사 | 검증·fallback 용도 |

과거 `devicemapper` driver는 dm-thin snapshot을 이용한 **블록 단위 CoW**였다. CoW가 없었던 것이 아니며, Docker Engine 25.0에서 driver 자체가 제거되었다. AUFS도 현재의 기본 선택지로 다루지 않는다.

**graph driver 기반 Engine에서 overlay2가 기본이던 이유:**
- 커널 메인라인 지원
- 높은 성능
- 안정성

Engine 29.0 이상의 fresh installation은 기본 backend가 containerd image store이고, 기존 upgrade 설치는 legacy graph driver를 유지할 수 있다. graph driver의 overlay2와 containerd snapshotter의 overlayfs는 서로 다른 backend다.

## 5. Docker의 내부 동작

### Docker 아키텍처

```text
┌──────────────────────────────────────┐
│  docker CLI                          │
└──────────┬───────────────────────────┘
           │ REST API
┌──────────▼───────────────────────────┐
│  dockerd (daemon)                    │
│  ├─ Image management                 │
│  ├─ Network management               │
│  └─ Volume management                │
└──────────┬───────────────────────────┘
           │ gRPC
┌──────────▼───────────────────────────┐
│  containerd                          │
│  └─ Container lifecycle              │
└──────────┬───────────────────────────┘
           │
┌──────────▼───────────────────────────┐
│  runc (OCI runtime)                  │
│  └─ Namespace/Cgroup 설정            │
└──────────────────────────────────────┘
```

### Container 생성 과정

**1. 이미지 Pull:**
```bash
$ docker pull nginx
```

레이어별로 다운로드해 저장한다. overlay2 graph driver를 쓰면 `/var/lib/docker/overlay2/`에 저장되지만, Engine 버전과 fresh/upgrade 조건에 따라 backend가 달라진다.

**2. 컨테이너 생성:**
```bash
$ docker create --name web nginx
```

- containerd가 컨테이너 메타데이터 생성
- OverlayFS 준비 (upper/work dir)

**3. 컨테이너 시작:**
```bash
$ docker start web
```

`runc`가 실행:
```c
// libcontainer/container_linux.go
func (c *linuxContainer) start(process *Process) error {
    // 1. Namespace 생성
    c.createNamespaces()

    // 2. Cgroup 설정
    c.createCgroups()

    // 3. rootfs 마운트
    c.setupRootfs()

    // 4. 초기 프로세스 실행
    c.exec()
}
```

**한 가지 단순화한 구현의 세부 단계:**
```text
1. clone()/clone3()의 CLONE_NEW* 또는 unshare()로 namespace 구성
   → 새 namespace 생성

2. cgroup v2 hierarchy에 process를 배치하고 controller 설정
   /sys/fs/cgroup/<runtime-scope>/cgroup.procs

3. mount namespace에서 pivot_root/chroot와 mount 조합으로 rootfs 구성

4. capability, seccomp, LSM, uid/gid와 descriptor를 정리한 뒤 execve()
   → 컨테이너 메인 프로세스 시작
```

실제 OCI runtime은 `clone3`, `unshare`, `setns`, helper process와 systemd가 위임한 cgroup을 조합할 수 있다. 위 순서와 경로는 이해를 위한 모델이지 Docker·containerd·runc 전체의 고정 call trace가 아니다.

### 네트워크 설정

**bridge 모드 (기본):**
```bash
$ docker run -p 8080:80 nginx
```

**과정:**
1. veth pair 생성
2. 한쪽을 컨테이너 netns로 이동
3. 다른 쪽을 docker0 bridge에 연결
4. iptables NAT 규칙 추가

```bash
# 생성된 iptables 규칙
$ iptables -t nat -L
DNAT tcp dpt:8080 to:172.17.0.2:80
```

**host 모드:**
```bash
$ docker run --network host nginx
```

호스트 network namespace 공유 (격리 없음).

**none 모드:**
```bash
$ docker run --network none nginx
```

네트워크 없음 (loopback만).

## 6. OCI 표준과 runc

### OCI (Open Container Initiative)

**문제**: Docker, rkt, LXC... 각자 다른 구현.

**해결**: 표준화!

**OCI Runtime Spec:**
- 컨테이너를 어떻게 실행할지 정의
- `config.json` 형식

**OCI Image Spec:**
- 이미지를 어떻게 패키징할지 정의
- Layer 형식, manifest

### config.json 예시

```json
{
  "ociVersion": "1.0.0",
  "process": {
    "terminal": true,
    "user": { "uid": 0, "gid": 0 },
    "args": ["/bin/sh"],
    "env": ["PATH=/usr/local/sbin:/usr/local/bin:..."],
    "cwd": "/"
  },
  "root": {
    "path": "rootfs",
    "readonly": false
  },
  "hostname": "my-container",
  "mounts": [
    {
      "destination": "/proc",
      "type": "proc",
      "source": "proc"
    }
  ],
  "linux": {
    "namespaces": [
      { "type": "pid" },
      { "type": "network" },
      { "type": "mount" },
      { "type": "uts" }
    ],
    "resources": {
      "memory": { "limit": 536870912 },
      "cpu": { "quota": 50000, "period": 100000 }
    }
  }
}
```

### runc 사용

```bash
# OCI bundle 준비
mkdir -p mycontainer/rootfs
cd mycontainer

# rootfs 구성 (Ubuntu 예시)
docker export $(docker create ubuntu) | tar -C rootfs -xf -

# config.json 생성
runc spec

# 컨테이너 실행
runc run mycontainer
```

**runc의 역할:**
- `config.json` 파싱
- Namespace/Cgroup 설정
- rootfs 마운트
- 프로세스 실행

### Alternative Runtimes

**crun (C로 작성):**
```bash
# runc보다 빠르고 메모리 효율적
crun run mycontainer
```

**youki (Rust):**
```bash
# 메모리 안전성 강화
youki create mycontainer
youki start mycontainer
```

**gVisor (runsc):**
```bash
# User-space 커널로 강화된 격리
docker run --runtime=runsc nginx
```

## 7. 보안 고려사항

컨테이너의 격리는 namespace와 cgroup만으로 완성되지 않는다. 같은 커널을 공유하는 만큼, 컨테이너 내부 프로세스가 호스트 권한을 얻을 가능성을 추가 메커니즘으로 차단해야 한다. Docker와 runc는 컨테이너를 띄울 때 다음 다섯 가지 보호를 적용한다. 앞의 셋은 기본값이고, User Namespace(`userns-remap`)와 읽기 전용 rootfs 같은 권한 옵션은 운영자가 명시적으로 켜야 한다.

- **Capabilities 제한**: `CAP_SYS_ADMIN`, `CAP_SYS_MODULE` 등 위험한 권한을 bounding set에서 제거하고, 컨테이너에 14개 안팎의 capability만 남긴다.
- **Seccomp 프로필**: 약 300개 시스템 콜만 허용하고, `reboot`, `swapon`, `kexec_load` 같은 호출은 `SCMP_ACT_ERRNO`로 차단한다.
- **LSM(AppArmor/SELinux) 프로필**: 컨테이너 프로세스가 호스트 경로나 디바이스에 접근하지 못하도록 강제 접근 통제(MAC) 정책을 부착한다.
- **User Namespace**: 컨테이너 내부의 root(UID 0)를 호스트의 비특권 UID로 매핑한다. 탈출이 발생해도 호스트 권한이 없다.
- **읽기 전용 rootfs와 권한 옵션**: `--read-only`, `--privileged` 금지, `/var/run/docker.sock` 노출 금지 같은 운영 정책으로 공격 표면을 줄인다.

가장 빈번한 사고는 메커니즘 자체의 결함이 아니라 운영 실수에서 나온다. `--privileged` 컨테이너, 호스트 `/proc` 마운트, Docker 소켓 마운트는 단일 명령으로 격리를 무력화한다. CVE-2022-0847(Dirty Pipe), CVE-2016-5195(Dirty COW) 같은 커널 버그는 컨테이너에서도 그대로 통하므로 호스트 커널 패치가 필수다.

## 8. 직접 컨테이너 만들기

### 최소 컨테이너 (C)

```c
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>

#define STACK_SIZE (1024 * 1024)

static char child_stack[STACK_SIZE];

int child_func(void *arg) {
    char *rootfs = (char *)arg;

    // 1. Hostname 설정
    if (sethostname("container", 9) == -1)
        return -1;

    // 2. mount namespace의 전파를 private로 (호스트로 역전파 차단)
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1)
        return -1;

    // 3. rootfs 변경
    if (chroot(rootfs) == -1)
        return -1;
    if (chdir("/") == -1)
        return -1;

    // 4. /proc 마운트
    if (mount("proc", "/proc", "proc", 0, NULL) == -1)
        return -1;

    // 5. 셸 실행
    char *argv[] = {"/bin/sh", NULL};
    execv(argv[0], argv);

    return -1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <rootfs>\n", argv[0]);
        return 1;
    }

    // Namespace 플래그
    int flags = CLONE_NEWNS    // Mount
              | CLONE_NEWUTS   // Hostname
              | CLONE_NEWPID   // PID
              | CLONE_NEWIPC;  // IPC

    // 자식 프로세스 생성
    pid_t pid = clone(child_func,
                      child_stack + STACK_SIZE,
                      flags | SIGCHLD,
                      argv[1]);

    if (pid == -1) {
        perror("clone");
        return 1;
    }

    // 대기
    waitpid(pid, NULL, 0);

    return 0;
}
```

**컴파일 및 실행:**
```bash
# 컴파일
gcc -o minicontainer minicontainer.c

# rootfs 준비
mkdir rootfs
# Alpine Linux rootfs 다운로드
wget https://dl-cdn.alpinelinux.org/alpine/v3.18/releases/x86_64/alpine-minirootfs-3.18.0-x86_64.tar.gz
tar -C rootfs -xzf alpine-minirootfs-*.tar.gz

# 실행 (root 필요)
sudo ./minicontainer rootfs
```

### Cgroup 추가

```c
#include <fcntl.h>
#include <string.h>

int setup_cgroup(pid_t pid) {
    char path[256];
    char buf[32];

    // Cgroup 디렉토리 생성
    if (mkdir("/sys/fs/cgroup/minicontainer", 0755) == -1)
        return -1;

    // Memory 제한 (512MB)
    snprintf(path, sizeof(path),
             "/sys/fs/cgroup/minicontainer/memory.max");
    int fd = open(path, O_WRONLY);
    if (fd == -1)
        return -1;
    if (write(fd, "536870912", 9) < 0) {
        close(fd);
        return -1;
    }
    close(fd);

    // PID 추가
    snprintf(path, sizeof(path),
             "/sys/fs/cgroup/minicontainer/cgroup.procs");
    fd = open(path, O_WRONLY);
    if (fd == -1)
        return -1;
    snprintf(buf, sizeof(buf), "%d", pid);
    if (write(fd, buf, strlen(buf)) < 0) {
        close(fd);
        return -1;
    }
    close(fd);

    return 0;
}

int main(int argc, char *argv[]) {
    // ... (이전 코드)

    int gate[2];
    pipe2(gate, O_CLOEXEC);
    struct ChildArgs child_args = {.start_gate = gate[0]};
    pid_t pid = clone(child_main, child_stack + STACK_SIZE, clone_flags, &child_args);

    // 자식은 gate에서 읽으며 execve 전에 대기한다.
    if (setup_cgroup(pid) == -1) {
        // 필수 격리/제한 설정 실패 → gate를 열지 않는다(fail-closed).
        close(gate[0]);
        close(gate[1]);
        waitpid(pid, NULL, 0);
        return 1;
    }
    write(gate[1], "x", 1);  // cgroup attach가 끝난 뒤 실행 허용
    close(gate[0]);
    close(gate[1]);

    waitpid(pid, NULL, 0);

    // 정리
    rmdir("/sys/fs/cgroup/minicontainer");

    return 0;
}
```

동기화 없이 `clone()` 뒤에 cgroup을 붙이면 자식이 먼저 `execve()`하여 제한 밖에서 실행되는 race가 생긴다. 실제 runtime은 위와 같은 parent-child gate를 두거나, 지원되는 커널에서는 `clone3()`의 `CLONE_INTO_CGROUP`으로 생성 시점부터 대상 cgroup에 넣는다.

### Network Namespace

```c
#include <sys/socket.h>
#include <linux/if.h>

int setup_network(pid_t pid) {
    char cmd[256];

    // veth pair 생성
    if (system("ip link add veth0 type veth peer name veth1") != 0)
        return -1;

    // veth1을 컨테이너 netns로
    // ip link set ... netns 인자는 PID 또는 등록된 netns 이름을 받는다.
    // /proc/<pid>/ns/net 경로 자체를 직접 넘기는 형태는 표준 사용법이 아니다.
    snprintf(cmd, sizeof(cmd),
             "ip link set veth1 netns %d", pid);
    if (system(cmd) != 0)
        return -1;

    // 호스트 측 설정
    if (system("ip addr add 10.0.0.1/24 dev veth0") != 0)
        return -1;
    if (system("ip link set veth0 up") != 0)
        return -1;

    // 컨테이너 측 설정 (netns 내부에서)
    snprintf(cmd, sizeof(cmd),
             "nsenter -t %d -n ip addr add 10.0.0.2/24 dev veth1", pid);
    if (system(cmd) != 0)
        return -1;

    snprintf(cmd, sizeof(cmd),
             "nsenter -t %d -n ip link set veth1 up", pid);
    if (system(cmd) != 0)
        return -1;

    return 0;
}

int main(int argc, char *argv[]) {
    // ... (이전 코드)

    // Network namespace 추가
    int flags = CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWPID
              | CLONE_NEWIPC | CLONE_NEWNET;

    int gate[2];
    pipe2(gate, O_CLOEXEC);
    struct ChildArgs child_args = {.start_gate = gate[0]};
    pid_t pid = clone(child_main, child_stack + STACK_SIZE, flags | SIGCHLD, &child_args);

    if (setup_cgroup(pid) == -1 || setup_network(pid) == -1) {
        // 필수 격리/제한 설정 실패 → gate를 열지 않는다(fail-closed).
        close(gate[0]);
        close(gate[1]);
        waitpid(pid, NULL, 0);
        return 1;
    }

    // cgroup과 veth/netns 구성이 끝난 뒤에만 자식의 execve를 허용한다.
    write(gate[1], "x", 1);
    close(gate[0]);
    close(gate[1]);

    waitpid(pid, NULL, 0);

    return 0;
}
```

### OverlayFS 적용

```c
int setup_overlay(const char *lower, const char *container_id) {
    char upper[256], work[256], merged[256];
    char mount_opts[512];

    // 디렉토리 준비
    snprintf(upper, sizeof(upper), "/var/lib/container/%s/upper", container_id);
    snprintf(work, sizeof(work), "/var/lib/container/%s/work", container_id);
    snprintf(merged, sizeof(merged), "/var/lib/container/%s/merged", container_id);

    if (mkdir(upper, 0755) == -1)
        return -1;
    if (mkdir(work, 0755) == -1)
        return -1;
    if (mkdir(merged, 0755) == -1)
        return -1;

    // OverlayFS 마운트
    snprintf(mount_opts, sizeof(mount_opts),
             "lowerdir=%s,upperdir=%s,workdir=%s",
             lower, upper, work);

    if (mount("overlay", merged, "overlay", 0, mount_opts) == -1)
        return -1;

    // chroot 대신 merged 사용
    if (chroot(merged) == -1)
        return -1;

    return 0;
}

int child_func(void *arg) {
    if (setup_overlay("/var/lib/images/ubuntu", "container1") == -1)
        return -1;

    // ... (나머지 코드)
}
```


---

## 결론

컨테이너는 마법이 아니라 리눅스 커널의 Namespaces와 Cgroups를 조합한 것이다.

1. **Namespaces**: PID, Mount, Network, UTS, IPC, User, Cgroup, Time의 8가지 격리 메커니즘
2. **Cgroups**: CPU, Memory, I/O, PIDs 같은 리소스 제한
3. **OverlayFS**: Copy-on-Write로 이미지 계층을 효율적으로 저장
4. **OCI 표준**: 컨테이너 런타임의 공통 인터페이스
5. **보안 메커니즘**: Capabilities, Seccomp, User Namespaces, LSM 프로필
6. **최소 실험 구현**: namespace와 rootfs 격리의 핵심은 짧은 코드로 재현할 수 있지만, 안전한 runtime에는 user namespace, capability, seccomp, LSM, cgroup, mount propagation, PID 1 처리와 오류 복구가 추가로 필요

Docker의 핵심은 이 기술을 이미지·배포·네트워크·볼륨 관리와 함께 묶는 데 있다. Linux runtime의 밑바닥에는 namespace를 만드는 여러 syscall 경로, mount API, cgroup과 보안 모듈이 있으며 하나의 `clone` 호출로 환원되지는 않는다.

컨테이너를 이해하면 `nsenter`, `lsns`로 디버깅하고, cgroup 파라미터로 성능을 튜닝하며, 권한 상승 경로를 차단할 수 있다.

컨테이너는 별도 게스트 커널을 제공하는 하드웨어 가상 머신과 다르며, host kernel을 공유하는 OS 수준 격리·자원 관리 방식이다. 보안 경계의 강도는 namespace만이 아니라 capability, seccomp, LSM, user namespace와 host kernel 공격면을 함께 구성한 결과다.

## 더 읽을거리

- [Linux Kernel Documentation: Namespaces (개괄 인덱스, 추가 읽을거리)](https://www.kernel.org/doc/html/latest/admin-guide/namespaces/index.html) — 개별 계약은 namespaces(7)·unshare(2)·clone(2) 참조
- [Linux Kernel Documentation: Cgroups](https://www.kernel.org/doc/html/latest/admin-guide/cgroup-v2.html)
- [OCI Runtime Specification](https://github.com/opencontainers/runtime-spec)
- [Docker Documentation (홈페이지, 추가 읽을거리)](https://docs.docker.com/)
- Linux v5.15 커널 소스: `kernel/nsproxy.c`(`copy_namespaces`), `kernel/pid_namespace.c`, `kernel/cgroup/`, `fs/overlayfs/`, `net/core/net_namespace.c`
- Michael Kerrisk, *The Linux Programming Interface*, No Starch Press, 2010
- Liz Rice, *Container Security*, O'Reilly Media, 2020
- Marko Lukša, *Kubernetes in Action*, Manning, 2017
- ["Namespaces in operation"](https://lwn.net/Articles/531114/) — LWN.net 시리즈
- ["Understanding the Docker Internals"](https://medium.com/@BeNitinAgarwal/understanding-the-docker-internals-7ccb052ce9fe)
- ["runc: The Little Engine That Could"](https://www.youtube.com/watch?v=ZAhzoz2zJj8) — DockerCon talk
- [runc](https://github.com/opencontainers/runc), [crun](https://github.com/containers/crun), [youki](https://github.com/containers/youki), [Podman](https://podman.io/)
- [nsenter(1)](https://man7.org/linux/man-pages/man1/nsenter.1.html), [lsns(8)](https://man7.org/linux/man-pages/man8/lsns.8.html)
- [Linux Containers from Scratch](https://github.com/lizrice/containers-from-scratch) — Liz Rice
- [Bocker](https://github.com/p8952/bocker) — Docker implemented in 100 lines of bash
- [Contained.af](https://contained.af/) — Container escape training
