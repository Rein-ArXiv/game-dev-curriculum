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

**어느 쪽이 옳은지는 코드가 정한다.** 측정 결과 `include/` 헤더 110개 중 제품이 도달하는 것은
85개다. 그러므로 `development.md` 쪽이 현재 코드의 상태를 정확히 기술하고 있고, 나머지 두
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

측정값(헤더 110개 기준):

| | 헤더 수 |
|---|---|
| 제품 도달 | 85 |
| 제품 미도달 | 25 |

85개 중 18개는 `include/game/` — 축 라이브러리가 아니라 디펜스 제품 그 자체다. 따라서
**축 라이브러리 92개 중 제품이 부르는 것은 67개**다.

---

## 13축 요약

| 축 | mega 안의 위치 | 제품 사용 | 어느 게임 기능이 쓰는가 |
|---|---|---|---|
| parts | `serialize/` `memory/` `containers/` `ecs/` `jobs/` `sync/` `profiler/` | **전부 (13/13)** | 적·발사체 저장(고정 열 SoA 와 archetype 컬럼 둘 다), 프레임·매치 수명 할당, 스냅샷 보관 링, 시스템별 계측, 워커 풀과 그 잠금 |
| system | `io/` `platform/process.*` `sched/` | **전부 (5/5)** | 외부 에셋 읽기, 매치 중 비동기 리로드, 트레이스 덤프 쓰기, 워커 코어 고정, 서버의 종료 신호 |
| network | `net/` `mp/` | **핵심 (7/14)** | 협동 세션 전체 — 명령·ACK·스냅샷·보간 |
| math | `math/` | **전부 (12/12)** | 좌표·거리, 절차 지형의 밀도장과 발파 브러시, 기울인 카메라의 투영·자세·표시 변환·연출 곡선, 조준과 정적 기하의 공간 색인 |
| physics | `physics/` | **전부 (6/6)** | 발사체 명중과 광역 피해 판정, 잔해 강체의 적분·접촉 응답·광역 탐색 |
| graphics | `render/` `platform/vk_present.hpp` | **전부 (5/5)** | 화면 전체 — 타일·스프라이트·HUD·GDI/Vulkan 표시 |
| ai | `ai/` | **없음 (0/9)** | 없다. 타게팅·경로 추종은 `game/game.hpp` 의 도메인 규칙이다 |
| engine | `engine/` | **전부 (7/7)** | 키→액션 매핑, 피격 파티클, 보스의 본 계층·포즈 클립·2본 IK, 스프라이트의 세대 핸들 소유, 카메라 기준 감쇠·패닝 |
| languages | `tools/script.hpp` | **전부 (1/1)** | `balance.script` 로 시작 자원·적 배율 계산 |
| db | `db/` | **전부 (3/3)** | 매치 결과와 프로필의 WAL 원자 커밋 |
| live_service | `live/` | **부분 (2/6)** | 전용 서버 입장 토큰 검증, 운영 지표 집계 |
| platform | `platform/` (표시·프로세스 제외) | **부분 (4/8)** | 창·입력, 효과음 출력, 실행 파일·사용자 데이터 경로, 에셋 경로 해석 |
| tools | `tools/` (스크립트 제외) | **부분 (2/3)** | 절차 맵의 미로 골격과 후보 격자 연결성 검사. 에셋 파이프라인만 남는다 |

"부분"과 "없음"은 결함이 아니라 **기록된 판정**이다. 판정이 기록되지 않은 상태만 결함이다.

---

## 축별 상세

### parts — 전부 (13/13)

**제품이 쓰는 것.**

| 헤더 | 어느 게임 기능이 부르는가 |
|---|---|
| `serialize/archive.h` | 프로필 레코드와 엔진 스냅샷의 바이트 인코딩. 영속 형식과 wire 형식이 한 구현에서 나온다 |
| `containers/soa_vector.h` | `EnemyStore` 의 SoA 팔. 적 저장의 열 배치(`entity_store=soa`) |
| `containers/hash_map.h` | 사라진 발사체 추적(파티클), `EnemyStore` 의 id→행 색인, 클라이언트 보간의 id 대응(`id_map=hash`) |
| `containers/ring_buffer.h` | `net::SnapshotBufferT` 의 고정 용량 보관소 |
| `containers/intrusive_list.h` | 발사체 풀의 live 목록과 deferred-free 목록 |
| `memory/arena.h` | 프레임 스크래치(`frame_alloc=arena`) — 조준 결과·스플래시 후보·개체별 피해·파티클 방출 좌표 |
| `memory/pool.h` | 발사체 블록의 고정 크기 풀(`body_alloc=pool`). 주소 안정성이 요구다 |
| `memory/slab.h` | 맵 타일·경로 배열의 크기 클래스 할당(`blob_alloc=slab`) |
| `jobs/scheduler.h` | 적 전진과 조준 질의의 인덱스 구간 분할(`jobs=1|2|4|8`), 비동기 IO 의 워커 |
| `sync/lock.h` | 스케줄러 큐·조건 변수의 futex 팔(`worker_lock=futex`) |
| `sync/futex.h` | 그 아래의 대기·깨우기, IO 완료 통지 |
| `profiler/trace.h` | 프레임·넷 펌프·렌더·시뮬 시스템별 구간 계측 |

이 축의 대부분은 **A/B 스위치의 한쪽 팔**로 제품에 있다. 기본값은 언제나 스위치 이전의
동작이고(`aos` · `linear` · `malloc` · `vector` · `std` · `jobs=0`), 두 팔의 결과가 같다는
것은 상태 해시로, 비용 차이는 프로파일러 구간으로 판정한다.

`ecs/world.h` 가 `EnemyStore` 의 **세 번째 팔**이다(`entity_store=ecs`). 들어온 계기는 규모가
아니라 **조합**이었다 — 보스만 리그(`EnemyRig`)를 갖게 되면서 고정 열 SoA 가 없는 컴포넌트에
자리를 낭비하기 시작했다. 순회는 조합 마스크 오름차순으로 고정한 래퍼
(`ForEachChunkOrdered`)를 통해서만 하며, 그러지 않으면 archetype 배열이 스폰 순서에 의존해
누적 결과가 실행마다 갈린다.

**참조 구현으로 남는 것.** 없다.

### system — 전부 (5/5)

**제품이 쓰는 것.** `io/read_file.hpp` → `io/file.h` 가 디스크의 유일한 입구다. `level.txt` ·
`waves.txt` · `balance.script` · PPM 스킨 10장 · Vulkan SPIR-V 셰이더가 전부 이 경로로만
들어온다. `platform/process.hpp` 는 헤드리스 서버의 Ctrl+C · SIGTERM 을 소유해 드레인 종료를
만든다.

축의 나머지 절반은 헤더가 아니라 **실행 형태**로 존재한다. `mega_defense` 와
`mega_defense_server` 는 서로 다른 두 OS 프로세스이고 실제 UDP 로 통신한다. 프로세스 경계는
`tests/test_defense_process.cpp` 가 실제로 두 프로세스를 띄워 검증한다.

`io/async_io.h` 는 매치 중 에셋 리로드가 부른다(`asset_io=async`). 시작 시 한 번 읽는 것은
동기로 충분하지만 매치 중 열 장을 프레임 스레드에서 읽으면 그 프레임이 통째로 멈추므로,
파일 열기·크기 질의·읽기를 워커로 옮긴다. 같은 `IoSystem` 이 `--trace-out` 의 쓰기도 맡고,
그 경로가 `io::File::WriteAt` 의 유일한 제품 호출자다.

`sched/affinity.h` 는 잡 워커가 부른다(`affinity=1`). `ScopedAffinity` 로 워커를 코어에
고정하고 성공하면 우선순위를 올린다. 고정할 수 없는 환경에서는 조용히 OS 배치로 폴백한다.

**참조 구현으로 남는 것.** 없다.

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

### math — 전부 (12/12)

**제품이 쓰는 것.** 전부다.

| 헤더 | 어느 게임 기능이 부르는가 |
|---|---|
| `math/vec.hpp` | 모든 좌표·속도·방향 |
| `math/scalar.hpp` | 허용 오차 비교, 각도 변환, 클램프·이징 |
| `math/spatial/aabb.hpp` | `physics/shape.hpp` 의 Box 경계, 공간 색인의 공용 통화 |
| `math/noise.hpp` | 절차 맵의 밀도장 — 미로 벽을 깎아 방을 만들고 광맥·타일 명암을 정한다 |
| `math/sdf.hpp` | 발파 브러시(구·캡슐)를 바위 밀도장에서 감산한다 |
| `math/mat.hpp` | 기울인 카메라의 `Perspective`·`LookAt`, 그리고 픽킹의 `Inverse` |
| `math/quat.hpp` | 카메라 자세와 엔티티 표시 회전. 피치 ±89° 클램프가 짐벌락을 막는다 |
| `math/transform.hpp` | 엔티티 표시 TRS. 포탑이 몸통의 자식이고, 타워 선택이 역변환으로 판정된다 |
| `math/interp.hpp` | 웨이브 시작 카메라 레일(Catmull-Rom · SmootherStep) |
| `math/spatial/uniform_grid.hpp` | 조준 후보 질의(`target_index=grid`), 잔해 광역 탐색(`broadphase=grid`) |
| `math/spatial/quadtree.hpp` | 조준 후보 질의의 세 번째 팔(`target_index=quadtree`) |
| `math/spatial/bvh.hpp` | 정적 기하 질의 — 발파 영역과 타워 사격선(`static_query=bvh`) |

**측정이 정한 기본값.** 공간 색인 셋은 전부 스위치 뒤에 있고 기본값은 전수 스캔이다. 적 64
규모에서 색인은 손해이고(제품 셀프테스트에서 `sim.tower_fire` 0.50ms → 1.06ms), 적 2048
규모에서 균일 격자가 42% 이긴다(32.5ms → 18.9ms). 쿼드트리는 매 틱 재구축 비용 때문에
같은 규모에서 3배 진다(99.6ms).

### physics — 전부 (6/6)

**제품이 쓰는 것.** 전부다. 두 층으로 나뉜다.

**규칙이 직접 부르는 판정.** `physics/shape.hpp` · `physics/rigid_body.hpp` ·
`physics/narrow_phase/collide.hpp`. `game/game.hpp` 가 발사체 명중과 스플래시 피해 후보를
`CollideCircleCircle` 로 판정한다 — 게임 규칙이 물리 모듈의 판정을 그대로 쓴다.

**잔해가 부르는 시뮬.** `physics/world.hpp`(적분→광역→협역→솔버→적분 파이프라인) ·
`physics/integrate.hpp`(Symplectic Euler 와 A/B 팔인 Explicit Euler) ·
`physics/solver/sequential_impulse.hpp`(비관통·반발·마찰의 반복 수렴). 철거된 타워와 발파된
바위가 실제 강체 파편이 되어 굴러가고, 멈춘 파편이 통행 격자를 막아 경로를 바꾼다 —
**물리 결과가 AI 입력이 되는 유일한 자리**다.

**측정.** 광역 탐색 격자가 최대 쌍 수를 47,400에서 518로 줄이면서 상태를 비트 동일하게
유지한다. 솔버 반복 4/8/16 이 정지 관통을 0.0277/0.0140/0.0078 로 절반씩 줄인다. Explicit
Euler 는 충격 관통이 Symplectic 의 2.4배다(0.690 vs 0.293).

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

### engine — 전부 (7/7)

**제품이 쓰는 것.** `engine/input.hpp`(물리 키를 액션으로 바꾸고 눌림 edge 를 프레임마다 비워
키를 누르고 있는 동안 강화가 반복되지 않게 한다) · `engine/particles.hpp`(명중 시 파티클 —
권위 상태를 수정하지 않는 렌더 전용이라는 방향성이 테스트로 강제된다) ·
`engine/{skeleton,anim,ik}`(보스의 절차 자세와 스톰프 원점) · `engine/resource.hpp`(스프라이트
수명) · `engine/audio.hpp`(카메라 기준 감쇠·패닝).

축의 **프레임 오케스트레이션**은 헤더가 아니라 `game/defense_app.hpp` 의 `DefenseApp::Step()`
이 소유한다. 고정 60Hz 시뮬과 가변 렌더의 분리, 한 프레임 최대 5틱 보충이 그 안에 있고,
창 프런트엔드와 헤드리스 셀프테스트가 **같은 Step 을** 부른다.

`engine/{skeleton,anim,ik}` 은 보스가 스프라이트 한 장에서 다족 개체가 되면서 들어왔다.
`game/boss_rig.hpp` 가 셋을 게임 좌표로 잇는다 — 본 계층이 몸통·고관절을 놓고, `assets/boss.pose`
의 클립이 다리가 가리키는 방향을 정하고, 2본 IK 가 무릎을 놓아 발을 착지시킨다. 그 발 좌표는
표시로 끝나지 않고 **스톰프 공격의 원점**이 되어 권위 시뮬로 들어간다.

`engine/resource.hpp` 는 `DefenseSkin` 의 스프라이트 소유를 값에서 세대 핸들 + 레퍼런스 카운트로
바꿨다. 매치 중 리로드가 생긴 뒤로 "시작 시 한 번 로드하고 유지"라는 수명 가정이 더는 참이
아니기 때문이다.

`engine/audio.hpp` 는 카메라를 청취자로 삼아 거리 감쇠와 좌우 패닝을 계산한다
(`game/sfx_bus.hpp`). 게인은 카메라를 아는 층에서, 합성은 파형을 가진 층에서 일어난다.

**참조 구현으로 남는 것.** 없다.

### languages — 전부 (1/1)

**제품이 쓰는 것.** `tools/script.hpp`. 렉서 → 파서 → AST → 트리 워킹 인터프리터이며,
`assets/balance.script` 가 시작 골드 · 코어 체력 · 적 체력/속도/보상 배율을 계산한다. 파일은
64KiB 로 제한하고, 파싱·실행 성공 뒤에도 각 출력의 유한성과 범위를 검사해 하나라도 실패하면
다섯 값을 전부 기본 구성으로 되돌린다. 인터프리터는 실행 전체에 걸리는 스텝 예산(10⁶ 노드
방문)과 중첩 깊이 상한(64)을 갖는다 — 교체 가능한 파일이 입력이므로 "끝나지 않는 스크립트"도
크기·범위와 같은 급의 방어 대상이고, 상한 초과는 진단을 남긴 뒤 기본 구성으로 흡수된다.

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
`platform/audio_device.hpp`(효과음 파형 합성 + waveOut 스테레오 출력) · `platform/paths.hpp`(실행 파일 위치 · 사용자 데이터
위치 · 환경 변수) · `platform/vfs.hpp`(논리 경로 → 물리 위치).

`platform/vk_present.hpp` 는 graphics 축, `platform/process.hpp` 는 system 축으로 세었다 —
디렉터리가 아니라 축이 소유의 단위이기 때문이다.

**참조 구현으로 남는 것.** `platform/{byteorder,capability,console,detect}`.

**왜 안 쓰는가.** 이 축의 관측 장치는 **CI 매트릭스**인데 현재 타깃이 Windows 하나다. 크로스
OS 접속이 없으므로 엔디안 정규화가 실패할 경로가 없고, 능력 탐지가 바꿀 규모 파라미터도 없다.
축이 미달인 것이 아니라 **축을 측정할 두 번째 플랫폼이 아직 없다.**

### tools — 부분 (2/3)

**제품이 쓰는 것.** `tools/procgen.hpp` 와 `tools/level_editor.hpp`(`tools/script.hpp` 는
위에서 languages 축으로 세었다).

`MazeGen::Generate` 가 절차 맵의 골격을 판다 — 신장 트리라서 모든 칸이 도달 가능하고,
그 성질이 밀도장 감산 뒤에도 유지된다. `Level::Validate` 의 flood fill 은 두 자리에서 돈다:
맵 채택 전 한 번, 그리고 **매 배치 명령마다** 후보 격자로 한 번(메이즈 디펜스의 배치 규칙).

**두 검증기가 공존하는 이유.** `tools/level_editor.hpp` 는 `.`·`#`·`S`·`G` 타일에 대해
**도달 가능성**을 묻고, `game/defense_map.hpp` 의 `FromAscii` 는 `S`·`P`·`C`·`.`·`#` 타일에
대해 **무분기 단일 경로**라는 더 강한 불변식을 요구한다. 후자는 생성 맵에 적용할 수 없고
(생성 맵은 분기 투성이다), 전자는 classic 맵의 계약을 표현하지 못한다. 두 맵 모델이 서로
다른 계약을 갖는 한 검증기도 둘이다.

**남는 것.** `tools/asset_pipeline.hpp`. 에셋이 텍스트 5종과 PPM 10장이라 증분 베이크가
절약할 시간이 없다.

**축의 실사용은 어디 있는가.** 절반은 위 두 헤더이고, 나머지 절반은 `include/` 밖이다 —
빌드·검증 도구는 `CMakeLists.txt` 와 `scripts/check_layers.py` · `scripts/reachability.py` 가
수행한다.

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
