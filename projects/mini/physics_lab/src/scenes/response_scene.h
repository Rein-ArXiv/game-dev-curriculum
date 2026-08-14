// Step 6 — 충돌 응답 비교. Narrow Phase 가 *normal + depth* 를 알려줬다면,
// Response 는 *그것을 어떻게 속도 / 위치 갱신에 반영하는가*.
//
// 시나리오: N 개의 원형 강체가 좌우 벽 사이 바닥에 쌓임. 같은 워크로드를
// 네 응답 방식으로 돌리며 *평균 침투 깊이 / 안정성 / 비용* 을 비교.
//
//   Penalty           : depth 에 비례한 spring force. stiff equation → 작은 dt 필요.
//   Impulse (1-shot)  : 매 contact 마다 normal velocity 를 한 번에 0 으로.
//                        position 도 분리. *iteration 1 회* — stacking 불안정.
//   Sequential Impulse: 같은 impulse 를 *K 번 반복* 적용. accumulated λ + clamp.
//                        Box2D / Bullet / PhysX 의 표준.
//   PBD (Position-Based Dynamics) : *position 직접* 보정 후 velocity = (pos-prev)/dt.
//                        cloth / rope / soft body 에서 표준.

#pragma once

#include "core/contact_solver.h"
#include "scenes/iscene.h"
#include "ui/line_graph.h"

#include "raylib.h"

#include <utility>
#include <vector>

namespace physlab {

enum class ResponseAlgo {
    Penalty           = 0,
    Impulse           = 1,
    SequentialImpulse = 2,
    PBD               = 3,
};

struct CircleRb {
    Vector2 pos;
    Vector2 vel;
    Vector2 prevPos;   // PBD 용
    float   radius   = 8.0f;
    float   mass     = 1.0f;
    float   invMass  = 1.0f;
};

// Contact / SolverParams / WorldBounds 는 core/contact_solver.h 가 정의한다 —
// Step 8 의 결정성 측정이 *같은 타입 위에서 같은 함수* 를 돌아야 하므로.

class ResponseScene : public IScene {
public:
    ResponseScene();

    void OnEnter() override;
    void HandleInput() override;
    void Step(float dt) override;
    void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) override;
    void DrawPanel(int panelX, int panelY, int panelW, int panelH) override;
    std::string_view Name() const override { return "Response"; }

    // --- 헤드리스 검증 진입점 (계약 확장 — 솔버 동작은 그대로) ---
    // GUI 는 DrawWorld 에서 캔버스 좌표를 넘겨주지만 테스트는 창이 없다.
    // 아래 세터로 같은 값을 주입하면 Step() 이 GUI 와 동일한 경로를 돈다.
    void   GatherContacts(std::vector<Contact>& out) const;
    void   ApplySequentialImpulse(std::vector<Contact>& contacts);
    void   ApplyPositionCorrection(const std::vector<Contact>& contacts);

    void   SetCanvas(int x, int y, int w, int h) {
        lastCanvasX_ = x; lastCanvasY_ = y;
        lastCanvasW_ = w; lastCanvasH_ = h;
    }
    void   SetAlgo(ResponseAlgo algo)   { algo_ = algo; }
    void   SetIterations(int iterations) { iterations_ = iterations; }
    void   SetBodyCount(int n)          { N_ = n; }
    void   ResetBodiesForTest() { ResetBodies(N_); initialized_ = true; }
    void   SetBodies(std::vector<CircleRb> bodies) {
        bodies_ = std::move(bodies);
        N_ = (int)bodies_.size();
        initialized_ = true;
    }
    const std::vector<CircleRb>& Bodies() const { return bodies_; }
    float  Slop() const             { return solver_.slop; }
    float  Restitution() const      { return solver_.restitution; }
    float  PositionCorrectFactor() const { return solver_.posCorrectFactor; }
    float  LastAvgPen() const       { return lastAvgPen_; }
    float  LastMaxPen() const       { return lastMaxPen_; }
    int    LastContacts() const     { return lastContacts_; }

private:
    void   ResetBodies(int n);
    void   IntegrateGravity(float dt);
    void   ApplyPenalty(const std::vector<Contact>& contacts, float dt);
    void   ApplyImpulseOnce(std::vector<Contact>& contacts);
    void   ApplyPBD(float dt);
    WorldBounds Bounds() const {
        return BoundsFromCanvas(lastCanvasX_, lastCanvasY_,
                                lastCanvasW_, lastCanvasH_);
    }

    std::vector<CircleRb> bodies_;
    int                   N_           = 24;
    int                   iterations_  = 8;
    float                 gravity_     = kDefaultGravity;
    float                 penaltyK_    = 20000.0f;
    // restitution / slop / posCorrectFactor 는 공유 솔버의 파라미터다.
    SolverParams          solver_;

    ResponseAlgo algo_ = ResponseAlgo::SequentialImpulse;

    GraphSeries penHist_;     // 평균 침투 깊이
    GraphSeries msHist_;      // 응답 비용
    GraphSeries keHist_;      // 총 운동 에너지

    float  lastAvgPen_      = 0.0f;
    float  lastMaxPen_      = 0.0f;
    int    lastContacts_    = 0;
    double lastMs_          = 0.0;

    bool initialized_ = false;
    int  lastCanvasX_ = 0, lastCanvasY_ = 0;
    int  lastCanvasW_ = 0, lastCanvasH_ = 0;

    unsigned rng_ = 1u;
};

}  // namespace physlab
