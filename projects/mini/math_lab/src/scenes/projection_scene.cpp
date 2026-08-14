#include "scenes/projection_scene.h"

#include "algo/depth.h"
#include "algo/kahan.h"

#include "raygui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace mathlab {

void ProjectionScene::OnEnter() {}

void ProjectionScene::Update(float) {}

void ProjectionScene::DrawWorld(int cx, int cy, int cw, int ch) {
    if (mode_ == Mode::DepthPrecision) {
        // 그래프 영역.
        int   margin = 60;
        int   gx     = cx + margin;
        int   gy     = cy + 60;
        int   gw     = cw - margin * 2;
        int   gh     = ch - 100;
        if (gw < 100 || gh < 80) return;

        // 축
        DrawLine(gx, gy + gh, gx + gw, gy + gh, Color{180, 180, 180, 255});
        DrawLine(gx, gy,      gx,      gy + gh, Color{180, 180, 180, 255});

        const int N = 256;
        // X 축 — viewZ 가 [-near, -far] 까지 (linear or log)
        // Y 축 — NDC z (0 ~ 1)
        auto plotPath = [&](auto formula, Color c) {
            for (int i = 0; i < N; ++i) {
                float t0 = (float)i / (float)(N - 1);
                float t1 = (float)(i + 1) / (float)(N - 1);
                if (i == N - 1) break;
                float vz0, vz1;
                if (logScale_) {
                    // log: near 에서 far 로 log 분포
                    float ln_n = std::log(near_);
                    float ln_f = std::log(far_);
                    vz0 = -std::exp(ln_n + (ln_f - ln_n) * t0);
                    vz1 = -std::exp(ln_n + (ln_f - ln_n) * t1);
                } else {
                    vz0 = -(near_ + (far_ - near_) * t0);
                    vz1 = -(near_ + (far_ - near_) * t1);
                }
                float y0 = formula(vz0, near_, far_);
                float y1 = formula(vz1, near_, far_);
                int   px0 = gx + (int)(t0 * gw);
                int   px1 = gx + (int)(t1 * gw);
                int   py0 = gy + gh - (int)(y0 * gh);
                int   py1 = gy + gh - (int)(y1 * gh);
                DrawLine(px0, py0, px1, py1, c);
            }
        };

        if (showStandard_) {
            plotPath(StdNdcZ, Color{120, 200, 255, 255});
        }
        if (showReverse_) {
            plotPath(ReverseNdcZ, Color{255, 200, 80, 255});
        }

        // 축 라벨
        DrawText("View Z (near → far)", gx + gw / 2 - 60,
                  gy + gh + 6, 12, Color{200, 200, 200, 255});
        DrawText("NDC z", gx - 50, gy + gh / 2, 12, Color{200, 200, 200, 255});
        DrawText("0", gx - 16, gy + gh - 6, 12, Color{200, 200, 200, 255});
        DrawText("1", gx - 16, gy - 6, 12, Color{200, 200, 200, 255});

        // 범례
        if (showStandard_) {
            DrawRectangle(gx + 10, gy + 10, 10, 10, Color{120, 200, 255, 255});
            DrawText("Standard Z [0, 1]", gx + 26, gy + 10, 12,
                       Color{200, 220, 240, 255});
        }
        if (showReverse_) {
            DrawRectangle(gx + 10, gy + 26, 10, 10, Color{255, 200, 80, 255});
            DrawText("Reverse Z [1, 0]", gx + 26, gy + 26, 12,
                       Color{240, 220, 180, 255});
        }

        // 정보 텍스트
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                       "DX/Vulkan style depth.   near = %.3f   far = %.1f"
                       "   X axis = %s",
                       near_, far_,
                       logScale_ ? "log scale" : "linear");
        DrawText(buf, cx + 10, cy + 10, 14, Color{200, 200, 200, 255});

        DrawText("Curves show only the NDC z mapping, not depth-buffer precision.",
                  cx + 10, cy + 30, 12, Color{200, 200, 200, 255});
        DrawText("Real precision gain comes from float depth ULPs * mapping slope.",
                  cx + 10, cy + 46, 12, Color{200, 200, 200, 255});
    } else {
        // Kahan vs Naive
        int gx = cx + 60, gy = cy + 60;
        int gw = cw - 120, gh = ch - 100;
        DrawLine(gx, gy + gh, gx + gw, gy + gh, Color{180, 180, 180, 255});
        DrawLine(gx, gy,      gx,      gy + gh, Color{180, 180, 180, 255});

        // N 개 누적: sum_naive, sum_kahan 의 시간에 따른 오차 누적.
        // 매 프레임 새로 계산. N 이 큼 → 한 프레임 안에 끝나야 60fps 안전.
        // N <= 1,000,000 까지 허용.
        int   N = std::max(100, kahanN_);
        if (N > 1000000) N = 1000000;

        // 진실 값 — double 로 누적해 reference 로 사용.
        double exactSum = 0.0;
        for (int i = 0; i < N; ++i) {
            exactSum += (double)kahanSmall_;
        }
        exactSum += (double)kahanBig_;

        // Naive (float)
        float naive = 0.0f;
        // Kahan (float, 보상) — 누적 규칙은 algo/kahan.h 한 곳에만 둔다.
        KahanAccumulator kahan;
        // 일정 간격 샘플
        int     samples = 200;
        std::vector<Vector2> naivePoints, kahanPoints;
        naivePoints.reserve((size_t)(samples + 1));
        kahanPoints.reserve((size_t)(samples + 1));
        int interval = std::max(1, N / samples);

        for (int i = 0; i < N; ++i) {
            float x = kahanSmall_;
            naive += x;
            kahan.Add(x);
            if (i % interval == 0 || i == N - 1) {
                double partial = (double)kahanSmall_ * (i + 1);
                float  errN    = (float)((double)naive       - partial);
                float  errK    = (float)((double)kahan.Sum() - partial);
                float  xn      = (float)i / (float)N;
                naivePoints.push_back({(float)gx + xn * gw,
                                         0.0f + errN});
                kahanPoints.push_back({(float)gx + xn * gw,
                                         0.0f + errK});
            }
        }

        // 추가로 *큰 수 + 작은 수* 케이스: kahanBig_ 한 번 add 후 작은 수 N 회.
        // 위 그래프와 별개의 메시지 — naive 에서는 작은 값이 큰 값의 ULP에 묻힘.
        float  naive2 = NaiveSum(kahanBig_, kahanSmall_, N);
        float  kSum2  = KahanSum(kahanBig_, kahanSmall_, N);
        double truth2 = (double)kahanBig_ + (double)kahanSmall_ * N;

        // 위 시각: 오차 추세를 *시간 (i)* 축, *오차* 를 Y 축에 (선형 스케일).
        // 오차 범위 자동 산정.
        float maxAbs = 0.0f;
        for (auto& p : naivePoints) maxAbs = std::max(maxAbs, std::fabs(p.y));
        for (auto& p : kahanPoints) maxAbs = std::max(maxAbs, std::fabs(p.y));
        if (maxAbs < 1e-7f) maxAbs = 1e-7f;

        auto plot = [&](const std::vector<Vector2>& pts, Color c) {
            for (size_t i = 1; i < pts.size(); ++i) {
                Vector2 a = pts[i - 1], b = pts[i];
                int y0 = gy + gh / 2 - (int)((a.y / maxAbs) * (gh * 0.45f));
                int y1 = gy + gh / 2 - (int)((b.y / maxAbs) * (gh * 0.45f));
                DrawLine((int)a.x, y0, (int)b.x, y1, c);
            }
        };

        plot(naivePoints, Color{255, 100, 100, 255});
        plot(kahanPoints, Color{120, 220, 120, 255});
        DrawLine(gx, gy + gh / 2, gx + gw, gy + gh / 2,
                  Color{120, 120, 120, 200});

        char buf[200];
        std::snprintf(buf, sizeof(buf),
                       "Sum of %d copies of %.5f.   Reference (double) = %.6f",
                       N, kahanSmall_, exactSum - (double)kahanBig_);
        DrawText(buf, cx + 10, cy + 10, 14, Color{200, 200, 200, 255});

        std::snprintf(buf, sizeof(buf),
                       "Naive float sum error vs reference (red), Kahan (green).");
        DrawText(buf, cx + 10, cy + 30, 12, Color{200, 200, 200, 255});

        // absorption / loss-of-significance 케이스 결과 표시.
        std::snprintf(buf, sizeof(buf),
                       "Big + small case (big=%.1f): naive=%.6f, kahan=%.6f, truth=%.6f",
                       kahanBig_, naive2, kSum2, truth2);
        DrawText(buf, cx + 10, cy + ch - 30, 12, Color{220, 220, 220, 255});
    }
    (void)cw; (void)ch;
}

void ProjectionScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;

    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18},
              "Projection / Stability");
    y += 22;

    int modeIdx = (int)mode_;
    GuiToggleGroup(Rectangle{(float)(px + 8), (float)y, 100, 22},
                    "Depth;Kahan", &modeIdx);
    mode_ = (Mode)modeIdx;
    y += 30;

    char buf[64];
    if (mode_ == Mode::DepthPrecision) {
        std::snprintf(buf, sizeof(buf), "near = %.3f", near_);
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
        GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
                   nullptr, nullptr, &near_, 0.001f, 10.0f);
        y += 42;

        std::snprintf(buf, sizeof(buf), "far = %.0f", far_);
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
        GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
                   nullptr, nullptr, &far_, 100.0f, 10000.0f);
        y += 42;

        GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
                     " standard z [0,1]", &showStandard_);
        y += 22;
        GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
                     " reverse z [1,0]", &showReverse_);
        y += 22;
        GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
                     " log X axis", &logScale_);
        y += 22;
    } else {
        float n = (float)kahanN_;
        std::snprintf(buf, sizeof(buf), "N = %d", kahanN_);
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
        GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
                   nullptr, nullptr, &n, 100.0f, 1000000.0f);
        kahanN_ = (int)n;
        y += 42;

        std::snprintf(buf, sizeof(buf), "small dx = %.5f", kahanSmall_);
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
        GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
                   nullptr, nullptr, &kahanSmall_, 0.00001f, 0.01f);
        y += 42;

        std::snprintf(buf, sizeof(buf), "big initial = %.0f", kahanBig_);
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
        GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
                   nullptr, nullptr, &kahanBig_, 0.0f, 100000.0f);
        y += 42;
    }
}

}  // namespace mathlab
