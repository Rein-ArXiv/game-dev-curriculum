// Scene 인터페이스 — 각 수학 토픽이 한 Scene 으로 구현된다.
// main 의 메인 루프가 활성 Scene 의 OnEnter / Update / DrawWorld / DrawPanel
// 을 호출한다. Scene 사이 전환은 main 이 OnExit / OnEnter 를 차례로 호출한다.

#pragma once

#include <string_view>

namespace mathlab {

class IScene {
public:
    virtual ~IScene() = default;

    // Scene 이 활성화될 때 호출. 자료구조 초기화, 핸들 위치 리셋 등.
    virtual void OnEnter() {}

    // Scene 이 비활성화될 때 호출. 리소스 해제.
    virtual void OnExit() {}

    // 매 프레임 입력·로직 갱신. dt 단위 초.
    virtual void Update(float dt) = 0;

    // 메인 캔버스에 그리기. 렌더 영역은 (canvasX, canvasY, canvasW, canvasH).
    virtual void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) = 0;

    // 사이드 패널에 그리기. 패널 영역은 (panelX, panelY, panelW, panelH).
    // raygui 컨트롤로 상태 표시 + 토글 / 슬라이더 노출.
    virtual void DrawPanel(int panelX, int panelY, int panelW, int panelH) = 0;

    // Scene 의 이름 — 탭 라벨에 쓰임.
    virtual std::string_view Name() const = 0;
};

}  // namespace mathlab
