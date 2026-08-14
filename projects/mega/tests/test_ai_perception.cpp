// 인지 검증: 시야 콘·가림·청각·메모리.
#include "ai/perception/perception.hpp"
#include "test_util.h"

using namespace mega::ai::perceive;
using mega::math::Vec2;
using mega::ai::path::GridMap;

static void TestVisionCone() {
    VisionCone cone = VisionCone::FromDegrees(20.0f, 90.0f);   // 90° 시야
    Vec2 eye{0, 0}, facing{1, 0};                              // +x 바라봄

    CHECK(InCone(eye, facing, cone, {10, 0}));     // 정면 → 보임
    CHECK(InCone(eye, facing, cone, {10, 5}));     // 45° 안 → 보임
    CHECK(!InCone(eye, facing, cone, {0, 10}));    // 정확히 90°(옆) → 경계 밖
    CHECK(!InCone(eye, facing, cone, {-10, 0}));   // 뒤 → 안 보임
    CHECK(!InCone(eye, facing, cone, {25, 0}));    // 범위 밖(>20) → 안 보임
}

static void TestOcclusion() {
    // 가운데 세로 벽. 왼쪽에서 오른쪽을 봄.
    GridMap m;
    m.ParseAscii({
        ".....",
        ".....",
        "..#..",
        ".....",
        ".....",
    });
    VisionCone cone = VisionCone::FromDegrees(20.0f, 120.0f);
    Vec2 eye{0.5f, 2.5f}, facing{1, 0};

    // 벽(2,2) 바로 너머 (4.5,2.5) → 가림.
    CHECK(!CanSee(m, eye, facing, cone, {4.5f, 2.5f}));
    // 벽을 피한 위쪽 경로 (4.5,0.5) 는 각도 안이고 벽 없음 → 보임.
    CHECK(CanSee(m, eye, facing, cone, {4.5f, 0.5f}));
}

static void TestHearing() {
    CHECK(CanHear({0, 0}, {3, 4}, 5.0f));      // 거리 5 == 반경 5 → 들림
    CHECK(!CanHear({0, 0}, {3, 4}, 4.9f));     // 거리 5 > 4.9 → 못 들음
}

static void TestMemory() {
    Memory mem;
    CHECK(!mem.hasTarget);

    mem.Observe({10, 20});
    CHECK(mem.hasTarget);
    CHECK(mem.lastSeen.x == 10.0f && mem.lastSeen.y == 20.0f);

    // 망각 시간 전: 유지.
    mem.Update(1.0f, 3.0f);
    CHECK(mem.hasTarget);
    mem.Update(1.0f, 3.0f);
    CHECK(mem.hasTarget);
    // 누적 3초 → 망각.
    mem.Update(1.0f, 3.0f);
    CHECK(!mem.hasTarget);

    // 다시 보면 갱신되고 나이 리셋.
    mem.Observe({5, 5});
    mem.Update(2.0f, 3.0f);
    CHECK(mem.hasTarget);              // 아직 3초 안 됨
    CHECK(mem.lastSeen.x == 5.0f);
}

static void RunTests() {
    TestVisionCone();
    TestOcclusion();
    TestHearing();
    TestMemory();
}

TEST_MAIN("ai_perception")
