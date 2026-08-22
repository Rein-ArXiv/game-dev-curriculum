// 보스의 골격과 2본 IK — 합성과 도달 가능성.
//
// 이 파일이 묻는 것은 둘이다.
//
//   1. Skeleton::ComputeWorld 가 부모 사슬을 손으로 합성한 것과 같은가. 그리고
//      그 전제(부모가 언제나 자식보다 앞선다)를 AddBone 이 실제로 강제하는가.
//   2. SolveTwoBone2D 가 도달 가능한 목표에는 정확히 닿고, 도달 불가에서는
//      **곧게 뻗는가**. 두 번째가 게임 규칙이다 — 스톰프 원점이 그 발이라,
//      닿지 못하는 목표에 발이 순간이동하면 피해 반경이 몸에서 떨어져 나간다.

#include "engine/ik.hpp"
#include "engine/skeleton.hpp"
#include "game/boss_rig.hpp"
#include "test_util.h"

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace mega;
using namespace mega::game;

namespace {

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool NearVec(const math::Vec3& a, const math::Vec3& b, float eps = 1e-4f) {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

// ── 1. 골격 ────────────────────────────────────────────────────────
void TestSkeletonShape() {
    const engine::Skeleton skeleton = MakeBossSkeleton();
    CHECK_EQ(skeleton.BoneCount(), kBossBoneCount);
    CHECK_EQ(skeleton.Parent(0), -1);
    // 부모는 언제나 자식보다 앞선다. ComputeJoint 의 전방 순회가 그 전제 위에 있다.
    for (int bone = 1; bone < skeleton.BoneCount(); ++bone)
        CHECK(skeleton.Parent(bone) < bone);
    // 다리마다 고관절 → 무릎 → 발 사슬.
    for (int leg = 0; leg < kBossLegCount; ++leg) {
        const int hip = 1 + leg * 3;
        CHECK_EQ(skeleton.Parent(hip), 0);
        CHECK_EQ(skeleton.Parent(hip + 1), hip);
        CHECK_EQ(skeleton.Parent(hip + 2), hip + 1);
    }
}

// AddBone 이 위상 정렬을 강제하는가. 강제하지 않으면 아직 계산되지 않은 부모
// 변환으로 자식이 합성돼 조용히 틀린 포즈가 나온다.
void TestAddBoneRejectsForwardParent() {
    engine::Skeleton skeleton;
    CHECK_EQ(skeleton.AddBone(-1, math::Transform{}), 0);
    CHECK_EQ(skeleton.AddBone(5, math::Transform{}), -1);    // 아직 없는 본
    CHECK_EQ(skeleton.AddBone(1, math::Transform{}), -1);    // 자기 자신
    CHECK_EQ(skeleton.AddBone(-2, math::Transform{}), -1);
    CHECK_EQ(skeleton.BoneCount(), 1);                       // 하나도 늘지 않았다
    CHECK_EQ(skeleton.AddBone(0, math::Transform{}), 1);
    CHECK_EQ(skeleton.BoneCount(), 2);
}

// 합성 결과가 손으로 접은 부모 사슬과 같은가.
void TestComputeWorldMatchesManualChain() {
    const engine::Skeleton skeleton = MakeBossSkeleton();
    BossPoseKey key;
    key.lift = -0.13f;
    key.roll = 27.0f;
    key.hip[0] = -41.0f; key.hip[1] = 33.0f; key.hip[2] = -12.0f; key.hip[3] = 64.0f;
    const engine::Pose local = BossPoseFromKey(key);
    const engine::Pose world = skeleton.ComputeWorld(local);
    CHECK_EQ(static_cast<int>(world.size()), kBossBoneCount);

    for (int bone = 0; bone < kBossBoneCount; ++bone) {
        // 루트까지 거슬러 올라가며 부모 ∘ 자식을 손으로 접는다.
        math::Transform folded = local[static_cast<size_t>(bone)];
        for (int parent = skeleton.Parent(bone); parent >= 0;
             parent = skeleton.Parent(parent))
            folded = local[static_cast<size_t>(parent)] * folded;
        CHECK(NearVec(folded.position, world[static_cast<size_t>(bone)].position));
    }

    // 몸통 롤이 실제로 고관절을 돌린다 — 돌지 않으면 클립의 roll 은 죽은 값이다.
    BossPoseKey flat = key;
    flat.roll = 0.0f;
    const engine::Pose upright = skeleton.ComputeWorld(BossPoseFromKey(flat));
    CHECK(!NearVec(upright[1].position, world[1].position, 1e-3f));
}

// ── 2. 2본 IK ──────────────────────────────────────────────────────
void TestTwoBoneReachable() {
    const math::Vec2 root{3.0f, -2.0f};
    const float l1 = 0.42f, l2 = 0.42f;
    // 뻗은 길이(0.84)보다 확실히 가까운 목표.
    const math::Vec2 target = root + math::Vec2{0.30f, -0.45f};
    const engine::IkResult solved = engine::SolveTwoBone2D(root, l1, l2, target);
    CHECK(solved.reachable);
    CHECK(Near(solved.end.x, target.x, 1e-4f));
    CHECK(Near(solved.end.y, target.y, 1e-4f));
    // 마디 길이가 보존된다 — 이것이 "무릎을 놓았다"의 정의다.
    CHECK(Near(math::Length(solved.joint - root), l1, 1e-3f));
    CHECK(Near(math::Length(solved.end - solved.joint), l2, 1e-3f));

    // 굽는 방향을 뒤집으면 무릎은 목표선의 반대쪽에 놓이고 발은 그대로다.
    const engine::IkResult flipped = engine::SolveTwoBone2D(root, l1, l2, target, false);
    CHECK(Near(flipped.end.x, target.x, 1e-4f));
    CHECK(!Near(flipped.joint.x, solved.joint.x, 1e-3f) ||
          !Near(flipped.joint.y, solved.joint.y, 1e-3f));
}

void TestTwoBoneUnreachableGoesStraight() {
    const math::Vec2 root{0.0f, 0.0f};
    const float l1 = 0.42f, l2 = 0.42f;
    const math::Vec2 target{3.0f, 4.0f};      // 거리 5, 뻗어도 0.84
    const engine::IkResult solved = engine::SolveTwoBone2D(root, l1, l2, target);
    CHECK(!solved.reachable);
    CHECK(Near(math::Length(solved.joint - root), l1, 1e-3f));
    CHECK(Near(math::Length(solved.end - root), l1 + l2, 1e-3f));
    // 그리고 목표 방향으로 뻗는다(엉뚱한 쪽으로 접히지 않는다).
    const math::Vec2 dir = solved.end * (1.0f / math::Length(solved.end));
    CHECK(Near(dir.x, 0.6f, 1e-3f));
    CHECK(Near(dir.y, 0.8f, 1e-3f));
    // 무엇보다 유한하다. NaN 이 나오면 그 발 좌표가 그대로 스톰프 원점이 된다.
    CHECK(std::isfinite(solved.end.x) && std::isfinite(solved.end.y));
    CHECK(std::isfinite(solved.joint.x) && std::isfinite(solved.joint.y));
}

// 목표가 루트와 겹치면 방향 벡터가 0/0 이 된다. 그 자리를 막지 않으면 NaN 이다.
void TestTwoBoneDegenerateTarget() {
    const math::Vec2 root{12.0f, 7.0f};
    const engine::IkResult solved = engine::SolveTwoBone2D(root, 0.42f, 0.42f, root);
    CHECK(std::isfinite(solved.joint.x) && std::isfinite(solved.joint.y));
    CHECK(std::isfinite(solved.end.x) && std::isfinite(solved.end.y));
    CHECK(!solved.reachable);
}

// ── 3. 보스 한 프레임 ──────────────────────────────────────────────
void TestBossFrameIsPositionPure() {
    const BossPoses poses;
    const math::Vec2 core{400.0f, 400.0f};
    const math::Vec2 at{120.0f, 260.0f};
    const BossFrame a = SolveBossFrame(poses, at, core, 30.0f);
    const BossFrame b = SolveBossFrame(poses, at, core, 30.0f);
    // 같은 입력 → 같은 출력. 시계도 카운터도 섞이지 않는다.
    for (int leg = 0; leg < kBossLegCount; ++leg) {
        CHECK(a.legs[leg].foot.x == b.legs[leg].foot.x);
        CHECK(a.legs[leg].foot.y == b.legs[leg].foot.y);
    }
    CHECK_EQ(a.plants, b.plants);
    CHECK_EQ(a.plantedLeg, b.plantedLeg);

    // 위상 바닥값이 곧 디딤 횟수이고, 디딘 다리는 그 나머지다.
    CHECK_EQ(a.plants, static_cast<uint32_t>(std::floor(BossPhase(at))));
    CHECK_EQ(a.plantedLeg, static_cast<int>(a.plants % 4u));

    // 다리 마디 길이는 어느 프레임에서도 보존된다.
    for (int leg = 0; leg < kBossLegCount; ++leg) {
        CHECK(Near(math::Length(a.legs[leg].knee - a.legs[leg].hip), kBossThigh * 30.0f, 1e-2f));
        CHECK(Near(math::Length(a.legs[leg].foot - a.legs[leg].knee), kBossShin * 30.0f, 1e-2f));
    }

    // 한 걸음 안에서 다리가 실제로 움직인다(포즈가 고정이 아니다).
    const math::Vec2 later{at.x + kBossStride * 0.5f, at.y};
    const BossFrame mid = SolveBossFrame(poses, later, core, 30.0f);
    bool moved = false;
    for (int leg = 0; leg < kBossLegCount; ++leg)
        if (!Near(math::Length(mid.legs[leg].foot - mid.legs[leg].hip),
                  math::Length(a.legs[leg].foot - a.legs[leg].hip), 1e-3f))
            moved = true;
    CHECK(moved);

    // 그리고 제품 보폭에서 실제로 뻗는 다리가 나온다 — 도달 불가 가지가 죽은
    // 코드가 아니라는 뜻이다.
    int straight = 0;
    for (int step = 0; step < 16; ++step) {
        const BossFrame f = SolveBossFrame(
            poses, {at.x + kBossStride * (static_cast<float>(step) / 16.0f), at.y}, core, 30.0f);
        for (int leg = 0; leg < kBossLegCount; ++leg)
            if (!f.legs[leg].reachable) ++straight;
    }
    CHECK(straight > 0);
    std::printf("  한 걸음 64 표본 중 뻗은 다리 %d개\n", straight);
}

// 저작 클립이 발 위치를 실제로 바꾸는가. 바꾸지 않으면 boss.pose 는 장식이다.
void TestClipDrivesFeet() {
    BossPoses base;
    BossPoses tilted;
    std::vector<BossPoseKey> keys = DefaultWalkKeys();
    for (BossPoseKey& key : keys) key.hip[0] += 40.0f;   // 앞왼 다리만 크게 벌린다
    tilted.walk = ClipFromKeys(keys);

    const math::Vec2 core{400.0f, 400.0f};
    const math::Vec2 at{100.0f, 100.0f};
    const BossFrame a = SolveBossFrame(base, at, core, 30.0f);
    const BossFrame b = SolveBossFrame(tilted, at, core, 30.0f);
    CHECK(!Near(a.legs[0].foot.x, b.legs[0].foot.x, 1e-2f) ||
          !Near(a.legs[0].foot.y, b.legs[0].foot.y, 1e-2f));
    // 건드리지 않은 다리는 그대로다.
    CHECK(Near(a.legs[2].foot.x, b.legs[2].foot.x, 1e-3f));
    CHECK(Near(a.legs[2].foot.y, b.legs[2].foot.y, 1e-3f));
}

// 한 프레임의 자세를 푸는 비용. 단언하지 않고 출력만 한다 — 판정은 위 검사들이
// 하고, 이 수는 "스프라이트 한 장 대신 무엇을 치르는가"에 답한다.
void ReportSolveCost() {
    const BossPoses poses;
    const math::Vec2 core{400.0f, 400.0f};
    const int iterations = 200000;

    double sink = 0.0;
    auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        const math::Vec2 at{100.0f + static_cast<float>(i % 977) * 0.31f, 200.0f};
        sink += SolveBossFrame(poses, at, core, 30.0f).legs[0].foot.x;
    }
    const double frameUs = std::chrono::duration<double, std::micro>(
                               std::chrono::steady_clock::now() - begin).count() / iterations;

    // 그중 클립 샘플링·블렌딩만.
    begin = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i)
        sink += poses.SampleLocal(static_cast<float>(i % 100) * 0.01f, 0.5f)[0].position.y;
    const double sampleUs = std::chrono::duration<double, std::micro>(
                                std::chrono::steady_clock::now() - begin).count() / iterations;

    std::printf("  SolveBossFrame=%.3fus/회 (그중 클립 샘플+블렌드 %.3fus, %.0f%%)  sink=%.0f\n",
                frameUs, sampleUs, sampleUs / frameUs * 100.0, sink);
    CHECK(frameUs < 50.0);   // 자릿수가 바뀌면 알아채야 한다
}

}  // namespace

static void RunTests() {
    TestSkeletonShape();
    TestAddBoneRejectsForwardParent();
    TestComputeWorldMatchesManualChain();
    TestTwoBoneReachable();
    TestTwoBoneUnreachableGoesStraight();
    TestTwoBoneDegenerateTarget();
    TestBossFrameIsPositionPure();
    TestClipDrivesFeet();
    ReportSolveCost();
}

TEST_MAIN("boss_rig")
