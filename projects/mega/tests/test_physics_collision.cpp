// narrow-phase 충돌 + 적분기 검증.
#include "physics/integrate.hpp"
#include "physics/narrow_phase/collide.hpp"
#include "test_util.h"

#include <cmath>

using namespace mega;
using namespace mega::physics;
using math::Vec2;

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static void TestCircleCircle() {
    // 겹침: 중심 거리 3, 반지름 2+2=4 → 관통 1, 법선 +x.
    Contact c = CollideCircleCircle({0, 0}, 2.0f, {3, 0}, 2.0f);
    CHECK(c.hit);
    CHECK(Near(c.penetration, 1.0f));
    CHECK(Near(c.normal.x, 1.0f));
    CHECK(Near(c.normal.y, 0.0f));

    // 분리: 거리 5 > 4 → 충돌 없음.
    CHECK(!CollideCircleCircle({0, 0}, 2.0f, {5, 0}, 2.0f).hit);

    // 접촉 직전(거리=4): 겹침 아님.
    CHECK(!CollideCircleCircle({0, 0}, 2.0f, {4, 0}, 2.0f).hit);

    // 대각선: 거리=5(3,4), rsum=6 → 관통 1, 법선 (0.6,0.8).
    Contact d = CollideCircleCircle({0, 0}, 3.0f, {3, 4}, 3.0f);
    CHECK(d.hit);
    CHECK(Near(d.penetration, 1.0f));
    CHECK(Near(d.normal.x, 0.6f));
    CHECK(Near(d.normal.y, 0.8f));
}

static void TestCircleBox() {
    // 박스 중심 원점, 반쪽 (1,1). 원 r=0.5 가 오른쪽 면 밖에서 접근.
    // 원 중심 (1.3,0): 면(x=1)까지 0.3 < 0.5 → 관통 0.2. 법선 A(원)→B(박스) = -x.
    Contact c = CollideCircleBox({1.3f, 0.0f}, 0.5f, {0, 0}, {1, 1});
    CHECK(c.hit);
    CHECK(Near(c.penetration, 0.2f));
    CHECK(Near(c.normal.x, -1.0f));

    // 원이 멀리: (2,0), 면까지 1.0 > 0.5 → 없음.
    CHECK(!CollideCircleBox({2.0f, 0.0f}, 0.5f, {0, 0}, {1, 1}).hit);

    // 원 중심이 박스 안((0.2,0)): 관통 = r + faceDepth = 0.5 + (1-0.2)=1.3.
    Contact in = CollideCircleBox({0.2f, 0.0f}, 0.5f, {0, 0}, {1, 1});
    CHECK(in.hit);
    CHECK(in.penetration > 0.5f);
}

static void TestBoxBox() {
    // 두 AABB 반쪽 (1,1). 중심 거리 (1.5,0) → x겹침 0.5, y겹침 2 → 관통 0.5, 법선 +x.
    Contact c = CollideBoxBox({0, 0}, {1, 1}, {1.5f, 0}, {1, 1});
    CHECK(c.hit);
    CHECK(Near(c.penetration, 0.5f));
    CHECK(Near(c.normal.x, 1.0f));
    CHECK(Near(c.normal.y, 0.0f));

    // y로 더 가까우면 y축 분리: 중심 (0.2, 1.5) → x겹침 1.8, y겹침 0.5 → 법선 +y.
    Contact d = CollideBoxBox({0, 0}, {1, 1}, {0.2f, 1.5f}, {1, 1});
    CHECK(d.hit);
    CHECK(Near(d.penetration, 0.5f));
    CHECK(Near(d.normal.y, 1.0f));

    // 분리: (2.5,0) → x겹침 -0.5 → 없음.
    CHECK(!CollideBoxBox({0, 0}, {1, 1}, {2.5f, 0}, {1, 1}).hit);
}

static void TestDispatchAndFlip() {
    RigidBody2D box;  box.shape = Shape::Box({1, 1});  box.position = {0, 0};
    RigidBody2D cir;  cir.shape = Shape::Circle(0.5f); cir.position = {1.3f, 0};
    // box(A) vs circle(B): 법선 A→B 는 +x(박스에서 원 쪽).
    Contact c = Collide(box, cir);
    CHECK(c.hit);
    CHECK(Near(c.normal.x, 1.0f));
    CHECK(Near(c.penetration, 0.2f));
}

static void TestIntegrator() {
    // Symplectic Euler: g=(0,-10), dt=1/60, invMass=1.
    RigidBody2D b;
    b.shape = Shape::Circle(1.0f);
    b.SetMass(1.0f);
    b.position = {0, 100};
    Vec2 g{0, -10};
    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i) Integrate(b, g, dt);
    // 60스텝 후 속도 ≈ -10(=g*1s).
    CHECK(Near(b.velocity.y, -10.0f, 1e-3f));
    // 위치는 떨어졌다(정확값은 적분 방식 의존, 부호·대략 확인).
    CHECK(b.position.y < 100.0f);
    CHECK(b.position.y > 90.0f);          // 1초에 약 -9.x m

    // 정적 바디는 중력 무시.
    RigidBody2D s;
    s.shape = Shape::Box({1, 1});
    s.SetStatic();
    Integrate(s, g, dt);
    CHECK(Near(s.velocity.y, 0.0f));
    CHECK(Near(s.position.y, 0.0f));

    // 질량 특성: SetMass 가 invMass·invInertia 설정.
    RigidBody2D m;
    m.shape = Shape::Circle(2.0f);
    m.SetMass(4.0f);
    CHECK(Near(m.invMass, 0.25f));
    CHECK(m.invInertia > 0.0f);           // I = 0.5*4*4 = 8 → invI=0.125
    CHECK(Near(m.invInertia, 0.125f));
}

static void RunTests() {
    TestCircleCircle();
    TestCircleBox();
    TestBoxBox();
    TestDispatchAndFlip();
    TestIntegrator();
}

TEST_MAIN("physics_collision")
