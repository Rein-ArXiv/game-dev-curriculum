// 공간 분할 검증: Quadtree/UniformGrid/BVH 쿼리를 브루트포스와 대조.
#include "math/spatial/bvh.hpp"
#include "math/spatial/quadtree.hpp"
#include "math/spatial/uniform_grid.hpp"
#include "test_util.h"

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace mega::math;

// 결정적 PRNG(시간 비의존).
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
    uint32_t Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float Unit() { return (Next() >> 8) * (1.0f / 16777216.0f); }
    float Range(float a, float b) { return a + (b - a) * Unit(); }
};

static void TestQuadtree() {
    Rng rng(42);
    const int N = 3000;
    Aabb2 world{{0, 0}, {1000, 1000}};
    std::vector<Vec2> pts(N);
    Quadtree<int> qt;
    qt.Init(world, 8, 10);
    for (int i = 0; i < N; ++i) {
        pts[i] = {rng.Range(0, 1000), rng.Range(0, 1000)};
        qt.Insert(pts[i], i);
    }
    CHECK_EQ(qt.Size(), (size_t)N);

    // 여러 무작위 영역에서 쿼리 == 브루트포스.
    for (int q = 0; q < 30; ++q) {
        float x0 = rng.Range(0, 900), y0 = rng.Range(0, 900);
        Aabb2 region{{x0, y0}, {x0 + rng.Range(20, 200), y0 + rng.Range(20, 200)}};

        std::vector<int> got;
        qt.Query(region, [&](int id) { got.push_back(id); });

        std::vector<int> expect;
        for (int i = 0; i < N; ++i) if (region.Contains(pts[i])) expect.push_back(i);

        std::sort(got.begin(), got.end());
        std::sort(expect.begin(), expect.end());
        CHECK(got == expect);
    }

    // 반경 쿼리: 결과가 모두 반경 안.
    Vec2 c{500, 500};
    float r = 80.0f;
    int radiusHits = 0, bruteHits = 0;
    qt.QueryRadius(c, r, [&](int id) {
        float dx = pts[id].x - c.x, dy = pts[id].y - c.y;
        if (dx * dx + dy * dy <= r * r) ++radiusHits;
    });
    for (int i = 0; i < N; ++i) {
        float dx = pts[i].x - c.x, dy = pts[i].y - c.y;
        if (dx * dx + dy * dy <= r * r) ++bruteHits;
    }
    CHECK_EQ(radiusHits, bruteHits);
}

static void TestUniformGrid() {
    Rng rng(7);
    const int N = 3000;
    Aabb2 world{{0, 0}, {1000, 1000}};
    std::vector<Vec2> pts(N);
    UniformGrid<int> grid;
    grid.Init(world, 25.0f);
    for (int i = 0; i < N; ++i) {
        pts[i] = {rng.Range(0, 1000), rng.Range(0, 1000)};
        grid.Insert(pts[i], i);
    }
    CHECK_EQ(grid.Size(), (size_t)N);

    for (int q = 0; q < 30; ++q) {
        float x0 = rng.Range(0, 900), y0 = rng.Range(0, 900);
        Aabb2 region{{x0, y0}, {x0 + rng.Range(20, 200), y0 + rng.Range(20, 200)}};
        std::vector<int> got;
        grid.Query(region, [&](int id) { got.push_back(id); });
        std::vector<int> expect;
        for (int i = 0; i < N; ++i) if (region.Contains(pts[i])) expect.push_back(i);
        std::sort(got.begin(), got.end());
        std::sort(expect.begin(), expect.end());
        CHECK(got == expect);
    }
}

static void TestBvh() {
    Rng rng(99);
    const int N = 2000;
    std::vector<Aabb3> prims(N);
    for (int i = 0; i < N; ++i) {
        Vec3 c{rng.Range(0, 500), rng.Range(0, 500), rng.Range(0, 500)};
        Vec3 h{rng.Range(1, 6), rng.Range(1, 6), rng.Range(1, 6)};
        prims[i] = Aabb3::FromCenterHalf(c, h);
    }
    Bvh bvh;
    bvh.Build(prims, 4);
    CHECK_EQ(bvh.PrimCount(), (size_t)N);

    for (int q = 0; q < 30; ++q) {
        Vec3 c{rng.Range(0, 500), rng.Range(0, 500), rng.Range(0, 500)};
        float s = rng.Range(20, 80);
        Aabb3 region = Aabb3::FromCenterHalf(c, {s, s, s});

        std::vector<int> got;
        bvh.Query(region, [&](int id) { got.push_back(id); });
        std::vector<int> expect;
        for (int i = 0; i < N; ++i) if (prims[i].Intersects(region)) expect.push_back(i);

        std::sort(got.begin(), got.end());
        std::sort(expect.begin(), expect.end());
        CHECK(got == expect);   // BVH 가 겹치는 프리미티브를 정확히(빠짐·과잉 없이) 반환
    }
}

static void RunTests() {
    TestQuadtree();
    TestUniformGrid();
    TestBvh();
}

TEST_MAIN("spatial")
