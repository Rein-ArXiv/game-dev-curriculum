// Step 2: Rotation Scene.
//   Mode A: Euler XYZ 슬라이더 + 짐벌락 시각화 (가운데 축 yaw ≈ ±90° 일 때).
//   Mode B: 두 keyframe 사이 Slerp vs Nlerp 비교 (t 슬라이더).
// 3D 큐브 + 좌표축 triad 를 raylib 의 Camera3D 로 렌더.

#pragma once

#include "raylib.h"
#include "scenes/iscene.h"

namespace mathlab {

class RotationScene : public IScene {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) override;
    void DrawPanel(int panelX, int panelY, int panelW, int panelH) override;
    std::string_view Name() const override { return "Rotation"; }

private:
    enum class Mode { Euler, SlerpVsNlerp };

    Mode    mode_   = Mode::Euler;
    Camera3D camera_{};
    bool    cameraReady_ = false;

    // Mode::Euler 상태
    float   pitch_ = 0.0f;     // X (radian)
    float   yaw_   = 0.0f;     // Y
    float   roll_  = 0.0f;     // Z

    // Mode::SlerpVsNlerp 상태
    float   keyA_pitch_ = -0.6f, keyA_yaw_ = -0.8f, keyA_roll_ = 0.0f;
    float   keyB_pitch_ =  0.6f, keyB_yaw_ =  0.9f, keyB_roll_ = 0.5f;
    float   tParam_     = 0.0f;
    bool    autoPlay_   = true;
    int     direction_  = 1;   // tParam_ 진행 방향

    void DrawAxesAndCube(Quaternion q, Vector3 offset, float scale, Color tint) const;
};

}  // namespace mathlab
