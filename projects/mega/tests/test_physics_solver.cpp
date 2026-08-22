// Sequential Impulse 솔버 + PhysicsWorld 검증: 탄성 충돌·정지·적층.
#include "physics/world.hpp"
#include "test_util.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace mega;
using namespace mega::physics;
using math::Vec2;

static bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// 정면 탄성 충돌(e=1, 동일 질량): 속도가 교환된다.
static void TestElasticSwap() {
    PhysicsWorld w;
    w.gravity = {0, 0};

    RigidBody2D a; a.shape = Shape::Circle(0.5f); a.SetMass(1.0f);
    a.position = {-3, 0}; a.velocity = {4, 0}; a.restitution = 1.0f; a.friction = 0;
    RigidBody2D b; b.shape = Shape::Circle(0.5f); b.SetMass(1.0f);
    b.position = { 3, 0}; b.velocity = {-4, 0}; b.restitution = 1.0f; b.friction = 0;
    int ia = w.AddBody(a), ib = w.AddBody(b);

    for (int i = 0; i < 200; ++i) w.Step(1.0f / 60.0f);

    // 충돌 후 서로 반대로 튕겨 멀어진다(질량 같으니 속도 교환).
    CHECK(w.Body(ia).velocity.x < -2.0f);   // A 는 -x 로
    CHECK(w.Body(ib).velocity.x >  2.0f);   // B 는 +x 로
    // 운동량 보존(합 ≈ 0).
    float p = w.Body(ia).velocity.x + w.Body(ib).velocity.x;
    CHECK(Near(p, 0.0f, 0.5f));
}

// 중력 하에서 원이 정적 바닥 위에 정지한다.
static void TestRestOnFloor() {
    PhysicsWorld w;
    w.gravity = {0, -10};

    RigidBody2D floor; floor.shape = Shape::Box({50, 1}); floor.SetStatic();
    floor.position = {0, 0}; floor.friction = 0.5f;
    w.AddBody(floor);

    RigidBody2D ball; ball.shape = Shape::Circle(1.0f); ball.SetMass(1.0f);
    ball.position = {0, 10}; ball.restitution = 0.0f; ball.friction = 0.5f;
    int ib = w.AddBody(ball);

    for (int i = 0; i < 400; ++i) w.Step(1.0f / 60.0f);

    // 바닥 윗면 y=1, 반지름 1 → 정지 높이 y≈2.
    CHECK(Near(w.Body(ib).position.y, 2.0f, 0.1f));
    CHECK(std::fabs(w.Body(ib).velocity.y) < 0.5f);   // 정지
    CHECK(!std::isnan(w.Body(ib).position.y));
}

// 박스가 바닥에 안정적으로 쌓인다(box-box AABB, 안 터짐).
static void TestBoxRest() {
    PhysicsWorld w;
    w.gravity = {0, -10};

    RigidBody2D floor; floor.shape = Shape::Box({50, 1}); floor.SetStatic();
    floor.position = {0, 0};
    w.AddBody(floor);

    RigidBody2D box; box.shape = Shape::Box({1, 1}); box.SetMass(1.0f);
    box.position = {0, 8}; box.restitution = 0.0f; box.friction = 0.5f;
    int ibx = w.AddBody(box);

    for (int i = 0; i < 400; ++i) w.Step(1.0f / 60.0f);

    // 바닥 윗면 1 + 박스 반쪽 1 → y≈2.
    CHECK(Near(w.Body(ibx).position.y, 2.0f, 0.15f));
    CHECK(std::fabs(w.Body(ibx).velocity.y) < 0.5f);
    CHECK(w.ContactCount() >= 1);                      // 접촉 유지
}

// 중력 자유낙하(접촉 없음)는 적분만: 1초 후 속도 ≈ -g.
static void TestFreeFall() {
    PhysicsWorld w;
    w.gravity = {0, -9.81f};
    RigidBody2D ball; ball.shape = Shape::Circle(0.5f); ball.SetMass(1.0f);
    ball.position = {0, 1000};
    int ib = w.AddBody(ball);
    for (int i = 0; i < 60; ++i) w.Step(1.0f / 60.0f);
    CHECK(Near(w.Body(ib).velocity.y, -9.81f, 0.05f));
    CHECK(w.Body(ib).position.y < 1000.0f);
}

// ── G2: 잔해 적층이 요구하는 세 성질 ───────────────────────────────

// 5단 적층에서 최대 관통은 slop 근처이고, 반복을 올리면 단조 감소한다.
static float StackPenetration(int iterations) {
    PhysicsWorld w;
    w.gravity = {0, -20};
    w.velocityIterations = iterations;

    RigidBody2D floor; floor.shape = Shape::Box({20, 1}); floor.SetStatic();
    floor.position = {0, 0}; floor.friction = 0.6f; floor.restitution = 0.0f;
    w.AddBody(floor);
    for (int i = 0; i < 5; ++i) {
        RigidBody2D box; box.shape = Shape::Box({1, 1}); box.SetMass(1.0f);
        box.position = {0, 3.0f + i * 2.05f};
        box.friction = 0.6f; box.restitution = 0.0f;
        w.AddBody(box);
    }
    for (int i = 0; i < 600; ++i) w.Step(1.0f / 60.0f);
    return w.MaxPenetration();     // 정지 상태의 관통
}

static void TestStackPenetrationMonotone() {
    const float four = StackPenetration(4);
    const float eight = StackPenetration(8);
    const float sixteen = StackPenetration(16);
    std::printf("  stack penetration: iters4=%.5f iters8=%.5f iters16=%.5f (slop=%.3f)\n",
                four, eight, sixteen, SolverParams{}.slop);
    CHECK(eight <= four + 1e-5f);
    CHECK(sixteen <= eight + 1e-5f);
    CHECK(sixteen <= SolverParams{}.slop + 0.05f);   // slop + eps 안쪽
}

// 마찰 임펄스는 쿨롱 한계를 넘지 않는다: |tangent| <= mu * normal.
static void TestFrictionWithinCoulombLimit() {
    RigidBody2D a; a.shape = Shape::Box({50, 1}); a.SetStatic();
    a.position = {0, 0}; a.friction = 0.5f;
    RigidBody2D b; b.shape = Shape::Circle(1.0f); b.SetMass(1.0f);
    b.position = {0, 0.9f}; b.friction = 0.5f; b.velocity = {12.0f, -3.0f};

    Contact c = Collide(a, b);
    CHECK(c.hit);
    ContactConstraint k = MakeConstraint(a, b, c, 1.0f / 60.0f);
    for (int i = 0; i < 32; ++i) {
        SolveVelocity(k);
        CHECK(k.normalImpulse >= 0.0f);                        // 당기지 않는다
        CHECK(std::fabs(k.tangentImpulse) <= k.friction * k.normalImpulse + 1e-5f);
    }
    CHECK(std::fabs(b.velocity.x) < 12.0f);                    // 마찰이 실제로 먹었다
}

// 적분기 A/B: 같은 적층에서 Symplectic 은 수렴하고 Explicit 은 파고든다.
//
// 차이의 출처는 한 줄이다. 솔버가 이번 스텝에 만든 임펄스는 **속도**에 들어간다.
// Symplectic 은 그 새 속도로 위치를 옮기므로 접촉 응답이 같은 스텝의 위치에
// 반영되지만, Explicit 은 갱신 전 속도로 옮기므로 응답이 한 스텝 늦는다.
// 그 지연이 매 스텝 누적돼 적층이 서로를 파고든다.
static void TestIntegratorStackStability() {
    auto worstPenetration = [](Integrator kind) {
        PhysicsWorld w;
        w.gravity = {0, -20};
        w.integrator = kind;
        RigidBody2D floor; floor.shape = Shape::Box({20, 1}); floor.SetStatic();
        floor.position = {0, 0}; floor.friction = 0.6f; floor.restitution = 0.0f;
        w.AddBody(floor);
        for (int i = 0; i < 5; ++i) {
            RigidBody2D box; box.shape = Shape::Box({1, 1}); box.SetMass(1.0f);
            box.position = {0, 6.0f + i * 2.6f};       // 떨어뜨려 충격을 만든다
            box.friction = 0.6f; box.restitution = 0.0f;
            w.AddBody(box);
        }
        float worst = 0.0f;
        for (int i = 0; i < 600; ++i) {
            w.Step(1.0f / 60.0f);
            worst = std::fmax(worst, w.MaxPenetration());
        }
        return worst;
    };
    const float symplectic = worstPenetration(Integrator::Symplectic);
    const float explicitEuler = worstPenetration(Integrator::Explicit);
    std::printf("  impact penetration: symplectic=%.5f explicit=%.5f\n",
                symplectic, explicitEuler);
    CHECK(explicitEuler > symplectic * 1.2f);
}

// 광역 탐색 A/B: SetBroadphase 호출 여부가 결과를 바꾸지 않는다.
static void TestBroadphaseArmsMatch() {
    auto run = [](bool grid) {
        PhysicsWorld w;
        w.gravity = {0, -20};
        if (grid) w.SetBroadphase(math::Aabb2{{-30, -5}, {30, 60}}, 4.0f);
        // 바닥을 조각으로 깐다. 커다란 정적 상자 하나로 깔면 그 바디의 경계
        // 반지름이 전역 최댓값이 되고, 격자 질의 반경(= 최대 반지름 x 2)이 맵을
        // 통째로 덮어 격자가 전수 탐색으로 퇴화한다 — 잔해 필드가 바위를 칸 크기
        // 상자로 쪼개 두는 이유가 정확히 이것이다.
        for (int i = 0; i < 13; ++i) {
            RigidBody2D slab; slab.shape = Shape::Box({2, 1}); slab.SetStatic();
            slab.position = {static_cast<float>(i) * 4.0f - 24.0f, 0.0f};
            slab.friction = 0.5f;
            w.AddBody(slab);
        }
        for (int i = 0; i < 80; ++i) {
            RigidBody2D ball; ball.shape = Shape::Circle(0.5f); ball.SetMass(1.0f);
            ball.position = {static_cast<float>(i % 10) * 1.1f - 5.0f,
                             3.0f + static_cast<float>(i / 10) * 1.3f};
            ball.friction = 0.5f; ball.restitution = 0.1f;
            w.AddBody(ball);
        }
        std::vector<float> trace;
        size_t peakPairs = 0;
        for (int i = 0; i < 300; ++i) {
            w.Step(1.0f / 60.0f);
            peakPairs = peakPairs > w.PairCount() ? peakPairs : w.PairCount();
        }
        for (size_t i = 0; i < w.Count(); ++i) {
            trace.push_back(w.Body(static_cast<int>(i)).position.x);
            trace.push_back(w.Body(static_cast<int>(i)).position.y);
        }
        trace.push_back(static_cast<float>(peakPairs));
        return trace;
    };
    const std::vector<float> naive = run(false);
    const std::vector<float> grid = run(true);
    CHECK_EQ(naive.size(), grid.size());
    size_t diffs = 0;
    for (size_t i = 0; i + 1 < naive.size(); ++i) if (naive[i] != grid[i]) ++diffs;
    std::printf("  broadphase pairs: naive=%.0f grid=%.0f (position diffs=%zu)\n",
                naive.back(), grid.back(), diffs);
    CHECK_EQ(diffs, size_t{0});
    CHECK(grid.back() < naive.back());
}

static void RunTests() {
    TestElasticSwap();
    TestRestOnFloor();
    TestBoxRest();
    TestFreeFall();
    TestStackPenetrationMonotone();
    TestFrictionWithinCoulombLimit();
    TestIntegratorStackStability();
    TestBroadphaseArmsMatch();
}

TEST_MAIN("physics_solver")
