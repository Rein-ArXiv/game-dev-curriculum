// Step 1 의 placeholder Scene. 두 개의 드래그 가능한 핸들을 화면에 두고,
// 그 사이를 잇는 선분 + 거리 표시. Scene framework / handle / panel /
// chrome 이 동작하는지 확인하는 용도.

#pragma once

#include "scenes/iscene.h"
#include "ui/handle.h"

#include "raylib.h"

namespace mathlab {

class WelcomeScene : public IScene {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) override;
    void DrawPanel(int panelX, int panelY, int panelW, int panelH) override;
    std::string_view Name() const override { return "Welcome"; }

private:
    DragHandle handleA_{};
    DragHandle handleB_{};
    bool       initialized_ = false;
};

}  // namespace mathlab
