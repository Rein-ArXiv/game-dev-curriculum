#pragma once

#include "scenes/iscene.h"

#include "raylib.h"

#include <cstdint>
#include <vector>

namespace netlab {

// Lockstep Scene — 두 클라이언트의 결정론 시뮬을 나란히 돌린다.
// 같은 시드 + 같은 입력이면 상태 해시가 비트 단위로 일치한다. "디싱크 주입"을
// 켜면 한쪽 시뮬에 미세한 비결정성을 넣어, 해시가 갈라지고 두 화면이 점차
// 어긋나는 것을 관찰한다. multiplayer part01 의 결정론 요구를 보인다.
class LockstepScene : public IScene {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void DrawWorld(int cx, int cy, int cw, int ch) override;
    void DrawPanel(int px, int py, int pw, int ph) override;
    std::string_view Name() const override { return "Lockstep"; }

private:
    struct Particle { float x, y, vx, vy; };

    void Reset();
    void StepSim(std::vector<Particle>& ps, bool injectDesync);
    uint32_t HashSim(const std::vector<Particle>& ps) const;

    std::vector<Particle> simA_;
    std::vector<Particle> simB_;
    uint32_t hashA_ = 0, hashB_ = 0;

    int   count_      = 24;
    bool  injectDesync_ = false;
    bool  paused_     = false;
    int   tick_       = 0;
    int   firstDesyncTick_ = -1;
    double accum_     = 0.0;

    uint32_t seed_ = 777u;
};

}  // namespace netlab
