// 카메라 레일: Catmull-Rom 이 제어점을 지나는가 · 맵을 벗어나지 않는가 ·
// **연출이 시뮬을 건드리지 않는가**.
//
// 마지막 항목이 이 파일의 이유다. 표시 전용이라는 주장은 문서에 적어서는 증명되지
// 않는다. 같은 명령열을 레일과 함께, 레일 없이 돌려 상태 해시가 같아야 한다.
#include "game/camera.hpp"
#include "game/defense_content.hpp"
#include "game/game.hpp"
#include "game/map_gen.hpp"
#include "test_util.h"

#include <cmath>
#include <vector>

using namespace mega;
using namespace mega::game;

static DefenseMap MakeMap() {
    mapgen::TerrainParams params;
    params.seed = 4;
    DefenseMap map;
    CHECK(mapgen::Generate(params, map));
    return map;
}

// Catmull-Rom 은 제어점을 지난다. 양 끝을 복제했으므로 첫 점과 끝 점도 정확히.
static void TestSplinePassesControlPoints() {
    std::vector<math::Vec2> controls{{10, 10}, {40, 80}, {120, 60}, {200, 150}, {260, 90}};
    DefenseCamera camera;
    camera.Configure(CameraKind::Tilted3d, 480, 480);
    camera.BeginRail(controls, 1.0f);
    CHECK(camera.RailActive());

    const size_t segments = controls.size() - 1;
    for (size_t i = 0; i < controls.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const math::Vec2 sampled = camera.SampleRail(t);
        CHECK(math::Length(sampled - controls[i]) < 1e-3f);
    }
    // 중간 샘플은 제어점이 아니다(직선 보간이 아니라 곡선이라는 것).
    const math::Vec2 middle = camera.SampleRail(0.375f);
    const math::Vec2 linear = math::Lerp(controls[1], controls[2], 0.5f);
    CHECK(math::Length(middle - linear) > 1e-3f);
}

// 레일 샘플은 제어점의 경계 상자를 크게 벗어나지 않는다(오버슛 한계).
static void TestRailStaysNearMap() {
    const DefenseMap map = MakeMap();
    std::vector<math::Vec2> controls;
    for (size_t i = 0; i < map.RouteLength(); i += 8) {
        const DefenseMap::Cell cell = map.RouteCell(i);
        controls.push_back(map.CellCenter(cell.x, cell.y));
    }
    CHECK(controls.size() >= 4);

    DefenseCamera camera;
    camera.Configure(CameraKind::Tilted3d, 480, 480);
    camera.Frame(map);
    camera.BeginRail(controls, 1.0f);

    const float margin = static_cast<float>(map.CellSize()) * 4.0f;
    const float minX = -margin, maxX = static_cast<float>(map.PixelWidth()) + margin;
    const float minY = -margin, maxY = static_cast<float>(map.PixelHeight()) + margin;
    for (int i = 0; i <= 200; ++i) {
        const math::Vec2 p = camera.SampleRail(static_cast<float>(i) / 200.0f);
        CHECK(p.x >= minX && p.x <= maxX);
        CHECK(p.y >= minY && p.y <= maxY);
    }
}

// 레일은 끝나면 원래 자세로 돌아온다. Slerp 는 최단 호를 고른다.
static void TestRailReturnsToRest() {
    const DefenseMap map = MakeMap();
    DefenseCamera camera;
    camera.Configure(CameraKind::Tilted3d, 480, 480);
    camera.Frame(map);
    camera.SetYawPitch(0.0f, -58.0f);
    const math::Vec2 restFocus = camera.Focus();
    const float restDistance = camera.Distance();
    const math::Quat restOrientation = camera.Orientation();

    std::vector<math::Vec2> controls;
    for (size_t i = 0; i < map.RouteLength(); i += 10) {
        const DefenseMap::Cell cell = map.RouteCell(i);
        controls.push_back(map.CellCenter(cell.x, cell.y));
    }
    camera.BeginRail(controls, 0.5f);
    bool moved = false;
    for (int i = 0; i < 40; ++i) {
        camera.UpdateRail(1.0f / 60.0f);
        if (camera.RailActive() && math::Length(camera.Focus() - restFocus) > 1.0f) moved = true;
        CHECK(camera.Invertible());
    }
    CHECK(moved);                                   // 실제로 훑었다
    CHECK(!camera.RailActive());
    CHECK(math::Length(camera.Focus() - restFocus) < 1e-3f);
    CHECK(std::fabs(camera.Distance() - restDistance) < 1e-3f);
    CHECK(std::fabs(math::Dot(camera.Orientation(), restOrientation)) > 0.9999f);
}

// Slerp 는 Dot < 0 인 쌍에서 최단 호로 간다(장반 호가 아니다).
static void TestSlerpShortestArc() {
    const math::Quat a = math::Quat::FromAxisAngle({0, 0, 1}, math::Radians(10.0f));
    math::Quat b = math::Quat::FromAxisAngle({0, 0, 1}, math::Radians(40.0f));
    b = b * -1.0f;                                   // 같은 회전, 반대 부호
    const math::Quat mid = math::Slerp(a, b, 0.5f);
    const math::Vec3 probe{1.0f, 0.0f, 0.0f};
    const math::Vec3 rotated = math::Rotate(math::Normalize(mid), probe);
    const math::Vec3 expected =
        math::Rotate(math::Quat::FromAxisAngle({0, 0, 1}, math::Radians(25.0f)), probe);
    CHECK(math::Length(rotated - expected) < 1e-3f);
}

// **판정**: 연출이 있어도 시뮬 상태 해시가 같다.
static void TestRailDoesNotTouchSimulation() {
    const DefenseMap map = MakeMap();
    Switches switches;
    switches.map = MapSource::Procgen;

    auto play = [&](bool withRail) {
        GameWorld world;
        world.Configure(switches);
        world.Init(map, defense::DefaultWaves(), 900, 400.0f);

        DefenseCamera camera;
        camera.Configure(withRail ? CameraKind::Tilted3d : CameraKind::Ortho2d, 480, 480);
        camera.Frame(*world.Map());
        std::vector<math::Vec2> controls;
        for (size_t i = 0; i < world.Map()->RouteLength(); i += 9) {
            const DefenseMap::Cell cell = world.Map()->RouteCell(i);
            controls.push_back(world.Map()->CellCenter(cell.x, cell.y));
        }

        int wave = world.Wave();
        for (int tick = 0; tick < 1500; ++tick) {
            if (tick == 60) {
                const DefenseMap::Cell c = world.Map()->RouteCell(5);
                world.Blast(world.Map()->CellCenter(c.x, c.y));
            }
            world.Step(1.0f / 60.0f);
            if (withRail) {
                if (world.Wave() != wave) {
                    wave = world.Wave();
                    camera.BeginRail(controls, 1.5f);
                }
                camera.UpdateRail(1.0f / 60.0f);
            }
        }
        return world.StateHash();
    };

    CHECK_EQ(play(false), play(true));
}

static void RunTests() {
    TestSplinePassesControlPoints();
    TestRailStaysNearMap();
    TestRailReturnsToRest();
    TestSlerpShortestArc();
    TestRailDoesNotTouchSimulation();
}

TEST_MAIN("camera_rail")
