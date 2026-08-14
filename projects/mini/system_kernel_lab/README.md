# system_kernel_lab — system 시리즈의 원리 측정·시각화

## 컨셉

이 lab은 **동일한 파일 IO + 동시 작업 워크로드를 여러 시스템 콜·동기화·스케줄링 방식으로 돌리며 커널 메커니즘의 비용 차이를 측정**한다. N개의 워커가 16개의 1MB 파일을 읽고 sampled checksum으로 가공하는 작업을 고정해두고, IO 모델·락 종류·스케줄링 정책·컨텍스트 스위치 방식·워커 수를 토글한다. 사이클 ms·처리량·컨텍스트 스위치·페이지 폴트가 토글에 따라 어떻게 달라지는지 그대로 본다.

## 짝 시리즈

- **system** — 시스템 콜 비용, IO 모델, 동기화 프리미티브, 스케줄러, 컨텍스트 스위치를 다루는 시리즈의 주장을 이 lab이 측정으로 검증한다.

## 토글 항목

| 항목 | 옵션 | 비교 대상 |
|---|---|---|
| IO 모델 | read / pread / mmap / aio / io_uring | system/part13_block_io.md, part14_io_uring.md |
| 동기화 | mutex / spinlock / futex / atomic CAS | system/part09_sync.md |
| 스케줄링 | default / nice +10 / SCHED_FIFO / 승인된 cgroup cpu.weight 200 | system/part08_scheduler.md |
| 실행 모델 | pthread 병렬 / ucontext 협력적 직렬 실행 | system/part02_process.md |
| 워커 수 | 1 / 2 / 4 / 8 | 스케일링 곡선 |

워크로드 크기는 16개 × 1MB 픽스처 파일로 고정이다(`/tmp/system_kernel_lab_data`에 자동 생성).

## 출력 (ImGui HUD)

- cycle ms 그래프 — 한 사이클(전체 파일 읽기)의 경과 시간.
- 처리량 그래프(MB/s).
- ctx switches / cycle — `getrusage` 기반 voluntary+involuntary 컨텍스트 스위치 delta.
- page faults / cycle — minor+major 페이지 폴트 delta.
- 좌측 stats — 현재 토글 조합, 파일 수·크기, 누적 사이클·바이트, fixture 경로.
- `sched apply` — 요청한 정책이 이 조합과 권한에서 실제 적용됐는지 표시.

strace/ftrace/perf 타임라인 통합은 미구현이며, 외부 도구로 병행 측정한다(articles/step08 참조).

## 출력 (CSV — `system_kernel_bench`)

GUI는 눈으로 읽는 경로라 인용도 대조도 되지 않는다. 같은 워크로드와 같은
계측기(`FrameTimer` + `getrusage`)를 **창 없이** 돌리고 조합을 명령줄로 받는
세 번째 실행 파일이 그 구멍을 메운다. SDL2/OpenGL/ImGui는 링크하지 않는다.

```bash
./build/system_kernel_bench --backend=io_uring --sync=mutex --sched=default \
    --ctx=pthread --workers=4 --cycles=200 --warmup=20 --out=uring_w4.csv
```

`--warmup` 사이클을 버린 뒤 사이클마다 4채널을 CSV 한 줄로 적는다.

```text
backend_req,backend,sync,sched,sched_applied,ctx,workers,files,file_bytes,cycle,cycle_ms,mb_per_s,ctx_sw,page_faults
io_uring,io_uring,mutex,default,1,pthread,4,16,1048576,0,1.732844,9682.317,32,0
```

- 조합 정보를 **행마다** 싣는다. 실행이 다른 파일을 그대로 이어 붙여 한 표로
  읽을 수 있고, 행 하나만 봐도 어떤 조합의 수치인지 알 수 있다.
- `backend_req` 와 `backend` 를 나눈다. io_uring/aio는 런타임 초기화가 실패하면
  조용히 read로 폴백하므로, 요청 값만 적으면 read로 돈 수치를 io_uring 수치로
  읽게 된다. `--strict-backend` 는 그 폴백 자체를 실패로 만든다.
- 요약(p50/p99 포함)은 stdout으로, `--out=-` 이면 CSV가 stdout이고 요약이
  stderr로 간다.
- 토글 이름은 정규화 후 별칭표와 맞춘다(`io_uring` = `uring`). **모르는 이름은
  거부한다** — 조용히 기본값으로 흘리면 CSV가 요청하지도 않은 조합의 수치를 그
  이름으로 담는다.

전체 인자는 `--help`. 실측 표와 해석은 articles/step08에 있다.

## 실험 해석 규칙

한 화면에 모든 토글이 있지만 각 조합이 같은 질문을 측정하지는 않는다.

| 알고 싶은 것 | 고정할 조건 | 해석하면 안 되는 것 |
|---|---|---|
| 동기 I/O와 batch 제출 비용 | `workers=1`, 같은 warm/cold cache 상태 | `workers>=2`에서는 backend가 파일 하나씩 호출되어 AIO/io_uring batch 폭이 사라진다 |
| 작업 큐 락 경합 | `workers>=2`, 같은 backend와 worker 수 | `workers=1`에서는 sync 토글이 실행 경로에 들어가지 않는다 |
| 스케줄링 정책 | `workers>=2`, 별도 경쟁 부하와 CPU affinity 기록 | 경쟁 부하가 없으면 nice/cgroup weight 차이가 거의 의미 없다 |
| pthread와 ucontext | 같은 backend/worker 수, 동기 I/O라는 조건 명시 | coroutine 모드는 I/O 대기에서 yield하지 않으므로 순수 context-switch 비용 비교가 아니다 |

`ucontext` 모드는 한 driver OS thread에서 동기식 `RunCycle`을 호출한다. 한
코루틴이 `read`에서 막히면 다른 코루틴도 진행하지 못한다. 따라서 pthread와
coroutine 그래프의 차이는 커널/유저 컨텍스트 전환 비용만이 아니라 **병렬 I/O
대 직렬 I/O + 전환 오버헤드**의 합이다.

## 측정 항목

- **사이클 ms**: `std::chrono::steady_clock`으로 RunCycle 전후 측정.
- **처리량**: 사이클당 처리 바이트 / 경과 시간.
- **컨텍스트 스위치·페이지 폴트**: `getrusage(RUSAGE_SELF)`의 `ru_nvcsw`/`ru_nivcsw`/`ru_minflt`/`ru_majflt` 사이클 delta.

## 기술 스택

- C++20.
- Linux 네이티브 (커널 5.15+ 권장, io_uring 사용).
- liburing.
- Dear ImGui (간단한 SDL2 백엔드).
- perf, strace, ftrace 도구 연동.

## 디렉토리 구조

```text
system_kernel_lab/
  CMakeLists.txt
  src/
    main.cpp                   SDL2+ImGui 루프, 사이클 측정
    workload/
      file_io_workload.{h,cpp} 공통 워크로드 (단일/워커풀/코루틴 3모드 + 픽스처 생성)
    io/
      io_backend.h  io_backend_factory.{h,cpp}
      read_backend / pread_backend / mmap_backend / aio_backend / uring_backend
    sync/
      i_sync.h  mutex_sync.h  spin_sync.h  futex_sync.h  cas_sync.h
      sync_factory.{h,cpp}
    sched/
      sched_policy.{h,cpp}     nice/FIFO/cgroup cpu.weight 적용·복구
    fault/
      fault_inject.{h,cpp}     실패 주입 seam (SKL_FAULT 환경변수 + 테스트 API)
      skl_syscalls.h           open/read/pread/fstat/mmap/setpriority 얇은 wrapper
    ctx/
      ctx_type.h               pthread/coroutine 토글 (코루틴 구현은 workload에)
    testhook/
      test_hook.h              테스트 전용 타이밍 훅 (SKL_TEST_HOOKS 빌드에만 존재)
    measure/
      frame_timer.{h,cpp}      사이클 ms·MB/s·카운터 슬라이딩 윈도우
      proc_counters.{h,cpp}    getrusage 래퍼
    ui/
      hud.{h,cpp}  toggles.h
  tools/
    bench_main.cpp             헤드리스 측정 러너 (조합 → 4채널 CSV)
  tests/
    system_core_tests.cpp      성공 경로 (backend 동등성 · sync 상호배제 ·
                               backend×실행모델×sync 전수 · 작업 큐 인계 회귀)
    system_fault_tests.cpp     오류 경로 (seam 주입: 폴백 · 링 재초기화 · 권한 실패)
    system_io_path_tests.cpp   부분 읽기 계약 · 실제 시그널 EINTR · 취소 후 배수
  scripts/
    install_deps.sh            Ubuntu/Debian 빌드 의존성 설치
  docs/
    LINUX_HANDOFF.md           별도 리눅스 기기용 실행 지시서 (권한·측정 재현)
```

빌드 산출물은 셋이다 — GUI 도구 `system_kernel_lab`, 테스트 세 개, 헤드리스
러너 `system_kernel_bench`. 뒤의 둘은 SDL2/OpenGL/ImGui를 링크하지 않고 같은
코어 TU 목록을 공유한다.

## 빌드와 실행

```bash
./scripts/install_deps.sh        # SDL2, liburing 등 (Ubuntu/Debian)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/system_kernel_lab                                # GUI 도구
./build/system_kernel_bench --cycles=200 --out=run.csv   # 헤드리스 측정
```

SCHED_FIFO는 권한이 필요하다(`ulimit -r` 또는 CAP_SYS_NICE). 권한이 없으면
stderr 안내 후 기본 정책으로 동작한다.

```bash
sudo setcap cap_sys_nice+ep ./build/system_kernel_lab   # FIFO용 (선택)
```

cgroup 토글은 현재 shell이나 사용자 세션이 공유하는 cgroup을 우발적으로
변경하지 않는다. 쓰기 권한이 위임된 일회성 benchmark cgroup에 프로세스를
넣은 뒤, `/proc/self/cgroup`으로 확인한 정확한 경로를 명시해야 한다.

```bash
export SKL_CGROUP_DIR=/sys/fs/cgroup/<delegated-benchmark-cgroup>
```

환경변수가 없거나 실제 현재 cgroup 경로와 다르면 쓰기를 거부한다. 정상
종료에서는 원래 `cpu.weight` 복구를 시도하지만, 강제 종료까지 복구할 수
있다는 보장은 없으므로 공유 cgroup에는 사용하지 않는다.

`SCHED_FIFO` 성공 경로, 위임 cgroup의 `cpu.weight` 쓰기·복구, 경쟁 부하에서의
스케줄링 비교, 베어메탈 대조 측정은 권한이나 별도 환경을 요구해 WSL2에서 닫지
못했다. 그 항목들을 다른 리눅스 기기에서 어떤 명령으로 돌리고 무엇이 나오면
통과인지는 `docs/LINUX_HANDOFF.md`에 있다. 같은 문서가 **리눅스 기기에서도
닫을 수 없는 항목**(FIFO 위 aio/uring 부분 읽기, pread·mmap의 FIFO EINTR,
io_uring 취소의 `-ECANCELED`)과 그 규약이 이미 닫혔는지도 구분해 둔다.

## 검증 범위

`system_core_tests`는 **성공 경로**를 본다 — 가용한 I/O backend가 동일한 총
바이트와 체크섬을 반환하는지, 네 동기화 구현이 다중 스레드에서 상호배제를
지키는지.

- **어느 backend가 실제로 돌았는가.** io_uring/aio는 런타임에 초기화가 실패하면
  조용히 read로 폴백한다. 그래서 요약 라인(`[backends] ... io_uring=io_uring`)을
  항상 stdout에 찍고, `SKL_REQUIRE_BACKENDS`가 설정된 환경에서는 폴백을 `CHECK`
  실패로 만든다. ctest는 stdout을 감추므로 "찍었다"만으로는 검증이 아니다.
  liburing/AIO가 없는 머신도 있으므로 그 강제는 별도 케이스로 분리했다 —
  `system_core_tests_strict`(`SKL_REQUIRE_BACKENDS=uring,aio`, `LABELS strict`).
  `ctest -L strict` 로 요구, `ctest -LE strict` 로 이식성 모드.
- **backend × 실행 모델 × sync 전수 순회.** 컴파일된 모든 backend를
  `{pthread 워커풀, 코루틴}` × 4 sync × 워커 4개로 돌려 단일 스레드 기준값과 총
  바이트·체크섬을 대조한다(현재 이 머신에서 40조합). 워커가 4개면 io_uring 링이
  4개 동시에 생기는 조합을 실제로 밟는다. 값이 같다고 해서 그 backend가 돌았다는
  뜻은 아니므로 — 폴백한 read도 같은 값을 낸다 — fault seam을 *주입 없이* 무장해
  backend별 syscall 호출 횟수를 세고 `> 0` 인지 `CHECK` 한다
  (`read`/`pread`/`mmap`/`aio_read`/`io_uring_submit`).
- **작업 큐 인계 경계.** 사이클 경계에는 "직전 사이클의 마지막 작업을 막 끝낸
  워커가 아직 pop 루프 안에 있는" 짧은 순간이 있고, `remaining_.store`가 큐
  unlock보다 뒤로 가면 그 워커가 0에서 감산해 `SIZE_MAX`로 언더플로한다. 반복
  스트레스(8워커 × 8000사이클 × sync 4종)와 **결정적 재현** 두 가지로 본다.
  결정적 재현은 `src/testhook/test_hook.h`의 타이밍 훅 두 개로 그 워커를 붙잡았다가
  push 직후에 풀어 주는 랑데부다 — 훅은 `SKL_TEST_HOOKS`를 정의하는 테스트
  타깃에만 존재하고, 앱 타깃에서는 호출이 통째로 사라진다. 회귀가 나면
  `RunCycle`이 돌아오지 않으므로 테스트 자체가 10초 워치독으로 진단을 찍고
  종료하며, ctest의 `TIMEOUT 60`이 그다음 안전망이다.

  두 판정의 사각이 서로 다르다는 점이 중요하다. 랑데부는 store를
  `RunTestHook(AfterQueueUnlock)` *아래* 로 내리는 변이만 잡는다. store를
  `Unlock()` 과 그 훅 *사이* 에 두면 훅이 워커를 풀어 줄 때 store가 이미
  끝나 있어 감산이 `N → N-1` 로 통과해 버린다 — 락 밖으로 나갔다는 진짜
  회귀인데도 그렇다. 그 변이는 스트레스만 잡을 수 있고, 잡으려면 두 가지가
  필요하다. (1) 언더플로를 *세는* 훅. 락도 대기도 없는 relaxed `fetch_add`
  하나라 스트레스가 노리는 인터리빙을 흐트러뜨리지 않는다. (2) 그 카운터를
  1ms 주기로 읽는 감시 스레드. 이 워크로드에서 언더플로와 hang은 *같은 사건*
  이라 — 0에서의 감산을 뒤이은 `store(N)` 이 지워 `remaining_` 이 영영 0이
  못 된다 — 언더플로를 일으킨 그 `RunCycle` 이 그대로 매달린다. 루프로
  돌아와 카운터를 읽는 코드는 실행될 기회가 없으므로 다른 스레드가 읽어야
  한다. 실측(변이 주입, 40회): 1600사이클은 4/20만 잡았고 32000사이클은
  40/40을 잡았다. 두 배치 각각 12회 연속 전부 검출, 실패까지 0.2초.
  정상 코드 12회 연속 오탐 0.

`system_fault_tests`는 **오류 경로**를 본다. 정규 파일과 정상 커널에서는
EINTR 재시도, `aio_suspend` 실패 후 취소+polling 수확, io_uring 링 재초기화,
factory의 io_uring→read 런타임 폴백, 스케줄링 권한 실패 폴백이 한 번도
실행되지 않으므로, `src/fault`의 주입 seam으로 그 분기를 결정적으로 밟는다.
모든 오류 경로 뒤에 `/proc/self/fd` 개수가 그대로인지(fd 누수 없음), 주입을
푼 다음 사이클이 정상값을 내는지(오류 처리가 backend를 망가뜨린 채 두지
않음)를 공통 사후조건으로 검사한다.

`system_io_path_tests`는 PROJECTS_PLAN이 이 lab에 지정한 I/O 3항목
**부분 읽기 · EINTR · 취소**를 본다.

- **부분 읽기(short read)**: 파일이 요청보다 짧은 것은 *오류가 아니라 EOF*
  라는 것이 `IIoBackend`의 계약이다(근거는 articles/step02). 딱 맞는 파일 ·
  요청의 절반 · 4097바이트 · 1바이트 · 빈 파일 · 요청보다 큰 파일을 한
  사이클에 섞어 두고, 컴파일된 모든 backend가 path당 `onRead`를 정확히 한 번
  부르며 *같은 `(크기, 체크섬)` 목록*과 *같은 총 바이트*를 내는지 확인한다.
- **EINTR**: `/tmp`에 FIFO를 만들고 별도 스레드가 `PIPE_BUF`(4 KB) 단위로
  끊어 write 하는 동안, `SA_RESTART` 없는 `SIGUSR1` 핸들러를 걸고 감시
  스레드가 읽기 스레드에만 `pthread_kill`을 1 ms 간격으로 보낸다. 검사
  항목은 *시그널이 와도 최종 바이트 수와 체크섬이 무시그널 실행과 동일*.
  `read`의 부분 읽기 루프, `aio_suspend`, `io_uring_wait_cqe` 세 대기 지점을
  모두 밟는다(FIFO는 seek 불가라 pread는 `ESPIPE`, mmap은 정규 파일이 아니라
  거부 — 그 두 거부도 명시적으로 검사한다).
- **취소**: `aio_suspend`가 못 쓰게 됐을 때 남은 요청 전부에 `aio_cancel`을
  걸고 *전부 `aio_return`까지 수확*했는지를 seam 호출 횟수로 센다. io_uring
  쪽은 `IORING_OP_ASYNC_CANCEL`을 걸고 *원 요청 CQE와 취소 요청 CQE를 모두*
  수확했는지를 `UringBackend::LastDrainStats()`의 CQE 개수로 센다(파일 4개
  사이클에서 `4 + 4`, 링에 남은 CQE 0).

"그 분기가 정말 실행됐는가"는 fault seam을 *주입 없이* 무장해 센다. `skip`을
아주 크게 준 규칙(`read:100000000`)은 아무것도 주입하지 않고 호출 횟수만 세는
순수 관측 모드다. `PIPE_BUF` 이하의 write는 원자적이라 시그널이 없으면 `read`
호출 수가 청크 수를 넘을 수 없고, 넘었다면 `EINTR` 재시도가 실제로 있었다는
뜻이다.

seam은 빌드를 바꾸지 않고 환경변수로도 열린다. 문법은
`op[:skip[:ERRNO[:times]]]`를 `,`로 나열하는 것이고, `skip`번째까지는 통과,
그 뒤부터 `times`회(0이면 무제한) 해당 errno로 실패시킨다.

```bash
SKL_FAULT=read:0:EINTR:3 ./build/system_kernel_lab       # read 앞 3회를 EINTR
SKL_FAULT=io_uring_queue_init:0:ENOSYS:1 ./build/system_core_tests  # 폴백 강제
```

주입 설정이 없으면 seam의 비용은 relaxed atomic load 하나이고 동작은
바뀌지 않는다. `SCHED_FIFO` 성공, cgroup `cpu.weight` 쓰기 성공처럼 권한이
필요한 경로는 테스트가 강제하지 않는다 — 실패했을 때 기본 정책으로
폴백하는지만 검사한다.

`system_kernel_bench`는 **성능**을 판정하지 않는다 — 환경마다 달라 회귀 기준이
될 수 없다. 대신 러너 자신의 정합성을 판정하고, 그 넷이 `add_test` 네 개다.

- `system_kernel_bench_smoke` / `..._smoke_workers` — 단일 모드와
  `pread × futex × coroutine × 워커 4` 조합을 3사이클씩. 사이클마다 총 바이트가
  `files * file_bytes` 인지, 체크섬이 사이클 간 동일한지 러너가 직접 검사하고
  어긋나면 0이 아닌 코드로 끝난다.
- `system_kernel_bench_fault_channel` — `--backend=mmap --min-faults=1`.
  mmap은 사이클마다 새 매핑을 만들어 반드시 minor fault를 내므로, 이 한 줄이
  `getrusage` → `DiffCounters` → `FrameTimer` → CSV 배선 전체를 못 박는다.
  카운터 인자 둘을 `EndCycle` 에서 떼면 나머지 케이스는 전부 통과하고 이것만
  실패한다 — *러너가 돌기는 도는데 두 채널이 아무것도 재지 않는* 상태다.
- `system_kernel_bench_rejects_bad_toggle` — `WILL_FAIL TRUE`. 모르는 토글
  이름을 조용히 기본값으로 흘리도록 바꾸면 이 케이스가 실패한다.

GUI의 성능 곡선도 CSV의 수치도 정답이 아니라 현재 머신·커널·파일시스템·캐시
상태의 관측값이다. `perf stat`, `strace -c`, `trace-cmd`를 함께 기록해야 원인을
구분할 수 있다. 이 머신(WSL2)의 실측 표와 해석은 articles/step08에 있다.
