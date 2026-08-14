#pragma once

#include "grid/grid.h"
#include "scenes/iscene.h"

#include "raylib.h"

#include <vector>

namespace ailab {

// Flow Field Scene — cost -> integration(Dijkstra) -> flow 벡터장.
// 다수 에이전트가 한 장의 벡터장을 따라 goal 로 이동한다 (ai/part10).
class FlowFieldScene : public IScene {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void DrawWorld(int cx, int cy, int cw, int ch) override;
    void DrawPanel(int px, int py, int pw, int ph) override;
    std::string_view Name() const override { return "FlowField"; }

private:
    void Rebuild();          // integration + flow 재계산
    void RespawnAgents();
    Cell SafeSpawnCell() const;

    Grid grid_;
    std::vector<float>   integration_;  // goal 까지 누적 비용
    std::vector<Vector2> flow_;         // 셀별 이동 방향 (정규화)

    struct Agent { Vector2 pos; Vector2 vel; };
    std::vector<Agent> agents_;

    int   gridDim_     = 40;
    int   agentCount_  = 200;
    float sepWeight_   = 0.6f;
    bool  showArrows_  = true;
    bool  dirty_       = true;
    bool  layoutReady_ = false;
};

}  // namespace ailab
