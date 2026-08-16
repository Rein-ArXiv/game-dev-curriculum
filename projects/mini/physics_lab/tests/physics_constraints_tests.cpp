// Step 7 (Constraints) 헤드리스 검증. chain solve 를 GUI 없이 돌린다.

#include "scenes/constraints_scene.h"

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

float RunChain(physlab::ConstraintSolver solver, int iterations,
               float baumgarte, int steps) {
    physlab::ConstraintsScene scene;
    scene.SetCanvas(0, 0, 800, 600);
    scene.SetSolver(solver);
    scene.SetIterations(iterations);
    scene.SetBaumgarte(baumgarte);
    scene.BuildChainForTest(12, 36.0f);
    for (int i = 0; i < steps; ++i) scene.Step(1.0f / 120.0f);
    return scene.LastAvgStretch();
}

// (1) chain 구성 계약 — 첫 입자는 anchor(invMass 0), 링크는 n-1 개,
//     초기 stretch 는 0.
void TestChainSetup() {
    physlab::ConstraintsScene scene;
    scene.SetCanvas(0, 0, 800, 600);
    scene.BuildChainForTest(12, 36.0f);
    CHECK(scene.Particles().size() == 12);
    CHECK(scene.Links().size() == 11);
    CHECK(scene.Particles()[0].invMass == 0.0f);
    for (size_t i = 1; i < scene.Particles().size(); ++i) {
        CHECK(scene.Particles()[i].invMass == 1.0f);
    }
    for (const auto& l : scene.Links()) {
        const auto& a = scene.Particles()[(size_t)l.a];
        const auto& b = scene.Particles()[(size_t)l.b];
        const float dx = b.pos.x - a.pos.x;
        const float dy = b.pos.y - a.pos.y;
        CHECK(std::fabs(std::sqrt(dx * dx + dy * dy) - 36.0f) < 1e-3f);
        CHECK(std::fabs(l.rest - 36.0f) < 1e-6f);
    }
    // snippet-begin: anchor-invariant
    // 한 스텝 뒤에도 anchor 는 제자리 — invMass 0 은 어떤 보정도 받지 않는다.
    const Vector2 anchor = scene.Particles()[0].pos;
    scene.Step(1.0f / 120.0f);
    CHECK(scene.Particles()[0].pos.x == anchor.x);
    CHECK(scene.Particles()[0].pos.y == anchor.y);
    CHECK(scene.Particles()[0].vel.x == 0.0f);
    CHECK(scene.Particles()[0].vel.y == 0.0f);
    // 나머지는 중력으로 내려갔어야 한다.
    CHECK(scene.Particles()[11].pos.y > anchor.y);
    // snippet-end: anchor-invariant
    std::cout << "  chain: " << scene.Particles().size() << " particles, "
              << scene.Links().size() << " links, anchor fixed at ("
              << anchor.x << ',' << anchor.y << ")\n";
}

// (2) PBD 는 iteration 이 stiffness — 반복을 늘리면 평균 stretch 가 단조 감소.
void TestPbdIterationStiffness() {
    const int counts[5] = {1, 2, 4, 8, 16};
    float stretch[5] = {0, 0, 0, 0, 0};
    for (int k = 0; k < 5; ++k) {
        stretch[k] = RunChain(physlab::ConstraintSolver::PBD, counts[k],
                              0.2f, 240);
        std::cout << "  PBD iterations=" << counts[k]
                  << " avgStretch=" << stretch[k] << '\n';
    }
    for (int k = 1; k < 5; ++k) {
        if (!(stretch[k] <= stretch[k - 1] + 1e-6f)) {
            std::cerr << "[not monotone] iterations=" << counts[k]
                      << " stretch=" << stretch[k]
                      << " prev=" << stretch[k - 1] << '\n';
        }
        CHECK(stretch[k] <= stretch[k - 1] + 1e-6f);
    }
    CHECK(stretch[0] > stretch[4] + 1e-4f);
    CHECK(stretch[4] < 0.05f);
}

// (3) SI 는 velocity 수준 제약이라 position drift 가 누적된다.
//     Baumgarte 항이 그 drift 를 되돌린다 — 0 이면 눈에 띄게 커진다.
void TestBaumgarteReducesDrift() {
    const float withoutBias = RunChain(physlab::ConstraintSolver::SI, 12,
                                       0.0f, 200);
    const float withBias    = RunChain(physlab::ConstraintSolver::SI, 12,
                                       0.2f, 200);
    std::cout << "  SI drift baumgarte=0 -> " << withoutBias
              << ", baumgarte=0.2 -> " << withBias << '\n';
    CHECK(withoutBias > withBias);
    CHECK(withoutBias > withBias * 2.0f);
    CHECK(withBias < 0.05f);
}

// (4) SI 도 iteration 을 늘리면 stretch 가 줄어든다.
void TestSiIterationStiffness() {
    const float few  = RunChain(physlab::ConstraintSolver::SI, 1, 0.2f, 200);
    const float many = RunChain(physlab::ConstraintSolver::SI, 16, 0.2f, 200);
    std::cout << "  SI iterations=1 -> " << few
              << ", iterations=16 -> " << many << '\n';
    CHECK(many < few);
}

const physlab::testkit::StepCase kCases[] = {
    {"step07", "chain_setup",           TestChainSetup},
    {"step07", "pbd_iteration_stiffness", TestPbdIterationStiffness},
    {"step07", "baumgarte_drift",       TestBaumgarteReducesDrift},
    {"step07", "si_iteration_stiffness", TestSiIterationStiffness},
};

}  // namespace

int main(int argc, char** argv) {
    return physlab::testkit::RunStepCases(
        argc, argv, kCases, (int)(sizeof(kCases) / sizeof(kCases[0])),
        "physics constraint", &failures);
}
