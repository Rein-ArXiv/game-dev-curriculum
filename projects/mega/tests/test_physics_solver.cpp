// Sequential Impulse 솔버 + PhysicsWorld 검증: 탄성 충돌·정지·적층.
#include "physics/world.hpp"
#include "test_util.h"

#include <cmath>

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

static void RunTests() {
    TestElasticSwap();
    TestRestOnFloor();
    TestBoxRest();
    TestFreeFall();
}

TEST_MAIN("physics_solver")
