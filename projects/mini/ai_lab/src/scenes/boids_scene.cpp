#include "scenes/boids_scene.h"

#include "model/ai_model.h"
#include "raygui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace ailab {

namespace {
Vector2 Add(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
Vector2 Sub(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
Vector2 Mul(Vector2 a, float s)   { return {a.x * s, a.y * s}; }
float   Len(Vector2 v)            { return std::sqrt(v.x * v.x + v.y * v.y); }
Vector2 Norm(Vector2 v) { float l = Len(v); return (l > 1e-5f) ? Mul(v, 1.0f / l) : Vector2{0, 0}; }
}  // namespace

// 실제 스폰은 캔버스 크기를 아는 Update 에서 한다. Draw 호출 횟수가
// 시뮬레이션 상태를 바꾸지 않도록 여기서는 비우기만 한다.
void BoidsScene::OnEnter() { boids_.clear(); }

void BoidsScene::Respawn() {
    boids_.clear();
    std::mt19937 rng(42u);
    float w = canvas_.width > 0 ? canvas_.width : 800.0f;
    float h = canvas_.height > 0 ? canvas_.height : 600.0f;
    std::uniform_real_distribution<float> rx(canvas_.x, canvas_.x + w);
    std::uniform_real_distribution<float> ry(canvas_.y, canvas_.y + h);
    std::uniform_real_distribution<float> rv(-1.0f, 1.0f);
    for (int i = 0; i < count_; ++i)
        boids_.push_back({{rx(rng), ry(rng)}, Mul(Norm({rv(rng), rv(rng)}), maxSpeed_)});
}

void BoidsScene::Update(float dt) {
    if (canvas_.width <= 0 || dt <= 0.0f) return;
    if ((int)boids_.size() != count_) Respawn();

    // 공간 해시 재구축 (셀 크기 = perception).
    float cell = std::max(8.0f, perception_);
    hash_.clear();
    for (int i = 0; i < (int)boids_.size(); ++i) {
        int cx = (int)std::floor(boids_[(size_t)i].pos.x / cell);
        int cy = (int)std::floor(boids_[(size_t)i].pos.y / cell);
        hash_[HashCell(cx, cy)].push_back(i);
    }

    float r2 = perception_ * perception_;
    float sepR2 = (perception_ * 0.5f) * (perception_ * 0.5f);
    std::vector<Vector2> newVel(boids_.size());

    for (int i = 0; i < (int)boids_.size(); ++i) {
        const Boid& b = boids_[(size_t)i];
        int cx = (int)std::floor(b.pos.x / cell);
        int cy = (int)std::floor(b.pos.y / cell);

        Vector2 sep{0, 0}, aliSum{0, 0}, cohSum{0, 0};
        int aliCount = 0, cohCount = 0;

        for (int oy = -1; oy <= 1; ++oy)
            for (int ox = -1; ox <= 1; ++ox) {
                auto it = hash_.find(HashCell(cx + ox, cy + oy));
                if (it == hash_.end()) continue;
                for (int j : it->second) {
                    if (j == i) continue;
                    const Boid& o = boids_[(size_t)j];
                    Vector2 d = Sub(b.pos, o.pos);
                    float d2 = d.x * d.x + d.y * d.y;
                    if (d2 > r2 || d2 < 1e-6f) continue;
                    // alignment / cohesion: 지각 반경 전체.
                    aliSum = Add(aliSum, o.vel); ++aliCount;
                    cohSum = Add(cohSum, o.pos); ++cohCount;
                    // separation: 더 가까운 이웃만, 멀어지는 단위 벡터의 합.
                    if (d2 < sepR2) sep = Add(sep, Mul(d, 1.0f / std::sqrt(d2)));
                }
            }

        Vector2 acc{0, 0};
        if (Len(sep) > 0) {
            Vector2 desired = Mul(Norm(sep), maxSpeed_);
            acc = Add(acc, Mul(Sub(desired, b.vel), wSep_));
        }
        if (aliCount > 0) {
            Vector2 desired = Mul(Norm(Mul(aliSum, 1.0f / aliCount)), maxSpeed_);
            acc = Add(acc, Mul(Sub(desired, b.vel), wAli_));
        }
        if (cohCount > 0) {
            Vector2 center = Mul(cohSum, 1.0f / cohCount);
            Vector2 desired = Mul(Norm(Sub(center, b.pos)), maxSpeed_);
            acc = Add(acc, Mul(Sub(desired, b.vel), wCoh_));
        }

        Vector2 v = Add(b.vel, Mul(acc, dt));
        const auto limited =
            LimitSpeed({v.x, v.y}, maxSpeed_ * 0.3f, maxSpeed_);
        v = {limited.x, limited.y};
        newVel[(size_t)i] = v;
    }

    // 적용 + 반사 경계. 이웃 거리와 같은 비주기 공간을 사용한다.
    for (int i = 0; i < (int)boids_.size(); ++i) {
        Boid& b = boids_[(size_t)i];
        b.vel = newVel[(size_t)i];
        b.pos = Add(b.pos, Mul(b.vel, dt));
        ReflectAxis(b.pos.x, b.vel.x, canvas_.x, canvas_.x + canvas_.width);
        ReflectAxis(b.pos.y, b.vel.y, canvas_.y, canvas_.y + canvas_.height);
    }
}

void BoidsScene::DrawWorld(int cx, int cy, int cw, int ch) {
    canvas_ = {(float)cx, (float)cy, (float)cw, (float)ch};

    for (const Boid& b : boids_) {
        Vector2 fwd = Norm(b.vel);
        if (Len(fwd) < 1e-4f) fwd = {1, 0};
        Vector2 right{-fwd.y, fwd.x};
        Vector2 a = Add(b.pos, Mul(fwd, 6.0f));
        Vector2 p2 = Add(Sub(b.pos, Mul(fwd, 4.0f)), Mul(right, 3.0f));
        Vector2 p3 = Sub(Sub(b.pos, Mul(fwd, 4.0f)), Mul(right, 3.0f));
        DrawTriangle(a, p2, p3, Color{120, 200, 235, 220});
    }
}

void BoidsScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;
    const float W = 240.0f;
    auto row = [&](int h) { float r = (float)y; y += h; return r; };
    char buf[64];

    GuiLabel(Rectangle{(float)(px + 8), row(22), W, 18}, "Boids / Flocking");

    float c = (float)count_;
    std::snprintf(buf, sizeof(buf), "count = %d", count_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &c, 10.0f, 2000.0f);
    count_ = (int)c;

    std::snprintf(buf, sizeof(buf), "perception = %.0f", perception_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &perception_, 12.0f, 80.0f);

    std::snprintf(buf, sizeof(buf), "separation = %.2f", wSep_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &wSep_, 0.0f, 4.0f);

    std::snprintf(buf, sizeof(buf), "alignment = %.2f", wAli_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &wAli_, 0.0f, 4.0f);

    std::snprintf(buf, sizeof(buf), "cohesion = %.2f", wCoh_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &wCoh_, 0.0f, 4.0f);

    std::snprintf(buf, sizeof(buf), "max speed = %.0f", maxSpeed_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr,
              &maxSpeed_, 40.0f, 320.0f);

    if (GuiButton(Rectangle{(float)(px + 8), row(32), W, 24}, "Respawn"))
        Respawn();

    y += 6;
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16},
             "공간 해시: 기대 O(N + 후보 쌍), 최악 O(N^2)");
}

}  // namespace ailab
