// 정적 기하 질의 A/B: 전수 순회 vs BVH. 히트 집합이 정확히 같은가.
//
// 그리고 무효화 규칙: 지형이 바뀐 뒤 Rebuild 하지 않으면 두 팔이 갈라진다.
// 그 갈라짐을 실제로 보여 주는 것이 이 파일의 두 번째 몫이다 — 규칙을 문서에만
// 적으면 지키지 않아도 아무 일이 일어나지 않는다.
#include "game/map_gen.hpp"
#include "game/terrain.hpp"
#include "test_util.h"

#include <algorithm>
#include <chrono>
#include <utility>
#include <cstdio>
#include <vector>

using namespace mega;
using namespace mega::game;

static DefenseMap MakeMap(uint32_t seed) {
    mapgen::TerrainParams params;
    params.seed = seed;
    DefenseMap map;
    CHECK(mapgen::Generate(params, map));
    return map;
}

// 무작위 200개 영역 질의에서 두 팔의 히트 집합이 같다.
static void TestRegionQueriesAgree() {
    const DefenseMap map = MakeMap(6);
    StaticGeometry geometry;
    geometry.Rebuild(map);
    CHECK(geometry.PrimCount() > 100);
    CHECK(geometry.NodeCount() > 1);

    uint32_t rng = 0xa5a51234u;
    auto next = [&rng] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
    std::vector<uint32_t> naive, bvh;
    size_t totalHits = 0, emptyQueries = 0;
    for (int i = 0; i < 200; ++i) {
        const float x = static_cast<float>(next() % 500u);
        const float y = static_cast<float>(next() % 500u);
        const float w = 4.0f + static_cast<float>(next() % 60u);
        const float h = 4.0f + static_cast<float>(next() % 60u);
        const math::Aabb3 region{{x, y, 0.0f}, {x + w, y + h, 1.0f}};
        geometry.QueryRegion(region, StaticQueryKind::Naive, naive);
        geometry.QueryRegion(region, StaticQueryKind::Bvh, bvh);
        CHECK_EQ(naive.size(), bvh.size());
        bool same = naive.size() == bvh.size();
        for (size_t k = 0; k < naive.size() && same; ++k) if (naive[k] != bvh[k]) same = false;
        CHECK(same);
        totalHits += naive.size();
        if (naive.empty()) ++emptyQueries;
    }
    CHECK(totalHits > 500);            // 질의가 실제로 무언가를 찾았다
    CHECK(emptyQueries < 200);
}

// 선분(사격선) 질의도 두 팔이 같은 답을 낸다.
static void TestSegmentQueriesAgree() {
    const DefenseMap map = MakeMap(8);
    StaticGeometry geometry;
    geometry.Rebuild(map);

    uint32_t rng = 0xc0ffee11u;
    auto next = [&rng] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
    int blocked = 0, clear = 0;
    // 맵을 가로지르는 긴 선분은 바위 564개 사이를 거의 언제나 지나간다 —
    // 전부 blocked 로 나오면 "두 팔이 같다"는 단언이 상수 함수 둘을 비교하는 것이
    // 된다. 타워 사거리쯤 되는 짧은 선분이라야 양쪽 답이 다 나온다.
    for (int i = 0; i < 400; ++i) {
        const math::Vec2 a{static_cast<float>(next() % 400u) + 10.0f,
                           30.0f + static_cast<float>(next() % 390u)};
        const math::Vec2 b{a.x + static_cast<float>(next() % 60u) - 30.0f,
                           a.y + static_cast<float>(next() % 60u) - 30.0f};
        const bool naive = geometry.SegmentBlocked(a, b, StaticQueryKind::Naive);
        const bool bvh = geometry.SegmentBlocked(a, b, StaticQueryKind::Bvh);
        CHECK_EQ(naive, bvh);
        if (naive) ++blocked; else ++clear;
    }
    CHECK(blocked > 20);               // 양쪽 답이 다 나온다
    CHECK(clear > 20);
}

// 무효화 규칙: 지형이 바뀌었는데 다시 짓지 않으면 답이 틀린다.
static void TestStaleIndexDiverges() {
    DefenseMap map = MakeMap(10);
    StaticGeometry fresh, stale;
    fresh.Rebuild(map);
    stale.Rebuild(map);
    const uint32_t versionBefore = fresh.Version();

    // 바위 한 줄을 연다. fresh 만 다시 짓는다.
    int opened = 0;
    for (int y = 1; y < map.Rows() - 1 && opened < 25; ++y)
        for (int x = 1; x < map.Columns() - 1 && opened < 25; ++x)
            if (map.OpenCell(x, y)) ++opened;
    CHECK(opened > 0);
    fresh.Rebuild(map);
    CHECK(fresh.Version() > versionBefore);
    CHECK(stale.Version() == versionBefore);
    CHECK(fresh.PrimCount() + static_cast<size_t>(opened) == stale.PrimCount());

    // 열린 칸을 정확히 덮는 질의에서 두 색인이 갈라진다.
    std::vector<uint32_t> freshHits, staleHits;
    size_t divergent = 0;
    for (int y = 1; y < map.Rows() - 1; ++y)
        for (int x = 1; x < map.Columns() - 1; ++x) {
            const math::Vec2 c = map.CellCenter(x, y);
            const math::Aabb3 region{{c.x - 1.0f, c.y - 1.0f, 0.0f}, {c.x + 1.0f, c.y + 1.0f, 1.0f}};
            fresh.QueryRegion(region, StaticQueryKind::Bvh, freshHits);
            stale.QueryRegion(region, StaticQueryKind::Bvh, staleHits);
            if (freshHits.size() != staleHits.size()) ++divergent;
        }
    CHECK_EQ(divergent, static_cast<size_t>(opened));
}

// 분할 축을 고정해도 히트 집합은 같아야 한다 — 갈라지면 질의 자체가 틀린 것이다.
// (Bvh 는 센트로이드 최장축을 고르지만, 프리미티브 순서를 뒤집어 트리 모양을
//  바꾼 뒤에도 같은 답이 나오는지로 같은 성질을 확인한다.)
static void TestTreeShapeDoesNotChangeAnswers() {
    const DefenseMap map = MakeMap(12);
    StaticGeometry geometry;
    geometry.Rebuild(map);

    std::vector<math::Aabb3> prims;
    for (size_t i = 0; i < geometry.PrimCount(); ++i) prims.push_back(geometry.Prim(i));
    std::vector<math::Aabb3> reversed(prims.rbegin(), prims.rend());

    math::Bvh a, b, leafy;
    a.Build(prims, 4);
    b.Build(reversed, 4);
    leafy.Build(prims, 1);

    uint32_t rng = 0x13572468u;
    auto next = [&rng] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
    for (int i = 0; i < 150; ++i) {
        const float x = static_cast<float>(next() % 460u);
        const float y = static_cast<float>(next() % 460u);
        const math::Aabb3 region{{x, y, 0.0f}, {x + 25.0f, y + 25.0f, 1.0f}};
        size_t ca = 0, cb = 0, cl = 0;
        a.Query(region, [&ca](int) { ++ca; });
        b.Query(region, [&cb](int) { ++cb; });
        leafy.Query(region, [&cl](int) { ++cl; });
        CHECK_EQ(ca, cb);
        CHECK_EQ(ca, cl);
    }
}

// 비용. 두 질의 종류에서 승자가 다르다 — 그 사실이 이 측정의 값이다.
static void TestQueryCost() {
    const DefenseMap map = MakeMap(6);
    StaticGeometry geometry;
    geometry.Rebuild(map);

    auto timeRegions = [&](StaticQueryKind kind) {
        std::vector<uint32_t> hits;
        uint32_t rng = 0x77771111u;
        auto next = [&rng] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
        size_t total = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 20000; ++i) {
            const float x = static_cast<float>(next() % 440u);
            const float y = static_cast<float>(next() % 440u);
            const math::Aabb3 region{{x, y, 0.0f}, {x + 25.0f, y + 25.0f, 1.0f}};
            geometry.QueryRegion(region, kind, hits);
            total += hits.size();
        }
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        return std::pair<double, size_t>{ms, total};
    };

    auto timeSegments = [&](StaticQueryKind kind) {
        uint32_t rng = 0x99992222u;
        auto next = [&rng] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
        size_t blocked = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 20000; ++i) {
            const math::Vec2 a{static_cast<float>(next() % 400u) + 10.0f,
                               30.0f + static_cast<float>(next() % 390u)};
            const math::Vec2 b{a.x + static_cast<float>(next() % 120u) - 60.0f,
                               a.y + static_cast<float>(next() % 120u) - 60.0f};
            if (geometry.SegmentBlocked(a, b, kind)) ++blocked;
        }
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        return std::pair<double, size_t>{ms, blocked};
    };

    const auto regionNaive = timeRegions(StaticQueryKind::Naive);
    const auto regionBvh = timeRegions(StaticQueryKind::Bvh);
    const auto segmentNaive = timeSegments(StaticQueryKind::Naive);
    const auto segmentBvh = timeSegments(StaticQueryKind::Bvh);
    CHECK_EQ(regionNaive.second, regionBvh.second);
    CHECK_EQ(segmentNaive.second, segmentBvh.second);
    std::printf("  20000 region queries (prims=%zu): naive=%.1fms bvh=%.1fms  hits=%zu\n",
                geometry.PrimCount(), regionNaive.first, regionBvh.first, regionNaive.second);
    std::printf("  20000 segment queries: naive=%.1fms bvh=%.1fms  blocked=%zu\n",
                segmentNaive.first, segmentBvh.first, segmentNaive.second);
}

static void RunTests() {
    TestRegionQueriesAgree();
    TestQueryCost();
    TestSegmentQueriesAgree();
    TestStaleIndexDiverges();
    TestTreeShapeDoesNotChangeAnswers();
}

TEST_MAIN("static_bvh_ab")
