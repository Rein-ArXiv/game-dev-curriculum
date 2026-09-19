// Step 4 — Broad Phase 비교. N 개의 움직이는 AABB 가 캔버스 안에서 튕긴다.
// 매 프레임 *후보 쌍* 을 추출하는 세 알고리즘 (Brute O(n²) / SAP / Spatial Hash)
// 중 하나로 처리, *후보 쌍 수* + *진짜 overlap 쌍 수* + *알고리즘 us* 를 측정.
//
// 학습 목적:
//   - Brute 의 후보 = N(N-1)/2. 모든 쌍 검사.
//   - SAP: x-축 정렬 후 active list 로 *x-overlap* 인 쌍만 narrow 에 전달.
//     temporally coherent (이동이 작음) 워크로드에서 효율 좋음.
//   - Spatial Hash: 셀 격자에 register, 같은 셀 안 쌍만 검사.
//     균등 분포 + 잦은 삽입 / 삭제에 유리. 셀 크기가 결정적 — 너무 크면 brute 비슷,
//     너무 작으면 같은 box 가 여러 셀에 등록되어 dedupe 비용 증가.
//
// 모든 알고리즘이 narrow phase 의 AABB-AABB 검사까지 마치고, *진짜 overlap* 의
// 수는 일관됨 — 알고리즘 정확성 검증용.

#pragma once

#include "scenes/iscene.h"
#include "ui/line_graph.h"

#include "raylib.h"

#include <utility>
#include <vector>

namespace physlab {

enum class BroadAlgo {
    Brute       = 0,
    SAP         = 1,
    SpatialHash = 2,
};

struct AabbBody {
    Vector2 pos;        // 중심
    Vector2 vel;
    Vector2 half;       // half size
};

class BroadPhaseScene : public IScene {
public:
    BroadPhaseScene();

    void OnEnter() override;
    void HandleInput() override;
    void Step(float dt) override;
    void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) override;
    void DrawPanel(int panelX, int panelY, int panelW, int panelH) override;
    std::string_view Name() const override { return "BroadPhase"; }

    // --- 헤드리스 검증 진입점 (계약 확장 — 알고리즘 동작은 그대로) ---
    // GUI 가 부르는 것과 *같은* 세 함수다. outPairs 가 nullptr 이면 기존 호출과
    // 완전히 동일하고, 주어지면 overlap 으로 판정한 쌍 (i < j) 을 기록한다.
    // 개수만 비교하면 false positive 와 false negative 가 서로를 상쇄해 가린다.
    using OverlapPair = std::pair<int, int>;
    void RunBrute(int& candidates, int& trueOverlap,
                  std::vector<OverlapPair>* outPairs = nullptr);
    void RunSAP(int& candidates, int& trueOverlap,
                std::vector<OverlapPair>* outPairs = nullptr);
    void RunSpatialHash(int& candidates, int& trueOverlap,
                        std::vector<OverlapPair>* outPairs = nullptr);

    void SetBodies(std::vector<AabbBody> bodies) {
        bodies_ = std::move(bodies);
    }
    void SetCellSize(float cellSize) { cellSize_ = cellSize; }
    const std::vector<AabbBody>& Bodies() const { return bodies_; }

private:
    void ResetBodies(int n);

    std::vector<AabbBody> bodies_;
    int                   N_                = 200;
    BroadAlgo             algo_             = BroadAlgo::Brute;
    float                 cellSize_         = 60.0f;
    float                 bodyHalf_         = 8.0f;
    float                 speed_            = 100.0f;
    int                   lastCandidates_   = 0;
    int                   lastAlgoOverlap_  = 0;   // 알고리즘이 찾은 overlap
    int                   lastGroundTruth_  = 0;   // brute 로 계산한 정답
    int                   lastMissed_       = 0;   // 정답 - 알고리즘 = false negative
    double                lastAlgoUs_       = 0.0;

    // 색은 알고리즘별로 한 색.
    Color algoColor_ = {120, 200, 255, 255};

    GraphSeries candHist_;          // 알고리즘 candidates
    GraphSeries msHist_;            // 알고리즘 us
    GraphSeries algoOverlapHist_;   // 알고리즘 overlap
    GraphSeries gtOverlapHist_;     // ground truth overlap

    bool initialized_ = false;
    int  lastCanvasX_ = 0, lastCanvasY_ = 0;
    int  lastCanvasW_ = 0, lastCanvasH_ = 0;

    unsigned rngState_ = 1u;
};

}  // namespace physlab
