#include "scenes/noise_scene.h"

#include "algo/noise.h"
#include "raygui.h"

#include <algorithm>
#include <cstdio>

namespace mathlab {

NoiseScene::NoiseScene()  = default;
NoiseScene::~NoiseScene() = default;

void NoiseScene::OnEnter() {
    pixels_.assign((size_t)(texW_ * texH_), Color{0, 0, 0, 255});
    Image img = GenImageColor(texW_, texH_, BLACK);
    tex_      = LoadTextureFromImage(img);
    UnloadImage(img);
    texLoaded_ = true;
    dirty_     = true;
}

void NoiseScene::OnExit() {
    if (texLoaded_) {
        UnloadTexture(tex_);
        texLoaded_ = false;
    }
}

void NoiseScene::Update(float) {
    // 입력 파라미터 변경 검출 — 패널이 갱신한 값과 last 값 비교.
    if (kind_ != lastKind_ || seed_ != lastSeed_ ||
        scale_ != lastScale_ ||
        offsetX_ != lastOffX_ || offsetY_ != lastOffY_ ||
        fbm_ != lastFbm_ || octaves_ != lastOct_ ||
        lacunarity_ != lastLac_ || gain_ != lastGain_) {
        dirty_ = true;
    }
}

void NoiseScene::RebuildIfNeeded() {
    if (!dirty_) return;
    if ((int)pixels_.size() != texW_ * texH_) {
        pixels_.assign((size_t)(texW_ * texH_), Color{0, 0, 0, 255});
    }

    auto sample = [&](float x, float y, uint32_t s) -> float {
        switch (kind_) {
            case Kind::Value:   return nz::Value2D(x, y, s) * 2.0f - 1.0f;
            case Kind::Perlin:  return nz::Perlin2D(x, y, s);
            case Kind::Simplex: return nz::Simplex2D(x, y, s);
        }
        return 0.0f;
    };

    for (int j = 0; j < texH_; ++j) {
        for (int i = 0; i < texW_; ++i) {
            float x = (i + offsetX_) * scale_;
            float y = (j + offsetY_) * scale_;
            float v;
            if (fbm_) {
                v = nz::FBM2D(x, y, (uint32_t)seed_,
                               octaves_, lacunarity_, gain_, sample);
            } else {
                v = sample(x, y, (uint32_t)seed_);
            }
            // [-1, 1] → [0, 255]
            float t = std::clamp(v * 0.5f + 0.5f, 0.0f, 1.0f);
            unsigned char gv = (unsigned char)(t * 255.0f);
            pixels_[(size_t)(j * texW_ + i)] = Color{gv, gv, gv, 255};
        }
    }
    UpdateTexture(tex_, pixels_.data());

    lastKind_ = kind_;
    lastSeed_ = seed_;
    lastScale_ = scale_;
    lastOffX_  = offsetX_; lastOffY_ = offsetY_;
    lastFbm_   = fbm_;
    lastOct_   = octaves_;
    lastLac_   = lacunarity_;
    lastGain_  = gain_;
    dirty_ = false;
}

void NoiseScene::DrawWorld(int cx, int cy, int cw, int ch) {
    RebuildIfNeeded();

    // 캔버스 가운데에 정사각 텍스처 배치 (scale = min(cw, ch) - 마진).
    int side = std::min(cw, ch) - 40;
    if (side < 64) side = 64;
    int x = cx + (cw - side) / 2;
    int y = cy + (ch - side) / 2;

    DrawTexturePro(
        tex_,
        Rectangle{0, 0, (float)texW_, (float)texH_},
        Rectangle{(float)x, (float)y, (float)side, (float)side},
        Vector2{0, 0}, 0.0f, WHITE);

    DrawRectangleLines(x, y, side, side, Color{80, 90, 100, 255});

    const char* kindName = (kind_ == Kind::Value)   ? "Value"
                          : (kind_ == Kind::Perlin) ? "Perlin"
                          : "Simplex";
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                   "%s   %s   scale = %.3f   seed = %d",
                   kindName,
                   fbm_ ? "FBM" : "single octave",
                   scale_,
                   seed_);
    DrawText(buf, cx + 10, cy + 10, 14, Color{200, 200, 200, 255});
}

void NoiseScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;

    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, "Noise Scene");
    y += 22;

    int kindIdx = (int)kind_;
    GuiToggleGroup(Rectangle{(float)(px + 8), (float)y, 70, 22},
                    "Value;Perlin;Simplex", &kindIdx);
    kind_ = (Kind)kindIdx;
    y += 30;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "scale (zoom) = %.3f", scale_);
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
               nullptr, nullptr, &scale_, 0.005f, 0.30f);
    y += 42;

    std::snprintf(buf, sizeof(buf), "offset X = %.0f", offsetX_);
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
               nullptr, nullptr, &offsetX_, -2000.0f, 2000.0f);
    y += 42;
    std::snprintf(buf, sizeof(buf), "offset Y = %.0f", offsetY_);
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
               nullptr, nullptr, &offsetY_, -2000.0f, 2000.0f);
    y += 42;

    float seedF = (float)seed_;
    std::snprintf(buf, sizeof(buf), "seed = %d", seed_);
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
               nullptr, nullptr, &seedF, 1.0f, 9999.0f);
    seed_ = (int)seedF;
    y += 42;

    GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
                 " FBM (multi-octave)", &fbm_);
    y += 24;

    if (fbm_) {
        float oct = (float)octaves_;
        std::snprintf(buf, sizeof(buf), "octaves = %d", octaves_);
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
        GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
                   nullptr, nullptr, &oct, 1.0f, 8.0f);
        octaves_ = (int)oct;
        y += 42;
        std::snprintf(buf, sizeof(buf), "lacunarity = %.2f", lacunarity_);
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
        GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
                   nullptr, nullptr, &lacunarity_, 1.5f, 4.0f);
        y += 42;
        std::snprintf(buf, sizeof(buf), "gain = %.2f", gain_);
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
        GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
                   nullptr, nullptr, &gain_, 0.1f, 0.9f);
        y += 42;
    }
}

}  // namespace mathlab
