// camera — 화면 좌표와 월드 좌표를 가르는 자리.
//
// 이 게임은 여태 카메라가 없었다. 정확히 말하면 카메라가 **항등 함수**였다 —
// 월드 픽셀 좌표가 곧 화면 픽셀 좌표라, 맵은 480x480 안에 들어오는 크기여야 했다.
// 절차 맵이 41x41 칸이 되면서 칸 하나가 10픽셀로 뭉개졌고, 그 순간 "어디를
// 보는가"가 처음으로 질문이 됐다.
//
// 두 팔을 둔다.
//
//   ortho2d   항등. Project 도 Pick 도 입력을 그대로 돌려준다. 스위치가 생기기
//             전의 동작이고 기본값이다.
//   tilted3d  월드 점 (x, y) 를 지면 (x, y, 0) 으로 올리고 Perspective·LookAt
//             으로 투영한다. 픽킹은 그 역이다 — 화면 점을 역행렬로 풀어 광선을
//             만들고 z=0 평면과 교차시킨다.
//
// 판정은 **왕복 항등**이다. 셀 중심을 투영한 뒤 그 화면 좌표를 다시 픽킹하면
// 같은 셀이 나와야 한다. 이 하나가 투영·역투영·평면 교차를 한꺼번에 잡는다.
//
// 자세는 쿼터니언이 든다. 상방은 월드 +z 로 고정한다(카메라가 롤하지 않는다).
// 그 조합에서 피치가 ±90° 에 닿으면 전방이 상방과 평행해져 LookAt 의
// Cross(forward, up) 가 0 이 되고 뷰 기저가 통째로 NaN 이 된다 — 오일러각 매개화의
// 짐벌락이 카메라에서 드러나는 형태가 정확히 이것이다. ±89° 클램프가 그 각도에
// 닿지 못하게 한다.
//
// 이 파일은 **표시 전용**이다. 시뮬은 카메라를 모르고, 카메라는 시뮬 상태를
// 바꾸지 않는다. 연출(레일)이 상태 해시를 흔들 수 없는 이유가 그 경계다.

#pragma once

#include "game/defense_map.hpp"
#include "game/switches.hpp"
#include "math/interp.hpp"
#include "math/mat.hpp"
#include "math/quat.hpp"
#include "math/scalar.hpp"
#include "math/transform.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

namespace mega {
namespace game {

class DefenseCamera {
public:
    // 짐벌락은 이 각도 너머에서 일어난다. 도달하지 못하게 막는 것이 방어다.
    static constexpr float kMaxPitchDegrees = 89.0f;

    void Configure(CameraKind kind, int viewWidth, int viewHeight) {
        kind_ = kind;
        viewWidth_ = viewWidth > 0 ? viewWidth : 1;
        viewHeight_ = viewHeight > 0 ? viewHeight : 1;
        Update();
    }
    CameraKind Kind() const { return kind_; }
    bool Tilted() const { return kind_ == CameraKind::Tilted3d; }

    // 맵 전체가 화면에 들어오는 자세로 맞춘다.
    void Frame(const DefenseMap& map) {
        const float width = static_cast<float>(map.PixelWidth());
        const float height = static_cast<float>(map.Rows() * map.CellSize());
        focus_ = {width * 0.5f, static_cast<float>(map.OriginY()) + height * 0.5f};
        distance_ = (width > height ? width : height) * 1.25f;
        restFocus_ = focus_;
        restDistance_ = distance_;
        SetYawPitch(0.0f, 58.0f);
    }

    void SetFocus(const math::Vec2& focus) { focus_ = focus; Update(); }
    void SetDistance(float distance) { distance_ = distance > 1.0f ? distance : 1.0f; Update(); }
    // 피치는 여기서만 클램프된다. 밖에서 90 을 넣어도 89 로 잘린다.
    void SetYawPitch(float yawDegrees, float pitchDegrees) {
        yaw_ = yawDegrees;
        pitch_ = math::Clamp(pitchDegrees, -kMaxPitchDegrees, kMaxPitchDegrees);
        Update();
    }
    float Yaw() const { return yaw_; }
    float Pitch() const { return pitch_; }
    float Distance() const { return distance_; }
    math::Vec2 Focus() const { return focus_; }
    const math::Quat& Orientation() const { return orientation_; }
    const math::Mat4& ViewProjection() const { return viewProjection_; }
    math::Vec3 Forward() const { return ForwardOf(orientation_); }
    math::Vec3 Eye() const { return eye_; }
    bool Invertible() const { return invertible_; }

    // 자세 → 기저. 기본 전방은 -y(화면 위쪽을 향한다), 상방은 월드 +z 고정이다.
    // 피치는 **지면 위로 올려본 각**이다: +58° 면 카메라가 맵 아래쪽 위에 떠서
    // 내려다보고, 그래야 y 가 큰 행이 화면 아래에 남아 ortho2d 와 방향이 같다.
    // snippet-begin: camera-orientation
    static math::Quat OrientationOf(float yawDegrees, float pitchDegrees) {
        return math::Quat::FromAxisAngle({0.0f, 0.0f, 1.0f}, math::Radians(yawDegrees)) *
               math::Quat::FromAxisAngle({1.0f, 0.0f, 0.0f}, math::Radians(pitchDegrees));
    }
    static math::Vec3 ForwardOf(const math::Quat& q) {
        return math::Rotate(q, math::Vec3{0.0f, -1.0f, 0.0f});
    }
    // snippet-end: camera-orientation

    // snippet-begin: camera-update
    void Update() {
        orientation_ = hasOverride_ ? override_ : OrientationOf(yaw_, pitch_);
        if (kind_ == CameraKind::Ortho2d) {
            viewProjection_ = math::Mat4::Identity();
            inverse_ = math::Mat4::Identity();
            invertible_ = true;
            return;
        }
        const math::Vec3 forward = ForwardOf(orientation_);
        const math::Vec3 target{focus_.x, focus_.y, 0.0f};
        eye_ = target - forward * distance_;
        const float aspect = static_cast<float>(viewWidth_) / static_cast<float>(viewHeight_);
        projection_ = math::Perspective(math::Radians(50.0f), aspect,
                                        distance_ * 0.05f, distance_ * 4.0f);
        // 상방은 월드 +z. 전방이 여기에 평행해지는 순간 이 행렬은 NaN 이 되고,
        // 그 각도에 닿지 못하게 하는 것이 SetYawPitch 의 클램프다.
        view_ = math::LookAt(eye_, target, math::Vec3{0.0f, 0.0f, 1.0f});
        viewProjection_ = projection_ * view_;
        invertible_ = math::Inverse(viewProjection_, inverse_);
    }
    // snippet-end: camera-update

    // 월드(맵 픽셀) → 화면 픽셀. behind 가 참이면 카메라 뒤라 그리면 안 된다.
    // snippet-begin: camera-project
    math::Vec2 Project(const math::Vec2& world, bool* behind = nullptr) const {
        if (behind) *behind = false;
        if (kind_ == CameraKind::Ortho2d) return world;
        const math::Vec4 clip =
            viewProjection_ * math::Vec4(math::Vec3{world.x, world.y, 0.0f}, 1.0f);
        if (clip.w <= 1e-4f) {
            if (behind) *behind = true;
            return {-1e6f, -1e6f};
        }
        const float inv = 1.0f / clip.w;
        return {(clip.x * inv * 0.5f + 0.5f) * static_cast<float>(viewWidth_),
                (0.5f - clip.y * inv * 0.5f) * static_cast<float>(viewHeight_)};
    }
    // snippet-end: camera-project

    // 화면 픽셀 → 지면 월드. 역행렬이 없거나 광선이 지면과 평행하면 거절한다.
    // snippet-begin: camera-pick
    bool Pick(float screenX, float screenY, math::Vec2& worldOut) const {
        if (kind_ == CameraKind::Ortho2d) {
            worldOut = {screenX, screenY};
            return true;
        }
        if (!invertible_) return false;
        const float ndcX = screenX / static_cast<float>(viewWidth_) * 2.0f - 1.0f;
        const float ndcY = 1.0f - screenY / static_cast<float>(viewHeight_) * 2.0f;
        math::Vec3 nearPoint, farPoint;
        if (!Unproject({ndcX, ndcY, -1.0f}, nearPoint)) return false;
        if (!Unproject({ndcX, ndcY,  1.0f}, farPoint)) return false;
        const math::Vec3 direction = farPoint - nearPoint;
        if (std::fabs(direction.z) < 1e-6f) return false;    // 지면과 평행한 광선
        const float t = -nearPoint.z / direction.z;
        if (t < 0.0f) return false;                          // 지면이 카메라 뒤에 있다
        worldOut = {nearPoint.x + direction.x * t, nearPoint.y + direction.y * t};
        return true;
    }
    // snippet-end: camera-pick

    // 그 자리에 놓인 스프라이트를 몇 배로 그릴 것인가. 원근에서 멀수록 작다.
    float ScaleAt(const math::Vec2& world) const {
        if (kind_ == CameraKind::Ortho2d) return 1.0f;
        bool behind = false;
        const math::Vec2 a = Project(world, &behind);
        if (behind) return 0.0f;
        const math::Vec2 b = Project({world.x + 10.0f, world.y}, &behind);
        if (behind) return 0.0f;
        return math::Length(b - a) * 0.1f;
    }

    // ── 웨이브 시작 레일 ───────────────────────────────────────────
    // 경로 셀을 제어점으로 Catmull-Rom 스플라인을 깔고 그 위를 훑는다. 줌은
    // SmootherStep 으로 이징하고, 자세는 Slerp 로 제자리에 돌아온다. 전부 표시
    // 전용이라 시뮬 상태 해시는 레일이 있든 없든 같아야 한다.
    // snippet-begin: camera-rail
    void BeginRail(const std::vector<math::Vec2>& controls, float seconds) {
        rail_ = controls;
        railSeconds_ = seconds > 0.01f ? seconds : 0.01f;
        railTime_ = 0.0f;
        railActive_ = rail_.size() >= 2;
        restFocus_ = focus_;
        restDistance_ = distance_;
        restOrientation_ = OrientationOf(yaw_, pitch_);
        // 훑기는 요를 25° 틀고 피치를 얕게 눕힌 자세에서 출발한다.
        swungOrientation_ = OrientationOf(yaw_ + 25.0f, pitch_ * 0.7f);
    }
    bool RailActive() const { return railActive_; }
    float RailProgress() const { return railActive_ ? math::Saturate(railTime_ / railSeconds_) : 1.0f; }

    void UpdateRail(float dt) {
        if (!railActive_) return;
        railTime_ += dt;
        const float raw = math::Saturate(railTime_ / railSeconds_);
        const float eased = math::SmootherStep(raw);
        focus_ = SampleRail(eased);
        // 가운데서 가장 가까이 붙었다가(0.55배) 양 끝에서 원래 거리로 돌아온다.
        const float pull = math::SmootherStep(raw < 0.5f ? raw * 2.0f : (1.0f - raw) * 2.0f);
        distance_ = restDistance_ * math::Lerp(1.0f, 0.55f, pull);
        override_ = math::Slerp(swungOrientation_, restOrientation_, eased);
        hasOverride_ = true;
        Update();
        if (raw >= 1.0f) {
            railActive_ = false;
            hasOverride_ = false;
            focus_ = restFocus_;
            distance_ = restDistance_;
            Update();
        }
    }

    // Catmull-Rom 은 제어점 p1..p2 구간만 지난다. 양 끝을 복제해 첫 점과 끝 점을
    // 정확히 통과시킨다 — 그렇지 않으면 레일이 맵 밖으로 튄다.
    math::Vec2 SampleRail(float t) const {
        if (rail_.empty()) return focus_;
        if (rail_.size() == 1) return rail_[0];
        const float scaled = math::Saturate(t) * static_cast<float>(rail_.size() - 1);
        size_t segment = static_cast<size_t>(scaled);
        if (segment + 2 > rail_.size()) segment = rail_.size() - 2;
        const float local = scaled - static_cast<float>(segment);
        const math::Vec2& p1 = rail_[segment];
        const math::Vec2& p2 = rail_[segment + 1];
        const math::Vec2& p0 = segment == 0 ? p1 : rail_[segment - 1];
        const math::Vec2& p3 = segment + 2 < rail_.size() ? rail_[segment + 2] : p2;
        return math::CatmullRom(p0, p1, p2, p3, local);
    }
    // snippet-end: camera-rail

private:
    bool Unproject(const math::Vec3& ndc, math::Vec3& out) const {
        const math::Vec4 v = inverse_ * math::Vec4(ndc, 1.0f);
        if (std::fabs(v.w) < 1e-9f) return false;
        const float inv = 1.0f / v.w;
        out = {v.x * inv, v.y * inv, v.z * inv};
        return true;
    }

    CameraKind kind_ = CameraKind::Ortho2d;
    int        viewWidth_ = 480, viewHeight_ = 480;
    math::Vec2 focus_{240.0f, 240.0f};
    float      distance_ = 520.0f;
    float      yaw_ = 0.0f, pitch_ = 58.0f;
    math::Quat orientation_ = math::Quat::Identity();
    math::Vec3 eye_{0.0f, 0.0f, 0.0f};
    math::Mat4 view_, projection_, viewProjection_, inverse_;
    bool       invertible_ = true;

    std::vector<math::Vec2> rail_;
    math::Vec2 restFocus_{240.0f, 240.0f};
    math::Quat restOrientation_ = math::Quat::Identity();
    math::Quat swungOrientation_ = math::Quat::Identity();
    math::Quat override_ = math::Quat::Identity();
    float      restDistance_ = 520.0f;
    float      railSeconds_ = 1.0f;
    float      railTime_ = 0.0f;
    bool       railActive_ = false;
    bool       hasOverride_ = false;
};

// 엔티티 표시 변환. 지면 위 (x, y, 0) 에 놓고, 화면 평면 안에서 요만큼 돌리고,
// 종류·티어가 정한 배율을 준다. 포탑은 이 변환의 **자식**이라 부모-자식 합성으로
// 총구 위치가 나온다 — 그것이 Transform::operator* 의 제품 소비자다.
// snippet-begin: display-transform
inline math::Transform EntityTransform(const math::Vec2& world, float yawDegrees, float scale) {
    math::Transform transform;
    transform.position = {world.x, world.y, 0.0f};
    transform.rotation = math::Quat::FromAxisAngle({0.0f, 0.0f, 1.0f}, math::Radians(yawDegrees));
    transform.scale = {scale, scale, scale};
    return transform;
}

// 포탑 총구: 타워 로컬 공간의 +y 방향 한 단위. 부모 변환과 합성해 월드로 올린다.
inline math::Transform BarrelTransform(float length) {
    math::Transform barrel;
    barrel.position = {0.0f, length, 0.0f};
    return barrel;
}

// 월드 점이 이 변환의 단위 원 안에 있는가. 역변환으로 로컬 공간에 내린 뒤
// 비교하므로 회전과 배율이 자동으로 반영된다 — 월드 공간에서 반지름 상수로
// 비교하면 배율이 바뀔 때마다 그 상수를 따로 고쳐야 한다.
inline bool TransformContains(const math::Transform& transform, const math::Vec2& world) {
    const math::Vec3 local =
        transform.InverseTransformPoint(math::Vec3{world.x, world.y, 0.0f});
    return local.x * local.x + local.y * local.y <= 1.0f;
}
// snippet-end: display-transform

}  // namespace game
}  // namespace mega
