#include "scenes/welcome_scene.h"

#include "raygui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace physlab {

namespace {

// 화면 좌표계 — y 아래 방향이 +. 따라서 중력 가속도 양수 → 아래로 떨어짐.
// 이 Scene 은 픽셀을 그대로 단위로 쓴다 (m / s 변환 없음).
constexpr float kGravity = 600.0f;    // px / s^2

}  // namespace

void WelcomeScene::OnEnter() {
    initialized_ = false;  // 첫 DrawWorld 에서 캔버스 크기에 맞춰 배치.
}

void WelcomeScene::HandleInput() {
    spawnHandle_.Update();
}

void WelcomeScene::Step(float dt) {
    // 드래그 중이면 강체는 핸들 따라가고 속도 0.
    if (spawnHandle_.IsDragging()) {
        position_ = spawnHandle_.Position();
        velocity_ = {0, 0};
        return;
    }

    // Semi-implicit Euler — 이 Scene 이 쓰는 유일한 적분기.
    velocity_.y += kGravity * dt;
    position_.x += velocity_.x * dt;
    position_.y += velocity_.y * dt;

    // 바닥 충돌 — 위치 교정 + 속도 반전 (단순 응답).
    float floorY = (float)(lastCanvasY_ + lastCanvasH_) - 8.0f;
    if (position_.y + radius_ > floorY) {
        position_.y = floorY - radius_;
        if (velocity_.y > 0) {
            velocity_.y = -velocity_.y * restitution_;
            if (std::fabs(velocity_.y) < 30.0f) velocity_.y = 0;
        }
    }

    float wallL = (float)lastCanvasX_ + 8.0f;
    float wallR = (float)(lastCanvasX_ + lastCanvasW_) - 8.0f;
    if (position_.x - radius_ < wallL) {
        position_.x = wallL + radius_;
        velocity_.x = -velocity_.x * restitution_;
    }
    if (position_.x + radius_ > wallR) {
        position_.x = wallR - radius_;
        velocity_.x = -velocity_.x * restitution_;
    }

    spawnHandle_.SetPosition(position_);
}

void WelcomeScene::DrawWorld(int cx, int cy, int cw, int ch) {
    lastCanvasX_ = cx; lastCanvasY_ = cy;
    lastCanvasW_ = cw; lastCanvasH_ = ch;

    if (!initialized_) {
        position_ = {(float)(cx + cw / 2), (float)(cy + ch / 3)};
        velocity_ = {120.0f, 0.0f};
        spawnHandle_.SetPosition(position_);
        initialized_ = true;
    }

    DrawRectangle(cx, cy, cw, ch, Color{20, 22, 28, 255});
    DrawRectangleLines(cx, cy, cw, ch, Color{60, 62, 70, 255});
    int floorY = cy + ch - 8;
    DrawLine(cx + 8, floorY, cx + cw - 8, floorY,
              Color{120, 130, 140, 255});

    DrawCircleV(position_, radius_, Color{255, 200, 80, 255});
    DrawCircleLines((int)position_.x, (int)position_.y, radius_,
                     Color{255, 230, 160, 255});

    Vector2 tip = {position_.x + velocity_.x * 0.1f,
                     position_.y + velocity_.y * 0.1f};
    DrawLineEx(position_, tip, 2.0f, Color{120, 200, 255, 220});

    spawnHandle_.Draw(Color{255, 90, 90, 180}, nullptr);

    DrawText("Step 1: skeleton + drag-to-reset body.",
              cx + 10, cy + 10, 14, Color{160, 160, 160, 255});
    DrawText("Drag the body to reset position. Bounces on floor + walls.",
              cx + 10, cy + 30, 14, Color{120, 120, 120, 255});
}

void WelcomeScene::DrawPanel(int panelX, int panelY, int panelW, int panelH) {
    (void)panelW;
    (void)panelH;

    int   y = panelY + 8;
    char  buf[160];

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 220, 18}, "Welcome scene");
    y += 24;

    std::snprintf(buf, sizeof(buf), "pos: (%.0f, %.0f)", position_.x, position_.y);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 220, 18}, buf);
    y += 18;

    std::snprintf(buf, sizeof(buf), "vel: (%.1f, %.1f) px/s",
                   velocity_.x, velocity_.y);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 220, 18}, buf);
    y += 18;

    float speed = std::sqrt(velocity_.x * velocity_.x
                              + velocity_.y * velocity_.y);
    std::snprintf(buf, sizeof(buf), "speed: %.1f px/s", speed);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 220, 18}, buf);
    y += 24;

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 220, 18},
              "integrator: semi-implicit Euler");
    y += 18;
    std::snprintf(buf, sizeof(buf), "restitution: %.2f", restitution_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 220, 18}, buf);
    y += 18;
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 220, 18},
              "fixed dt: 8.33 ms (120 Hz)");
    y += 24;

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 220, 18},
              "Drag body to reset position.");
    y += 18;
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 220, 18},
              "Skeleton scene: no integrator toggle here.");
}

}  // namespace physlab
