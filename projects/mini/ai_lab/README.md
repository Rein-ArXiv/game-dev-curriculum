# ai_lab — ai 시리즈의 원리 측정·시각화

## 컨셉

이 lab은 **인터랙티브 그리드/캔버스 위에서 게임 AI 알고리즘의 내부 상태를 직접 관찰**하는 도구다. 마우스로 장애물을 그리면 A*/Dijkstra/Greedy/JPS 같은 탐색이 즉시 다시 실행되며 open/closed 셀 컬러링과 경로가 갱신된다. Flow Field, Steering, Boids, Influence Map도 같은 도구 안의 별도 Scene으로 얹어 비교한다.

각 Scene은 ai 시리즈 글의 도식·의사 코드를 *살아 있는 시각화*로 만든다. 글에서 "JPS는 jump point만 open list에 넣어 A* 대비 expand 노드 수를 줄인다"고 적은 문장이, 이 lab에서는 같은 맵에서 두 알고리즘의 expand 수가 실제로 갈라지는 수치로 확인된다.

## 짝 시리즈

- **ai** — 길찾기, Flow Field, Steering, 군집(Boids), 영향력 맵 등 게임 AI 주제를 다루는 시리즈. 이 lab이 그 알고리즘들을 실행 가능한 형태로 만든다.

## Scene와 토글

상단 탭으로 Scene을 전환한다. 각 Scene의 우측 패널에 raygui 슬라이더/콤보/버튼이 있다.

| Scene | 토글 / 슬라이더 | 측정·관찰 | 참조 |
|---|---|---|---|
| **Pathfind** | Algorithm(A*/Dijkstra/Greedy/JPS), Heuristic(Manhattan/Euclidean/Octile/Chebyshev), grid 크기, auto-run, step/s, Step/Run/Reset | expand 노드 수, max open size, path cost/len | ai/part06~07 |
| **FlowField** | grid 크기, 에이전트 수, separation 가중치, show arrows | integration 히트맵, flow 벡터장, 다수 에이전트 추종 | ai/part10 |
| **Steering** | Behavior(Seek/Arrive/Wander/Pursue), max speed/force, slow radius | 궤적(trail), 속도 벡터 | ai/part11 |
| **Boids** | count, perception, separation/alignment/cohesion 가중치, max speed | 무리 창발, 공간 해시 가속 | ai/part12 |
| **Influence** | view(Control/Tension/Vulnerability), grid, decay, spread, iters | 영향력 히트맵, stamp/spread/decay 루프 | ai/part09 |

## 조작

- **좌클릭 드래그** — 벽 그리기 (Pathfind/FlowField).
- **우클릭 드래그** — 벽 지우기.
- **Influence Scene** — 좌클릭=아군 유닛, 우클릭=적 유닛 배치.
- **Steering Scene** — 마우스 위치가 목표 (Pursue는 자동 원운동 타깃).
- 패널 슬라이더/콤보/버튼으로 알고리즘과 파라미터를 토글한다.

## 측정 항목

- 탐색 expand 노드 수 (A* vs Dijkstra vs Greedy vs JPS).
- open list 최대 크기 (메모리 근사).
- 경로 비용 / 길이 — Octile·Euclidean·Chebyshev처럼 이 8방향 비용 모델에서 admissible한 휴리스틱을 쓰면 A*/Dijkstra/JPS는 같은 최단 비용을 내야 한다. Manhattan은 대각 이동에서 과대평가하므로 비최적 경로가 가능한 비교용 설정이고, Greedy도 최적성을 보장하지 않는다.
- 프레임 ms / fps (하단 푸터, EMA 평활).

## 기술 스택

- C++20.
- **raylib 5.0** — 윈도우/2D 드로잉/입력 (FetchContent로 자동 취득, 시스템 설치 불필요).
- **raygui 4.0** — 패널 컨트롤 (단일 헤더).

> 같은 mini 시리즈의 `math_lab` / `physics_lab` 과 동일한 raylib + raygui Scene 아키텍처를 따른다. (초기 설계 메모의 ImGui/ImPlot 대신, 검증된 2D lab 패턴인 raygui를 사용한다.)

## 디렉토리 구조

```text
ai_lab/
  CMakeLists.txt
  src/
    main.cpp                  Scene 등록 + 메인 루프
    measure/frame_timer.*     ms/fps EMA 측정
    ui/
      scene_chrome.*          탭 바 / 푸터 / 레이아웃
      raygui_impl.cpp         raygui 단일 정의 TU
    grid/grid.*               공유 그리드 (마우스 페인트, 좌표 변환)
    pathfind/pathfinder.h     A*/Dijkstra/Greedy/JPS (step 가능, header-only)
    model/                    raylib 비의존 계산 코어 (header-only, 테스트 대상)
      ai_model.h              속도 절단 / 영향력 결합 / flow 방향 선택 / 축 반사
      steering.h              Seek·Arrive·Wander·Pursue 한 스텝 + 경계 처리
      flow_field.h            integration field 생성 (goal 기점 8방향 Dijkstra)
      influence.h             영향력 장 한 틱 (decay → stamp → spread)
    scenes/
      iscene.h                Scene 인터페이스
      welcome_scene.*
      pathfind_scene.*
      flowfield_scene.*
      steering_scene.*
      boids_scene.*
      influence_scene.*
  tests/
    pathfinder_tests.cpp      → ai_pathfinder_tests 타깃
    ai_model_tests.cpp        → ai_model_tests 타깃
```

## 빌드와 실행

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/Release/ai_lab        # Windows (MSVC): build/Release/ai_lab.exe
```

raylib·raygui는 CMake FetchContent로 자동으로 가져온다. 인터넷 연결과 C++20 컴파일러만 있으면 된다.

## 자동 테스트

창을 띄우지 않고 도는 테스트 타깃이 두 개 있다. 계산 코어는 전부 raylib 비의존 헤더에 있어 GUI 없이 실행된다.

```bash
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

개별 실행:

```bash
ctest --test-dir build -C Release -R ai_pathfinder_tests --output-on-failure
ctest --test-dir build -C Release -R ai_model_tests --output-on-failure
./build/Release/ai_pathfinder_tests   # "pathfinder tests passed"
./build/Release/ai_model_tests        # "AI model tests passed"
```

| 타깃 | 검증 대상 | 고정하는 것 |
|---|---|---|
| `ai_pathfinder_tests` | `pathfind/pathfinder.h`, `grid/grid.cpp` | 절대 경로 비용(직선 1·대각 √2), `Path()` 의 시작·끝·8-인접·통행 가능성·비용 재계산 일치, 시드 1..200 랜덤 맵에서 JPS ≡ Dijkstra, 대각 forced neighbor 고정 맵, `expanded`/`maxOpen` 부등식, Greedy·Manhattan 의 비최적성 |
| `ai_model_tests` | `model/*.h` | 속도 상한 `\|v\| <= maxSpeed`(프레임 스톨 dt 포함), Arrive 감속 단조성, Pursue lookahead 값, 경계 반사 부호, integration field 의 octile 해석해·무한대·우회 비용, 영향력 장의 확산 거리·감쇠·벽 차단·결정론 |

두 타깃 모두 `src/scenes/*.cpp` 를 컴파일하지 않는다. Scene 은 입력·렌더와 파라미터 전달만 맡고 계산은 `pathfind/`·`model/` 헤더가 담당하므로, 이 테스트가 죽이는 결함은 앱이 실제로 쓰는 코드의 결함이다.

## 구현 범위 메모

- 길찾기는 A*/Dijkstra/Greedy/JPS 4종으로 구현했다. ai/part07 의 Theta*/HPA*/D* Lite, 그리고 NavMesh(part08)는 그리드 기반 이 lab의 범위를 넘는 *설계 토픽*으로 남긴다 (글에서 다룬다).
- 이동 모델은 8방향·코너 컷 허용으로 통일했다. 최단 비용 동등성 검증은 Octile·Euclidean·Chebyshev 휴리스틱으로 수행한다.
- BT 시각화(part01~05)와 인지/분대/슬롯/보스(part13~16)는 그리드 시각화보다 별도 UI가 필요해, 이 lab의 첫 범위에서 제외했다. 의사결정·전술 토픽은 시리즈 글에서 다룬다.
- part17~25(국소 회피·AI 디버깅·적대 탐색·행동 계층·블랙보드·EQS 등)도 구현하지 않았다. 특히 part21의 Velocity Obstacle / RVO / ORCA는 없고, FlowField Scene의 회피는 반경 안 이웃을 밀어내는 O(n²) separation 한 종류뿐이다 — part21이 한계로 지적하는 바로 그 방식이다.
- part10 중 이 lab이 구현한 것은 integration field와 flow 벡터장까지다. cost field 단계(셀별 통행 비용 인코딩, 정적·동적 비용 합성)는 없어 이동 비용이 직선 1·대각 √2로 균일하며, 유닛별 양선형 보간도 없어 에이전트는 자기 셀의 방향 벡터를 그대로 읽는다.
- 정리하면 이 lab이 실행 가능한 형태로 다루는 것은 25편 중 part06, part07(일부), part09, part10(일부), part11, part12 다.
