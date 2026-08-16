// Step 8 (Measure) 헤드리스 검증. step08 의 '기본 토글이면 같은 프로세스
// 안에서 항상 bit-exact' 주장을 GUI 의 KEY_T 없이 ctest 가 확인한다.

#include "scenes/measure_scene.h"
#include "scenes/response_scene.h"

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

physlab::MeasureScene MakeScene(bool shuffle) {
    physlab::MeasureScene scene;
    scene.SetCanvas(0, 0, 400, 320);
    scene.SetBodyCount(24);
    scene.SetSeed(1u);
    scene.SetIterations(8);
    scene.SetShuffleContacts(shuffle);
    return scene;
}

float MaxDelta(const std::vector<physlab::MeasureBody>& a,
               const std::vector<physlab::MeasureBody>& b) {
    float worst = 0.0f;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const float dx = a[i].pos.x - b[i].pos.x;
        const float dy = a[i].pos.y - b[i].pos.y;
        worst = std::max(worst, std::sqrt(dx * dx + dy * dy));
    }
    return worst;
}

std::vector<physlab::MeasureBody> Run(const physlab::MeasureScene& scene,
                                      int frames) {
    std::vector<physlab::MeasureBody> sim;
    scene.InitBodies(sim, 24, 1u);
    for (int i = 0; i < frames; ++i) scene.StepSim(sim, 1.0f / 120.0f);
    return sim;
}

// (1) 같은 seed / 같은 설정으로 600 스텝 두 번 → 위치가 bit-exact.
void TestBitExactRepeat() {
    const physlab::MeasureScene scene = MakeScene(false);
    const auto first  = Run(scene, 600);
    const auto second = Run(scene, 600);
    CHECK(first.size() == 24);
    CHECK(MaxDelta(first, second) == 0.0f);
    std::cout << "  ordered maxDelta=" << MaxDelta(first, second)
              << " after 600 steps\n";

    // 초기 배치 자체도 seed 로 완전히 결정된다.
    std::vector<physlab::MeasureBody> a, b, other;
    scene.InitBodies(a, 24, 1u);
    scene.InitBodies(b, 24, 1u);
    scene.InitBodies(other, 24, 2u);
    CHECK(MaxDelta(a, b) == 0.0f);
    CHECK(MaxDelta(a, other) > 0.0f);

    // 시뮬이 실제로 움직였는지 — 아무것도 안 해도 bit-exact 는 성립한다.
    CHECK(MaxDelta(a, first) > 1.0f);
    for (const auto& body : first) {
        CHECK(std::isfinite(body.pos.x));
        CHECK(std::isfinite(body.pos.y));
    }
}

// (2) contact 순서를 섞으면 Gauss-Seidel 풀이가 갈라진다.
void TestShuffleBreaksDeterminism() {
    physlab::MeasureScene scene = MakeScene(true);
    const auto first  = Run(scene, 600);
    const auto second = Run(scene, 600);
    const float delta = MaxDelta(first, second);
    std::cout << "  shuffled maxDelta=" << delta << '\n';
    CHECK(delta > 1e-3f);
}

// (3) GUI 의 KEY_T 가 부르는 RunDeterminismTest 자체의 분류도 확인한다.
void TestSceneOwnDeterminismReport() {
    physlab::MeasureScene ordered = MakeScene(false);
    ordered.SetTestFrames(600);
    ordered.RunDeterminismTest();
    CHECK(ordered.TestRan());
    CHECK(ordered.LastTestMaxDelta() == 0.0f);
    CHECK(ordered.LastTestAvgDelta() == 0.0f);
    CHECK(ordered.LastTestDeterministic());
    CHECK(!ordered.LastTestShuffle());

    physlab::MeasureScene shuffled = MakeScene(true);
    shuffled.SetTestFrames(600);
    shuffled.RunDeterminismTest();
    CHECK(shuffled.TestRan());
    CHECK(shuffled.LastTestMaxDelta() > 1e-3f);
    CHECK(!shuffled.LastTestDeterministic());
    CHECK(shuffled.LastTestShuffle());

    // 캔버스가 없으면(창 크기를 아직 모르면) 테스트는 실행되지 않는다.
    physlab::MeasureScene noCanvas;
    noCanvas.RunDeterminismTest();
    CHECK(!noCanvas.TestRan());
}

// (4) 측정 지표 계약 — 겹침이 있으면 avgPen > 0, 정지 상태면 KE == 0.
void TestMetrics() {
    const physlab::MeasureScene scene = MakeScene(false);
    std::vector<physlab::MeasureBody> sim;
    sim.push_back({{100.0f, 100.0f}, {0.0f, 0.0f}, 12.0f});
    sim.push_back({{108.0f, 100.0f}, {0.0f, 0.0f}, 12.0f});
    float pen = -1.0f, ke = -1.0f;
    scene.ComputeMetrics(sim, pen, ke);
    CHECK(std::fabs(pen - 16.0f) < 1e-4f);   // 24 - 8
    CHECK(ke == 0.0f);
    const float overlapPen = pen;

    sim[0].vel = {100.0f, 0.0f};
    scene.ComputeMetrics(sim, pen, ke);
    CHECK(std::fabs(ke - 0.5f) < 1e-4f);     // 0.5 * (100^2) / 100^2
    const float movingKe = ke;

    std::vector<physlab::MeasureBody> apart;
    apart.push_back({{0.0f, 0.0f}, {0.0f, 0.0f}, 12.0f});
    apart.push_back({{100.0f, 0.0f}, {0.0f, 0.0f}, 12.0f});
    scene.ComputeMetrics(apart, pen, ke);
    CHECK(pen == 0.0f);
    std::cout << "  metrics: overlapping avgPen=" << overlapPen
              << " separated avgPen=" << pen << " one body at 100 px/s KE="
              << movingKe << '\n';
}

// (5) 절대 기준 — 반복성만 보면 '아무것도 하지 않는 StepSim' 도 통과한다.
//     900 스텝 뒤 모든 body 는 벽 / 바닥 안에서 가라앉아 있어야 한다.
void TestSimSettlesInsideCanvas() {
    const physlab::MeasureScene scene = MakeScene(false);
    const auto sim = Run(scene, 900);
    // SetCanvas(0, 0, 400, 320) 기준 좌벽 4, 우벽 396, 바닥 316.
    const float wallL = 4.0f, wallR = 396.0f, floorY = 316.0f;
    float lowest = -1e30f, leftMost = 1e30f, rightMost = -1e30f, ke = 0.0f;
    for (const auto& b : sim) {
        lowest    = std::max(lowest, b.pos.y + b.radius);
        leftMost  = std::min(leftMost, b.pos.x - b.radius);
        rightMost = std::max(rightMost, b.pos.x + b.radius);
        ke += 0.5f * (b.vel.x * b.vel.x + b.vel.y * b.vel.y) / 10000.0f;
    }
    std::cout << "  settled: lowest=" << lowest << " left=" << leftMost
              << " right=" << rightMost << " ke=" << ke << '\n';
    CHECK(lowest <= floorY + 2.0f);
    CHECK(leftMost >= wallL - 2.0f);
    CHECK(rightMost <= wallR + 2.0f);
    CHECK(ke < 5.0f);
    // 실제로 바닥까지 내려왔는가 (초기 y 는 40 부근).
    CHECK(lowest > floorY - 40.0f);

    float pen = 0.0f, energy = 0.0f;
    scene.ComputeMetrics(sim, pen, energy);
    std::cout << "  settled avgPen=" << pen << '\n';
    CHECK(pen < 2.0f);
}

// (6) 결정성 측정이 도는 SI 가 *Step 6 이 가르친 그 SI* 인지. 같은 캔버스 /
//     같은 body 수 / 같은 iteration 으로 두 Scene 을 나란히 돌려 위치가
//     bit-exact 인지 본다. 갈라지면 측정은 교재의 솔버가 아니라 사본을 재고
//     있는 것이고, 그때의 '결정적' 이라는 판정은 아무것도 보증하지 않는다.
void TestMeasureRunsTheResponseSolver() {
    constexpr int kFrames = 240;

    const physlab::MeasureScene measure = MakeScene(false);
    const auto measured = Run(measure, kFrames);

    physlab::ResponseScene response;
    response.SetCanvas(0, 0, 400, 320);
    response.SetAlgo(physlab::ResponseAlgo::SequentialImpulse);
    response.SetBodyCount(24);
    response.SetIterations(8);
    response.ResetBodiesForTest();
    for (int i = 0; i < kFrames; ++i) response.Step(1.0f / 120.0f);

    const auto& bodies = response.Bodies();
    CHECK(bodies.size() == measured.size());

    float worstPos = 0.0f, worstVel = 0.0f;
    const size_t n = std::min(bodies.size(), measured.size());
    for (size_t i = 0; i < n; ++i) {
        worstPos = std::max(worstPos,
                            std::fabs(bodies[i].pos.x - measured[i].pos.x));
        worstPos = std::max(worstPos,
                            std::fabs(bodies[i].pos.y - measured[i].pos.y));
        worstVel = std::max(worstVel,
                            std::fabs(bodies[i].vel.x - measured[i].vel.x));
        worstVel = std::max(worstVel,
                            std::fabs(bodies[i].vel.y - measured[i].vel.y));
    }
    std::cout << "  measure vs response: maxPosDelta=" << worstPos
              << " maxVelDelta=" << worstVel << '\n';
    // snippet-begin: same-solver
    // 두 Scene 이 정말 같은 솔버를 도는가. 갈라지면 측정은 교재의 솔버가 아니라
    // 사본을 재고 있는 것이고, 그때의 '결정적' 판정은 아무것도 보증하지 않는다.
    CHECK(n == 24);
    CHECK(worstPos == 0.0f);
    CHECK(worstVel == 0.0f);
    // snippet-end: same-solver
}

const physlab::testkit::StepCase kCases[] = {
    {"step08", "bit_exact_repeat",    TestBitExactRepeat},
    {"step08", "settles_in_canvas",   TestSimSettlesInsideCanvas},
    {"step08", "shuffle_diverges",    TestShuffleBreaksDeterminism},
    {"step08", "scene_report",        TestSceneOwnDeterminismReport},
    {"step08", "metrics",             TestMetrics},
    {"step08", "same_solver_as_response", TestMeasureRunsTheResponseSolver},
};

}  // namespace

int main(int argc, char** argv) {
    return physlab::testkit::RunStepCases(
        argc, argv, kCases, (int)(sizeof(kCases) / sizeof(kCases[0])),
        "physics determinism", &failures);
}
