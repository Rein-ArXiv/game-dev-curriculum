// physics_lab — physics 시리즈의 원리를 인터랙티브하게 측정·시각화하는 도구.
//
// 시간 모델: fixed dt accumulator.
//   매 프레임 GetFrameTime() 만큼 누적기에 더하고, kFixedDt 만큼씩 차감하며
//   Scene::Step(kFixedDt) 를 반복 호출. 적분기 비교 / 결정성 측정의 기반.
//   frameTime 이 kMaxAccum 을 초과하면 잘라낸다 — 한 프레임이 길어졌을 때
//   다음 프레임에서 100 번 substep 을 도는 spiral of death 방지.
//
// 등록 Scene (8 개):
//   Welcome          (Step 1) — Scene 골격 + 단일 강체 / 중력 / 바닥 충돌
//   Integrators      (Step 2, physics part01~02) — Euler / Semi-implicit / Verlet / RK4
//   Forces           (Step 3, physics part03 병진 한정) — 스프링·드래그·외력
//   BroadPhase       (Step 4, physics part05) — brute / SAP / Spatial Hash
//   NarrowPhase      (Step 5, physics part06~07) — SAT / GJK / EPA
//   Response         (Step 6, physics part08) — Penalty / Impulse / Sequential
//                     (법선 impulse 만 — 마찰 part09 는 미구현)
//   Constraints      (Step 7, physics part10~12) — distance constraint (PBD / SI)
//   Measure          (Step 8) — 에너지 / 안정성 / 결정성 측정

#include "raylib.h"

#include "measure/frame_timer.h"
#include "scenes/iscene.h"
#include "scenes/welcome_scene.h"
#include "scenes/integrators_scene.h"
#include "scenes/forces_scene.h"
#include "scenes/broad_phase_scene.h"
#include "scenes/narrow_phase_scene.h"
#include "scenes/response_scene.h"
#include "scenes/constraints_scene.h"
#include "scenes/measure_scene.h"
#include "ui/handle.h"
#include "ui/scene_chrome.h"

#include <memory>
#include <vector>

namespace {

constexpr int   kWindowW           = 1280;
constexpr int   kWindowH           = 720;
constexpr int   kPanelW            = 280;
constexpr float kFixedDt           = 1.0f / 120.0f;  // 8.33 ms — Step 2 의 기준 dt
constexpr float kMaxAccum          = 0.25f;          // 한 프레임 최대 누적
constexpr int   kMaxStepsPerFrame  = 8;              // per-frame substep 상한.
                                                       // 초과 시 accumulator 를
                                                       // 버려 시뮬이 wall-clock
                                                       // 보다 느려지더라도 *프레임
                                                       // 시간 폭발 (spiral) 회피*.

}  // namespace

int main() {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(kWindowW, kWindowH, "physics_lab");
    SetTargetFPS(60);

    std::vector<std::unique_ptr<physlab::IScene>> scenes;
    scenes.emplace_back(std::make_unique<physlab::WelcomeScene>());
    scenes.emplace_back(std::make_unique<physlab::IntegratorsScene>());
    scenes.emplace_back(std::make_unique<physlab::ForcesScene>());
    scenes.emplace_back(std::make_unique<physlab::BroadPhaseScene>());
    scenes.emplace_back(std::make_unique<physlab::NarrowPhaseScene>());
    scenes.emplace_back(std::make_unique<physlab::ResponseScene>());
    scenes.emplace_back(std::make_unique<physlab::ConstraintsScene>());
    scenes.emplace_back(std::make_unique<physlab::MeasureScene>());

    int   activeIdx   = 0;
    int   prevIdx     = -1;
    float accumulator = 0.0f;

    physlab::FrameTimer timer;

    while (!WindowShouldClose()) {
        timer.BeginFrame();

        // Scene 전환 — 입력 / 시뮬 / 그리기 전에 한 번.
        if (activeIdx != prevIdx) {
            if (prevIdx >= 0) scenes[(size_t)prevIdx]->OnExit();
            scenes[(size_t)activeIdx]->OnEnter();
            prevIdx = activeIdx;
            accumulator = 0.0f;  // 새 Scene 으로 들어가면 누적 초기화.
        }

        // 1) 입력 — 프레임당 1 번. DragHandle 의 한 핸들 정책도 여기서.
        physlab::DragHandle::BeginFrame();
        scenes[(size_t)activeIdx]->HandleInput();
        physlab::DragHandle::EndFrame();

        // 2) 시뮬 — 누적 시간만큼 fixed dt 로 쪼개 돌림.
        float frameTime = GetFrameTime();
        if (frameTime > kMaxAccum) frameTime = kMaxAccum;
        accumulator += frameTime;
        int substepCount = 0;
        while (accumulator >= kFixedDt && substepCount < kMaxStepsPerFrame) {
            scenes[(size_t)activeIdx]->Step(kFixedDt);
            accumulator -= kFixedDt;
            ++substepCount;
        }
        // 상한 도달 — 남은 누적은 *버린다*. 시뮬이 wall-clock 보다 느려지더라도
        // 한 프레임이 30 substep 으로 폭발하는 spiral 을 막는다.
        if (substepCount >= kMaxStepsPerFrame) accumulator = 0.0f;

        // 3) 그리기.
        BeginDrawing();
        ClearBackground(Color{12, 14, 18, 255});

        auto layout = physlab::ComputeLayout(kWindowW, kWindowH, kPanelW);

        physlab::DrawCanvasBackground(layout.canvas);
        physlab::DrawPanelBackground(layout.panel);

        scenes[(size_t)activeIdx]->DrawWorld(
            (int)layout.canvas.x, (int)layout.canvas.y,
            (int)layout.canvas.width, (int)layout.canvas.height);

        scenes[(size_t)activeIdx]->DrawPanel(
            (int)layout.panel.x, (int)layout.panel.y,
            (int)layout.panel.width, (int)layout.panel.height);

        std::vector<std::string_view> labels;
        labels.reserve(scenes.size());
        for (const auto& s : scenes) labels.push_back(s->Name());
        int nextIdx = physlab::DrawTabBar(layout.tabBar, labels, activeIdx);

        physlab::DrawFooter(layout.footer,
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
