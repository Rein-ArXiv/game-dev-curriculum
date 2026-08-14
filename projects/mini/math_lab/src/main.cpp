// math_lab — math 시리즈의 알고리즘을 인터랙티브하게 시각화하는 도구.
//
// 등록 Scene (Step 1~8):
//   Welcome          (Step 1) — Scene 골격 + DragHandle 검증
//   Rotation         (Step 2, math part04~05)
//   Interpolation    (Step 3, math part07~08)
//   Noise            (Step 4, math part09~10)
//   Collision        (Step 5, math part11~13)
//   Spatial / BVH    (Step 6, math part15~16)
//   SDF / RayMarch   (Step 7, math part14)
//   Projection /
//     Stability      (Step 8, math part18 + part20)

#include "raylib.h"

#include "measure/frame_timer.h"
#include "scenes/collision_scene.h"
#include "scenes/interpolation_scene.h"
#include "scenes/iscene.h"
#include "scenes/noise_scene.h"
#include "scenes/projection_scene.h"
#include "scenes/rotation_scene.h"
#include "scenes/sdf_scene.h"
#include "scenes/spatial_scene.h"
#include "scenes/welcome_scene.h"
#include "ui/handle.h"
#include "ui/scene_chrome.h"

#include <memory>
#include <vector>

namespace {

constexpr int kWindowW = 1280;
constexpr int kWindowH = 720;
constexpr int kPanelW  = 280;

}  // namespace

int main() {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(kWindowW, kWindowH, "math_lab");
    SetTargetFPS(60);

    // 등록된 Scene 들. Step 1~8의 결과가 모두 연결된다.
    std::vector<std::unique_ptr<mathlab::IScene>> scenes;
    scenes.emplace_back(std::make_unique<mathlab::WelcomeScene>());
    scenes.emplace_back(std::make_unique<mathlab::RotationScene>());
    scenes.emplace_back(std::make_unique<mathlab::InterpolationScene>());
    scenes.emplace_back(std::make_unique<mathlab::NoiseScene>());
    scenes.emplace_back(std::make_unique<mathlab::CollisionScene>());
    scenes.emplace_back(std::make_unique<mathlab::SpatialScene>());
    scenes.emplace_back(std::make_unique<mathlab::SdfScene>());
    scenes.emplace_back(std::make_unique<mathlab::ProjectionScene>());

    int activeIdx = 0;
    int prevIdx   = -1;  // OnEnter / OnExit 트리거용

    mathlab::FrameTimer timer;

    while (!WindowShouldClose()) {
        timer.BeginFrame();
        float dt = GetFrameTime();

        // Scene 전환 처리. 탭 클릭은 *이전 프레임 끝* 에 활성화돼서
        // 이번 프레임의 Update / Draw 가 새 Scene 에서 실행된다.
        if (activeIdx != prevIdx) {
            if (prevIdx >= 0) scenes[(size_t)prevIdx]->OnExit();
            scenes[(size_t)activeIdx]->OnEnter();
            prevIdx = activeIdx;
        }

        // DragHandle 의 프레임 시작 — 클릭 후보 초기화.
        mathlab::DragHandle::BeginFrame();
        scenes[(size_t)activeIdx]->Update(dt);
        // 후보 중 가장 가까운 한 개만 capture.
        mathlab::DragHandle::EndFrame();

        BeginDrawing();
        ClearBackground(Color{12, 14, 18, 255});

        auto layout = mathlab::ComputeLayout(kWindowW, kWindowH, kPanelW);

        mathlab::DrawCanvasBackground(layout.canvas);
        mathlab::DrawPanelBackground(layout.panel);

        scenes[(size_t)activeIdx]->DrawWorld(
            (int)layout.canvas.x, (int)layout.canvas.y,
            (int)layout.canvas.width, (int)layout.canvas.height);

        scenes[(size_t)activeIdx]->DrawPanel(
            (int)layout.panel.x, (int)layout.panel.y,
            (int)layout.panel.width, (int)layout.panel.height);

        // 탭 라벨 수집. 탭 클릭은 다음 프레임에 반영 (현재 프레임은 footer 도
        // 현재 Scene 으로 일관되게 표시).
        std::vector<std::string_view> labels;
        labels.reserve(scenes.size());
        for (const auto& s : scenes) labels.push_back(s->Name());
        int nextIdx = mathlab::DrawTabBar(layout.tabBar, labels, activeIdx);

        mathlab::DrawFooter(layout.footer,
                              timer.SmoothedMs(),
                              timer.Fps(),
                              scenes[(size_t)activeIdx]->Name());

        EndDrawing();

        activeIdx = nextIdx;
        timer.EndFrame();
    }

    if (prevIdx >= 0) scenes[(size_t)prevIdx]->OnExit();
    CloseWindow();
    return 0;
}
