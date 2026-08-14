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

```cpp
class IScene {
public:
    virtual ~IScene() = default;
    virtual std::string_view Name() const = 0;
    virtual void OnEnter() {}
    virtual void OnExit() {}
    virtual void Update(float dt) = 0;
    virtual void DrawWorld(int x, int y, int w, int h) = 0;
    virtual void DrawPanel(int x, int y, int w, int h) = 0;
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

```cpp
timer.BeginFrame();
activeScene.Update(dt);

BeginDrawing();
DrawChrome();
activeScene.DrawWorld(...);
activeScene.DrawPanel(...);
EndDrawing();

timer.EndFrame();
```

FrameTimer는 순간값 대신 EMA를 제공한다. Scene을 바꿀 때 프레임 시간이 흔들려도 수치가 읽을 수 있는 속도로 움직인다. 이 값은 알고리즘 벤치마크가 아니라 UI가 과도하게 느려지는지 확인하는 안전 계기다.

## 5. 검증 기준

첫 단계에서는 알고리즘 정확성보다 경계가 중요하다.

1. 탭 전환 시 `OnEnter`가 한 번 호출된다.
2. 창 크기가 바뀌어도 캔버스와 패널이 겹치지 않는다.
3. 화면 가장자리 클릭이 그리드 범위를 벗어나지 않는다.
4. Draw 호출 횟수가 Scene 상태를 바꾸지 않는다.
5. Welcome Scene만 등록한 상태로 빌드·실행된다.

**실행과 관측**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/Release/ai_lab        # Windows (MSVC): build\Release\ai_lab.exe
```

실행하면 1280x720 크기의 창이 열리고, 상단 탭 바와 우측 280px 패널, 하단 푸터가 보인다. 푸터에는 활성 Scene 이름과 함께 프레임 `ms` 와 `fps` 가 EMA 값으로 갱신되므로 한 프레임이 튀어도 표시값이 순간적으로 요동치지 않는다. 창을 줄였다 늘려도 캔버스와 패널 경계가 겹치지 않고, 캔버스 밖(패널 위)에서 클릭해도 그리드 편집이 일어나지 않는다.

Scene 을 하나 붙이는 데 드는 비용은 `Scene` 인터페이스 구현 한 개와 등록 한 줄이다. 창·탭·패널·푸터·입력 라우팅은 이미 골격이 떠맡고 있으므로, 알고리즘과 검증 지표만 남는다.

---

## 결론

Scene 생명주기, 공유 그리드, 고정된 화면 경계, EMA 프레임 계기를 분리해 알고리즘 실험이 UI 상태에 오염되지 않는 실행 골격을 확보했다. 같은 입력과 초기 상태에서 Scene 전환과 렌더 호출이 시뮬레이션 결과를 바꾸지 않는 것이 이 단계의 핵심 결과다.

현재 실행 경로에는 Welcome Scene만 있으므로 탐색이나 행동 알고리즘의 정확성은 아직 검증 대상이 아니다. 이 단계가 보장하는 범위는 실험을 올릴 수 있는 경계와 측정 기반까지다.
