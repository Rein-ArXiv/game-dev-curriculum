// Step 3 — 외력 / 스프링 / 드래그. 단일 mass 에 여러 force 를 토글로 켜고 끄며
// 운동 에너지 + 위치 에너지 + 총 에너지 그래프를 본다.
//
// 보존력 (중력, 스프링) 만 켜면 *총 에너지 ≈ 일정* — semi-implicit Euler 의
// 약 보존성 + jitter 한도 안에서. 드래그 (선형, 2 차) 가 켜지면 *총 에너지가
// 단조 감소* — 비보존력의 정의 그대로.
//
// 입력:
//   - mass 핸들 — 클릭+드래그로 위치 리셋 (속도 0).
//   - anchor 핸들 — 스프링 anchor 점 이동.
//   - P 키 — mass 에 위 방향 임펄스 (튀어오름).

#pragma once

#include "scenes/iscene.h"
#include "ui/handle.h"
#include "ui/line_graph.h"

#include "raylib.h"

namespace physlab {

class ForcesScene : public IScene {
public:
    ForcesScene();

    void OnEnter() override;
    void HandleInput() override;
    void Step(float dt) override;
    void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) override;
    void DrawPanel(int panelX, int panelY, int panelW, int panelH) override;
    std::string_view Name() const override { return "Forces"; }

    // --- 헤드리스 검증용 ---
    // GUI 패널의 체크박스 / 슬라이더가 만들던 상태를 창 없이 만든다. 값의 의미와
    // 기본값은 패널과 동일하고, GUI 경로는 이 함수들을 부르지 않는다. force 식
    // 자체는 이미 (pos, vel) 만으로 결정되는 const 함수라 그대로 노출한다.
    Vector2 ComputeForce(Vector2 pos, Vector2 vel) const;

    void SetCanvas(int x, int y, int w, int h);
    void SetState(Vector2 pos, Vector2 vel);
    void SetGravity(bool on, float g);
    void SetSpring(bool on, float k, float rest, Vector2 anchor);
    void SetDrag(float linear, float quad);

    Vector2 Position() const { return pos_; }
    Vector2 Velocity() const { return vel_; }
    float   Mass()     const { return mass_; }
    // 마지막 Step 이 그래프에 넣은 총 에너지 (KE + PE, m·s 단위).
    float   LastTotalEnergy() const { return totalHist_.Latest(); }

private:
    void    ApplyImpulse(Vector2 dv);

    Vector2 pos_      = {0, 0};
    Vector2 vel_      = {0, 0};
    float   radius_   = 14.0f;
    float   mass_     = 1.0f;

    DragHandle massHandle_{};
    DragHandle anchorHandle_{};

    // 토글들.
    bool  gravityOn_   = true;
    bool  springOn_    = false;
    float dragLinear_  = 0.0f;       // 선형 drag 계수 c1
    float dragQuad_    = 0.0f;       // 2 차 drag 계수 c2
    float springK_     = 8.0f;       // 스프링 상수
    float springRest_  = 80.0f;      // 자연 길이 (px)
    float gravityG_    = 600.0f;     // px / s²

    // 임펄스 1 회 (Step 동안만 적용).
    bool    pulsePending_ = false;
    Vector2 pulseDv_      = {0, 0};

    // 에너지 그래프.
    GraphSeries keHist_;
    GraphSeries peHist_;
    GraphSeries totalHist_;

    bool initialized_ = false;
    int  lastCanvasX_ = 0, lastCanvasY_ = 0;
    int  lastCanvasW_ = 0, lastCanvasH_ = 0;
};

}  // namespace physlab
