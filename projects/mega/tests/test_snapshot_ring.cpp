// net::SnapshotBufferT 의 보관소가 고정 용량 링이 된 뒤의 의미론.
//
// 덱을 링으로 바꾼 것은 최적화가 아니라 자료구조를 요구에 맞춘 것이다. 보관 개수는
// 처음부터 상한이 정해져 있었고(기본 8~16), 그래서 밀려난 스냅샷의 노드를 반납했다
// 다시 할당하는 일은 전부 낭비였다. 링은 같은 자리를 덮어쓴다.
//
// 대신 지켜야 할 것이 생겼다 — "가장 오래된 것을 정확히 하나 버린다"는 규칙이
// 이제 RingBuffer::Push 의 Full() 분기 하나에 걸려 있다. 그 분기가 죽으면 보간
// 창이 어긋나고 클라이언트가 과거를 그린다.

#include "net/session.hpp"
#include "net/snapshot.hpp"
#include "test_util.h"

#include <cstdint>
#include <limits>

using namespace mega;
using namespace mega::net;

namespace {

Snapshot At(uint32_t tick, float x) {
    Snapshot s;
    s.tick = tick;
    s.entities = {{1, {x, 0}, 100}};
    return s;
}

// 상한을 넘겨 밀어 넣으면 가장 오래된 것부터 정확히 하나씩 밀려난다.
void TestOldestFallsOutOneAtATime() {
    SnapshotBufferT<Snapshot> buffer(8);
    CHECK_EQ(buffer.Capacity(), static_cast<size_t>(8));
    for (uint32_t tick = 1; tick <= 8; ++tick) {
        buffer.Add(At(tick, static_cast<float>(tick)));
        CHECK_EQ(buffer.Size(), static_cast<size_t>(tick));
        CHECK_EQ(buffer.OldestTick(), 1u);
        CHECK_EQ(buffer.NewestTick(), tick);
    }
    for (uint32_t tick = 9; tick <= 40; ++tick) {
        buffer.Add(At(tick, static_cast<float>(tick)));
        CHECK_EQ(buffer.Size(), static_cast<size_t>(8));
        // 창은 정확히 8개 뒤에서 시작한다. 이 등식이 보간 지연의 상한을 정한다.
        CHECK_EQ(buffer.OldestTick(), tick - 7);
        CHECK_EQ(buffer.NewestTick(), tick);
    }
    // 밀려난 뒤에도 창 안의 임의 지점을 보간할 수 있다.
    const Snapshot mid = buffer.Sample(36.5, LerpSnapshot);
    CHECK(mid.entities.size() == 1);
    CHECK(mid.entities[0].pos.x > 36.0f && mid.entities[0].pos.x < 37.0f);
}

// keep 는 2의 거듭제곱으로 올림된다(모듈로가 비트 마스크가 되는 조건). "적어도
// 이만큼"이라는 하한 의미는 지켜진다.
void TestCapacityRoundsUp() {
    SnapshotBufferT<Snapshot> buffer(5);
    CHECK_EQ(buffer.Capacity(), static_cast<size_t>(8));
    for (uint32_t tick = 1; tick <= 20; ++tick) buffer.Add(At(tick, 0));
    CHECK_EQ(buffer.Size(), static_cast<size_t>(8));
    CHECK(buffer.Size() >= 5);
}

// 같은 틱 재수신은 자리를 늘리지 않고 덮어쓴다.
void TestSameTickOverwritesInPlace() {
    SnapshotBufferT<Snapshot> buffer(4);
    buffer.Add(At(7, 1.0f));
    buffer.Add(At(7, 2.0f));
    CHECK_EQ(buffer.Size(), static_cast<size_t>(1));
    CHECK(buffer.Newest()->entities[0].pos.x == 2.0f);
}

// 카운터 순환에서 링을 비우고 새 타임라인을 연다. Clear 는 인덱스만 되감으므로
// 이 동작에 할당이 없다.
void TestWrapStartsNewEpoch() {
    SnapshotBufferT<Snapshot> buffer(8);
    const uint32_t high = std::numeric_limits<uint32_t>::max() - 1;
    buffer.Add(At(high, 1.0f));
    buffer.Add(At(high + 1, 2.0f));
    const uint32_t epochBefore = buffer.Epoch();
    buffer.Add(At(0, 3.0f));                     // 순환
    CHECK_EQ(buffer.Size(), static_cast<size_t>(1));
    CHECK_EQ(buffer.OldestTick(), 0u);
    CHECK_EQ(buffer.NewestTick(), 0u);
    CHECK(buffer.Epoch() == epochBefore + 1);
    CHECK(!buffer.Add(At(high, 4.0f)));          // 지나간 틱은 거절
}

// 링 위에서도 보간 뷰의 시계 규칙(전진·클램프)이 그대로다.
void TestInterpolatedViewOnRing() {
    InterpolatedView<Snapshot> view(InterpolationClock{3.0, 2.0, 8});
    for (uint32_t tick = 1; tick <= 12; ++tick) view.Add(At(tick, static_cast<float>(tick)));
    CHECK(view.Ready());
    CHECK_EQ(view.NewestTick(), 12u);
    view.Advance(100.0);                         // 위쪽 클램프
    CHECK(view.RenderTick() <= 10.0 + 1e-9);
    view.Advance(-1000.0);                       // 아래쪽 클램프(버퍼 밖으로 못 나간다)
    CHECK(view.RenderTick() >= 5.0 - 1e-9);
}

}  // namespace

static void RunTests() {
    TestOldestFallsOutOneAtATime();
    TestCapacityRoundsUp();
    TestSameTickOverwritesInPlace();
    TestWrapStartsNewEpoch();
    TestInterpolatedViewOnRing();
}

TEST_MAIN("snapshot_ring")
