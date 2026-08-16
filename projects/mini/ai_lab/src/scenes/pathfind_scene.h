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

    // 콤보 박스의 초기 선택. 인덱스를 손으로 적으면 열거형 순서와 조용히
    // 어긋난다 (heurIdx_ 가 3 = Chebyshev 인데 주석은 Octile 이었다).
    // 열거형에서 유도하면 다시 어긋날 수 없다.
    static constexpr int kDefaultAlgoIdx = static_cast<int>(Algo::AStar);
    static constexpr int kDefaultHeurIdx = static_cast<int>(Heur::Octile);

private:
    void Restart();   // 현재 알고리즘/맵으로 탐색 초기화

    Grid       grid_;
    Pathfinder pf_;

    int   algoIdx_  = kDefaultAlgoIdx;   // Algo (A*)
    int   heurIdx_  = kDefaultHeurIdx;   // Heur (Octile)
    int   gridDim_  = 48;  // 정사각 그리드 변 길이
    bool  autoRun_  = true;
    float stepRate_ = 200.0f;   // 초당 step 수
    float stepAcc_  = 0.0f;
    bool  needRestart_ = true;

    // 패널 버튼이 세우고 Update 가 소비하는 요청 플래그.
    // 탐색 진행은 Update 에서만 일어나야 한다 (Draw 횟수와 무관).
    bool  pendingStep_ = false;
    bool  pendingRun_  = false;

    // 미사용 — 패널은 편집 상태가 없는 GuiComboBox 로 알고리즘/휴리스틱을 고른다.
    bool  algoEdit_ = false;
    bool  heurEdit_ = false;
};

}  // namespace ailab
