// Scene 인터페이스 — 각 AI 토픽이 한 Scene 으로 구현된다.
// main 의 메인 루프가 활성 Scene 의 OnEnter / Update / DrawWorld / DrawPanel
// 을 호출한다. Scene 사이 전환은 main 이 OnExit / OnEnter 를 차례로 호출한다.

#pragma once

#include <string_view>

namespace ailab {

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

}  // namespace ailab
