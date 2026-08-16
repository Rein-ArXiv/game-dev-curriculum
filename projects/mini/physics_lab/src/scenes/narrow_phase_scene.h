// Step 5 — Narrow Phase. Broad Phase 가 넘긴 후보 쌍에 대해 *진짜 충돌이 있는가*,
// 있다면 *접촉 법선 + penetration depth* 를 계산.
//
// 비교: SAT (Separating Axis Theorem) vs GJK + EPA.
//   - SAT: 두 볼록 다각형의 모든 변 방향이 분리축 후보. 모든 축에서 분리되지
//     않으면 충돌. 각 축의 침투량은 *두 투영 구간을 떼어놓는 최소 이동 거리*
//     min(amx - bmn, bmx - amn) 이고 (교집합 길이가 아니다 — 포함 관계에서
//     과소평가한다), 그 값이 가장 작은 축과 밀어내는 방향이 contact normal.
//   - GJK: support 함수만으로 동작 → 임의 볼록 도형 지원. simplex 를 확장해
//     원점이 Minkowski difference 안에 있는지 검사.
//   - EPA: GJK 가 "충돌" 이라고 답하면 그 simplex 를 확장해 원점까지의 최단 거리
//     = penetration depth + normal 을 구함.
//
// 두 알고리즘 모두 매 frame 동시에 돌고, 결과 (collision yes/no + depth) 를
// 비교 — 갈라지면 패널과 캔버스에 "boundary / numerical mismatch" 표시.

#pragma once

#include "scenes/iscene.h"
#include "ui/handle.h"
#include "ui/line_graph.h"

#include "raylib.h"

#include <vector>

namespace physlab {

struct Polygon2D {
    std::vector<Vector2> local;   // 로컬 좌표 (회전 / 평행이동 전)
    Vector2              pos    = {0, 0};
    float                angle  = 0;

    Vector2              ToWorld(Vector2 lp) const;
    std::vector<Vector2> WorldVerts() const;
    Vector2              Support(Vector2 dir) const;
};

enum class NarrowAlgo {
    SAT     = 0,
    GjkEpa  = 1,
};

class NarrowPhaseScene : public IScene {
public:
    NarrowPhaseScene();

    void OnEnter() override;
    void HandleInput() override;
    void Step(float dt) override;
    void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) override;
    void DrawPanel(int panelX, int panelY, int panelW, int panelH) override;
    std::string_view Name() const override { return "NarrowPhase"; }

private:
    void MakeRegularPolygon(Polygon2D& p, int vertices, float radius);
    void UpdatePolygonFromHandles(Polygon2D& p,
                                     DragHandle& center,
                                     DragHandle& rotation,
                                     float rotRadius);

    Polygon2D   a_, b_;
    DragHandle  aCenter_{}, aRot_{};
    DragHandle  bCenter_{}, bRot_{};
    float       rotRadius_ = 50.0f;

    NarrowAlgo  algo_  = NarrowAlgo::SAT;
    int         vertsA_ = 5;
    int         vertsB_ = 4;
    float       radiusA_ = 40.0f;
    float       radiusB_ = 50.0f;

    // 현재 알고리즘 결과.
    bool    lastCollision_ = false;
    Vector2 lastNormal_    = {0, 0};
    float   lastDepth_     = 0.0f;
    double  lastUs_        = 0.0;

    // SAT 와 GJK+EPA 의 결과 비교 — collision 일치 + depth 차 < 1.5 px.
    // normal 은 이 비교에 들어가지 않는다.
    bool    bothAgree_   = true;
    bool    satCollision_  = false;
    bool    gjkCollision_  = false;
    float   satDepth_      = 0.0f;
    float   gjkDepth_      = 0.0f;
    Vector2 satNormal_     = {0, 0};
    Vector2 gjkNormal_     = {0, 0};

    GraphSeries depthHist_;
    GraphSeries usHist_;

    bool initialized_ = false;
    int  lastCanvasX_ = 0, lastCanvasY_ = 0;
    int  lastCanvasW_ = 0, lastCanvasH_ = 0;
};

// SAT 와 GJK + EPA 의 free function 구현. 다른 코드에서도 재사용 가능.
bool SATCollide(const Polygon2D& a, const Polygon2D& b,
                  Vector2& normal, float& depth);
bool GjkEpaCollide(const Polygon2D& a, const Polygon2D& b,
                     Vector2& normal, float& depth);

}  // namespace physlab
