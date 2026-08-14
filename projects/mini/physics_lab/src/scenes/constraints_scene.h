// Step 7 — 제약 (Constraints). N 개 mass 가 일렬로 연결된 chain 시나리오.
// 첫 mass 는 *pin* — 사용자가 드래그할 수 있는 anchor. 나머지는 중력에 따라
// 늘어지며 *distance constraint* 로 일정 거리를 유지.
//
// 비교:
//   PBD distance constraint — position 직접 보정. iterations 반복.
//     장점: 단순 / 안정 / cloth·rope 에서 표준.
//     단점: stiffness 가 iterations 에 의존 — 적게 돌리면 chain 이 늘어남.
//   Sequential Impulse joint — velocity level. 한 step 에 K 번 반복.
//     장점: 진짜 rigid 한 강체 사이 강한 제약. *진동* 보임 (velocity drift).
//     단점: position drift 가 누적 가능 → Baumgarte / split impulse 필요.
//
// 이 Scene 의 핸들: 첫 mass (anchor). 클릭+드래그로 끌고 다닐 수 있다.

#pragma once

#include "scenes/iscene.h"
#include "ui/handle.h"
#include "ui/line_graph.h"

#include "raylib.h"

#include <vector>

namespace physlab {

enum class ConstraintSolver {
    PBD = 0,
    SI  = 1,
};

struct Particle {
    Vector2 pos, vel, prevPos;
    float   invMass = 1.0f;
};

struct DistanceConstraint {
    int   a, b;
    float rest;
    float lambda;   // SI 의 accumulated impulse
};

class ConstraintsScene : public IScene {
public:
    ConstraintsScene();

    void OnEnter() override;
    void HandleInput() override;
    void Step(float dt) override;
    void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) override;
    void DrawPanel(int panelX, int panelY, int panelW, int panelH) override;
    std::string_view Name() const override { return "Constraints"; }

    // --- 헤드리스 검증 진입점 (계약 확장 — 솔버 동작은 그대로) ---
    void SetCanvas(int x, int y, int w, int h) {
        lastCanvasX_ = x; lastCanvasY_ = y;
        lastCanvasW_ = w; lastCanvasH_ = h;
    }
    void SetSolver(ConstraintSolver solver) { solver_ = solver; }
    void SetIterations(int iterations)      { iterations_ = iterations; }
    void SetBaumgarte(float baumgarte)      { baumgarte_ = baumgarte; }
    void BuildChainForTest(int n, float segmentLen) {
        N_ = n; segment_ = segmentLen;
        BuildChain(n, segmentLen);
        initialized_ = true;
    }
    const std::vector<Particle>&           Particles() const { return particles_; }
    const std::vector<DistanceConstraint>& Links() const     { return links_; }
    float Segment() const        { return segment_; }
    float LastAvgStretch() const { return lastAvgStretch_; }
    float LastMaxStretch() const { return lastMaxStretch_; }

private:
    void BuildChain(int n, float segmentLen);
    void IntegrateGravity(float dt);
    void SolvePBD();
    void SolveSI(float dt);

    std::vector<Particle>            particles_;
    std::vector<DistanceConstraint>  links_;
    DragHandle                       anchorHandle_{};

    int   N_           = 12;
    float segment_     = 36.0f;
    int   iterations_  = 12;
    float gravity_     = 600.0f;
    float baumgarte_   = 0.2f;   // SI: position drift 보정 비율
    ConstraintSolver solver_ = ConstraintSolver::PBD;

    GraphSeries stretchHist_;  // chain 의 평균 segment stretch 비율
    GraphSeries msHist_;

    float lastAvgStretch_ = 0;
    float lastMaxStretch_ = 0;
    double lastMs_ = 0;

    bool initialized_ = false;
    int  lastCanvasX_ = 0, lastCanvasY_ = 0;
    int  lastCanvasW_ = 0, lastCanvasH_ = 0;
};

}  // namespace physlab
