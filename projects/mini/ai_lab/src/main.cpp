// ai_lab — ai 시리즈의 알고리즘을 인터랙티브하게 시각화·측정하는 도구.
//
// 등록 Scene:
//   Welcome    — 범위/조작 안내
//   Pathfind   — A*/Dijkstra/Greedy/JPS 비교 (ai/part06~07)
//   FlowField  — cost->integration->flow 벡터장 (ai/part10)
//   Steering   — Seek/Arrive/Wander/Pursue (ai/part11)
//   Boids      — Separation/Alignment/Cohesion (ai/part12)
//   Influence  — stamp/spread/decay 영향력 맵 (ai/part09)

#include "raylib.h"

#include "measure/frame_timer.h"
#include "scenes/boids_scene.h"
#include "scenes/flowfield_scene.h"
#include "scenes/influence_scene.h"
#include "scenes/iscene.h"
#include "scenes/pathfind_scene.h"
#include "scenes/steering_scene.h"
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
    InitWindow(kWindowW, kWindowH, "ai_lab");
    SetTargetFPS(60);

    std::vector<std::unique_ptr<ailab::IScene>> scenes;
    scenes.emplace_back(std::make_unique<ailab::WelcomeScene>());
    scenes.emplace_back(std::make_unique<ailab::PathfindScene>());
    scenes.emplace_back(std::make_unique<ailab::FlowFieldScene>());
    scenes.emplace_back(std::make_unique<ailab::SteeringScene>());
    scenes.emplace_back(std::make_unique<ailab::BoidsScene>());
    scenes.emplace_back(std::make_unique<ailab::InfluenceScene>());

    int activeIdx = 0;
    int prevIdx   = -1;
    ailab::FrameTimer timer;

    while (!WindowShouldClose()) {
        timer.BeginFrame();
        float dt = GetFrameTime();

        if (activeIdx != prevIdx) {
            if (prevIdx >= 0) scenes[(size_t)prevIdx]->OnExit();
            scenes[(size_t)activeIdx]->OnEnter();
            prevIdx = activeIdx;
        }

        int   screenW = GetScreenWidth();
        int   screenH = GetScreenHeight();
        auto  layout  = ailab::ComputeLayout(screenW, screenH, kPanelW);

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

        std::vector<std::string_view> labels;
        labels.reserve(scenes.size());
        for (const auto& s : scenes) labels.push_back(s->Name());
        int nextIdx = ailab::DrawTabBar(layout.tabBar, labels, activeIdx);

        ailab::DrawFooter(layout.footer, timer.SmoothedMs(), timer.Fps(),
                          scenes[(size_t)activeIdx]->Name());

        EndDrawing();

        activeIdx = nextIdx;
        timer.EndFrame();
    }

    if (prevIdx >= 0) scenes[(size_t)prevIdx]->OnExit();
    CloseWindow();
    return 0;
}
