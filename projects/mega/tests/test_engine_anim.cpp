// 애니메이션 검증: 스켈레톤 월드 변환·포즈 블렌딩·클립 샘플링·2본 IK.
#include "engine/anim.hpp"
#include "engine/ik.hpp"
#include "engine/skeleton.hpp"
#include "test_util.h"

#include <cmath>

using namespace mega::engine;
using mega::math::Transform;
using mega::math::Quat;
using mega::math::Vec2;
using mega::math::Vec3;

static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
static bool VNear(const Vec3& a, const Vec3& b, float e = 1e-3f) {
    return Near(a.x, b.x, e) && Near(a.y, b.y, e) && Near(a.z, b.z, e);
}

static void TestSkeletonWorld() {
    // 루트 → 자식 체인. 자식 로컬이 부모 월드로 누적되는가.
    Skeleton sk;
    Transform root; root.position = {10, 0, 0};
    Transform child; child.position = {5, 0, 0};       // 부모 기준 +5 x
    int r = sk.AddBone(-1, root);
    int c = sk.AddBone(r, child);
    (void)r; (void)c;

    Pose local = {root, child};
    Pose world = sk.ComputeWorld(local);
    CHECK(VNear(world[0].position, {10, 0, 0}));        // 루트 그대로
    CHECK(VNear(world[1].position, {15, 0, 0}));        // 10 + 5

    // 루트가 90° Z 회전하면 자식 오프셋이 회전된다.
    Pose local2 = local;
    local2[0].rotation = Quat::FromAxisAngle({0, 0, 1}, 1.57079633f);  // 90°
    Pose world2 = sk.ComputeWorld(local2);
    // 자식 로컬 (5,0,0) 가 루트 90°Z 회전 → (0,5,0), + 루트 위치 (10,0,0).
    CHECK(VNear(world2[1].position, {10, 5, 0}, 1e-2f));
}

static void TestBlendPoses() {
    Pose a = {Transform{}}, b = {Transform{}};
    a[0].position = {0, 0, 0};
    b[0].position = {10, 20, 0};
    a[0].rotation = Quat::Identity();
    b[0].rotation = Quat::FromAxisAngle({0, 0, 1}, 1.57079633f);   // 90°Z

    Pose mid = BlendPoses(a, b, 0.5f);
    CHECK(VNear(mid[0].position, {5, 10, 0}));          // 위치 중점
    // 회전 중점 45°Z: x축이 (cos45,sin45,0)로.
    Vec3 rotated = mega::math::Rotate(mid[0].rotation, {1, 0, 0});
    float r2 = std::sqrt(0.5f);
    CHECK(VNear(rotated, {r2, r2, 0}, 1e-2f));
}

static void TestClipSample() {
    AnimClip clip;
    Pose p0 = {Transform{}}, p1 = {Transform{}};
    p0[0].position = {0, 0, 0};
    p1[0].position = {100, 0, 0};
    clip.AddKey(0.0f, p0);
    clip.AddKey(1.0f, p1);

    CHECK(VNear(clip.Sample(0.0f)[0].position, {0, 0, 0}));
    CHECK(VNear(clip.Sample(1.0f)[0].position, {100, 0, 0}));
    CHECK(VNear(clip.Sample(0.25f)[0].position, {25, 0, 0}));
    CHECK(VNear(clip.Sample(-1.0f)[0].position, {0, 0, 0}));     // 클램프
    CHECK(VNear(clip.Sample(5.0f)[0].position, {100, 0, 0}));
}

static void TestTwoBoneIk() {
    // 루트 원점, 본 길이 3,4. 목표 (5,0) → 도달 가능(d=5 < 7).
    IkResult r = SolveTwoBone2D({0, 0}, 3.0f, 4.0f, {5, 0}, true);
    CHECK(r.reachable);
    CHECK(Near(r.end.x, 5.0f) && Near(r.end.y, 0.0f));          // 끝이 목표에
    // 관절은 루트에서 3, 목표에서 4 떨어진다.
    CHECK(Near(mega::math::Length(r.joint), 3.0f, 1e-2f));
    CHECK(Near(mega::math::Length(r.joint - Vec2{5, 0}), 4.0f, 1e-2f));
    // 3-4-5 직각삼각형: 관절 y 부호는 bend 방향.
    CHECK(r.joint.y > 0);

    // 반대 방향 굽힘.
    IkResult r2 = SolveTwoBone2D({0, 0}, 3.0f, 4.0f, {5, 0}, false);
    CHECK(r2.joint.y < 0);

    // 도달 불가(d=10 > 7) → 곧게 뻗음.
    IkResult r3 = SolveTwoBone2D({0, 0}, 3.0f, 4.0f, {10, 0}, true);
    CHECK(!r3.reachable);
    CHECK(Near(mega::math::Length(r3.end), 7.0f, 1e-2f));       // 최대 reach
}

static void RunTests() {
    TestSkeletonWorld();
    TestBlendPoses();
    TestClipSample();
    TestTwoBoneIk();
}

TEST_MAIN("engine_anim")
