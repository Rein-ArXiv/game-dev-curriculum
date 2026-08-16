// Step 6 (Response) 헤드리스 검증. GatherContacts / ApplySequentialImpulse /
// ApplyPositionCorrection 을 GUI 없이 직접 부른다.

#include "scenes/response_scene.h"
#include "core/physics_core.h"

#include "step_cases.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr)                                                          \
    do {                                                                     \
        if (!(expr)) {                                                       \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':' \
                      << __LINE__ << ")\n";                                  \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

physlab::CircleRb Circle(float x, float y, float radius = 8.0f) {
    physlab::CircleRb b;
    b.pos     = {x, y};
    b.prevPos = b.pos;
    b.vel     = {0, 0};
    b.radius  = radius;
    b.mass    = 1.0f;
    b.invMass = 1.0f;
    return b;
}

// 캔버스는 충분히 커서 두 body 가 벽 / 바닥에 닿지 않게 잡는다.
physlab::ResponseScene MakePairScene(float gap) {
    physlab::ResponseScene scene;
    scene.SetCanvas(0, 0, 4000, 4000);
    scene.SetBodies({Circle(100.0f, 100.0f), Circle(100.0f + gap, 100.0f)});
    return scene;
}

// (1) GatherContacts 의 depth / normal 을 해석적 리터럴로 못 박는다.
//     반지름 8 두 개가 중심거리 12 면 depth = 4, normal = (1,0) (i -> j).
void TestGatherContactsGeometry() {
    physlab::ResponseScene scene = MakePairScene(12.0f);
    std::vector<physlab::Contact> contacts;
    scene.GatherContacts(contacts);
    CHECK(contacts.size() == 1);
    if (contacts.size() != 1) return;
    const auto& c = contacts.front();
    // snippet-begin: contact-geometry
    // 반지름 8 두 개의 중심거리가 12 면 깊이는 정확히 4, 법선은 i -> j 방향.
    CHECK(c.i == 0);
    CHECK(c.j == 1);
    CHECK(std::fabs(c.depth - 4.0f) < 1e-4f);
    CHECK(std::fabs(c.normal.x - 1.0f) < 1e-5f);
    CHECK(std::fabs(c.normal.y) < 1e-5f);
    CHECK(c.lambda == 0.0f);
    // snippet-end: contact-geometry
    std::cout << "  pair contact: depth=" << c.depth << " normal=("
              << c.normal.x << ',' << c.normal.y << ")\n";

    // 정확히 맞닿으면(거리 == 반지름 합) 접촉이 아니다 — dist < r 규약.
    physlab::ResponseScene touching = MakePairScene(16.0f);
    std::vector<physlab::Contact> none;
    touching.GatherContacts(none);
    CHECK(none.empty());

    // 벽 / 바닥 접촉: 좌벽은 x = 4, 바닥은 y = h - 4.
    physlab::ResponseScene walls;
    walls.SetCanvas(0, 0, 200, 200);
    walls.SetBodies({Circle(8.0f, 100.0f)});   // 좌벽을 4px 파고듦
    std::vector<physlab::Contact> wallContacts;
    walls.GatherContacts(wallContacts);
    CHECK(wallContacts.size() == 1);
    if (wallContacts.size() == 1) {
        CHECK(wallContacts[0].j == -1);
        CHECK(std::fabs(wallContacts[0].depth - 4.0f) < 1e-4f);
        CHECK(wallContacts[0].normal.x == 1.0f);
        CHECK(wallContacts[0].normal.y == 0.0f);
        std::cout << "  wall contact: j=" << wallContacts[0].j << " depth="
                  << wallContacts[0].depth << '\n';
    }
}

// (2) ApplyPositionCorrection 은 (depth - slop) * factor 만큼만, 질량 비율로
//     나눠 민다. 리터럴로 계산 가능: (4 - 0.5) * 0.2 = 0.7, 둘로 나눠 0.35.
void TestPositionCorrectionAmount() {
    physlab::ResponseScene scene = MakePairScene(12.0f);
    CHECK(std::fabs(scene.Slop() - 0.5f) < 1e-6f);
    CHECK(std::fabs(scene.PositionCorrectFactor() - 0.2f) < 1e-6f);

    std::vector<physlab::Contact> contacts;
    scene.GatherContacts(contacts);
    const float before0 = scene.Bodies()[0].pos.x;
    const float before1 = scene.Bodies()[1].pos.x;
    scene.ApplyPositionCorrection(contacts);
    const float after0 = scene.Bodies()[0].pos.x;
    const float after1 = scene.Bodies()[1].pos.x;
    CHECK(std::fabs((before0 - after0) - 0.35f) < 1e-4f);
    CHECK(std::fabs((after1 - before1) - 0.35f) < 1e-4f);
    std::cout << "  (depth 4 - slop " << scene.Slop() << ") * factor "
              << scene.PositionCorrectFactor() << " split two ways: "
              << (before0 - after0) << " each\n";

    // slop 이하의 얕은 침투는 건드리지 않는다 (지터 방지).
    physlab::ResponseScene shallow = MakePairScene(15.8f);   // depth 0.2
    std::vector<physlab::Contact> shallowContacts;
    shallow.GatherContacts(shallowContacts);
    CHECK(shallowContacts.size() == 1);
    const float sx0 = shallow.Bodies()[0].pos.x;
    shallow.ApplyPositionCorrection(shallowContacts);
    CHECK(shallow.Bodies()[0].pos.x == sx0);
}

// (3) ApplySequentialImpulse 는 접근 속도를 목표까지 없앤다. 정면 충돌
//     (+v, -v) 에서 반발 임계 이하면 target = 0 → 상대 법선 속도 0.
void TestSequentialImpulseConverges() {
    physlab::ResponseScene scene = MakePairScene(12.0f);
    auto bodies = scene.Bodies();
    bodies[0].vel = {20.0f, 0.0f};    // 서로 접근 (임계 50 이하)
    bodies[1].vel = {-20.0f, 0.0f};
    scene.SetBodies(bodies);
    scene.SetIterations(8);

    std::vector<physlab::Contact> contacts;
    scene.GatherContacts(contacts);
    CHECK(contacts.size() == 1);
    scene.ApplySequentialImpulse(contacts);

    const float relative = scene.Bodies()[1].vel.x - scene.Bodies()[0].vel.x;
    CHECK(std::fabs(relative) < 1e-3f);
    // 운동량 보존 — 같은 질량이면 속도 합이 0 그대로.
    CHECK(std::fabs(scene.Bodies()[0].vel.x + scene.Bodies()[1].vel.x) < 1e-4f);
    CHECK(contacts.front().lambda > 0.0f);

    // 임계(50 px/s) 를 넘는 접근은 반발이 살아난다 — e = 0.1 이므로
    // 상대 속도 -200 → +20 으로 튕겨 나가야 한다.
    physlab::ResponseScene fast = MakePairScene(12.0f);
    auto fastBodies = fast.Bodies();
    fastBodies[0].vel = {100.0f, 0.0f};
    fastBodies[1].vel = {-100.0f, 0.0f};
    fast.SetBodies(fastBodies);
    fast.SetIterations(8);
    std::vector<physlab::Contact> fastContacts;
    fast.GatherContacts(fastContacts);
    fast.ApplySequentialImpulse(fastContacts);
    const float fastRelative =
        fast.Bodies()[1].vel.x - fast.Bodies()[0].vel.x;
    const float expected = physlab::core::RestitutionTarget(
        -200.0f, fast.Restitution(), 50.0f);
    CHECK(std::fabs(expected - 20.0f) < 1e-4f);
    CHECK(std::fabs(fastRelative - 20.0f) < 1e-2f);
    std::cout << "  approach -40 -> relative " << relative
              << " ; approach -200 -> relative " << fastRelative << '\n';

    // 이미 분리 중인 접촉은 임펄스를 만들지 않는다 (λ >= 0 클램프).
    physlab::ResponseScene separating = MakePairScene(12.0f);
    auto sepBodies = separating.Bodies();
    sepBodies[0].vel = {-30.0f, 0.0f};
    sepBodies[1].vel = {30.0f, 0.0f};
    separating.SetBodies(sepBodies);
    separating.SetIterations(8);
    std::vector<physlab::Contact> sepContacts;
    separating.GatherContacts(sepContacts);
    separating.ApplySequentialImpulse(sepContacts);
    CHECK(separating.Bodies()[0].vel.x == -30.0f);
    CHECK(separating.Bodies()[1].vel.x == 30.0f);
    CHECK(sepContacts.front().lambda == 0.0f);
}

// (4) iteration 을 늘리면 남는 평균 침투가 단조 감소하고, 16 회에서는
//     slop + 1px 이하로 떨어진다 — step06 이 주장하는 수렴.
void TestIterationConvergence() {
    const int iterationCounts[5] = {1, 2, 4, 8, 16};
    float pen[5] = {0, 0, 0, 0, 0};
    float slop = 0.0f;

    for (int k = 0; k < 5; ++k) {
        physlab::ResponseScene scene;
        scene.SetCanvas(0, 0, 400, 320);
        scene.SetAlgo(physlab::ResponseAlgo::SequentialImpulse);
        scene.SetBodyCount(24);
        scene.SetIterations(iterationCounts[k]);
        scene.ResetBodiesForTest();
        for (int step = 0; step < 400; ++step) scene.Step(1.0f / 120.0f);
        pen[k] = scene.LastAvgPen();
        slop   = scene.Slop();
        CHECK(scene.LastContacts() > 0);
    }

    for (int k = 0; k < 5; ++k) {
        std::cout << "  iterations=" << iterationCounts[k]
                  << " avgPen=" << pen[k] << '\n';
    }
    // snippet-begin: iteration-convergence
    for (int k = 1; k < 5; ++k) {
        if (!(pen[k] <= pen[k - 1] + 1e-4f)) {
            std::cerr << "[not monotone] iterations=" << iterationCounts[k]
                      << " pen=" << pen[k] << " prev=" << pen[k - 1] << '\n';
        }
        CHECK(pen[k] <= pen[k - 1] + 1e-4f);
    }
    CHECK(pen[4] <= slop + 1.0f);
    // 1 회 반복은 실제로 더 깊게 파고들어야 한다 (전부 같은 값이면 무의미).
    CHECK(pen[0] > pen[4] + 1e-3f);
    // snippet-end: iteration-convergence
}

const physlab::testkit::StepCase kCases[] = {
    {"step06", "gather_contacts",     TestGatherContactsGeometry},
    {"step06", "position_correction", TestPositionCorrectionAmount},
    {"step06", "sequential_impulse",  TestSequentialImpulseConverges},
    {"step06", "iteration_convergence", TestIterationConvergence},
};

}  // namespace

int main(int argc, char** argv) {
    return physlab::testkit::RunStepCases(
        argc, argv, kCases, (int)(sizeof(kCases) / sizeof(kCases[0])),
        "physics response", &failures);
}
