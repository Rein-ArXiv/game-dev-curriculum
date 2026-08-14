#pragma once

#include "grid/grid.h"
#include "pathfind/pathfinder.h"
#include "scenes/iscene.h"

namespace ailab {

// 길찾기 비교 Scene — A*/Dijkstra/Greedy/JPS 를 같은 맵에서 step 실행.
class PathfindScene : public IScene {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void DrawWorld(int cx, int cy, int cw, int ch) override;
    void DrawPanel(int px, int py, int pw, int ph) override;
    std::string_view Name() const override { return "Pathfind"; }

private:
    void Restart();   // 현재 알고리즘/맵으로 탐색 초기화

    Grid       grid_;
    Pathfinder pf_;

    int   algoIdx_  = 0;   // Algo
    int   heurIdx_  = 3;   // Heur (Octile)
    int   gridDim_  = 48;  // 정사각 그리드 변 길이
    bool  autoRun_  = true;
    float stepRate_ = 200.0f;   // 초당 step 수
    float stepAcc_  = 0.0f;
    bool  needRestart_ = true;

    // 패널 버튼이 세우고 Update 가 소비하는 요청 플래그.
    // 탐색 진행은 Update 에서만 일어나야 한다 (Draw 횟수와 무관).
    bool  pendingStep_ = false;
    bool  pendingRun_  = false;

    // 콤보 박스 편집 상태 (raygui 는 클릭 시 드롭다운 모드).
    bool  algoEdit_ = false;
    bool  heurEdit_ = false;
};

}  // namespace ailab
