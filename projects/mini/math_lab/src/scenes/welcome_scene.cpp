#include "scenes/welcome_scene.h"

#include "raygui.h"

#include <cmath>
#include <cstdio>

namespace mathlab {

void WelcomeScene::OnEnter() {
    initialized_ = false;  // 첫 DrawWorld 에서 캔버스 크기에 맞춰 핸들 배치
}

void WelcomeScene::Update(float dt) {
    (void)dt;
    handleA_.Update();
    handleB_.Update();
}

void WelcomeScene::DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) {
    if (!initialized_) {
        handleA_.SetPosition({(float)(canvasX + canvasW / 3),
                               (float)(canvasY + canvasH / 2)});
        handleB_.SetPosition({(float)(canvasX + 2 * canvasW / 3),
                               (float)(canvasY + canvasH / 2)});
        initialized_ = true;
    }

    // 캔버스 배경
    DrawRectangle(canvasX, canvasY, canvasW, canvasH, Color{20, 22, 28, 255});
    DrawRectangleLines(canvasX, canvasY, canvasW, canvasH, Color{60, 62, 70, 255});

    // 핸들 두 개를 잇는 선분 + 거리
    Vector2 a = handleA_.Position();
    Vector2 b = handleB_.Position();
    DrawLineEx(a, b, 2.0f, Color{180, 200, 220, 255});

    handleA_.Draw(Color{255, 200, 80, 255}, "A");
    handleB_.Draw(Color{120, 200, 255, 255}, "B");

    float dx   = b.x - a.x;
    float dy   = b.y - a.y;
    float dist = std::sqrt(dx * dx + dy * dy);
    char  buf[64];
    std::snprintf(buf, sizeof(buf), "dist = %.1f px", dist);
    Vector2 mid = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f - 12.0f};
    DrawText(buf, (int)mid.x - 50, (int)mid.y, 16, Color{220, 220, 220, 255});

    DrawText("Step 1: scene framework + drag handle.",
              canvasX + 10, canvasY + 10, 14, Color{160, 160, 160, 255});
    DrawText("Drag the two circles. Other tabs become live in later steps.",
              canvasX + 10, canvasY + 30, 14, Color{120, 120, 120, 255});
}

void WelcomeScene::DrawPanel(int panelX, int panelY, int panelW, int panelH) {
    (void)panelW;
    (void)panelH;
    int   y    = panelY + 8;
    char  buf[128];
    Vector2 a = handleA_.Position();
    Vector2 b = handleB_.Position();
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 220, 18}, "Welcome scene");
    y += 24;
    std::snprintf(buf, sizeof(buf), "A: (%.0f, %.0f)", a.x, a.y);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 220, 18}, buf);
    y += 18;
    std::snprintf(buf, sizeof(buf), "B: (%.0f, %.0f)", b.x, b.y);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 220, 18}, buf);
    y += 24;
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 220, 18},
              "drag with left mouse");
}

}  // namespace mathlab
