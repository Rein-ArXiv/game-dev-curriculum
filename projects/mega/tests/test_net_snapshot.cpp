// 스냅샷 직렬화 + 보간 버퍼 검증.
#include "net/snapshot.hpp"
#include "test_util.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace mega::net;
using mega::math::Vec2;

static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

static void TestSerialize() {
    Snapshot s;
    s.tick = 42;
    s.entities = {{1, {10, 20}, 100}, {2, {-5, 7}, 50}, {3, {0, 0}, 1}};
    auto bytes = WriteSnapshot(s);

    Snapshot r;
    CHECK(ReadSnapshot(bytes, r));
    CHECK_EQ(r.tick, 42u);
    CHECK_EQ(r.entities.size(), (size_t)3);
    CHECK_EQ(r.entities[0].id, 1u);
    CHECK(Near(r.entities[0].pos.x, 10));
    CHECK(Near(r.entities[1].pos.y, 7));
    CHECK(Near(r.entities[2].hp, 1));
}

static void TestInterpolateMidpoint() {
    SnapshotBuffer buf;
    Snapshot a; a.tick = 0;  a.entities = {{1, {0, 0}, 100}};
    Snapshot b; b.tick = 10; b.entities = {{1, {100, 50}, 80}};
    buf.Add(a); buf.Add(b);

    auto mid = buf.Sample(5.0f);              // 중간
    CHECK_EQ(mid.size(), (size_t)1);
    CHECK(Near(mid[0].pos.x, 50));            // 0→100 의 절반
    CHECK(Near(mid[0].pos.y, 25));
    CHECK(Near(mid[0].hp, 90));              // 100→80 의 절반

    auto q = buf.Sample(2.5f);                // 25%
    CHECK(Near(q[0].pos.x, 25));
}

static void TestClamp() {
    SnapshotBuffer buf;
    Snapshot a; a.tick = 10; a.entities = {{1, {10, 0}, 100}};
    Snapshot b; b.tick = 20; b.entities = {{1, {20, 0}, 100}};
    buf.Add(a); buf.Add(b);
    CHECK(Near(buf.Sample(5.0f)[0].pos.x, 10));    // 가장 오래된 것으로 클램프
    CHECK(Near(buf.Sample(25.0f)[0].pos.x, 20));   // 가장 최신으로 클램프
}

static void TestNewEntity() {
    SnapshotBuffer buf;
    Snapshot a; a.tick = 0;  a.entities = {{1, {0, 0}, 100}};
    Snapshot b; b.tick = 10; b.entities = {{1, {10, 0}, 100}, {2, {50, 50}, 30}};   // 2 새로 등장
    buf.Add(a); buf.Add(b);
    auto mid = buf.Sample(5.0f);
    CHECK_EQ(mid.size(), (size_t)2);
    // 1 은 보간, 2 는 보간 불가라 b 값.
    const EntitySnap* e2 = nullptr;
    for (auto& e : mid) if (e.id == 2) e2 = &e;
    CHECK(e2 != nullptr);
    CHECK(Near(e2->pos.x, 50));
}

static void TestKeepLimit() {
    SnapshotBuffer buf(4);
    for (uint32_t t = 0; t < 10; ++t) {
        Snapshot s; s.tick = t; s.entities = {{1, {(float)t, 0}, 100}};
        buf.Add(s);
    }
    CHECK_EQ(buf.OldestTick(), 6u);          // 최근 4개만(6,7,8,9)
    CHECK_EQ(buf.NewestTick(), 9u);
}

static void TestRejectOldAndResetAtWrap() {
    SnapshotBuffer buf;
    Snapshot latest; latest.tick = 10; latest.entities = {{1, {10, 0}, 100}};
    Snapshot old; old.tick = 9; old.entities = {{1, {90, 0}, 100}};
    buf.Add(latest);
    buf.Add(old);
    CHECK_EQ(buf.NewestTick(), 10u);
    CHECK(Near(buf.Sample(10.0f)[0].pos.x, 10));

    Snapshot replacement = latest;
    replacement.entities[0].pos.x = 11;
    buf.Add(replacement);
    CHECK(Near(buf.Sample(10.0f)[0].pos.x, 11));

    SnapshotBuffer wrapping;
    Snapshot before;
    before.tick = std::numeric_limits<uint32_t>::max();
    before.entities = {{1, {1, 0}, 100}};
    Snapshot after;
    after.tick = 0;
    after.entities = {{1, {2, 0}, 100}};
    wrapping.Add(before);
    wrapping.Add(after);
    CHECK_EQ(wrapping.OldestTick(), 0u);
    CHECK_EQ(wrapping.NewestTick(), 0u);
    CHECK(Near(wrapping.Sample(0.0f)[0].pos.x, 2));
}

static void RunTests() {
    TestSerialize();
    TestInterpolateMidpoint();
    TestClamp();
    TestNewEntity();
    TestKeepLimit();
    TestRejectOldAndResetAtWrap();
}

TEST_MAIN("net_snapshot")
