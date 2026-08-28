# 14. 카메라가 생긴다 — 투영, 픽킹, 자세, 레일

## 들어가며

13편까지 이 게임에는 카메라가 없었다. 정확히 말하면 **카메라가 항등 함수였다.**

```cpp
const math::Vec2 mouse{static_cast<float>(frame.mouseX),
                       static_cast<float>(frame.mouseY)};
```

마우스의 화면 좌표를 그대로 월드 좌표로 썼다. 그림도 같은 가정 위에 있었다 — `map.CellCenter(x, y)` 가 내는 픽셀 좌표에 `render::FillRect` 로 사각형을 채우고, 스프라이트를 그 좌표에 blit 했다. 화면과 월드가 같은 공간이었다.

그 가정에는 대가가 하나 있다. **맵이 480×480 안에 들어와야 한다.** 16×15 칸 × 30픽셀은 정확히 맞았다.

13편이 그 조건을 깼다. 41×41 칸을 480픽셀에 담으면 칸 하나가 **10픽셀**이다. 타워 스프라이트가 뭉개지고, 광맥과 보통 칸의 색 차이가 한 픽셀 줄에 갇힌다. 그 순간 "어디를 얼마나 크게 보는가"가 처음으로 질문이 됐다.

이 편은 그 질문에 답하는 물건을 만든다. 네 헤더가 들어온다.

| 헤더 | 하는 일 |
|---|---|
| `math/mat.hpp` | `Perspective` · `LookAt` 로 지면을 투영하고, `Inverse` 로 픽킹을 되돌린다 |
| `math/quat.hpp` | 카메라 자세를 쿼터니언으로 든다. 짐벌락이 여기서 실물로 나타난다 |
| `math/transform.hpp` | 엔티티 표시 TRS. 포탑이 몸통의 **자식**이 된다 |
| `math/interp.hpp` | 웨이브 시작 카메라 레일(Catmull-Rom · SmootherStep · Slerp) |

판정은 하나다. **왕복 항등** — 셀 중심을 투영한 화면 좌표를 다시 픽킹하면 같은 셀이 나와야 한다. 요 4종 × 피치 3종의 열두 자세에서 표본이 전부 같은 셀로 돌아오고(화면 밖으로 나간 표본 0), 월드 좌표 오차의 최댓값은 **0.5픽셀 미만**이다.

그리고 옛 경로는 지우지 않았다. `camera=ortho2d` 가 기본값이고, 그 팔에서 `Project` 와 `Pick` 은 입력을 **그대로** 돌려준다.

---

## 목차
1. [항등도 카메라다](#1-항등도-카메라다)
2. [지면을 z=0 평면으로](#2-지면을-z0-평면으로)
3. [자세: 쿼터니언과 ±89°](#3-자세-쿼터니언과-89)
4. [짐벌락은 문서가 아니라 NaN 으로 온다](#4-짐벌락은-문서가-아니라-nan-으로-온다)
5. [픽킹: 역행렬과 평면 교차](#5-픽킹-역행렬과-평면-교차)
6. [타일은 더 이상 사각형이 아니다](#6-타일은-더-이상-사각형이-아니다)
7. [표시 변환 — 포탑은 몸통의 자식이다](#7-표시-변환--포탑은-몸통의-자식이다)
8. [선택 판정이 배율을 따라간다](#8-선택-판정이-배율을-따라간다)
9. [레일: 표시 전용이라는 주장을 증명하는 법](#9-레일-표시-전용이라는-주장을-증명하는-법)
10. [헤드리스 스크립트가 화면을 클릭한다](#10-헤드리스-스크립트가-화면을-클릭한다)
11. [변이: 원근 나눗셈을 지운다](#11-변이-원근-나눗셈을-지운다)
12. [실행과 관측](#12-실행과-관측)

---

## 1. 항등도 카메라다

스위치 규약 2번은 "기본값은 언제나 스위치가 생기기 전의 동작"이다. 카메라에서 그 말은 **`camera=ortho2d` 에서 화면 좌표가 곧 월드 좌표여야 한다**는 뜻이다.

그것을 "카메라를 안 쓴다"로 표현할 수도 있었다. `if (tilted) { ... } else { ... }` 를 렌더러와 입력 처리 곳곳에 두는 방식이다. 그러지 않았다 — 분기가 열 군데면 아홉 군데만 고친 상태가 조용히 생긴다.

대신 **항등 카메라**를 만들었다.

<!-- snippet: include/game/camera.hpp@camera-project -->
```cpp
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
```

첫 줄이 전부다. ortho2d 팔은 인자를 그대로 반환한다 — 부동소수 연산이 하나도 끼어들지 않으므로 **비트 동일**이 보장된다. `ScaleAt` 도 같은 방식으로 상수 1을 돌려준다.

그래서 그리기 코드는 분기를 갖지 않는다.

<!-- snippet: include/game/defense_view.hpp@blit-through-camera -->
```cpp
inline void BlitThroughCamera(render::Framebuffer& framebuffer, const DefenseCamera& camera,
                              const render::Sprite& sprite, const math::Vec2& world, int scale) {
    bool behind = false;
    const math::Vec2 screen = camera.Project(world, &behind);
    if (behind) return;
    const float zoom = camera.ScaleAt(world);
    const int scaled = std::max(1, static_cast<int>(std::lround(scale * zoom)));
    BlitCentered(framebuffer, sprite, screen, scaled);
}
```

`camera=ortho2d` 에서 이 함수는 `screen == world`, `zoom == 1`, `scaled == scale` 이라 13편의 `BlitCentered(framebuffer, sprite, position, scale)` 와 완전히 같은 호출이 된다.

## 2. 지면을 z=0 평면으로

2D 게임에 3D 카메라를 붙일 때 첫 결정은 좌표 규약이다.

이 저장소는 **Z-up** 을 골랐다. 월드 점 (x, y) 는 지면 위의 (x, y, 0) 이고, 상방은 +z 다.

이유는 세 가지다.

1. **2D 좌표가 그대로 x, y 로 간다.** (x, 0, y) 로 매립하는 Y-up 규약이면 맵의 y 와 3D 의 z 가 뒤바뀌어, 코드를 읽을 때마다 축을 머릿속에서 한 번 돌려야 한다.
2. **정적 기하와 규약이 같다.** 15편의 BVH 는 바위 칸을 z∈[0,1] 인 `Aabb3` 로 매립한다. 같은 규약이라 카메라와 질의가 같은 좌표를 말한다.
3. **지면이 평면 z=0 이다.** 픽킹의 교차 대상이 `z = 0` 이라는 한 줄로 끝난다.

`math/mat.hpp` 의 `Perspective` 와 `LookAt` 은 상방 벡터를 인자로 받으므로 규약에 중립이다. 그래서 헤더를 고치지 않고 그대로 썼다.

## 3. 자세: 쿼터니언과 ±89°

카메라 자세는 요와 피치 두 각으로 조작하고, 내부에서는 쿼터니언으로 든다.

<!-- snippet: include/game/camera.hpp@camera-orientation -->
```cpp
static math::Quat OrientationOf(float yawDegrees, float pitchDegrees) {
    return math::Quat::FromAxisAngle({0.0f, 0.0f, 1.0f}, math::Radians(yawDegrees)) *
           math::Quat::FromAxisAngle({1.0f, 0.0f, 0.0f}, math::Radians(pitchDegrees));
}
static math::Vec3 ForwardOf(const math::Quat& q) {
    return math::Rotate(q, math::Vec3{0.0f, -1.0f, 0.0f});
}
```

기본 전방이 **−y** 인 이유가 하나 있다. 이 게임의 화면 y 는 아래로 자란다(맵 0행이 화면 위). 기본 전방을 +y 로 두면 카메라가 맵 위쪽에서 아래를 향하게 되고, 그러면 **0행이 화면 아래에 그려진다** — ortho2d 와 상하가 뒤집힌 그림이 나온다. 전방을 −y 로 두고 피치를 "지면 위로 올려본 각"으로 정의하면(+58°가 기본), 카메라가 맵 아래쪽 위에 떠서 위를 내려다보고 두 팔의 방향이 일치한다.

처음 구현은 +y 를 기본 전방으로 두었고, 그 상하 반전은 테스트가 먼저 잡았다 — `TestScaleFallsWithDistance` 가 "아래 행이 더 가깝다"를 단언했는데 반대가 나왔다.

행렬 조립은 이렇다.

<!-- snippet: include/game/camera.hpp@camera-update -->
```cpp
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
    // 상방은 월드 +z. 전방이 여기에 평행해지는 순간 이 행렬은 특이(singular)가 되고,
    // 그 각도에 닿지 못하게 하는 것이 SetYawPitch 의 클램프다.
    view_ = math::LookAt(eye_, target, math::Vec3{0.0f, 0.0f, 1.0f});
    viewProjection_ = projection_ * view_;
    invertible_ = math::Inverse(viewProjection_, inverse_);
}
```

근·원 평면을 거리에 비례시킨 것(`distance_ * 0.05f`, `distance_ * 4.0f`)은 줌 인 상태에서 지면이 근평면 뒤로 잘리지 않게 하려는 것이다. 고정 상수로 두면 레일이 카메라를 0.55배까지 당길 때 지면 앞쪽이 사라진다.

`Inverse` 의 반환값을 `invertible_` 에 담아 두는 것도 중요하다. 역행렬이 없는 자세에서 픽킹이 **성공을 가장하면** 커서가 맵 어딘가를 찍고 있는 것처럼 보인다.

## 4. 짐벌락은 문서가 아니라 NaN 으로 온다

피치는 ±89°로 잘린다.

```cpp
void SetYawPitch(float yawDegrees, float pitchDegrees) {
    yaw_ = yawDegrees;
    pitch_ = math::Clamp(pitchDegrees, -kMaxPitchDegrees, kMaxPitchDegrees);
    Update();
}
```

그 클램프가 막고 있는 것이 정확히 무엇인지가 이 절의 주제다. 두 가지가 같은 각도에서 일어난다.

**(a) LookAt 의 기저가 무너진다.** 상방을 월드 +z 로 고정한 상태에서 피치가 ±90°가 되면 전방이 (0, 0, ∓1) 이 되어 상방과 평행해진다. `LookAt` 의 첫 줄이 `Normalize(Cross(f, up))` 인데, 평행한 두 벡터의 외적은 0 벡터다. `math::Normalize` 는 길이가 `kEpsilon` 이하면 **0 벡터를 돌려주므로**, 뷰 행렬의 우측 축이 통째로 0 이 되고 그 행렬은 특이(singular)가 된다.

```cpp
const math::Vec3 forward = math::Normalize(target - eye);
const math::Vec3 right = math::Cross(forward, math::Vec3{0.0f, 0.0f, 1.0f});
CHECK(math::Length(right) < 1e-6f);
const math::Mat4 degenerate = math::LookAt(eye, target, {0.0f, 0.0f, 1.0f});
math::Mat4 unused;
CHECK(!math::Inverse(degenerate, unused));
```

`Inverse` 가 거짓을 돌려준다 = 그 자세에서는 **픽킹 자체가 불가능하다.**

**(b) 오일러각 매개화가 자유도를 잃는다.** 이것이 짐벌락의 본래 정의다. 피치 90°에서는 요 회전과 롤 회전이 **같은 회전**이 된다.

```cpp
const math::Quat a = math::Quat::FromEuler(math::Radians(90.0f), math::Radians(30.0f), 0.0f);
const math::Quat b = math::Quat::FromEuler(math::Radians(90.0f), 0.0f, math::Radians(-30.0f));
const math::Vec3 probe{1.0f, 2.0f, 3.0f};
const math::Vec3 ra = math::Rotate(a, probe);
const math::Vec3 rb = math::Rotate(b, probe);
CHECK(math::Length(ra - rb) < 1e-4f);    // 두 축이 같은 회전을 낸다 = 짐벌락
```

요를 30° 돌린 것과 롤을 −30° 돌린 것이 같은 점으로 간다. 축 하나가 사라진 것이다. 89°에서는 아직 구분된다 — 같은 테스트가 그 대조도 함께 단언한다.

**쿼터니언을 쓰면 짐벌락이 사라지는가.** 아니다. 쿼터니언 자체는 특이점이 없지만, **요·피치라는 두 각으로 자세를 조작하는 순간** 그 매개화의 특이점이 되돌아온다. 쿼터니언이 주는 것은 다른 것이다 — 누적 회전이 어긋나지 않고(재직교화가 필요 없고), 보간이 일정 각속도로 부드럽다(§9). 특이점을 피하는 것은 클램프의 몫이다.

## 5. 픽킹: 역행렬과 평면 교차

투영의 역이 픽킹이다. 화면 픽셀을 NDC 로 바꾸고, 근·원 평면의 두 점을 역투영해 광선을 만들고, 그 광선을 지면 z=0 과 교차시킨다.

<!-- snippet: include/game/camera.hpp@camera-pick -->
```cpp
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
```

거절 조건이 셋이다. 역행렬 없음, 광선이 지면과 평행, 교차점이 카메라 뒤. 셋 다 "실패했지만 숫자는 나왔다"가 가능한 자리라, 반환값 없이 좌표만 돌려주면 호출자가 구분할 수 없다.

`Unproject` 가 `w` 로 나누는 것이 원근 나눗셈의 역이다.

```cpp
bool Unproject(const math::Vec3& ndc, math::Vec3& out) const {
    const math::Vec4 v = inverse_ * math::Vec4(ndc, 1.0f);
    if (std::fabs(v.w) < 1e-9f) return false;
    const float inv = 1.0f / v.w;
    out = {v.x * inv, v.y * inv, v.z * inv};
    return true;
}
```

**왕복 항등이 판정인 이유.** 투영이 틀렸는지, 역투영이 틀렸는지, 평면 교차가 틀렸는지를 따로 시험하면 세 테스트가 필요하고 각각이 서로의 오류를 가릴 수 있다. 셋을 연달아 태워 원점으로 돌아오는지 보면, 어느 하나라도 틀린 순간 항등이 깨진다.

프레임 루프 쪽 소비자는 이렇다.

<!-- snippet: include/game/defense_app.hpp@mouse-pick -->
```cpp
math::Vec2 mouse{-100.0f, -100.0f};
const bool onGround =
    frame.mouseY >= map_.OriginY() &&
    camera_.Pick(static_cast<float>(frame.mouseX),
                 static_cast<float>(frame.mouseY), mouse);
```

HUD 영역(`mouseY < OriginY()`)은 **화면 좌표로** 판정한다. HUD 는 지면이 아니라 화면에 붙어 있기 때문이다. 그 아래에서만 픽킹을 시도한다.

## 6. 타일은 더 이상 사각형이 아니다

기울인 시점에서 타일은 축 정렬 사각형이 아니다. `render::FillRect` 로는 그릴 수 없다.

`render::FillTriangle` 은 1편부터 있었고, 지금까지 제품이 부른 적이 없었다. 여기서 처음 부른다.

<!-- snippet: include/game/defense_view.hpp@draw-tile-quad -->
```cpp
inline void DrawTileQuad(render::Framebuffer& framebuffer, const DefenseCamera& camera,
                         const DefenseMap& map, int x, int y, render::Color color) {
    const float cell = static_cast<float>(map.CellSize());
    const float px = static_cast<float>(x) * cell;
    const float py = static_cast<float>(map.OriginY()) + static_cast<float>(y) * cell;
    bool behind = false;
    math::Vec2 corner[4] = {
        camera.Project({px, py}, &behind),
        camera.Project({px + cell, py}, &behind),
        camera.Project({px + cell, py + cell}, &behind),
        camera.Project({px, py + cell}, &behind),
    };
    if (behind) return;
    auto ix = [](float v) { return static_cast<int>(std::lround(v)); };
    render::FillTriangle(framebuffer, ix(corner[0].x), ix(corner[0].y),
                         ix(corner[1].x), ix(corner[1].y),
                         ix(corner[2].x), ix(corner[2].y), color);
    render::FillTriangle(framebuffer, ix(corner[0].x), ix(corner[0].y),
                         ix(corner[2].x), ix(corner[2].y),
                         ix(corner[3].x), ix(corner[3].y), color);
}
```

네 꼭짓점 중 하나라도 카메라 뒤면 통째로 건너뛴다. 원근 나눗셈이 뒤집힌 좌표를 내므로, 그 상태에서 삼각형을 채우면 화면 반대편에 쐐기가 그려진다.

비용은 정직하게 크다(§12). 소프트웨어 래스터라이저가 1681개 쿼드 × 2삼각형을 매 프레임 채운다.

## 7. 표시 변환 — 포탑은 몸통의 자식이다

`math/transform.hpp` 는 TRS 하나와 부모∘자식 합성(`operator*`)을 준다. 그것이 제품에서 무엇을 하는가.

**타워에 포탑이 생긴다.** 몸통은 위치·요·배율을 갖고, 총구는 몸통의 로컬 +y 방향 한 단위다. 총구의 월드 좌표는 부모 곱하기 자식이다.

<!-- snippet: include/game/camera.hpp@display-transform -->
```cpp
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
```

그리기 쪽:

```cpp
const math::Transform body =
    TowerTransform(tower, view, static_cast<float>(map.CellSize()));
const math::Transform muzzle = body * BarrelTransform(1.0f);
const math::Vec2 from = camera.Project(tower.pos);
const math::Vec2 to = camera.Project({muzzle.position.x, muzzle.position.y});
render::DrawLine(framebuffer, ..., {225, 215, 190, 210});
```

`BarrelTransform(1.0f)` 의 1은 **로컬 단위**다. 몸통의 배율이 티어에 따라 커지면 총구도 같은 비율로 길어진다 — 배율 곱이 합성에 들어 있기 때문이다. 그 곱을 `Transform::operator*` 에서 빼면 총구 길이가 티어와 무관해지고, `tests/test_transform_chain.cpp` 의 세 번째 블록이 그것을 잡는다.

**요는 어디서 오는가.** 권위 상태가 아니라 **보이는 것**에서 온다.

<!-- snippet: include/game/defense_view.hpp@tower-facing -->
```cpp
inline float TowerFacing(const wire::Snapshot& view, const math::Vec2& towerPos) {
    const math::Vec2* nearest = nullptr;
    float best = 0.0f;
    for (const auto& enemy : view.enemies) {
        const float d = math::LengthSq(enemy.pos - towerPos);
        if (!nearest || d < best) { best = d; nearest = &enemy.pos; }
    }
    if (!nearest) return 0.0f;
    const math::Vec2 d = *nearest - towerPos;
    return math::Degrees(std::atan2(d.x, d.y)) * -1.0f;   // 로컬 +y 가 총구 방향
}

inline math::Transform TowerTransform(const wire::TowerState& tower,
                                      const wire::Snapshot& view, float cellSize) {
    const float scale = cellSize * (0.35f + 0.06f * static_cast<float>(tower.tier));
    return EntityTransform(tower.pos, TowerFacing(view, tower.pos), scale);
}
```

스냅샷만 읽으므로 호스트와 클라이언트가 같은 각을 얻는다. 타워가 실제로 조준한 대상을 와이어에 실을 수도 있었지만, 그러면 타워당 4바이트가 늘고 스냅샷 예산이 그만큼 준다 — 표시용 각도가 살 값이 아니다. 대신 "가장 가까운 적"이라는 근사를 쓴다. 사거리 밖 적을 향해 총구가 돌아 있을 수 있고, 그것이 이 근사의 눈에 보이는 오차다.

## 8. 선택 판정이 배율을 따라간다

타워를 클릭해 고르는 판정도 변환을 지난다.

<!-- snippet: include/game/defense_view.hpp@tower-at -->
```cpp
inline const wire::TowerState* TowerAt(const wire::Snapshot& view,
                                       const math::Vec2& position, float radius) {
    for (const auto& tower : view.towers) {
        const math::Transform body = EntityTransform(tower.pos, 0.0f, radius);
        if (TransformContains(body, position)) return &tower;
    }
    return nullptr;
}
```

`TransformContains` 는 월드 점을 **역변환으로 로컬에 내린 뒤** 단위 원과 비교한다. 배율 `radius` 로 만든 변환에서 그 판정은 반지름 `radius` 의 원과 정확히 같다 — 그래서 이 교체는 13편까지의 동작과 비트 동일하다.

같지 않게 만들 수도 있었다. 초안은 단위 **상자**와 비교했고, 그러면 판정 영역이 원보다 넓어져 클릭 라우팅이 달라진다. 셀프테스트가 그 차이를 잡기 전에 되돌렸다 — 기본값이 달라지는 변경은 스위치가 아니라 변경이다.

역변환을 쓰는 값은 **회전과 비균일 배율이 들어올 때** 나온다. 지금은 요가 0이고 배율이 균일해서 원과 같지만, 티어별 몸통 크기가 판정에 반영되어야 한다는 관계는 이제 코드가 들고 있다 — 상수 하나를 따로 고칠 자리가 없다.

## 9. 레일: 표시 전용이라는 주장을 증명하는 법

웨이브가 바뀌면 카메라가 경로를 훑고 돌아온다.

<!-- snippet: include/game/camera.hpp@camera-rail -->
```cpp
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
```

`math/interp.hpp` 의 셋이 각각 다른 일을 한다.

- **`CatmullRom`** — 경로 셀을 제어점으로 지나는 곡선. 제어점을 **정확히 통과**하는 성질이 필요했다(Bezier 는 중간 제어점을 지나지 않는다).
- **`SmootherStep`** — 진행도 이징. 양 끝에서 속도와 가속도가 0이라 시작·끝이 튀지 않는다.
- **`Slerp`** — 자세 보간. 최단 호를 고른다. `Dot < 0` 인 쌍(같은 회전의 반대 부호)에서 장반 호로 돌면 카메라가 한 바퀴 돌아온다.

양 끝 제어점 복제는 Catmull-Rom 의 정의에서 나오는 필연이다. 이 곡선은 p1→p2 구간만 그리고 접선을 p0, p3 에서 유추하므로, 복제하지 않으면 첫 구간과 끝 구간이 없다.

**표시 전용이라는 주장은 어떻게 증명하는가.** 문서에 적어서는 증명되지 않는다. 같은 명령열을 레일과 함께, 레일 없이 돌려 **상태 해시가 같아야 한다.**

```cpp
CHECK_EQ(play(false), play(true));
```

`tests/test_camera_rail.cpp` 의 `TestRailDoesNotTouchSimulation` 이 그것이다. 1500틱을 돌리며 60틱에 발파를 한 번 넣고, 웨이브가 바뀔 때마다 레일을 시작한다. 두 실행의 `GameWorld::StateHash()` 가 같다.

프레임 루프의 배선은 두 줄이다.

<!-- snippet: include/game/defense_app.hpp@wave-rail -->
```cpp
if (camera_.Tilted() && view_.wave != previousWave_) {
    previousWave_ = view_.wave;
    camera_.BeginRail(RailControls(), 1.5f);
}
camera_.UpdateRail(static_cast<float>(frameSeconds));
```

`view_.wave` 를 보는 것이 의도다 — 호스트든 클라이언트든 자기가 그리는 스냅샷의 웨이브가 바뀔 때 연출이 돈다. 권위 월드를 보면 클라이언트에서는 영원히 돌지 않는다.

## 10. 헤드리스 스크립트가 화면을 클릭한다

카메라가 생기면서 셀프테스트 스크립트가 틀렸다. 스크립트는 `map.CellCenter(x, y)` 가 내는 **월드 좌표**를 마우스 좌표에 넣고 있었다.

`camera=ortho2d` 에서는 둘이 같으니 아무 문제가 없다. `tilted3d` 에서는 엉뚱한 칸이 눌린다. 첫 실행이 그것을 보여 줬다 — `result=LOST ticks=2711 towers=5 wave=2`.

스크립트는 사람 대신 **화면을** 클릭해야 한다.

<!-- snippet: include/game/defense_app.hpp@selftest-click -->
```cpp
auto clickAt = [&app](FrameInput& input, const math::Vec2& world) {
    const math::Vec2 screen = app.Camera().Project(world);
    input.mouseX = static_cast<int>(std::lround(screen.x));
    input.mouseY = static_cast<int>(std::lround(screen.y));
};
```

이 한 줄이 카메라가 생겼다는 사실을 가장 정직하게 드러낸다. **월드 좌표를 마우스에 넣을 수 있던 시절이 끝났다.**

`camera=ortho2d` 에서 `Project` 는 항등이므로 이 람다는 예전과 같은 값을 넣는다 — classic 셀프테스트가 `ticks=1377` 로 변하지 않은 이유의 일부다.

## 11. 변이: 원근 나눗셈을 지운다

`math/mat.hpp` 의 `Perspective` 에서 `At(3,2) = -1.0f` 한 줄을 지운다. 그러면 클립 좌표의 `w` 가 언제나 0이 되어 **원근 나눗셈이 성립하지 않는다** — 4행이 통째로 (0,0,0,0) 이라, 그 행렬은 역행렬이 없다.

```
[test] camera_pick
  FAIL tests\test_camera_pick.cpp:49  CHECK(camera.Invertible())
  FAIL tests\test_camera_pick.cpp:49  CHECK(camera.Invertible())
  ... (12개 자세 전부)
[test] camera_pick: 564 checks, 다수 실패
```

`Invertible()` 이 먼저 죽고, 그 뒤로 왕복 항등이 전부 죽는다. 흥미로운 것은 **그림은 계속 나온다**는 점이다 — `Project` 는 `w = 1` 로 나눠도 좌표를 내므로 타일이 그려지고 스프라이트가 붙는다. 원근이 사라진 평평한 그림이지만 화면은 비어 있지 않다. 픽셀 유무만 보는 검사로는 잡히지 않는다.

## 12. 실행과 관측

```
> ctest -C Release
100% tests passed out of 77
```

**카메라 자체.**

```
> build\bin\Release\test_camera_pick.exe
[test] camera_pick: 564 checks, 0 failures
```

564개 중 대부분이 왕복 항등이다. 요 4종 × 피치 3종 × 격자 표본에서 **투영 → 픽킹 → 셀 = 원래 셀**이 전부 성립하고, 화면 밖으로 나간 표본은 0이다. 월드 좌표 오차의 최댓값은 0.5픽셀 미만이다.

**레일.**

```
> build\bin\Release\test_camera_rail.exe
[test] camera_rail: 460 checks, 0 failures
```

제어점 통과(오차 1e-3 미만), 맵 경계 밖으로 튀지 않음(여유 4칸), 끝난 뒤 원래 자세 복귀(`Dot > 0.9999`), Slerp 최단 호, 그리고 **시뮬 상태 해시 동일**.

**표시 변환.**

```
> build\bin\Release\test_transform_chain.exe
[test] transform_chain: 30 checks, 0 failures
```

**제품에서 두 팔의 값.** 절차 지형 위에서 카메라만 바꿔 셀프테스트를 돌렸다.

| (3회 실행) | `camera=ortho2d` | `camera=tilted3d` |
|---|---|---|
| `render.draw` 합계 | 1707.03 / 1707.02 / 1749.00ms | 6443.98 / 6221.38 / 6362.63ms |
| `render.draw` 최댓값 | 1.942 / 1.838 / 1.712ms | 28.190 / 26.224 / 36.174ms |
| 프레임 수 | 2971 | 2971 |
| 시뮬 결과 | `CLEAR ticks=2971 towers=7` | `CLEAR ticks=2971 towers=7` |

**3.7배다.** 그리고 그 값은 놀랍지 않다 — 소프트웨어 래스터라이저가 프레임마다 1681개 타일을 3362개 삼각형으로 채우고, 삼각형 채우기는 `FillRect` 와 달리 바운딩 박스 안의 모든 픽셀에서 에지 함수를 세 번 평가한다. 기울인 쿼드의 바운딩 박스는 쿼드 면적보다 크므로 낭비가 그만큼 붙는다.

**프레임 최댓값 26~36ms 는 60Hz 예산 16.7ms 를 넘는다.** 평균은 2.1ms(6362/2971)라 대부분의 프레임은 여유가 있지만, 카메라가 지면 가까이 내려오는 순간(레일이 0.55배까지 당길 때) 화면을 채우는 쿼드가 커지면서 한 프레임이 예산을 두 배로 초과한다. 이 그림을 GPU 로 옮기지 않는 한 tilted3d 는 기본값이 될 수 없고, 그것이 이 팔을 스위치 뒤에 둔 이유다.

**시뮬 결과가 같다는 것이 중요하다.** `ticks=2971 towers=7 wave=6` 이 두 팔에서 같다 — 카메라가 표시 전용이라는 주장의 제품 수준 증거다.

다만 조건이 하나 붙는다. 그 동일성은 **스크립트가 화면을 클릭하도록 고친 뒤**의 것이다(§10). 카메라는 시뮬을 바꾸지 않지만 **입력이 만들어지는 방식**은 바꾼다. 사람이 조작할 때도 같다 — 기울인 시점에서 같은 칸을 누르려면 다른 픽셀을 눌러야 한다.

**전체 스택.** ctest 에 등록한 `mega_defense_selftest_procgen` 은 절차 지형 + 기울인 카메라 + 격자 조준 색인 + BVH 정적 질의 + 격자 광역 탐색을 한 번에 켠다.

```
> build\bin\Release\mega_defense.exe --selftest 25000 --ab map=procgen --ab camera=tilted3d
      --ab target_index=grid --ab static_query=bvh --ab broadphase=grid
[terrain] seed=7 41x41 maze=799 carved=318 rock=564 veins=148 route=79 bvh_prims=564/nodes=359
[selftest] result=CLEAR ticks=2971 towers=7 wave=6 packet=115 protocol=OK render=OK assets=10/10 content=external
[selftest] terrain route=79 opened=16 debris_live=0 debris_settled=27 target_rebuilds=1100
```

---

## 결론

카메라 넷이 들어왔고, 각각이 서로 다른 질문에 답한다. `mat` 은 "그 점이 화면 어디인가"와 그 역, `quat` 은 "카메라가 어느 쪽을 보는가", `transform` 은 "엔티티의 부품이 어디 붙는가", `interp` 는 "그 사이를 어떻게 지나가는가".

가장 값이 있었던 것은 판정 방법이었다. **왕복 항등**은 세 단계(투영·역투영·평면 교차)를 한 단언으로 묶어, 어느 하나가 틀리면 반드시 깨진다. 그리고 **상태 해시 동일**은 "표시 전용"이라는 주장을 문장에서 검사로 바꾼다.

짐벌락은 이 편에서 처음으로 실물이 됐다. 그것은 문서에 적힌 주의사항이 아니라 `Inverse` 가 거짓을 돌려주는 상태이고, 그 상태에서 픽킹은 실패한다. ±89°는 임의의 숫자가 아니라 그 상태에 닿지 않는 가장 가까운 각이다.

현재 상태의 한계 넷.

**카메라는 조작할 수 없다.** `Frame(map)` 이 맵 전체를 담는 자세를 한 번 잡고, 그 뒤로 사람이 팬·줌·회전을 시킬 입력이 없다. 자세를 바꾸는 코드는 레일뿐이다. 마우스 드래그·휠 입력은 `FrameInput` 에 축이 없어서 지금은 넣을 자리가 없다.

**깊이가 없다.** 프레임버퍼에 깊이 버퍼가 없으므로 그리기 순서가 곧 앞뒤다. 타일은 행 순서로 그려져 우연히 맞지만, 스프라이트는 스냅샷 순서로 그려진다 — 뒤쪽 적이 앞쪽 타워 위에 그려지는 경우가 있다.

**tilted3d 는 최악 프레임에서 예산을 넘는다.** 프레임 최댓값 26~36ms 가 전부 그리기이고 시뮬은 그 안에서 0.5ms 다. 평균 2.1ms 로 도는 그림이 특정 자세에서만 터지므로, 고치려면 화면을 덮는 쿼드의 픽셀 수를 줄이는 것(타일 컬링·해상도 분리)이 먼저다.

**포탑 방향은 근사다.** 스냅샷에서 가장 가까운 적을 향하므로, 실제로 조준 중인 적(모드가 First·Strongest 면 다를 수 있다)과 어긋난다.
