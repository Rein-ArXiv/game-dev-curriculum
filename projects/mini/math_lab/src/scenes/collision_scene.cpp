#include "scenes/collision_scene.h"

#include "raygui.h"

#include <cmath>
#include <cstdio>

namespace mathlab {

void CollisionScene::OnEnter() {
    initialized_ = false;
    gjkStep_     = 0;
}

void CollisionScene::Update(float dt) {
    centerA_.Update();
    centerB_.Update();
    rotA_.Update();
    rotB_.Update();

    if (mode_ == Mode::GJK && gjkAuto_) {
        gjkAccum_ += dt;
        if (gjkAccum_ > 0.4f) {
            gjkAccum_ = 0;
            ++gjkStep_;
        }
    }
}

OBB2D CollisionScene::BuildOBB(DragHandle const& center, DragHandle const& rot,
                                  float halfX, float halfY) const {
    OBB2D r{};
    r.center = center.Position();
    Vector2 d = {rot.Position().x - r.center.x,
                  rot.Position().y - r.center.y};
    r.angle  = std::atan2(d.y, d.x);
    r.halfX  = halfX;
    r.halfY  = halfY;
    return r;
}

void CollisionScene::DrawWorld(int cx, int cy, int cw, int ch) {
    if (!initialized_) {
        centerA_.SetPosition({(float)(cx + cw * 0.35f), (float)(cy + ch * 0.5f)});
        centerB_.SetPosition({(float)(cx + cw * 0.62f), (float)(cy + ch * 0.5f)});
        rotA_.SetPosition({(float)(cx + cw * 0.35f) + 70.0f,
                            (float)(cy + ch * 0.5f)});
        rotB_.SetPosition({(float)(cx + cw * 0.62f) + 50.0f,
                            (float)(cy + ch * 0.5f) + 30.0f});
        initialized_ = true;
    }

    OBB2D A = BuildOBB(centerA_, rotA_, sizeA_x_, sizeA_y_);
    OBB2D B = BuildOBB(centerB_, rotB_, sizeB_x_, sizeB_y_);

    // OBB 그리기
    auto drawOBB = [](const OBB2D& obb, Color edge, Color fill) {
        auto C = obb.Corners();
        Vector2 poly[4] = {C[0], C[1], C[2], C[3]};
        // 채우기
        DrawTriangle(poly[0], poly[1], poly[2], fill);
        DrawTriangle(poly[0], poly[2], poly[3], fill);
        // 외곽선
        for (int i = 0; i < 4; ++i) {
            DrawLineEx(poly[i], poly[(i + 1) % 4], 2.0f, edge);
        }
        // 중심
        DrawCircleV(obb.center, 3.0f, WHITE);
    };
    drawOBB(A, Color{255, 200, 80, 255},  Color{255, 200, 80, 60});
    drawOBB(B, Color{120, 200, 255, 255}, Color{120, 200, 255, 60});

    if (mode_ == Mode::SAT) {
        SATResult sat = OBBOBBSat2D(A, B);

        // 4 분리축을 캔버스 가장자리에 표시 + 각 축의 투영 막대.
        Vector2 origin = {(float)(cx + 60), (float)(cy + 60)};
        float   axisLen = 180.0f;
        for (int i = 0; i < (int)sat.axes.size(); ++i) {
            const auto& ax = sat.axes[i];
            // 축 방향 단위 벡터
            float L = std::sqrt(ax.axis.x * ax.axis.x + ax.axis.y * ax.axis.y);
            if (L < 1e-6f) continue;
            Vector2 dir = {ax.axis.x / L, ax.axis.y / L};

            Vector2 base = {origin.x, origin.y + (float)i * 30.0f};
            // 축 라인
            DrawLineEx(base,
                        {base.x + dir.x * axisLen, base.y + dir.y * axisLen},
                        1.0f, Color{120, 120, 120, 200});

            // 두 도형의 투영 구간을 base 위에 mapping.
            // 단순화를 위해 SAT 결과의 [minA, maxA], [minB, maxB] 의
            // 상대 위치를 axisLen 안에 표현.
            float lo = std::min(ax.minA, ax.minB);
            float hi = std::max(ax.maxA, ax.maxB);
            float span = (hi - lo);
            if (span < 1e-6f) continue;
            auto plot = [&](float v0, float v1, Color c, float yOff) {
                float t0 = (v0 - lo) / span;
                float t1 = (v1 - lo) / span;
                Vector2 p0 = {base.x + dir.x * t0 * axisLen,
                                base.y + dir.y * t0 * axisLen + yOff};
                Vector2 p1 = {base.x + dir.x * t1 * axisLen,
                                base.y + dir.y * t1 * axisLen + yOff};
                DrawLineEx(p0, p1, 4.0f, c);
            };
            plot(ax.minA, ax.maxA, Color{255, 200, 80, 255},  -3.0f);
            plot(ax.minB, ax.maxB, Color{120, 200, 255, 255},  3.0f);

            char buf[64];
            std::snprintf(buf, sizeof(buf), "axis %d  overlap %+.1f%s",
                           i, ax.overlap, ax.separating ? "  SEPARATING" : "");
            DrawText(buf, (int)base.x, (int)base.y + 8, 12,
                       ax.separating ? Color{255, 100, 100, 255}
                                      : Color{200, 200, 200, 255});
        }

        const char* status = sat.intersect ? "INTERSECTING" : "SEPARATED";
        Color sc = sat.intersect ? Color{120, 255, 120, 255}
                                   : Color{255, 100, 100, 255};
        DrawText(status, cx + 10, cy + 10, 18, sc);
    } else {
        // GJK 시각화: 단계별 simplex.
        auto trace = GJK2DTraced(A, B);
        if (gjkStep_ >= (int)trace.size()) gjkStep_ = (int)trace.size() - 1;
        if (gjkStep_ < 0) gjkStep_ = 0;
        const GJKStep& step = trace[(size_t)gjkStep_];

        // Minkowski 차의 원점은 시각화에서 캔버스 가운데에 두고,
        // simplex 점들을 원점 + (Minkowski 좌표) 형태로 그린다.
        // 단순화: simplex 점 자체가 픽셀 좌표.
        Vector2 minkOrigin = {(float)(cx + cw * 0.5f),
                                (float)(cy + ch * 0.5f)};
        DrawCircleV(minkOrigin, 4.0f, Color{255, 255, 255, 255});
        DrawText("origin", (int)minkOrigin.x + 8,
                   (int)minkOrigin.y - 6, 12, WHITE);

        // simplex 그리기
        const auto& S = step.simplex;
        for (size_t i = 0; i < S.size(); ++i) {
            Vector2 p = {minkOrigin.x + S[i].x, minkOrigin.y + S[i].y};
            DrawCircleV(p, 5.0f, Color{255, 220, 80, 255});
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", (int)i);
            DrawText(buf, (int)p.x + 6, (int)p.y - 6, 12, WHITE);
        }
        if (S.size() == 2) {
            DrawLineEx({minkOrigin.x + S[0].x, minkOrigin.y + S[0].y},
                        {minkOrigin.x + S[1].x, minkOrigin.y + S[1].y},
                        2.0f, Color{255, 220, 80, 255});
        } else if (S.size() == 3) {
            for (int i = 0; i < 3; ++i) {
                int j = (i + 1) % 3;
                DrawLineEx({minkOrigin.x + S[i].x, minkOrigin.y + S[i].y},
                            {minkOrigin.x + S[j].x, minkOrigin.y + S[j].y},
                            2.0f, Color{255, 220, 80, 255});
            }
        }

        // 새 search direction 화살표
        Vector2 d = step.direction;
        float L = std::sqrt(d.x * d.x + d.y * d.y);
        if (L > 1e-6f) {
            Vector2 dn = {d.x / L * 60.0f, d.y / L * 60.0f};
            DrawLineEx(minkOrigin,
                        {minkOrigin.x + dn.x, minkOrigin.y + dn.y},
                        2.0f, Color{120, 200, 255, 255});
        }

        char buf[128];
        std::snprintf(buf, sizeof(buf),
                       "GJK step %d / %d   simplex size %d   %s",
                       gjkStep_, (int)trace.size() - 1,
                       (int)S.size(),
                       step.done
                         ? (step.intersect ? "INTERSECT" : "SEPARATED")
                         : "running");
        DrawText(buf, cx + 10, cy + 10, 14,
                   step.done
                     ? (step.intersect ? Color{120, 255, 120, 255}
                                       : Color{255, 100, 100, 255})
                     : Color{200, 200, 200, 255});
    }

    // 핸들
    centerA_.Draw(Color{255, 200, 80, 255}, "A");
    centerB_.Draw(Color{120, 200, 255, 255}, "B");
    rotA_.SetRadius(5.0f);
    rotB_.SetRadius(5.0f);
    rotA_.Draw(Color{200, 150, 50, 255}, "rotA");
    rotB_.Draw(Color{60, 150, 220, 255}, "rotB");
    DrawLineEx(centerA_.Position(), rotA_.Position(), 1.0f,
                Color{200, 150, 50, 150});
    DrawLineEx(centerB_.Position(), rotB_.Position(), 1.0f,
                Color{60, 150, 220, 150});

    (void)cw; (void)ch;
}

void CollisionScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;

    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, "Collision Scene");
    y += 22;

    int modeIdx = (int)mode_;
    GuiToggleGroup(Rectangle{(float)(px + 8), (float)y, 60, 22},
                    "SAT;GJK", &modeIdx);
    mode_ = (Mode)modeIdx;
    y += 30;

    char buf[96];
    std::snprintf(buf, sizeof(buf), "A halfX %.0f", sizeA_x_);
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
               nullptr, nullptr, &sizeA_x_, 20.0f, 200.0f);
    y += 42;
    std::snprintf(buf, sizeof(buf), "A halfY %.0f", sizeA_y_);
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
               nullptr, nullptr, &sizeA_y_, 20.0f, 200.0f);
    y += 42;
    std::snprintf(buf, sizeof(buf), "B halfX %.0f", sizeB_x_);
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
               nullptr, nullptr, &sizeB_x_, 20.0f, 200.0f);
    y += 42;
    std::snprintf(buf, sizeof(buf), "B halfY %.0f", sizeB_y_);
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
               nullptr, nullptr, &sizeB_y_, 20.0f, 200.0f);
    y += 42;

    if (mode_ == Mode::GJK) {
        GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
                     " auto step", &gjkAuto_);
        y += 22;
        if (GuiButton(Rectangle{(float)(px + 8), (float)y, 90, 22}, "Step +")) {
            gjkStep_++;
        }
        if (GuiButton(Rectangle{(float)(px + 100), (float)y, 90, 22}, "Reset")) {
            gjkStep_ = 0;
        }
        y += 28;
    }

    GuiLabel(Rectangle{(float)(px + 8), (float)y, 260, 18},
              "drag center / rot handle");
}

}  // namespace mathlab
