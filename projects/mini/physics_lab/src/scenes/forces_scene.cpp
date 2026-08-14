#include "scenes/forces_scene.h"

#include "raygui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace physlab {

namespace {

constexpr int kGraphCapacity = 600;

}  // namespace

ForcesScene::ForcesScene() {
    keHist_.Reset(kGraphCapacity);
    peHist_.Reset(kGraphCapacity);
    totalHist_.Reset(kGraphCapacity);
}

void ForcesScene::OnEnter() {
    initialized_  = false;
    pulsePending_ = false;
    keHist_.Clear();
    peHist_.Clear();
    totalHist_.Clear();
}

Vector2 ForcesScene::ComputeForce(Vector2 pos, Vector2 vel) const {
    Vector2 f = {0, 0};
    if (gravityOn_) f.y += mass_ * gravityG_;
    if (springOn_) {
        Vector2 a = anchorHandle_.Position();
        Vector2 d = {pos.x - a.x, pos.y - a.y};
        float   len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len > 1e-4f) {
            float scale = -springK_ * (len - springRest_) / len;
            f.x += scale * d.x;
            f.y += scale * d.y;
        }
    }
    if (dragLinear_ > 0.0f) {
        f.x += -dragLinear_ * vel.x;
        f.y += -dragLinear_ * vel.y;
    }
    if (dragQuad_ > 0.0f) {
        float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
        f.x += -dragQuad_ * speed * vel.x;
        f.y += -dragQuad_ * speed * vel.y;
    }
    return f;
}

void ForcesScene::ApplyImpulse(Vector2 dv) {
    pulsePending_ = true;
    pulseDv_      = dv;
}

void ForcesScene::HandleInput() {
    massHandle_.Update();
    anchorHandle_.Update();
    if (IsKeyPressed(KEY_P)) {
        ApplyImpulse({0, -400.0f});  // 위 방향 임펄스
    }
    if (IsKeyPressed(KEY_R)) {
        // 키 R 으로 위치 / 속도 리셋.
        if (lastCanvasW_ > 0) {
            pos_ = {(float)(lastCanvasX_ + lastCanvasW_ / 2),
                     (float)(lastCanvasY_ + lastCanvasH_ / 2)};
            vel_ = {0, 0};
            massHandle_.SetPosition(pos_);
            keHist_.Clear();
            peHist_.Clear();
            totalHist_.Clear();
        }
    }
}

void ForcesScene::Step(float dt) {
    // 핸들 드래그 = 위치 리셋 (Welcome 패턴).
    if (massHandle_.IsDragging()) {
        pos_ = massHandle_.Position();
        vel_ = {0, 0};
        keHist_.Clear();
        peHist_.Clear();
        totalHist_.Clear();
        return;
    }
    if (anchorHandle_.IsDragging()) {
        // anchor 는 외부 입력으로만 갱신. mass 자체는 그대로 진행.
    }

    // pending impulse 적용.
    if (pulsePending_) {
        vel_.x += pulseDv_.x;
        vel_.y += pulseDv_.y;
        pulsePending_ = false;
    }

    // semi-implicit Euler.
    Vector2 f   = ComputeForce(pos_, vel_);
    vel_.x     += (f.x / mass_) * dt;
    vel_.y     += (f.y / mass_) * dt;
    pos_.x     += vel_.x * dt;
    pos_.y     += vel_.y * dt;

    // 바닥 / 벽 충돌 (단순 응답 — Step 6 에서 본격적인 응답 비교).
    float floorY = (float)(lastCanvasY_ + lastCanvasH_) - 8.0f;
    if (pos_.y + radius_ > floorY) {
        pos_.y = floorY - radius_;
        if (vel_.y > 0) { vel_.y = -vel_.y * 0.5f; }
    }
    float wallL = (float)lastCanvasX_ + 8.0f;
    float wallR = (float)(lastCanvasX_ + lastCanvasW_) - 8.0f;
    if (pos_.x - radius_ < wallL) {
        pos_.x = wallL + radius_;
        if (vel_.x < 0) vel_.x = -vel_.x * 0.5f;
    }
    if (pos_.x + radius_ > wallR) {
        pos_.x = wallR - radius_;
        if (vel_.x > 0) vel_.x = -vel_.x * 0.5f;
    }

    // 에너지 계산. PE_grav 기준선은 캔버스 바닥. PE_spring 은 (1/2) k (len - rest)².
    float floorRef = (float)(lastCanvasY_ + lastCanvasH_);
    float h        = (floorRef - pos_.y) / 100.0f;   // 100 px = 1 m 로 normalize
    float vMs2     = (vel_.x * vel_.x + vel_.y * vel_.y) / (100.0f * 100.0f);
    float KE       = 0.5f * mass_ * vMs2;
    float PE_grav  = gravityOn_ ? (mass_ * (gravityG_ / 100.0f) * h) : 0.0f;
    float PE_spring = 0.0f;
    if (springOn_) {
        Vector2 a   = anchorHandle_.Position();
        Vector2 d   = {pos_.x - a.x, pos_.y - a.y};
        float   len = std::sqrt(d.x * d.x + d.y * d.y);
        float   dx  = (len - springRest_) / 100.0f;
        PE_spring   = 0.5f * springK_ * dx * dx;
    }
    float PE = PE_grav + PE_spring;
    keHist_.Push(KE);
    peHist_.Push(PE);
    totalHist_.Push(KE + PE);

    // mass handle 은 mass 위치 따라감 (드래그 모드 아니면).
    if (!massHandle_.IsDragging()) {
        massHandle_.SetPosition(pos_);
    }
}

void ForcesScene::DrawWorld(int cx, int cy, int cw, int ch) {
    lastCanvasX_ = cx; lastCanvasY_ = cy;
    lastCanvasW_ = cw; lastCanvasH_ = ch;

    if (!initialized_) {
        pos_ = {(float)(cx + cw / 2), (float)(cy + ch / 2)};
        vel_ = {0, 0};
        massHandle_.SetPosition(pos_);
        anchorHandle_.SetPosition({(float)(cx + cw / 4), (float)(cy + 60)});
        initialized_ = true;
    }

    DrawRectangle(cx, cy, cw, ch, Color{20, 22, 28, 255});
    DrawRectangleLines(cx, cy, cw, ch, Color{60, 62, 70, 255});
    int floorY = cy + ch - 8;
    DrawLine(cx + 8, floorY, cx + cw - 8, floorY, Color{120, 130, 140, 255});

    // 스프링.
    if (springOn_) {
        Vector2 a = anchorHandle_.Position();
        DrawLineEx(a, pos_, 2.0f, Color{180, 220, 120, 220});
        DrawCircleV(a, 6.0f, Color{180, 220, 120, 255});
    }

    // 중력 벡터 (mass 아래 화살표).
    if (gravityOn_) {
        Vector2 gtip = {pos_.x, pos_.y + 40.0f};
        DrawLineEx(pos_, gtip, 2.0f, Color{200, 200, 200, 180});
        DrawTriangle(gtip,
                      Vector2{pos_.x - 4, gtip.y - 8},
                      Vector2{pos_.x + 4, gtip.y - 8},
                      Color{200, 200, 200, 180});
        DrawText("g", (int)gtip.x + 6, (int)gtip.y - 4, 12,
                  Color{200, 200, 200, 220});
    }

    // 속도 벡터.
    Vector2 vtip = {pos_.x + vel_.x * 0.1f, pos_.y + vel_.y * 0.1f};
    DrawLineEx(pos_, vtip, 2.0f, Color{120, 200, 255, 220});

    // 강체.
    DrawCircleV(pos_, radius_, Color{255, 200, 80, 255});
    DrawCircleLines((int)pos_.x, (int)pos_.y, radius_,
                     Color{255, 230, 160, 255});

    // 핸들.
    massHandle_.Draw(Color{255, 90, 90, 180}, nullptr);
    anchorHandle_.Draw(Color{120, 220, 120, 180}, "anchor");

    DrawText("Step 3: forces (gravity / spring / drag). P = pulse.",
              cx + 10, cy + 10, 14, Color{160, 160, 160, 255});

    // === 에너지 그래프 — 캔버스 우상단에 작게 ===
    Rectangle gr = {
        (float)(cx + cw - 320),
        (float)(cy + 36),
        300.0f,
        140.0f,
    };
    GraphInput inputs[3] = {
        {&keHist_,    Color{120, 200, 255, 255}, "KE"},
        {&peHist_,    Color{255, 200, 100, 255}, "PE"},
        {&totalHist_, Color{255, 120, 120, 255}, "Total"},
    };
    DrawLineGraph(gr, inputs, 3, "Energy (m,s units)", 0.0f, 1.0f, true);
}

void ForcesScene::DrawPanel(int panelX, int panelY, int panelW, int panelH) {
    (void)panelW; (void)panelH;
    int  y = panelY + 8;
    char buf[160];

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, "Forces scene");
    y += 24;

    GuiCheckBox(Rectangle{(float)(panelX + 8), (float)y, 18, 18},
                 "gravity",      &gravityOn_);
    y += 22;
    GuiCheckBox(Rectangle{(float)(panelX + 8), (float)y, 18, 18},
                 "spring",       &springOn_);
    y += 22;

    std::snprintf(buf, sizeof(buf), "linear drag c1: %.2f", dragLinear_);
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &dragLinear_, 0.0f, 5.0f);
    y += 22;

    std::snprintf(buf, sizeof(buf), "quadratic drag c2: %.3f", dragQuad_);
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &dragQuad_, 0.0f, 0.02f);
    y += 22;

    std::snprintf(buf, sizeof(buf), "spring k: %.2f", springK_);
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &springK_, 1.0f, 40.0f);
    y += 22;

    std::snprintf(buf, sizeof(buf), "spring rest: %.0f px", springRest_);
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &springRest_, 0.0f, 300.0f);
    y += 22;

    std::snprintf(buf, sizeof(buf), "gravity g: %.0f px/s2", gravityG_);
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &gravityG_, 0.0f, 1500.0f);
    y += 28;

    if (GuiButton(Rectangle{(float)(panelX + 8), (float)y, 100, 22},
                   "Reset (R)")) {
        pos_ = {(float)(lastCanvasX_ + lastCanvasW_ / 2),
                 (float)(lastCanvasY_ + lastCanvasH_ / 2)};
        vel_ = {0, 0};
        massHandle_.SetPosition(pos_);
        keHist_.Clear();
        peHist_.Clear();
        totalHist_.Clear();
    }
    if (GuiButton(Rectangle{(float)(panelX + 116), (float)y, 100, 22},
                   "Pulse (P)")) {
        ApplyImpulse({0, -400.0f});
    }
    y += 28;

    // 상태 라벨.
    Vector2 f = ComputeForce(pos_, vel_);
    std::snprintf(buf, sizeof(buf), "pos: (%.0f, %.0f)", pos_.x, pos_.y);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 18;
    std::snprintf(buf, sizeof(buf), "vel: (%.0f, %.0f) px/s",
                   vel_.x, vel_.y);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 18;
    std::snprintf(buf, sizeof(buf), "F net: (%.1f, %.1f)", f.x, f.y);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 22;

    std::snprintf(buf, sizeof(buf), "Total energy: %.3f",
                   totalHist_.Latest());
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 22;

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "Conservative (g, spring) only:");
    y += 16;
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "Total stays ~flat. Drag drains it.");
}

}  // namespace physlab
