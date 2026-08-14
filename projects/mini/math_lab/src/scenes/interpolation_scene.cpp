#include "scenes/interpolation_scene.h"

#include "algo/curves.h"
#include "raygui.h"

#include <cstdio>

namespace mathlab {

void InterpolationScene::OnEnter() {
    initialized_ = false;
}

void InterpolationScene::Update(float dt) {
    p0_.Update();
    p1_.Update();
    p2_.Update();
    p3_.Update();
    if (autoPlay_) {
        tParam_ += dt * 0.25f;
        if (tParam_ > 1.0f) tParam_ -= 1.0f;
    }
}

Vector2 InterpolationScene::SampleCurve(float t) const {
    Vector2 a = p0_.Position();
    Vector2 b = p1_.Position();
    Vector2 c = p2_.Position();
    Vector2 d = p3_.Position();
    switch (curve_) {
        case Curve::Bezier:     return CubicBezier(a, b, c, d, t);
        case Curve::CatmullRom: return CatmullRom(a, b, c, d, t);
        case Curve::Hermite:
        default: {
            Vector2 m0 = V2Sub(b, a);
            Vector2 m1 = V2Sub(d, c);
            return CubicHermite(a, m0, c, m1, t);
        }
    }
}

void InterpolationScene::DrawWorld(int cx, int cy, int cw, int ch) {
    if (!initialized_) {
        p0_.SetPosition({(float)(cx + cw * 0.20f), (float)(cy + ch * 0.70f)});
        p1_.SetPosition({(float)(cx + cw * 0.35f), (float)(cy + ch * 0.25f)});
        p2_.SetPosition({(float)(cx + cw * 0.65f), (float)(cy + ch * 0.25f)});
        p3_.SetPosition({(float)(cx + cw * 0.80f), (float)(cy + ch * 0.70f)});
        initialized_ = true;
    }

    // 곡선 자체 — N 샘플 + 라인 스트립.
    const int N  = sampleCount_;
    Vector2 prev = SampleCurve(0.0f);
    for (int i = 1; i <= N; ++i) {
        float u  = (float)i / (float)N;
        Vector2 p = SampleCurve(u);
        DrawLineEx(prev, p, 2.0f, Color{180, 220, 255, 255});
        prev = p;
    }

    // Hermite 면 접선 화살표.
    if (curve_ == Curve::Hermite) {
        Vector2 a  = p0_.Position();
        Vector2 c  = p2_.Position();
        Vector2 m0 = V2Add(a, V2Sub(p1_.Position(), a));   // = p1_
        Vector2 m1 = V2Add(c, V2Sub(p3_.Position(), c));
        DrawLineEx(a, m0, 1.0f, Color{120, 220, 120, 200});
        DrawLineEx(c, m1, 1.0f, Color{120, 220, 120, 200});
    } else if (curve_ == Curve::Bezier) {
        // Control polygon
        DrawLineEx(p0_.Position(), p1_.Position(), 1.0f, Color{120, 120, 120, 180});
        DrawLineEx(p1_.Position(), p2_.Position(), 1.0f, Color{120, 120, 120, 180});
        DrawLineEx(p2_.Position(), p3_.Position(), 1.0f, Color{120, 120, 120, 180});
    }

    // 이동하는 점 (t 또는 arc-length s).
    Vector2 pos;
    if (useArcLen_) {
        ArcLengthLUT lut;
        lut.Build(N, [this](float u) { return SampleCurve(u); });
        pos = lut.Sample(tParam_, [this](float u) { return SampleCurve(u); });
    } else {
        pos = SampleCurve(tParam_);
    }
    DrawCircleV(pos, 8.0f, Color{255, 220, 80, 255});
    DrawCircleLines((int)pos.x, (int)pos.y, 8.0f, Color{255, 255, 255, 255});

    // Control point 핸들
    p0_.Draw(Color{255, 100, 100, 255}, "P0");
    p1_.Draw(Color{255, 180, 100, 255}, "P1");
    p2_.Draw(Color{180, 100, 255, 255}, "P2");
    p3_.Draw(Color{100, 180, 255, 255}, "P3");

    const char* curveName = (curve_ == Curve::Bezier) ? "Cubic Bezier"
                          : (curve_ == Curve::CatmullRom) ? "Catmull-Rom"
                          : "Cubic Hermite";
    char buf[80];
    std::snprintf(buf, sizeof(buf),
                   "%s   t = %.2f%s",
                   curveName, tParam_,
                   useArcLen_ ? "  (arc-length parametrization)"
                              : "  (parameter t)");
    DrawText(buf, cx + 10, cy + 10, 14, Color{200, 200, 200, 255});
    (void)cw; (void)ch;
}

void InterpolationScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;

    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, "Interpolation Scene");
    y += 22;

    int curveIdx = (int)curve_;
    GuiToggleGroup(Rectangle{(float)(px + 8), (float)y, 80, 22},
                    "Bezier;Catmull;Hermite", &curveIdx);
    curve_ = (Curve)curveIdx;
    y += 30;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "t = %.2f", tParam_);
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
               nullptr, nullptr, &tParam_, 0.0f, 1.0f);
    y += 42;

    GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
                 " auto play", &autoPlay_);
    y += 22;
    GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
                 " arc-length param", &useArcLen_);
    y += 22;

    float samples = (float)sampleCount_;
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, "samples (curve resolution)");
    y += 18;
    GuiSlider(Rectangle{(float)(px + 8), (float)y, 220, 16},
               nullptr, nullptr, &samples, 8.0f, 256.0f);
    sampleCount_ = (int)samples;
    y += 22;

    if (useArcLen_) {
        ArcLengthLUT lut;
        lut.Build(sampleCount_, [this](float u) { return SampleCurve(u); });
        std::snprintf(buf, sizeof(buf), "arc length: %.1f px", lut.TotalLength());
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
        y += 18;
    }
}

}  // namespace mathlab
