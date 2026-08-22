# projects/mega 통합 감사 보고서 (2026-08)

- 대상: `projects/mega/` — include 89파일 9,310줄 + games/defense 8파일 2,093줄 + tests 51파일 5,550줄
- 기준: `PROJECTS_PLAN.md`가 정의한 mega의 완료 상태 — "중복 없는 최종 통합 소스와 실제 게임",
  "이론의 가정과 실패 조건을 코드와 측정으로 확인하고 **여러 축을 실제 게임으로 통합**"
- 방법: (1) 13개 이론 축 ↔ mega 매핑, (2) README·docs 주장 대 코드 실재 대조,
  (3) 실제 빌드·ctest 실행 + **변이 테스트(mutation testing)로 검증력 측정**
- 검수 **2026-08** / 반영 **2026-08-13**(재설계안 P0 · 구조 게이트) · **2026-08-14**(검증력
  공백과 문서 정합). 작성 시점에는 지적만 했고 본문·코드를 수정하지 않았다. 그 선언은 더
  이상 유효하지 않다 — 두 차례 반영으로 32건 중 **24건이 닫히고 3건이 부분 해소**됐다.
  **아래 지적들은 현재형으로 쓰여 있지만 그중 대부분은 이미 해소된 상태이므로, 이 문서를
  할 일 목록으로 읽으면 안 된다.** 건별 판정은 각 항목 아래의 상태 줄(**최신이 위**)과
  문서 끝의 [반영 상태 (2026-08-13)](#반영-상태-2026-08-13) ·
  [반영 상태 (2026-08-14)](#반영-상태-2026-08-14)에 있다.
  인용된 코드·행번호는 **감사 시점(P0 이전)** 기준이라 현재 파일과 어긋난다.

> 총 **32건** — 치명 2 · 높음 12 · 중간 11 · 낮음 7

## 상태 (2026-08-23) - 32건 전부 닫힘

마지막까지 열려 있던 **#25** 가 닫혔다. 그것으로 이 감사의 32건은 전부 닫힌 상태다.

| # | 이전 상태 | 지금 |
|---|---|---|
| #25 | 미해소 - sanitizer 문자열 0건, 제품 시뮬의 결정성 replay·checksum 없음 | **닫힘** |

**#25 의 두 반쪽이 각각 어디로 갔는가.**

- *sanitizer* - `CMakeLists.txt` 에 `MEGA_SANITIZE` 캐시 변수가 생겼고(MSVC 는 `address`
  하나만 허용하고 나머지는 구성 단계에서 `FATAL_ERROR` 다), `.github/workflows/ci.yml` 에
  `asan-windows` 잡이 있다. ASan 빌드로 ctest 117개가 전부 통과한다.
- *고정 seed replay·checksum* - `mega_defense --record` / `--verify-replay` 가 제품
  실행 파일 안에 있고, `mp::GoldenChain` 이 매 틱 (틱 수, 월드 해시, 아바타 체크섬,
  아바타 프레임)을 접는다. ctest 에 솔로·호스트·아바타 세 팔의 기록·재생 짝 여섯이
  fixture 로 묶여 있다. 감사가 지적한 "checksum 을 쓰는 테스트가 제품 미사용 모듈
  `mp/det_sim.hpp` 만 겨냥한다" 는 상태는 사라졌다 - `mp/det_sim.hpp` 도 `mp/lockstep.hpp`
  도 이제 제품 도달 헤더다.

**감사 시점과의 대조(실측).**

| 항목 | 감사 시점 | 2026-08-23 |
|---|---|---|
| `include/` 헤더 | 89 | 114 |
| 제품 도달 헤더 | 25 | **114** |
| 랩 전용 등재 | - | **0행** |
| ctest | 51 | **117** (5회 연속 green) |

아래 본문의 지적들은 여전히 **감사 시점의 현재형**으로 쓰여 있다. 할 일 목록이 아니라
그 시점의 기록으로 읽어야 하며, 건별 판정은 각 항목 아래의 상태 줄이 소유한다.

---

## 먼저: 빌드와 테스트는 멀쩡하다

감사에서 직접 재현한 것:

```
cmake -S projects/mega -B build_audit        → exit 0
cmake --build build_audit --config Release   → exit 0, warning 0건 (MSVC 19.51.36252.0)
ctest -C Release                             → 100% tests passed out of 51 (10.86 sec)
mega_defense --selftest                      → CLEAR, 1374프레임
mega_defense --gpu-selftest                  → Vulkan 5프레임 present OK
```

외부 에셋 10/10 로드, 외부 level·waves·balance 채택도 재현된다.
**README가 나열한 17개 기능 중 "없음"으로 판정된 것은 하나도 없다.**

"이상하다"의 정체는 기능 부재가 아니라 **선언된 목표와 실제 구조의 괴리**, 그리고
**테스트가 통과한다는 사실과 테스트가 무엇을 보장하는지 사이의 간극**이다.

---

## 1. [치명] include/의 61%(64/89 헤더, 5,681줄)가 게임에서 도달 불가 — "통합"이라는 선언과 실제가 정면으로 어긋난다

> **[P0로 닫힘 · 2026-08-13]** 도달성 게이트 신설. 헤더 99개 중 제품 도달 41 / `docs/LAB_ONLY.md` 등재 58, 근거 없는 미도달 **64 → 0**. 제3의 상태는 빌드 실패. 두 상반된 선언은 `docs/AXIS_OWNERSHIP.md`가 조정한다. 다만 **미도달에 근거를 강제한 것이지 게임이 부르게 만든 것은 아니다** — 축을 실제 호출로 끌어들이는 것은 P1~P8이다.

<sub>관점: 이론 시리즈 ↔ mega 통합 매핑 감사</sub>

**근거**

```text
전이 include 폐포 분석(games/defense/*.cpp,*.hpp 전체를 시드로 #include "..." 재귀 추적) 결과:
- 게임 도달 가능: 25/89 헤더
- 게임 도달 불가: 64/89 헤더, 합계 5,681줄 (전체 9,310줄의 61%)
- 게임·테스트 어디서도 도달 불가: include/io/file.cpp, include/platform/console.hpp, include/sched/affinity.cpp

도달 불가 서브시스템 전체: ai/(9파일 812줄), containers/(4파일 354줄), ecs/(260줄), jobs/(149줄), memory/(3파일 307줄), mp/(5파일 363줄), profiler/(137줄), sched/(186줄), sync/(2파일 256줄), io/(3파일 267줄)

게임 진입점이 실제로 include 하는 것은 `games/defense/main.cpp:10-23`의 8개 헤더뿐이고, `include/game/game.hpp:10-11`은 `math/vec.hpp`와 `physics/narrow_phase/collide.hpp` 두 개만 include 한다.

선언: `PROJECTS_PLAN.md:3` "이론의 가정과 실패 조건을 코드와 측정으로 확인하고 여러 축을 실제 게임으로 통합하는 것이다."
반대 선언: `projects/mega/docs/development.md:9` "캡스톤의 목표는 모듈 이름을 한 실행 파일에 모두 넣는 것이 아니다."
`projects/mega/README.md:150-152` "메모리 할당자, ECS, 작업 스케줄러, lockstep/rollback과 범용 AI 라이브러리는 ... 최대 64개 적을 다루는 권위 런타임에 측정 근거 없이 호출하지 않는다."
```

저자가 "이상하다"고 느끼는 것의 1차 원인이다. `include/`의 21개 서브시스템 디렉터리 구조는 13개 이론 축과 1:1로 대응하도록 배치돼 있어 "모든 축이 게임에 들어갔다"는 인상을 준다. 그러나 실제 실행 경로는 25개 헤더만 지나가고, 나머지 64개는 컴파일되지도 않는다(헤더온리이므로 아무도 include 하지 않으면 코드 생성 자체가 없다). 이 상태에서 mega는 PROJECTS_PLAN이 정의한 "통합 소스"가 아니라 "게임 하나 + 그와 무관한 라이브러리 모음"이다.

더 심각한 것은 저장소가 이 상태에 대해 **두 개의 상반된 공식 입장**을 동시에 갖고 있다는 점이다. 루트의 기준 문서(PROJECTS_PLAN.md:3, CURRICULUM_MAP.md:111 "별개 샘플 묶음이 아니라 하나의 플레이 가능한 멀티플레이 디펜스로 통합한다")는 통합을 완료 조건으로 걸고, mega 내부 문서(README.md:148-154, docs/architecture.md의 "라이브러리 소유권" 표, docs/development.md:9)는 통합하지 않는 것이 옳다고 논증한다. 어느 쪽이 기준인지 저장소가 스스로 답하지 못하는 상태이며, 감사자·독자·저자 본인 모두 "완료됐다"를 판정할 수 없다.

mega 내부 문서의 논증(64개 적 규모에서 잡 스케줄러·ECS는 측정 근거 없이 도입하지 않는다)은 엔지니어링 판단으로는 타당하다. 문제는 그 판단이 **커리큘럼 기준 문서에 반영되지 않았다**는 것이다.

**방향**: 둘 중 하나를 고른 뒤 반대쪽 문서를 고쳐야 한다. (a) 통합을 기준으로 유지한다면, 게임이 실제로 부를 이유가 생기는 규모/경계를 만들거나(예: 적 상한 64→수천으로 올려 flow field·jobs·SoA가 측정으로 정당화되는 지점 만들기) 부르지 못하는 축은 mega에서 제거하고 mini로 되돌린다. (b) mega를 "플레이 가능한 제품 + 독립 검증 라이브러리"로 재정의한다면, PROJECTS_PLAN.md:3/10과 CURRICULUM_MAP.md:111을 그 정의로 고쳐 "통합"이라는 단어를 없애고, 각 축의 완료 조건을 mini lab 쪽으로 명시적으로 이전한다.

---

## 2. [치명] graphics 축(40편·21,286줄)이 16줄짜리 blit 셰이더 한 쌍으로 축소 — GL·DX 19편은 대응물이 0

> **[미해소 · 2026-08-13]** graphics는 재설계안 P1(소프트웨어 래스터 백엔드)·P3(Vulkan 백엔드)에서 다뤄진다. 현재 렌더는 그대로 CPU 프레임버퍼 blit.

<sub>관점: 이론 시리즈 ↔ mega 통합 매핑 감사</sub>

**근거**

```text
mega의 그래픽스 전량:
- `include/render/` 3파일 392줄 (framebuffer 93, raster2d 87, sprite 212) — 전부 CPU 소프트웨어 래스터
- `games/defense/shaders/fullscreen.vert` 8줄 + `blit.frag` 8줄. blit.frag 본문 전체: `outColor = texture(uTex, vUV);`
- `games/defense/vk_present.hpp` 421줄 — 헤더 주석 1-6줄이 스스로 밝히듯 "CPU 프레임버퍼(RGBA8, Framebuffer::Data())를 GPU 텍스처에 매 프레임 업로드하고, 풀스크린 삼각형으로 그 텍스처를 샘플링해 스왑체인 이미지에 그린 뒤 present 한다."

개념 부재 확인 — `grep -rniE "depth|z-?buffer|mipmap|msaa|blinn|phong|pbr|shadow ?map|gbuffer|deferred|tessell|instanc" include/ games/` 결과 그래픽스 관련 히트 0건(모든 히트가 quadtree maxDepth, rollback lastDepth, collide faceDepth, VK_..._PRIMARY 등 무관 문자열).
- 깊이 버퍼 없음(vk_present.hpp:184-203 렌더패스에 color attachment 1개뿐)
- 밉맵 없음(vk_present.hpp:282 `ci.mipLevels = 1`), MSAA 없음(vk_present.hpp:254-255 `VK_SAMPLE_COUNT_1_BIT`)
- 정점 버퍼 없음(vk_present.hpp:247 빈 VertexInputState, :383 `vkCmdDraw(cmd_, 3, 1, 0, 0)`)
- OpenGL·DirectX 코드 0줄 (stage1 10편 + stage2 9편 = 19편 대응물 없음)

알파 합성은 8비트 sRGB 공간에서 감마 보정 없이 수행(`include/render/framebuffer.hpp:49-58`) — stage0_fund_04(색과 감마)의 핵심 주장이 적용되지 않음.
```

graphics는 13축 중 문서량 2위(40편, 21,286줄)인데 mega에서의 대응물은 "CPU가 그린 480x480 RGBA8 버퍼를 한 장의 텍스처로 올려 화면에 붙이는 것"이 전부다. stage0 기초 9편이 다루는 파이프라인 단계·MVP 변환·래스터화 규칙·색/감마·셰이딩·깊이 가시성·텍스처 샘플링·멀티패스·지오메트리 중, mega가 코드로 담은 것은 래스터화(CPU, raster2d.hpp)와 알파 합성(감마 없이) 정도다. stage0-adv 4편(GPU 아키텍처·컴퓨트·GI/RT·GPU 메모리)과 stage1~3의 프로젝트 회차(큐브·모델·라이팅·그림자·후처리·PBR·컴퓨트) 19+11편은 mega에 흔적이 없다.

수학 쪽에도 증상이 남아 있다: `include/math/mat.hpp:121 Perspective()`, `:134 LookAt()`이 구현돼 있지만 게임에서 도달 불가다. 3D 카메라 행렬이 있는데 그릴 3D가 없다.

Vulkan 경로 자체도 그래픽스 축의 검증 기준을 만족하지 못한다. `PROJECTS_PLAN.md:137`은 렌더링 검증을 "캡처 회귀, 자원 수명 validation, 프레임 계측"으로 정의하는데, `vk_present.hpp:104-112 CreateInstance()`는 확장 2개만 켜고 validation layer를 전혀 활성화하지 않으며(enabledLayerCount 미설정), GPU 경로의 캡처 회귀 테스트도 없다.

**방향**: graphics를 "통합됨"으로 세지 않는 것이 먼저다. 그다음 (a) 이 축의 검증 소유권을 `projects/mini/graphics_{gl,dx,vk}_lab`에 명시적으로 넘기고 mega README에서 그래픽스 통합 주장을 내리거나, (b) mega에서 최소 한 가지 실제 GPU 개념을 제품 경로에 넣는다(예: 스프라이트를 GPU 인스턴싱으로 그려 정점 버퍼·디스크립터·깊이/블렌드 상태가 실제로 생기게 하고, validation layer를 켠 뒤 프레임 캡처 회귀 테스트를 ctest에 추가). 어느 쪽이든 `include/render/framebuffer.hpp:3`의 "이 머신엔 Vulkan SDK 가 없어 GPU 백엔드를 빌드·검증할 수 없다"는 주석은 지금 상태와 반대이므로 함께 고쳐야 한다.

---

## 3. [높음] mega의 미사용 라이브러리 층이 mini lab과 같은 주제의 두 번째 구현 — "중복 없는 최종 통합 소스" 위반

> **[부분 · 2026-08-13]** `docs/AXIS_OWNERSHIP.md`가 13축의 소유권과 도달 여부를 컴파일러 의존성 출력 기준으로 기록해 "어느 쪽이 권위 있는 원본인가"는 답이 생겼다. **mini↔mega의 실제 코드 중복은 그대로다.**

<sub>관점: 이론 시리즈 ↔ mega 통합 매핑 감사</sub>

**근거**

```text
주제 대응(mega 쪽은 모두 게임 도달 불가):
- `mega/include/ai/pathfinding/astar.hpp`,`flow_field.hpp`,`steering/steering.hpp` ↔ `mini/ai_lab/src/scenes/pathfind_scene.cpp`,`flowfield_scene.cpp`,`boids_scene.cpp`,`influence_scene.cpp`
- `mega/include/math/quat.hpp`,`interp.hpp`,`noise.hpp`,`sdf.hpp`,`spatial/*` ↔ `mini/math_lab/src/scenes/rotation_scene.cpp`,`interpolation_scene.cpp`,`noise_scene.cpp`,`sdf_scene.cpp`,`spatial_scene.cpp`,`projection_scene.cpp`
- `mega/include/physics/integrate.hpp`,`world.hpp`,`narrow_phase/collide.hpp`,`solver/sequential_impulse.hpp` ↔ `mini/physics_lab/src/scenes/integrators_scene.cpp`,`broad_phase_scene.cpp`,`narrow_phase_scene.cpp`,`response_scene.cpp`,`constraints_scene.cpp`
- `mega/include/tools/script.hpp`(트리 워킹 인터프리터) ↔ `mini/languages_lab`(PROJECTS_PLAN:32가 요구하는 바이트코드 VM·호스트 바인딩)

테스트 역할도 겹친다: `grep -l "GameWorld\|DefenseMap" tests/*.cpp` → 7개 파일만 히트(test_dedicated_process, test_defense_network, test_defense_process, test_defense_protocol, test_defense_soak, test_game, test_game_expansion). ctest 51개 중 44개가 게임 상태를 한 번도 건드리지 않는 순수 라이브러리 단위 테스트다.

선언: `PROJECTS_PLAN.md:9-10` "| `projects/mini/` | 한 이론 축의 주장을 분리해 측정하는 실험실 | ... | `projects/mega/` | 중복 없는 최종 통합 소스와 실제 게임 |", `PROJECTS_PLAN.md:12-13` "단계별 소스 복사본은 코드 드리프트와 검증 중복을 만들므로 보관하지 않으며"
```

PROJECTS_PLAN이 mini/mega를 나눈 이유는 역할 분리다 — mini는 축 하나를 분리해 측정하는 실험실, mega는 그 결과를 게임으로 합치는 통합본. 그런데 mega의 미사용 64개 헤더 + 그것을 검사하는 44개 테스트는 정확히 mini의 역할을 수행하고 있다. 게다가 mini 쪽은 조작 변수·관측값·실패 주입(PROJECTS_PLAN:19-27의 추적 단위)을 갖춘 씬 기반 측정 장치인데, mega 쪽 사본은 그 측정 장치 없이 정확성 단언만 있다 — 같은 주제의 **약한 사본**이다.

"단계별 소스 복사본은 만들지 않는다"는 규칙은 지켜졌지만(games/에 defense만 남음), 그보다 큰 중복인 mini↔mega 라이브러리 중복은 남아 있다. 두 곳의 A*·쿼터니언·적분기가 서로 다르게 진화하면 정확히 PROJECTS_PLAN이 경계한 "코드 드리프트와 검증 중복"이 발생한다.

**방향**: 각 축의 검증 소유권을 한 곳으로 못 박아야 한다. 게임이 부르지 않는 mega 라이브러리는 (a) mini의 해당 lab으로 옮겨 측정 장치까지 붙이거나, (b) mega에 남긴다면 README/PROJECTS_PLAN에 "이 축의 소유자는 mega의 독립 라이브러리이고 mini에는 두지 않는다"를 명시하고 mini 쪽 중복을 정리한다. 지금처럼 양쪽에 같은 주제가 다른 구현으로 존재하는 상태는 어느 쪽도 권위 있는 원본이 아니다.

---

## 4. [높음] ai 축(25편·17,468줄): 9개 헤더 전부 게임 미호출 + 16개 챕터는 대응물 자체가 없음

> **[부분 · 2026-08-13]** ai 헤더 9개는 `docs/LAB_ONLY.md`에 P2·P4 도입 단계와 사유로 등재됐다(근거 없는 미도달은 해소). **게임이 부르지 않는다는 사실 자체는 그대로**이고, 레벨의 무분기 단일 경로 제약도 남아 있다.

<sub>관점: 이론 시리즈 ↔ mega 통합 매핑 감사</sub>

**근거**

```text
게임 도달 불가: `ai/bt/{builder,node,tick}.hpp`, `ai/fsm/state_machine.hpp`, `ai/pathfinding/{astar,flow_field,grid_map}.hpp`, `ai/perception/perception.hpp`, `ai/steering/steering.hpp` — 9/9 전부.

게임의 실제 AI:
- 적 이동 = 웨이포인트 추종(`include/game/game.hpp:489-521 EnemyAdvanceSystem`, path_[e.pathIndex]로 직선 이동)
- 타워 조준 = 전 적 선형 스캔 후 점수 비교(`include/game/game.hpp:235-258 SelectTarget`)
- 경로는 런타임 탐색이 아니라 레벨 파일이 소유한 고정 체인(`game.hpp:105-159 FromAscii`가 분기 없는 단일 경로를 강제)

대응물 없는 챕터(25편 중 16편): part03 utility AI, part04 GOAP, part05 HTN, part07 advanced pathfinding, part08 navmesh, part09 influence map, part12 flocking/boids, part14 squad coordination, part15 attack slot, part16 boss director, part17 RL, part18 imitation learning, part19 neural NPC, part20 AI debugging, part21 local avoidance, part22 adversarial search, part25 EQS.

`docs/architecture.md`(라이브러리 소유권 표) "| 선택 시뮬레이션 | 범용 `ai`, `mp` lockstep/rollback | 도메인 타게팅·권위 스냅샷 모델과 중복되는 구현은 직접 호출하지 않음 |"
```

AI는 문서량 3위(17,468줄) 축인데 게임에서의 실체는 "미리 정해진 타일 경로를 따라 직진하는 적"과 "사거리 안에서 세 가지 점수 함수로 고르는 타워"다. 이것을 도메인 AI라고 부를 수는 있으나, 25편이 다루는 의사결정 모델(BT/FSM/Utility/GOAP/HTN), 공간 표현(navmesh/influence/flow field), 지역 제어(steering/RVO/flocking), 관측(perception/EQS/blackboard) 중 게임에 반영된 것은 0개다.

특히 mega가 이미 **구현해 둔** flow field·A*·BT·perception조차 부르지 않는 것이 구조적 위화감의 핵심이다. 디펜스는 원래 flow field의 교과서적 적용 대상(다수 개체·단일 목표)인데, 레벨 형식을 "분기 없는 단일 경로"로 못 박아(game.hpp:151 `if (candidateCount != 1) return false;`) 경로 탐색이 필요할 여지 자체를 설계에서 제거했다.

**방향**: 레벨 형식의 단일 경로 제약을 푸는 것이 가장 적은 비용으로 축을 실제 통합시키는 지점이다. 분기·다중 스폰을 허용하면 flow_field.hpp가 즉시 필요해지고(타워 배치가 경로를 바꾸는 미로형 디펜스라면 A*/재계산 비용 측정까지 자연히 생긴다), perception/BT는 보스 웨이브의 국지 행동에 붙일 수 있다. 그럴 계획이 없다면 `include/ai/`는 mini/ai_lab으로 이관하고 mega README의 "수학·시뮬레이션 라이브러리"(README.md:140) 서술에서 ai를 빼는 편이 정직하다.

---

## 5. [높음] net 서브시스템 자기중복: 제품이 자기 net 모듈을 쓰지 않고 같은 기능을 다시 구현

> **[부분 · 2026-08-13]** 제품(`include/game/defense_app.hpp:29-30`)이 `net/reliable_channel.hpp`·`net/session.hpp`(→ `net/snapshot.hpp`)를 실제로 include 한다. **`net/rtt.hpp`는 P5로 등재 이월** — 현 와이어 프로토콜에 ping/pong 왕복 표본이 없어 넣을 입력이 없다는 사유가 `LAB_ONLY.md:85`에 적혀 있다.

<sub>관점: 이론 시리즈 ↔ mega 통합 매핑 감사</sub>

**근거**

```text
게임 도달 불가한 net 헤더: `net/packet.hpp`(60), `net/reliable_channel.hpp`(118), `net/rtt.hpp`(42), `net/session.hpp`(69), `net/snapshot.hpp`(123) — 합계 412줄. 게임이 쓰는 것은 sequence/transport/udp_transport/udp_server 뿐.

`include/net/session.hpp:1-6` 주석: "session — host-authoritative 협동 세션의 양 끝. ... 호스트가 적·물리의 권위를 갖고 시뮬한 뒤 스냅샷을 뿌린다. 클라이언트는 스냅샷을 버퍼에 쌓아 보간 지연만큼 과거를 그려 부드럽게 표시한다." — 이것이 정확히 제품이 하는 일인데 제품은 이 파일을 include 하지 않는다.

제품 쪽 재구현:
- 재전송+누적 ACK 정리: `games/defense/main.cpp:686-700` (reliable_channel.hpp 대신 인라인)
- 스냅샷 보간 버퍼: `games/defense/main.cpp:155-228 class ReplicaBuffer` (net/snapshot.hpp의 보간 버퍼 대신)
- 스냅샷 인코딩/디코딩: `include/game/defense_protocol.hpp`(363줄, net/snapshot.hpp와 별개 포맷)
- RTT 추정: 없음. `main.cpp:686` 재전송 주기는 `std::chrono::milliseconds(100)` 고정 상수 (rtt.hpp의 적응형 타임아웃 미사용)
```

network는 mega에서 가장 잘 통합된 축이지만(실제 UDP·2프로세스·토큰 입장이 ctest로 검증됨), 그 안에서 같은 개념이 두 번 구현돼 있다. `net/`의 재사용 가능한 계층(신뢰 채널·스냅샷 버퍼·세션·RTT)이 테스트에서만 살아 있고, 제품은 그 계층을 우회해 `defense_protocol.hpp` + `main.cpp` 안에 같은 로직을 다시 썼다.

실질적 손해도 있다: `net/rtt.hpp`가 제공하는 RTT+지터 기반 적응형 타임아웃 대신 100ms 고정 재전송을 쓰므로, RTT가 100ms를 넘는 실제 인터넷 경로에서는 매 주기 불필요한 중복 전송이 발생한다. 이론(network part11 measurement·diagnosis)이 코드에 반영되지 않아 생긴 구체적 결함이다.

또한 `mp/`(lockstep 74, rollback 116, det_sim 67, matchmaking 76, input 30)도 전량 게임 미호출이라 network part10(rollback)은 대응물이 있으나 게임과 무관하다.

**방향**: 둘 중 하나로 정리: (a) 제품이 `net/reliable_channel.hpp`·`net/snapshot.hpp`·`net/rtt.hpp`를 실제로 타도록 defense_protocol을 그 위에 얹는다(최소한 rtt.hpp 도입은 고정 100ms의 실제 결함을 고친다). (b) 제품 포맷이 별개여야 한다면 미사용 net 헤더를 제거하거나 mini/game_network_lab으로 옮겨, 같은 저장소에 두 개의 신뢰 채널·두 개의 스냅샷 버퍼가 공존하지 않게 한다.

---

## 6. [높음] parts(24편)·algorithms(11편)·system(22편) 축의 핵심 주제 대부분이 mega에 코드로 존재하지 않음

> **[미해소 · 2026-08-13]** parts·algorithms·system 축은 재설계안 P1~P8 대상. 현재 게임 규모에서는 여전히 호출 이유가 없다.

<sub>관점: 이론 시리즈 ↔ mega 통합 매핑 감사</sub>

**근거**

```text
parts(26,654줄) 대응물 = `containers/` 354줄 + `memory/` 307줄 + `ecs/world.h` 260줄 + `serialize/archive.h` 128줄, 이 중 serialize만 게임 도달 가능.
- SIMD(part19): `grep -rniE "simd|sse|avx|__m128|intrin|neon" include/ games/ tests/` → 히트 4건 전부 주석/문자열, 실제 intrinsic 0건
- 코루틴·파이버(part14): 히트 3건 전부 `include/jobs/scheduler.h:8-11`의 "Fiber 는 플랫폼 어셈블리/ucontext 의존이라 ... 이 위에 얹는 설계 토픽으로 남긴다" 주석
- lock-free(part13): 같은 주석이 "lock-free Chase-Lev deque"를 미구현으로 명시. `containers/ring_buffer.h:7` "이 버전은 단일 스레드용이다"
- 대응물 없음: 스마트 포인터(05), 문자열(06), 인코딩(07), vptr/다형성(15), 컴파일러 UB(16), 타입 소거(17), 캐시 무관 알고리즘(20), 분기 예측(21), hot/cold 분할(22), 메모리 프로파일링(24)

algorithms(11편) 대응물: `grep -rniE "huffman|fenwick|segment tree|aho|kmp|union.?find|kruskal|knapsack|bellman|scc|topolog|fuzz|property.?test|quickselect" include/ games/ tests/` → 히트 0건. 존재하는 것은 A*(ai/pathfinding, 게임 미사용)와 flow_field 내부 Dijkstra(게임 미사용)뿐. part02~05, 07~11의 필수 구현 목록(algorithms/README의 장별 표: lower_bound·stable merge sort·quickselect·Fenwick·KMP·Aho-Corasick·DSU·MST·Bellman-Ford·knapsack·max flow·Huffman·property generator) 중 mega에 있는 것은 0개.

system(29,672줄, 최대 문서량) 대응물 = `io/` 267 + `sync/` 256 + `sched/` 186 + `jobs/` 149 = 858줄, 전부 게임 미호출.
- IOCP/epoll/io_uring: `grep -rn "io_uring\|IOCP\|epoll\|CreateIoCompletionPort" include/ games/` → 히트 3건 전부 "교체되는 지점으로 남긴다"류 주석(`io/async_io.h:8`, `io/file.cpp:4-5`)
- 대응물 없음: 부팅(01), 프로세스(02), 가상 메모리(03), 커널 할당자(04), 시스템 콜(05), 인터럽트(06), 시그널(07), 커널 스케줄러(08), ELF/링커(10), 동적 링킹(11), 파일시스템(12), 블록 IO(13), IPC(15), 네트워크 스택(16), 드라이버(18), 가상화(19), 컨테이너(20), 보안(21), 커널 관측(22) — 22편 중 19편
```

문서량 1·2위 축(parts 26,654줄 / system 29,672줄, 합계 56,326줄 = 전체 이론의 33%)에 대한 mega의 대응이 각각 1,049줄·858줄이고 그마저 게임이 부르지 않는다. algorithms는 대응물이 사실상 0이다.

system 축은 성격상 게임 제품 코드로 통합되기 어렵다는 반론이 가능하고 실제로 `projects/mini/system_kernel_lab`이 그 소유자다. 문제는 **parts와 algorithms**다. 이 둘은 "게임 코드에 직접 나타나는 CS"인데, mega의 게임 루프에는 커스텀 할당자도, SoA도, ECS도, SIMD도, 알고리즘 구조도 전혀 나타나지 않는다. 게임 상태는 `include/game/game.hpp:615-617`의 평범한 `std::vector<Enemy>/<Tower>/<Projectile>` 멤버이고, 정리는 `std::remove_if`(game.hpp:592-598)다.

`include/ecs/world.h:1-10`은 archetype ECS를 "parts 시리즈의 ECS 비교 결론"으로 구현했다고 적어 두었지만, 게임은 그 결론을 적용하지 않는다 — ECS 비교의 결론이 게임에 도달하지 못한 것이다.

**방향**: parts/algorithms는 mega에서 통합 축으로 세지 않는 편이 정확하다. 통합을 원한다면 게임 규모를 키워 구조가 강제되는 지점을 만들거나(적 상한 64→수천), 최소한 `GameWorld`의 엔티티 저장을 `containers/soa_vector.h`·`memory/pool.h`로 바꿔 "제품 경로가 실제로 쓰는 자료구조"를 하나라도 만들고 그 변경 전후를 측정으로 남긴다. algorithms 축은 mini에 대응 lab이 아예 없으므로(`projects/mini/`에 algorithms_lab 없음) 소유자를 먼저 정해야 한다.

---

## 7. [높음] 문서가 CTest 검증 항목으로 적은 "Vulkan 스왑체인 5프레임 present"가 CTest에도 CI에도 없다

> **[닫힘 · 2026-08-14]** `add_test(NAME mega_defense_gpu_selftest COMMAND mega_defense --gpu-selftest)`가 등록됐다. 감사가 제안한 (a)와 (b) 중 어느 쪽도 그대로 쓰지 않았다 — `if(Vulkan_FOUND)` 안에 넣으면 **테스트 목록 자체가 머신마다 달라져** "ctest N개"라는 서술이 어디선가 틀리고, 무엇보다 GPU가 없다는 사실이 결과에 아무 흔적도 남기지 않는다. 그래서 항상 등록하고, 요구 조건(Vulkan 빌드 · 창 백엔드 · 장치와 셰이더)이 없으면 실행 파일이 `kDefenseSkipExitCode`(77, `defense_app.hpp:68`)로 끝나 CTest의 `SKIP_RETURN_CODE`가 **Skipped**로 센다. 실패는 셋 다 갖췄는데 present가 깨졌을 때뿐이다. 문서 세 곳(README · `architecture.md` · `development.md`)과 CI 주석이 모두 "Skipped는 검증됐다는 뜻이 아니라 이 머신에서는 물을 수 없다는 뜻"을 함께 적는다.
>
> **[미해소 · 2026-08-13]** `--gpu-selftest`를 등록하는 `add_test`는 지금도 없고, `README.md`의 "…Vulkan 스왑체인 5프레임 present를 자동 검증한다" 문장도 그대로다.

<sub>관점: README·docs 주장 대 코드 실재 대조</sub>

**근거**

```text
README.md:29 "반복 매치 soak, Vulkan 스왑체인 5프레임 present를 자동 검증한다." / docs/development.md:219 "CTest는 51개 대상을 실행한다. 핵심 검증은 다음과 같다." 바로 아래 :233 "| GPU | 실제 Vulkan swapchain present |" / docs/architecture.md:167 "- GPU: 실제 Vulkan 장치·스왑체인 5프레임 present". 그러나 CMakeLists.txt:59 의 `add_test(NAME mega_defense_selftest COMMAND mega_defense --selftest)` 가 실행 파일을 도는 유일한 테스트다. `ctest --test-dir build -C Release -N` → "Total Tests: 51", 51번은 mega_defense_selftest 이고 GPU 항목은 없다. .github/workflows/ci.yml 말미 주석도 "CI는 창을 열지 않고 mega_defense --selftest와 엔진·게임 테스트를 실행한다." 라고 스스로 적고 있다. 수동 실행은 성공한다: `mega_defense.exe --gpu-selftest` → "[gpu-selftest] Vulkan present 5 frames: OK" (exit 0).
```

기능 자체는 실재한다(과장이지 허위가 아니다). 문제는 PROJECTS_PLAN.md:141 이 "README에는 구현된 기능, 시뮬레이션으로만 검증한 기능, 교체 지점만 있는 기능을 구분해 적는다"고 요구하는데, GPU present는 "자동 검증"(=CTest/CI)이 아니라 사람이 커맨드를 쳐야만 도는 수동 스모크라는 점이 세 문서 모두에서 지워져 있다는 것이다. development.md의 표는 문맥상 "CTest 51개 대상"의 계층 분해로 읽히므로 특히 오해를 만든다.

**방향**: 둘 중 하나. (a) `add_test(NAME mega_defense_gpu_selftest COMMAND mega_defense --gpu-selftest)` 를 Vulkan_FOUND 블록 안에서 등록하고 CI에서는 스킵 처리한다. (b) 세 문서에서 GPU 행을 CTest 표 밖으로 빼고 "수동 재현 명령"으로 분리 표기한다.

---

## 8. [높음] 교체 가능한 balance.script 하나로 두 실행 파일이 시작 시 무한정 멈춘다 — "제한된 DSL / 전체 폴백" 주장과 어긋남

> **[닫힘 · 2026-08-14 — 지적보다 컸다]** 감사는 정지 경로를 **하나**(중첩 While에서 지역 `guard`가 곱해져 10^12회) 지목했다. 고치는 과정에서 **같은 파일 하나로 재현되는 정지 경로가 셋**이었음이 드러났고, 셋 다 닫았다.
>
> 1. **지역 guard가 곱해진다** — 감사가 지목한 그것. `while` 노드마다 카운터가 0으로 되돌아가므로 상한이 더해지지 않고 곱해진다.
> 2. **파서가 전진하지 않는다** — `ParsePrimary`가 모르는 토큰을 소비하지 않고 돌아와 최상위 문 루프가 제자리를 돈다. **`}` 한 글자짜리 스크립트**가 노드를 무한 할당한다. 이건 실행 예산이 아니라 파싱 단계라, 예산을 걸어도 **도달조차 하지 못한다**.
> 3. **재귀 깊이 상한이 없다** — 괄호를 충분히 겹치면 재귀 하강이 스택을 넘긴다. 폴백이 아니라 프로세스의 죽음이다.
>
> 옛 코드에서 **실행으로 증명했다**: 중첩 while 소스는 **30.77초**, `}` 한 글자 소스는 **20.22초** 뒤에도 `Run()`에서 돌아오지 않았다(둘 다 강제 종료). 방어는 이제 While 노드가 아니라 `Run()` 한 번 전체에 걸린다 — 스텝 예산 `kDefaultStepBudget`(Eval 노드 방문 10⁶), 중첩 깊이 상한 `kMaxNestingDepth`(64), 그리고 모르는 토큰을 반드시 소비하는 파서 전진 보장. 셋 다 조용히 멈추지 않고 `<스크립트 이름>:<줄>: <무엇이>` 형식의 진단을 남기며(예산 초과는 `실행 스텝 예산 1000000 을 넘겨 중단했다(끝나지 않는 반복으로 보인다)`), `LoadBalanceScript`가 그 문자열을 out 인자로 올려 두 실행 파일이 stderr에 찍고 기본 구성으로 진행한다 — **조용한 폴백이 아니게** 만든 것도 이번 변경이다. `tests/test_tools_script.cpp`에 다섯 케이스를 추가하고 ctest에 `TIMEOUT 30`을 걸었다(방어가 무너지면 테스트는 실패하는 게 아니라 끝나지 않으므로, 그 상태를 실패로 바꿔 주는 것은 타임아웃뿐이다).
>
> **[미해소 · 2026-08-13]** `include/tools/script.hpp:189`의 방어는 여전히 While 노드당 지역 `guard`뿐이다. 전역 스텝 예산 없음. **게임 규모와 무관한 순수 결함이라 지금 닫을 수 있다.**

<sub>관점: README·docs 주장 대 코드 실재 대조</sub>

**근거**

```text
docs/architecture.md:109-111 "`balance.script`는 시작 골드, 코어 체력, 적 체력·속도·보상 배율을 계산한다. 파일은 64KiB로 제한하고 파싱·실행 성공 뒤에도 각 출력의 유한성과 범위를 검사한다. 하나라도 실패하면 다섯 값을 모두 기본 구성으로 되돌린다." / README.md:12 "밸런스 스크립트: … 제한된 DSL로 계산하고 범위 밖 결과를 전체 폴백". 그러나 include/tools/script.hpp:189 의 유일한 방어는 While 노드 하나에 지역 카운터다: `case Node::While: { int guard = 0; while (Eval(n->kids[0].get()) != 0) { Eval(n->kids[1].get()); if (++guard > 1000000) { ok_ = false; break; } } return 0; }` — guard가 While 노드 평가마다 새로 0이 되므로 중첩 while은 10^12회까지 돈다. 전역 실행 예산은 없다.
실증: 스크래치 assets 디렉터리에 `i = 0` / `while i < 1000000 { j = 0 while j < 1000000 { j = j + 1 } i = i + 1 }` 를 넣은 balance.script를 두고 `mega_defense_server.exe --port 39311 --server-id x --assets <scratch> --run-seconds 1` 실행 → 20.05초 후에도 프로세스 미종료(강제 kill). 표준출력은 "[security] using the local development token secret…" 한 줄에서 멈췄고 server_main.cpp:155 의 `[content] map=… waves=… balance=…` 줄은 끝내 나오지 않았다 = LoadBalanceScript 안에서 멈춤.
```

같은 로더를 클라이언트(games/defense/main.cpp:534 `defense::LoadBalanceScript(vfs.Resolve("assets/balance.script"), &externalBalance)`)와 전용 서버(games/defense/server_main.cpp:137)가 공유하므로 두 제품 모두 영향을 받는다. README.md:115 "`games/defense/assets/`의 파일은 재빌드 없이 교체할 수 있다"가 이 파일을 신뢰 경계 밖 콘텐츠로 선언하고 있고, 문서는 실패 시 "전체 폴백"을 약속하지만 실제로는 실패 판정에 도달하기 전에 멈춘다. 파싱·범위 검증은 정상 작동하므로 "없음"이 아니라 "과장"이다.

**방향**: `tools::Script`에 전역 스텝 예산(Eval 호출 카운터)을 두고 초과 시 `ok_ = false` 로 종료시키면 기존 `if (!script.Run(source)) return fallback;` 경로(content.hpp:49-52)가 그대로 폴백을 수행한다. 또는 문서에서 "실행 예산은 없고 중첩 반복은 사실상 무한하다"를 명시한다(PROJECTS_PLAN.md:33 이 languages_lab에 "실행 예산"을 요구하는 것과 대비된다).

---

## 9. [높음] 제품의 신뢰 전송·스냅샷 계층이 include/net 의 검증된 구현과 별개로 손으로 3벌 재구현되어 있다 — "중복 없는 최종 통합 소스" 위반

> **[P0로 닫힘 · 2026-08-13]** 제품이 `net/`의 신뢰 채널·세션을 실제로 호출한다. `games/defense/main.cpp`는 29줄, `server_main.cpp`는 18줄로 줄었고 프레임 루프·프로토콜 배선은 `include/game/defense_app.hpp`·`defense_server.hpp`가 갖는다.

<sub>관점: README·docs 주장 대 코드 실재 대조</sub>

**근거**

```text
헤더 포함 폐포 분석(main.cpp + server_main.cpp 기점, 89개 헤더 중 25개 도달) 결과 `net/reliable_channel.hpp`, `net/snapshot.hpp`, `net/session.hpp`, `net/packet.hpp`, `net/rtt.hpp` 는 제품에서 도달 불가이며 각각 tests/test_net_reliable.cpp, tests/test_net_snapshot.cpp, tests/test_net_session.cpp, tests/test_net_transport.cpp 만이 include한다.
제품 쪽 별도 구현: games/defense/main.cpp:673 `if (command.sequence == lastRemoteCommand + 1) {`, main.cpp:686 `if (!pending.empty() && now - lastResend >= std::chrono::milliseconds(100)) {`, games/defense/server_main.cpp:234 `command.sequence != client->second.lastCommand + 1`.
테스트 쪽 또 한 벌: tests/test_defense_network.cpp:101-127(손실 35%·지연·재정렬 아래 호스트/클라 루프를 테스트 안에서 다시 작성), tests/test_defense_process.cpp:36-96(RunHost/RunClient).
한편 README.md:137 은 "    net/                  loopback 테스트 백엔드, 실제 UDP, 신뢰 채널, 스냅샷", docs/architecture.md:140 은 "| 제품 경로 | `game`, `math`, `physics`, `render`, `engine` 입력·파티클, `tools` 스크립트, `net`, `serialize`, `db`, `live`, `platform` |" 라고 적어 `net` 전체를 제품 경로로 제시한다.
```

PROJECTS_PLAN.md:10 은 mega의 완료 상태를 "중복 없는 최종 통합 소스와 실제 게임"으로 정의하고, :12-13 은 "단계별 소스 복사본은 코드 드리프트와 검증 중복을 만들므로 보관하지 않으며"라고 못박는다. stage 디렉터리는 제거됐지만 같은 종류의 드리프트가 헤더 계층에서 재현되고 있다: 누적 ACK·재전송·중복 억제라는 동일 개념이 (1) 검증되지만 안 쓰이는 라이브러리, (2) 클라이언트 제품, (3) 전용 서버 제품, (4) 테스트 하네스 네 곳에 따로 존재한다. 결과적으로 test_net_reliable.cpp가 통과해도 제품의 신뢰성 로직에 대해서는 아무것도 보증하지 않는다.

**방향**: 제품이 `net::ReliableOrderedChannel`/`net::SnapshotBuffer`를 실제로 쓰도록 통합하거나, 반대로 쓰지 않기로 확정하고 미사용 net 모듈을 architecture.md:143 의 "선택 시뮬레이션" 행처럼 명시적으로 비제품 경로로 강등해 README:137 의 표기를 고친다.

---

## 10. [높음] README가 명시한 "클라이언트 60Hz 보간"은 테스트 0건 — 구현이 main.cpp 익명 네임스페이스에 갇혀 있다

> **[P0로 닫힘 · 2026-08-13]** `ReplicaBuffer` 재구현이 제거되고 클라이언트 루프가 `include/game/defense_app.hpp`의 `DefenseApp::Step()`으로 올라갔다. `tests/test_defense_client_loop.cpp` 신설 — 실제 UDP 소켓 위에서 제품 프레임 루프를 돌린다.

<sub>관점: README·docs 주장 대 코드 실재 대조</sub>

**근거**

```text
README.md:21 "- 상태 복제: 20Hz 제한 크기 스냅샷, 안정 엔티티 ID, 클라이언트 60Hz 보간" 및 docs/architecture.md:63-67(8개 보관, 최신-3틱 렌더, double 누적, tick wrap 시 epoch 재시작까지 상세 서술). 구현은 games/defense/main.cpp:53 `namespace {` … :155 `class ReplicaBuffer {` … :512 `}  // namespace` 안에 있다. 저장소 전체 grep에서 `ReplicaBuffer` 는 main.cpp:155(정의)와 main.cpp:608(`ReplicaBuffer replicas;`) 두 곳뿐이며 tests/ 에는 없다. CMakeLists.txt:35-44 의 테스트 타깃은 games/defense 를 include 경로로만 추가할 뿐 main.cpp 를 링크하지 않는다. 반대로 tests/test_net_snapshot.cpp:30 `TestInterpolateMidpoint()` 가 검증하는 `net::SnapshotBuffer`(include/net/snapshot.hpp)는 제품이 링크하지 않는다.
```

architecture.md:66-67 "보간 시각은 `double`로 누적해 장시간 세션에서 `float`가 연속 정수 tick을 구분하지 못하는 문제를 피한다. tick이 32비트 경계를 넘으면 wrap 이전 표본을 버리고 새 보간 epoch를 시작한다" 같은 구체적 불변식은 문서에만 존재하고 어떤 자동 테스트도 이를 확인하지 않는다. docs/architecture.md:152-168 "검증 경계" 목록에도 클라이언트 보간 항목이 없어, 문서 스스로도 이 공백을 드러내지 않는다.

**방향**: `ReplicaBuffer`를 games/defense 의 헤더(예: replica.hpp)로 옮기면 CMakeLists.txt:40-41 이 이미 tests 에 games/defense 를 include 경로로 넣고 있으므로 곧바로 단위 테스트를 붙일 수 있다. 최소한 tick wrap epoch 재시작과 8개 링 버퍼 경계만이라도 검증 대상에 넣는다.

---

## 11. [높음] 테스트가 검증하는 네트워크 스택과 게임이 실제로 쓰는 네트워크 스택이 다르다

> **[P0로 닫힘 · 2026-08-13]** `mega_core` 단일 정적 라이브러리를 제품과 테스트가 같이 링크한다. 테스트가 엔진 `.cpp`를 개별 나열하거나 `games/defense`를 인클루드 루트로 여는 경로가 CMake에서 제거됐다.

<sub>관점: 빌드·테스트 실제 실행과 검증력 판정</sub>

**근거**

```text
제품 도달성 분석(main.cpp/server_main.cpp에서 #include 전이 폐포): include/ 89개 파일 중 실행 파일에서 도달 가능 25개, 도달 불가 64개.

도달 불가 목록에 포함: include/net/reliable_channel.hpp, include/net/snapshot.hpp, include/net/session.hpp, include/net/packet.hpp, include/net/rtt.hpp.

저장소 전체 grep 결과 이 다섯 헤더를 include하는 곳은 tests/ 뿐이다:
  ./tests/test_net_reliable.cpp:2:#include "net/reliable_channel.hpp"
  ./tests/test_net_session.cpp:2:#include "net/session.hpp"
  ./tests/test_net_snapshot.cpp:2:#include "net/snapshot.hpp"
  ./tests/test_net_transport.cpp:2:#include "net/packet.hpp"
(include/net/session.hpp만이 net/snapshot.hpp를 쓰는데 session.hpp 자체가 제품 미사용)

제품은 같은 기능을 games/defense/main.cpp 안에 재구현한다:
  main.cpp:155  class ReplicaBuffer {          ← net::SnapshotBuffer 와 같은 역할, 73줄
  main.cpp:686  if (!pending.empty() && now - lastResend >= std::chrono::milliseconds(100)) {  ← 재전송
  main.cpp:673  if (command.sequence == lastRemoteCommand + 1) {  ← 중복/재정렬 억제

PROJECTS_PLAN.md:10  "| `projects/mega/` | 중복 없는 최종 통합 소스와 실제 게임 |"
```

tests/test_net_snapshot.cpp는 순서 역전 거부, 32비트 wrap 재시작, keep-limit, 보간, 클램프를 전부 검사한다(TestRejectOldAndResetAtWrap 등). tests/test_net_reliable.cpp는 35~40% 손실·재정렬 하 exactly-once 전달과 시퀀스 wrap을 검사한다. 품질은 높다. 문제는 이 코드가 게임에 링크되지 않는다는 점이다. 게임이 실제로 실행하는 동등 로직은 main.cpp 안 익명 네임스페이스에 있고 테스트에서 참조할 방법이 없다. 결과적으로 mega는 "중복 없는 최종 통합 소스"가 아니라, 검증된 미사용 구현과 미검증 사용 구현이 짝으로 존재하는 상태다.

**방향**: 둘 중 하나로 수렴시켜야 한다. ReplicaBuffer/재전송/ack 로직을 include/ 아래 헤더로 올려 기존 net 테스트가 그 구현을 겨냥하게 하거나, 반대로 제품이 net/snapshot.hpp·reliable_channel.hpp를 실제로 사용하고 main.cpp의 사본을 지우는 것. 어느 쪽이든 "테스트되는 구현 = 배포되는 구현"이 되어야 PROJECTS_PLAN:10의 완료 조건을 만족한다.

---

## 12. [높음] 플레이 가능한 클라이언트의 네트워크 루프를 무력화해도 51개 테스트가 전부 통과한다

> **[P0로 닫힘 · 2026-08-13]** 변이 3건(호스트 중복 억제 · 클라 스냅샷 순서 · 명령 재전송)이 이제 전부 검출된다. `--selftest`가 조기 반환하지 않고 제품 프레임 루프를 N틱 구동하도록 바뀌었고, `test_defense_client_loop`가 `PumpClient()`의 배선을 직접 겨냥한다.

<sub>관점: 빌드·테스트 실제 실행과 검증력 판정</sub>

**근거**

```text
원본 미변경 복사본에서 변이 테스트 실행(각 변이마다 재빌드 후 ctest 전체 실행). 통제군이 정상 작동함을 먼저 확인:
  M0 CONTROL: SequenceMoreRecent inverted  -> CAUGHT by: #14, #32, #34
  M1 build-on-path/blocked allowed         -> CAUGHT by: #14, #20
  M7 sell refund 70% -> 200% (economy)     -> CAUGHT by: #21
  M12 admission: audience+expiry removed   -> CAUGHT by: #1

제품 코드(games/defense/main.cpp) 변이 결과:
  M9  PRODUCT host loop: dup/reorder suppression removed      -> *** SURVIVED (no test failed) ***
      (main.cpp:673 `if (command.sequence == lastRemoteCommand + 1) {` → `if (true) {`)
  M10 PRODUCT ReplicaBuffer: snapshot ordering guard removed  -> *** SURVIVED (no test failed) ***
      (main.cpp:160 `if (!net::SequenceMoreRecent(snapshot.tick, newest)) return false;` → `if (false) return false;`)
  M11 PRODUCT client: pending-command resend disabled         -> *** SURVIVED (no test failed) ***
      (main.cpp:686 재전송 조건 → `if (false) {`)

README.md:20  "- 명령 신뢰성: 단조 시퀀스, 재전송, 누적 확인, 중복 억제"
README.md:21  "- 상태 복제: 20Hz 제한 크기 스냅샷, 안정 엔티티 ID, 클라이언트 60Hz 보간"
PROJECTS_PLAN.md:110  "- sequence·ack·resend·duplicate suppression"
```

mega_defense가 ctest에서 실행되는 유일한 경로는 CMakeLists.txt:59 `add_test(NAME mega_defense_selftest COMMAND mega_defense --selftest)`이고, main.cpp:521 `if (options.selftest) return RunSelfTest();`가 line 523 이후의 Windows 프런트엔드보다 먼저 반환한다. 따라서 VFS/에셋 로드, 밸런스 스크립트 연결, 창·입력·렌더·파티클·오디오, UDP 호스트/클라이언트 루프, ReplicaBuffer, 진행도 기록, 지표까지 전부 ctest가 한 번도 진입하지 않는다. RunSelfTest는 main.cpp:485-510의 26줄로, GameWorld 시뮬레이션과 스냅샷 인코드/디코드만 확인한다. README:20-21이 자동 검증된 기능으로 나열한 신뢰성·복제 항목은 이 세 변이가 살아남은 것으로 반증된다.

**방향**: main.cpp의 순수 로직(ReplicaBuffer, 명령 큐/ack 정리, CaptureView)을 games/defense/ 아래 테스트 가능한 헤더로 분리하고, 손실·재정렬 주입 아래 순서·중복·재전송 수렴을 그 구현에 대해 검사하는 테스트를 추가한다. 그 전까지 README의 해당 줄은 "수동 확인"으로 표기하는 편이 실제와 맞는다.

---

## 13. [높음] "독립 호스트·클라이언트 프로세스" 테스트가 제품 실행 파일을 실행하지 않는다

> **[P0로 닫힘 · 2026-08-13]** `tests/test_defense_coop_process.cpp`가 `argv[0]`이 아니라 **제품 실행 파일** `mega_defense.exe`를 `--host` / `--join`으로 스폰한다. 테스트 안의 `RunHost`/`RunClient` 스텁 재구현은 사라졌다.

<sub>관점: 빌드·테스트 실제 실행과 검증력 판정</sub>

**근거**

```text
tests/test_defense_process.cpp:154,156 — 스폰 대상이 argv[0](테스트 자신)이다.
    CHECK(Spawn(argv[0], "--child-host", port, host));
    CHECK(Spawn(argv[0], "--child-client", port, client));
자식 프로세스가 실행하는 호스트 로직은 같은 파일 안의 테스트 전용 재구현이다(test_defense_process.cpp:36-61 `int RunHost(uint16_t port)`, :63-96 `int RunClient(uint16_t port)`).

대조군: tests/test_dedicated_process.cpp:115는 진짜 제품 바이너리를 스폰한다.
    server /= "mega_defense_server.exe";

PROJECTS_PLAN.md:105  "- 하나의 `mega_defense` 실행 파일을 `--host`와 `--join` 모드의 독립 프로세스로 실행"
PROJECTS_PLAN.md:114  "- 독립 호스트·클라이언트 OS 프로세스의 Hello→명령→권위 snapshot 테스트"
docs/development.md §13 표: "| 프로세스 | 독립 listen host·클라이언트, 전용 서버·두 UDP 끝점의 권위 상태 |"
```

두 프로세스가 실제 UDP로 Hello→Command→Snapshot을 주고받는다는 사실 자체는 검증된다. 그러나 검증되는 것은 wire 포맷과 소켓이지, `mega_defense --host` / `mega_defense --join`이라는 제품 실행 모드가 아니다. 전용 서버 쪽(test_dedicated_process)은 실제 exe를 띄우므로 같은 저장소 안에 옳은 패턴이 이미 존재한다. 이 비대칭이 저자가 느낀 위화감의 큰 부분일 가능성이 높다. 참고로 test_dedicated_process도 정상 입장 경로만 확인하며, server_main.cpp:207-211의 tokenConflict/playerConflict/matchConflict/full 거부 분기와 :233 명령 시퀀스 검사는 어떤 테스트도 실행하지 않는다(docs/architecture.md는 이 거부 규칙들을 명시적으로 보장으로 서술한다).

**방향**: test_dedicated_process와 같은 방식으로 mega_defense.exe를 --host/--join으로 스폰하는 테스트로 바꾼다. 대화형 프런트엔드가 걸림돌이면 --host/--join에 --run-seconds 류의 헤드리스 종료 옵션을 제품에 추가하는 편이(전용 서버가 이미 그렇게 한다) 스텁 재구현보다 실제를 검증한다.

---

## 14. [높음] 저장소에 변경 이력이 존재하지 않는다 — .git이 완전히 빈 디렉터리다

> **[닫힘 · 2026-08-14]** 저장소가 실제 git 저장소가 됐다. `git log`가 커밋을 반환한다 — `870198c` (2026-08-14 19:33, 1,618 파일, 그중 `projects/mega/` 178개). 감사가 지적한 상태(`.git`이 빈 디렉터리, 모든 git 명령이 `fatal: not a git repository`)는 사라졌고, bisect·이력 대조의 전제가 성립한다. **다만 이것은 감사의 "방향"이 급하다고 적은 그 초기 커밋 한 개다** — PROJECTS_PLAN:12의 "개발 순서는 변경 이력으로 설명한다"가 실제로 성립하려면 이후 작업이 이력으로 쌓여야 하고, 이번 2026-08-14 작업분은 아직 워킹 트리에 커밋되지 않은 상태다(항목 26도 같다).
>
> **[미해소 · 2026-08-13]** `git log`가 지금도 `your current branch 'master' does not have any commits yet`로 실패한다. P0 계획의 "git 실사용 개시"는 이뤄지지 않았다.

<sub>관점: 빌드·테스트 실제 실행과 검증력 판정</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/.git 은 존재하지만 내용물이 0개다.
  Get-ChildItem -Force 'C:\Users\REIN\Documents\VeLog\.git' | Measure-Object  →  Count: 0
  Get-ChildItem -Force -Recurse (같은 경로)                                    →  Count: 0
  cat .git/HEAD → No such file or directory
결과적으로 모든 git 명령이 실패한다:
  git -C 'C:\Users\REIN\Documents\VeLog' log --oneline -5   → fatal: not a git repository (or any of the parent directories): .git
  git -C '...\projects\mega' status                          → fatal: not a git repository
projects/mini, projects/mega 어디에도 별도 .git은 없다.

PROJECTS_PLAN.md:12-13  "개발 순서는 문서와 변경 이력으로 설명한다. 단계별 소스 복사본은 코드 드리프트와 검증\n중복을 만들므로 보관하지 않으며, `projects/mega/`의 최종 코드만 권위 있는 원본으로 둔다."
```

계획 문서는 단계별 소스 스냅숏을 의도적으로 폐기하고 그 대가로 "변경 이력"에 개발 순서 설명을 위임했다. 그 변경 이력이 존재하지 않는다. 검증 관점에서 이것은 치명적인 부작용을 낳는다 — 어떤 커밋에서 어떤 테스트가 추가·삭제되었는지 추적할 수 없고, 회귀가 생겼을 때 bisect가 불가능하며, docs/development.md가 서술하는 "구현 과정"을 코드로 대조할 방법이 없다. 저자가 "mega가 이상하다"고 느낀 배경에 stage01~13 계층이 사라진 흔적이 남지 않은 점이 있다면 원인은 여기다.

**방향**: 먼저 현 상태를 초기 커밋으로 남겨 이후 감사·bisect가 가능하게 만드는 것이 급하다. 그 뒤 PROJECTS_PLAN:12의 문장이 실제로 성립하려면 docs/development.md의 각 절이 최소한 어떤 커밋 범위에 대응하는지 연결되어야 한다. (이 항목은 저장소 변경이 필요하므로 판단은 저자에게 맡긴다.)

---

## 15. [중간] languages 축(7편·4,977줄)이 상수 5줄짜리 설정 파일로 축소 — 호스트 바인딩·바이트코드·GC·리플렉션 대응물 없음

> **[미해소 · 2026-08-13]** languages 축(바이트코드 VM·호스트 바인딩·리플렉션)은 재설계안 P6 대상.

<sub>관점: 이론 시리즈 ↔ mega 통합 매핑 감사</sub>

**근거**

```text
대응물 전량 = `include/tools/script.hpp` 211줄(렉서→파서→AST→트리 워킹 인터프리터).

게임에서 실제 실행되는 스크립트 `games/defense/assets/balance.script` 전문(5줄):
```
start_gold = 420
core_hp = 260
enemy_hp_multiplier = 1.0
enemy_speed_multiplier = 1.0
reward_multiplier = 1.0
```
if/else/while/괄호/비교 연산 등 언어 기능을 하나도 쓰지 않는다. 실행 결과는 `games/defense/content.hpp:52-73`에서 5개 변수를 `GetOr`로 꺼내 범위 검사하는 것이 전부다.

`Script` 클래스의 공개 API는 `Run(src)`, `Get(name,out)`, `GetOr(name,def)` 3개뿐(script.hpp:26-43) — 스크립트가 호스트 함수를 호출할 통로가 없다(엔진 바인딩 부재).

`docs/architecture.md`: "스크립트는 호스트와 싱글플레이 초기화 때만 실행되고 매 틱 실행되지 않는다."

미대응 챕터: part02 실행 런타임(바이트코드 VM), part03 GC, part04 컴파일러 최적화·ABI, part05 JIT, part06 엔진 스크립팅(바인딩·핫 리로드·샌드박스·디버깅), part07 리플렉션·메타데이터. `grep -rniE "bytecode|opcode|gc |mark.?sweep|jit" include/` → 0건.

`PROJECTS_PLAN.md:32`는 이 축의 검증을 "`languages_lab`: 토큰 위치·우선순위, 바이트코드 제어 흐름·스택 검증, 실행 예산, 호스트 바인딩"으로 정의 — mega에는 넷 중 셋이 없다(실행 예산은 script.hpp:189의 `guard > 1000000` 루프 상한만).
```

languages 축의 게임 내 실체는 "5개 숫자를 담은 설정 파일"이다. 파서·AST·인터프리터는 있지만 그 능력이 게임에서 한 번도 발휘되지 않으므로, 이 축은 "부분 통합"보다는 "파일은 있으나 개념이 도달하지 않은" 쪽에 가깝다. 특히 part06(엔진 스크립팅)이 이 축의 게임 개발자용 핵심인데, 스크립트가 엔진 객체를 만지거나 호출할 수단이 전혀 없어 대응물이 성립하지 않는다.

`include/tools/script.hpp:5` 주석은 이 파일을 "(tools 스크립팅 챕터)" 소산으로 표기하지만, `tools/`에는 스크립팅 챕터가 없다(part01, part03~06만 존재하고 part02는 결번). 소스 표기가 존재하지 않는 문서를 가리킨다.

**방향**: 이 축을 mega에서 실제로 살리려면 스크립트가 데이터가 아니라 **동작**을 소유해야 한다 — 예: 웨이브 규칙(`GenerateWave`, game.hpp:279-302)을 스크립트로 옮기고 호스트 함수 바인딩(spawn/reward 조회)을 노출하면 part06의 바인딩·샌드박스·실행 예산이 전부 실제 문제가 된다. 그럴 계획이 없다면 이 축의 소유자를 `mini/languages_lab`으로 명시하고, `include/tools/script.hpp:5`의 존재하지 않는 챕터 참조를 고친다.

---

## 16. [중간] tools·platform·engine 축: 있는 모듈을 게임이 우회하고 같은 일을 다시 구현

> **[미해소 · 2026-08-13]** tools·platform·engine의 우회 지점(레벨 연결성 검증, 오디오 믹서, capability 질의)은 그대로. P2·P8·P9 대상.

<sub>관점: 이론 시리즈 ↔ mega 통합 매핑 감사</sub>

**근거**

```text
tools(5편) — `tools/level_editor.hpp`(94), `procgen.hpp`(68), `asset_pipeline.hpp`(79) 전부 게임 도달 불가. 유일하게 도달하는 `script.hpp`는 위 항목 참조.
- `include/tools/level_editor.hpp:3-5`가 제공하는 "스폰(S)에서 목표(G)까지 도달 가능한가를 flood fill 로 검사"를, 게임은 쓰지 않고 `include/game/game.hpp:105-159 DefenseMap::FromAscii`에 별도 연결성 검증을 다시 구현했다.
- 대응물 없음: part04 디버그, part06 빌드

platform(7편) — `platform/vfs.hpp`만 게임 사용(main.cpp:16,527-528). `byteorder.hpp`·`capability.hpp`·`detect.hpp` 게임 미호출, `console.hpp`(22줄)는 게임·테스트 모두 미도달.
- `console.hpp`는 part02(콘솔 게임기)가 아니라 part07(Windows 텍스트·코드페이지) 대응물이다 — 이름이 축의 챕터명과 충돌한다.
- 플레이 가능 경로가 Windows 전용: `games/defense/main.cpp:523-525` `#ifndef _WIN32 ... "interactive frontend is currently available on Windows; use --selftest"`. `vk_present.hpp:13 #ifdef _WIN32`, `win_app.hpp`, `audio.hpp:10 #ifdef _WIN32`, CI도 windows-latest 단일(`.github/workflows/ci.yml`).
- 대응물 없음: part01 모바일, part02 콘솔, part04 웹, part06 클라우드. part05 크로스플랫폼은 detect/capability가 있으나 게임이 질의하지 않는다.

engine(9편) — 7파일 중 게임 사용은 `input.hpp`·`particles.hpp` 2개뿐. `anim.hpp`·`skeleton.hpp`·`ik.hpp`·`resource.hpp`·`audio.hpp` 미호출.
- `include/engine/audio.hpp`는 3D 거리 감쇠·equal-power 패닝·다중 소스 믹싱을 구현했으나 게임은 이를 우회하고 `games/defense/audio.hpp`에서 winmm waveOut으로 모노 톤 5개를 fire-and-forget 재생한다(audio.hpp:37-41 `Synth(...)` 5회). 감쇠도 패닝도 믹싱도 없다.
- 대응물 없음: part03 VFX(particles가 일부), part06 UI, part07 카메라, part08 런타임 오케스트레이션, part09 월드 스트리밍·영속
```

세 축 모두 같은 패턴이다: 이론에 대응하는 모듈이 `include/`에 존재하지만 게임이 그것을 부르지 않고, 필요한 최소 기능을 제품 코드 안에 다시 짠다. 레벨 연결성 검증(tools/level_editor ↔ game.hpp FromAscii)과 오디오(engine/audio ↔ games/defense/audio)가 가장 명확한 사례로, 각각 같은 저장소 안에 같은 문제의 두 해답이 공존한다.

platform 축은 특히 선언과 실제의 간격이 크다. `capability.hpp:3-5`는 "상위 코드는 #ifdef 대신 '터치 되나?', '메모리 예산은?' 을 런타임으로 묻는다"고 적었지만, 실제 제품 코드는 `#ifdef _WIN32`로 프런트엔드 전체를 감싸고 비-Windows에서는 실행 자체를 거부한다. 이 모듈이 해결하려던 문제를 제품이 정반대 방식으로 처리하고 있다.

engine 축은 mega가 통합했다고 볼 수 있는 부분(입력 액션 매핑, 파티클)이 실재하므로 부분 통합이지만, 9편 중 7편은 코드로 도달하지 않는다.

**방향**: 우회 지점 두 곳부터 붙이는 것이 비용 대비 효과가 크다: `DefenseMap::FromAscii`를 `tools/level_editor.hpp`의 검증 위에 올리고, `SfxPlayer`가 `engine/audio.hpp`의 믹서를 거쳐 waveOut에 내보내게 하면(적 위치 기준 패닝·감쇠) 두 모듈이 제품 경로로 들어오고 중복이 사라진다. platform은 `capability.hpp` 프로파일을 실제 질의 지점(창 크기·오디오 활성·에셋 경로)에 연결하거나, 아니면 Windows 전용임을 README에 축 수준에서 명시한다.

---

## 17. [중간] README·architecture의 검증 주장과 ctest 실제가 불일치 (Vulkan 자동 검증 없음, 테스트 개수 불일치)

> **[부분 · 2026-08-14 — 두 headline은 닫혔고 잔여 1건]** Vulkan 절반이 항목 7·24와 함께 닫혔다. `mega_defense_gpu_selftest`가 ctest에 등록돼 "자동 검증한다"는 문장이 근거를 얻었고, 동시에 세 문서가 "이 하나는 환경에 따라 Skipped가 될 수 있고 Skipped는 검증됐다는 뜻이 아니다"를 함께 적어 과장을 없앴다. `development.md`는 **56**(그중 55개는 어디서든 Passed여야 함)으로 갱신됐다. **다만 이 항목이 함께 지적한 것 하나는 남아 있다** — `include/render/framebuffer.hpp:3`의 "이 머신엔 Vulkan SDK 가 없어 GPU 백엔드를 빌드·검증할 수 없다"는 주석은 지금도 그대로이고, 같은 저장소가 README에서는 Vulkan 백엔드를 지원 기능으로 적으므로 소스 주석과 README가 여전히 반대 사실을 말한다.
>
> **[부분 · 2026-08-13]** 테스트 개수 불일치는 해소 — `docs/architecture.md`에서 하드코딩된 개수가 사라졌고 `docs/development.md:224`는 실제와 같은 **55**를 적는다. **Vulkan present를 "자동 검증"으로 적은 주장은 그대로**(항목 7·24 참조).

<sub>관점: 이론 시리즈 ↔ mega 통합 매핑 감사</sub>

**근거**

```text
`projects/mega/README.md:27-29` "검증 환경은 MSVC 19.51 Release다. 실제 로컬 UDP 왕복, 독립 호스트·클라이언트 프로세스 왕복, 전용 서버와 두 클라이언트의 토큰 입장, 손실·지연·재정렬 복구, 반복 매치 soak, **Vulkan 스왑체인 5프레임 present를 자동 검증한다**."
`docs/architecture.md:166` "- GPU: 실제 Vulkan 장치·스왑체인 5프레임 present" (검증 경계 항목)

실제 `ctest --test-dir build -C Release -N` 출력: Test #1~#51, 마지막이 `mega_defense_selftest`. gpu/vulkan을 이름에 포함한 테스트 0개. `CMakeLists.txt`에서 등록되는 테스트는 `tests/test_*.cpp` glob 50개 + `add_test(NAME mega_defense_selftest COMMAND mega_defense --selftest)` 1개뿐이며, `--gpu-selftest`(main.cpp:96-98, 624-638)를 부르는 add_test는 없다.

`ctest --test-dir build -C Release` 실행 결과: `100% tests passed out of 51`.
`docs/architecture.md:168` "- 전체 코드베이스: CTest 49개" vs `README.md:151` "51개 테스트" vs 실제 51개.

또한 `.github/workflows/ci.yml` 말미 주석: "Vulkan SDK가 없는 러너에서는 GDI 표시 경로만 빌드된다" — CI에서 Vulkan 경로는 컴파일조차 되지 않는다.

`include/render/framebuffer.hpp:3` "// 이 머신엔 Vulkan SDK 가 없어 GPU 백엔드를 빌드·검증할 수 없다." — 현재 Vulkan SDK가 설치돼 있고 `build/bin/Release/`에 `blit.frag.spv`·`fullscreen.vert.spv`가 생성돼 있으며 README.md:23은 Vulkan 표시 백엔드를 지원 기능으로 적는다.
```

`PROJECTS_PLAN.md:141`은 "README에는 구현된 기능, 시뮬레이션으로만 검증한 기능, 교체 지점만 있는 기능을 구분해 적는다"고 요구한다. Vulkan present는 "수동으로 실행하면 되는 기능"이지 "자동 검증되는 기능"이 아닌데 README는 후자로 적었다. GPU 경로는 CI에서 빌드도 안 되고 로컬 ctest에도 없으므로, 회귀가 발생해도 아무도 알아채지 못한다.

테스트 개수 49/51 불일치는 사소하지만, docs/architecture.md가 코드 상태를 따라가지 못하고 있다는 신호다. framebuffer.hpp:3의 "Vulkan SDK가 없어"는 이제 사실이 아닌 서술이 소스 주석에 남아 있는 경우로, 같은 저장소 안에서 README(있다)와 헤더 주석(없다)이 반대 사실을 말한다.

**방향**: `--gpu-selftest`를 ctest에 등록하되 Vulkan 미탐지 시 SKIP 처리하거나(CMake `if(Vulkan_FOUND)` 안에서 add_test), 등록하지 않는다면 README.md:29의 "자동 검증한다" 목록에서 Vulkan 항목을 빼고 "수동 확인"으로 분리한다. docs/architecture.md:168의 테스트 개수는 하드코딩 대신 서술을 없애거나 실제 값으로 맞추고, framebuffer.hpp:3의 낡은 주석을 갱신한다.

---

## 18. [중간] "독립 호스트·클라이언트 프로세스 왕복" 테스트는 제품 실행 파일이 아니라 테스트 실행 파일 자신을 두 프로세스로 띄운다

> **[P0로 닫힘 · 2026-08-13]** 항목 13과 같은 근거. 제품 바이너리 두 개가 실제 UDP로 협동 왕복을 돈다.

<sub>관점: README·docs 주장 대 코드 실재 대조</sub>

**근거**

```text
tests/test_defense_process.cpp:154 `CHECK(Spawn(argv[0], "--child-host", port, host));` / :156 `CHECK(Spawn(argv[0], "--child-client", port, client));` — 자식 프로세스는 같은 테스트 바이너리이며 호스트·클라 루프는 :36 `int RunHost(uint16_t port)` 와 :63 `int RunClient(uint16_t port)` 로 테스트 안에 다시 작성돼 있다. 대조적으로 tests/test_dedicated_process.cpp:113 `server /= "mega_defense_server.exe";` 는 제품 바이너리를 기동한다. 문서 주장: README.md:27-28 "실제 로컬 UDP 왕복, 독립 호스트·클라이언트 프로세스 왕복 … 자동 검증한다", PROJECTS_PLAN.md:114 "독립 호스트·클라이언트 OS 프로세스의 Hello→명령→권위 snapshot 테스트", REVIEW_STATE.md:91 "테스트가 독립 호스트·클라이언트 OS 프로세스를 기동해 실제 UDP 세션 통과".
```

문장 자체는 거짓이 아니다(진짜로 두 OS 프로세스가 실제 UDP로 통신한다). 다만 검증되는 것은 wire 포맷과 UdpTransport이지, `mega_defense --host` / `--join` 이 실제로 쓰는 루프(60Hz 누적기, 500ms Hello, 100ms 재전송, hostTick%3 스냅샷, ReplicaBuffer)는 아니다. 전용 서버 경로만 제품 프로세스가 검증된다. 결과적으로 README:16 "실제 2인 네트워크: 독립된 호스트·클라이언트 프로세스가 IPv4 UDP로 같은 세계를 공유"의 listen-host 토폴로지는 자동 검증 밖에 있다.

**방향**: 두 문장을 "프로토콜 수준의 2프로세스 UDP 왕복"과 "제품 바이너리 프로세스 검증(전용 서버 경로만)"으로 나눠 적거나, `mega_defense`에 `--host --run-seconds N` 같은 헤드리스 옵션을 추가해 실제 제품 루프를 프로세스 테스트에 넣는다.

---

## 19. [중간] CTest 개수가 문서마다 49와 51로 갈린다 (실제 51)

> **[P0로 닫힘 · 2026-08-13]** 실제 55개(`tests/test_*.cpp` 52 + `mega_defense_selftest` + `layer_rules` + `reachability_gate`). `development.md`가 55로 갱신됐고 `architecture.md`는 개수 서술을 없앴다.

<sub>관점: README·docs 주장 대 코드 실재 대조</sub>

**근거**

```text
실제: `ctest -N` → "Total Tests: 51"; 전체 실행 → "100% tests passed out of 51" (Total Test time 5.22 sec). 51 = tests/test_*.cpp 50개 + CMakeLists.txt:59 의 mega_defense_selftest.
51로 적은 곳: README.md:151 "…같은 코드베이스의 독립 라이브러리로 51개", docs/development.md:219 "CTest는 51개 대상을 실행한다."
49로 적은 곳: docs/architecture.md:168 "- 전체 코드베이스: CTest 49개", REVIEW_STATE.md:89 "- CTest 49/49".
```

같은 저장소의 네 문서가 같은 수치를 두 값으로 말한다. REVIEW_STATE.md는 "이 문서는 현재 사실과 검증 결과만 기록한다"(:10-11)고 선언한 현행 상태 문서이므로 특히 문제가 된다. 검증 자체는 통과하므로 품질 문제라기보다 문서 정합성 문제다.

**방향**: architecture.md:168 과 REVIEW_STATE.md:89 를 51로 맞추거나, 숫자를 문서에서 빼고 `ctest -N` 재현 명령만 남긴다(재발 방지).

---

## 20. [중간] "여러 축을 실제 게임으로 통합"의 실측치: include/ 89개 헤더 중 25개(9310줄 중 3629줄)만 제품에 도달하며, 미통합 목록이 문서에서 축소돼 있다

> **[P0로 닫힘 · 2026-08-13]** 미통합 목록이 축소돼 있던 문제가 사라졌다 — 빌드가 `docs/PRODUCT_LAYERS.md`를 생성해 헤더 단위 도달 현황을 소유하고, `LAB_ONLY.md`가 미도달 58개 전부에 도입 단계와 사유를 요구한다. 손으로 줄일 수 없다.

<sub>관점: README·docs 주장 대 코드 실재 대조</sub>

**근거**

```text
제품(main.cpp + server_main.cpp) 기점 헤더 포함 폐포: 도달 25개(db/kv_store, db/records, db/repository, engine/input, engine/particles, game/defense_protocol, game/game, live/admission_token, live/analytics, math/scalar, math/spatial/aabb, math/vec, net/sequence, net/transport, net/udp_server, net/udp_transport, physics/narrow_phase/collide, physics/rigid_body, physics/shape, platform/vfs, render/framebuffer, render/raster2d, render/sprite, serialize/archive.h, tools/script) = 3629줄. include/ 전체 9310줄. 미도달 64개에는 containers/(4), io/(2), sync/(2), sched/(1), profiler/(1), engine/{anim,skeleton,ik,resource,audio}, live/{economy,patch,feature_flag,event_schedule}, tools/{level_editor,asset_pipeline,procgen}, math/{mat,quat,transform,noise,sdf,interp,spatial/bvh,spatial/quadtree,spatial/uniform_grid}, physics/{integrate,solver/sequential_impulse,world}, platform/{byteorder,capability,console,detect}, net/{packet,reliable_channel,rtt,session,snapshot} 가 포함된다.
문서의 미통합 고백 범위: README.md:150-152 "메모리 할당자, ECS, 작업 스케줄러, lockstep/rollback과 범용 AI 라이브러리는 같은 코드베이스의 독립 라이브러리로 51개 / 테스트에 포함되지만, 최대 64개 적을 다루는 권위 런타임에 측정 근거 없이 호출하지 않는다." / docs/architecture.md:143 "| 선택 시뮬레이션 | 범용 `ai`, `mp` lockstep/rollback |".
```

PROJECTS_PLAN.md:3 은 mega의 목적을 "여러 축을 실제 게임으로 통합하는 것"으로, CURRICULUM_MAP.md:111 은 "별개 샘플 묶음이 아니라 하나의 플레이 가능한 멀티플레이 디펜스로 통합한다"로 규정한다. README.md:148-149 의 제품 경로 열거는 실측과 정확히 일치해 정직하지만, 그 반대편 미통합 목록은 5개 범주로만 축소돼 있어 실제로 게임에 닿지 않는 물리 솔버·수학 행렬/쿼터니언/변환·spatial 가속구조·profiler·io·sync·sched·containers·에셋/레벨 툴·라이브옵스 모듈이 언급되지 않는다. 저자가 느낀 "이상하다"의 상당 부분이 여기일 가능성이 크다: 이론 축은 15개인데 플레이되는 코드가 닿는 축은 그 절반 이하다.

**방향**: README/architecture의 미통합 목록을 실측 기준으로 확장해 적거나(권장), 각 미통합 모듈에 "왜 제품 경로가 아닌가"의 한 줄 근거를 development.md §12 형식으로 붙인다. 어느 쪽이든 "통합"의 정의를 '한 CMake 그래프에 있음'이 아니라 '게임 실행 경로가 호출함'으로 문서에서 고정해야 한다.

---

## 21. [중간] WAL 원자성 테스트가 실제 원자성 경계(COMMIT 마커)를 건드리지 않는다

> **[닫힘 · 2026-08-14]** 감사가 제안한 시나리오를 그대로 짓는 도구(`WalPut`/`WalDel`/`WalCommit` — 구현의 레코드 형식과 1:1)를 테스트에 두고 케이스 셋을 추가했다. `TestReplayDropsCompleteUncommittedTail`은 **완전한 PUT + 완전한 DEL**을 붙인 뒤 COMMIT 마커만 뺀 로그와, 같은 바이트열에 마커 1바이트만 더한 대조군을 나란히 돌린다 — 두 로그의 차이가 그 한 바이트뿐이므로 결과 차이의 원인도 그것뿐이다(즉 (1)의 통과가 "레코드가 사실은 깨져 있어서"가 아님이 증명된다). `TestReplayMixedCommittedAndUncommitted`는 미커밋 꼬리가 앞선 커밋 값을 덮어쓰거나 지우려 드는 배치를 골라, 마커 검사가 없으면 `b`가 `"BAD"`가 되고 `c`가 사라지게 만들었다. `CHECK` 수는 **28 → 56**이다.
>
> **덤으로 실제 버그가 하나 나왔다.** WAL은 append-only이고 COMMIT 마커는 "그 앞에 쌓인 pending 전부"에 걸리므로, 크래시가 남긴 미커밋 꼬리를 파일에 그대로 둔 채 운영을 재개하면 **재기동 뒤 첫 트랜잭션이 적는 마커 하나가 그 꼬리까지 커밋해 버린다** — 원자성이 재기동 한 번을 못 넘긴다. 감사가 "구현 자체는 정상"이라 판정한 것은 1차 재생까지만 본 것이라 맞았고, 그 다음 한 걸음이 비어 있었다. `Replay()`가 마지막 COMMIT 지점(`committedEnd`)을 돌려주고 `Open()`이 거기까지 파일을 잘라내도록 고쳤으며, `TestUncommittedTailNeverResurfaces`가 재기동 → 정상 쓰기 → 재기동의 2회 왕복으로 유령 레코드가 되살아나지 않음을 확인한다.
>
> **[미해소 · 2026-08-13]** `tests/test_db_kv.cpp`는 여전히 토막난 레코드만 주입한다. COMMIT 마커 없는 완전 레코드 재생 케이스 없음. **지금 닫을 수 있다.**

<sub>관점: 빌드·테스트 실제 실행과 검증력 판정</sub>

**근거**

```text
변이 결과: M5 WAL replays UNCOMMITTED txn (atomicity broken) -> *** SURVIVED (no test failed) ***
(include/db/kv_store.hpp:157 `pending.push_back(...)` 직후에 `Apply(pending.back());`를 삽입 = 미커밋 레코드를 즉시 적용)

원인: tests/test_db_kv.cpp:108-110이 주입하는 것은 "토막난" 레코드다.
    uint8_t vl[4] = {100, 0, 0, 0};      // valLen=100 인데 실제 바이트는 부족(토막)
    std::fwrite("xx", 1, 2, f);
이 입력은 include/db/kv_store.hpp:155 `if (pos + vl > buf.size()) break;`에서 파서가 이탈하므로 :157 pending.push_back에 도달조차 하지 않는다. 즉 CHECK(!db.Has("dirty"))(test_db_kv.cpp:117)는 파서 경계 검사만 확인하고, kv_store.hpp:159 주석이 선언한 "pending 에 남은(미커밋) 트랜잭션은 버린다 — 원자성"은 실행되지 않는다.

구현 자체는 정상임을 직접 확인했다. 완전한 PUT 레코드를 쓰고 COMMIT 마커 없이 종료한 WAL을 재생시키는 프로브를 원본 헤더로 컴파일해 실행:
    complete-but-uncommitted record visible after replay: no (atomicity holds)
    committed record survived: yes

PROJECTS_PLAN.md:40  "- `db_lab`: WAL 원자성·내구성, 절단 복구, 멱등성, 동시성"
docs/architecture.md: "복구는 커밋된 완전한 접두사만 재생하므로 잘린 마지막 트랜잭션은 부분 적용되지 않는다."
```

구현은 옳지만 테스트는 그 옳음을 지키지 못한다. Replay가 미커밋 레코드를 적용하도록 회귀해도 51개 테스트가 전부 통과한다. test_db_kv.cpp:74-90의 TestTransactionRollback도 Rollback()이 txnBuf_만 비우므로 WAL에 아무것도 쓰이지 않아 재생 경로를 건드리지 않는다. 결과적으로 "WAL 원자성"이라는 이름의 테스트는 실제로는 "손상된 꼬리 파싱"만 검증한다.

**방향**: 완전한 PUT/DEL 레코드들을 WAL에 append한 뒤 COMMIT 바이트를 쓰지 않은 상태로 재생시키는 케이스를 test_db_kv.cpp에 추가한다(이미 검증한 프로브와 동일한 시나리오). 그래야 kv_store.hpp:159의 불변식이 회귀 보호를 받는다.

---

## 22. [중간] 엔티티 상한이 세 곳에 중복 상수로 존재하고, 유일한 검사는 자기 자신과 비교하는 항등식

> **[닫힘 · 2026-08-14]** 세 사본 중 프로토콜 쪽 둘(`WriteSnapshot` 지역 상수, `ReadSnapshot`의 리터럴 `16/64/20`)이 `wire::kSnapshotMaxTowers`·`kSnapshotMaxEnemies`·`kSnapshotMaxProjectiles` **하나씩**으로 합쳐졌고, 인코더와 디코더가 같은 상수를 읽는다. **감사가 제안한 "셋을 하나로"는 채택하지 않았다** — `GameWorld::kMaxTowers`는 밸런스 규칙이고 프로토콜 상한은 패킷 예산이라 지금 값이 같은 것이 우연이기 때문이다. 대신 **묶여야 할 방향만 단언한다**: `static_assert(GameWorld::kMaxTowers <= kSnapshotMaxTowers)`가 감사가 지목한 발산(월드가 더 담게 되면 초과 타워가 `min()`에 잘려 모든 스냅샷에서 조용히 사라지는 것)을 런타임이 아니라 **빌드**에서 막는다. 패킷 예산도 같이 컴파일 시간으로 내렸다 — `static_assert(kSnapshotMaxBytes <= kMaxPacketBytes)`.
>
> 테스트는 이제 **리터럴과 비교한다**(`test_defense_soak.cpp:25` → `<= 16`, 새 `TestEntityCaps`는 네 상수를 각각 리터럴에 못박는다). 항등식이 아님을 넘어, 상한이 **실제로 작동하는지**도 확인한다 — 골드도 빈 건설 가능 셀도 남은 상태에서 17번째 타워가 거절되는 것(`refusedOnEmptyCell > 0` · `world.Gold() > 0`)과, 인코더 상한을 정확히 채운 패킷은 받아들여지고 어느 컬렉션이든 하나 초과하면 거부되는 것(`ForgeSnapshot(17,64,20)` 등 3건)까지 짚는다.
>
> **[미해소 · 2026-08-13]** `game.hpp:318`·`defense_protocol.hpp:230`의 두 상수와 `:301`의 리터럴이 그대로이고, `test_defense_soak.cpp:25`는 여전히 구현이 쓰는 상수와 비교한다. **지금 닫을 수 있다.**

<sub>관점: 빌드·테스트 실제 실행과 검증력 판정</sub>

**근거**

```text
같은 프로토콜 상한이 서로 연결되지 않은 세 곳에 있다:
  include/game/game.hpp:318              static constexpr size_t kMaxTowers = 16;
  include/game/defense_protocol.hpp:229  static constexpr size_t kMaxTowers = 16;   (WriteSnapshot 지역 상수)
  include/game/defense_protocol.hpp:301  if (towerCount > 16 || enemyCount > 64 || projectileCount > 20) return false;  (리터럴)

유일한 상한 검사는 자기 자신과의 비교다:
  tests/test_defense_soak.cpp:25  CHECK(world.Towers().size() <= GameWorld::kMaxTowers);

변이 결과: M6 tower cap 16 -> 999 (snapshot bound) -> *** SURVIVED (no test failed) ***
(game.hpp:318만 999로 바꿔도 51/51 통과. tests/에 리터럴 16을 검사하는 곳은 없다 — grep '16' tests/test_game_expansion.cpp tests/test_defense_soak.cpp 결과 없음)
```

CHECK(x.size() <= GameWorld::kMaxTowers)는 구현이 쓰는 바로 그 상수와 비교하므로 상수 값이 무엇이든 항상 참이다. 과제에서 물은 "통과하는 것이 당연한 테스트(자기 자신과 비교)"의 정확한 사례다. 더 중요한 실질 위험은 세 사본의 발산이다. GameWorld::kMaxTowers가 16을 넘으면 WriteSnapshot(defense_protocol.hpp:233)이 조용히 min()으로 잘라내고 초과 타워는 모든 스냅샷에서 사라지지만, 에러도 테스트 실패도 발생하지 않는다.

**방향**: 세 사본을 하나의 상수로 통합하고(예: wire 네임스페이스가 소유, GameWorld가 그것을 참조), 테스트는 상수가 아니라 리터럴 기대값(16/64/20)과 비교하도록 바꾼다. 인코더 상한과 디코더 상한이 같음을 명시적으로 검사하는 테스트도 한 줄이면 된다.

---

## 23. [중간] 원격 바이트 한 개에 대한 범위 검사가 테스트 없이 존재한다 — 제거해도 51/51 통과

> **[닫힘 · 2026-08-14 — 지적보다 컸다]** 감사는 "테스트가 없다"를 지적했다. 테스트를 쓰자 **수정 전 15 failures**가 나왔다 — 즉 없던 것은 테스트만이 아니었다.
>
> - **범위 밖 `argument` 7건이 전부 디코드에 성공했다.** `ReadCommand`는 `type`만 검증하고 `argument`는 통과시켰다. 그런데 `argument`의 도메인(`TowerType` 집합 · `TargetMode` 집합)은 **월드를 봐야 알 수 있는 값이 아니라 와이어 포맷이 이미 아는 것**이므로 검사할 자리는 디코더다. `CommandArgumentInRange()`를 만들어 `ReadCommand`가 부르게 했다. `Upgrade`·`Sell`은 `argument`를 읽지 않으므로 0을 강제한다 — 읽지 않는 필드를 자유롭게 두면 같은 동작을 뜻하는 패킷이 256가지가 되고, 다음 버전이 그 바이트에 의미를 주는 순간 옛 클라이언트의 쓰레기가 명령이 된다.
> - **거부된 패킷이 out-param을 오염시켰다 4건.** `ReadCommand`가 필드를 호출자 구조체에 **먼저 쓰고** 나중에 거부했다. 반환값을 무시하는 호출자가 생기면 범위 밖 값이 그대로 남는다. `ReadSnapshot`과 같은 규율(지역에 풀고 전부 검증된 뒤에만 내보낸다)로 맞췄다.
> - **그리고 소비자 층 4건 — 이것이 진짜다.** 감사는 이 가드가 무너졌을 때의 피해를 "type=7 타워가 서고 모든 클라이언트의 `ReadSnapshot`이 거부해 세션이 끊긴다"로 봤다. 실제 피해는 한 단계 앞이다: 호스트는 **디코드에 성공한 명령을 곧바로 신뢰 채널에 `Accept`** 하고, `Accept`는 그 자체로 상태 변경이다(`nextExpected_`가 앞당겨진다). 기대 시퀀스에 얹힌 **위조 명령 하나가 그 자리를 태우면, 정직한 클라이언트가 같은 시퀀스로 보낼 진짜 명령은 영원히 중복 취급돼 사라진다** — 플레이어에게는 "요청한 타워가 조용히 안 선다"로 보인다. `ApplyCommand`에서 거절하는 것으로는 늦다는 뜻이고, `TestOutOfRangeCommandCannotBurnSequence`가 호스트 수신 단계와 같은 모양으로 그 경로를 재현한다.
>
> 감사가 함께 지목한 M2(`if (!reader.Done()) return false;` 제거가 살아남은 것)도 닫았다. `pop_back()`이 payload 길이 필드에서 먼저 걸려 소진 검사를 자극하지 못하던 문제는 **길이 필드를 실제 크기에 맞춰 다시 봉인**(`ResealPayloadLength`)해서 풀었고, 그 규칙을 스냅샷뿐 아니라 Command·Hello·AdmissionHello **모든 디코더**에 적용했다.
>
> **[미해소 · 2026-08-13]** `tests/test_defense_protocol.cpp`에 범위 밖 `argument` 케이스가 없다. **지금 닫을 수 있다.**

<sub>관점: 빌드·테스트 실제 실행과 검증력 판정</sub>

**근거**

```text
변이 결과: M8 ApplyCommand tower-type bounds check removed -> *** SURVIVED (no test failed) ***
대상: include/game/defense_protocol.hpp:342
    if (command.argument >= static_cast<uint8_t>(TowerType::Count)) return false;

ReadCommand(defense_protocol.hpp:216-226)는 command.type(패킷 종류)만 검증하고 command.argument는 검증하지 않는다.

docs/architecture.md: "디코더는 magic, 버전, payload 길이, 컬렉션 상한, enum 범위, 체력 범위를 검사하고 남거나 부족한 바이트가 있으면 패킷 전체를 거부한다."
```

이 가드가 사라지면 원격 클라이언트가 argument=7인 Place 명령 하나로 호스트에 type=7인 타워를 만들 수 있다(TowerSpecFor는 game.hpp:34의 폴백 스펙을 반환하므로 메모리 안전 문제는 없다). 그 이후 WriteSnapshot은 type=7을 인코딩하고, 모든 클라이언트의 ReadSnapshot이 defense_protocol.hpp:311에서 거부하므로 세션의 모든 스냅샷이 버려진다 — 원격 바이트 하나로 세션 전체가 끊긴다. 가드는 올바르게 존재하지만, 이를 지키는 테스트가 없다. 관련해서 M2(ReadSnapshot의 `if (!reader.Done()) return false;` 제거)도 살아남았다: tests/test_defense_protocol.cpp:57 `bytes.pop_back()`은 payload 길이 필드에서 먼저 걸리므로 필드 단위 잔여 바이트 검사를 자극하지 못한다.

**방향**: 범위 밖 argument를 담은 Command와, payload 길이는 일치하지만 뒤에 잉여 바이트가 붙은 Snapshot을 각각 만들어 거부되는지 확인하는 케이스를 test_defense_protocol.cpp에 추가한다.

---

## 24. [중간] "Vulkan 스왑체인 5프레임 present를 자동 검증한다"는 주장이 ctest·CI에 존재하지 않는다

> **[닫힘 · 2026-08-14]** 항목 7과 같다. `add_test(NAME mega_defense_gpu_selftest …)`가 등록됐고, 세 문서가 "자동 검증(ctest)"과 "환경이 없으면 Skipped"를 구분해 적는다. CI 주석도 갱신돼 **"이 잡의 green은 GPU 경로가 검증됐다는 뜻이 아니다 — 그 항목이 Passed로 셌을 때만 그렇다"**를 명시한다. 감사가 덤으로 지목한 `scripts/check_live_mega.ps1`(수동 .NET 스크립트)은 여전히 ctest·CI 밖이다.
>
> **[미해소 · 2026-08-13]** 항목 7과 같다. `add_test`도 CI 잡도 없다.

<sub>관점: 빌드·테스트 실제 실행과 검증력 판정</sub>

**근거**

```text
README.md:27-29  "검증 환경은 MSVC 19.51 Release다. 실제 로컬 UDP 왕복, 독립 호스트·클라이언트\n프로세스 왕복, 전용 서버와 두 클라이언트의 토큰 입장, 손실·지연·재정렬 복구,\n반복 매치 soak, Vulkan 스왑체인 5프레임 present를 자동 검증한다."
docs/architecture.md 검증 경계 목록: "- GPU: 실제 Vulkan 장치·스왑체인 5프레임 present"
docs/development.md §13 표: "| GPU | 실제 Vulkan swapchain present |"

저장소 전체에서 'gpu-selftest'가 등장하는 파일은 셋뿐이다:
  projects/mega/docs/development.md
  projects/mega/games/defense/main.cpp
  projects/mega/README.md
CMakeLists.txt의 add_test는 단 하나다: CMakeLists.txt:59  add_test(NAME mega_defense_selftest COMMAND mega_defense --selftest)
.github/workflows/ci.yml은 configure/build/ctest만 실행하며 스스로 이렇게 적어 두었다: "CI는 창을 열지 않고 mega_defense --selftest와 엔진·게임 테스트를 실행한다."

실제 ctest 실행 결과에도 GPU 관련 항목은 없다(51개 전부 test_* + mega_defense_selftest).
```

--gpu-selftest는 main.cpp:624-638에 구현되어 있고 창 생성(main.cpp:559)과 Vulkan 초기화가 필요한 수동 명령이다. CI 주석은 이 사실을 정확히 알고 있으나, README:27-29는 GPU present를 ctest로 자동 검증되는 다른 다섯 항목과 같은 문장에 나열한다. 같은 성격의 미연결 검증이 하나 더 있다: README.md:44가 안내하는 scripts/check_live_mega.ps1(VeLog 루트에 실존)은 live_service_lab의 .NET 빌드를 요구하는 별도 수동 스크립트이며 ctest에도 CI에도 없다.

**방향**: README와 docs에서 자동 검증(ctest) 항목과 수동 재현 항목을 분리 표기한다. PROJECTS_PLAN.md:141이 요구하는 "구현된 기능, 시뮬레이션으로만 검증한 기능, 교체 지점만 있는 기능을 구분"에 "수동으로만 검증한 기능"을 하나 더 두면 된다.

---

## 25. [중간] PROJECTS_PLAN 검증 단계표의 sanitizer와 고정 seed replay·checksum이 mega에 없다

> **[닫힘 · 2026-08-23]** 둘 다 생겼다. `MEGA_SANITIZE=address` 로 구성하는 ASan 빌드가 `CMakeLists.txt` 와 CI(`asan-windows`)에 있고, 제품 실행 파일이 `--record` / `--verify-replay` 를 갖는다. 후자는 `mp::GoldenChain` 으로 매 틱의 (월드 해시 · 아바타 체크섬)을 접어 **다른 프로세스**가 같은 값을 재생하는지 본다 - 솔로·호스트·아바타 세 팔의 기록·재생 짝 여섯이 ctest 에 있다.
>
> **[미해소 · 2026-08-13]** sanitizer 관련 문자열이 CMakeLists·`.github/`·docs 전체에 0건이고, `GameWorld`를 대상으로 한 고정 seed replay·checksum 테스트도 없다.

<sub>관점: 빌드·테스트 실제 실행과 검증력 판정</sub>

**근거**

```text
PROJECTS_PLAN.md:134  "| 코어 | 단위 테스트, property test, sanitizer |"
PROJECTS_PLAN.md:135  "| 시뮬레이션 | 고정 seed replay, checksum, 장기 soak |"

sanitizer: projects/mega의 CMakeLists.txt, .github/, docs/, README.md 전체에 대해 'sanitiz|fsanitize|asan|ubsan' grep 결과 0건.

checksum/replay: tests/ 전체에서 Checksum()을 쓰는 것은 test_mp_lockstep.cpp와 test_mp_rollback.cpp뿐이고, 둘 다 include/mp/det_sim.hpp를 대상으로 한다 — 도달성 분석상 제품 미사용 모듈이다. GameWorld를 쓰는 6개 테스트(test_game, test_game_expansion, test_defense_protocol, test_defense_network, test_defense_process, test_defense_soak) 중 결정성/checksum을 검사하는 것은 하나도 없다(grep 'GameWorld' tests/*.cpp | grep -i 'checksum|determin|replay' → 0건).

GameWorld는 실제로 결정적이다: include/game/game.hpp:331 `rng_ = 0x1234567u;`가 Init마다 재시드한다.
```

결정성이라는 성질은 구현되어 있으나(고정 시드 재설정), 그것을 지키는 자동 검사가 없다. tests/test_defense_soak.cpp는 유한성·ID 유일성·상한만 보며 "같은 입력 → 같은 상태"는 검사하지 않는다. 반면 그 검사를 갖춘 mp::DetSim은 게임이 쓰지 않는다. 다시 finding 1과 같은 패턴이다.

**방향**: 동일 시드·동일 명령열로 GameWorld를 두 번 돌려 상태 해시가 일치하는지 보는 테스트를 추가하면 mp/det_sim의 checksum 개념을 제품 시뮬레이션에 실제로 적용하게 된다. sanitizer는 최소한 CI에 /fsanitize=address 잡 하나(또는 Linux clang ASan/UBSan 잡)를 추가하면 계획표와 실제가 맞는다.

---

## 26. [낮음] 저장소에 변경 이력이 없어 PROJECTS_PLAN이 요구한 "개발 순서 설명" 수단이 성립하지 않는다

> **[닫힘 · 2026-08-14]** 항목 14와 같다. 저장소에 커밋 `870198c`가 존재하므로 "변경 이력이 없다"는 상태 자체는 사라졌다. **감사가 이 항목에서 물은 것(어느 축이 언제 붙었다 떨어졌는지)은 초기 커밋 하나로는 아직 답이 되지 않는다** — 그 답은 이후 커밋이 쌓여야 생긴다.
>
> **[미해소 · 2026-08-13]** 항목 14와 같다.

<sub>관점: 이론 시리즈 ↔ mega 통합 매핑 감사</sub>

**근거**

```text
`C:/Users/REIN/Documents/VeLog/.git` 디렉터리는 존재하지만 내용이 비어 있다(`ls -la .git` → `.`와 `..`만, `du -sh` → 0). `git log`·`git ls-files`가 모두 `fatal: not a git repository`로 실패한다.

`PROJECTS_PLAN.md:12-13` "개발 순서는 문서와 변경 이력으로 설명한다. 단계별 소스 복사본은 코드 드리프트와 검증 중복을 만들므로 보관하지 않으며, `projects/mega/`의 최종 코드만 권위 있는 원본으로 둔다."

`projects/mega/.github/workflows/ci.yml`이 존재하지만 저장소가 git 이력을 갖지 않으므로 실행된 적이 없다.
```

PROJECTS_PLAN은 "단계별 소스 스냅숏을 만들지 않는 대신 변경 이력으로 설명한다"는 교환 조건 위에 서 있다. 스냅숏은 실제로 제거됐지만(games/에 defense만 존재) 교환의 반대급부인 변경 이력이 존재하지 않아, 개발 순서를 설명하는 수단이 `docs/development.md` 한 편만 남았다. 이 감사 관점(축별 통합 여부)에서 직접적인 영향은 "어느 축이 언제 붙었다 떨어졌는지 확인할 방법이 없다"는 것이며, 실제로 이번 감사에서 games/roguelike·games/breakout 같은 과거 산출물이 언제 어떤 이유로 사라졌는지 코드로는 확인할 수 없었다.

**방향**: 저장소를 실제 git 이력이 있는 상태로 되돌리거나(초기 커밋이라도), PROJECTS_PLAN.md:12에서 "변경 이력"을 근거로 드는 문장을 실제로 유지 가능한 수단으로 바꾼다.

---

## 27. [낮음] README 코드 구조 트리에 실제 include 디렉터리 6개가 빠져 있고, 같은 README가 그중 하나(serialize)를 제품 경로로 열거한다

> **[닫힘 · 2026-08-14]** 트리에 빠져 있던 다섯(`containers/`·`sync/`·`sched/`·`profiler/`·`serialize/`)이 들어갔다. 더 중요한 것은 **손으로 맞추지 않게 만든 것**이다 — 감사의 방향 두 가지(누락분을 한 줄로 추가 / 완전성 주장을 거둠) 중 어느 쪽도 다음 디렉터리가 생기는 순간 다시 어긋나기 때문이다. `scripts/reachability.py`에 `--readme-tree`를 추가해 README 트리의 `include/` 하위 이름 집합과 실제 디렉터리 집합을 **양방향으로** 대조하고(실재하는데 트리에 없어도, 트리에 있는데 실재하지 않아도 위반), 빌드와 `reachability_gate` 테스트에서 실패시킨다. 트리를 통째로 지워 통과하는 경로도 막았다 — `include/` 줄을 못 찾으면 그 자체가 위반이다. README에도 "이 트리는 손으로 관리하지 않는다"를 적었다.
>
> **[미해소 · 2026-08-13]** README 코드 구조 트리는 다시 쓰였지만 `containers/`·`sync/`·`sched/`·`profiler/`·`serialize/`가 여전히 빠져 있다. `serialize/`는 제품 도달 헤더다.

<sub>관점: README·docs 주장 대 코드 실재 대조</sub>

**근거**

```text
README.md:132-146 트리는 include 하위로 game/, net/, render/, db/, live/, platform/, math/, physics/, ai/, memory/, ecs/, jobs/, engine/, tools/, mp/ 15개만 적는다. 실제 `include/` 하위 디렉터리는 21개로 containers/, io/, sync/, sched/, profiler/, serialize/ 가 빠졌다. 그런데 README.md:148-149 는 "제품 경로가 직접 호출하는 계층은 수학, 게임 규칙, 물리 충돌, 렌더, 엔진 입력·파티클, 제한 스크립트, 실제 UDP, 직렬화, WAL DB, 운영 지표, VFS, OS 창·오디오다." 라며 '직렬화'(include/serialize/archive.h)를 제품 경로로 명시한다. 폐포 분석에서도 serialize/archive.h 는 db/records.hpp 경유로 제품에 도달한다.
```

REVIEW_LOG.md:1085 가 과거 패스에서 "README 코드-문서 불일치 다수(가공 디렉토리 트리 → 실제 구조)"를 고쳤다고 기록한 것과 같은 종류의 불일치가 mega README에 남아 있다. 기능 영향은 없으나, 트리를 보고 소스를 찾는 독자가 제품 경로 헤더(serialize)를 찾지 못한다.

**방향**: 트리에 누락 6개 디렉터리를 한 줄로 추가하거나, 트리를 "대표 디렉터리"로 명시해 완전성 주장을 거둔다.

---

## 28. [낮음] include/platform/console.hpp 는 제품·테스트 어디서도 include 되지 않아 컴파일조차 되지 않는다

> **[P0로 닫힘 · 2026-08-13]** `mega_core`가 `include/` 헤더 하나당 번역 단위를 생성하므로 `platform/console.hpp`도 이제 컴파일된다(`build/mega_core_units/unit_platform_console_hpp.cpp`). 제품 미도달 상태는 `LAB_ONLY.md:92`에 P8 도입으로 등재됐다 — 감사가 지목한 "빌드 그래프 밖의 사문"은 아니게 됐다.

<sub>관점: README·docs 주장 대 코드 실재 대조</sub>

**근거**

```text
전체 테스트 50개 + 제품 2개 기점 헤더 폐포에서 미도달 헤더는 정확히 1개: `platform/console.hpp`. 저장소 grep 결과 `console.hpp` 문자열은 자기 자신 외에 어디에도 없다(tests/test_platform_caps.cpp:37 의 `profiles::Console()` 은 include/platform/capability.hpp:53 의 다른 심볼이다). 파일 내용은 :15-18 `extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int wCodePageID);` / `inline void EnableUtf8Console() { SetConsoleOutputCP(65001u); }` 뿐이다.
```

README.md:150-152 는 제품이 호출하지 않는 라이브러리들도 "51개 테스트에 포함된다"고 말하지만 이 파일은 그 어느 쪽에도 포함되지 않는다. 즉 빌드 그래프 밖의 사문(死文)이며, PROJECTS_PLAN.md:10 의 "중복 없는 최종 통합 소스" 기준에서 잔재로 남은 파일이다. 참고로 REVIEW_LOG.md:1049 는 과거 stage12 README에 "실존 console.hpp 누락 → 추가"를 기록하고 있어, stage 구조 제거 과정에서 소비자만 사라지고 파일이 남은 것으로 보인다.

**방향**: 삭제하거나, UTF-8 콘솔 설정이 실제로 필요한 제품 진입점(games/defense/server_main.cpp 의 콘솔 출력)에서 호출해 빌드 그래프 안으로 되돌린다.

---

## 29. [낮음] 난이도 곡선 생성기 GenerateWave/GenerateWaves 는 테스트 전용이고 제품은 고정 5웨이브만 쓴다

> **[닫힘 · 2026-08-14]** 감사가 제시한 두 방향 중 **후자를 택했고 그 이유를 소스에 적었다**. `GenerateWaves`를 제품에 연결하지 **않는 것이 설계**다 — 제품의 웨이브는 콘텐츠(`assets/waves.txt`, 실패 시 `defense::DefaultWaves()`로 파일 전체 폴백)이고, 난이도 곡선을 코드가 소유하면 "재빌드 없이 밸런스를 바꾼다"는 계약이 깨진다. `game.hpp`의 함수 위에 그 판단과 **실제 소비자**를 명시했다: `test_defense_soak.cpp`가 회차마다 다른 길이의 웨이브 열로 반복 매치의 유한성·ID 유일성을 재고, `test_game_expansion.cpp`가 곡선 자체(단조 증가·보스 주기·종류 로테이션)를 단언한다. 죽은 코드가 아니라 시험 장치다. 감사가 같은 항목에서 지적한 다른 절반 — `MakeWaves()`/`DefaultWaves()`의 5웨이브 리터럴이 두 파일에 복제돼 하나만 고치면 클라이언트와 전용 서버의 기본 밸런스가 갈라지던 것 — 은 P0에서 해소됐다(`defense_content.hpp`의 `DefaultWaves()` 하나를 `defense_app.hpp`·`defense_server.hpp`가 함께 부른다).
>
> **[미해소 · 2026-08-13]** `GenerateWaves`를 참조하는 곳은 여전히 `tests/`뿐이다.

<sub>관점: README·docs 주장 대 코드 실재 대조</sub>

**근거**

```text
include/game/game.hpp:277-278 주석 "엔드리스/난이도 스케일링: 웨이브 인덱스가 오를수록 어려워지는 웨이브를 생성한다.", :279 `inline WaveDef GenerateWave(int index)`, :304 `inline std::vector<WaveDef> GenerateWaves(int n)`. 저장소 전체 참조는 tests/test_defense_soak.cpp:61 `world.Init(map, GenerateWaves(10 + run % 6), 1800, 300);` 와 tests/test_game_expansion.cpp:170-198 뿐이다. 제품은 games/defense/main.cpp:72-80 `MakeWaves()`(5줄 하드코딩)와 games/defense/server_main.cpp:50-58 `DefaultWaves()`(동일 내용을 두 번째로 하드코딩), 그리고 assets/waves.txt(5줄)만 사용한다.
```

PROJECTS_PLAN.md:63 "적과 웨이브가 데이터로 정의되고 난이도 곡선이 있다"는 5웨이브 내부 곡선으로 최소 충족되지만, 코드에 있는 진짜 난이도 곡선 로직은 게임에 연결돼 있지 않다. 또한 `MakeWaves()`/`DefaultWaves()` 는 같은 5개 웨이브 리터럴이 두 파일에 복제된 상태로, 하나만 고치면 클라이언트와 전용 서버의 기본 밸런스가 갈라진다.

**방향**: 기본 웨이브 세트를 공용 헤더(games/defense/content.hpp)로 올려 중복을 없애고, GenerateWaves 를 제품에 연결하거나 테스트 전용 유틸임을 game.hpp 주석에 명시한다.

---

## 30. [낮음] "운영 지표"는 클라이언트 실행 파일에만 있고, 수집한 스냅샷 크기 히스토그램은 출력되지 않는다

> **[닫힘 · 2026-08-14]** 종료 출력에 `snapshot_bytes_p95` · `snapshot_bytes_max` · `snapshots_truncated`가 추가됐다. p95와 최댓값을 함께 찍는 이유는 둘이 다른 질문에 답하기 때문이다 — p95는 평소 대역폭, 최댓값은 1200바이트 예산까지의 여유. 히스토그램 이름·버킷 설정도 `FrameHistogram()`/`SnapshotBytesHistogram()` 접근자 한 곳으로 모아, 기록하는 곳과 읽는 곳에 같은 리터럴을 두 번 써서 조용히 다른 히스토그램이 생기는 경로를 없앴다. `architecture.md`·`development.md`는 **"수집만 하고 출력하지 않는 지표를 두지 않는다"를 이 레지스트리의 불변식으로 적고**, 지표가 `mega_defense`의 것이며 전용 서버는 자기 루프 통계를 직접 출력한다는 사실(감사가 지적한 나머지 절반)도 명시한다.
>
> **[미해소 · 2026-08-13]** `snapshot_bytes` 히스토그램은 `defense_app.hpp:469`에서 기록되지만 종료 출력에는 `frame_p95` 등만 나온다.

<sub>관점: README·docs 주장 대 코드 실재 대조</sub>

**근거**

```text
README.md:25 "- 운영 지표: 프레임 시간, 원격 명령 수, 스냅샷 수·크기 집계" / docs/architecture.md:131-132 "운영 레지스트리는 프레임 시간 히스토그램, 원격 명령 카운터, 전송 스냅샷 카운터·크기를 집계한다. 현재는 종료 시 로컬 콘솔에 출력하며…". 구현은 games/defense/main.cpp:622 `live::Registry metrics;` 에만 있다. games/defense/server_main.cpp 에는 live/analytics.hpp include 자체가 없고 :271-275 에서 tick/players/commands/rejected 만 printf 한다. 또 main.cpp:761-762 가 `metrics.GetHistogram("snapshot_bytes", wire::kMaxPacketBytes, 24).Record(...)` 로 크기를 기록하지만, 종료 시 출력(main.cpp:809-813)은 matches/remote_commands/snapshots/frame_p95 네 값만 찍고 snapshot_bytes 는 어디에도 나오지 않는다.
```

"집계한다"는 문자 그대로는 참이나, 두 실행 파일 중 하나에만 존재한다는 점과 수집된 크기 지표가 관측 불가능하다는 점이 문서에 드러나지 않는다. architecture.md:132 는 "현재는 종료 시 로컬 콘솔에 출력하며"라고 적어 크기도 출력되는 것처럼 읽힌다.

**방향**: 종료 printf에 snapshot_bytes 퍼센타일을 추가하거나, 문서에서 "크기는 집계만 하고 현재 출력하지 않는다 / 지표는 클라이언트 프로세스에만 있다"로 범위를 좁혀 적는다.

---

## 31. [낮음] 1200바이트 상한과 truncated 플래그는 현재 콘텐츠 상한상 도달 불가한 죽은 코드이며 아무도 읽지 않는다

> **[닫힘 · 2026-08-14]** 감사가 제시한 두 방향을 **둘 다** 했다 — 서로 다른 두 사실에 대한 답이기 때문이다.
>
> - **1200바이트 상한은 "구성상 도달 불가"로 다시 적었다.** 감사의 산술이 옳았으므로 그것을 `static_assert(kSnapshotMaxBytes <= kMaxPacketBytes)`로 컴파일 시간에 못박고, `WriteSnapshot` 끝의 크기 비교에는 "이 분기의 else는 구성상 도달할 수 없다 — 변이 테스트가 이 줄을 지워도 아무 테스트도 실패하지 않은 것은 테스트의 구멍이 아니라 도달 불가라는 사실의 반영"이라는 주석을 달았다. `development.md`도 "검사로 보장"이 아니라 "예산은 `static_assert`, 뒤의 비교는 마지막 자물쇠"로 고쳤다.
> - **`truncated`는 반대로 소비자를 만들었다.** 이건 도달 불가가 아니라 상한 관계가 깨지는 순간 켜지는 신호이고, 감사가 지적한 대로 그때 조용히 엔티티가 사라진다. 출구를 **양쪽에** 냈다 — 보내는 쪽은 `WriteSnapshot(..., bool* truncatedOut)`(호스트가 자기 손실을 셀 수 있어야 한다. 와이어 flags만으로는 클라이언트가 붙어 있을 때만 관측된다), 받는 쪽은 `PumpClient`가 디코드한 flags를 읽어 둘 다 `snapshots_truncated` 카운터로 들어가고 항목 30의 종료 출력에 나온다. `TestSnapshotTruncationSignalsLoss`가 웨이브 하나에 적 100마리를 넣어 켜지는 경로를 실제로 자극하고, **상한 이하 월드에서는 양쪽 다 꺼져 있는지도 함께 확인한다** — 항상 켜지는 플래그는 켜지지 않는 플래그와 똑같이 아무 말도 하지 않기 때문이다.
>
> **[미해소 · 2026-08-13]** `truncated`를 읽는 테스트가 지금도 0건이다.

<sub>관점: 빌드·테스트 실제 실행과 검증력 판정</sub>

**근거**

```text
변이 결과: M3 1200-byte wire cap removed -> *** SURVIVED (no test failed) ***
대상: include/game/defense_protocol.hpp:280
    return bytes.size() <= kMaxPacketBytes ? bytes : net::Bytes{};

최대 크기 계산: 헤더8 + 고정필드22 + 타워 16×10 + 적 64×13 + 발사체 20×8 = 1182 바이트 < kMaxPacketBytes(1200). 즉 상한에 도달할 수 없다.

적 수가 64를 넘을 수 없음도 확인: 웨이브는 include/game/game.hpp:483 `if (spawned_ >= w.count && AliveEnemies() == 0)` 조건으로만 진행하므로 동시 생존은 한 웨이브분이고, 외부 웨이브 파일은 games/defense/content.hpp:143 `wave.count < 1 || wave.count > 64`로 제한된다.

truncated 플래그는 생산만 되고 소비되지 않는다. 저장소 전체 grep 결과 등장 위치는 4곳뿐이며 전부 defense_protocol.hpp 내부다(:75 선언, :236 계산, :249 인코딩, :297 디코딩). tests/, games/ 어디에서도 읽지 않는다.

docs/development.md §13 표: "| 프로토콜 | 명령·스냅샷 왕복, 손상·절단 거부, 1200바이트 상한 |"
```

"제한 크기 스냅샷"과 "1200바이트 상한"은 구성상 참이지만 테스트로 확인된 것이 아니라 산술적으로 도달 불가해서 참이다. tests/test_defense_soak.cpp:47 CHECK(packet.size() <= wire::kMaxPacketBytes)도 결코 실패할 수 없다. 초과 상황을 알리도록 설계된 truncated 플래그를 제품도 테스트도 읽지 않으므로, 만약 상한 관계가 나중에 깨지면(예: kMaxEnemies 상향) 조용히 엔티티가 사라질 뿐 아무 신호도 나오지 않는다. finding 5의 중복 상수 문제와 같은 뿌리다.

**방향**: 엔티티 캡을 넘는 월드를 인위적으로 만들어 truncated=true가 켜지고 패킷이 상한 이하로 유지되는지 확인하는 테스트를 하나 두거나, 반대로 도달 불가함이 의도라면 문서에서 "검사로 보장"이 아니라 "구성상 도달 불가"로 서술을 바꾼다.

---

## 32. [낮음] 문서 간 테스트 개수 불일치(49 vs 51)

> **[P0로 닫힘 · 2026-08-13]** 항목 19와 같다.

<sub>관점: 빌드·테스트 실제 실행과 검증력 판정</sub>

**근거**

```text
docs/architecture.md 검증 경계 마지막 줄: "- 전체 코드베이스: CTest 49개"
docs/development.md §13: "CTest는 51개 대상을 실행한다."
README.md:151-152: "... 같은 코드베이스의 독립 라이브러리로 51개\n테스트에 포함되지만 ..."
실측: ctest 출력 "100% tests passed out of 51" (tests/test_*.cpp 50개 + mega_defense_selftest 1개)
```

architecture.md만 49로 남아 있다. 기능적 영향은 없지만, 같은 저장소의 세 문서가 같은 수치를 다르게 적고 있다는 점에서 문서 검증이 자동화되어 있지 않음을 보여준다.

**방향**: architecture.md의 49를 51로 고치거나, 더 낫게는 이런 수치를 문서에서 빼고 ctest 출력에 맡긴다.

---

## 관점별 총평

### 이론 시리즈 ↔ mega 통합 매핑 감사

mega는 "빌드·테스트가 다 녹색인데 뭔가 이상한" 상태인데, 그 정체는 품질이 아니라 **구조**다. `include/`의 89개 헤더 중 64개(5,681/9,310줄, 61%)가 실제 게임(`games/defense/`)에서 전이적으로 도달조차 하지 않는다 — ai 9개 전부, ecs/jobs/memory/containers/mp/sync/io/profiler/sched 전부, physics 6개 중 3개, math 12개 중 9개, engine 7개 중 5개가 게임과 무관하게 서 있다. 그래서 저장소는 "여러 축을 하나의 게임으로 통합한 것"이 아니라 **작은 게임 + 그 게임이 부르지 않는 라이브러리 컬렉션**이고, 후자는 주제가 `projects/mini/`의 랩과 그대로 겹친다(ai_lab↔include/ai, math_lab↔include/math, physics_lab↔include/physics). 즉 mega의 절반 이상이 mini의 두 번째 구현이며, 이는 PROJECTS_PLAN:10의 "중복 없는 최종 통합 소스"와 정면으로 어긋난다. 더 큰 문제는 저장소 안에 **서로 반대되는 두 선언**이 있다는 점이다: PROJECTS_PLAN:3은 "여러 축을 실제 게임으로 통합"이라 하고, mega/docs/development.md:9는 "모듈 이름을 한 실행 파일에 모두 넣는 것이 아니다"라며 미호출을 정당화한다 — 저자가 느끼는 위화감은 이 두 문장 사이의 균열이다. 축별로 보면 graphics(40편·21,286줄)가 16줄짜리 blit 셰이더 한 쌍으로, languages(7편)가 상수 5줄짜리 `balance.script`로 축소된 것이 가장 극단적이다.

### README·docs 주장 대 코드 실재 대조

mega는 "죽어 있는" 프로젝트가 아니다. 실제로 빌드되고 `ctest` 51/51이 통과하며(`100% tests passed out of 51`), `mega_defense --selftest`(CLEAR, 1374프레임), `--gpu-selftest`(Vulkan 5프레임 present OK), 외부 에셋 10/10 로드, 외부 level/waves/balance 채택까지 이 머신에서 직접 재현된다. README가 나열한 17개 기능 중 "없음"으로 판정되는 것은 하나도 없다. 이상한 곳은 다른 데 있다. 첫째, 문서가 "CTest가 검증한다"고 적어 놓은 항목 중 일부(Vulkan 스왑체인 present)는 CTest에도 CI에도 등록돼 있지 않고, CTest 개수마저 문서마다 49와 51로 갈린다. 둘째, 제품이 실제로 링크하는 코드와 테스트가 검증하는 코드가 갈라져 있다 — `include/` 89개 헤더 중 25개(9310줄 중 3629줄)만 게임에 도달하고, 신뢰 채널·스냅샷 보간처럼 README가 자랑하는 기능은 제품 안에서 손으로 다시 구현되어 있으며 그 구현(`ReplicaBuffer`)은 main.cpp 익명 네임스페이스에 갇혀 테스트가 0건이다. PROJECTS_PLAN이 못박은 "중복 없는 최종 통합 소스"가 stage 디렉터리 대신 헤더 계층에서 다시 깨진 셈이다. 셋째, 교체 가능하다고 광고한 `balance.script` 하나로 두 실행 파일 모두 시작 시 무한정 멈추는 것을 실증했다("전체 폴백"이 실행 시간에는 적용되지 않는다). 저자가 느낀 "이상함"의 정체는 기능 부재가 아니라, 문서가 서술하는 검증 경계와 실제 검증 경계가 어긋나 있다는 점으로 보인다. 참고로 `games/`에 defense만 남고 roguelike·breakout·stage01~13이 사라진 것은 드리프트가 아니라 REVIEW_STATE.md:69-70이 명시적으로 선언한 의도된 제거이며, 저장소 어디에도 사라진 것을 가리키는 살아 있는 참조는 없었다(REVIEW_LOG.md의 stage/games 언급은 과거 이력 로그다).

### 빌드·테스트 실제 실행과 검증력 판정

빌드와 테스트는 주장대로 깨끗하다. `cmake -S projects/mega -B build_audit` → `cmake --build ... --config Release`는 exit 0, 빌드 로그 전체에 warning 0건(MSVC 19.51.36252.0, Vulkan presenter enabled), `ctest -C Release`는 "100% tests passed out of 51" / "Total Test time (real) = 10.86 sec"였다. 즉 "빌드가 안 된다"류의 문제는 없다. 저자가 느낀 "이상함"의 정체는 **테스트가 통과한다는 사실과 테스트가 무엇을 보장하는지 사이의 괴리**다. 원본을 건드리지 않은 별도 복사본에서 변이 테스트(mutation testing) 12건을 돌린 결과, 통제군(M0 `SequenceMoreRecent` 반전)은 3개 테스트가 정확히 잡았지만, 실질적 결함 변이 9건 중 6건이 51개 테스트를 전부 초록으로 통과했다. 특히 **플레이 가능한 클라이언트 `mega_defense`의 네트워크 루프 세 곳(호스트 중복 억제, 클라이언트 스냅샷 순서 보장, 명령 재전송)을 통째로 무력화해도 51/51 통과**한다. 구조적 원인은 명확하다: `include/` 89개 파일 중 실제 실행 파일에서 도달 가능한 것은 25개뿐이고, 잘 만들어진 네트워크 테스트(`test_net_reliable`, `test_net_snapshot`, `test_net_session`)가 검증하는 `net/reliable_channel.hpp`·`net/snapshot.hpp`·`net/session.hpp`는 제품이 링크하지 않는다. 제품은 같은 기능을 `games/defense/main.cpp` 안에 익명 네임스페이스로 재구현해 두었고 그쪽엔 테스트 진입점이 없다. PROJECTS_PLAN이 mega에 요구한 "중복 없는 최종 통합 소스"가 바로 여기서 깨진다. 게임 규칙·경제·콘텐츠 폴백·HMAC 토큰 보안은 반대로 실패 주입이 제대로 있고 변이를 잡아낸다.


---

## 반영 상태 (2026-08-13)

> **이 절의 집계는 2026-08-13 시점의 것이고, 그대로 두었다.** 하루 뒤 두 번째 반영이 있었고
> 그 결과는 [반영 상태 (2026-08-14)](#반영-상태-2026-08-14)에 있다. 현재 집계는 그쪽이다
> (닫힘 **24** / 부분 **3** / 미해소 **5**).

감사는 **2026-08**, 반영은 **2026-08-13**이다. 이 절은 위 32건이 그 뒤 어떻게 됐는지를 기록한다.
반영의 실체는 재설계안([REDESIGN_MEGA_2026-08.md](REDESIGN_MEGA_2026-08.md))의 **P0 · 구조 게이트**
하나이며, **P1~P9는 착수되지 않았다.** 감사 32건 중 절반 이상이 미해소로 남은 이유가 이것이다 —
그 항목들은 "게임이 그 축을 부를 이유가 없다"는 규모 문제라 게임이 커지는 P1~P8에서 닫힌다.

### 판정 근거

주장이 아니라 현재 파일로 판정했다. 확인한 것은 다음과 같다.

- `CMakeLists.txt` — `mega_core` 타깃 정의, `add_test` 등록 목록, 헤더당 번역 단위 생성
- `scripts/reachability.py` · `scripts/check_layers.py` — 게이트 본체의 실재
- `docs/LAB_ONLY.md` · `docs/AXIS_OWNERSHIP.md` · `docs/PRODUCT_LAYERS.md` — 신설 여부와 등재 내용
- `games/defense/main.cpp`(29줄) · `server_main.cpp`(18줄) · `include/game/defense_app.hpp` — 재구현 제거 여부
- `tests/` 디렉터리 목록과 `test_defense_coop_process.cpp` · `test_defense_client_loop.cpp` 본문
- 미해소 판정 항목은 감사가 지목한 **바로 그 줄**을 다시 열어 무변경임을 확인

확인하지 못한 것은 명시한다 — **빌드·ctest를 재실행하지는 않았다.** 아래 "55개" 는 CMake의
등록 규칙(`tests/test_*.cpp` 52개 + 명시 등록 3개)에서 센 값이고 `ctest -N` 출력이 아니다.

### 집계

| 판정 | 건수 | 뜻 |
|---|---:|---|
| **P0로 닫힘** | **11** | 감사가 지적한 상태가 현재 파일에서 사라졌다 |
| **부분** | **4** | 일부만 해소. 남은 부분을 아래에 적었다 |
| **미해소** | **17** | 감사 시점 그대로. 대부분 P1~P9 대기 |
| 계 | **32** | 치명 2 · 높음 12 · 중간 11 · 낮음 7 |

| 판정 | 항목 번호 |
|---|---|
| P0로 닫힘 | 1 · 9 · 10 · 11 · 12 · 13 · 18 · 19 · 20 · 28 · 32 |
| 부분 | 3 · 4 · 5 · 17 |
| 미해소 | 2 · 6 · 7 · 8 · 14 · 15 · 16 · 21 · 22 · 23 · 24 · 25 · 26 · 27 · 29 · 30 · 31 |

치명 2건 중 **1번은 닫혔고 2번(graphics)은 P1·P3 대기**다.

### P0가 실제로 한 일

| P0 항목 | 결과 | 닫은 감사 항목 |
|---|---|---|
| `include/` → 정적 라이브러리 타깃 | `mega_core`. 제품과 테스트가 같은 타깃을 링크한다. 헤더 하나당 번역 단위를 생성하므로 아무도 include 하지 않던 헤더도 컴파일된다 | 11 · 28 |
| 도달성 게이트 | `scripts/reachability.py` + ctest `reachability_gate`. 헤더 99개 중 제품 도달 **41** / `LAB_ONLY.md` 등재 **58**. 근거 없는 미도달 **64 → 0**, 제3의 상태는 빌드 실패 | 1 · 20 |
| 계층 규칙 게이트 | `scripts/check_layers.py` + ctest `layer_rules`. `games/`는 OS API 직접 호출 금지, 진입점 150줄 상한, `games/` 아래 헤더 금지 | 10(부수) |
| 제품의 net 재구현 제거 | `defense_app.hpp`가 `net/reliable_channel.hpp`·`net/session.hpp`를 include. `ReplicaBuffer` 사본 소멸. `main.cpp` 29줄 / `server_main.cpp` 18줄 | 5(부분) · 9 · 10 |
| 제품 루프의 테스트 진입점 | `tests/test_defense_client_loop.cpp` 신설 — 실제 UDP 소켓 위에서 `DefenseApp::Step()`을 돌린다. `--selftest`도 조기 반환을 그만두고 제품 루프를 N틱 구동한다 | 10 · 12 |
| 프로세스 테스트를 제품 바이너리로 | `test_defense_coop_process.cpp`가 `mega_defense.exe --host` / `--join`을 스폰. 테스트 안의 `RunHost`/`RunClient` 스텁 제거 | 13 · 18 |
| 문서 모순 해소 | `docs/AXIS_OWNERSHIP.md`(13축 소유권) · `docs/LAB_ONLY.md`(미도달 근거) · `docs/PRODUCT_LAYERS.md`(빌드가 생성) 신설. `architecture.md`에서 하드코딩 테스트 개수 삭제, `development.md`는 55 | 17(부분) · 19 · 32 |
| A/B 스위치·벤치·골든 해시 인프라 | **미착수** | — |
| git 실사용 개시 | **미착수** — `git log`가 지금도 커밋 없음으로 실패 | 14 · 26 미해소 |

테스트 수는 **51 → 55**다(`tests/test_*.cpp` 52개 + `mega_defense_selftest` + `layer_rules` +
`reachability_gate`). 감사가 "51개를 전부 통과했다"고 기록한 변이 3건 —
호스트 중복 억제 · 클라이언트 스냅샷 순서 · 명령 재전송 — 이 이제 전부 검출된다.

### 부분 해소 4건 — 무엇이 남았나

1. **3번 (mini↔mega 중복).** `AXIS_OWNERSHIP.md`가 축별 소유권과 도달 여부를 컴파일러 의존성
   출력 기준으로 기록해 "어느 쪽이 권위 있는 원본인가"에는 답이 생겼다. 그러나 **같은 주제의
   두 구현이 물리적으로 남아 있는 상태는 그대로다.** 재설계안은 이관(`## 이관`)으로 처리하려 한다.
2. **4번 (ai 축).** 헤더 9개 전부가 `LAB_ONLY.md`에 P2·P4 도입 단계와 사유로 등재됐다. 근거 없는
   미도달은 해소됐지만 **게임이 부르지 않는다는 사실은 그대로**이고, 감사가 지목한 레벨의
   무분기 단일 경로 제약(`game.hpp` `candidateCount != 1`)도 남아 있다.
3. **5번 (net 자기중복).** 신뢰 채널·세션·스냅샷은 제품 경로로 들어왔다. **남은 것은 `net/rtt.hpp`**로,
   `LAB_ONLY.md:85`가 P5 도입으로 등재하며 사유를 이렇게 적는다 — 현 와이어 프로토콜에 ping/pong
   왕복 표본이 없어 넣을 입력 자체가 없고, 링크만 시키는 것은 가짜 도달이므로 하지 않았다.
   즉 감사가 지적한 **"RTT 100ms 고정"의 실질적 손해는 아직 열려 있다.**
4. **17번 (문서 주장 대 ctest 실제).** 테스트 개수 불일치는 해소됐다. **Vulkan present를
   "자동 검증한다"고 적은 README 문장은 그대로**이고 `--gpu-selftest`를 등록하는 `add_test`도 없다
   (항목 7 · 24와 같은 문제).

### 미해소 17건의 성격

| 성격 | 항목 | 언제 닫히나 |
|---|---|---|
| 게임 규모가 커져야 닫힘 | 2(graphics) · 6(parts·algorithms·system) · 15(languages) · 16(tools·platform·engine) | 재설계안 P1~P9 |
| 문서·주장 정합 | 7 · 24(Vulkan 자동 검증) · 27(README 트리) · 30(`snapshot_bytes` 미출력) | 지금 가능 (문서 수정) |
| 검증력 공백 — 지금 닫을 수 있다 | 8(스크립트 실행 예산) · 21(WAL COMMIT) · 22(엔티티 상한 상수·항등식 테스트) · 23(`argument` 범위) · 25(sanitizer·결정성 replay) · 29(`GenerateWaves` 미연결) · 31(`truncated` 미소비) | 지금 가능 |
| 저장소 운영 | 14 · 26(git 이력 부재) | 지금 가능 (초기 커밋) |

**게임 규모와 무관한 순수 결함 넷**이 특히 값싸다 — 8 · 21 · 22 · 23. 각각 확인한 현재 상태는
해당 항목의 상태 줄에 적었다.

### 이 절이 하지 않은 것

- **본문 수정 없음.** 위 32건의 지적 내용·근거 코드 인용·방향 제안은 **한 글자도 지우지 않았다.**
  머리말 한 줄을 사실에 맞게 교체하고, 각 지적 아래에 상태 줄 한 줄씩(32개)을 넣고, 이 절을
  덧붙인 것이 전부다. 판정을 나중에 재검토하려면 원래 근거가 남아 있어야 하기 때문이다.
- **감사 시점의 변이 테스트를 다시 돌리지 않았다.** 변이 3건이 이제 검출된다는 것은 P0 작업의
  기록과 `test_defense_client_loop.cpp` 머리 주석에 근거하며, 이 절이 독립적으로 재현한 것은 아니다.
- **빌드·ctest 재실행 없음.** 위에 적었다.

---

## 반영 상태 (2026-08-14)

두 번째 반영이다. **P1~P9는 여전히 한 줄도 착수되지 않았고 게임은 그대로 디펜스다.**
이번에 닫은 것은 전부 **게임 규모와 무관한 항목** — 앞 절이 "지금 닫을 수 있다"로 분류해 둔
검증력 공백 넷(8 · 21 · 22 · 23), 문서·주장 정합 넷(7 · 24 · 27 · 30), 죽은 신호 하나(31),
설계 의도 명시 하나(29), 그리고 저장소 운영(14 · 26)이다.

### 판정 근거

주장이 아니라 **현재 파일과 백업 대비 diff**로 판정했다.

- 기준선 둘: `VeLog_backup_2026-08-11`(P0 이전)과 커밋 `870198c`(P0 직후, 2026-08-14 19:33).
  후자 대비 워킹 트리 diff가 이번 회차의 변경분을 정확히 분리해 준다 —
  `projects/mega/` 19파일 **+1,224 / −84**.
- 닫힘 판정 항목은 감사가 지목한 **바로 그 줄**을 열어 변경을 확인했고, 미해소 판정 항목도
  같은 방식으로 무변경을 확인했다(`sanitiz|fsanitize|asan|ubsan` 0건, intrinsic 0건,
  algorithms 키워드 0건, `net/rtt.hpp` 참조는 `tests/`뿐, `game.hpp:157 candidateCount != 1` 그대로).
- 테스트 수는 CMake 등록 규칙에서 셌다: `tests/test_*.cpp` **52** + `mega_defense_selftest` +
  `mega_defense_gpu_selftest` + `layer_rules` + `reachability_gate` = **56**.

이번에는 **빌드와 ctest를 실제로 돌렸다**(앞 절이 하지 못했다고 명시한 부분) — 클린 빌드 2회,
**ctest 56/56**, P0 종료 조건 유지(변이 3건 전부 검출), 도달성·계층 두 게이트 green.
도달성 수치는 P0에서 변하지 않았다: 헤더 99개 중 제품 도달 **41** / `LAB_ONLY.md` 등재 **58** /
근거 없는 미도달 **0**.

### 집계

| 판정 | 2026-08-13 | **2026-08-14** | 뜻 |
|---|---:|---:|---|
| **닫힘** | 11 | **24** | 감사가 지적한 상태가 현재 파일에서 사라졌다 |
| **부분** | 4 | **3** | 일부만 해소 |
| **미해소** | 17 | **5** | 감사 시점 그대로 |
| 계 | 32 | **32** | 치명 2 · 높음 12 · 중간 11 · 낮음 7 |

| 판정 | 항목 번호 |
|---|---|
| 닫힘 | **P0(08-13)**: 1 · 9 · 10 · 11 · 12 · 13 · 18 · 19 · 20 · 28 · 32<br>**이번(08-14)**: 7 · 8 · 14 · 21 · 22 · 23 · 24 · 26 · 27 · 29 · 30 · 31 |
| 부분 | 3 · 4 · 5 · 17 |
| 미해소 | 2 · 6 · 15 · 16 · 25 |

치명 2건 중 **1번은 닫혔고 2번(graphics)은 P1·P3 대기**다.

### 지적보다 컸던 것 둘

앞 절의 닫힘은 대부분 "지적한 대로 고쳤다"였다. 이번엔 둘이 지적의 범위를 넘었고,
둘 다 **테스트를 쓰는 과정에서** 드러났다.

**#8 — 정지 경로가 하나가 아니라 셋이었다.** 감사는 중첩 `while`에서 지역 `guard`가 곱해지는
것(2겹이면 10^12회)을 지목했다. 같은 파일 하나로 재현되는 정지 경로가 둘 더 있었다:
파서가 모르는 토큰을 소비하지 않고 돌아와 최상위 문 루프가 전진하지 않는 것(**`}` 한 글자짜리
스크립트**가 노드를 무한 할당한다)과, 재귀 하강에 깊이 제한이 없어 괄호를 겹치면 스택이
넘치는 것(폴백이 아니라 프로세스의 죽음). 앞의 것은 **파싱 단계**라 실행 예산을 걸어도
도달조차 하지 못한다 — 즉 감사가 제안한 수정만으로는 안 닫혔다. 옛 코드에서 실행으로
증명한 미반환 시간은 **30.77초**(중첩 while)와 **20.22초**(`}` 한 글자)다. 셋 다 `Run()` 한 번
전체에 걸리는 상한으로 바꾸고 진단 문자열(`<스크립트>:<줄>: …`)을 남기게 했으며,
두 실행 파일이 그 문자열을 stderr에 찍고 기본 구성으로 진행하도록 배선했다 —
**조용한 폴백이 아니게** 만든 것도 이번 변경이다.

**#23 — 15 failures 중 4건이 소비자 층이었다.** 감사는 "범위 검사에 테스트가 없다"를
지적했고 피해를 "세션의 모든 스냅샷이 버려진다"로 봤다. 테스트를 쓰자 수정 전 **15건**이
실패했다: 범위 밖 `argument` 7건이 전부 디코드에 성공, 거부된 패킷이 out-param을 오염 4건,
그리고 **소비자 층 4건**. 마지막이 진짜 피해다 — 호스트는 디코드에 성공한 명령을 곧바로
신뢰 채널에 `Accept` 하고 `Accept`는 그 자체로 상태 변경이므로(`nextExpected_`가 앞당겨진다),
**기대 시퀀스에 얹힌 위조 명령 하나가 그 자리를 태우면 정직한 클라이언트의 진짜 명령이
영원히 중복 취급돼 사라진다.** 플레이어에게는 "요청한 타워가 조용히 안 선다"로 보인다.
`ApplyCommand`에서 거절하는 것으로는 늦다는 뜻이고, 검사가 디코더에 있어야 하는 이유다.

덤으로 하나 더 나왔다. **#21**에서 WAL의 미커밋 꼬리를 파일에 남겨 두면 **재기동 뒤 첫
트랜잭션의 COMMIT 마커가 그 꼬리까지 커밋한다**(append-only + 마커가 pending 전부에 걸림).
감사가 "구현 자체는 정상"이라 판정한 것은 1차 재생까지만 본 것이라 맞았고, 그다음 한 걸음이
비어 있었다. `Open()`이 마지막 커밋 지점까지 로그를 잘라내도록 고쳤다.

### 부분 3건 — 무엇이 남았나

1. **3번 (mini↔mega 중복).** 08-13과 같다. 소유권은 `AXIS_OWNERSHIP.md`가 답했지만
   **같은 주제의 두 구현이 물리적으로 남아 있는 상태는 그대로**다. 재설계안의 `## 이관`이 처리한다.
2. **4번 (ai 축).** 08-13과 같다. 헤더 9개는 `LAB_ONLY.md`에 P2·P4로 등재됐고
   게임은 여전히 부르지 않는다. 레벨의 무분기 단일 경로 제약(`game.hpp:157 candidateCount != 1`)도 그대로다.
3. **17번 (문서 주장 대 ctest 실제).** 두 headline은 이번에 닫혔다 — 개수 불일치(→ 56)와
   Vulkan "자동 검증" 주장(→ `mega_defense_gpu_selftest` 등록 + Skipped 의미 명시).
   **잔여 1건**: `include/render/framebuffer.hpp:3`의 "이 머신엔 Vulkan SDK 가 없어
   GPU 백엔드를 빌드·검증할 수 없다"는 주석이 그대로여서, 같은 저장소가 README(있다)와
   소스 주석(없다)으로 반대 사실을 말한다. 항목 2도 같은 줄을 지목한다.

**5번(net 자기중복)은 08-13의 부분 판정에서 바뀌지 않았다** — `net/rtt.hpp`는 지금도
`tests/`만 include 하고, `LAB_ONLY.md:85`의 P5 이월 사유(현 와이어 프로토콜에 ping/pong
왕복 표본이 없어 넣을 입력이 없다)가 유효하다. 즉 감사가 지목한 "RTT 100ms 고정"의
실질적 손해는 열려 있다.

### 미해소 5건 — 언제 닫히나

| 성격 | 항목 | 언제 |
|---|---|---|
| 게임 규모가 커져야 닫힘 | 2(graphics) · 6(parts·algorithms·system) · 15(languages) · 16(tools·platform·engine) | 재설계안 P1~P9 |
| 검증력 공백 — 지금도 닫을 수 있다 | **25**(sanitizer · `GameWorld` 고정 seed replay·checksum) | 지금 가능 |

**이제 P1~P9를 기다리지 않는 미해소는 25번 하나다.** 앞 절에서 "지금 가능"으로 분류한
나머지는 전부 닫혔다. 25번의 두 절반은 성격이 다르다 — sanitizer는 CI 잡 하나면 되고,
결정성 replay는 `mp::DetSim`의 checksum 개념을 제품 시뮬레이션(`GameWorld`)에 적용하는
일이라 재설계안 장치 B의 L1(골든 해시 체인)과 같은 방향이고, 그 인프라는 P0에서
**미착수로 남은 항목**이다.

15번(languages)에 대해 한 줄 덧붙인다. 이번에 `tools/script.hpp`가 크게 바뀌었지만
**축 판정은 바뀌지 않는다** — 추가된 것은 실행 예산·깊이 상한·진단이고, `PROJECTS_PLAN.md:32`가
요구한 넷(토큰 위치·바이트코드 제어 흐름·**실행 예산**·호스트 바인딩) 중 하나가 채워졌을 뿐
바이트코드 VM도 호스트 바인딩도 없다. 스크립트는 여전히 5개 숫자를 담은 설정 파일이다.

### 이 절이 하지 않은 것

- **본문 수정 없음.** 32건의 지적 내용·근거 코드 인용·방향 제안은 이번에도 **한 글자도 지우지
  않았다.** 머리말 한 줄을 갱신하고, 상태가 바뀐 13개 항목 위에 새 상태 줄을 얹고(옛 상태 줄은
  아래 그대로 남겨 판정의 변화가 보이게 했다), 이 절을 덧붙인 것이 전부다.
- **`.md` 본문·소스 수정 없음.** 이 절은 보고서만 고친다.
- **재설계안의 규모 수치를 확정하지 않았다.** P0 벤치(A/B 스위치·벤치 하네스·골든 해시 체인)는
  여전히 미착수이므로 「규모 수치 (잠정 — P0에서 실측 후 확정)」은 잠정으로 남아 있다.
