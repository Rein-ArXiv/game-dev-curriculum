// 표시 변환: 부모∘자식 합성 · 역변환 왕복 · 제품 소비자(포탑·타워 선택).
//
// operator* 가 스케일 곱을 빠뜨리면 아래 합성 단언이 즉시 깨진다. 그 곱이 없으면
// 자식의 위치는 맞고 크기만 틀리므로, 위치만 비교하는 테스트로는 잡히지 않는다.
#include "game/camera.hpp"
#include "game/defense_view.hpp"
#include "test_util.h"

#include <cmath>

using namespace mega;
using namespace mega::game;

static bool Near(const math::Vec3& a, const math::Vec3& b, float eps = 1e-4f) {
    return math::Length(a - b) <= eps;
}

// (부모 * 자식).TransformPoint(p) == 부모.TransformPoint(자식.TransformPoint(p))
static void TestCompositionMatchesNesting() {
    math::Transform parent;
    parent.position = {12.0f, -4.0f, 3.0f};
    parent.rotation = math::Quat::FromAxisAngle({0.2f, 1.0f, 0.4f}, 0.9f);
    parent.scale = {2.0f, 2.0f, 2.0f};        // 균일 스케일(비균일은 행렬만 정확)

    math::Transform child;
    child.position = {0.0f, 1.5f, 0.0f};
    child.rotation = math::Quat::FromAxisAngle({0.0f, 0.0f, 1.0f}, -0.6f);
    child.scale = {0.5f, 0.5f, 0.5f};

    const math::Transform composed = parent * child;
    const math::Vec3 probes[4] = {
        {0, 0, 0}, {1, 0, 0}, {0.3f, -0.7f, 1.2f}, {-2.0f, 4.0f, 0.5f}};
    for (const math::Vec3& p : probes)
        CHECK(Near(composed.TransformPoint(p), parent.TransformPoint(child.TransformPoint(p)), 1e-3f));

    // 스케일 곱이 실제로 반영됐는가. 빠뜨리면 1.0 이 나온다.
    CHECK(std::fabs(composed.scale.x - 1.0f) < 1e-5f);
    CHECK(std::fabs(composed.scale.x - parent.scale.x * child.scale.x) < 1e-5f);
}

// Inverse() 와의 왕복은 항등이다.
static void TestInverseRoundTrip() {
    math::Transform t;
    t.position = {30.0f, 17.0f, -5.0f};
    t.rotation = math::Quat::FromEuler(0.4f, -1.1f, 0.25f);
    t.scale = {3.0f, 3.0f, 3.0f};
    const math::Transform inverse = t.Inverse();
    const math::Vec3 probes[4] = {
        {0, 0, 0}, {5, -2, 1}, {-11, 3, 7}, {0.25f, 0.5f, -0.75f}};
    for (const math::Vec3& p : probes) {
        CHECK(Near(inverse.TransformPoint(t.TransformPoint(p)), p, 1e-3f));
        CHECK(Near(t.TransformPoint(inverse.TransformPoint(p)), p, 1e-3f));
        CHECK(Near(t.InverseTransformPoint(t.TransformPoint(p)), p, 1e-3f));
    }
}

// 포탑 총구: 몸통의 자식이 회전과 배율을 물려받는다.
static void TestBarrelFollowsBody() {
    const math::Vec2 towerPos{120.0f, 240.0f};
    const float length = 1.0f;
    // 요 0° → 총구는 +y 로 scale 만큼 나간다.
    {
        const math::Transform body = EntityTransform(towerPos, 0.0f, 10.0f);
        const math::Transform muzzle = body * BarrelTransform(length);
        CHECK(std::fabs(muzzle.position.x - towerPos.x) < 1e-3f);
        CHECK(std::fabs(muzzle.position.y - (towerPos.y + 10.0f)) < 1e-3f);
    }
    // 요 90° → 총구가 옆으로 돈다. 배율을 키우면 더 멀리 나간다.
    {
        const math::Transform body = EntityTransform(towerPos, 90.0f, 10.0f);
        const math::Transform muzzle = body * BarrelTransform(length);
        CHECK(std::fabs(muzzle.position.x - (towerPos.x - 10.0f)) < 1e-3f);
        CHECK(std::fabs(muzzle.position.y - towerPos.y) < 1e-3f);
    }
    {
        const math::Transform small = EntityTransform(towerPos, 0.0f, 5.0f);
        const math::Transform large = EntityTransform(towerPos, 0.0f, 20.0f);
        const float shortReach = (small * BarrelTransform(length)).position.y - towerPos.y;
        const float longReach = (large * BarrelTransform(length)).position.y - towerPos.y;
        CHECK(longReach > shortReach * 3.0f);
    }
}

// 타워 선택 판정이 배율을 따라간다 — 반지름 상수 하나로는 표현되지 않는 관계.
static void TestSelectionScalesWithTransform() {
    const math::Vec2 towerPos{200.0f, 200.0f};
    const math::Transform small = EntityTransform(towerPos, 0.0f, 8.0f);
    const math::Transform large = EntityTransform(towerPos, 0.0f, 20.0f);
    const math::Vec2 probe{towerPos.x + 12.0f, towerPos.y};
    CHECK(!TransformContains(small, probe));
    CHECK(TransformContains(large, probe));
    CHECK(TransformContains(small, towerPos));
}

// 스냅샷 위의 실제 소비자: 포탑이 가장 가까운 적을 향한다.
static void TestTowerFacingFollowsNearestEnemy() {
    wire::Snapshot view;
    wire::TowerState tower;
    tower.id = 1;
    tower.pos = {100.0f, 100.0f};
    tower.tier = 2;
    view.towers.push_back(tower);
    CHECK(std::fabs(TowerFacing(view, tower.pos)) < 1e-5f);   // 적이 없으면 0

    view.enemies.push_back({7, {100.0f, 160.0f}, 10.0f, 10.0f, EnemyType::Normal});
    const float towardPlusY = TowerFacing(view, tower.pos);
    CHECK(std::fabs(towardPlusY) < 1e-3f);                    // 로컬 +y 방향

    // 두 적이 같은 거리면 승자는 먼저 나온 쪽이다. 방향이 도는 것을 보려면
    // 두 번째 적이 실제로 더 가까워야 한다.
    view.enemies.push_back({8, {60.0f, 100.0f}, 10.0f, 10.0f, EnemyType::Normal});
    const float towardMinusX = TowerFacing(view, tower.pos);
    CHECK(std::fabs(towardMinusX - 90.0f) < 1e-2f);           // 더 가까운 쪽으로 돈다

    const math::Transform body = TowerTransform(view.towers.front(), view, 20.0f);
    const math::Transform muzzle = body * BarrelTransform(1.0f);
    CHECK(muzzle.position.x < tower.pos.x);                   // 총구가 그쪽을 본다
}

static void RunTests() {
    TestCompositionMatchesNesting();
    TestInverseRoundTrip();
    TestBarrelFollowsBody();
    TestSelectionScalesWithTransform();
    TestTowerFacingFollowsNearestEnemy();
}

TEST_MAIN("transform_chain")
