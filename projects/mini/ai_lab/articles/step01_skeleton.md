## 들어가며

`ai_lab`은 게임 AI 알고리즘의 결과만 보여 주는 데모가 아니다. 탐색이 어떤 셀을 열고 닫는지, 다수 에이전트가 어떤 벡터를 따라가는지, 파라미터 변화가 비용과 행동을 어떻게 바꾸는지를 관찰하는 실험 도구다.

첫 단계에서는 모든 실험이 공유할 골격을 만든다. raylib 창, Scene 인터페이스, 탭과 패널, 프레임 측정, 그리드 좌표 변환을 분리한다. 이후 알고리즘은 Scene 하나로 추가되며, UI나 메인 루프를 다시 고치지 않는다.

---

## 목차
1. [완성 목표](#1-완성-목표)
2. [Scene 경계](#2-scene-경계)
3. [공유 그리드](#3-공유-그리드)
4. [프레임 루프](#4-프레임-루프)
5. [검증 기준](#5-검증-기준)

---

## 1. 완성 목표

이 단계가 끝나면 다음 구조가 동작한다.

```text
main.cpp
  ├─ Scene 목록과 활성 탭
  ├─ FrameTimer
  └─ Update → DrawWorld → DrawPanel

scenes/iscene.h
  └─ OnEnter / OnExit / Update / DrawWorld / DrawPanel / Name

grid/grid.*
  └─ 셀 상태, 화면↔셀 변환, 마우스 편집, 그리기

ui/scene_chrome.*
  └─ 탭 바, 캔버스·패널·푸터 레이아웃
```

Scene은 알고리즘 상태를 소유하고, chrome은 화면 배치만 책임진다. 이 분리가 있어야 Pathfind Scene의 open list나 Boids Scene의 개체 배열이 UI 코드와 섞이지 않는다.

## 2. Scene 경계

`IScene`의 핵심 계약은 다음과 같다.

<!-- snippet: src/scenes/iscene.h#IScene -->
```cpp
class IScene {
public:
    virtual ~IScene() = default;

    virtual void OnEnter() {}
    virtual void OnExit() {}

    virtual void Update(float dt) = 0;

    // 메인 캔버스에 그리기.
    virtual void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) = 0;

    // 사이드 패널에 raygui 컨트롤 그리기.
    virtual void DrawPanel(int panelX, int panelY, int panelW, int panelH) = 0;

    virtual std::string_view Name() const = 0;
};
```

`Update`와 `DrawWorld`를 분리하는 이유는 측정과 재현성이다. 알고리즘 진행은 Update에서만 일어나야 하며, 화면을 두 번 그렸다고 탐색이 두 단계 진행되면 안 된다. `OnEnter`는 장면 전환 시 초기화할 상태를 명시한다. 탭을 바꾸면 메인 루프가 떠나는 Scene 의 `OnExit` 를 먼저 부르고, 들어오는 Scene 의 `OnEnter` 를 부른다.

## 3. 공유 그리드

경로 탐색, Flow Field, Influence Map은 같은 셀 공간을 사용한다. `Grid`는 알고리즘을 모르고 다음 기능만 제공한다.

- 폭·높이와 셀 배열
- 시작점·목표점·벽 상태
- `ScreenToCell`과 `CellCenter`
- 좌클릭 벽 추가, 우클릭 벽 제거
- 캔버스 크기에 맞춘 셀 배치

중요한 불변식은 *화면 좌표와 알고리즘 좌표를 섞지 않는 것* 이다. 알고리즘은 정수 셀 좌표만 받고, 픽셀 좌표 변환은 Grid가 담당한다.

## 4. 프레임 루프

메인 루프의 순서는 고정한다.

<!-- snippet: src/main.cpp#main -->
```cpp
while (!WindowShouldClose()) {
    timer.BeginFrame();
    float dt = GetFrameTime();
    // ... 탭이 바뀌었으면 OnExit -> OnEnter, 그리고 레이아웃 계산
    scenes[(size_t)activeIdx]->Update(dt);

    BeginDrawing();
    ClearBackground(Color{12, 14, 18, 255});

    ailab::DrawCanvasBackground(layout.canvas);
    ailab::DrawPanelBackground(layout.panel);

    scenes[(size_t)activeIdx]->DrawWorld(
        (int)layout.canvas.x, (int)layout.canvas.y,
        (int)layout.canvas.width, (int)layout.canvas.height);

    scenes[(size_t)activeIdx]->DrawPanel(
        (int)layout.panel.x, (int)layout.panel.y,
        (int)layout.panel.width, (int)layout.panel.height);
    // ... 탭 바와 푸터는 Scene 위에 그린다
    EndDrawing();

    activeIdx = nextIdx;
    timer.EndFrame();
}
```

FrameTimer는 순간값 대신 EMA를 제공한다. Scene을 바꿀 때 프레임 시간이 흔들려도 수치가 읽을 수 있는 속도로 움직인다. 이 값은 알고리즘 벤치마크가 아니라 UI가 과도하게 느려지는지 확인하는 안전 계기다.

## 5. 검증 기준

첫 단계에서는 알고리즘 정확성보다 경계가 중요하다.

1. 탭 전환 시 `OnEnter`가 한 번 호출된다.
2. 창 크기가 바뀌어도 캔버스와 패널이 겹치지 않는다.
3. 화면 가장자리 클릭이 그리드 범위를 벗어나지 않는다.
4. Draw 호출 횟수가 Scene 상태를 바꾸지 않는다.
5. Welcome Scene만 등록한 상태로 빌드·실행된다.

이 중 2번과 3번은 창 없이 판정된다. `ComputeLayout` 과 `Grid` 의 `Layout`·`CellCenter`·
`ScreenToCell`·`Resize` 는 raylib 함수를 하나도 부르지 않는 계산이라 `InitWindow` 없이
그대로 호출할 수 있다. 그래서 둘은 눈으로 보는 대신 테스트로 고정하고, 렌더 진입점과
Scene 생명주기에 달린 나머지(1·4·5)만 실행 화면에서 확인한다.

**실행과 관측**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release -R ai_step01_skeleton --output-on-failure
./build/Release/ai_chrome_tests step01   # 같은 네 케이스를 직접 실행
```

```text
1/1 Test #1: ai_step01_skeleton ...............   Passed
100% tests passed out of 1

ai_chrome_tests [step01] 4 case(s)
  ok  layout/canvas-panel-disjoint
  ok  grid/screen-round-trip
  ok  grid/edge-clicks-in-bounds
  ok  grid/resize-clamps-endpoints
chrome tests passed
```

`layout/canvas-panel-disjoint` 은 창 크기 20 가지를 훑으며 네 사각형이 화면을 겹침도
빈틈도 없이 덮는지 본다. 핵심은 부등호가 아니라 등호다 — 캔버스 오른쪽 끝과 패널 왼쪽
끝이 *정확히 같은 x* 여야 한다. `canvasW` 를 1 만 늘려도 두 사각형이 한 픽셀 겹치는데,
부등호로만 적으면 그 겹침이 통과한다.

<!-- snippet: tests/chrome_tests.cpp@layout-disjoint -->
```cpp
// 캔버스 오른쪽 끝과 패널 왼쪽 끝이 정확히 맞닿는다.
CHECK_CTX(L.canvas.x + L.canvas.width == L.panel.x, ctx);
CHECK_CTX(L.canvas.width + L.panel.width == (float)screenW, ctx);
CHECK_CTX(L.panel.width == (float)kPanelW, ctx);

// 세로도 같다: 탭바 → 콘텐츠 → 푸터가 이어 붙는다.
CHECK_CTX(L.tabBar.y == 0.0f, ctx);
CHECK_CTX(L.tabBar.y + L.tabBar.height == L.canvas.y, ctx);
CHECK_CTX(L.canvas.y + L.canvas.height == L.footer.y, ctx);
CHECK_CTX(L.footer.y + L.footer.height == (float)screenH, ctx);

// 캔버스와 패널은 같은 띠를 나눠 쓴다.
CHECK_CTX(L.canvas.y == L.panel.y, ctx);
CHECK_CTX(L.canvas.height == L.panel.height, ctx);
```

`grid/screen-round-trip` 은 모든 셀에서 `ScreenToCell(CellCenter(c)) == c` 를 확인한 뒤,
셀 중심의 양옆 반 칸을 찍어 셀 경계가 어디인지 다시 묻는다. 왕복만 보면 `CellCenter` 가
셀 *모서리* 를 돌려줘도 통과한다 — `ScreenToCell` 이 같은 식을 그대로 되돌리기 때문이다.
`grid/edge-clicks-in-bounds` 는 탭바·패널·푸터·창 밖 좌표를 찍어, `ScreenToCell` 이 참을
주면 반드시 격자 안이고 거짓이면 출력 인자를 건드리지 않는지 본다. 캔버스 밖 클릭이
그리드 편집으로 새는 경로가 이 지점에서 막힌다. `grid/resize-clamps-endpoints` 는
40x30 → 8x6 → 40x30 으로 크기를 왕복시켜 start·goal 이 새 범위 안으로 clamp 되고 살아남은
영역의 벽이 보존되는지 본다.

나머지 세 기준은 창을 띄워 본다.

```bash
./build/Release/ai_lab        # Windows (MSVC): build\Release\ai_lab.exe
```

실행하면 1280x720 크기의 창이 열리고, 상단 탭 바와 우측 280px 패널, 하단 푸터가 보인다. 푸터에는 활성 Scene 이름과 함께 프레임 `ms` 와 `fps` 가 EMA 값으로 갱신되므로 한 프레임이 튀어도 표시값이 순간적으로 요동치지 않는다. 창을 줄였다 늘려도 캔버스와 패널 경계가 겹치지 않고, 캔버스 밖(패널 위)에서 클릭해도 그리드 편집이 일어나지 않는다.

Scene 을 하나 붙이는 데 드는 비용은 `Scene` 인터페이스 구현 한 개와 등록 한 줄이다. 창·탭·패널·푸터·입력 라우팅은 이미 골격이 떠맡고 있으므로, 알고리즘과 검증 지표만 남는다.

---

## 결론

Scene 생명주기, 공유 그리드, 고정된 화면 경계, EMA 프레임 계기를 분리해 알고리즘 실험이 UI 상태에 오염되지 않는 실행 골격을 확보했다. 같은 입력과 초기 상태에서 Scene 전환과 렌더 호출이 시뮬레이션 결과를 바꾸지 않는 것이 이 단계의 핵심 결과다.

현재 실행 경로에는 Welcome Scene만 있으므로 탐색이나 행동 알고리즘의 정확성은 아직 검증 대상이 아니다. 이 단계가 보장하는 범위는 실험을 올릴 수 있는 경계와 측정 기반까지다.
