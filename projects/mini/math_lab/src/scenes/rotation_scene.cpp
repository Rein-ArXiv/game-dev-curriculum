#include "scenes/rotation_scene.h"

#include "algo/quaternion.h"
#include "raygui.h"
#include "raymath.h"
#include "rlgl.h"

#include <cmath>
#include <cstdio>

namespace mathlab {

namespace {

constexpr float kPi = 3.14159265358979f;

void DrawArrow3D(Vector3 from, Vector3 to, Color color, float thick) {
    DrawCylinderEx(from, to, thick, thick, 8, color);
}

}  // namespace

void RotationScene::OnEnter() {
    // 회전 효과를 잘 보이려면 카메라가 약간 위에서 비스듬히 본다.
    camera_.position   = {3.5f, 3.0f, 3.5f};
    camera_.target     = {0, 0, 0};
    camera_.up         = {0, 1, 0};
    camera_.fovy       = 50.0f;
    camera_.projection = CAMERA_PERSPECTIVE;
    cameraReady_       = true;
}

void RotationScene::Update(float dt) {
    if (mode_ == Mode::SlerpVsNlerp && autoPlay_) {
        tParam_ += dt * 0.4f * (float)direction_;
        if (tParam_ > 1.0f) { tParam_ = 1.0f; direction_ = -1; }
        if (tParam_ < 0.0f) { tParam_ = 0.0f; direction_ =  1; }
    }
}

void RotationScene::DrawAxesAndCube(Quaternion q, Vector3 offset,
                                       float scale, Color tint) const {
    // 회전된 축을 화살표로. world 축 = q 가 적용된 단위 벡터들.
    // snippet-begin: axis-triad
    Vector3 ex = QuatRotateVec(q, {scale, 0, 0});
    Vector3 ey = QuatRotateVec(q, {0, scale, 0});
    Vector3 ez = QuatRotateVec(q, {0, 0, scale});
    DrawArrow3D(offset, Vector3Add(offset, ex), Color{255, 90, 90, 255},  0.04f);
    DrawArrow3D(offset, Vector3Add(offset, ey), Color{120, 255, 120, 255}, 0.04f);
    DrawArrow3D(offset, Vector3Add(offset, ez), Color{120, 160, 255, 255}, 0.04f);
    // snippet-end: axis-triad

    // 큐브: 기본 행렬을 quaternion 의 행렬로 대체해 그린다.
    // snippet-begin: cube-matrix
    rlPushMatrix();
    rlTranslatef(offset.x, offset.y, offset.z);
    Matrix m = QuatToMatrix(q);
    float t[16] = {
        m.m0,  m.m1,  m.m2,  m.m3,
        m.m4,  m.m5,  m.m6,  m.m7,
        m.m8,  m.m9,  m.m10, m.m11,
        m.m12, m.m13, m.m14, m.m15,
    };
    rlMultMatrixf(t);
    Vector3 size = {scale * 0.9f, scale * 0.55f, scale * 0.35f};
    DrawCubeV({0, 0, 0}, size, tint);
    DrawCubeWiresV({0, 0, 0}, size, Color{220, 220, 220, 200});
    rlPopMatrix();
    // snippet-end: cube-matrix
}

void RotationScene::DrawWorld(int cx, int cy, int cw, int ch) {
    if (!cameraReady_) OnEnter();

    // 캔버스 영역에 3D 그리려면 BeginMode3D 의 viewport 가 전체 화면 — 약간의 트릭.
    // 단순화를 위해 화면 전체에 그리고 캔버스 영역만 자른 듯한 효과는
    // 별도 RenderTexture 가 필요. 현재는 캔버스 영역 위에 직접 그리고
    // 패널 영역은 패널 그리기로 덮인다.
    BeginMode3D(camera_);
    DrawGrid(8, 0.5f);

    if (mode_ == Mode::Euler) {
        Quaternion q = QuatFromEulerXYZ(pitch_, yaw_, roll_);
        DrawAxesAndCube(q, {0, 0, 0}, 1.0f, Color{220, 200, 120, 220});
    } else {
        Quaternion qa = QuatFromEulerXYZ(keyA_pitch_, keyA_yaw_, keyA_roll_);
        Quaternion qb = QuatFromEulerXYZ(keyB_pitch_, keyB_yaw_, keyB_roll_);
        Quaternion qSlerp = QuatSlerp(qa, qb, tParam_);
        Quaternion qNlerp = QuatNlerp(qa, qb, tParam_);

        // 키 A / B 는 양 옆에 작게.
        DrawAxesAndCube(qa, {-2.2f, 0, 0}, 0.5f, Color{120, 200, 255, 200});
        DrawAxesAndCube(qb, { 2.2f, 0, 0}, 0.5f, Color{255, 200, 80, 200});

        // 중앙: slerp (위), nlerp (아래) 두 큐브.
        DrawAxesAndCube(qSlerp, {0,  0.7f, 0}, 0.6f, Color{180, 255, 180, 220});
        DrawAxesAndCube(qNlerp, {0, -0.7f, 0}, 0.6f, Color{255, 180, 180, 220});
    }
    EndMode3D();

    // 캔버스 안에만 텍스트 (3D viewport 는 화면 전체였음).
    DrawText(mode_ == Mode::Euler
                ? "Mode: Euler XYZ - gimbal lock at yaw ~ ±90° (middle axis)"
                : "Mode: Slerp (green, top) vs Nlerp (red, bottom)",
              cx + 10, cy + 10, 14, Color{200, 200, 200, 255});

    // snippet-begin: gimbal-warning
    if (mode_ == Mode::Euler && EulerNearGimbalLock(yaw_)) {
        DrawText("GIMBAL LOCK - pitch and roll collapsed onto same axis",
                  cx + 10, cy + 30, 14, Color{255, 100, 100, 255});
    }
    // snippet-end: gimbal-warning
    (void)cw; (void)ch;
}

void RotationScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;

    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, "Rotation Scene");
    y += 22;

    // 모드 토글
    // snippet-begin: mode-toggle
    int   modeIdx  = (mode_ == Mode::Euler) ? 0 : 1;
    int   newMode  = GuiToggleGroup(Rectangle{(float)(px + 8), (float)y, 130, 22},
                                     "Euler;Slerp/Nlerp", &modeIdx);
    (void)newMode;
    mode_ = (modeIdx == 0) ? Mode::Euler : Mode::SlerpVsNlerp;
    // snippet-end: mode-toggle
    y += 30;

    char buf[128];
    if (mode_ == Mode::Euler) {
        std::snprintf(buf, sizeof(buf), "pitch %5.1f deg", pitch_ * 57.2958f);
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
        GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
                   nullptr, nullptr, &pitch_, -kPi, kPi);
        y += 42;
        std::snprintf(buf, sizeof(buf), "yaw   %5.1f deg", yaw_ * 57.2958f);
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
        GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
                   nullptr, nullptr, &yaw_, -kPi, kPi);
        y += 42;
        std::snprintf(buf, sizeof(buf), "roll  %5.1f deg", roll_ * 57.2958f);
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
        GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
                   nullptr, nullptr, &roll_, -kPi, kPi);
        y += 42;

        Quaternion q = QuatFromEulerXYZ(pitch_, yaw_, roll_);
        std::snprintf(buf, sizeof(buf),
                       "q = (%.2f, %.2f, %.2f, %.2f)",
                       q.x, q.y, q.z, q.w);
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 260, 18}, buf);
        y += 18;
        std::snprintf(buf, sizeof(buf), "|q| = %.4f (should be 1)", QuatNorm(q));
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 260, 18}, buf);
    } else {
        std::snprintf(buf, sizeof(buf), "t = %.2f", tParam_);
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
        GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
                   nullptr, nullptr, &tParam_, 0.0f, 1.0f);
        y += 42;
        GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
                     " auto play", &autoPlay_);
        y += 28;
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 260, 18}, "Key A pitch/yaw/roll:");
        y += 18;
        GuiSlider(Rectangle{(float)(px + 8), (float)y, 220, 14},
                   nullptr, nullptr, &keyA_pitch_, -kPi, kPi); y += 16;
        GuiSlider(Rectangle{(float)(px + 8), (float)y, 220, 14},
                   nullptr, nullptr, &keyA_yaw_,   -kPi, kPi); y += 16;
        GuiSlider(Rectangle{(float)(px + 8), (float)y, 220, 14},
                   nullptr, nullptr, &keyA_roll_,  -kPi, kPi); y += 24;
        GuiLabel(Rectangle{(float)(px + 8), (float)y, 260, 18}, "Key B pitch/yaw/roll:");
        y += 18;
        GuiSlider(Rectangle{(float)(px + 8), (float)y, 220, 14},
                   nullptr, nullptr, &keyB_pitch_, -kPi, kPi); y += 16;
        GuiSlider(Rectangle{(float)(px + 8), (float)y, 220, 14},
                   nullptr, nullptr, &keyB_yaw_,   -kPi, kPi); y += 16;
        GuiSlider(Rectangle{(float)(px + 8), (float)y, 220, 14},
                   nullptr, nullptr, &keyB_roll_,  -kPi, kPi); y += 16;
    }
}

}  // namespace mathlab
