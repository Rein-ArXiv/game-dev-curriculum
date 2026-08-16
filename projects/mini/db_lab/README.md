# db_lab — db 시리즈의 내구성·성능 근거 측정

## 컨셉

이 lab은 **WAL 기반 트랜잭셔널 키-값 스토어를 최소 구현으로 직접 만들고, db 시리즈의 핵심 주장 세 가지를 수치와 검사로 확인**하는 도구다. GUI 없이 CLI로 돈다 — 측정 대상이 화면이 아니라 디스크와 시간이기 때문이다.

확인하는 주장:

1. **내구성에는 가격이 있다** — fsync ON/OFF의 커밋 지연 차이가 그 가격이고, 트랜잭션에 op를 묶을수록 op당 비용이 떨어진다(그룹화의 원리). `db_bench`가 표로 출력한다.
2. **재시작 비용은 로그 길이에 선형이다** — 체크포인트 없는 설계의 복구(재생) 시간을 로그 크기별로 잰다.
3. **COMMIT 마커는 잘린 꼬리에서 원자성을 지킨다** — 이체 트랜잭션 로그를 파일 전체 범위의 여러 바이트 위치에서 잘라 복구한 뒤, 잔액 보존·부분 적용 부재 불변식을 검사한다. `db_crash_test`가 수백 회 절단과 수백 회 바이트 손상을 자동 수행한다.

`projects/mega/include/db`와 디펜스 진행도 저장의 WAL 계층이 이 lab의 주장 위에 선다.

## 짝 시리즈

- **db** — 영속·스토리지 엔진·트랜잭션·경제 정합을 다루는 시리즈. WAL의 "변경을 먼저 적고 fsync, COMMIT 마커 없는 꼬리는 복구가 버린다"는 서술을 이 lab이 실행 가능한 형태로 만든다.

## 구성 요소와 측정 항목

| 실행 파일 | 무엇을 하나 | 측정·검사 |
|---|---|---|
| `db_bench` | 커밋 지연 스윕 + 재생 시간 | fsync × txn 크기별 avg/max ms, commits/sec; 로그 크기별 replay ms |
| `db_crash_test` | 크래시 주입 일관성 스윕 | 파일 전체 범위의 절단 지점 표본 + 바이트 손상 지점 표본 × 시드 3종 — 보존·원자성 불변식 |
| `db_tests` | 단위 테스트 (ctest) | 프레임 바이트 배치와 CRC 검사값, put/get/del 가시성, 재시작 복구, 미완 꼬리 폐기, CRC 손상 검출과 손상 이후 꼬리 폐기, Del 프레임 디스크 왕복, fsync ON 커밋의 내구성, 프레임 길이 상한 거부 |

스토어 자체(`src/wal_kv.hpp`)는 헤더 하나다 — 프레임 형식 `[len][crc32][payload]`, 트랜잭션 = op 프레임들 + COMMIT 프레임, 복구 = 재생하되 COMMIT 없는 꼬리와 CRC 불일치 이후를 유효 접두사 끝에서 절단.

### 테스트가 실제로 무엇을 잡는가

각 검사는 '이 구현 결함이 들어오면 빨개진다'는 대응을 가진다. 아래 대응은 헤더에 결함을 직접 넣어 확인한 것이다.

| 결함 | 잡는 검사 |
|---|---|
| CRC 다항식·반사·엔디언이 바뀜 | `db_tests`의 `frame_codec_bytes` (IEEE 검사값 `0xCBF43926`) |
| CRC 불일치에서 멈추지 않고 재동기화 (`break`→`continue`) | `db_tests`의 `midlog_corruption_drops_tail`, `db_crash_test`의 바이트 손상 스윕 |
| `Op::Del` 프레임 디코드 실패 | `db_tests`의 `del_survives_restart` |
| fsync ON 경로가 항상 실패 | `db_tests`의 `fsync_on_commit_durable`, `db_bench --smoke` |
| 프레임 길이 상한(`kMaxFrame`) 가드 제거 | `db_tests`의 `oversized_frame_rejected` |

## 빌드와 실행

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/db_bench
./build/db_crash_test
```

Windows(MSVC)에서는 `cmake --build build --config Release`, 실행 파일은 `build/Release/`, ctest는 `ctest --test-dir build -C Release --output-on-failure`. 외부 의존성이 없다 — C++20 컴파일러와 CMake만 있으면 된다.

ctest에 등록된 대상은 다섯이다 — 전체 실행 셋과, step 문서가 자기 검증에 쓰는 step별 실행 둘이다.

| 테스트 이름 | 명령 | step | 걸리는 시간 |
|---|---|---|---|
| `db_tests` | `./build/Release/db_tests` | — (전체) | 1초 미만 |
| `db_crash_test` | `./build/Release/db_crash_test` | step04 | 20초 안팎 |
| `db_bench_smoke` | `./build/Release/db_bench --smoke` | step03 | 1초 안팎 |
| `db_tests_step01` | `./build/Release/db_tests --step=01` | step01 | 1초 미만 |
| `db_tests_step02` | `./build/Release/db_tests --step=02` | step02 | 1초 미만 |

step별 실행은 전체 실행을 대체하지 않고 *더한다*. step k를 막 끝낸 독자가 그 시점에 확인할 수 있는 것만 골라 이름과 함께 찍는 것이 목적이다 — 어느 검사가 어느 step 소속인지는 `tests/db_tests.cpp`의 `kCases` 표가 정한다.

```text
$ ./build/Release/db_tests --step=01
  ok   frame_codec_bytes
  ok   crc_mismatch_stops_replay
  ok   garbage_length_rejected
  ok   oversized_frame_rejected
db_tests[step01]: 4/4 passed
```

`db_bench --smoke`는 표본을 줄인 축소 실행이라 측정값으로는 쓸 수 없다. 등록 이유는 fsync ON 커밋 경로와 벤치의 실패 검사(커밋·재생이 실패하면 즉시 비정상 종료)를 매 ctest 실행에서 지나가게 하기 위해서다. 수치를 보려면 인자 없이 `./build/Release/db_bench`를 직접 돌린다.

기대 출력:

```text
db_tests: all passed
crash sweep: 633 trials, 0 failures
flip sweep : 291 trials, 0 failures
```

## 결과 해석 가이드

- `db_bench` 표 1에서 fsync ON의 avg ms는 저장 장치의 fsync 지연 그 자체다 — NVMe와 HDD에서 자릿수가 다르게 나온다. OFF와의 비율이 "내구성의 가격"이고, `txn_ops`를 키울 때 commits/sec가 떨어져도 ops/sec(= commits/sec × txn_ops)는 올라간다 — 커밋당 fsync 1회 상각의 효과다.
- `db_bench` 표 2의 replay ms가 로그 크기에 비례해 자라는 것이 체크포인트(스냅샷 후 로그 절단)의 존재 이유다 — 이 lab은 의도적으로 체크포인트를 빼서 그 비용을 드러낸다.
- `db_crash_test`의 "0 failures"는 검사한 절단 위치와 손상 위치에서 복구 결과가 항상 *커밋된 트랜잭션들의 완전한 접두사* 였음을 뜻한다. 부분 적용된 이체가 없다는 강한 증거지만, 모든 저장 장치 장애와 모든 바이트 위치를 증명하는 것은 아니다.
- 두 스윕의 역할이 다르다. 절단(`crash sweep`)은 잘린 프레임이 길이·본문 부족 검사에서 먼저 걸러지므로 CRC 경로를 지나가지 않는다. 바이트 손상(`flip sweep`)은 손상 프레임 뒤에 멀쩡한 COMMIT이 남아 있는 상태를 만들기 때문에, 손상 지점에서 멈추지 않고 재동기화하는 구현을 여기서만 잡는다.

## 디렉토리 구조

```text
db_lab/
  CMakeLists.txt
  README.md
  src/
    wal_kv.hpp          WAL 트랜잭셔널 KV (헤더 단일, fsync 토글, 복구 통계)
    db_bench.cpp        커밋 지연 / 재생 시간 벤치
  tests/
    db_crash_test.cpp   절단 스윕 크래시 주입 + 불변식 검사
    db_tests.cpp        단위 테스트 (ctest, step별 이름 필터)
```

## 구현 범위 메모

- 단일 스레드·단일 파일 — 동시성(잠금·MVCC)과 인덱스 자료구조(B-tree/LSM)는 시리즈 글의 영역이고, 이 lab은 내구성·원자성의 물리만 떼어 측정한다.
- 체크포인트·로그 압축은 의도적으로 제외 — replay 비용이 로그에 선형임을 보이는 것이 목적이다. mega의 DB 계층은 같은 설계에 도메인 레포지토리를 얹는다.
- fsync는 POSIX `fsync` / Windows `_commit` 분기. OS 페이지 캐시·디스크 캐시 계층의 차이는 측정 결과의 플랫폼별 변동으로 나타난다 — 그것 자체가 관찰 포인트다.
