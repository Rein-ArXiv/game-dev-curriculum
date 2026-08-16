// Step 4: Noise Scene.
//   Value / Perlin / Simplex 토글
//   + FBM (옥타브 / lacunarity / gain) 토글
//   + scale (zoom), seed, offset 슬라이더
//   + Image 로 noise heatmap 을 빌드 후 Texture 로 표시 (파라미터가 바뀐
//     프레임에만 256x256 을 다시 계산해 UpdateTexture).

#pragma once

#include "raylib.h"
#include "scenes/iscene.h"

#include <vector>

namespace mathlab {

class NoiseScene : public IScene {
public:
    NoiseScene();
    ~NoiseScene() override;

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) override;
    void DrawPanel(int panelX, int panelY, int panelW, int panelH) override;
    std::string_view Name() const override { return "Noise"; }

private:
    enum class Kind { Value, Perlin, Simplex };

    Kind     kind_       = Kind::Perlin;
    float    scale_      = 0.04f;       // 1 픽셀 당 noise 좌표 스케일
    float    offsetX_    = 0.0f;
    float    offsetY_    = 0.0f;
    int      seed_       = 1;
    bool     fbm_        = false;
    int      octaves_    = 4;
    float    lacunarity_ = 2.0f;
    float    gain_       = 0.5f;

    // 텍스처 캐시
    int                 texW_ = 256;
    int                 texH_ = 256;
    Texture2D           tex_{};
    bool                texLoaded_ = false;
    std::vector<Color>  pixels_;
    bool                dirty_ = true;
    int                 lastSeed_ = -1;
    Kind                lastKind_ = Kind::Perlin;
    float               lastScale_ = 0.0f;
    float               lastOffX_  = 0.0f, lastOffY_ = 0.0f;
    bool                lastFbm_   = false;
    int                 lastOct_   = 0;
    float               lastLac_   = 0.0f;
    float               lastGain_  = 0.0f;

    void RebuildIfNeeded();
};

}  // namespace mathlab
