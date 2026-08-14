# 리눅스 기기 인수인계 — WSL2 에서 닫지 못한 검증

이 문서는 **step 문서가 아니다.** `articles/step01~08` 은 lab 을 만드는 과정이고, 이 문서는
**이미 만들어진 lab 을 다른 리눅스 기기에서 한 번 더 돌리기 위한 실행 지시서** 다.
이것 하나만 보고 그대로 따라 할 수 있게 적었다.

- 작성: 2026-08-15
- 작성 환경: Ubuntu 26.04 / WSL2 / 커널 `6.18.33.2-microsoft-standard-WSL2` / 16 논리 코어 /
  g++ 15.2.0 / cmake 4.2.3 / liburing 2.14 / **`sudo` 사용 불가**
- 작성 환경의 현재 상태: `ctest` **8/8 통과**, 문서 검사 0 error

## 0. 이 문서가 시키는 일

WSL2 에서 닫지 못한 것이 8건 남았다. 성격이 셋으로 갈린다 — 권한이 없어서(1~4),
구조·커널·하드웨어가 막아서(5~7), 환경이 달라서(8).

| # | 항목 | 성격 | 리눅스 기기에서 |
|---|---|---|---|
| 1 | cgroup `cpu.weight` 쓰기 성공 + 복구 | 권한 | **닫을 수 있다** |
| 2 | `SCHED_FIFO` 적용 성공 | 권한 | **닫을 수 있다** |
| 3 | `--sched=fifo` / `--sched=cgroup` 성공 경로 측정 | 권한 | **닫을 수 있다** |
| 4 | `SCHED_FIFO` + 부하에서 ctx-sw 감소 가설 | 권한 + 부하 | **닫을 수 있다** |
| 5 | aio/uring 이 FIFO 에서 부분 읽기 루프를 도는 경로 | 구조 | **닫을 수 없다 — 파지 마라** |
| 6 | pread·mmap 의 FIFO EINTR | 커널이 금지 | **닫을 수 없다 — 파지 마라** |
| 7 | io_uring 취소가 진행 중 요청을 `-ECANCELED` 로 끊는 경로 | 하드웨어 | **선택 — 규약은 이미 닫혔다** |
| 8 | 베어메탈 대조 수치 | 측정 | **해야 한다** |

1~4 는 §3, 5~7 은 §4, 8 은 §5 에 있다. **5·6·7 은 "안 된다"는 결론까지 이미 났다.**
다시 파지 않도록 이유를 §4 에 적어 뒀다.

> **§6 을 먼저 읽어라.** 이 문서를 쓰면서 실측으로 **인수인계 전제 두 가지가 틀렸음** 을
> 확인했다. 그 둘을 모르면 §3 의 판정을 잘못 읽는다.

---

## 1. 환경 준비

### 1.1 패키지

Ubuntu/Debian 계열. 저장소에 `scripts/install_deps.sh` 가 있고 내용은 아래와 같다.

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake git pkg-config \
    libsdl2-dev libgl1-mesa-dev liburing-dev

# 측정 도구 (§3-4, §5 에서 쓴다)
sudo apt-get install -y strace linux-tools-generic trace-cmd stress-ng
```

`stress-ng` 는 `install_deps.sh` 에 없다 — **§3-4 를 하려면 따로 깔아야 한다.**
`util-linux` 의 `chrt`, `libcap2-bin` 의 `setcap`/`getcap` 도 §3 에서 쓴다(보통 기본 설치).

작성 환경에 실제로 깔려 있던 버전:

```text
build-essential 12.12ubuntu2.26.04.2   cmake 4.2.3-2ubuntu2      git 1:2.53.0-1ubuntu1
libgl1-mesa-dev 26.0.3-1ubuntu1        libsdl2-dev 2.32.10       liburing-dev 2.14-1
pkg-config 2.5.1-4
```

### 1.2 버전 하한

| 대상 | 하한 | 근거 |
|---|---|---|
| CMake | **3.20** | `CMakeLists.txt:1` 의 `cmake_minimum_required`. `ctest --test-dir` 도 3.20+ |
| C++ | **C++20** | `CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_EXTENSIONS OFF` |
| liburing | **`io_uring_prep_cancel64` 가 있는 버전** | `uring_backend.cpp` 이 취소 경로에서 직접 부른다 |
| 커널 | **5.15+ 권장** | `README.md:90`. `IORING_OP_READ`·`IORING_OP_ASYNC_CANCEL` 이 필요 |

liburing 은 버전 번호로 판정하지 말고 **헤더가 실제로 무엇을 주는지** 로 판정하라.
이 lab 이 부르는 liburing API 는 `io_uring_queue_init` / `io_uring_queue_exit` /
`io_uring_get_sqe` / `io_uring_prep_read` / `io_uring_sqe_set_data64` / `io_uring_submit` /
`io_uring_wait_cqe` / `io_uring_peek_cqe` / `io_uring_cqe_get_data64` / `io_uring_cqe_seen` /
**`io_uring_prep_cancel64`** 다. 마지막 것이 가장 늦게 들어온 API 라 이것만 보면 된다.

```bash
printf '#include <liburing.h>\nint main(){struct io_uring_sqe s;io_uring_prep_cancel64(&s,1,0);io_uring_sqe_set_data64(&s,1);return 0;}\n' > /tmp/c.c \
  && cc /tmp/c.c -luring -o /tmp/c && echo 'liburing OK'
```

기대 출력: `liburing OK` (작성 환경에서 실행 확인).
컴파일이 실패하면 liburing 을 올려야 한다. **liburing 이 아예 없어도 빌드는 된다** —
CMake 의 `pkg_check_modules(LIBURING IMPORTED_TARGET liburing)` 이 감지에 실패하면
`SKL_HAS_LIBURING` 을 정의하지 않아 io_uring backend 가 통째로 빠지고, configure 가
`-- io_uring: disabled (liburing not found)` 를 찍는다. 그 상태에서는 §2.4 의 strict
케이스가 실패하는 것이 정상 동작이다(`SKL_REQUIRE_BACKENDS` 가 요구하는데 컴파일조차
안 됐으므로). POSIX AIO 도 같은 구조다(`SKL_POSIX_AIO_AVAILABLE`).

### 1.3 소스와 빌드 디렉터리 위치

네이티브 리눅스 기기라면 아무 데나 둬도 된다. 아래는 **WSL2 에서 다시 돌릴 때만** 해당한다
(작성 환경이 그랬다).

- 소스는 `/mnt/c` 에 둬도 된다. 빌드 디렉터리는 리눅스 쪽(`~/build-skl`)에 두는 편이
  훨씬 빠르다 — `/mnt/c` 는 9p 를 거친다.
- **`--fixture-dir` 을 `/mnt/c` 아래로 주지 마라.** 9p 는 `mkfifo` 를 지원하지 않고,
  I/O 경로가 한 겹 더 끼어 측정이 무의미해진다.
- 테스트가 만드는 FIFO 와 픽스처는 소스·빌드 위치와 무관하게
  `std::filesystem::temp_directory_path()`(= `/tmp`)에 생긴다. 그래서 소스가 `/mnt/c` 에
  있어도 `system_io_path_tests` 는 정상 동작한다.

### 1.4 configure 가 네트워크를 쓴다

`CMakeLists.txt` 의 `FetchContent` 가 **configure 시점에** Dear ImGui v1.90.4 를
`github.com/ocornut/imgui` 에서 clone 한다. GUI 타깃만 쓰는 의존인데도 configure 단계에서
무조건 받으므로, 오프라인 기기에서는 configure 자체가 실패한다.

오프라인이면 ImGui 소스를 미리 두고 경로를 넘겨라.

```bash
cmake -S <소스> -B ~/build-skl -DCMAKE_BUILD_TYPE=Release \
      -DFETCHCONTENT_SOURCE_DIR_IMGUI=/path/to/imgui-1.90.4
```

작성 환경에서 이 우회로 configure 가 끝까지 도는 것을 확인했다.

---

## 2. 빌드와 실행

### 2.1 configure · build

```bash
SRC=/path/to/projects/mini/system_kernel_lab

cmake -S "$SRC" -B ~/build-skl -DCMAKE_BUILD_TYPE=Release
cmake --build ~/build-skl -j
```

configure 마지막에 다음 두 줄이 나오는지 본다. **여기서 `disabled` 가 뜨면 §2.4 의
strict 케이스가 실패하는 것이 정상이다** — 빌드에 그 backend 가 아예 없다는 뜻이다.

```text
-- POSIX AIO: enabled
-- io_uring: enabled (liburing 2.14)
```

산출물 5개:

| 바이너리 | 무엇 |
|---|---|
| `system_kernel_lab` | SDL2+ImGui GUI 도구. 유일하게 SDL2/OpenGL/ImGui 를 링크한다 |
| `system_core_tests` | 성공 경로 — backend 동등성 · sync 상호배제 · 전수 조합 · 작업 큐 인계 회귀 |
| `system_fault_tests` | 오류 경로 — `src/fault` seam 주입 |
| `system_io_path_tests` | 부분 읽기 계약 · 실제 시그널 EINTR · 취소 후 배수 |
| `system_kernel_bench` | 헤드리스 측정 러너 (조합 → 4채널 CSV) |

### 2.2 ctest

```bash
ctest --test-dir ~/build-skl --output-on-failure
```

기대 출력 (작성 환경 실측, 총 2.33 초):

```text
1/8 Test #1: system_core_tests ........................   Passed    0.90 sec
2/8 Test #2: system_fault_tests .......................   Passed    0.03 sec
3/8 Test #3: system_io_path_tests .....................   Passed    0.65 sec
4/8 Test #4: system_core_tests_strict .................   Passed    0.73 sec
5/8 Test #5: system_kernel_bench_smoke ................   Passed    0.00 sec
6/8 Test #6: system_kernel_bench_smoke_workers ........   Passed    0.00 sec
7/8 Test #7: system_kernel_bench_fault_channel ........   Passed    0.00 sec
8/8 Test #8: system_kernel_bench_rejects_bad_toggle ...   Passed    0.00 sec

100% tests passed, 0 tests failed out of 8
```

`system_kernel_bench_rejects_bad_toggle` 은 `WILL_FAIL TRUE` 다 — **실패해야 통과** 하는
케이스이므로 "8/8 Passed" 안에 들어 있는 것이 맞다.

### 2.3 개별 실행과 요약 라인

ctest 는 통과한 테스트의 stdout 을 감춘다. 이 lab 은 **감춰지면 안 되는 사실을 stdout
요약 라인으로 찍으므로**, 인수인계 판정은 바이너리를 직접 돌려서 하라.

```bash
cd ~/build-skl
./system_core_tests | grep -E '^\[backends\]|^\[strict\]|^\[handoff\]'
```

기대 출력 (작성 환경 실측):

```text
[backends] read=read pread=pread mmap=mmap aio=aio io_uring=io_uring
[strict] SKL_REQUIRE_BACKENDS=<unset> (fallback tolerated)
[handoff] 4 sync types x 8000 cycles with 8 workers; remaining_ underflow observations=0
[handoff] deterministic straggler rendezvous reproduced
```

`[backends]` 의 `요청=실제` 가 어긋나면 그 backend 는 런타임에 폴백한 것이다
(예: `io_uring=read`). §2.4 가 그 폴백을 실패로 바꾼다.

### 2.4 strict 라벨

```bash
ctest --test-dir ~/build-skl -L strict     # io_uring·aio 폴백을 실패로 (요구 모드)
ctest --test-dir ~/build-skl -LE strict    # 폴백 허용 (이식성 모드)
```

`-L strict` 는 `system_core_tests_strict` 한 케이스만 돌리고, 그 케이스는 CMake 가
`ENVIRONMENT "SKL_REQUIRE_BACKENDS=uring,aio"` 를 걸어 준다. 작성 환경 기대 출력:

```text
100% tests passed, 0 tests failed out of 1
```

### 2.5 환경변수

**코드에서 직접 확인한 것만 적었다.** 이 lab 이 읽는 환경변수는 전부 아래 넷이다
(`grep -rn getenv src tests tools` 결과와 일치).

| 변수 | 읽는 곳 | 문법 | 기본 | 하는 일 |
|---|---|---|---|---|
| `SKL_FAULT` | `src/fault/fault_inject.cpp:174` (프로세스 시작 시 1회) | `op[:skip[:ERRNO[:times]]]` 를 `,` 로 나열 | 없음 | syscall seam 에 실패를 주입한다 |
| `SKL_REQUIRE_BACKENDS` | `tests/system_core_tests.cpp:103` | backend 이름을 `,` 로 나열 | 미설정 = 폴백 허용 | 목록에 있는 backend 가 폴백하면 테스트 실패 |
| `SKL_CGROUP_DIR` | `src/sched/sched_policy.cpp:64` | cgroup 절대 경로 하나 | 미설정 = 거부 | 이 경로가 현재 cgroup 과 **정확히 일치할 때만** `cpu.weight` 쓰기 허용 |
| `SKL_STRESS_CYCLES` | `tests/system_core_tests.cpp:423` | 정수 | 8000 | 큐 인계 스트레스 사이클 수. **바닥이 8000이라 줄일 수 없다** (`std::max`) |

#### `SKL_FAULT` 문법

`op[:skip[:ERRNO[:times]]]`. `skip` 번째 호출까지는 통과시키고, 그 뒤부터 `times` 회
(0 = 무제한) 해당 errno 로 실패시킨다. 파싱에 실패하면 **기존 설정을 포함해 전부 해제** 된다.

사용 가능한 `op` (`src/fault/fault_inject.h:42-47` 이 목록의 원본):

```text
open  read  pread  fstat  mmap  munmap
aio_read  aio_suspend  aio_cancel  aio_return
io_uring_queue_init  io_uring_get_sqe  io_uring_submit  io_uring_submit_short
io_uring_wait_cqe  uring_user_data
setpriority  pthread_setschedparam  cgroup_read  cgroup_write
```

`ERRNO` 는 이름(`EACCES EAGAIN EBADF ECANCELED EFAULT EINTR EINVAL EIO EMFILE ENFILE
ENOENT ENOMEM ENOSPC ENOSYS EOVERFLOW EPERM`) 또는 1~4095 정수. 기본 `EIO`.

동작 확인 (작성 환경 실측):

```bash
cd ~/build-skl
SKL_FAULT=io_uring_queue_init:0:ENOSYS:1 ./system_kernel_bench \
    --backend=io_uring --strict-backend --cycles=1 --warmup=0 \
    --files=2 --bytes=4096 --out=/dev/null --fixture-dir=/tmp/skl_f
echo "exit=$?"
```

```text
skl: fault injection armed: io_uring_queue_init:0:ENOSYS:1
skl: io_uring runtime init failed (io_uring_queue_init failed: Function not implemented), falling back to read
system_kernel_bench: backend 'io_uring' unavailable at runtime, running 'read' instead
exit=1
```

`skip` 을 아주 크게 준 규칙(`read:100000000`)은 **아무것도 주입하지 않고 호출 횟수만 세는
순수 관측 모드** 다. 테스트가 "그 분기가 정말 실행됐는가" 를 셀 때 쓴다.

### 2.6 벤치 러너 인자

`./system_kernel_bench --help` 가 원본이다. 요약:

| 인자 | 값 | 기본 |
|---|---|---|
| `--backend` | `read` `pread` `mmap` `aio` `io_uring`(= `uring`) | `read` |
| `--sync` | `mutex` `spin`(= `spinlock`) `futex` `cas` | `mutex` |
| `--sched` | `default` `nice` `fifo` `cgroup` | `default` |
| `--ctx` | `pthread` `coroutine`(= `coro`) | `pthread` |
| `--workers` `--cycles` `--warmup` `--files` `--bytes` | 정수 | `1` `30` `2` `16` `1048576` |
| `--fixture-dir` | 픽스처 경로 | `/tmp/system_kernel_bench_data` |
| `--out` | CSV 경로, `-` 면 stdout(요약은 stderr) | `run.csv` |
| `--strict-backend` | 런타임 폴백을 실패(exit 1)로 | 꺼짐 |
| `--min-faults=N` | 사이클당 평균 page fault 가 N 미만이면 실패 | `0` |

이름은 `[a-z0-9]` 만 남기고 정규화한 뒤 별칭표와 맞춘다 — `io_uring` `IO_URING` `uring` 이
모두 같다. **모르는 이름은 거부하고 exit 2** 다.

CSV 열:

```text
backend_req,backend,sync,sched,sched_applied,ctx,workers,files,file_bytes,cycle,cycle_ms,mb_per_s,ctx_sw,page_faults
```

`backend_req` 와 `backend` 가 다르면 런타임 폴백이 있었다는 뜻이다.
**`sched_applied` 열은 그대로 믿으면 안 된다 — §6.2 를 반드시 읽어라.**

---

## 3. 권한이 필요한 항목 (1~4)

### 3-1. cgroup v2 `cpu.weight` 쓰기 성공 + 복구

**확인하려는 것**

`sched_policy.cpp` 의 두 경로가 실제로 도는가.

- `ApplyCgroupCpuWeight(200)` 의 성공 분기 — `WriteCgroupFile` 이 참을 돌려주고
  `CgroupState().modified = true` 가 되는 곳 (`sched_policy.cpp:164-171`).
- `RestoreCgroupCpuWeightIfNeeded()` 의 `modified == true` 분기 — 원래 값을 되돌려
  쓰는 곳 (`sched_policy.cpp:174-191`).

WSL2 에서 못 한 이유는 `cpu.weight` 를 **쓸 권한이 없어서** 다(파일 자체는 있다 — §6.1).

**`SKL_CGROUP_DIR` 이 기대하는 값**

코드(`sched_policy.cpp:51-76`)가 하는 일은 정확히 이렇다.

1. `/proc/self/cgroup` 을 읽어 `0::` 로 시작하는 줄을 찾는다.
2. 그 줄의 `0::` 뒤 문자열이 `rel` 이면, 검출 경로는 `/sys/fs/cgroup` + `rel` 이다
   (`rel` 이 비었거나 `/` 면 `/sys/fs/cgroup`).
3. `getenv("SKL_CGROUP_DIR")` 가 **그 검출 경로와 문자열이 정확히 같을 때만** 쓰기를 허용한다.
   다르거나 미설정이면 stderr 에 거부 메시지를 찍고 포기한다.

즉 `SKL_CGROUP_DIR` 은 "이 프로세스가 지금 들어 있는 cgroup 의 절대 경로" 다. 다른
cgroup 을 가리키는 용도가 아니다. 공유 session cgroup 을 실수로 바꾸지 않기 위한 opt-in 이다.

**위임 cgroup 만드는 절차**

```bash
# (1) 부모에 cpu 컨트롤러가 켜져 있는지 확인. 없으면 켠다.
cat /sys/fs/cgroup/cgroup.subtree_control          # 여기에 'cpu' 가 있어야 한다
echo +cpu | sudo tee /sys/fs/cgroup/cgroup.subtree_control

# (2) 일회용 벤치 cgroup 생성 + 소유권 위임
sudo mkdir -p /sys/fs/cgroup/skl-bench
sudo chown -R "$USER" /sys/fs/cgroup/skl-bench

# (3) 현재 셸을 그 cgroup 으로 옮긴다
echo $$ | sudo tee /sys/fs/cgroup/skl-bench/cgroup.procs

# (4) 검출 경로가 기대대로인지 확인 — 이 값이 그대로 SKL_CGROUP_DIR 이 된다
cat /proc/self/cgroup                              # → 0::/skl-bench
cat /sys/fs/cgroup/skl-bench/cpu.weight            # 원래 값. 보통 100
export SKL_CGROUP_DIR=/sys/fs/cgroup/skl-bench
```

`(4)` 의 `0::/skl-bench` 앞에 `/sys/fs/cgroup` 을 붙인 것이 `SKL_CGROUP_DIR` 이다.
**`/proc/self/cgroup` 출력이 `0::/user.slice/...` 처럼 나오면 그 전체 경로를 써야 한다.**

**명령**

```bash
cd ~/build-skl

# 원래 값을 적어 둔다
ORIG=$(cat "$SKL_CGROUP_DIR/cpu.weight"); echo "orig=$ORIG"

# 수십 초 도는 실행을 배경으로 띄우고, 도는 중에 한 번 읽고, 정상 종료를 기다린다.
# 사이클 수는 기기 속도에 맞춰 조정한다 — 최소 10초는 돌아야 한다.
./system_kernel_bench --sched=cgroup --workers=4 --cycles=20000 --warmup=0 \
    --files=8 --bytes=1048576 --out=/tmp/cg.csv --fixture-dir=/tmp/skl_cg &
BP=$!
sleep 3
echo "during=$(cat "$SKL_CGROUP_DIR/cpu.weight")"
wait $BP; echo "exit=$?"
echo "after=$(cat "$SKL_CGROUP_DIR/cpu.weight")"
head -2 /tmp/cg.csv
```

**중간에 죽이지 마라.** 복구는 `Shutdown()` 경로에서만 일어나므로 `kill` 로 끊으면
`cpu.weight` 가 200 인 채 남는다. 그 상태에서 다음 실행을 하면 **그 200 을 "원래 값" 으로
캡처해** `after` 도 200 이 되어, 복구가 깨진 것처럼 보인다. 그렇게 됐다면 손으로
`echo $ORIG > "$SKL_CGROUP_DIR/cpu.weight"` 로 되돌린 뒤 다시 시작한다.

**무엇이 나오면 통과인가**

| 확인 | 통과 | 실패 |
|---|---|---|
| stderr | cgroup 관련 메시지가 **하나도 없다** | `skl: writing .../cpu.weight failed: Permission denied` → 위임이 안 됐다 |
| stderr | — | `skl: refusing to modify the current cgroup. Set SKL_CGROUP_DIR exactly to '...'` → 경로 불일치. 메시지가 알려 주는 경로를 그대로 쓰면 된다 |
| `during=` | **200** | `orig` 그대로면 쓰기가 안 된 것 |
| `after=` | **`$ORIG` 와 같음** | 200 이면 복구 분기가 안 돌았다 |
| CSV 5번째 열 | `1` | `0` 이면 `ApplySchedProcessSetup` 이 false 를 돌려줬다 |

복구 분기는 `FileIoWorkload::Shutdown()` → `ApplySchedProcessSetup(SchedType::Default)`
경로로만 도달한다(`file_io_workload.cpp:347-355`). 그래서 **정상 종료한 실행** 이어야
`after` 가 의미가 있다. `kill -9` 로 죽인 실행은 복구되지 않는다(설계상 그렇고,
README 도 그렇게 적어 두었다).

**끝나면 치울 것**

```bash
echo $$ | sudo tee /sys/fs/cgroup/cgroup.procs      # 셸을 루트 cgroup 으로 되돌리고
sudo rmdir /sys/fs/cgroup/skl-bench
```

**주의.** `system_fault_tests` 로는 이 항목을 확인할 수 없다. 그 테스트는 `cpu.weight` 가
쓰기 가능하면 "실제 cgroup 을 바꿀 위험" 때문에 **통째로 건너뛴다**
(`system_fault_tests.cpp:550-555`). 위임 cgroup 안에서 돌리면 다음 줄이 나오고 그게 정상이다.

```text
  /sys/fs/cgroup/skl-bench/cpu.weight is writable — skipping (would mutate a real cgroup)
```

---

### 3-2. `SCHED_FIFO` 적용 성공

**확인하려는 것**

`ApplyFifo(50)`(`sched_policy.cpp:91-104`)의 성공 분기. WSL2 에서는 `ulimit -r` 이 0 이고
`CAP_SYS_NICE` 도 없어 항상 `EPERM` 으로 떨어졌다 — 실패 폴백만 검증됐다.

**권한 주기 (셋 중 하나)**

```bash
# 옵션 A: 바이너리에 capability 부여 (가장 깔끔. 일반 셸에서 그대로 실행 가능)
sudo setcap cap_sys_nice+ep ~/build-skl/system_fault_tests
sudo setcap cap_sys_nice+ep ~/build-skl/system_kernel_bench
getcap ~/build-skl/system_kernel_bench        # cap_sys_nice=ep 가 보이면 된다

# 옵션 B: RLIMIT_RTPRIO 상향 (/etc/security/limits.conf, 재로그인 필요)
#   @users  -  rtprio  50
ulimit -r                                      # 50 이상이어야 한다

# 옵션 C: sudo 실행 (개발용)
sudo ~/build-skl/system_fault_tests
```

**우선순위 50 이 필요하다.** `ApplyFifo` 는 `sched_priority = 50` 으로 고정이므로
`ulimit -r` 이 49 이하면 여전히 실패한다.

**명령과 기대 출력 (a) — 테스트 쪽**

```bash
~/build-skl/system_fault_tests | grep 'SCHED_FIFO applied'
```

| 출력 | 뜻 |
|---|---|
| `  SCHED_FIFO applied without injection: yes (privileged)` | **통과.** 성공 경로가 돌았다 |
| `  SCHED_FIFO applied without injection: no (expected without CAP_SYS_NICE)` | 권한이 안 붙었다. 작성 환경이 항상 이 값이었다 |

이 줄 바로 위의 주입 케이스(`pthread_setschedparam:0:EPERM` → false 반환)는 권한이
있어도 그대로 통과해야 한다. **성공 경로를 얻는 대신 실패 폴백 검증을 잃으면 안 된다** —
둘 다 초록이어야 정상이다.

**명령과 기대 출력 (b) — 실제 스레드 정책 확인**

`sched_applied` 열은 이 항목의 증거로 쓸 수 없다(§6.2). 스레드 정책을 직접 본다.

```bash
cd ~/build-skl
./system_kernel_bench --sched=fifo --workers=4 --cycles=8000 --warmup=0 \
    --files=4 --bytes=65536 --out=/dev/null --fixture-dir=/tmp/skl_s4 >/dev/null 2>/dev/null &
BP=$!
sleep 1
for t in /proc/$BP/task/*; do
    tid=$(basename "$t"); echo "tid=$tid  $(chrt -p "$tid" 2>&1 | head -1)"
done
wait $BP
```

`--cycles` 는 **프로세스가 최소 3초는 살아 있도록** 조정한다(작성 환경에서 8000사이클 ≈ 1.1초).
너무 짧으면 `sleep 1` 뒤에 이미 끝나 있어 `/proc/$BP/task/*` 가 비고 glob 이 그대로 남는다.

작성 환경(권한 없음) 실측 = **음성 대조군**:

```text
tid=141018  pid 141018's current scheduling policy: SCHED_OTHER
tid=141020  pid 141020's current scheduling policy: SCHED_OTHER
tid=141021  pid 141021's current scheduling policy: SCHED_OTHER
tid=141022  pid 141022's current scheduling policy: SCHED_OTHER
tid=141023  pid 141023's current scheduling policy: SCHED_OTHER
```

**통과 판정**: 5개 중 **워커 4개가 `SCHED_FIFO`**, 첫 줄(메인 스레드)만 `SCHED_OTHER`.
그리고 stderr 에 `skl: pthread_setschedparam(SCHED_FIFO, prio=50) failed: ...` 가
**한 줄도 없어야** 한다(권한이 없으면 워커 수만큼, 즉 4줄이 나온다).

`--workers=1` 로는 이 항목을 확인할 수 없다. 단일 모드는 워커 스레드를 만들지 않아
스레드 정책을 아예 적용하지 않는다(`file_io_workload.cpp:73-78`).

---

### 3-3. `--sched=fifo` / `--sched=cgroup` 성공 경로의 측정

**확인하려는 것**

3-1·3-2 로 권한이 붙은 상태에서 **벤치 러너가 그 조합의 수치를 실제로 남기는가.**
작성 환경에서는 권한이 없어 `sched=fifo` 행이 전부 "정책이 적용되지 않은 상태의 수치" 였다.

**명령**

```bash
cd ~/build-skl
rm -rf /tmp/skl_fx

for combo in "--sched=default" "--sched=nice" "--sched=fifo" "--sched=cgroup"; do
    ./system_kernel_bench $combo --backend=read --workers=4 \
        --warmup=20 --cycles=200 --fixture-dir=/tmp/skl_fx --out=-
done > /tmp/sched.csv 2>/tmp/sched.err

awk 'NR == 1 || $0 !~ /^backend_req/' /tmp/sched.csv > /tmp/sched_all.csv
grep -c 'failed' /tmp/sched.err          # 0 이어야 한다
```

`--sched=cgroup` 행은 `SKL_CGROUP_DIR` 이 export 된 셸에서 돌려야 의미가 있다.

**무엇이 나오면 통과인가**

- `grep -c 'failed' /tmp/sched.err` → **0**. 권한 실패 메시지가 하나도 없다.
- `sched=cgroup` 행의 `sched_applied` = **1** (cgroup 은 프로세스 단위 정책이라
  §6.2 의 경쟁 조건에 걸리지 않는다 — 이 열을 믿어도 된다).
- `sched=fifo` 행은 `sched_applied` 대신 **§3-2 (b) 의 `chrt` 확인** 으로 판정한다.
- 네 정책의 `cycle_ms` 가 **부하 없이는 거의 같게 나온다.** 그게 옳은 결과다 —
  경쟁 부하가 없으면 스케줄링 정책이 할 일이 없다. 차이를 보려면 §3-4 로 간다.

---

### 3-4. `SCHED_FIFO` + 경쟁 부하에서 ctx-sw 감소 가설

**확인하려는 것**

`articles/step08_perf_visualization.md` 의 시나리오 E(§6, 현재 326~350행)가 적은 예측:
부하 상태에서 `default` 는 involuntary ctx-sw 가 늘고, `SCHED_FIFO` 는 ctx-sw 가 *줄어든다*.
같은 문서 앞부분 예측 표(현재 54~61행)의 `sched: default → SCHED_FIFO + 부하` 행도 같은 주장이다.

작성 환경에서는 **권한과 경쟁 부하 둘 다** 없어서 확인할 수 없었고, 그래서 step08 의 해당
문장에는 조건이 붙어 있다("권한과 코어 상황이 맞으면"). 이 항목이 그 조건을 걷어낼 수 있는지,
아니면 예측이 이 워크로드에서 틀렸는지를 정한다.

**명령**

```bash
cd ~/build-skl
rm -rf /tmp/skl_load

run () {   # $1 = sched
    ./system_kernel_bench --sched="$1" --backend=read --workers=4 \
        --warmup=20 --cycles=200 --fixture-dir=/tmp/skl_load --out=- 2>/dev/null
}

# (1) 부하 없음 — 기준선
run default > /tmp/nl_default.csv
run fifo    > /tmp/nl_fifo.csv

# (2) 부하 있음
stress-ng --cpu "$(nproc)" --timeout 120s &
SP=$!
sleep 2
run default > /tmp/ld_default.csv
run nice    > /tmp/ld_nice.csv
run fifo    > /tmp/ld_fifo.csv
kill $SP 2>/dev/null; wait $SP 2>/dev/null

for f in nl_default nl_fifo ld_default ld_nice ld_fifo; do
    printf '%-12s ' "$f"
    awk -F, 'NR>1 {ms+=$11; sw+=$13; n++} END {printf "cycle_ms=%.3f  ctx_sw=%.1f\n", ms/n, sw/n}' /tmp/$f.csv
done
```

**무엇이 나오면 통과인가**

"통과/실패" 가 아니라 **어느 쪽이든 기록해야 하는** 항목이다. 판정 기준:

| 관측 | 뜻 | 문서 처리 |
|---|---|---|
| `ld_default` 의 `ctx_sw` 가 `nl_default` 보다 뚜렷이 크고, `ld_fifo` 의 `ctx_sw` 가 `ld_default` 보다 작다 | 예측이 맞다 | step08 시나리오 E 와 예측 표의 조건절을 실측으로 대체 |
| `ld_fifo` 의 `ctx_sw` 가 `ld_default` 와 같거나 크다 | 예측이 이 워크로드에서 틀렸다 | 그 사실을 그대로 적는다. §5 의 "가설 깨짐을 받아들임" 이 이 lab 의 서술 원칙이다 |
| `nl_*` 두 줄의 `ctx_sw` 가 이미 0 근처 | 부하 주입이 실패했거나 워커가 블록되지 않는다 | 부하를 먼저 고친다. 작성 환경에서 `read, w4` 의 ctx-sw 는 사이클당 0.3 이었다 |

`SCHED_FIFO` 워커 4개 + `stress-ng --cpu $(nproc)` 는 **시스템을 굶길 수 있다.**
`--timeout` 을 반드시 걸고, 원격 접속 중이라면 별도 셸을 하나 열어 두고 시작하라.
이 lab 의 워커는 `SCHED_FIFO` 일 때 idle 루프에서 50µs sleep 하도록 되어 있지만
(`file_io_workload.cpp:231-234` — 다른 정책에서는 `yield()`),
그것만으로 안전이 보장되지는 않는다.

---

## 4. 커널·하드웨어 제약 (5~7)

**이 셋은 리눅스 기기에서도 안 될 수 있다.** 왜 그런지와, 그럼에도 검증 대상 규약이
닫혔는지를 항목마다 분명히 적었다.

### 4-5. aio/uring 이 FIFO 에서 부분 읽기 루프를 도는 경로 — **닫을 수 없다. 파지 마라.**

**왜 안 되는가.** 구조다.

`read` backend 만 `while (pos < fileBytes)` 루프를 돌며 요청한 만큼 채운다
(`read_backend.cpp:30-45`). aio 와 io_uring 은 **파일당 요청을 하나만** 만든다
(`aio_backend.cpp` 의 `aio_read` 1회, `uring_backend.cpp` 의 `io_uring_prep_read` 1회).
FIFO 는 한 번의 읽기가 파이프에 들어 있는 만큼만 돌려주므로 그 1회가 short 로 끝나고,
**루프가 없으니 두 번째 읽기가 아예 없다.** 작성 환경 실측: 16청크를 write 하는 동안
4096 바이트만 반환하고 끝났다.

권한을 올리든 하드웨어를 바꾸든 이 사실은 바뀌지 않는다. 바뀌려면 aio/uring backend 를
"미달이면 재요청" 구조로 고쳐야 하는데, 그건 검증이 아니라 **제품 코드 변경** 이다.

**규약은 닫혔는가 — 닫혔다(다른 방식으로).**

- 부분 읽기 계약 자체(`io_backend.h` 의 short read 계약: path 당 `onRead` 정확히 1회,
  실제 읽은 크기, 짧은 파일은 EOF 이지 오류가 아님)는 `TestShortReadContract` 가
  **정규 파일** 로 5개 backend 전수 검증한다 — 딱 맞는 파일·요청의 절반·4097바이트·
  1바이트·빈 파일·요청보다 큰 파일을 한 사이클에 섞어 같은 `(크기, 체크섬)` 목록을 내는지 본다.
- aio/uring 의 EINTR 은 **부분 읽기 루프가 아니라 대기 지점** 에서 검증했다 —
  `aio_suspend` 와 `io_uring_wait_cqe` (`TestFifoBlockingWaitEintr`).

즉 "FIFO 위에서 aio/uring 이 부분 읽기 루프를 돈다" 는 **애초에 이 코드의 성질이 아니다.**
검증 공백이 아니라 존재하지 않는 경로다.

### 4-6. pread·mmap 의 FIFO EINTR — **닫을 수 없다. 커널이 막는다.**

**왜 안 되는가.** 커널이 거부한다. 작성 환경에서 FIFO 하나를 열어 두 syscall 을 직접 불러 확인:

```text
pread  -> -1 errno=29 (Illegal seek)      ← ESPIPE
mmap   -> MAP_FAILED errno=19 (No such device)   ← ENODEV
```

FIFO 는 seek 불가라 offset 인자를 받는 읽기가 성립하지 않고, 정규 파일이 아닌 것은
매핑할 수 없다. 이건 WSL2 의 제약이 아니라 POSIX·리눅스의 정의다. 네이티브 기기에서도 같다.
확인하고 싶으면 `mkfifo` → `open(O_RDONLY|O_NONBLOCK)` → `pread`/`mmap` 20줄이면 된다.

**규약은 닫혔는가 — 닫혔다.**

두 backend 가 FIFO 를 **조용히 0 바이트로 돌려주지 않고 명시적으로 거부한다** 는 것을
`TestFifoRejectedByNonStreamBackends` 가 못 박았다(`system_io_path_tests.cpp:607-622`).

- `pread` → throw, 메시지에 `"pread failed"` 포함.
- `mmap` → throw, 메시지에 `"requires a regular file"` 포함.
  (`mmap_backend.cpp` 이 `fstat` 뒤 `S_ISREG` 를 확인해 던진다.)

"EINTR 을 검증하지 못했다" 가 아니라 **"이 두 backend 는 스트림을 읽지 않는다"가 검증됐다.**
EINTR 검증은 `read`·`aio`·`io_uring` 세 backend 로 닫혔다.

### 4-7. io_uring 취소가 `-ECANCELED` 를 밟는 경로 — **선택. 규약은 이미 닫혔다.**

**왜 안 밟히는가.** 하드웨어·캐시 조건이다.

픽스처가 정규 파일이고 페이지 캐시 hit 이라, `IORING_OP_ASYNC_CANCEL` 이 도착하기 전에
원 read 가 이미 끝나 있다. 그래서 취소 요청은 `-ENOENT`(그런 요청 없음) 또는
`-EALREADY`(이미 진행 완료) 로 돌아오고, 원 요청은 정상 결과로 완료된다.
원 요청이 `-ECANCELED` 로 끝나려면 취소가 도착하는 순간까지 **요청이 정말로 in-flight** 여야 한다.

**밟으려면 무엇이 필요한가.**

- 느린 블록 디바이스(회전 디스크, 네트워크 블록 디바이스, `dm-delay` 로 지연을 넣은 장치), 또는
- `O_DIRECT` 로 페이지 캐시를 우회해 실제 I/O 를 강제하는 것.

둘 다 이 lab 에는 없다. `uring_backend.cpp` 은 `SklOpen(path, O_RDONLY)` 로 열고
`O_DIRECT` 토글이 없다. 넣으려면 정렬 요구(버퍼·오프셋·길이)까지 따라와서 backend 를
고쳐야 한다 — **제품 코드 변경이다.**

**규약은 닫혔는가 — 닫혔다. 이 항목은 다시 파지 마라.**

검증 대상은 "원 요청이 어떤 `res` 로 끝나는가" 가 **아니라** drain 회계다:
요청 하나당 CQE 하나, 총 `n + k` 개를 수확하고 링에 남긴 것이 0 이어야 한다.
`cancelAndDrain` 은 `cqe->res` 를 보지 않고 CQE 개수만 센다(`uring_backend.cpp:169-198`).
`-ECANCELED` 든 정상 완료든 **CQE 는 똑같이 하나 나오므로 회계는 같다.**

`TestUringCancelDrainsBothCqes` 가 파일 4개에 대해 다음을 단언한다.

```text
  wait_cqe failure: requestCqes=4 cancelSqes=4 cancelCqes=4 leftoverCqes=0
  short submit:     requestCqes=4 cancelSqes=4 cancelCqes=4 leftoverCqes=0
```

같은 테스트가 변이 주입으로 검출 범위까지 기록해 두었다(`system_io_path_tests.cpp:687-694`).
**잡는다**: 취소+배수 전체 제거, 일부 요청만 취소, 원 요청 CQE 를 기다리지 않음.
**못 잡는다**: `pendingCancels` 를 0 으로 두는 변이 — 이 환경에서는 취소 CQE 가 먼저 도착해
배수 루프가 어차피 전부 수확하므로 관측 가능한 차이가 없다.

느린 디바이스가 있는 기기라면 **그 변이 하나의 검출 여부** 를 확인할 가치는 있다
(도착 순서가 뒤집히면 `leftoverCqes != 0` 이 되어 잡힌다). 그 외에는 얻을 것이 없다.

---

## 5. 재측정 절차 — 어느 표를 교체하는가

### 5.1 측정

`articles/step08_perf_visualization.md` §11 의 "재현" 블록(현재 835~851행)이 원본이다.
그대로 옮기면:

```bash
cmake -S <소스> -B ~/build-skl -DCMAKE_BUILD_TYPE=Release
cmake --build ~/build-skl -j
cd ~/build-skl
rm -rf /tmp/skl_fx                      # 픽스처를 새로 만들어 캐시 상태를 맞춘다

for combo in "--backend=read" "--backend=pread" "--backend=mmap" \
             "--backend=aio" "--backend=io_uring" \
             "--backend=read --workers=4" "--backend=read --workers=8" \
             "--backend=read --workers=4 --ctx=coroutine" \
             "--backend=read --workers=4 --sync=spin" \
             "--backend=io_uring --workers=4" "--backend=mmap --workers=4"; do
    ./system_kernel_bench $combo --warmup=20 --cycles=200 \
        --fixture-dir=/tmp/skl_fx --out=- 2>/dev/null
done > raw.csv

awk 'NR == 1 || $0 !~ /^backend_req/' raw.csv > all.csv
```

조합당 **3회 반복** 하고, 픽스처를 새로 만들어 전 조합을 처음부터 다시 도는
**독립된 두 세션(A/B)** 을 만든다. 그게 기존 표가 만들어진 방식이다.
`cycle_ms` 는 3회 반복의 p50 중 중앙값을 쓴다.

부수 측정 두 개도 같이 한다(기존 표의 해석 문단이 이 값을 인용한다).

```bash
# (a) io_uring, w1 의 fault 가 워크로드 크기에 정비례하는가 (기존: 992 / 2016 / 4064)
for n in 4 8 16; do
    ./system_kernel_bench --backend=io_uring --files=$n --warmup=20 --cycles=200 \
        --fixture-dir=/tmp/skl_fx$n --out=- 2>&1 >/dev/null | grep faults
done

# (b) mmap 의 콜드 캐시 값 (기존: 256 fault / 0.7 ms → 4096 fault / 5.2 ms)
#     픽스처를 만든 직후 한 번, 캐시에서 밀려난 뒤 한 번.
```

### 5.2 교체할 표 — 정확한 위치

| 파일 | 위치 | 무엇을 한다 |
|---|---|---|
| `articles/step08_perf_visualization.md` | `## 11. 실측 표 …` → `### 측정 환경` 표 (현재 785~795행) | **교체하지 말고 열을 추가하거나 표를 하나 더 둔다.** WSL2 열은 그대로 두고 베어메탈 열을 옆에 붙이는 쪽이 낫다 — 아래 §5.3 |
| " | `### 표` 의 11행 표 (현재 803~815행) | 같은 원칙. `cycle ms (세션 A / B)` 형식을 유지한다 |
| " | 787~795행의 항목 값 | OS · 커널 · 실행 형태 · CPU · 컴파일러 · liburing · 파일시스템 · 픽스처 · 사이클 |
| " | 797행 "이 수치를 절대값으로 인용하면 안 된다" 문단 | 베어메탈 수치가 생기면 이 단서의 내용이 바뀐다. §5.3 |
| " | `### 읽히는 것` (현재 819~831행) | **여섯 문단 전부가 숫자를 직접 인용한다.** 13배·3%·48·17·32·256·4064·992·2016·0.66→0.61 등. 새 수치로 다시 쓰거나, 어느 환경의 관측인지 문장마다 밝힌다 |
| " | 823행 "파일 수를 4·8·16으로 바꾸면 992·2016·4064" | §5.1 (a) 의 결과로 교체 |
| " | 825행 "4096 fault, 5.2 ms" | §5.1 (b) 의 결과로 교체 |
| " | 829행 코루틴 ctx-sw 문단 · 831행 sync 축 문단 | **두 문단은 "관측되지 않는다" 는 결론이다.** 베어메탈에서도 같은지 다시 판정한다 |
| " | `## 6.` 의 `### 시나리오 E` (현재 326~350행) | §3-4 의 결과를 반영. 조건절("권한과 코어 상황이 맞으면")을 실측으로 바꾼다 |
| " | `## 1.` 의 예측 표 (현재 54~61행) | `sched: default → SCHED_FIFO + 부하` 행이 §3-4 의 대상이다 |
| " | `## 결론` 의 891행 | "가설 3(단, 별도 경쟁 부하를 만들었을 때)" 서술이 §3-4 결과에 달려 있다 |
| `README.md` | CSV 예시 행 (현재 49행) | 이 머신에서 뽑은 실제 행이다. 새 기기 값으로 바꿔도 되고 그대로 둬도 되지만, **그대로 둔다면 어느 환경의 행인지 밝힌다** |
| `README.md` | 289행 "이 머신(WSL2)의 실측 표와 해석은 articles/step08에 있다" | 표가 둘이 되면 이 문장도 바꾼다 |

### 5.3 교체가 아니라 병기를 권한다

기존 표의 가치는 숫자 자체가 아니라 **"WSL2 에서는 이렇게 갈라진다"** 는 관측이다.
덮어쓰면 그 관측이 사라지고, 두 환경 사이의 차이 — 이 lab 이 실제로 배우게 하려는 것 —
도 함께 사라진다. 권하는 형태:

```markdown
### 측정 환경

| 항목 | 환경 A (WSL2) | 환경 B (베어메탈) |
|---|---|---|
| OS | Ubuntu 26.04 LTS | … |
| 커널 | 6.18.33.2-microsoft-standard-WSL2 | … |
| 실행 형태 | WSL2 VM | 베어메탈 |
...
```

그리고 797행의 단서를 "**WSL2 수치는 절대값으로 인용하면 안 된다**" 에서
"**두 환경의 차이가 곧 가상화 계층의 비용이다. 어느 쪽도 절대값으로 인용하지 않는다**"
쪽으로 옮긴다. 두 열이 생기면 §11 의 제목("이 머신에서 조합이 실제로 갈라지는가")도
더 강한 주장을 할 수 있다 — **조합의 상대 순서가 두 환경에서 보존되는가** 가
훨씬 인용 가치가 높은 결과다.

---

## 6. 이 문서를 쓰며 실측으로 정정한 것

인수인계 지시에 실려 있던 전제 두 가지가 **이 기기에서 확인해 보니 틀렸다.**
§3 의 판정을 잘못 읽지 않도록 여기 적는다.

### 6.1 "이 WSL 에는 `cpu.weight` 파일 자체가 없다" — 틀렸다

인수인계 메모는 "cgroup 이 `/sys/fs/cgroup/init.scope` 인데 `cpu.weight` 파일 자체가 없어
(컨트롤러 미활성) 원래 값 *읽기* 에서 먼저 폴백한다 — `cgroup_write` seam 에 도달조차 못 한다"
고 적었다. 실측 결과:

```text
$ cat /sys/fs/cgroup/cgroup.subtree_control
cpu memory pids
$ cat /sys/fs/cgroup/init.scope/cpu.weight
100
```

**파일은 있고 읽힌다.** 읽기 경로도 실제로 성공한다 — 승인 경로를 주고 벤치를 돌리면
읽기를 통과해 **쓰기 seam 까지 도달하고** 진짜 `EACCES` 로 실패한다:

```text
$ SKL_CGROUP_DIR=/sys/fs/cgroup/init.scope ./system_kernel_bench --sched=cgroup ...
skl: writing /sys/fs/cgroup/init.scope/cpu.weight failed: Permission denied — keeping default
```

**그런데도 `system_fault_tests` 는 `cgroup write seam reached: no (cpu.weight unreadable here)`
를 찍는다.** 원인은 환경이 아니라 **테스트 순서 + 정적 상태 캐시** 다.

`CaptureOriginalCgroupWeight`(`sched_policy.cpp:136-144`)는 `state.captured && state.dir == dir`
이면 즉시 반환하는데, **읽기가 실패했을 때도 `captured = true` 로 표시한다.**
`TestSchedCgroupFallback` 은 (3)에서 `cgroup_read:0:EACCES` 를 주입해 읽기를 실패시키고,
그 결과 `originalWeight = ""` 인 채 캐시가 굳는다. 이어지는 (4)의 `cgroup_write` 주입 케이스는
같은 `dir` 이라 재캡처를 건너뛰고 `originalWeight.empty()` 에서 먼저 빠져나가므로
**쓰기 seam 을 영원히 밟지 못한다.** stderr 에 "reading … failed" 가 **두 번** 찍히는 것이 증거다.

```text
skl: reading /sys/fs/cgroup/init.scope/cpu.weight failed — keeping default   ← (3) 주입 때문
skl: reading /sys/fs/cgroup/init.scope/cpu.weight failed — keeping default   ← (4) 캐시 때문
```

**따라서:**

- `cgroup_write` seam(주입에 의한 쓰기 실패 폴백)은 **어떤 환경에서도** `system_fault_tests`
  로는 밟히지 않는다. `cpu.weight` 가 쓰기 가능한 기기에서는 그 앞 단계에서 테스트가
  통째로 skip 되고(§3-1 마지막), 쓰기 불가한 기기에서는 이 캐시 문제로 막힌다.
- 리눅스 기기에서도 **이 줄은 여전히 `no` 로 나온다.** `no` 가 나왔다고 §3-1 이 실패한 게
  아니다. §3-1 의 판정은 `cpu.weight` 값과 stderr 로 하라.
- 고치려면 `CaptureOriginalCgroupWeight` 가 **읽기 실패 시 `captured` 를 세우지 않게** 하거나,
  테스트가 (3)과 (4) 사이에 상태를 리셋해야 한다. 이 문서는 제품 코드를 건드리지 않았다.

### 6.2 "`--sched=fifo` 는 `sched_applied=0` 으로 기록된다" — 항상 그렇지는 않다

`sched_applied` 열은 벤치 러너가 `workload.Init()` **직후** 한 번 읽어 고정한다
(`bench_main.cpp:326`). 그런데 `SCHED_FIFO`·`nice` 의 적용은 **워커 스레드가 자기 루프
시작 시점에** 하고(`file_io_workload.cpp:223-224`), 실패해야 `schedApplied_` 가 false 로
내려간다. 두 시점 사이에 경쟁이 있다 — 워커가 아직 `ApplySchedToCurrentThread` 를 실행하기
전에 메인이 값을 읽으면 **실패했는데도 1 이 기록된다.**

작성 환경 실측. 워커 4개가 **전부 `EPERM` 으로 실패했는데도** 1 이 기록된 실행의 비율:

```text
--cycles=1  (10회)    0 이 9,  1 이 1
--cycles=3  (12회)    0 이 7,  1 이 5
```

**사이클 수를 늘려도 해결되지 않는다.** 이 열은 측정 루프가 돌기 전에 한 번 정해지므로
`--cycles` 와 무관하고, 값은 순전히 스레드 기동 경쟁으로 갈린다. 위 두 비율의 차이는
경향이 아니라 표본 잡음으로 읽어야 한다 — **아무 실행에서나 1 이 나올 수 있다.**

**따라서 §3-2·§3-3 에서 `sched_applied=1` 을 "FIFO 가 적용됐다" 의 증거로 쓰면 안 된다.**
증거는 둘이다.

1. stderr 에 `pthread_setschedparam(SCHED_FIFO, prio=50) failed:` 가 **없는 것**.
2. `chrt -p <tid>` 가 워커 스레드에 대해 **`SCHED_FIFO` 를 보고하는 것**(§3-2 (b)).

`--sched=cgroup` 은 프로세스 단위 정책이라 `Init` 안에서 동기적으로 끝난다.
그 조합의 `sched_applied` 는 믿어도 된다.

---

## 7. 체크리스트

리눅스 기기에서 끝났을 때 아래가 전부 채워져야 한다.

- [ ] §2.2 `ctest` **8/8 통과** (네이티브 리눅스 기준선 확보)
- [ ] §2.4 `ctest -L strict` 통과 (io_uring·aio 가 실제로 돌았다)
- [ ] §3-1 `during=200` · `after=<원래 값>` · CSV `sched_applied=1`
- [ ] §3-2 `SCHED_FIFO applied without injection: yes (privileged)`
- [ ] §3-2 `chrt -p` 가 워커 4개에 `SCHED_FIFO` 보고
- [ ] §3-2 권한을 준 뒤에도 주입 케이스(실패 폴백)가 그대로 통과
- [ ] §3-3 `grep -c 'failed' /tmp/sched.err` → 0
- [ ] §3-4 부하 유/무 × `default`/`nice`/`fifo` 5개 CSV 와 결론 한 줄
- [ ] §5 두 세션 × 11조합 CSV, 부수 측정 (a)(b)
- [ ] §5.2 표 교체 또는 병기, `### 읽히는 것` 여섯 문단 재작성
- [ ] §4 의 5·6·7 은 **손대지 않았음** (닫힌 항목을 다시 파지 않았다)
