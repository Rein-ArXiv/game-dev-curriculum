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
    physics_core_tests.cpp      core 헤더 단위 테스트 (ctest: physics_core_tests)
    physics_collision_tests.cpp SAT / GJK+EPA 테스트 — narrow_phase_scene.cpp 를
                                 그대로 컴파일 (ctest: physics_collision_tests)
    physics_broad_tests.cpp     brute / SAP / Spatial Hash 의 overlap 쌍 집합 대조
                                 (ctest: physics_broad_tests)
    physics_response_tests.cpp  접촉 수집 / SI / 위치 보정 (ctest: physics_response_tests)
    physics_constraints_tests.cpp chain solve · Baumgarte drift
                                 (ctest: physics_constraints_tests)
    physics_determinism_tests.cpp 같은 seed 반복 재현성 (ctest: physics_determinism_tests)
```

Scene 테스트 타깃은 모두 *제품 TU 를 그대로 컴파일* 한다. 테스트 전용 사본은 두지
않으며, GUI 진입점 (`DrawWorld` / `DrawPanel`) 을 부르지 않으므로 창 없이 돈다.
캔버스 좌표처럼 GUI 가 넘겨주던 값은 각 Scene 의 `SetCanvas` 같은 세터로 주입한다.

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
`-R` 로 이름을 건다.

`ctest` 는 여섯 개를 돌린다 — 기대 출력은 `100% tests passed out of 6` (CMake 4.4 기준).

| 타깃 | 검사 대상 |
|---|---|
| `physics_core_tests` | 적분기 오차 차수·에너지 거동, restitution target 리터럴, 누적 impulse 의 λ ≥ 0 클램프, AABB, 거리 projection |
| `physics_collision_tests` | SAT / GJK+EPA 의 hit·miss, 해석적 침투 깊이 (0.5 / 0.75 / 1+√2), 법선 부호 규약, 포함 배치에서 두 구현의 깊이 일치, `normal` 로 `depth` 만큼 밀면 분리되는지, 무작위 100 배치 대조 |
| `physics_broad_tests` | SAP / Spatial Hash 가 낸 overlap **쌍 집합** 이 brute 와 같은지 — 경계 접촉·음수 셀·`cellSize < bodyHalf`·무작위 50 배치 |
| `physics_response_tests` | 접촉 수집의 깊이·법선, 위치 보정량 `(depth - slop) * factor`, SI 수렴, iteration 증가 시 침투 단조 감소 |
| `physics_constraints_tests` | chain 구성, PBD iteration 이 stiffness 로 작동하는지, Baumgarte 가 SI 의 position drift 를 줄이는지 |
| `physics_determinism_tests` | 같은 seed 600 스텝 두 번이 bit-exact 인지, contact 순서를 섞으면 갈라지는지, 900 스텝 뒤 캔버스 안에 가라앉는지 |

8개 Scene (Welcome / Integrators / Forces / BroadPhase / NarrowPhase / Response / Constraints / Measure) 이 탭으로 전환된다. 좌측은 캔버스, 우측 280 px 패널이 토글 / 슬라이더 / 상태 라벨을 노출. 하단 24 px 푸터에 ms/fps 표시.

## 시리즈 글과의 관계

physics 시리즈에서 "Explicit Euler 가 발산하는 dt 를 Semi-implicit 이 견딘다", "Sequential Impulse 가 Penalty 대비 안정적이다" 같은 주장이 이 lab 의 그래프에서 그대로 재현된다. "Spatial Hash 가 SAP 보다 동적 씬에서 빠르다" 같은 주장은 *워크로드 (균등 분포 + 잦은 삽입 / 삭제) 에 따라 다르다* — 그 조건도 lab 안에서 토글로 바꿔가며 직접 측정한다. 발산하는 순간을 직접 눈으로 보면 글의 주장이 수치적 직관으로 자리 잡는다.

## 거대 프로젝트와의 관계

거대 프로젝트에는 강체 충돌·Sequential Impulse 모듈과 테스트가 있지만 현재 고정 경로 디펜스 제품은 이를 직접 호출하지 않는다. 디펜스의 적과 발사체는 경로 이동과 단순 교차 판정만으로 충분하기 때문이다. 이 lab은 강체, 관절, 로프나 천이 실제 제품 요구사항이 될 때 적분기·Broad Phase·Impulse·PBD를 선택할 측정 근거를 만든다.
