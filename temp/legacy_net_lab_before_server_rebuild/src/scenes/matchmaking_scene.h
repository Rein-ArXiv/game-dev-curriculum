#pragma once

#include "scenes/iscene.h"

#include "raylib.h"

#include <cstdint>
#include <vector>

namespace netlab {

// Matchmaking Scene — MMR 큐 + Progressive Widening 시각화.
// 가상 플레이어가 큐에 들어오고, 대기할수록 허용 MMR 범위가 넓어진다.
// 두 플레이어의 범위가 겹치면 매치. 대기시간 vs 매치품질을 측정. part02.
class MatchmakingScene : public IScene {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void DrawWorld(int cx, int cy, int cw, int ch) override;
    void DrawPanel(int px, int py, int pw, int ph) override;
    std::string_view Name() const override { return "Matchmaking"; }

private:
    struct Player { int mmr; float wait; };
    struct Flash  { float ax, bx, y; float life; };

    void Reset();
    float AllowedDiff(float maxWait) const { return baseWindow_ + maxWait * slope_; }

    std::vector<Player> queue_;
    std::vector<Flash>  flashes_;

    float spawnRate_  = 4.0f;    // 초당 신규 플레이어
    float slope_      = 60.0f;   // 초당 허용 MMR 범위 확장
    float baseWindow_ = 50.0f;   // 초기 허용 범위
    float spawnAccum_ = 0.0;

    // 측정.
    int   totalMatches_ = 0;
    float waitSum_ = 0.0f;
    float diffSum_ = 0.0f;
    float matchTimer_ = 0.0f;
    int   matchesThisWin_ = 0;
    int   matchesPerMinDisp_ = 0;

    uint32_t rng_ = 555u;
    uint32_t Next() { rng_ = rng_ * 1664525u + 1013904223u; return rng_; }
    int   RandMMR();   // 1500 중심 분포

    Rectangle canvas_{0, 0, 0, 0};
    static constexpr int kMinMMR = 0;
    static constexpr int kMaxMMR = 3000;
};

}  // namespace netlab
