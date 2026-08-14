#pragma once

#include "scenes/iscene.h"

#include "raylib.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ailab {

// Boids Scene — Separation / Alignment / Cohesion 세 규칙 + 공간 해시 가속.
// 가중치 슬라이더로 무리의 성격을 조절한다 (ai/part12).
class BoidsScene : public IScene {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void DrawWorld(int cx, int cy, int cw, int ch) override;
    void DrawPanel(int px, int py, int pw, int ph) override;
    std::string_view Name() const override { return "Boids"; }

private:
    struct Boid { Vector2 pos; Vector2 vel; };

    void Respawn();
    int64_t HashCell(int cx, int cy) const {
        return ((int64_t)(uint32_t)cx << 32) | (uint32_t)cy;
    }

    std::vector<Boid> boids_;
    std::unordered_map<int64_t, std::vector<int>> hash_;

    int   count_     = 300;
    float perception_ = 36.0f;
    float wSep_      = 1.5f;
    float wAli_      = 1.0f;
    float wCoh_      = 1.0f;
    float maxSpeed_  = 140.0f;

    Rectangle canvas_{0, 0, 0, 0};
};

}  // namespace ailab
