// Step 3 (Forces) 헤드리스 검증.
//
// `ComputeForce(pos, vel)` 는 현재 상태만으로 결정되는 순수 함수라 창 없이 부를
// 수 있고, `Step(dt)` 도 캔버스만 주면 GUI 없이 돈다. 그래서 이 Scene 의 주장
// 세 가지를 ctest 가 직접 확인한다 — force 식의 부호와 크기, terminal velocity,
// 그리고 "보존력만 있으면 총 에너지가 평평하고 드래그가 켜지면 단조 감소".

#include "scenes/forces_scene.h"

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

// 토글이 모두 꺼진 상태에서 시작한다 — 각 케이스가 필요한 것만 켠다.
physlab::ForcesScene MakeScene() {
    physlab::ForcesScene scene;
    scene.SetCanvas(0, 0, 800, 600);
    scene.SetGravity(false, 600.0f);
    scene.SetSpring(false, 8.0f, 80.0f, {400.0f, 300.0f});
    scene.SetDrag(0.0f, 0.0f);
    scene.SetState({400.0f, 300.0f}, {0.0f, 0.0f});
    return scene;
}

// (1) 각 항의 크기를 해석적 리터럴로 못 박는다. 질량 1, g = 600 이면 중력은
//     정확히 (0, 600) 이고 속도에 의존하지 않는다.
void TestForceTerms() {
    physlab::ForcesScene scene = MakeScene();
    CHECK(scene.Mass() == 1.0f);

    scene.SetGravity(true, 600.0f);
    const Vector2 g0 = scene.ComputeForce({100.0f, 100.0f}, {0.0f, 0.0f});
    const Vector2 g1 = scene.ComputeForce({0.0f, 0.0f}, {250.0f, -70.0f});
    CHECK(g0.x == 0.0f);
    CHECK(std::fabs(g0.y - 600.0f) < 1e-4f);
    CHECK(g1.x == g0.x && g1.y == g0.y);   // 중력은 상태와 무관

    // 선형 drag: c1 = 2, v = (100, 0) → (-200, 0).
    scene.SetGravity(false, 600.0f);
    scene.SetDrag(2.0f, 0.0f);
    const Vector2 dl = scene.ComputeForce({0.0f, 0.0f}, {100.0f, 0.0f});
    CHECK(std::fabs(dl.x + 200.0f) < 1e-4f);
    CHECK(std::fabs(dl.y) < 1e-4f);

    // 2 차 drag: c2 = 0.01, v = (30, 40) → |v| = 50 → -0.01*50*v = (-15, -20).
    scene.SetDrag(0.0f, 0.01f);
    const Vector2 dq = scene.ComputeForce({0.0f, 0.0f}, {30.0f, 40.0f});
    CHECK(std::fabs(dq.x + 15.0f) < 1e-3f);
    CHECK(std::fabs(dq.y + 20.0f) < 1e-3f);

    // 정지 상태에서는 어느 drag 도 힘을 만들지 않는다.
    scene.SetDrag(2.0f, 0.01f);
    const Vector2 still = scene.ComputeForce({0.0f, 0.0f}, {0.0f, 0.0f});
    CHECK(still.x == 0.0f && still.y == 0.0f);

    std::cout << "  gravity=(" << g0.x << ',' << g0.y << ") linear drag=("
              << dl.x << ',' << dl.y << ") quad drag=(" << dq.x << ','
              << dq.y << ")\n";
}

// (2) 스프링의 부호. rest 보다 멀면 anchor 쪽으로 당기고, 가까우면 밀어낸다.
//     anchor (400,300), rest 80, k 8 에서 pos (500,300) 이면 늘어난 길이 20 →
//     크기 8*20 = 160 의 힘이 -x 방향.
void TestSpringSign() {
    physlab::ForcesScene scene = MakeScene();
    scene.SetSpring(true, 8.0f, 80.0f, {400.0f, 300.0f});

    // snippet-begin: spring-sign
    // 늘어남 (len 100 > rest 80): anchor 쪽 (-x) 으로 8 * 20 = 160.
    const Vector2 stretched = scene.ComputeForce({500.0f, 300.0f}, {0, 0});
    CHECK(std::fabs(stretched.x + 160.0f) < 1e-3f);
    CHECK(std::fabs(stretched.y) < 1e-3f);

    // 눌림 (len 50 < rest 80): 바깥 (+x) 으로 8 * 30 = 240.
    const Vector2 compressed = scene.ComputeForce({450.0f, 300.0f}, {0, 0});
    CHECK(std::fabs(compressed.x - 240.0f) < 1e-3f);

    // 자연 길이에서는 정확히 0.
    const Vector2 atRest = scene.ComputeForce({480.0f, 300.0f}, {0, 0});
    CHECK(std::fabs(atRest.x) < 1e-3f);
    CHECK(std::fabs(atRest.y) < 1e-3f);

    // anchor 와 겹치면 방향이 정의되지 않는다 — 0/0 대신 힘 0.
    const Vector2 degenerate = scene.ComputeForce({400.0f, 300.0f}, {0, 0});
    CHECK(degenerate.x == 0.0f && degenerate.y == 0.0f);
    // snippet-end: spring-sign

    // 대각선 배치에서도 힘은 anchor 를 향한다 (거리 100, 늘어남 20).
    const Vector2 diagonal = scene.ComputeForce({460.0f, 380.0f}, {0, 0});
    CHECK(std::fabs(diagonal.x + 96.0f) < 1e-3f);   // -160 * 0.6
    CHECK(std::fabs(diagonal.y + 128.0f) < 1e-3f);  // -160 * 0.8

    std::cout << "  stretched=" << stretched.x << " compressed="
              << compressed.x << " at rest=" << atRest.x << " diagonal=("
              << diagonal.x << ',' << diagonal.y << ")\n";
}

// 캔버스를 아주 크게 잡아 벽 / 바닥에 닿지 않는 자유 낙하를 만든다.
float FallToTerminal(float linear, float quad, int steps) {
    physlab::ForcesScene scene;
    scene.SetCanvas(0, 0, 200000, 200000);
    scene.SetGravity(true, 600.0f);
    scene.SetSpring(false, 8.0f, 80.0f, {0.0f, 0.0f});
    scene.SetDrag(linear, quad);
    scene.SetState({100000.0f, 0.0f}, {0.0f, 0.0f});
    for (int i = 0; i < steps; ++i) scene.Step(1.0f / 120.0f);
    return scene.Velocity().y;
}

// (3) terminal velocity — 두 drag 의 식이 서로 다른 평형 속도를 낸다.
//     linear:    m g = c1 v      → v = 600 / 3     = 200
//     quadratic: m g = c2 v^2    → v = sqrt(600 / 0.006) = 316.228
void TestTerminalVelocity() {
    const float linear = FallToTerminal(3.0f, 0.0f, 3000);
    const float quad   = FallToTerminal(0.0f, 0.006f, 3000);
    const float freeFall = FallToTerminal(0.0f, 0.0f, 120);

    CHECK(std::fabs(linear - 200.0f) < 0.1f);
    CHECK(std::fabs(quad - std::sqrt(600.0f / 0.006f)) < 0.5f);
    // drag 가 없으면 유한한 종단 속도 자체가 없다 — 1 초 뒤 v = g t = 600.
    CHECK(std::fabs(freeFall - 600.0f) < 5.0f);
    CHECK(freeFall > quad);

    std::cout << "  terminal v: linear(c1=3)=" << linear
              << " quadratic(c2=0.006)=" << quad
              << " no drag after 1 s=" << freeFall << '\n';
}

// 스프링만 켠 진동자를 돌리며 총 에너지 시계열을 모은다 (중력 off → PE 는
// 스프링 항만, 바닥 충돌 없음).
std::vector<float> EnergySeries(float linearDrag, int steps) {
    physlab::ForcesScene scene;
    scene.SetCanvas(0, 0, 800, 600);
    scene.SetGravity(false, 600.0f);
    scene.SetSpring(true, 8.0f, 80.0f, {400.0f, 300.0f});
    scene.SetDrag(linearDrag, 0.0f);
    scene.SetState({400.0f, 410.0f}, {0.0f, 0.0f});   // 자연 길이보다 30 px 아래

    std::vector<float> series;
    series.reserve((size_t)steps);
    for (int i = 0; i < steps; ++i) {
        scene.Step(1.0f / 120.0f);
        series.push_back(scene.LastTotalEnergy());
    }
    return series;
}

// (4) 이 Scene 의 간판 주장. 보존력만 있으면 총 에너지가 평평하고, 드래그가
//     켜지면 단조 감소한다.
void TestDragDrainsEnergy() {
    const int steps = 1200;                     // 10 초
    const std::vector<float> free    = EnergySeries(0.0f, steps);
    const std::vector<float> damped  = EnergySeries(1.0f, steps);
    CHECK(free.size() == (size_t)steps);
    CHECK(damped.size() == (size_t)steps);

    const float e0   = free.front();
    const float low  = *std::min_element(free.begin(), free.end());
    const float high = *std::max_element(free.begin(), free.end());
    CHECK(e0 > 0.3f);                           // 0.5 * 8 * 0.3^2 = 0.36
    CHECK((high - low) < 0.05f * e0);           // 평평하다
    CHECK(std::fabs(free.back() - e0) < 0.05f * e0);

    // 드래그 쪽은 단조 감소 — 국소 반등은 이산화 오차 범위 안에서만 허용한다.
    bool monotone = true;
    for (size_t i = 1; i < damped.size(); ++i) {
        if (damped[i] > damped[i - 1] + 1e-4f) monotone = false;
    }
    CHECK(monotone);
    CHECK(damped.back() < 0.05f * e0);

    std::cout << "  conservative E: start=" << e0 << " end=" << free.back()
              << " spread=" << (high - low) << '\n';
    std::cout << "  with linear drag c1=1: start=" << damped.front()
              << " end=" << damped.back() << " (monotone decreasing)\n";
}

const physlab::testkit::StepCase kCases[] = {
    {"step03", "force_terms",       TestForceTerms},
    {"step03", "spring_sign",       TestSpringSign},
    {"step03", "terminal_velocity", TestTerminalVelocity},
    {"step03", "drag_drains_energy", TestDragDrainsEnergy},
};

}  // namespace

int main(int argc, char** argv) {
    return physlab::testkit::RunStepCases(
        argc, argv, kCases, (int)(sizeof(kCases) / sizeof(kCases[0])),
        "physics forces", &failures);
}
