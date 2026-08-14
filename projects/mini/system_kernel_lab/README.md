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
    ctx/
      ctx_type.h               pthread/coroutine 토글 (코루틴 구현은 workload에)
    measure/
      frame_timer.{h,cpp}      사이클 ms·MB/s·카운터 슬라이딩 윈도우
      proc_counters.{h,cpp}    getrusage 래퍼
    ui/
      hud.{h,cpp}  toggles.h
  scripts/
    install_deps.sh            Ubuntu/Debian 빌드 의존성 설치
```

## 빌드와 실행

```bash
./scripts/install_deps.sh        # SDL2, liburing 등 (Ubuntu/Debian)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/system_kernel_lab
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

## 검증 범위

`system_core_tests`는 가용한 I/O backend가 동일한 총 바이트와 체크섬을
반환하는지, 네 동기화 구현이 다중 스레드에서 상호배제를 지키는지 검사한다.
GUI의 성능 곡선은 정답 수치가 아니라 현재 머신·커널·파일시스템·캐시 상태의
관측값이다. `perf stat`, `strace -c`, `trace-cmd`를 함께 기록해야 원인을
구분할 수 있다.
