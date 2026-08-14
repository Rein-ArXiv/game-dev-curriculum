# 13축 소유권 지도

이 문서는 커리큘럼 13축이 mega 안에서 **어디에 살고, 제품이 실제로 부르는지, 부른다면 어느 게임
기능이 부르는지**를 기록한다. 판정 근거는 주장이 아니라 컴파일러가 남긴 의존성 출력이다.

## 왜 이 문서가 필요했는가

공식 문서 세 개가 서로 다른 말을 하고 있었다.

| 문서 | 하던 말 |
|---|---|
| `PROJECTS_PLAN.md` | 프로젝트의 목적은 "여러 축을 실제 게임으로 통합하는 것" |
| `CURRICULUM_MAP.md` | mega 는 "별개 샘플 묶음이 아니라 하나의 플레이 가능한 멀티플레이 디펜스로 통합한다" |
| `docs/development.md` | "캡스톤의 목표는 모듈 이름을 한 실행 파일에 모두 넣는 것이 아니다" |

앞의 둘을 읽으면 `include/`의 모든 축이 제품 경로에 들어가 있어야 한다. 뒤의 하나를 읽으면
그럴 필요가 없다. 두 해석 아래에서 같은 코드가 동시에 "완성"이고 "미완성"이므로, 미도달 헤더가
쌓여도 어느 문서도 위반되지 않았다. 감사가 헤더 대부분이 제품에서 도달 불가라고 지적했을 때
반박도 시인도 불가능했던 이유가 이것이다.

**어느 쪽이 옳은지는 코드가 정한다.** 측정 결과 `include/` 헤더 99개 중 제품이 도달하는 것은
41개다. 그러므로 `development.md` 쪽이 현재 코드의 상태를 정확히 기술하고 있고, 나머지 두
문서의 "통합"은 **"모든 축을 호출한다"가 아니라 "게임이 요구하는 축을 하나의 플레이 가능한
제품에 모으고, 요구하지 않는 축은 근거와 함께 경계 밖으로 밀어낸다"** 로 읽어야 한다.
그 경계가 이 문서다.

## 판정 기준

"제품이 쓴다"는 취향이 아니라 다음 한 줄로 정의한다.

> 제품 진입점 `games/defense/main.cpp` · `games/defense/server_main.cpp` 를 컴파일할 때
> 컴파일러가 **실제로 연 파일**에 그 헤더가 있는가.

텍스트로 `#include` 를 훑는 근사가 아니라 컴파일러의 의존성 출력(MSVC `/sourceDependencies`)이
근거다. 전처리기 조건부가 정확히 반영되고, 손으로 고칠 수 없다. 헤더 단위의 전체 목록은
빌드가 생성하는 [`PRODUCT_LAYERS.md`](PRODUCT_LAYERS.md)가 소유하며, 도달 불가인데 근거도
없는 제3의 상태는 [`LAB_ONLY.md`](LAB_ONLY.md) 등재를 요구하는 도달성 게이트가 빌드 실패로
막는다. 이 문서는 그 위에 **축 단위의 해석**을 얹는다.

측정값(헤더 99개 기준):

| | 헤더 수 |
|---|---|
| 제품 도달 | 41 |
| 제품 미도달 | 58 |

41개 중 7개는 `include/game/` — 축 라이브러리가 아니라 디펜스 제품 그 자체다. 따라서
**축 라이브러리 92개 중 제품이 부르는 것은 34개**다.

---

## 13축 요약

| 축 | mega 안의 위치 | 제품 사용 | 어느 게임 기능이 쓰는가 |
|---|---|---|---|
| parts | `serialize/` `memory/` `containers/` `ecs/` `jobs/` `sync/` `profiler/` | **부분 (1/13)** | 프로필 레코드와 스냅샷의 바이트 인코딩 |
| system | `io/` `platform/process.*` `sched/` | **핵심 (3/5)** | 외부 에셋 읽기, 서버의 종료 신호, 두 OS 프로세스 |
| network | `net/` `mp/` | **핵심 (7/14)** | 협동 세션 전체 — 명령·ACK·스냅샷·보간 |
| math | `math/` | **핵심 (3/12)** | 좌표·거리·사거리 판정, 형상 경계 상자 |
| physics | `physics/` | **핵심 (3/6)** | 발사체 명중과 광역 피해 판정 |
| graphics | `render/` `platform/vk_present.hpp` | **전부 (5/5)** | 화면 전체 — 타일·스프라이트·HUD·GDI/Vulkan 표시 |
| ai | `ai/` | **없음 (0/9)** | 없다. 타게팅·경로 추종은 `game/game.hpp` 의 도메인 규칙이다 |
| engine | `engine/` | **부분 (2/7)** | 키→액션 매핑, 피격 파티클 |
| languages | `tools/script.hpp` | **전부 (1/1)** | `balance.script` 로 시작 자원·적 배율 계산 |
| db | `db/` | **전부 (3/3)** | 매치 결과와 프로필의 WAL 원자 커밋 |
| live_service | `live/` | **부분 (2/6)** | 전용 서버 입장 토큰 검증, 운영 지표 집계 |
| platform | `platform/` (표시·프로세스 제외) | **부분 (4/8)** | 창·입력, 효과음 출력, 실행 파일·사용자 데이터 경로, 에셋 경로 해석 |
| tools | `tools/` (스크립트 제외) | **없음 (0/3)** | `include/` 에는 없다. 축의 실사용은 빌드·검사 인프라 쪽이다 |

"부분"과 "없음"은 결함이 아니라 **기록된 판정**이다. 판정이 기록되지 않은 상태만 결함이다.

---

## 축별 상세

### parts — 부분 (1/13)

**제품이 쓰는 것.** `serialize/archive.h` 하나. `Serialize(Ar&)` 를 한 번 쓰면 읽기와 쓰기가
같이 나오는 단일 함수 아카이브이며, `db/records.hpp` 의 계정·매치 결과 레코드와
`net/snapshot.hpp` 의 스냅샷 인코딩이 같은 구현을 공유한다. 영속 형식과 wire 형식이 한
구현에서 나온다는 점이 이 축이 제품에 남아 있는 이유다.

**참조 구현으로 남는 것.** `memory/{arena,pool,slab}` · `containers/{hash_map,intrusive_list,
ring_buffer,soa_vector}` · `ecs/world.h` · `jobs/scheduler.h` · `sync/{futex,lock}` ·
`profiler/trace.h`.

**왜 안 쓰는가.** 권위 런타임의 규모가 타워 16 · 적 64 · 발사체 20이다. 이 규모에서 아레나와
풀은 malloc 대비 측정 가능한 차이를 만들지 못하고, 작업 스케줄러의 동기화 비용은 직렬 계산보다
크다. SoA 와 ECS 는 캐시 미스가 병목이 되는 개체 수를 전제하는데 그 전제가 성립하지 않는다.
**측정 근거 없이 호출하지 않는다**는 판단이며, 이 판단이 옳다는 것과 그 코드가 제품에서
검증되지 않는다는 것은 동시에 참이다.

### system — 핵심 (3/5)

**제품이 쓰는 것.** `io/read_file.hpp` → `io/file.h` 가 디스크의 유일한 입구다. `level.txt` ·
`waves.txt` · `balance.script` · PPM 스킨 10장 · Vulkan SPIR-V 셰이더가 전부 이 경로로만
들어온다. `platform/process.hpp` 는 헤드리스 서버의 Ctrl+C · SIGTERM 을 소유해 드레인 종료를
만든다.

축의 나머지 절반은 헤더가 아니라 **실행 형태**로 존재한다. `mega_defense` 와
`mega_defense_server` 는 서로 다른 두 OS 프로세스이고 실제 UDP 로 통신한다. 프로세스 경계는
`tests/test_defense_process.cpp` 가 실제로 두 프로세스를 띄워 검증한다.

**참조 구현으로 남는 것.** `io/async_io.h`(잡 스케줄러 위의 비동기 IO), `sched/affinity.h`.
비동기 IO 는 스트리밍할 월드가 없어 요구가 성립하지 않고, 어피니티는 데드라인이 걸린 스레드가
없어 측정할 대상이 없다.

### network — 핵심 (7/14)

**제품이 쓰는 것.** `net/transport.hpp`(전송 추상) · `udp_transport.hpp`(클라·리슨 호스트의 단일
피어 소켓) · `udp_server.hpp`(전용 서버의 다중 끝점 소켓) · `session.hpp` · `reliable_channel.hpp`
· `sequence.hpp` · `snapshot.hpp`. 협동 플레이의 모든 것이 여기 있다 — 명령의 단조 시퀀스와
재전송, 누적 ACK, 중복 억제, 20Hz 제한 크기 스냅샷, 클라이언트 60Hz 보간.

감사가 지적한 최악의 결함이 정확히 이 축이었다. 제품이 `main.cpp` 익명 네임스페이스에서
복제 버퍼를 재구현했고 `include/net/` 은 테스트만 링크했다. 지금은 제품이 이 헤더들을 직접
링크하며, `games/` 가 소켓 헤더를 인클루드할 수 없게 하는 계층 규칙 검사가 그 재발 경로를 막는다.

**참조 구현으로 남는 것.** `net/packet.hpp`(seq/ack/ackBits 헤더 — 제품은 자체 wire 헤더를
쓴다) · `net/rtt.hpp` · `mp/{det_sim,input,lockstep,matchmaking,rollback}`.

**왜 안 쓰는가.** 호스트 권위 + 스냅샷 복제 모델과 lockstep · rollback 은 같은 문제에 대한
서로 다른 답이다. 둘을 동시에 제품에 넣으면 권위의 원본이 둘이 된다. 2인 로컬 세션에는
매치메이킹도 없다.

### math — 핵심 (3/12)

**제품이 쓰는 것.** `math/vec.hpp`(모든 좌표·속도·방향) · `math/scalar.hpp`(허용 오차 비교와
각도) · `math/spatial/aabb.hpp`(`physics/shape.hpp` 의 Box 경계).

**참조 구현으로 남는 것.** `mat` · `quat` · `transform` · `interp` · `noise` · `sdf` ·
`spatial/{bvh,quadtree,uniform_grid}`.

**왜 안 쓰는가.** 게임이 2D 탑다운이라 회전이 없다. 사원수와 4×4 변환이 표현할 대상이 없다.
공간 분할은 적 64개에서 이득이 없다 — 전수 순회가 더 빠른 구간이다. 절차 노이즈와 SDF 는
지형을 절차 생성하지 않으므로 소비처가 없다.

### physics — 핵심 (3/6)

**제품이 쓰는 것.** `physics/shape.hpp` · `physics/rigid_body.hpp` · `physics/narrow_phase/
collide.hpp`. `game/game.hpp` 가 발사체 명중과 스플래시 피해 후보를 `CollideCircleCircle` 로
판정한다 — 게임 규칙이 물리 모듈의 판정을 그대로 쓴다.

**참조 구현으로 남는 것.** `physics/integrate.hpp` · `physics/solver/sequential_impulse.hpp` ·
`physics/world.hpp`.

**왜 안 쓰는가.** 발사체는 목표의 안정 ID 를 추적하는 규칙 이동이지 힘을 적분하는 물체가
아니다. 접촉 응답이 필요한 적층도, 마찰도, 제약도 게임에 없다.

### graphics — 전부 (5/5)

**제품이 쓰는 것.** 전부다. `render/framebuffer.hpp`(RGBA8 CPU 프레임버퍼) ·
`render/raster2d.hpp`(타일·도형) · `render/sprite.hpp`(PPM 스킨 블릿과 마젠타 투명 키) ·
`render/text.hpp`(HUD 5x7 비트맵 폰트) · `platform/vk_present.hpp`(`--vulkan` 스왑체인 표시).

미도달 헤더가 하나도 없는 축은 graphics · languages · db 셋이며 이 축이 그중 가장 크다.
화면에 나오는 모든 픽셀이 이 다섯 파일을 지난다.

**mega 가 소유하지 않는 부분.** 커리큘럼의 graphics 축은 mega 가 흡수하는 것보다 훨씬 넓다.
GL · DirectX API 트랙, 레이트레이싱, 고급 GI 는 `projects/mini/` 의 그래픽스 랩이 소유한다.
2D 소프트웨어 래스터 + Vulkan 표시는 이 게임이 요구하는 만큼이고, 그 이상을 여기에 넣을
게임플레이 근거가 없다.

### ai — 없음 (0/9)

**제품이 쓰는 것.** 없다. `ai/` 아래 아홉 헤더 전부 제품에서 도달하지 않는다.

**제품의 AI 는 어디 있는가.** `game/game.hpp` 안이다. 타워 타게팅(Nearest · First · Strongest)은
같은 적 집합에 서로 다른 점수 함수를 적용하는 도메인 규칙이고, 적 이동은 검증된 무분기 경로를
따라가는 경로 추종이다. 둘 다 게임 규칙의 일부이지 범용 AI 라이브러리의 소비가 아니다.

**참조 구현으로 남는 것.** `ai/bt/*` · `ai/fsm/*` · `ai/pathfinding/{astar,flow_field,grid_map}` ·
`ai/perception/*` · `ai/steering/*`.

**왜 안 쓰는가.** A* 는 경로가 이미 레벨 파일에 무분기로 정의돼 있어 탐색할 것이 없고,
플로우 필드는 목표를 공유하는 에이전트 수가 임계에 한참 못 미친다. 행동 트리는 상태가 "경로를
따라 걷는다" 하나뿐인 적에게 노드 트리를 얹는 일이다. 지각은 시야·엄폐 개념이 게임에 없다.
**이름을 쓰려고 같은 계산을 두 번 하지 않는다**는 것이 판정이며, 이 축은 13축 중 mega 의 현재
게임과 가장 멀다.

### engine — 부분 (2/7)

**제품이 쓰는 것.** `engine/input.hpp`(물리 키를 액션으로 바꾸고 눌림 edge 를 프레임마다 비워
키를 누르고 있는 동안 강화가 반복되지 않게 한다) · `engine/particles.hpp`(명중 시 파티클 —
권위 상태를 수정하지 않는 렌더 전용이라는 방향성이 테스트로 강제된다).

축의 **프레임 오케스트레이션**은 헤더가 아니라 `game/defense_app.hpp` 의 `DefenseApp::Step()`
이 소유한다. 고정 60Hz 시뮬과 가변 렌더의 분리, 한 프레임 최대 5틱 보충이 그 안에 있고,
창 프런트엔드와 헤드리스 셀프테스트가 **같은 Step 을** 부른다.

**참조 구현으로 남는 것.** `engine/{anim,skeleton,ik}`(2D 스프라이트 게임에 본도 클립도 없다) ·
`engine/audio.hpp`(3D 감쇠·패닝 믹서 — 제품은 `platform/audio_device.hpp` 의 단순 효과음
출력만 쓴다) · `engine/resource.hpp`(핸들·레퍼런스 카운트 — 에셋이 시작 시 한 번 로드되고
언로드되지 않아 수명 관리가 필요 없다).

### languages — 전부 (1/1)

**제품이 쓰는 것.** `tools/script.hpp`. 렉서 → 파서 → AST → 트리 워킹 인터프리터이며,
`assets/balance.script` 가 시작 골드 · 코어 체력 · 적 체력/속도/보상 배율을 계산한다. 파일은
64KiB 로 제한하고, 파싱·실행 성공 뒤에도 각 출력의 유한성과 범위를 검사해 하나라도 실패하면
다섯 값을 전부 기본 구성으로 되돌린다.

**mega 가 소유하지 않는 부분.** 바이트코드 VM · GC · JIT · ABI 는 `languages_lab` 이 소유한다.
현재 스크립트는 호스트 초기화 때 한 번 실행되므로 인터프리터 실행 비용이 프레임 예산에
나타나지 않는다 — 바이트코드로 바꿀 측정 근거가 없다.

### db — 전부 (3/3)

**제품이 쓰는 것.** 전부다. `db/kv_store.hpp`(WAL 기반 트랜잭셔널 KV) · `db/records.hpp`(프로필 ·
매치 결과) · `db/repository.hpp`. 매치 종료 정산이 프로필 갱신과 매치 기록을 **한 WAL
트랜잭션**에 넣고 마지막 COMMIT 마커 뒤 fsync 한다. 복구는 커밋된 완전한 접두사만 재생하므로
잘린 마지막 트랜잭션이 부분 적용되지 않는다.

미도달 헤더가 없는 세 축 중 하나이며, 게임 규칙("결과가 남아야 한다")이 축을 직접 요구한
경우다.

**mega 가 소유하지 않는 부분.** 샤딩 · 복제는 `db_lab` 이 소유한다. 실 트래픽이 없다.

### live_service — 부분 (2/6)

**제품이 쓰는 것.** `live/admission_token.hpp` — 전용 서버가 HMAC-SHA256 게임 토큰의 서명 ·
종류 · `ServerId` · 만료 · 중복 결합 · 매치 정원을 오프라인 검증한 뒤에만 UDP 끝점을 승인한다.
`live/analytics.hpp` — 프레임 시간 히스토그램, 원격 명령 카운터, 스냅샷 수·크기 집계.

**참조 구현으로 남는 것.** `live/{economy,event_schedule,feature_flag,patch}`.

**왜 안 쓰는가.** 운영 대상이 되는 라이브 서비스가 없다. 통화 유입·유출 경로도, 기간 한정
이벤트도, 롤아웃할 모집단도, 배포된 클라이언트 버전도 존재하지 않는다. 코드는 있으나 이
게임에서 켤 스위치가 없다.

### platform — 부분 (4/8)

**제품이 쓰는 것.** `platform/window.hpp`(창 · 키보드 · 마우스 · 프레임버퍼 blit 을 소유하는
유일한 계층. `HWND` 를 `void*` 로 돌려주므로 게임 코드는 그 타입을 모른다) ·
`platform/audio_device.hpp`(waveOut 효과음) · `platform/paths.hpp`(실행 파일 위치 · 사용자 데이터
위치 · 환경 변수) · `platform/vfs.hpp`(논리 경로 → 물리 위치).

`platform/vk_present.hpp` 는 graphics 축, `platform/process.hpp` 는 system 축으로 세었다 —
디렉터리가 아니라 축이 소유의 단위이기 때문이다.

**참조 구현으로 남는 것.** `platform/{byteorder,capability,console,detect}`.

**왜 안 쓰는가.** 이 축의 관측 장치는 **CI 매트릭스**인데 현재 타깃이 Windows 하나다. 크로스
OS 접속이 없으므로 엔디안 정규화가 실패할 경로가 없고, 능력 탐지가 바꿀 규모 파라미터도 없다.
축이 미달인 것이 아니라 **축을 측정할 두 번째 플랫폼이 아직 없다.**

### tools — 없음 (0/3)

**제품이 쓰는 것.** `include/tools/` 안에는 없다. `tools/script.hpp` 는 위에서 languages 축으로
세었으므로, `asset_pipeline` · `level_editor` · `procgen` 셋 다 도달하지 않는다.

**왜 안 쓰는가.** 에셋이 텍스트 5종과 PPM 10장이라 증분 베이크가 절약할 시간이 없다. 절차
생성은 레벨이 손으로 쓴 파일이라 소비처가 없다. `tools/level_editor.hpp` 는 `.`·`#`·`S`·`G`
타일에 대해 **도달 가능성**을 flood fill 로 검사하는데, 제품의 레벨 검증은 `S`·`P`·`C`·`.`·`#`
타일에 대해 **무분기 단일 경로**라는 더 강한 불변식을 요구한다. 알파벳도 불변식도 다르므로
제품의 검증기가 아니다.

**축의 실사용은 어디 있는가.** `include/` 밖이다. tools 축이 소유하는 것 — 빌드 · 검증 도구 —
은 `CMakeLists.txt` 와 `scripts/check_layers.py` · `scripts/reachability.py` 가 실제로 수행하고
있다. 헤더가 아니라 인프라의 형태로 존재하는 유일한 축이다.

---

## 이 문서를 무엇이 지키는가

이 문서는 손으로 쓴 해석이므로 코드와 어긋날 수 있다. 어긋나지 않게 하는 것은 문서가 아니라
빌드다.

- **도달성 게이트** — `include/` 의 모든 헤더는 제품에서 도달하거나 [`LAB_ONLY.md`](LAB_ONLY.md)에
  이유와 함께 등재돼야 한다. 제3의 상태는 빌드 실패다. 위에서 "참조 구현"으로 적은 것은
  그 등재 목록과 같은 집합이어야 한다.
- **도달성 리포트** — [`PRODUCT_LAYERS.md`](PRODUCT_LAYERS.md)는 손으로 쓰지 않고 빌드가
  생성한다. 헤더 단위의 사실은 그쪽이 소유하고, 이 문서는 축 단위의 해석만 소유한다.
- **계층 규칙 검사** — `scripts/check_layers.py` 가 `games/` 의 OS API 직접 호출과 150줄 초과
  진입점을 막는다. 이것이 없으면 제품은 축을 부르는 대신 진입점에서 다시 구현할 수 있고,
  실제로 그런 적이 있다.

축 하나를 제품에 새로 들이는 절차는 다음과 같다. 게임 기능이 그 축을 요구하는 근거를 먼저
적고, 제품 경로에서 호출하고, 도달성 리포트에서 그 헤더가 `PRODUCT` 로 넘어온 것을 확인하고,
이 문서의 해당 행을 고친다. **순서를 뒤집어 "쓰이게 하려고" 호출하지 않는다.**
