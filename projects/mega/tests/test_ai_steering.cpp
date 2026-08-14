// 스티어링 행동 + priority blender 검증.
#include "ai/steering/steering.hpp"
#include "test_util.h"

#include <cmath>
#include <vector>

using namespace mega::ai::steer;
using mega::math::Vec2;

static void Integrate(Agent& a, const Vec2& force, float dt) {
    a.velocity += force * dt;                       // 질량 1, 힘=가속
    a.velocity = Truncate(a.velocity, a.maxSpeed);
    a.position += a.velocity * dt;
}

static void TestSeekFlee() {
    Agent a; a.position = {0, 0}; a.velocity = {0, 0};
    Vec2 target{100, 0};
    Vec2 f = Seek(a, target);
    CHECK(f.x > 0);                                 // 목표(+x) 쪽

    Vec2 ff = Flee(a, target);
    CHECK(ff.x < 0);                                // 반대(-x)

    // Seek 으로 200스텝 → 목표에 접근.
    for (int i = 0; i < 300; ++i) Integrate(a, Seek(a, target), 1.0f / 60.0f);
    CHECK(a.position.x > 50.0f);                    // 상당히 이동
}

static void TestArrive() {
    Agent a; a.position = {0, 0}; a.velocity = {0, 0};
    a.maxSpeed = 50; a.maxForce = 200;
    Vec2 target{80, 0};
    for (int i = 0; i < 600; ++i) Integrate(a, Arrive(a, target, 20.0f), 1.0f / 60.0f);
    // 목표 근처에서 거의 정지.
    CHECK(std::fabs(a.position.x - 80.0f) < 3.0f);
    CHECK(mega::math::Length(a.velocity) < 5.0f);   // 감속해 멈춤
}

static void TestSeparation() {
    Agent a; a.position = {0, 0};
    std::vector<Agent> nb(1);
    nb[0].position = {1, 0};                        // 바로 오른쪽 이웃
    Vec2 f = Separation(a, nb, 5.0f);
    CHECK(f.x < 0);                                 // 이웃 반대(-x)로 밀림
}

static void TestCohesion() {
    Agent a; a.position = {0, 0};
    std::vector<Agent> nb(2);
    nb[0].position = {10, 0};
    nb[1].position = {10, 0};                        // 무게중심 (10,0)
    Vec2 f = Cohesion(a, nb);
    CHECK(f.x > 0);                                 // 중심(+x)으로
}

static void TestAlignment() {
    Agent a; a.position = {0, 0}; a.velocity = {0, 0};
    std::vector<Agent> nb(2);
    nb[0].velocity = {0, 50};
    nb[1].velocity = {0, 50};                        // 모두 +y 로 이동
    Vec2 f = Alignment(a, nb);
    CHECK(f.y > 0);                                 // +y 로 정렬
}

static void TestPriorityBlender() {
    // 첫 힘이 예산(maxForce=100)을 다 쓰면 둘째는 무시된다.
    PriorityBlender pb(100.0f);
    bool more1 = pb.Add({100, 0});                  // 정확히 예산 소진
    bool more2 = pb.Add({0, 100});                  // 무시되어야
    CHECK(!more1);                                  // 예산 소진
    CHECK(!more2);
    Vec2 r = pb.Result();
    CHECK(std::fabs(r.x - 100.0f) < 1e-3f);
    CHECK(std::fabs(r.y) < 1e-3f);                  // 둘째 힘 반영 안 됨

    // 예산 내 두 힘은 합산.
    PriorityBlender pb2(100.0f);
    pb2.Add({30, 0});
    pb2.Add({0, 40});
    Vec2 r2 = pb2.Result();
    CHECK(std::fabs(r2.x - 30.0f) < 1e-3f);
    CHECK(std::fabs(r2.y - 40.0f) < 1e-3f);
}

static void RunTests() {
    TestSeekFlee();
    TestArrive();
    TestSeparation();
    TestCohesion();
    TestAlignment();
    TestPriorityBlender();
}

TEST_MAIN("ai_steering")
