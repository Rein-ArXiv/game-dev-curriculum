# Mini Lab 검증 현황

기준일: 2026-08-23

이 문서는 mini lab의 **빌드 가능 여부**와 **동작 정확성 검증 여부**를 구분한다. 실행 파일이 만들어지는 것과 알고리즘이 옳게 작동하는 것은 같은 완료 조건이 아니다.

## 반복 검증

Windows에서 다음 명령으로 Windows 대상 lab 12개를 configure, Release build, CTest 순서로 검사한다.

```powershell
pwsh -File scripts/check_projects.ps1
```

기존 CMake 캐시와 분리한 디렉터리에서 모든 프로젝트를 다시 구성하려면
`-BuildRoot`를 지정한다. 컴파일러나 CMake를 갱신한 뒤 캐시 호환성까지 배제해야
하는 검수에는 이 형태를 사용한다.

```powershell
pwsh -File scripts/check_projects.ps1 -BuildRoot "$env:TEMP/velog-project-check"
```

이미 configure된 build 디렉터리를 그대로 검사하려면 다음을 사용한다.

```powershell
pwsh -File scripts/check_projects.ps1 -SkipConfigure
```

`system_kernel_lab`은 Linux 전용이므로 이 스크립트에서 제외한다. WSL2 배포판 또는 별도 Linux 환경에서
README의 빌드 절차를 실행한다. 2026-08-15 WSL2 Ubuntu 26.04(커널 6.18.33.2)에서 빌드·CTest 25개 통과를 확인했다.

권한 항목(cgroup 쓰기 성공 경로, `SCHED_FIFO` 적용 성공 경로)은 2026-08-24 `wsl -u root`(무비밀번호
루트 진입)로 WSL2 안에서 검증 완료 — 강제 모드(`SKL_REQUIRE_SCHED=fifo,cgroup`) EXIT=0, cpu.weight
100→200→100 복구, 무권한 ctest 25/25 유지. 남은 별도 환경 항목은 베어메탈 대조 수치(선택)와
io_uring -ECANCELED 경로뿐이며 인수인계 문서에 정리돼 있다.

## 현재 결과

| Lab | Release build | 자동 테스트 | 단계 문서 |
|---|---|---|---:|
| `parts_perf_lab` | 통과 | CTest 9개. 전체 1개 + step별 8개(실행 파일이 첫 인자를 step 필터로 받는다) | 8 |
| `languages_lab` | 통과 | CTest 7개. 전체 1개 + 계약별 6개(step 라벨로 등록표를 거른다). 사례 19개 중 4개가 문법 유도 fuzz 배터리(기본 8만 표본, 고정 시드, 반례 축소) | 6 |
| `algorithms_lab` | 통과 | CTest 11개. 전체 1개 + step별 8개(`algo_tests --step=NN`) + property 층 1개 + bench smoke 1개. 고정 사례 45개(52만 CHECK)가 느린 독립 오라클과 대조하고, property 11종이 6,950 표본을 돈다. 무력화 변이 15건 중 14건 검출 | 8 |
| `math_lab` | 통과 | CTest 10개. 전체 1개 + step별 8개(실행 파일이 첫 인자를 step 필터로 받는다) + property 층 1개. 사례 26개 중 13개가 시드 무작위 property(기본 42,300 표본, 고정 시드, 반례 축소) | 8 |
| `physics_lab` | 통과 | CTest 23개. 실행 파일 전체 9개 + step별 14개(실행 파일이 첫 인자를 step 필터로 받는다). 아홉 번째는 불변식 property 층 — 사례 5개(볼록쌍 1500 표본 · 에너지 소산 · 침투 상계 · Measure⇔Response 프레임 동일 · bit 결정성)를 고정 시드에서 돌리고, 실패하면 입력을 shrink 해 시드·재현 명령과 함께 찍는다 | 8 |
| `ai_lab` | 통과 | CTest 9개. 전체 2개(pathfinder·model) + step별 7개 | 6 |
| `graphics_gl_lab` | 통과 | CTest 12개. 전체 2개(계약·렌더) + step별 10개(실행 파일이 첫 인자를 step 필터로 받는다) | 7 |
| `graphics_dx_lab` | 통과 | CTest 8개. 전체 1개 + step별 7개(실행 파일이 첫 인자를 step 필터로 받는다) | 7 |
| `graphics_vk_lab` | 통과 | CTest 8개. 전체 1개 + step별 7개(실행 파일이 첫 인자를 step 필터로 받는다) | 7 |
| `game_network_lab` | 통과 (Windows IOCP + WSL2 Ubuntu 26.04 epoll) | CTest 12개. 실행 파일 전체 5개 + step 단위 7개(backend는 플랫폼마다 하나만 등록) | 6 |
| `live_service_lab` | 통과 | step 단위로 나뉜 검사 18개(step01 3 · step02 4 · step03 6 · step04 3 · step05 2). step05 둘은 API를 loopback에 띄워 status code를 읽는다 | 5 |
| `db_lab` | 통과 | CTest 5개. 전체 3개(단위·크래시 스윕·bench smoke) + step별 2개(`db_tests --step=NN`) | 4 |
| `system_kernel_lab` | 통과 (Linux 전용 — WSL2 Ubuntu 26.04) | CTest 25개. 전체 8개(실행 파일 3 + strict 1 + bench 4) + step별 17개(`--step=NN` 필터). 5개 backend가 실제로 실행됨을 호출 카운터로 확인 | 8 |

전체 스크립트의 마지막 검사는 C# Live Service의 실제 game token 두 개를 C++ Mega 전용 서버가
검증하고 서로 다른 UDP 끝점 두 곳에 snapshot을 보내는 교차 언어 process smoke다.

### property·fuzz 층 (2026-08-23 독립 재검증)

`math_lab` · `physics_lab` · `languages_lab` 셋은 `algorithms/part11_property_fuzz_verification.md`의
실행 자산이기도 하다. 어느 절이 어디서 도는지는
[`algorithms/README.md`의 실행 자산 표](../../algorithms/README.md#part11의-실행-자산--어느-lab에서-도는가)에 있다.

| 확인 | 결과 |
|---|---|
| 클린 빌드(캐시 없는 새 디렉터리, MSVC Release `/W4 /permissive- /utf-8`) | 제품·테스트 소스 경고 **0**. math·physics의 raylib/raygui/glfw 서드파티 경고(각 43 · 203)는 전부 `_deps/` 소재 |
| ctest 전수 · 연속 3회 | math 10/10 · physics 23/23 · languages 7/7 — 3회 모두 동일 |
| 배터리 출력 반복 동일 | 세 실행 파일 모두 3회 실행 출력이 **바이트 동일** |
| 무력화 변이 | physics EPA 깊이 ×1.02 → `step05.property` 사망 · math SAT 후보축 4→3 → `collision/sat-gjk-agree` 사망 · languages 실행 예산 ×1000 → `step04` 정지 실패로 CMake `TIMEOUT`이 사망 판정. 셋 다 복원 뒤 SHA-256 동일 |
| 반례 재현 | 출력된 재현 명령(`physics_property_tests convex_pair <seed>` · `math_core_tests step05 --only=... --seed=... --sample=...`)을 그대로 실행해 같은 반례·같은 축소 입력·종료 코드 1 확인 |

### algorithms_lab 검증 근거 (2026-08-24 신설)

이 lab은 자체 property 하네스(`tests/property.h`)를 가진다. 위 세 lab의 규약(고정 시드 ·
생성기 · 축소기 · 재현 명령)을 그대로 따르되 파라미터가 이산이라 정수 domain을 쓴다.

| 확인 | 결과 |
|---|---|
| 클린 빌드(MSVC Release `/W4 /permissive- /utf-8`) | 제품·테스트 소스 경고 **0**. 서드파티 의존 없음 |
| ctest 전수 · 연속 3회 | 11/11 — 3회 모두 동일, 총 2.5초 안팎 |
| 배터리 출력 반복 동일 | `algo_tests` · `algo_tests --properties` 3회 실행 출력이 **바이트 동일**. `algo_bench --smoke`는 벽시계 수치를 찍으므로 바이트 동일일 수 없다 — 실수 칸(ms·MB/s)을 마스킹하면 3회 출력이 일치하고, 정수 카운터(비교·이동 바이트·방문·probe·증가 횟수)와 `checksum` 줄은 3회 모두 같다 |
| 무력화 변이 | 15건 주입 → **14건 검출**. 생존 1건은 `HuffmanDecompress`의 Kraft 호출 제거로, 같은 손상을 Adler-32가 잡기 때문이다(Kraft는 거부를 만드는 것이 아니라 더 싸게 만든다). 15건 전부 복원 뒤 SHA-256 동일 |
| 검사가 잡지 못하는 것 | DSU의 union by size 제거, Dijkstra의 lazy deletion 처리 제거는 정확성 검사를 죽이지 않는다. 최적화는 답을 바꾸지 않기 때문이고, `algo_bench`의 평균 find 깊이(1.00 대 9,999.50)와 확장 수(1,200 대 3,322)가 대신 드러낸다 |
| 벤치의 자체 검사 | 각 팔이 답 대조·불변식 확인을 통과하지 못하면 표에 오르기 전에 프로세스가 죽는다(`algo_bench_smoke`가 그 경로를 매 ctest 실행에서 지나간다) |

## 남은 완료 조건

GUI lab은 장면을 직접 실행해 화면과 상호작용을 확인해야 한다. 현재 headless
CTest는 다음 핵심 계약을 검사한다.

- `algorithms_lab`: 정렬의 순서·multiset 보존·안정성 세 계약, 순서 보존 실수 키의 단조성,
  lazy tag의 apply/compose 대수와 결합법칙, open addressing의 조회 종료 불변식(삭제 정책 3종),
  Aho-Corasick의 일치 multiset, condensation이 DAG임, 절단점·다리를 정점/간선 제거 후 BFS
  재실행과 대조, DSU 연결 판정 대 BFS 성분, Dijkstra·Bellman-Ford 대 Floyd-Warshall,
  전수 열거 최적값 대 DP·분기 한정, max-flow == min-cut certificate와 유량 보존,
  CSP의 Apply/Undo 상태 해시와 가지치기 해 집합 보존, Kraft 부등식과 H<=L<H+1,
  왕복·절단·손상·자원 상한에 대한 복호기 거부
- `parts_perf_lab`: allocator 정렬·해제와 simulation 모드 계약
- `languages_lab`: 토큰 위치, 우선순위·결합, 바이트코드 검증기, 실행 예산·스택 상한,
  호스트 바인딩. 여기에 더해 문법 유도 생성기와 토큰·명령 변형기로 도는 property 배터리가
  컴파일 결정성, 검증기의 닫힌 거부 어휘, 실행 결정성과 자원 상한, 진단 어휘·위치를
  고정 시드 8만 표본에서 검사한다(`--seed`·`--samples`·`--only`로 확장·재현)
- `math_lab`: 행렬·쿼터니언·교차 판정·수치 안정성. 여기에 더해 구성적 생성기와 축소기로 도는
  property 층이 쿼터니언 항등식·이중 덮개·등각속도, Perlin/Simplex 범위 상계(그라디언트 조합
  256 가지를 세는 독립 오라클), SAT⇔GJK 일치와 분리면 실증·강체 불변, 역행렬 왕복의 조건수
  규칙을 고정 시드 42,300 표본에서 검사한다(`--seed`·`--scale`·`--only`·`--sample`로 확장·재현)
- `physics_lab`: chrome 레이아웃 분할, 적분기 차수·에너지 거동, force 항과 종단 속도,
  임펄스 클램프, AABB/PBD, SAT·GJK/EPA, 결정성과 Response 솔버 동일성
- `ai_lab`: 경로 비용, flow field, 반사 경계, boid 속도, influence 결합
- `graphics_gl_lab`: draw 제출 수, 바인딩 비용, mip 저장량 계산, FrameTimer EMA·히스토리 링,
  KHR_debug 원형 버퍼, 그리고 실제 컨텍스트에서 세 드로우 모드 픽셀 해시·셰이더 perm 구성·
  Rebuild 핸들 수명·MSAA 폴백
- `graphics_dx_lab`: draw 제출 수, descriptor 수명 계획, barrier 전이 기술
- `graphics_vk_lab`: draw 제출 수 공식, barrier stage/access mask와 기록 경로·실패 주입, timeline 값 증가와
  모드 전환 격리, swapchain 협상, push constant 예산, queue family fallback, descriptor set 모양
  (실제 GPU 제출·present와 validation 메시지 문구는 자동 검증 없음)
- `game_network_lab`: TCP fragmentation·coalescing, sequence wrap, oversized UDP 오류 뒤 IOCP
  receive 복구, server/client handshake와 snapshot, 연결별 송신 queue 상한, backend drain 뒤
  재시작, 고정 틱 catch-up 상한
- `live_service_lab`: access token 위조·만료, match allocation, drain, 중복 result 경로의 server
  소유권, 완료된 match 뒤 재큐잉

GPU 렌더 캡처 회귀, GUI 입력 자동화, Linux 전용 system 랩의 실제 실행은 별도
환경 검증이 필요하다. 자동 테스트가 있더라도 화면 출력과 드라이버별 validation을
대체하지는 않는다.
