// Step 5: Collision Scene.
//   2D OBB-OBB.
//   Mode A — SAT: 4 분리축 후보 + 각 축의 투영 + 최소 overlap (MTV) 표시.
//   Mode B — GJK: simplex 진화의 단계별 stepping. 자동 또는 수동.

#pragma once

#include "algo/collision2d.h"
#include "scenes/iscene.h"
#include "ui/handle.h"

namespace mathlab {

class CollisionScene : public IScene {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) override;
    void DrawPanel(int panelX, int panelY, int panelW, int panelH) override;
    std::string_view Name() const override { return "Collision"; }

private:
    enum class Mode { SAT, GJK };

    Mode    mode_ = Mode::SAT;

    DragHandle centerA_, centerB_;     // OBB 중심
    DragHandle rotA_,    rotB_;        // OBB 회전을 끌기로 (중심 + 회전 핸들 사이 각도)
    bool       initialized_ = false;
    float      sizeA_x_ = 80, sizeA_y_ = 50;
    float      sizeB_x_ = 60, sizeB_y_ = 90;

    // GJK 시각화용
    int        gjkStep_   = 0;
    bool       gjkAuto_   = true;
    float      gjkAccum_  = 0.0f;

    OBB2D BuildOBB(DragHandle const& center, DragHandle const& rot,
                    float halfX, float halfY) const;
};

}  // namespace mathlab
