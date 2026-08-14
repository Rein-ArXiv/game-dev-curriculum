#include "scenes/influence_scene.h"

#include "model/ai_model.h"
#include "model/influence.h"
#include "raygui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ailab {

namespace {
constexpr double kInfluenceStepSeconds = 1.0 / 30.0;
}

void InfluenceScene::OnEnter() {
    if (grid_.Width() != gridDim_) grid_.Init(gridDim_, gridDim_);
    int n = grid_.Width() * grid_.Height();
    fField_.assign((size_t)n, 0.0f);
    eField_.assign((size_t)n, 0.0f);
    if (friendly_.empty() && enemy_.empty()) {
        friendly_.push_back({gridDim_ / 4, gridDim_ / 2});
        enemy_.push_back({3 * gridDim_ / 4, gridDim_ / 2});
    }
    stepAccumulator_ = 0.0;
}

void InfluenceScene::Step() {
    const int W = grid_.Width(), H = grid_.Height();
    const InfluenceParams params{decay_, spread_, iters_};
    const auto walkable = [&](int x, int y) { return grid_.Walkable(x, y); };

    // decay → stamp → spread. 두 필드는 서로를 읽지 않으므로 같은 순수 함수를
    // 각각 적용하면 된다 (model/influence.h).
    StepInfluenceField(W, H, fField_, friendly_, params, walkable);
    StepInfluenceField(W, H, eField_, enemy_, params, walkable);
}

void InfluenceScene::Update(float dt) {
    if (grid_.Width() != gridDim_) {
        grid_.Resize(gridDim_, gridDim_);
        friendly_.clear();
        enemy_.clear();
        OnEnter();
    }
    // 유닛 배치 (캔버스 위에서만).
    Vector2 m = GetMousePosition();
    bool overPanel = m.x > (float)(GetScreenWidth() - 280);
    Cell c;
    if (!overPanel && grid_.ScreenToCell(m, c)) {
        if (grid_.Walkable(c.x, c.y)) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))  friendly_.push_back(c);
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) enemy_.push_back(c);
        }
    }

    stepAccumulator_ += std::clamp((double)dt, 0.0, 0.25);
    while (stepAccumulator_ >= kInfluenceStepSeconds) {
        Step();
        stepAccumulator_ -= kInfluenceStepSeconds;
    }
}

void InfluenceScene::DrawWorld(int cx, int cy, int cw, int ch) {
    grid_.Layout(cx, cy, cw, ch);
    int W = grid_.Width(), H = grid_.Height();

    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int idx = grid_.Index(x, y);
            float f = fField_[(size_t)idx], e = eField_[(size_t)idx];
            const auto view = static_cast<InfluenceView>(
                std::clamp(viewMode_, 0, 2));
            const float val = CombineInfluence(f, e, view);

            if (viewMode_ == 0) {
                if (val > 0.01f)
                    grid_.DrawCellFill(x, y, Color{60, (unsigned char)(120 + 100 * std::min(1.0f, val)), 90,
                                                   (unsigned char)(40 + 180 * std::min(1.0f, val))});
                else if (val < -0.01f)
                    grid_.DrawCellFill(x, y, Color{(unsigned char)(140 + 90 * std::min(1.0f, -val)), 70, 70,
                                                   (unsigned char)(40 + 180 * std::min(1.0f, -val))});
            } else if (val > 0.01f) {
                unsigned char a = (unsigned char)(40 + 200 * std::min(1.0f, val));
                grid_.DrawCellFill(x, y, Color{220, 180, 60, a});
            }
        }

    grid_.Draw();

    // 유닛 마커.
    float r = std::max(2.5f, grid_.CellSize() * 0.3f);
    for (Cell c : friendly_) DrawCircleV(grid_.CellCenter(c), r, Color{90, 220, 130, 255});
    for (Cell c : enemy_)    DrawCircleV(grid_.CellCenter(c), r, Color{230, 90, 90, 255});
}

void InfluenceScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;
    const float W = 240.0f;
    auto row = [&](int h) { float r = (float)y; y += h; return r; };
    char buf[64];

    GuiLabel(Rectangle{(float)(px + 8), row(22), W, 18}, "Influence Map");
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "View");
    GuiComboBox(Rectangle{(float)(px + 8), row(26), W, 22},
                "Control;Tension;Vulnerability", &viewMode_);

    float dim = (float)gridDim_;
    std::snprintf(buf, sizeof(buf), "grid = %d x %d", gridDim_, gridDim_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &dim, 16.0f, 72.0f);
    gridDim_ = (int)dim;

    std::snprintf(buf, sizeof(buf), "decay / 30 Hz tick = %.2f", decay_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &decay_, 0.5f, 0.99f);

    std::snprintf(buf, sizeof(buf), "spread = %.2f", spread_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &spread_, 0.3f, 0.95f);

    float it = (float)iters_;
    std::snprintf(buf, sizeof(buf), "spread iters / tick = %d", iters_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &it, 1.0f, 6.0f);
    iters_ = (int)it;

    float by = row(32);
    if (GuiButton(Rectangle{(float)(px + 8), by, 116, 24}, "Clear units"))
        { friendly_.clear(); enemy_.clear(); }
    if (GuiButton(Rectangle{(float)(px + 8 + 124), by, 116, 24}, "Walls rnd")) {
        grid_.RandomWalls(55u + (unsigned)gridDim_, 0.12f);
        std::erase_if(friendly_, [&](Cell c) { return !grid_.Walkable(c.x, c.y); });
        std::erase_if(enemy_, [&](Cell c) { return !grid_.Walkable(c.x, c.y); });
    }

    y += 6;
    std::snprintf(buf, sizeof(buf), "friendly=%d  enemy=%d",
                  (int)friendly_.size(), (int)enemy_.size());
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "좌클릭=아군,  우클릭=적");
}

}  // namespace ailab
