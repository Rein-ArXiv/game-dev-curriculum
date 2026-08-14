// Step 6: Spatial / BVH Scene.
//   N 개의 작은 AABB 를 캔버스에 분포 (시드 기반).
//   BVH 빌드 후 노드 박스를 *깊이까지만* 그려 빌드 단계 애니메이션.
//   드래그 핸들 두 개로 광선 시작 / 끝 입력 — BVH 트래버설 후 방문 노드 강조.

#pragma once

#include "algo/bvh2d.h"
#include "scenes/iscene.h"
#include "ui/handle.h"

#include <vector>

namespace mathlab {

class SpatialScene : public IScene {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) override;
    void DrawPanel(int panelX, int panelY, int panelW, int panelH) override;
    std::string_view Name() const override { return "Spatial / BVH"; }

private:
    int                 numObjects_   = 60;
    int                 seed_         = 1;
    int                 leafThreshold_ = 4;
    int                 maxDepth_     = 0;     // 표시할 최대 깊이 (애니메이션)
    bool                showOnlyVisited_ = false;
    bool                showRay_      = true;

    std::vector<AABB2D> boxes_;
    BVH2D               bvh_;

    DragHandle          rayStart_;
    DragHandle          rayEnd_;
    bool                initialized_ = false;
    int                 lastSeed_    = -1;
    int                 lastN_       = -1;
    int                 lastLeaf_    = -1;
    int                 lastCx_ = 0, lastCy_ = 0, lastCw_ = 0, lastCh_ = 0;

    void Regenerate(int cx, int cy, int cw, int ch);
};

}  // namespace mathlab
