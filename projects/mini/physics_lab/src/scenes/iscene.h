// Scene 인터페이스 — 각 물리 토픽이 한 Scene 으로 구현된다.
// main 이 fixed dt accumulator 로 시뮬을 돌리기 때문에 Scene 콜백이
// 두 갈래로 나뉜다:
//
//   HandleInput()  — 프레임당 1 번. 마우스·키보드·DragHandle 갱신, 토글 상태
//                    변화 같은 *frame-rate dependent* 입력 처리.
//   Step(dt)       — 누적 시간이 fixed dt 만큼 쌓일 때마다 호출. 따라서
//                    프레임당 0~N 번. *순수 물리 적분 + 충돌 응답* 만.
//
// 이렇게 분리한 이유:
//   - 적분기 비교 (Step 2 부터) 에서 dt 가 일정해야 *수치적 동작이 재현 가능* .
//   - 결정성 측정 (Step 8) 이 가능해진다 — 같은 입력 시퀀스에서 같은 결과.
//   - 프레임 드랍이 있어도 시뮬은 *실제 시간만큼 따라잡음* (spiral of death
//     방지를 위해 maxAccum 으로 cap).

#pragma once

#include <string_view>

namespace physlab {

class IScene {
public:
    virtual ~IScene() = default;

    // Scene 이 활성화될 때 호출. 자료구조 초기화, 핸들 위치 리셋 등.
    virtual void OnEnter() {}

    // Scene 이 비활성화될 때 호출. 리소스 해제.
    virtual void OnExit() {}

    // 프레임당 1 번. 마우스 / 키보드 / DragHandle 갱신, 토글 변경.
    // 기본 구현은 no-op — 입력 처리가 없는 Scene 도 가능.
    virtual void HandleInput() {}

    // 누적 시간이 dt 만큼 쌓일 때마다 호출. dt 는 항상 같은 값 (fixed).
    // 물리 적분 + 충돌 응답 + 제약 해결만.
    virtual void Step(float dt) = 0;

    // 메인 캔버스에 그리기. 렌더 영역은 (canvasX, canvasY, canvasW, canvasH).
    virtual void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) = 0;

    // 사이드 패널에 그리기. 패널 영역은 (panelX, panelY, panelW, panelH).
    // raygui 컨트롤로 상태 표시 + 토글 / 슬라이더 노출.
    virtual void DrawPanel(int panelX, int panelY, int panelW, int panelH) = 0;

    // Scene 의 이름 — 탭 라벨에 쓰임.
    virtual std::string_view Name() const = 0;
};

}  // namespace physlab
