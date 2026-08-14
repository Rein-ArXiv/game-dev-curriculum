#include "scenes/flowfield_scene.h"

#include "model/ai_model.h"
#include "model/flow_field.h"
#include "raygui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>

namespace ailab {

namespace {
constexpr float kInf = std::numeric_limits<float>::infinity();

Vector2 Norm(Vector2 v) {
    float l = std::sqrt(v.x * v.x + v.y * v.y);
    return (l > 1e-5f) ? Vector2{v.x / l, v.y / l} : Vector2{0, 0};
}
}  // namespace

void FlowFieldScene::OnEnter() {
    if (grid_.Width() != gridDim_) grid_.Init(gridDim_, gridDim_);
    dirty_ = true;
    layoutReady_ = false;
    agents_.clear();
}

void FlowFieldScene::Rebuild() {
    int W = grid_.Width(), H = grid_.Height(), n = W * H;
    flow_.assign((size_t)n, Vector2{0, 0});

    // integration field: goal 에서 Dijkstra (8방향, cost 1 또는 sqrt(2)).
    Cell goal = grid_.Goal();
    integration_ = BuildIntegrationField(
        W, H, [&](int nx, int ny) { return grid_.Walkable(nx, ny); }, goal.x, goal.y);

    // flow: edge cost + 이웃 integration 비용이 가장 작은 방향.
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int idx = grid_.Index(x, y);
            if (integration_[(size_t)idx] == kInf) continue;
            const GridDirection step = SelectFlowDirection(
                x, y,
                [&](int nx, int ny) { return grid_.Walkable(nx, ny); },
                [&](int nx, int ny) {
                    return integration_[(size_t)grid_.Index(nx, ny)];
                });
            Vector2 dir{(float)step.dx, (float)step.dy};
            flow_[(size_t)idx] = Norm(dir);
        }
    dirty_ = false;
}

void FlowFieldScene::RespawnAgents() {
    agents_.clear();
    std::mt19937 rng(7u);
    std::vector<Cell> candidates;
    for (int y = 0; y < grid_.Height(); ++y) {
        for (int x = 0; x < grid_.Width(); ++x) {
            const int idx = grid_.Index(x, y);
            if (grid_.Walkable(x, y) &&
                std::isfinite(integration_[(size_t)idx]) &&
                Cell{x, y} != grid_.Goal()) {
                candidates.push_back({x, y});
            }
        }
    }
    if (candidates.empty()) candidates.push_back(grid_.Goal());

    std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
    for (int i = 0; i < agentCount_; ++i) {
        const Cell c = candidates[pick(rng)];
        agents_.push_back({grid_.CellCenter(c), {0, 0}});
    }
}

Cell FlowFieldScene::SafeSpawnCell() const {
    const Cell start = grid_.Start();
    if (grid_.Walkable(start.x, start.y) &&
        std::isfinite(integration_[(size_t)grid_.Index(start.x, start.y)]) &&
        start != grid_.Goal()) {
        return start;
    }
    for (int y = 0; y < grid_.Height(); ++y) {
        for (int x = 0; x < grid_.Width(); ++x) {
            if (grid_.Walkable(x, y) &&
                std::isfinite(integration_[(size_t)grid_.Index(x, y)]) &&
                Cell{x, y} != grid_.Goal()) {
                return {x, y};
            }
        }
    }
    return grid_.Goal();
}

void FlowFieldScene::Update(float dt) {
    if (grid_.Width() != gridDim_) {
        grid_.Resize(gridDim_, gridDim_);
        dirty_ = true;
        layoutReady_ = false;
        agents_.clear();
    }
    // 필드 구축은 화면 레이아웃과 무관하므로 첫 프레임부터 Update 에서 끝낸다.
    if (dirty_) Rebuild();
    if (!layoutReady_) return;

    Vector2 m = GetMousePosition();
    bool overPanel = m.x > (float)(GetScreenWidth() - 280);
    if (!overPanel && grid_.HandleMouseEdit(EditMode::Walls)) dirty_ = true;
    if (dirty_) Rebuild();
    if ((int)agents_.size() != agentCount_) RespawnAgents();

    // 에이전트: flow 추종 + 간단한 분리.
    float cell = grid_.CellSize();
    float maxSpeed = cell * 6.0f;
    for (auto& a : agents_) {
        Cell c;
        if (!grid_.ScreenToCell(a.pos, c) || !grid_.Walkable(c.x, c.y) ||
            !std::isfinite(integration_[(size_t)grid_.Index(c.x, c.y)])) {
            a.pos = grid_.CellCenter(SafeSpawnCell());
            a.vel = {0, 0};
            continue;
        }
        Vector2 f = flow_[(size_t)grid_.Index(c.x, c.y)];
        Vector2 desired{f.x * maxSpeed, f.y * maxSpeed};

        // 분리: 가까운 이웃에서 밀어냄 (O(n^2) — 에이전트 수가 적당해 허용).
        Vector2 sep{0, 0};
        for (const auto& o : agents_) {
            float ddx = a.pos.x - o.pos.x, ddy = a.pos.y - o.pos.y;
            float d2 = ddx * ddx + ddy * ddy;
            if (d2 > 1e-4f && d2 < cell * cell) {
                float d = std::sqrt(d2);
                sep.x += ddx / d; sep.y += ddy / d;
            }
        }
        desired.x += sep.x * maxSpeed * sepWeight_;
        desired.y += sep.y * maxSpeed * sepWeight_;

        a.vel.x += (desired.x - a.vel.x) * std::min(1.0f, dt * 6.0f);
        a.vel.y += (desired.y - a.vel.y) * std::min(1.0f, dt * 6.0f);
        a.pos.x += a.vel.x * dt;
        a.pos.y += a.vel.y * dt;

        // goal 도달 시 무작위 재배치 (지속적 흐름 관찰).
        if (grid_.ScreenToCell(a.pos, c) && c == grid_.Goal()) {
            a.pos = grid_.CellCenter(SafeSpawnCell());
            a.vel = {0, 0};
        } else if (!grid_.ScreenToCell(a.pos, c) || !grid_.Walkable(c.x, c.y)) {
            a.pos = grid_.CellCenter(SafeSpawnCell());
            a.vel = {0, 0};
        }
    }
}

void FlowFieldScene::DrawWorld(int cx, int cy, int cw, int ch) {
    grid_.Layout(cx, cy, cw, ch);
    layoutReady_ = true;
    float cell = grid_.CellSize();

    // integration 히트맵 (도달 가능 셀의 거리).
    float maxV = 1.0f;
    for (float v : integration_) if (v != kInf && v > maxV) maxV = v;
    for (int y = 0; y < grid_.Height(); ++y)
        for (int x = 0; x < grid_.Width(); ++x) {
            float v = integration_[(size_t)grid_.Index(x, y)];
            if (v == kInf || !grid_.Walkable(x, y)) continue;
            float t = v / maxV;
            grid_.DrawCellFill(x, y, Color{(unsigned char)(40 + 60 * t),
                                           (unsigned char)(60 + 80 * (1 - t)),
                                           (unsigned char)(120 * (1 - t)), 90});
        }

    // flow 화살표.
    if (showArrows_ && cell >= 10.0f) {
        for (int y = 0; y < grid_.Height(); ++y)
            for (int x = 0; x < grid_.Width(); ++x) {
                Vector2 f = flow_[(size_t)grid_.Index(x, y)];
                if (f.x == 0 && f.y == 0) continue;
                Vector2 ctr = grid_.CellCenter(x, y);
                float len = cell * 0.35f;
                Vector2 tip{ctr.x + f.x * len, ctr.y + f.y * len};
                DrawLineV(ctr, tip, Color{150, 160, 180, 160});
                DrawCircleV(tip, 1.5f, Color{200, 210, 230, 200});
            }
    }

    grid_.Draw();

    // 에이전트.
    for (const auto& a : agents_)
        DrawCircleV(a.pos, std::max(2.0f, cell * 0.18f), Color{240, 200, 80, 230});
}

void FlowFieldScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;
    const float W = 240.0f;
    auto row = [&](int h) { float r = (float)y; y += h; return r; };

    GuiLabel(Rectangle{(float)(px + 8), row(22), W, 18}, "Flow Field");

    char buf[64];
    float dim = (float)gridDim_;
    std::snprintf(buf, sizeof(buf), "grid = %d x %d", gridDim_, gridDim_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &dim, 16.0f, 80.0f);
    gridDim_ = (int)dim;

    float ac = (float)agentCount_;
    std::snprintf(buf, sizeof(buf), "agents = %d", agentCount_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &ac, 0.0f, 1000.0f);
    agentCount_ = (int)ac;

    std::snprintf(buf, sizeof(buf), "separation = %.2f", sepWeight_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &sepWeight_, 0.0f, 2.0f);

    GuiCheckBox(Rectangle{(float)(px + 8), row(24), 16, 16}, " show arrows", &showArrows_);

    float by = row(32);
    if (GuiButton(Rectangle{(float)(px + 8), by, 116, 24}, "Random walls"))
        { grid_.RandomWalls(909u + (unsigned)gridDim_, 0.22f); dirty_ = true; }
    if (GuiButton(Rectangle{(float)(px + 8 + 124), by, 116, 24}, "Clear walls"))
        { grid_.ClearWalls(); dirty_ = true; }
    if (GuiButton(Rectangle{(float)(px + 8), row(32), W, 24}, "Respawn agents"))
        RespawnAgents();

    y += 6;
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16},
             "좌클릭=벽,  우클릭=지움. goal 이동은 맵 편집.");
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16},
             "필드 구축 비용은 에이전트 수와 무관.");
}

}  // namespace ailab
