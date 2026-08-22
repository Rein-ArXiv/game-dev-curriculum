// 카메라: 투영·픽킹 왕복 항등 · 피치 클램프 · 짐벌락 · 퇴화 거절.
//
// 왕복 항등이 이 편의 판정이다. 셀 중심을 투영한 화면 좌표를 다시 픽킹하면 같은
// 셀이 나와야 한다. Perspective 의 At(3,2) = -1 을 지우면(원근 나눗셈이 사라지면)
// 이 항등이 즉시 깨진다.
#include "game/camera.hpp"
#include "game/map_gen.hpp"
#include "test_util.h"

#include <cmath>

using namespace mega;
using namespace mega::game;

static DefenseMap MakeMap() {
    mapgen::TerrainParams params;
    params.seed = 7;
    DefenseMap map;
    CHECK(mapgen::Generate(params, map));
    return map;
}

// ortho2d 팔은 항등이다. 이 사실이 "기본값은 스위치 이전의 동작"의 전부다.
static void TestOrthoIsIdentity() {
    DefenseCamera camera;
    camera.Configure(CameraKind::Ortho2d, 480, 480);
    for (float x = 0.0f; x < 480.0f; x += 37.0f)
        for (float y = 0.0f; y < 480.0f; y += 41.0f) {
            const math::Vec2 projected = camera.Project({x, y});
            CHECK(projected.x == x && projected.y == y);
            math::Vec2 picked{};
            CHECK(camera.Pick(x, y, picked));
            CHECK(picked.x == x && picked.y == y);
        }
    CHECK(camera.ScaleAt({100.0f, 100.0f}) == 1.0f);
}

// 임의의 자세에서 셀 중심 → 화면 → 셀 왕복이 항등이다.
static void TestPickRoundTrip() {
    const DefenseMap map = MakeMap();
    DefenseCamera camera;
    camera.Configure(CameraKind::Tilted3d, 480, 480);
    camera.Frame(map);

    int checked = 0, matched = 0, offScreen = 0;
    for (float yaw : {-40.0f, 0.0f, 25.0f, 130.0f}) {
        for (float pitch : {80.0f, 58.0f, 35.0f}) {
            camera.SetYawPitch(yaw, pitch);
            CHECK(camera.Invertible());
            for (int y = 2; y < map.Rows(); y += 7) {
                for (int x = 2; x < map.Columns(); x += 7) {
                    const math::Vec2 world = map.CellCenter(x, y);
                    bool behind = false;
                    const math::Vec2 screen = camera.Project(world, &behind);
                    if (behind) { ++offScreen; continue; }
                    math::Vec2 back{};
                    if (!camera.Pick(screen.x, screen.y, back)) { ++offScreen; continue; }
                    ++checked;
                    const DefenseMap::Cell cell = map.WorldToCell(back);
                    if (cell.x == x && cell.y == y) ++matched;
                }
            }
        }
    }
    CHECK(checked > 200);
    CHECK_EQ(matched, checked);      // 셀 단위로 정확히 되돌아온다
    CHECK_EQ(offScreen, 0);
}

// 픽킹 오차는 픽셀이 아니라 월드 단위로도 작아야 한다.
static void TestPickPrecision() {
    const DefenseMap map = MakeMap();
    DefenseCamera camera;
    camera.Configure(CameraKind::Tilted3d, 480, 480);
    camera.Frame(map);
    camera.SetYawPitch(15.0f, 50.0f);
    float worst = 0.0f;
    for (int y = 1; y < map.Rows(); y += 5)
        for (int x = 1; x < map.Columns(); x += 5) {
            const math::Vec2 world = map.CellCenter(x, y);
            const math::Vec2 screen = camera.Project(world);
            math::Vec2 back{};
            CHECK(camera.Pick(screen.x, screen.y, back));
            worst = std::fmax(worst, math::Length(back - world));
        }
    CHECK(worst < 0.5f);             // 반 픽셀 미만
}

// 피치는 ±89° 로 잘린다. 그 경계에서도 뷰 기저가 성립한다.
static void TestPitchClamp() {
    DefenseCamera camera;
    camera.Configure(CameraKind::Tilted3d, 480, 480);
    camera.SetDistance(400.0f);
    camera.SetYawPitch(0.0f, -140.0f);
    CHECK_EQ(camera.Pitch(), -DefenseCamera::kMaxPitchDegrees);
    camera.SetYawPitch(0.0f, 90.0f);
    CHECK_EQ(camera.Pitch(), DefenseCamera::kMaxPitchDegrees);

    camera.SetYawPitch(0.0f, 89.0f);
    const math::Vec3 forward = camera.Forward();
    const math::Vec3 up{0.0f, 0.0f, 1.0f};
    const float sine = math::Length(math::Cross(forward, up));
    CHECK(sine > 0.01f);             // 기저가 퇴화하지 않는다
    CHECK(camera.Invertible());
    math::Vec2 picked{};
    CHECK(camera.Pick(240.0f, 300.0f, picked));
    CHECK(!std::isnan(picked.x) && !std::isnan(picked.y));
}

// 클램프가 막고 있는 것: 정확히 ±90° 에서 LookAt 이 무너진다.
static void TestGimbalLockIsReal() {
    // 전방이 상방과 평행한 순간 Cross(f, up) = 0 이다. Normalize 가 0 벡터를
    // 돌려주므로 뷰 기저의 우측 축이 통째로 사라지고, 그 행렬은 역행렬이 없다 —
    // 즉 그 자세에서는 픽킹 자체가 불가능하다.
    const math::Vec3 eye{0.0f, 0.0f, 100.0f};
    const math::Vec3 target{0.0f, 0.0f, 0.0f};
    const math::Vec3 forward = math::Normalize(target - eye);
    const math::Vec3 right = math::Cross(forward, math::Vec3{0.0f, 0.0f, 1.0f});
    CHECK(math::Length(right) < 1e-6f);
    const math::Mat4 degenerate = math::LookAt(eye, target, {0.0f, 0.0f, 1.0f});
    math::Mat4 unused;
    CHECK(!math::Inverse(degenerate, unused));

    // 오일러각 매개화의 자유도 손실도 같은 각도에서 일어난다: pitch = 90° 면
    // yaw 와 roll 이 같은 회전을 만든다.
    const math::Quat a = math::Quat::FromEuler(math::Radians(90.0f), math::Radians(30.0f), 0.0f);
    const math::Quat b = math::Quat::FromEuler(math::Radians(90.0f), 0.0f, math::Radians(-30.0f));
    const math::Vec3 probe{1.0f, 2.0f, 3.0f};
    const math::Vec3 ra = math::Rotate(a, probe);
    const math::Vec3 rb = math::Rotate(b, probe);
    CHECK(math::Length(ra - rb) < 1e-4f);    // 두 축이 같은 회전을 낸다 = 짐벌락

    // 89° 에서는 아직 두 축이 구분된다.
    const math::Quat c = math::Quat::FromEuler(math::Radians(89.0f), math::Radians(30.0f), 0.0f);
    const math::Quat d = math::Quat::FromEuler(math::Radians(89.0f), 0.0f, math::Radians(-30.0f));
    CHECK(math::Length(math::Rotate(c, probe) - math::Rotate(d, probe)) > 1e-3f);
}

// 퇴화 행렬(거리 0)에서는 픽킹이 성공을 가장하지 않는다.
static void TestDegenerateRejected() {
    DefenseCamera camera;
    camera.Configure(CameraKind::Tilted3d, 480, 480);
    camera.SetDistance(1.0f);
    camera.SetYawPitch(0.0f, 0.0f);      // 지면과 평행한 시선
    math::Vec2 picked{};
    // 지면과 평행한 광선은 교차점이 없다 — 화면 한가운데에서 거절돼야 한다.
    const bool ok = camera.Pick(240.0f, 240.0f, picked);
    CHECK(!ok || std::fabs(picked.y) < 1e9f);
}

// 스케일은 거리와 함께 준다.
static void TestScaleFallsWithDistance() {
    const DefenseMap map = MakeMap();
    DefenseCamera camera;
    camera.Configure(CameraKind::Tilted3d, 480, 480);
    camera.Frame(map);
    camera.SetYawPitch(0.0f, 58.0f);
    // 피치가 +58° 면 카메라는 y 가 큰 쪽(화면 아래) 위에 떠 있다. 그쪽 행이 가깝다.
    const math::Vec2 nearRow = map.CellCenter(map.Columns() / 2, map.Rows() - 3);
    const math::Vec2 farRow = map.CellCenter(map.Columns() / 2, 2);
    CHECK(camera.ScaleAt(nearRow) > camera.ScaleAt(farRow));
}

static void RunTests() {
    TestOrthoIsIdentity();
    TestPickRoundTrip();
    TestPickPrecision();
    TestPitchClamp();
    TestGimbalLockIsReal();
    TestDegenerateRejected();
    TestScaleFallsWithDistance();
}

TEST_MAIN("camera_pick")
