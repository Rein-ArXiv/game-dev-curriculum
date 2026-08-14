#include "scenes/pathfind_scene.h"

#include "raylib.h"
#include "raygui.h"

#include <cstdio>

namespace ailab {

void PathfindScene::OnEnter() {
    if (grid_.Width() != gridDim_ || grid_.Height() != gridDim_) {
        grid_.Init(gridDim_, gridDim_);
    }
    needRestart_ = true;
}

void PathfindScene::Restart() {
    pf_.Reset(grid_, (Algo)algoIdx_, (Heur)heurIdx_);
    stepAcc_     = 0.0f;
    needRestart_ = false;
    pendingStep_ = false;
    pendingRun_  = false;
}

void PathfindScene::Update(float dt) {
    if (grid_.Width() != gridDim_) {
        grid_.Resize(gridDim_, gridDim_);
        needRestart_ = true;
    }
    // 마우스가 캔버스 위에 있을 때만 벽 편집 (패널 클릭과 충돌 방지).
    Vector2 m = GetMousePosition();
    bool overPanel = m.x > (float)(GetScreenWidth() - 280);
    if (!overPanel && grid_.HandleMouseEdit(EditMode::Walls)) {
        needRestart_ = true;
    }
    if (needRestart_) Restart();

    // 패널 버튼 요청 소비 — 탐색 진행은 이 지점에서만 일어난다.
    if (pendingRun_)  { pf_.RunToCompletion(grid_); pendingRun_ = false; }
    if (pendingStep_) { pf_.Step(grid_); pendingStep_ = false; }

    if (autoRun_ && !pf_.Stats().done) {
        stepAcc_ += stepRate_ * dt;
        int steps = (int)stepAcc_;
        stepAcc_ -= (float)steps;
        for (int i = 0; i < steps && pf_.Step(grid_); ++i) {}
    }
}

void PathfindScene::DrawWorld(int cx, int cy, int cw, int ch) {
    grid_.Layout(cx, cy, cw, ch);

    // open / closed 색칠.
    int n = grid_.Width() * grid_.Height();
    for (int i = 0; i < n; ++i) {
        int gx = i % grid_.Width(), gy = i / grid_.Width();
        if (!grid_.Walkable(gx, gy)) continue;
        auto st = pf_.State(i);
        if (st == Pathfinder::Open)
            grid_.DrawCellFill(gx, gy, Color{60, 110, 200, 150});
        else if (st == Pathfinder::Closed)
            grid_.DrawCellFill(gx, gy, Color{70, 80, 110, 150});
    }

    // 최종 경로.
    for (Cell c : pf_.Path())
        grid_.DrawCellFill(c, Color{240, 210, 70, 220});

    grid_.Draw();
}

void PathfindScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;
    const float W = 240.0f;
    auto row = [&](float h) { float r = (float)y; y += (int)h; return r; };

    GuiLabel(Rectangle{(float)(px + 8), row(22), W, 18}, "길찾기 비교");

    int prevAlgo = algoIdx_, prevHeur = heurIdx_;
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "Algorithm");
    GuiComboBox(Rectangle{(float)(px + 8), row(26), W, 22}, AlgoNames(), &algoIdx_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "Heuristic (Dijkstra 무시)");
    GuiComboBox(Rectangle{(float)(px + 8), row(26), W, 22}, HeurNames(), &heurIdx_);
    if (algoIdx_ != prevAlgo || heurIdx_ != prevHeur) needRestart_ = true;

    char buf[80];
    float dim = (float)gridDim_;
    std::snprintf(buf, sizeof(buf), "grid = %d x %d", gridDim_, gridDim_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &dim, 16.0f, 96.0f);
    gridDim_ = (int)dim;

    GuiCheckBox(Rectangle{(float)(px + 8), row(24), 16, 16}, " auto run", &autoRun_);

    std::snprintf(buf, sizeof(buf), "step/s = %.0f", stepRate_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &stepRate_, 1.0f, 2000.0f);

    // 버튼 행.
    float by = row(30);
    if (GuiButton(Rectangle{(float)(px + 8), by, 74, 24}, "Step"))
        pendingStep_ = true;
    if (GuiButton(Rectangle{(float)(px + 8 + 82), by, 74, 24}, "Run"))
        pendingRun_ = true;
    if (GuiButton(Rectangle{(float)(px + 8 + 164), by, 74, 24}, "Reset"))
        needRestart_ = true;

    by = row(30);
    if (GuiButton(Rectangle{(float)(px + 8), by, 116, 24}, "Random walls"))
        { grid_.RandomWalls(1337u + (unsigned)gridDim_, 0.25f); needRestart_ = true; }
    if (GuiButton(Rectangle{(float)(px + 8 + 124), by, 116, 24}, "Clear walls"))
        { grid_.ClearWalls(); needRestart_ = true; }

    // 측정값.
    y += 8;
    const PathStats& s = pf_.Stats();
    GuiLabel(Rectangle{(float)(px + 8), row(20), W, 18}, "측정");
    std::snprintf(buf, sizeof(buf), "expanded  = %d", s.expanded);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    std::snprintf(buf, sizeof(buf), "max open  = %d", s.maxOpen);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    std::snprintf(buf, sizeof(buf), "path cost = %.2f", s.pathCost);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    std::snprintf(buf, sizeof(buf), "path len  = %d", s.pathLen);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    std::snprintf(buf, sizeof(buf), "status    = %s",
                  !s.done ? "searching..." : (s.found ? "found" : "no path"));
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
}

}  // namespace ailab
