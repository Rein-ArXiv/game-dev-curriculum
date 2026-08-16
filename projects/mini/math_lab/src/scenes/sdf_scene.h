// Step 7: SDF + Ray Marching Scene (2D).
//   두 도형 (Circle / Box) 의 위치는 핸들로, 크기는 패널 슬라이더로.
//   합성 모드 토글: Union (min) / SmoothMin / Subtract.
//   캔버스 전체에 SDF 등고선 (texture — 밝기 = 거리, 안/밖은 색조로 구분).
//   광선 ro → rd 가 sphere tracing 단계별로 표면까지 이동.

#pragma once

#include "raylib.h"
#include "scenes/iscene.h"
#include "ui/handle.h"

#include <vector>

namespace mathlab {

class SdfScene : public IScene {
public:
    SdfScene();
    ~SdfScene() override;

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) override;
    void DrawPanel(int panelX, int panelY, int panelW, int panelH) override;
    std::string_view Name() const override { return "SDF / RayMarch"; }

private:
    enum class Op { Union, Smin, Subtract };

    Op          op_      = Op::Smin;
    float       smoothK_ = 60.0f;
    float       sizeA_   = 90.0f;        // 도형 A (circle radius)
    float       boxBHalfX_ = 70.0f;      // 도형 B (box half size)
    float       boxBHalfY_ = 50.0f;
    bool        showRay_  = true;
    bool        showContours_ = true;

    DragHandle  centerA_, centerB_;
    DragHandle  rayStart_, rayEnd_;
    bool        initialized_ = false;

    // Texture cache
    int                 texW_ = 256;
    int                 texH_ = 256;
    Texture2D           tex_{};
    bool                texLoaded_ = false;
    std::vector<Color>  pixels_;
    bool                dirty_ = true;
    int                 lastCx_ = 0, lastCy_ = 0, lastCw_ = 0, lastCh_ = 0;

    // 마지막 입력 캐시 — dirty 검출용
    Vector2  lastA_{}, lastB_{};
    Op       lastOp_ = Op::Smin;
    float    lastK_  = 0.0f;
    float    lastSizeA_ = 0.0f, lastBHX_ = 0.0f, lastBHY_ = 0.0f;
    bool     lastShowContours_ = true;

    float SampleSDF(Vector2 p) const;
    void  RebuildTextureIfNeeded(int cx, int cy, int cw, int ch);
};

}  // namespace mathlab
