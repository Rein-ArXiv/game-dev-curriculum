# physics_lab — physics 시리즈의 원리 측정·시각화

## 컨셉

이 lab 은 **동일한 강체 시뮬을 여러 통합기(integrator)·충돌 응답·Broad Phase 로 돌려놓고 수치 안정성과 비용 차이를 측정**한다. N 개의 강체가 중력·접촉·제약 조건 아래 움직이는 워크로드를 고정해두고, 통합기를 Explicit Euler 에서 RK4 까지, 응답을 Penalty 에서 PBD 까지, Broad Phase 를 brute 에서 Spatial Hash 까지 토글한다. 토글이 바뀌는 순간 에너지 보존 그래프가 발산하거나 수렴하는 모습을 그대로 본다.

## 짝 시리즈

- **physics** — 수치 적분, 충돌 응답, Broad/Narrow Phase, 제약 해결을 다루는 시리즈의 주장을 이 lab 이 측정으로 검증한다.

대응 범위는 physics/01·02 (운동학·적분기), 05 (Broad Phase), 06·07 (SAT / GJK+EPA), 08 (법선 응답), 10·11·12 (제약·SI·PBD) 이다. 비범위 — 마찰 (part09), 회전 강체 동역학 / 관성 모멘트 (part03·04), 조인트 (part13), part14~20. 모든 물체는 점질량이거나 회전 자유도가 없는 원·다각형이고, 접선 impulse·각속도·토크는 구현하지 않는다.

## 토글 항목

| 항목 | 옵션 | 비교 대상 |
|---|---|---|
| 통합기 | Explicit Euler / Semi-implicit Euler / Verlet / RK4 | physics/02 적분기 |
| 충돌 응답 | Penalty / Impulse / Sequential Impulse / PBD | physics/08·11·12 (법선 impulse 만) |
| Broad Phase | brute O(n²) / SAP / Spatial Hash | physics/05 Broad Phase |
| 시간 간격 | dt = 1/30 / 1/60 / 1/120 / 1/240 | 안정성 곡선 |
| 강체 수 | BroadPhase 10~2000 / Response 4~100 / Measure 4~64 | 스케일링 |
| 반발 계수 | 0 / 0.5 / 1 | 에너지 보존 한계 |

> Semi-implicit Euler 는 symplectic Euler 의 다른 이름. 별도 옵션으로 두지 않는다.

## 출력

- 시뮬 뷰포트 — 강체와 접촉점, 법선 벡터, AABB 시각화.
- 에너지 그래프 — 총 운동에너지 + 위치에너지의 시간 추이. 발산하면 그래프의 y 축이 자동 확장되어 드러난다.
- 알고리즘 시간 그래프 — Broad Phase 알고리즘 us (Step 4), 응답 솔버 ms (Step 6). Scene 별로 자기 알고리즘 하나만 잰다.
- 충돌 카운트 — 프레임당 Broad Phase 후보 쌍 수, Narrow Phase 통과 쌍 수.
- 안정성 지표 — penetration depth 평균.

## 측정 항목

- **에너지 보존**: $E = \sum (\frac{1}{2}mv^2 + mgh)$ 매 프레임 합산.
- **알고리즘 비용**: Broad Phase 알고리즘 시간 (Step 4), 응답 솔버 시간 (Step 6) 을 해당 Scene 안에서 측정. 한 프레임을 Broad / Narrow / 응답 / 통합으로 쪼갠 분해 측정은 없다.
- **충돌 쌍 수**: Broad 에서 통과한 쌍 수, Narrow 에서 실제 접촉 판정된 쌍 수.
- **수치 안정성**: dt 를 키워가며 에너지가 발산하는 임계점.
- **동일 프로세스 반복성**: 같은 초기 상태에서 두 번 돌려 위치 차이 최댓값.
  cross-build·cross-machine 결정성을 증명하지 않는다.
- **불변식 property**: 고정 배치가 아니라 *생성기가 만든 배치 공간* 에서 계약을
  검사한다. 무작위 볼록쌍의 깊이 일치와 `(normal, depth)` 계약, 무작위 강체 장면의
  에너지 비증가·관통 잔량 상계·bit 결정성. 실패하면 입력을 shrink 해서 시드와
  재현 명령 한 줄과 함께 찍는다.

## 기술 스택

- C++20.
- raylib 5.0 (2D 시각화, 즉시 모드).
- raygui 4.0 (단일 헤더, 라벨 / 토글 / 슬라이더).
- 자체 구현 강체 엔진 (외부 물리 엔진 사용 안 함 — 모든 차이가 코드에 있어야 함).

> Dear ImGui / ImPlot 도입은 그래프 표현이 raygui 의 라인 그래프로 부족해질 때만 검토한다. Step 8 까지 raygui 만으로 충분하다고 본다.

## 디렉토리 구조 (현재 Step 진행 시점)

```text
physics_lab/
  CMakeLists.txt
  README.md
  articles/
    step01_skeleton.md
    step02_integrators.md
    step03_forces.md
    step04_broad_phase.md
    step05_narrow_phase.md
    step06_response.md
    step07_constraints.md
    step08_measure.md
  src/
    main.cpp                    fixed dt accumulator + Scene tab loop
    core/
      physics_core.h            Scene 공용 계산 코어 (적분기 / restitution target /
                                 누적 impulse / AABB / 거리 projection / 안전 법선)
    scenes/
      iscene.h                  HandleInput / Step(dt) / Draw* 분리
      welcome_scene.{h,cpp}     Step 1
      integrators_scene.{h,cpp} Step 2
      forces_scene.{h,cpp}      Step 3
      broad_phase_scene.{h,cpp} Step 4
      narrow_phase_scene.{h,cpp} Step 5
      response_scene.{h,cpp}    Step 6
      constraints_scene.{h,cpp} Step 7
      measure_scene.{h,cpp}     Step 8
    ui/
      handle.{h,cpp}            DragHandle
      scene_chrome.{h,cpp}      탭 / 패널 / 푸터
      line_graph.{h,cpp}        GraphSeries + 라인 차트 (Step 2~)
      raygui_impl.cpp           RAYGUI_IMPLEMENTATION
    measure/
      frame_timer.{h,cpp}
  tests/
    step_cases.h                {step, 이름, 함수} 표 + argv[1] 케이스 필터 하네스
    physics_chrome_tests.cpp    ComputeLayout 의 분할 규약 (step01)
    physics_core_tests.cpp      core 헤더 단위 테스트 (step02 / step04 / step06 / step07)
    physics_forces_tests.cpp    force 항·종단 속도·에너지 소산 — forces_scene.cpp 를
                                 그대로 컴파일 (step03)
    physics_collision_tests.cpp SAT / GJK+EPA 테스트 — narrow_phase_scene.cpp 를
                                 그대로 컴파일 (step05)
    physics_broad_tests.cpp     brute / SAP / Spatial Hash 의 overlap 쌍 집합 대조
                                 (step04)
    physics_response_tests.cpp  접촉 수집 / SI / 위치 보정 (step06)
    physics_constraints_tests.cpp chain solve · Baumgarte drift (step07)
    physics_determinism_tests.cpp 같은 seed 반복 재현성 (step08)
    property_kit.h              property 실행 하네스 — 결정적 난수원(splitmix64) +
                                 표본별 시드 파생 + shrink 루프 + 반례 보고
    sat_reference.h             독립 참조 구현 (교집합 길이 vs 진짜 MTD) —
                                 collision 타깃과 property 타깃이 함께 쓴다
    physics_property_tests.cpp  불변식 property 층 — 무작위 볼록쌍(step05) ·
                                 무작위 강체 장면(step06) · bit 결정성(step08)
```

Scene 테스트 타깃은 모두 *제품 TU 를 그대로 컴파일* 한다. 테스트 전용 사본은 두지
않으며, GUI 진입점 (`DrawWorld` / `DrawPanel`) 을 부르지 않으므로 창 없이 돈다.
캔버스 좌표처럼 GUI 가 넘겨주던 값은 각 Scene 의 `SetCanvas` 같은 세터로 주입한다.

각 실행 파일은 `argv[1]` 로 케이스 필터를 받는다 (`step_cases.h`). 인자가 없으면
표 전체를 돌고, `step04` 나 `aabb_overlap` 을 주면 그것만 돈다. `--list` 는 표를
찍는다. 필터가 아무것도 고르지 못하면 조용히 통과하지 않고 exit 2 로 실패한다.

`physics_property_tests` 는 `argv[2]` 로 시드를 하나 더 받는다. 반례 보고가 찍는
재현 명령이 그 형태다 — `physics_property_tests convex_pair 12793680451439230182`
은 그 시드가 만든 표본 하나만 돌린다. 무작위는 전부 고정 마스터 시드에서
파생되고 (splitmix64), 벽시계나 프로세스 시간은 어디에도 들어가지 않는다.

## 디렉토리 구조 (확장 / 리팩터링 목표)

> 현재는 각 Scene 안에 로컬 구현이 많다. 코드량이 커지거나 알고리즘이 다른
> Scene 사이에서 재사용될 때 아래 구조로 모듈화하면 좋다.

```text
physics_lab/
  src/
    main.cpp
    scenes/
      welcome_scene.cpp         단일 강체 데모
      integrators_scene.cpp     적분기 4 종 비교 (Step 2)
      forces_scene.cpp          외력·스프링·드래그 (Step 3)
      broad_phase_scene.cpp     brute / SAP / spatial hash (Step 4)
      narrow_phase_scene.cpp    SAT / GJK / EPA (Step 5)
      response_scene.cpp        penalty / impulse / SI / PBD (Step 6)
      constraints_scene.cpp     distance constraint (PBD / SI+Baumgarte) (Step 7)
      measure_scene.cpp         에너지·결정성·안정성 측정 (Step 8)
    body/                       강체 자료구조 (Step 2+ 공용)
    integrator/                 적분기 4 종 구현 (Step 2)
    broad/                      brute / SAP / spatial hash (Step 4)
    narrow/                     SAT / GJK / EPA (Step 5)
    response/                   응답 4 종 (Step 6)
    measure/                    energy / timing / determinism (Step 8)
```

## 빌드와 실행

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
ctest --test-dir build -C Release --output-on-failure
./build/Release/physics_lab     # MSVC
./build/physics_lab             # 그 외
```

특정 타깃만 돌릴 때는 `ctest --test-dir build -C Release -R physics_broad_tests` 처럼
`-R` 로 이름을 건다. step 단위로 돌릴 때도 같은 방식이다 —
`ctest --test-dir build -C Release -R step04` 는 그 step 이 추가한 검사만 돌린다.

`ctest` 는 23 개를 돌린다 — 기대 출력은 `100% tests passed out of 23` (CMake 4.4 기준).
등록 이름이 두 층이다. 실행 파일 하나를 통째로 도는 이름 9 개와, 그 실행 파일들을
step 단위로 자른 이름 14 개.

| 타깃 | 검사 대상 | step 이름 |
|---|---|---|
| `physics_chrome_tests` | `ComputeLayout` 의 네 사각형 분할 규약 (빈틈·겹침 없음), 실제 창 1280×720 / 패널 280 의 좌표 리터럴, 퇴화 패널 폭 | `step01.layout` |
| `physics_core_tests` | 적분기 오차 차수·에너지 거동, restitution target 리터럴, 누적 impulse 의 λ ≥ 0 클램프, AABB, 거리 projection | `step02.integrators` · `step04.aabb_overlap` · `step06.contact_impulse` · `step07.distance_projection` |
| `physics_forces_tests` | 중력·스프링·선형/2 차 drag 의 크기와 부호, 두 drag 의 종단 속도 (200 / 316.23), 보존력만 있을 때 총 에너지의 평탄성과 drag 의 단조 감소 | `step03.forces` |
| `physics_collision_tests` | SAT / GJK+EPA 의 hit·miss, 해석적 침투 깊이 (0.5 / 0.75 / 1+√2), 법선 부호 규약, 포함 배치에서 두 구현의 깊이 일치, `normal` 로 `depth` 만큼 밀면 분리되는지, 무작위 100 배치 대조 | `step05.narrow_phase` |
| `physics_broad_tests` | SAP / Spatial Hash 가 낸 overlap **쌍 집합** 이 brute 와 같은지 — 경계 접촉·음수 셀·`cellSize < bodyHalf`·무작위 50 배치·퇴화 배치 | `step04.broad_phase` |
| `physics_response_tests` | 접촉 수집의 깊이·법선, 위치 보정량 `(depth - slop) * factor`, SI 수렴, iteration 증가 시 침투 단조 감소 | `step06.response` |
| `physics_constraints_tests` | chain 구성, PBD iteration 이 stiffness 로 작동하는지, Baumgarte 가 SI 의 position drift 를 줄이는지 | `step07.constraints` |
| `physics_determinism_tests` | 같은 seed 600 스텝 두 번이 bit-exact 인지, contact 순서를 섞으면 갈라지는지, 900 스텝 뒤 캔버스 안에 가라앉는지, Response Scene 과 같은 솔버인지 | `step08.determinism` |
| `physics_property_tests` | 입력을 손으로 고르지 않는 층. 무작위 볼록쌍 1500 표본 (SAT 깊이 = EPA 깊이 = 독립 참조의 MTD, `(normal, depth)` 양면 계약, 인자 대칭, 강체 변환 불변), 무작위 강체 장면 (중력 0·반발 0 에서 운동에너지 비증가, 관통 잔량·에너지 상계, NaN/Inf 부재, 정착, 배터리의 프레임이 Response Scene 과 bit 동일), 무작위 장면의 bit 결정성 + 순서 변경 음성 대조군 | `step05.property` · `step06.property` · `step08.property` |

8개 Scene (Welcome / Integrators / Forces / BroadPhase / NarrowPhase / Response / Constraints / Measure) 이 탭으로 전환된다. 좌측은 캔버스, 우측 280 px 패널이 토글 / 슬라이더 / 상태 라벨을 노출. 하단 24 px 푸터에 ms/fps 표시.

## 시리즈 글과의 관계

physics 시리즈에서 "Explicit Euler 가 발산하는 dt 를 Semi-implicit 이 견딘다", "Sequential Impulse 가 Penalty 대비 안정적이다" 같은 주장이 이 lab 의 그래프에서 그대로 재현된다. "Spatial Hash 가 SAP 보다 동적 씬에서 빠르다" 같은 주장은 *워크로드 (균등 분포 + 잦은 삽입 / 삭제) 에 따라 다르다* — 그 조건도 lab 안에서 토글로 바꿔가며 직접 측정한다. 발산하는 순간을 직접 눈으로 보면 글의 주장이 수치적 직관으로 자리 잡는다.

## 거대 프로젝트와의 관계

거대 프로젝트에는 강체 충돌·Sequential Impulse 모듈과 테스트가 있지만 현재 고정 경로 디펜스 제품은 이를 직접 호출하지 않는다. 디펜스의 적과 발사체는 경로 이동과 단순 교차 판정만으로 충분하기 때문이다. 이 lab은 강체, 관절, 로프나 천이 실제 제품 요구사항이 될 때 적분기·Broad Phase·Impulse·PBD를 선택할 측정 근거를 만든다.
