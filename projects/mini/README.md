# 미니 프로젝트 - 원리 측정·시각화 실험실

이 디렉터리는 각 이론 축의 주장과 실패 조건을 독립적으로 재현하는 실험실
모음이다. 미니 프로젝트의 목적은 작은 게임을 많이 만드는 것이 아니라, 같은
입력과 워크로드를 여러 구현에 적용해 정확성·시간·공간 비용을 비교하는 데 있다.

## 완료 기준

빌드 성공이나 화면 출력만으로 완료로 보지 않는다.

| 항목 | 확인할 질문 |
|---|---|
| 이론 주장 | 어떤 모델, 복잡도, 불변식을 검증하는가 |
| 조작 변수 | 어떤 구현이나 조건을 바꾸는가 |
| 통제 변수 | 비교할 때 동일하게 유지하는 입력은 무엇인가 |
| 관측값 | 시간, 메모리, 오차, 처리량 중 무엇을 재는가 |
| 실패 주입 | 어떤 경계 입력이 잘못된 구현을 드러내는가 |
| 자동 테스트 | GUI 없이 어떤 계산 계약을 검증하는가 |

절대 성능 수치는 하드웨어·드라이버·운영체제에 종속된다. 문서가 보장하는 것은
특정 배수가 아니라 측정 방법, 비교 조건, 결과를 해석하는 기준이다.

## 현재 랩

| 랩 | 실제 구현 범위 | headless 검증 상태 |
|---|---|---|
| [`parts_perf_lab`](parts_perf_lab/) | AoS/SoA/Archetype, scalar/SSE/AVX2, predication, malloc/Arena/Pool/Slab, vector/list/intrusive | 할당 정렬·slab 소유권·레이아웃 결과 동등성·SIMD 오차 테스트 |
| [`system_kernel_lab`](system_kernel_lab/) | Linux read/pread/mmap/POSIX AIO/io_uring, mutex/spin/futex/CAS, pthread/ucontext, scheduler 정책 | Linux 전용 CTest 포함, Linux 실행 환경 필요 |
| [`languages_lab`](languages_lab/) | 위치 보존 lexer, 우선순위 parser, 바이트코드 compiler·verifier·VM, 제한 host binding | 구문·제어 흐름·스택 깊이·실행 예산·오류 진단 계약 테스트 |
| [`algorithms_lab`](algorithms_lab/) | 정렬·선택 5팔, 구간 질의(Fenwick·lazy segment tree), 해시 표 4팔, 문자열 탐색 5팔+Aho-Corasick, 그래프 구조(SCC 2팔·절단점·DSU 4조합·MST 2팔), 최단 경로 4팔+A\*, 탐욕·DP, 유량 3팔·매칭 3팔·CSP, 무손실 압축 4팔 | 고정 사례 45개가 느린 독립 오라클(전수 열거·정의 그대로의 재계산)과 대조하고, 시드 무작위 property 11종이 같은 계약을 6,950 표본에서 확인한다. 계약은 multiset 보존·안정성·lazy tag 대수·조회 종료 불변식·condensation DAG·max-flow==min-cut certificate·Apply/Undo 상태 해시·Kraft·H<=L<H+1. 무력화 변이 15건 중 14건 검출(생존 1건은 사유와 함께 등재) |
| [`math_lab`](math_lab/) | 회전, 보간·곡선, 노이즈, SAT/GJK, SDF, BVH, 투영·수치 오차 시각화 | 쿼터니언·곡선 경계·충돌·SDF·BVH·noise 고정 사례에 더해 시드 무작위 property 층 13종(`tests/property.h` — 생성기·축소기·반례 보고). 쿼터니언 항등식과 이중 덮개, Perlin/Simplex 범위 상계(그라디언트 조합 256 가지를 세는 독립 오라클), SAT⇔GJK 일치와 분리면 실증·강체 불변, 행렬 역행렬 왕복(조건수 규칙)과 합성 일치 |
| [`physics_lab`](physics_lab/) | 적분기, 힘, broad/narrow phase, 충돌 응답, 거리 제약, 에너지·반복성 관찰 | 적분기 오차 차수·normal impulse·퇴화 법선 테스트. broad phase 는 brute 와의 쌍 집합 대조, narrow phase 와 접촉 솔버는 고정 사례에 더해 불변식 property 배터리(무작위 볼록쌍의 깊이 일치와 `(normal, depth)` 계약, 무작위 강체 장면의 에너지 비증가·관통 상계·bit 결정성) |
| [`ai_lab`](ai_lab/) | A*/Dijkstra/Greedy/JPS, flow field, steering, boids, influence map | 최단 경로·도달 불가·휴리스틱 계약 테스트. 나머지 장면 테스트 보강 필요 |
| [`graphics_gl_lab`](graphics_gl_lab/) | individual/instanced/MDI, shader permutation, uniform/UBO/SSBO, BC 포맷, MSAA | 빌드 검증 완료. GL 상태·렌더 캡처 회귀 보강 필요 |
| [`graphics_dx_lab`](graphics_dx_lab/) | per-draw/instanced, PSO·RootSig 정책, descriptor write 수명, barrier 기록 형태, fence-safe timestamp | 빌드 검증 완료. 순수 상태 계약 테스트와 캡처 회귀 보강 필요 |
| [`graphics_vk_lab`](graphics_vk_lab/) | per-draw/instanced, barrier mask, fence/timeline 완료 추적, timestamp, validation | 빌드 검증 완료. 동기화 상태 테스트와 validation smoke 보강 필요 |
| [`game_network_lab`](game_network_lab/) | 실제 TCP control, Windows IOCP/Linux epoll, UDP endpoint binding, 권위 tick·snapshot | Windows Release·protocol CTest·별도 server/client socket smoke 통과. Linux 실행 검증 필요 |
| [`live_service_lab`](live_service_lab/) | HTTP login, match queue, server registration·drain, game token, 멱등 result | .NET Release 빌드·core contract test 통과. 교차 언어 HTTP·UDP smoke는 mega 항목에서 실행. token audience·capacity·heartbeat timeout 실패 주입 테스트 보강 필요 |
| [`db_lab`](db_lab/) | WAL KV, fsync·batch·replay 벤치, 로그 절단 crash injection | 단위 테스트와 crash test 통과 |

이 표의 “보강 필요”는 실행 코드가 없다는 뜻이 아니다. 시각화 구현은 존재하지만,
잘못된 결과를 자동으로 검출하는 독립 검증이 아직 완료 조건에 미치지 못한다는
뜻이다.

## 랩별 관측 축

### parts

- 같은 엔티티 상태를 레이아웃과 SIMD 구현만 바꿔 갱신한다.
- 할당자와 컨테이너는 시뮬레이션 시간에 섞지 않고 별도 워크로드로 측정한다.
- 캐시 미스·분기 미스는 앱의 벽시계만으로 추론하지 않고 perf/VTune 같은 하드웨어
  카운터로 확인한다.

### system

- 동일한 파일과 checksum 작업을 I/O backend만 바꿔 실행한다.
- 처리량과 함께 context switch, page fault, 권한 실패를 관찰한다.
- `SCHED_FIFO`, cgroup, `io_uring`은 환경 지원 여부를 결과와 구분한다.

### algorithms

- 같은 문제에 서로 다른 팔을 붙이고, 통제 변수를 "같은 입력 복사본"으로 고정해 비교한다.
  게임이 밀도 n/k를 2⁻¹⁰부터 2¹⁰까지 스윕해 주지 않으므로 워크로드를 직접 만든다.
- 시간만 재지 않는다. 비교 횟수·이동 바이트·방문 노드·probe 길이는 기계 독립이므로 그것을
  먼저 세고, 시간은 그 위에 얹는다. 결과 checksum을 소비해 최적화기가 측정 대상을 지우지
  못하게 한다.
- 검사로 잡히지 않는 결함이 있다는 것을 표에 남긴다. BFS의 표시 시점, Dijkstra의 stale
  항목 처리, DSU의 union by size는 답을 바꾸지 않고 계측기로만 드러난다.

### math·physics·AI

- 수학 랩은 기하·보간 결과의 정확성과 퇴화 입력을 우선한다.
- 물리 랩은 에너지 그림만 보지 않고 오차 차수, 운동량, 침투, 제약 오차를
  수치 계약으로 고정해야 한다.
- AI 랩은 탐색 노드 수와 최단 비용을 구분한다. 빠른 탐색이 항상 최적 경로를
  뜻하지 않는다.

### graphics

- 세 API 랩은 비슷한 큐브 워크로드를 사용하지만 API 우열을 판정하지 않는다.
- OpenGL은 driver-managed state와 draw submission, DX12와 Vulkan은 명시적 자원
  수명·동기화 계약을 관찰한다.
- GPU timestamp는 CPU 프레임 시간과 다르며, query readback 완료를 fence 또는
  availability로 확인한 값만 사용한다.
- bindless, multi-queue overlap, mesh shader처럼 현재 실행 경로에 없는 항목은
  구현된 토글로 세지 않는다.

### network·live service·database

- `game_network_lab`은 별도 프로세스 TCP·UDP server/client로 실제 I/O 수명과 권위 tick을
  검증한다. Windows는 IOCP completion, Linux는 epoll readiness backend를 사용한다.
- `live_service_lab`은 실시간 tick을 소유하지 않는다. endpoint와 짧은 수명의 game token을
  할당하고 멱등 match result를 받는 HTTP 제어 경로를 검증한다.
- Mega는 live service와 같은 HMAC game token 계약을 사용한다. 전용 서버 process test에서 같은
  매치의 token 두 개가 서로 다른 UDP 끝점에 결합되고 두 클라이언트가 권위 snapshot을 받는다.
- 자동 server register·heartbeat·result 제출은 Mega process가 아니라 fleet agent의 배포 책임으로
  남아 있으며, 미니 lab의 HTTP API가 그 상태 전이를 검증한다.
- DB 랩은 단일 스레드 WAL의 원자성·내구성·복구 비용에 집중한다. B-tree/LSM,
  MVCC, 복제까지 구현한 저장소로 해석하지 않는다.

## 빌드 검증

Windows에서 지원되는 랩은 루트의 스크립트로 일괄 구성·빌드할 수 있다.

```powershell
pwsh -File scripts/check_projects.ps1
```

이 스크립트는 모든 빌드와 CTest 뒤 C# Live Service가 발급한 실제 game token 두 개로 C++ Mega
전용 서버의 UDP session을 열고 양쪽 snapshot을 확인한다. 교차 언어 계약만 재검증할 때는 다음을
사용한다.

```powershell
pwsh -File scripts/check_live_mega.ps1
```

컴파일러 또는 CMake 갱신 뒤 기존 캐시와 분리해 검증하려면 외부 빌드 루트를
지정한다.

```powershell
pwsh -File scripts/check_projects.ps1 -BuildRoot "$env:TEMP/velog-project-check"
```

각 랩에 CTest가 있으면 다음 명령으로 정확성 검증을 실행한다.

```powershell
ctest --test-dir projects/mini/<lab>/build -C Release --output-on-failure
```

`system_kernel_lab`은 Linux 커널 API와 권한 모델을 다루므로 WSL2 또는 Linux에서
별도로 구성·실행한다. 그래픽스 랩은 빌드 외에도 실제 창 실행, validation/debug
메시지, GPU query 결과를 확인해야 한다.

## 단계 문서

`articles/step*.md`는 완성 코드를 잘게 복사한 문서가 아니라 공개 계약이 누적되는
구현 경로다. 각 step은 시작 상태, 변경 파일, 실행 명령, 관측 결과를 자체적으로
설명하고 후속 step을 예고하지 않는다. 최종 소스와 문서 스니펫의 드리프트는
별도 검증 대상으로 관리한다.

새 미니 랩은 시리즈 수에 맞춰 자동으로 늘리지 않는다. 기존 랩이나 거대 프로젝트가
소유할 수 없는 핵심 불변식이 있고, 독립된 비교 워크로드와 자동 검증을 설계할 수
있을 때만 추가한다.
