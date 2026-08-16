// Step 1 의 placeholder Scene. 단일 원형 강체 + 중력 + 바닥 충돌.
// 강체 위의 핸들을 잡아 끌면 그 위치로 옮긴다 (속도 0 으로 reset).
// Scene framework / handle / panel / chrome / fixed dt accumulator 가 동작하는지
// 확인 + *위치 / 속도 / dt 적분 / 충돌 응답* 구조를 가장 단순한 형태로 시연.

#pragma once

#include "scenes/iscene.h"
#include "ui/handle.h"

#include "raylib.h"

namespace physlab {

class WelcomeScene : public IScene {
public:
    void OnEnter() override;
    void HandleInput() override;
    void Step(float dt) override;
    void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) override;
    void DrawPanel(int panelX, int panelY, int panelW, int panelH) override;
    std::string_view Name() const override { return "Welcome"; }

private:
    Vector2 position_    = {0, 0};
    Vector2 velocity_    = {0, 0};
    float   radius_      = 16.0f;
    float   mass_        = 1.0f;
    float   restitution_ = 0.6f;

    DragHandle spawnHandle_{};

    bool initialized_ = false;
    int  lastCanvasX_ = 0, lastCanvasY_ = 0;
    int  lastCanvasW_ = 0, lastCanvasH_ = 0;
};

}  // namespace physlab
