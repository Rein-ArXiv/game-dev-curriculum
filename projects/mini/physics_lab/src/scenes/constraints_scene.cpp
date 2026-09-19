#include "scenes/constraints_scene.h"

#include "core/physics_core.h"
#include "raygui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace physlab {

namespace {

constexpr int kGraphCapacity = 600;

inline float Dot(Vector2 a, Vector2 b) { return a.x*b.x + a.y*b.y; }
inline Vector2 Sub(Vector2 a, Vector2 b) { return {a.x-b.x, a.y-b.y}; }

}  // namespace

ConstraintsScene::ConstraintsScene() {
    stretchHist_.Reset(kGraphCapacity);
    msHist_.Reset(kGraphCapacity);
}

void ConstraintsScene::BuildChain(int n, float segmentLen) {
    particles_.clear();
    links_.clear();
    if (n < 2) n = 2;
    N_ = n;   // 클램프를 N_ 에도 반영 — 아니면 Step 이 매 스텝 chain 을 다시 짓는다.

    Vector2 start = {(float)(lastCanvasX_ + lastCanvasW_ / 4),
                      (float)(lastCanvasY_ + 60)};
    if (lastCanvasW_ <= 0) start = {200, 100};

    for (int i = 0; i < n; ++i) {
        Particle p;
        p.pos = {start.x + i * segmentLen, start.y};
        p.prevPos = p.pos;
        p.vel = {0, 0};
        p.invMass = (i == 0) ? 0.0f : 1.0f;   // 첫 mass 는 anchor (정지)
        particles_.push_back(p);
    }
    for (int i = 0; i < n - 1; ++i) {
        links_.push_back({i, i + 1, segmentLen, 0.0f});
    }
    anchorHandle_.SetPosition(particles_[0].pos);
}

void ConstraintsScene::OnEnter() {
    initialized_  = false;
    stretchHist_.Clear();
    msHist_.Clear();
}

void ConstraintsScene::HandleInput() {
    anchorHandle_.Update();
}

void ConstraintsScene::IntegrateGravity(float dt) {
    for (auto& p : particles_) {
        if (p.invMass <= 0) continue;
        p.vel.y += gravity_ * dt;
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
    }
}

void ConstraintsScene::SolvePBD() {
    for (int iter = 0; iter < iterations_; ++iter) {
        for (const auto& l : links_) {
            Particle& a = particles_[(size_t)l.a];
            Particle& b = particles_[(size_t)l.b];
            core::ProjectDistance(a.pos.x, a.pos.y, a.invMass,
                                  b.pos.x, b.pos.y, b.invMass, l.rest);
        }
    }
}

void ConstraintsScene::SolveSI(float dt) {
    // Sequential Impulse — 각 distance constraint 를 velocity 수준 등식
    // (v_b - v_a) · n = -β/dt * (|x_b - x_a| - rest)
    // 로 풀고 *iterations* 만큼 반복. 각 link 의 accumulated lambda 는 부호
    // 제한 없음 (거리 *유지* 라 양/음 모두 허용 — 압축도 늘어짐도 막음).
    for (auto& l : links_) l.lambda = 0.0f;

    for (int iter = 0; iter < iterations_; ++iter) {
        for (auto& l : links_) {
            Particle& a = particles_[(size_t)l.a];
            Particle& b = particles_[(size_t)l.b];
            Vector2 d   = Sub(b.pos, a.pos);
            float   len = std::sqrt(d.x*d.x + d.y*d.y);
            if (len < 1e-6f) continue;
            Vector2 n = {d.x / len, d.y / len};

            float vRel = Dot(Sub(b.vel, a.vel), n);
            float bias = -(baumgarte_ / dt) * (len - l.rest);
            float wsum = a.invMass + b.invMass;
            if (wsum < 1e-9f) continue;
            float dLambda = -(vRel - bias) / wsum;
            l.lambda += dLambda;
            a.vel.x -= dLambda * a.invMass * n.x;
            a.vel.y -= dLambda * a.invMass * n.y;
            b.vel.x += dLambda * b.invMass * n.x;
            b.vel.y += dLambda * b.invMass * n.y;
        }
    }
}

void ConstraintsScene::Step(float dt) {
    if (lastCanvasW_ <= 0) return;
    if (!initialized_) {
        BuildChain(N_, segment_);
        initialized_ = true;
    }
    if ((int)particles_.size() != N_) BuildChain(N_, segment_);

    // anchor 위치 갱신 — DragHandle 이 끄는 동안.
    particles_[0].pos = anchorHandle_.Position();
    particles_[0].vel = {0, 0};

    for (auto& p : particles_) p.prevPos = p.pos;

    auto t0 = std::chrono::high_resolution_clock::now();

    if (solver_ == ConstraintSolver::PBD) {
        // PBD: predict pos → solve position constraints → derive velocity.
        for (auto& p : particles_) {
            if (p.invMass <= 0) continue;
            p.vel.y += gravity_ * dt;
            p.pos.x += p.vel.x * dt;
            p.pos.y += p.vel.y * dt;
        }
        SolvePBD();
        float invDt = 1.0f / dt;
        for (size_t i = 0; i < particles_.size(); ++i) {
            if (particles_[i].invMass <= 0) {
                particles_[i].vel = {0, 0};
                continue;
            }
            particles_[i].vel.x = (particles_[i].pos.x
                                      - particles_[i].prevPos.x) * invDt;
            particles_[i].vel.y = (particles_[i].pos.y
                                      - particles_[i].prevPos.y) * invDt;
        }
    } else {
        // SI: gravity 적분 → velocity 제약 풀기 → pos 적분.
        for (auto& p : particles_) {
            if (p.invMass <= 0) continue;
            p.vel.y += gravity_ * dt;
        }
        SolveSI(dt);
        for (auto& p : particles_) {
            if (p.invMass <= 0) continue;
            p.pos.x += p.vel.x * dt;
            p.pos.y += p.vel.y * dt;
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    lastMs_ = std::chrono::duration<double, std::milli>(t1 - t0).count();

    float sum = 0, mx = 0;
    for (const auto& l : links_) {
        Vector2 d = Sub(particles_[(size_t)l.b].pos,
                         particles_[(size_t)l.a].pos);
        float len = std::sqrt(d.x*d.x + d.y*d.y);
        float s   = std::fabs(len - l.rest) / l.rest;
        sum += s;
        mx = std::max(mx, s);
    }
    lastAvgStretch_ = links_.empty() ? 0 : sum / (float)links_.size();
    lastMaxStretch_ = mx;
    stretchHist_.Push(lastAvgStretch_);
    msHist_.Push((float)lastMs_);
}

void ConstraintsScene::DrawWorld(int cx, int cy, int cw, int ch) {
    lastCanvasX_ = cx; lastCanvasY_ = cy;
    lastCanvasW_ = cw; lastCanvasH_ = ch;

    DrawRectangle(cx, cy, cw, ch, Color{20, 22, 28, 255});
    DrawRectangleLines(cx, cy, cw, ch, Color{60, 62, 70, 255});

    Color algoCol = (solver_ == ConstraintSolver::PBD)
                       ? Color{120, 230, 130, 255}
                       : Color{120, 200, 255, 255};

    // 링크.
    for (const auto& l : links_) {
        const auto& a = particles_[(size_t)l.a];
        const auto& b = particles_[(size_t)l.b];
        Vector2 d = Sub(b.pos, a.pos);
        float len = std::sqrt(d.x*d.x + d.y*d.y);
        float stretch = std::fabs(len - l.rest) / l.rest;
        Color c = algoCol;
        // 늘어남이 클수록 빨강.
        unsigned char redness = (unsigned char)std::min(255.0f, stretch * 800.0f);
        c.r = (unsigned char)std::min(255, (int)c.r + redness);
        DrawLineEx(a.pos, b.pos, 2.5f, c);
    }

    // mass.
    for (size_t i = 0; i < particles_.size(); ++i) {
        const auto& p = particles_[i];
        Color c = (p.invMass <= 0) ? Color{255, 90, 90, 255} : algoCol;
        DrawCircleV(p.pos, 5.0f, c);
    }

    anchorHandle_.Draw(Color{255, 90, 90, 200}, "anchor");

    const char* solverName =
        (solver_ == ConstraintSolver::PBD) ? "PBD" : "Sequential Impulse";
    DrawText(TextFormat("Step 7: Constraints = %s   N=%d   iter=%d",
                          solverName, N_, iterations_),
              cx + 10, cy + 10, 14, Color{200, 200, 200, 255});

    Rectangle gr1 = {(float)(cx + cw - 320), (float)(cy + 40), 300.0f, 110.0f};
    Rectangle gr2 = {(float)(cx + cw - 320), (float)(cy + 160), 300.0f, 110.0f};
    GraphInput in1[1] = {{&stretchHist_, algoCol, "avg stretch"}};
    DrawLineGraph(gr1, in1, 1, "Avg link stretch (|len-rest|/rest)", 0, 1, true);
    GraphInput in2[1] = {{&msHist_, algoCol, "solver ms"}};
    DrawLineGraph(gr2, in2, 1, "Solver time (ms)", 0, 1, true);
}

void ConstraintsScene::DrawPanel(int panelX, int panelY, int panelW, int panelH) {
    (void)panelW; (void)panelH;
    int  y = panelY + 8;
    char buf[160];

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, "Constraints");
    y += 24;

    const char* names[2] = {"PBD", "Sequential Impulse"};
    for (int i = 0; i < 2; ++i) {
        Rectangle r = {(float)(panelX + 8), (float)y, 240, 22};
        bool selected = ((int)solver_ == i);
        if (selected) DrawRectangleRec(r, Color{60, 100, 160, 255});
        if (GuiButton(r, names[i])) {
            solver_ = (ConstraintSolver)i;
            BuildChain(N_, segment_);
            stretchHist_.Clear();
            msHist_.Clear();
        }
        y += 26;
    }
    y += 6;

    std::snprintf(buf, sizeof(buf), "chain length: %d", N_);
    float fn = (float)N_;
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &fn, 2.0f, 50.0f);
    int nn = (int)fn;
    if (nn != N_) {
        N_ = nn;
        BuildChain(N_, segment_);
        stretchHist_.Clear(); msHist_.Clear();
    }
    y += 22;

    std::snprintf(buf, sizeof(buf), "segment len: %.0f px", segment_);
    float sPrev = segment_;
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &segment_, 10.0f, 80.0f);
    if (segment_ != sPrev) {
        for (auto& l : links_) l.rest = segment_;
    }
    y += 22;

    std::snprintf(buf, sizeof(buf), "iterations: %d", iterations_);
    float fi = (float)iterations_;
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &fi, 1.0f, 50.0f);
    iterations_ = (int)fi;
    y += 22;

    std::snprintf(buf, sizeof(buf), "Baumgarte (SI): %.2f", baumgarte_);
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &baumgarte_, 0.0f, 1.0f);
    y += 28;

    if (GuiButton(Rectangle{(float)(panelX + 8), (float)y, 100, 22},
                   "Reset")) {
        BuildChain(N_, segment_);
        stretchHist_.Clear(); msHist_.Clear();
    }
    y += 28;

    std::snprintf(buf, sizeof(buf), "avg stretch: %.3f", lastAvgStretch_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 18;
    std::snprintf(buf, sizeof(buf), "max stretch: %.3f", lastMaxStretch_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 18;
    std::snprintf(buf, sizeof(buf), "solver: %.3f ms", lastMs_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 24;

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "PBD low iter: chain stretches.");
    y += 16;
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "SI low Baumgarte: pos drifts.");
    y += 16;
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "Drag the anchor handle around.");
}

}  // namespace physlab
