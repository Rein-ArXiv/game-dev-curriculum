// 런타임 모듈 검증: 파티클·입력 매핑·리소스 핸들.
#include "engine/input.hpp"
#include "engine/particles.hpp"
#include "engine/resource.hpp"
#include "test_util.h"

#include <cmath>
#include <string>

using namespace mega::engine;
using mega::math::Vec3;

static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

static void TestParticles() {
    ParticleSystem ps;
    ps.Emit({0, 0, 0}, {10, 0, 0}, /*life*/ 1.0f);
    ps.Emit({0, 0, 0}, {0, 5, 0}, 2.0f);
    CHECK_EQ(ps.AliveCount(), (size_t)2);

    ps.Update(0.5f);                       // 0.5초
    CHECK_EQ(ps.AliveCount(), (size_t)2);
    // 첫 파티클 x = 10*0.5 = 5.
    bool found = false;
    for (auto& p : ps.Particles()) if (Near(p.pos.x, 5.0f) && Near(p.vel.x, 10.0f)) found = true;
    CHECK(found);

    ps.Update(0.6f);                       // 누적 1.1초 → 첫 파티클(life 1.0) 사망
    CHECK_EQ(ps.AliveCount(), (size_t)1);

    // 페이드: 남은 수명 비율.
    ParticleSystem ps2;
    ps2.Emit({0, 0, 0}, {0, 0, 0}, 2.0f);
    ps2.Update(0.5f);                      // 1.5/2.0 남음
    CHECK(Near(ParticleSystem::Alpha(ps2.Particles()[0]), 0.75f));

    // 중력 적용.
    ParticleSystem ps3;
    ps3.Emit({0, 10, 0}, {0, 0, 0}, 5.0f);
    ps3.Update(1.0f, Vec3{0, -10, 0});     // vy = -10, y = 10 + (-10)*1 = 0
    CHECK(Near(ps3.Particles()[0].vel.y, -10.0f));
}

static void TestInputMap() {
    InputMap input;
    input.Bind(32, "jump");                // space
    input.Bind(70, "fire");                // F

    CHECK(!input.IsActive("jump"));
    input.BeginFrame();
    input.OnKey(32, true);
    CHECK(input.IsActive("jump"));
    CHECK(input.WasPressed("jump"));
    input.BeginFrame();
    input.OnKey(32, true);
    CHECK(!input.WasPressed("jump"));
    input.OnKey(32, false);
    CHECK(!input.IsActive("jump"));
    CHECK(input.WasReleased("jump"));

    input.BeginFrame();
    input.OnKey(70, true);
    CHECK(input.IsActive("fire"));
    CHECK(input.WasPressed("fire"));
    CHECK(!input.IsActive("jump"));
    input.OnKey(999, true);                // 바인딩 안 된 키 → 무시
    CHECK(!input.IsActive("unknown"));

    // 데드존.
    CHECK(Near(InputMap::ApplyDeadzone(0.1f, 0.2f), 0.0f));   // 데드존 안 → 0
    CHECK(InputMap::ApplyDeadzone(0.6f, 0.2f) > 0.0f);        // 밖 → 양수
    CHECK(Near(InputMap::ApplyDeadzone(1.0f, 0.2f), 1.0f));   // 최대는 1
    CHECK(InputMap::ApplyDeadzone(-0.6f, 0.2f) < 0.0f);       // 부호 유지
}

static void TestResourcePool() {
    ResourcePool<std::string> pool;
    Handle h1 = pool.Load("mesh_a");
    Handle h2 = pool.Load("mesh_b");
    CHECK(pool.IsValid(h1));
    CHECK(pool.IsValid(h2));
    CHECK_EQ(*pool.Get(h1), std::string("mesh_a"));
    CHECK_EQ(pool.AliveCount(), (size_t)2);

    // 레퍼런스 카운트.
    pool.AddRef(h1);                       // refcount 2
    CHECK_EQ(pool.RefCount(h1), 2);
    pool.Release(h1);                      // 1 → 살아 있음
    CHECK(pool.IsValid(h1));
    pool.Release(h1);                      // 0 → 해제
    CHECK(!pool.IsValid(h1));
    CHECK(pool.Get(h1) == nullptr);        // 무효 핸들 접근 안전
    CHECK_EQ(pool.AliveCount(), (size_t)1);

    // 슬롯 재사용 + 세대로 옛 핸들 무효화.
    Handle h3 = pool.Load("mesh_c");
    CHECK_EQ(h3.index, h1.index);          // 같은 슬롯 재사용
    CHECK(h3.generation != h1.generation); // 다른 세대
    CHECK(pool.IsValid(h3));
    CHECK(!pool.IsValid(h1));              // 옛 핸들은 여전히 무효
    CHECK_EQ(*pool.Get(h3), std::string("mesh_c"));
}

static void RunTests() {
    TestParticles();
    TestInputMap();
    TestResourcePool();
}

TEST_MAIN("engine_runtime")
