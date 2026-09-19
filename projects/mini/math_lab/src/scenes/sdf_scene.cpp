#include "scenes/sdf_scene.h"

#include "algo/sdf2d.h"
#include "raygui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mathlab {

SdfScene::SdfScene()  = default;
SdfScene::~SdfScene() = default;

void SdfScene::OnEnter() {
    pixels_.assign((size_t)(texW_ * texH_), Color{0, 0, 0, 255});
    Image img = GenImageColor(texW_, texH_, BLACK);
    tex_      = LoadTextureFromImage(img);
    UnloadImage(img);
    texLoaded_   = true;
    dirty_       = true;
    initialized_ = false;
}

void SdfScene::OnExit() {
    if (texLoaded_) {
        UnloadTexture(tex_);
        texLoaded_ = false;
    }
}

void SdfScene::Update(float) {
    centerA_.Update();
    centerB_.Update();
    rayStart_.Update();
    rayEnd_.Update();

    Vector2 a = centerA_.Position();
    Vector2 b = centerB_.Position();
    if (a.x != lastA_.x || a.y != lastA_.y || b.x != lastB_.x || b.y != lastB_.y ||
        op_ != lastOp_ || smoothK_ != lastK_ ||
        sizeA_ != lastSizeA_ ||
        boxBHalfX_ != lastBHX_ || boxBHalfY_ != lastBHY_ ||
        showContours_ != lastShowContours_) {
        dirty_ = true;
    }
}

float SdfScene::SampleSDF(Vector2 p) const {
    float a = sdf::Circle(p, centerA_.Position(), sizeA_);
    float b = sdf::Box(p, centerB_.Position(), {boxBHalfX_, boxBHalfY_});
    switch (op_) {
        case Op::Union:    return sdf::OpUnion(a, b);
        case Op::Smin:     return sdf::SminPoly(a, b, smoothK_);
        case Op::Subtract: return sdf::OpSubtract(a, b);
    }
    return a;
}

void SdfScene::RebuildTextureIfNeeded(int cx, int cy, int cw, int ch) {
    bool resize = (cx != lastCx_ || cy != lastCy_ ||
                    cw != lastCw_ || ch != lastCh_);
    if (!dirty_ && !resize) return;

    lastCx_ = cx; lastCy_ = cy; lastCw_ = cw; lastCh_ = ch;

    // 캔버스 좌표를 [0, texW_) × [0, texH_) 로 매핑.
    for (int j = 0; j < texH_; ++j) {
        float py = (float)cy + (((float)j + 0.5f) / (float)texH_) * (float)ch;
        for (int i = 0; i < texW_; ++i) {
            float px = (float)cx + (((float)i + 0.5f) / (float)texW_) * (float)cw;
            float d = SampleSDF({px, py});

            // 거리 기반 grayscale: 안쪽은 어둡게, 바깥쪽은 밝게.
            float t = std::clamp(d / 200.0f, -1.0f, 1.0f);
            float gray = 0.5f + 0.5f * t;     // 표면에서 0.5

            // 등고선: |fract(d/30)| 가 작으면 진하게.
            // snippet-begin: contour-band
            if (showContours_) {
                float band = std::fmod(std::fabs(d) / 30.0f, 1.0f);
                if (band > 0.92f || band < 0.08f) gray *= 0.65f;
            }
            // snippet-end: contour-band

            unsigned char gv = (unsigned char)(std::clamp(gray, 0.0f, 1.0f) * 255.0f);
            unsigned char rv = (d < 0) ? (unsigned char)(gv * 0.65f) : gv;
            unsigned char bv = (d < 0) ? gv : (unsigned char)(gv * 0.85f);
            pixels_[(size_t)(j * texW_ + i)] = Color{rv, gv, bv, 255};
        }
    }
    UpdateTexture(tex_, pixels_.data());

    lastA_  = centerA_.Position();
    lastB_  = centerB_.Position();
    lastOp_ = op_;
    lastK_  = smoothK_;
    lastSizeA_ = sizeA_;
    lastBHX_   = boxBHalfX_;
    lastBHY_   = boxBHalfY_;
    lastShowContours_ = showContours_;
    dirty_ = false;
}

void SdfScene::DrawWorld(int cx, int cy, int cw, int ch) {
    if (!initialized_) {
        centerA_.SetPosition({(float)(cx + cw * 0.40f),
                                (float)(cy + ch * 0.50f)});
        centerB_.SetPosition({(float)(cx + cw * 0.60f),
                                (float)(cy + ch * 0.55f)});
        rayStart_.SetPosition({(float)(cx + 30),
                                 (float)(cy + ch - 40)});
        rayEnd_.SetPosition({(float)(cx + cw * 0.5f),
                                (float)(cy + ch * 0.5f)});
        initialized_ = true;
        dirty_ = true;
    }

    RebuildTextureIfNeeded(cx, cy, cw, ch);

    DrawTexturePro(
        tex_,
        Rectangle{0, 0, (float)texW_, (float)texH_},
        Rectangle{(float)cx, (float)cy, (float)cw, (float)ch},
        Vector2{0, 0}, 0.0f, WHITE);

    // Sphere tracing 단계 시각화.
    if (showRay_) {
        // snippet-begin: sphere-trace
        Vector2 ro = rayStart_.Position();
        Vector2 rd = {rayEnd_.Position().x - ro.x, rayEnd_.Position().y - ro.y};
        float L = std::sqrt(rd.x * rd.x + rd.y * rd.y);
        if (L > 1e-6f) {
            Vector2 dn = {rd.x / L, rd.y / L};
            Vector2 cur = ro;
            const int   maxIter = 64;
            const float epsilon = 0.5f;
            const float maxT    = 2000.0f;
            float t = 0.0f;
            int   iter = 0;
            bool  hit  = false;
            for (; iter < maxIter; ++iter) {
                float d = SampleSDF(cur);
                // 안전 구 (sphere tracing 의 최대 안전 반지름) 표시.
                // snippet-begin: safe-circle
                DrawCircleLines((int)cur.x, (int)cur.y,
                                  std::max(d, 0.0f),
                                  Color{200, 230, 255, 70});
                // snippet-end: safe-circle
                if (d < epsilon) { hit = true; break; }
                if (t > maxT)    break;       // 표면 못 만남 — miss
                t   += d;
                cur  = {ro.x + dn.x * t, ro.y + dn.y * t};
            }
            DrawLineEx(ro, cur, 2.0f, Color{255, 240, 120, 255});
            if (hit) {
                DrawCircleV(cur, 5.0f, Color{255, 80, 80, 255});
            }

            // snippet-begin: ray-status
            char buf[64];
            std::snprintf(buf, sizeof(buf),
                           "ray steps: %d   t = %.0f   %s",
                           iter, t, hit ? "HIT" : "miss");
            DrawText(buf, cx + 10, cy + 30, 14, Color{220, 220, 220, 255});
            // snippet-end: ray-status
        }
        // snippet-end: sphere-trace
    }

    centerA_.Draw(Color{255, 200, 80, 255}, "A");
    centerB_.Draw(Color{120, 200, 255, 255}, "B");
    rayStart_.Draw(Color{120, 220, 120, 255}, "ro");
    rayEnd_.Draw(Color{220, 120, 220, 255}, "rd");

    const char* opName = (op_ == Op::Union)    ? "Union (min)"
                          : (op_ == Op::Smin)    ? "Smooth Min"
                          : "Subtract";
    char buf[80];
    std::snprintf(buf, sizeof(buf), "SDF op: %s%s",
                   opName,
                   (op_ == Op::Smin) ? "" : "");
    DrawText(buf, cx + 10, cy + 10, 14, Color{220, 220, 220, 255});
}

void SdfScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;

    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, "SDF / Ray March");
    y += 22;

    int opIdx = (int)op_;
    GuiToggleGroup(Rectangle{(float)(px + 8), (float)y, 70, 22},
                    "Union;Smin;Subtract", &opIdx);
    op_ = (Op)opIdx;
    y += 30;

    char buf[64];
    if (op_ == Op::Smin) {
        std::snprintf(buf, sizeof(buf), "smin k = %.0f", smoothK_);
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
        GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
                   nullptr, nullptr, &smoothK_, 0.0f, 200.0f);
        y += 42;
    }

    std::snprintf(buf, sizeof(buf), "A radius = %.0f", sizeA_);
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
               nullptr, nullptr, &sizeA_, 20.0f, 200.0f);
    y += 42;

    std::snprintf(buf, sizeof(buf), "B halfX = %.0f", boxBHalfX_);
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
               nullptr, nullptr, &boxBHalfX_, 20.0f, 200.0f);
    y += 42;

    std::snprintf(buf, sizeof(buf), "B halfY = %.0f", boxBHalfY_);
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
               nullptr, nullptr, &boxBHalfY_, 20.0f, 200.0f);
    y += 42;

    GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
                 " contour bands", &showContours_);
    y += 22;
    GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
                 " show sphere-trace ray", &showRay_);
    y += 22;
}

}  // namespace mathlab
