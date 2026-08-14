// net_lab — multiplayer 시리즈의 원리를 인터랙티브하게 측정·시각화하는 도구.
//
// 등록 Scene:
//   Welcome     — 범위/조작 안내
//   Rollback    — Delay vs Rollback 넷코드 비교 (part01)
//   Prediction  — 클라이언트 예측 + 서버 보정 (part07)
//   Lockstep    — 결정론 시뮬 + 디싱크 주입 (part01)
//   Matchmaking — MMR 큐 + Progressive Widening (part02)
//   LagSim      — 패킷 지연/손실 + 보간 vs 외삽 (part01/04)

#include "raylib.h"

#include "measure/frame_timer.h"
#include "scenes/iscene.h"
#include "scenes/lagsim_scene.h"
#include "scenes/lockstep_scene.h"
#include "scenes/matchmaking_scene.h"
#include "scenes/prediction_scene.h"
#include "scenes/rollback_scene.h"
#include "scenes/welcome_scene.h"
#include "ui/scene_chrome.h"

#include <memory>
#include <vector>

namespace {
constexpr int kWindowW = 1280;
constexpr int kWindowH = 720;
constexpr int kPanelW  = 280;
}  // namespace

int main() {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(kWindowW, kWindowH, "net_lab");
    SetTargetFPS(60);

    std::vector<std::unique_ptr<netlab::IScene>> scenes;
    scenes.emplace_back(std::make_unique<netlab::WelcomeScene>());
    scenes.emplace_back(std::make_unique<netlab::RollbackScene>());
    scenes.emplace_back(std::make_unique<netlab::PredictionScene>());
    scenes.emplace_back(std::make_unique<netlab::LockstepScene>());
    scenes.emplace_back(std::make_unique<netlab::MatchmakingScene>());
    scenes.emplace_back(std::make_unique<netlab::LagSimScene>());

    int activeIdx = 0;
    int prevIdx   = -1;
    netlab::FrameTimer timer;

    while (!WindowShouldClose()) {
        timer.BeginFrame();
        float dt = GetFrameTime();

        if (activeIdx != prevIdx) {
            if (prevIdx >= 0) scenes[(size_t)prevIdx]->OnExit();
            scenes[(size_t)activeIdx]->OnEnter();
            prevIdx = activeIdx;
        }

        int  screenW = GetScreenWidth();
        int  screenH = GetScreenHeight();
        auto layout  = netlab::ComputeLayout(screenW, screenH, kPanelW);

        scenes[(size_t)activeIdx]->Update(dt);

        BeginDrawing();
        ClearBackground(Color{12, 14, 18, 255});

        netlab::DrawCanvasBackground(layout.canvas);
        netlab::DrawPanelBackground(layout.panel);

        scenes[(size_t)activeIdx]->DrawWorld(
            (int)layout.canvas.x, (int)layout.canvas.y,
            (int)layout.canvas.width, (int)layout.canvas.height);
        scenes[(size_t)activeIdx]->DrawPanel(
            (int)layout.panel.x, (int)layout.panel.y,
            (int)layout.panel.width, (int)layout.panel.height);

        std::vector<std::string_view> labels;
        labels.reserve(scenes.size());
        for (const auto& s : scenes) labels.push_back(s->Name());
        int nextIdx = netlab::DrawTabBar(layout.tabBar, labels, activeIdx);

        netlab::DrawFooter(layout.footer, timer.SmoothedMs(), timer.Fps(),
                           scenes[(size_t)activeIdx]->Name());

        EndDrawing();
        activeIdx = nextIdx;
        timer.EndFrame();
    }

    if (prevIdx >= 0) scenes[(size_t)prevIdx]->OnExit();
    CloseWindow();
    return 0;
}
