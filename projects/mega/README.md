# Mega Defense

게임 개발 CS 커리큘럼의 개념을 하나의 완성 코드베이스에서 검증하는 C++20 협동 타워 디펜스다.
학습 단계별 소스 복사본이나 다른 장르 데모를 보관하지 않는다. 구현 과정은
[`docs/development.md`](docs/development.md)에 기록하고, 프로젝트에는 최종 코드만 둔다.

## 현재 구현

- 실제 조작 가능한 2D 타워 디펜스: 4종 타워, 5종 적, 5개 웨이브, 강화·판매·타게팅
- 명시적 타일 맵: 건설 가능·경로·스폰·코어·차단 셀, 셀 스냅과 중복 배치 금지
- 외부 레벨·웨이브 검증: 맵과 난이도 구성을 재빌드 없이 교체하고 파일 전체를 원자적으로 폴백
- 밸런스 스크립트: 시작 골드·코어 체력·적 배율을 제한된 DSL로 계산하고 범위 밖 결과를 전체 폴백
- 액션 입력과 렌더 전용 VFX: 물리 키를 게임 명령에서 분리하고 파티클이 권위 상태를 수정하지 않음
- 이동 표적 발사체: 안정 ID로 목표를 추적하고 공용 원 충돌 판정으로 명중·광역 피해를 계산
- 실제 2인 네트워크: 독립된 호스트·클라이언트 프로세스가 IPv4 UDP로 같은 세계를 공유
- 전용 권위 서버: 렌더 없는 60Hz 프로세스가 서로 다른 UDP 끝점의 두 플레이어를 수용
- 라이브 서비스 입장: HMAC-SHA256 게임 토큰의 서버 audience·만료·재사용·매치 경계 검증
- 호스트 권위: 클라이언트는 명령만 전송하고 호스트가 맵·경제·게임 상태를 검증
- 피어 승인: magic·버전·길이가 유효한 Hello 뒤에만 호스트가 원격 주소를 고정
- 명령 신뢰성: 단조 시퀀스, 재전송, 누적 확인, 중복 억제
- 상태 복제: 20Hz 제한 크기 스냅샷, 안정 엔티티 ID, 클라이언트 60Hz 보간
- 외부 스킨: PPM 이미지 10개를 런타임에 교체, 누락·손상 시 내장 스프라이트 폴백
- 표시 백엔드: Win32 GDI 또는 같은 실행 파일의 Vulkan 스왑체인(`--vulkan`, Vulkan SDK가
  있는 빌드에서만). 어느 쪽이든 그리기는 CPU 소프트웨어 래스터라이저가 하고 백엔드는
  그 결과를 표시만 한다
- 영속 진행도: 매치 결과와 누적 프로필을 하나의 WAL 트랜잭션으로 커밋
- 운영 지표: 프레임 시간 p95, 원격 명령 수, 스냅샷 수·바이트 p95/최댓값·잘림 횟수를
  집계하고 종료 시 한 줄로 출력

검증 환경은 MSVC 19.51 Release다. 실제 로컬 UDP 왕복, 독립 호스트·클라이언트
프로세스 왕복, 전용 서버와 두 클라이언트의 토큰 입장, 손실·지연·재정렬 복구,
반복 매치 soak를 자동 검증한다.

Vulkan 스왑체인 5프레임 present도 ctest 대상(`mega_defense_gpu_selftest`)이다. 다만 이
하나는 실제 GPU와 창이 있어야 돌므로, 없는 환경에서는 실패가 아니라 **SKIP**으로
보고한다(종료 코드 77 + CTest `SKIP_RETURN_CODE`). 즉 "이 머신에서 GPU 경로가 검증됐다"는
`ctest`가 이 항목을 Passed로 셌을 때만 참이고, Skipped면 검증되지 않은 것이다.

## 빠른 시작

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\bin\Release\mega_defense.exe
```

C++ 컴파일러 외에 **Python 3**가 PATH에 있어야 한다. 계층 규칙 검사와 도달성 게이트가
`scripts/`의 Python 스크립트이고, 둘 다 끌 수 있는 옵션이 아니기 때문이다 — 끌 수 있는
게이트는 게이트가 아니다. 없으면 구성 단계에서 이유를 적고 멈춘다.

Live Service와의 실제 token 직렬화·HMAC·UDP 입장 호환성은 저장소 루트에서 다음 명령으로
재현한다.

```powershell
pwsh -File scripts/check_live_mega.ps1
```

플레이 가능한 클라이언트는 `build/bin/Release/mega_defense.exe`, 렌더 없는 권위 프로세스는
`mega_defense_server.exe`다. 둘은 같은 게임 규칙·콘텐츠·패킷 형식을 링크한다. CMake는 실행
파일 옆에 `assets/`와 클라이언트용 Vulkan 셰이더를 복사한다.

### 협동 플레이

호스트:

```powershell
.\build\bin\Release\mega_defense.exe --host 27015
```

클라이언트:

```powershell
.\build\bin\Release\mega_defense.exe --join 127.0.0.1 27015
```

다른 PC에서는 `127.0.0.1` 대신 호스트의 사설 또는 공인 IPv4 주소를 사용한다. 방화벽에서
선택한 UDP 포트를 허용해야 한다. 현재 토폴로지는 호스트 1명과 원격 클라이언트 1명이다.

### 전용 서버 협동 플레이

`live_service_lab`의 서버 등록 `ServerId`와 전용 서버의 `--server-id`가 같아야 한다.
로컬 개발 기본 비밀은 두 프로젝트가 공유하지만 외부에 노출해서는 안 된다.

```powershell
.\build\bin\Release\mega_defense_server.exe --port 27016 --server-id mega-local-1
.\build\bin\Release\mega_defense.exe --join 127.0.0.1 27016 --token <PLAYER_1_GAME_TOKEN>
.\build\bin\Release\mega_defense.exe --join 127.0.0.1 27016 --token <PLAYER_2_GAME_TOKEN>
```

두 토큰은 라이브 서비스가 같은 `MatchId`, `ServerId`로 각 플레이어에게 발급한 값이어야 한다.
서버는 두 명이 승인된 뒤 시뮬레이션을 시작한다. 배포 비밀은 32바이트 이상의 무작위 값을
Base64로 인코딩해 양쪽 프로세스의 `LIVE_SERVICE_TOKEN_SECRET`에 동일하게 주입한다.

### Vulkan 표시

Vulkan SDK와 런타임 드라이버가 있으면 같은 실행 명령에 `--vulkan`을 붙인다.

```powershell
.\build\bin\Release\mega_defense.exe --vulkan
.\build\bin\Release\mega_defense.exe --host 27015 --vulkan
.\build\bin\Release\mega_defense.exe --gpu-selftest
```

Vulkan은 게임 시뮬레이션을 바꾸지 않는다. CPU 프레임버퍼를 텍스처에 업로드하고 풀스크린
삼각형으로 스왑체인에 표시한다. SDK가 없으면 GDI 경로만 빌드된다.

`--gpu-selftest`는 `ctest`가 `mega_defense_gpu_selftest`로 실행하는 것과 같은 명령이다.
Vulkan 빌드가 아니거나, 창 백엔드가 없거나, 장치·셰이더를 준비하지 못하면 이유를 적고
종료 코드 77로 끝난다 — 그 경우 ctest는 실패가 아니라 Skipped로 센다. 실패는 장치도 창도
있는데 present가 깨졌을 때뿐이다.

## A/B 스위치

같은 역할의 구현이 둘 있는 자리는 하나를 지우지 않고 런타임에 고른다. 기본값은 언제나
그 스위치가 생기기 전의 동작이라, 스위치를 켜지 않은 실행은 예전과 같은 결과를 낸다.

```powershell
.\build\bin\Release\mega_defense.exe --ab entity_store=soa --ab jobs=4 --selftest
$env:MEGA_AB = "frame_alloc=arena,body_alloc=pool"; .\build\bin\Release\mega_defense.exe --selftest
```

| 이름 | 값(기본값이 앞) | 무엇이 갈리는가 |
|---|---|---|
| `entity_store` | `aos` \| `soa` \| `ecs` | 적 저장이 구조체 배열인가, 고정 열 배열인가, 조합별 archetype 컬럼인가 |
| `id_map` | `linear` \| `hash` | id → 행 대응이 전수 스캔인가 해시 조회인가 |
| `frame_alloc` | `malloc` \| `arena` | 프레임 스크래치가 힙인가 되감는 아레나인가 |
| `blob_alloc` | `malloc` \| `slab` | 맵·경로 버퍼가 힙인가 크기 클래스 freelist 인가 |
| `body_alloc` | `vector` \| `pool` | 발사체가 벡터인가 주소가 고정된 블록 풀인가 |
| `worker_lock` | `std` \| `futex` | 스케줄러 잠금이 표준 라이브러리인가 futex 직접 구현인가 |
| `asset_io` | `sync` \| `async` | 매치 중 리로드를 프레임 스레드가 하는가 워커가 하는가 |
| `scale` | `small` \| `large` | 동시 생존 적 상한 64인가 2048인가 |
| `map` | `classic` \| `procgen` | 맵이 `assets/level.txt` 인가 시드에서 생성한 41x41 아레나인가 |
| `map_seed` | `1`..`8` | 절차 맵의 시드(`map=procgen` 일 때만 읽는다) |
| `camera` | `ortho2d` \| `tilted3d` | 화면 좌표 = 월드 좌표인가, 원근 투영 + 역투영 픽킹인가 |
| `target_index` | `linear` \| `grid` \| `quadtree` | 사거리 안 적 질의가 전수 스캔인가 균일 격자인가 쿼드트리인가 |
| `static_query` | `naive` \| `bvh` | 바위 영역·사격선 질의가 전수 순회인가 BVH 인가 |
| `broadphase` | `naive` \| `grid` | 잔해 강체의 광역 탐색이 전수 짝짓기인가 균일 격자인가 |
| `integrator` | `symplectic` \| `explicit` | 잔해가 갱신 후 속도로 옮기는가 갱신 전 속도로 옮기는가 |
| `boss_render` | `sprite` \| `rig` | 보스를 스프라이트 한 장으로 그리는가, 본 계층 + 클립 + 2본 IK 로 푸는가 |
| `solver_iters` | `4` \| `8` \| `16` | 접촉 솔버 속도 반복 횟수(결과를 바꾸는 파라미터 노브) |
| `jobs` | `0` \| `1` \| `2` \| `4` \| `8` | 잡 워커 수(0 = 직렬) |
| `affinity` | `0` \| `1` | 워커를 코어에 고정하는가 |

출처는 셋이고 뒤가 앞을 덮는다: 기본값 → 환경 변수 `MEGA_AB` → 명령줄 `--ab`. 알 수 없는
이름이나 값은 **거절**한다(종료 코드 2) — 오타 난 스위치가 무시되면 두 팔이 같은 코드를
돌면서 "차이가 없다"는 거짓 결론이 나온다.

판정은 두 단계다. (a) 두 팔의 상태 해시가 같다는 단언, (b) 두 팔의 비용 수치. (a) 없는 (b)는
의미가 없다 — 빨라진 쪽이 다른 답을 냈을 수 있기 때문이다. `--trace-out PATH` 는 프레임·넷
펌프·렌더·시뮬 시스템별 구간 시간을 TSV 로 남긴다.

## 조작

| 입력 | 동작 |
|---|---|
| `1`~`4` | Cannon, Gatling, Splash, Sniper 선택 |
| 빈 건설 셀 클릭 | 선택한 타워 건설 |
| 타워 클릭 | 타워 선택 |
| `U` | 선택 타워 강화 |
| `X` | 선택 타워 판매 |
| `T` | Nearest → First → Strongest 타게팅 순환 |
| `P` | 싱글플레이 tactical pause; 일시정지 중에도 건설·강화 가능 |
| `R` | 호스트 또는 싱글플레이의 종료된 매치 재시작 |
| `L` | 스프라이트 다시 읽기(매치 중 핫 리로드) |
| `Esc` | 종료 |

협동 세션에서 양쪽은 같은 골드를 공유한다. 클라이언트 명령은 호스트가 승인한 스냅샷이
돌아온 뒤 화면에 확정된다.

## 에셋과 레벨

`games/defense/assets/`의 파일은 재빌드 없이 교체할 수 있다.

- `tower_*.ppm`, `enemy_*.ppm`, `core.ppm`: PPM P3 스프라이트
- `level.txt`: `S` 스폰, `P` 경로, `C` 코어, `.` 건설 가능, `#` 차단
- `waves.txt`: 한 줄마다 `count hp speed reward interval enemy_type`
- `balance.script`: 시작 자원과 적 체력·속도·보상 배율을 계산하는 제한된 스크립트
  (실행 스텝 예산 10⁶ · 중첩 깊이 64 — 끝나지 않는 스크립트도 진단을 남기고 기본값으로 흡수된다)
- `boss.pose`: 보스의 걷기·내려찍기 키프레임. 한 줄이 `key <시각> <몸통 들림> <몸통 롤> <고관절 x4>`
  이고, 마디 길이와 부모 사슬은 코드가 소유한다 — 파일이 만들 수 있는 최악은 이상한 걸음이지
  깨진 골격이 아니다. 이 파일은 표시 데이터가 아니라 **시뮬 입력**이다(발 좌표가 스톰프의 원점)

순수 마젠타 `(255, 0, 255)`는 투명 키다. 이미지 최대 크기는 256x256이며 모든 채널을
검증한다. 레벨은 직사각형이고 `S`와 `C`가 각각 하나여야 하며, 모든 `P`가 하나의
무분기 경로를 이뤄야 한다. 검증 실패 시 안전한 기본 맵을 사용한다.
웨이브 파일은 최대 100줄이며 수치 범위와 적 종류를 전부 검증한다. 한 줄이라도
잘못되면 일부만 섞지 않고 기본 5개 웨이브 전체로 폴백한다.

프로필 WAL은 Windows에서 `%LOCALAPPDATA%/MegaDefense/profile.wal`에 저장된다.

## 코드 구조

```text
mega/
  CMakeLists.txt
  include/
    game/                 게임 규칙, 맵, 프로토콜, 콘텐츠 로더, 진행도, 프레임 루프
    net/                  loopback 테스트 백엔드, 실제 UDP, 신뢰 채널, 스냅샷
    render/               프레임버퍼, 래스터, 스프라이트, 비트맵 폰트
    io/                   파일 핸들(positioned IO)과 전체 읽기 — 디스크의 유일한 입구
    platform/             창·오디오 장치·Vulkan 표시·경로·신호·VFS — OS 의 유일한 입구
    db/ serialize/ live/  영속(WAL·아카이브)과 운영 지표
    math/ physics/ ai/    수학·시뮬레이션 라이브러리
    memory/ containers/   할당자와 자료구조
    ecs/ jobs/ sched/     엔티티·작업·스케줄링
    sync/ profiler/       동기화 프리미티브와 계측
    engine/ tools/ mp/    도메인 엔진·도구·멀티플레이어 동기화 라이브러리
  scripts/check_layers.py   계층 규칙 검사(구성 시 + 빌드마다 + ctest)
  scripts/reachability.py   도달성 게이트 + `docs/PRODUCT_LAYERS.md` 생성(빌드마다 + ctest)
  games/defense/          진입점 2개(각 30줄 미만) + 에셋 + 셰이더
  tests/                  엔진·게임·프로토콜·실소켓 테스트
  docs/                   설계와 구현 과정
```

**이 트리는 손으로 관리하지 않는다.** `include/` 아래 디렉터리 집합과 위 트리가 다르면
`scripts/reachability.py`가 빌드와 `reachability_gate` 테스트에서 실패시킨다. 실재하는
디렉터리가 빠져도, 없어진 디렉터리가 남아 있어도 마찬가지다 — 트리를 보고 소스를 찾는
독자에게 누락된 계층은 존재하지 않는 것과 같기 때문이다.

**계층 규칙.** `games/`는 OS API(소켓·창·파일·오디오·GPU·신호·환경 변수)를 직접
부를 수 없고, 게임 진입점은 150줄을 넘을 수 없으며, `games/` 아래에는 헤더를 둘 수 없다.
`scripts/check_layers.py`가 구성 시와 매 빌드와 `layer_rules` 테스트에서 이를 검사해
위반하면 빌드를 실패시킨다. 그래서 진입점에는
인자 파싱과 `game::RunDefense()` 호출만 남는다 — 프레임 루프는
`include/game/defense_app.hpp`의 `DefenseApp::Step()`이고, `mega_defense --selftest`는
창 없이 **그 루프를 그대로** N틱 구동한다(에셋 로드·명령 경로·고정 스텝 시뮬·
소프트웨어 렌더·진행도 커밋이 전부 실제로 실행된다).

**도달성 규칙.** `include/` 아래 모든 헤더는 제품에서 도달 가능하거나
[`docs/LAB_ONLY.md`](docs/LAB_ONLY.md)에 이유와 함께 등재돼야 한다. 도달 불가인데 근거도
없는 제3의 상태는 빌드 실패다. 판정은 `scripts/reachability.py`가 하고, 입력은 제품
타깃이 컴파일될 때 컴파일러가 직접 남긴 의존성 출력(MSVC `/sourceDependencies`,
GCC/Clang `-MD`)이다 — 텍스트로 `#include`를 훑은 추정치가 아니라 컴파일러가 실제로 연
파일 목록이다. 매 빌드와 `reachability_gate` 테스트에서 돈다.

**제품이 호출하는 계층 목록은 손으로 쓰지 않는다.** 같은 게이트가
[`docs/PRODUCT_LAYERS.md`](docs/PRODUCT_LAYERS.md)를 빌드마다 생성하므로 그 문서는 현실과
어긋날 수 없고, 어긋나기 전에 빌드가 먼저 실패한다. 도달하지 않는 헤더가 왜 제품에
없는지 — 어느 단계에서 어떤 게임 기능이 그것을 부르게 되는지, 아니면 제품 편입 계획이
없는 참조 구현인지 — 는 `docs/LAB_ONLY.md`가 헤더 하나 단위로 소유한다.
커리큘럼 13축 각각이 어디에 살고 제품이 쓰는지 아니면 참조 구현인지는
[`docs/AXIS_OWNERSHIP.md`](docs/AXIS_OWNERSHIP.md)가, 모듈 단위의 소유 관계와 불변식은
[`docs/architecture.md`](docs/architecture.md)가 소유한다.

## 네트워크 범위

현재 구현은 학습 가능한 실제 2인 세션이다.

- 지원: 실제 UDP, listen host 또는 2인 전용 서버, 권위 명령, 재전송·확인, 제한 크기
  스냅샷, 보간, HMAC 입장 토큰
- 미지원: NAT traversal, relay, UDP payload 암호화·패킷별 인증, 재접속, 악성 패킷
  rate limiting, delta snapshot, AOI, 자동 서버 기동·heartbeat·결과 제출

따라서 입장 토큰이 있다고 해서 그대로 인터넷 공개 서비스가 되는 것은 아니다. 현재 실행
범위는 로컬호스트·LAN·보호된 배포망·직접 포트 포워딩 환경의 협동 게임이다.

## 문서

- [아키텍처와 불변식](docs/architecture.md)
- [13축 소유권 지도](docs/AXIS_OWNERSHIP.md)
- [제품이 호출하는 계층](docs/PRODUCT_LAYERS.md) — 빌드가 생성한다
- [랩 전용 헤더 등재부](docs/LAB_ONLY.md) — 제품에 없는 헤더와 그 이유
- [구현 과정](docs/development.md)
- [구축 서사](articles/) — 이 게임이 단계별로 어떻게 만들어졌는지의 기록(step01~step19)

## 라이선스

MIT. 자세한 내용은 [`LICENSE`](LICENSE)를 참조한다.
