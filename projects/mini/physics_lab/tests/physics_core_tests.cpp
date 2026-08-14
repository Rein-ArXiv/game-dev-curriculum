#include "core/physics_core.h"

#include <algorithm>
#include <cmath>
#include <iostream>

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

float IntegrateError(physlab::core::HarmonicIntegrator kind, float dt) {
    float x = 1.0f;
    float v = 0.0f;
    const int steps = (int)std::lround(1.0f / dt);
    for (int i = 0; i < steps; ++i)
        physlab::core::StepHarmonic(kind, 1.0f, dt, x, v);
    return std::fabs(x - std::cos(1.0f));
}

void TestIntegratorOrder() {
    const float explicitCoarse = IntegrateError(
        physlab::core::HarmonicIntegrator::ExplicitEuler, 0.1f);
    const float explicitFine = IntegrateError(
        physlab::core::HarmonicIntegrator::ExplicitEuler, 0.05f);
    CHECK(explicitFine < explicitCoarse);
    CHECK(explicitCoarse / explicitFine > 1.7f);

    const float verletCoarse = IntegrateError(
        physlab::core::HarmonicIntegrator::VelocityVerlet, 0.1f);
    const float verletFine = IntegrateError(
        physlab::core::HarmonicIntegrator::VelocityVerlet, 0.05f);
    CHECK(verletFine < verletCoarse);
    CHECK(verletCoarse / verletFine > 3.5f);

    const float rkCoarse = IntegrateError(
        physlab::core::HarmonicIntegrator::RK4, 0.2f);
    const float rkFine = IntegrateError(
        physlab::core::HarmonicIntegrator::RK4, 0.1f);
    CHECK(rkFine < rkCoarse);
    CHECK(rkCoarse / rkFine > 10.0f);
}

void TestNormalImpulse() {
    float vi = 1.0f;
    float vj = -1.0f;
    float lambda = 0.0f;
    const float initialRelative = vj - vi;
    // 목표 분리 속도는 리터럴로 못 박는다. 초기 상대 법선 속도 -2, e = 0.5
    // 이므로 반발 후 상대 속도는 +1.0 이어야 한다 (RestitutionTarget 을 다시
    // 불러 자기 자신과 비교하면 어떤 구현이든 통과하는 항등식이 된다).
    CHECK(std::fabs(initialRelative - (-2.0f)) < 1e-6f);
    CHECK(std::fabs(physlab::core::RestitutionTarget(-2.0f, 0.5f, 0.0f)
                    - 1.0f) < 1e-6f);

    const float target = physlab::core::RestitutionTarget(
        initialRelative, 0.5f, 0.0f);
    const float impulse = physlab::core::AccumulateNormalImpulse(
        initialRelative, target, 2.0f, lambda);
    vi -= impulse;
    vj += impulse;

    CHECK(std::fabs((vi + vj) - 0.0f) < 1e-6f);
    CHECK(std::fabs((vj - vi) - 1.0f) < 1e-6f);
    CHECK(std::fabs(impulse - 1.5f) < 1e-6f);

    const float repeated = physlab::core::AccumulateNormalImpulse(
        vj - vi, target, 2.0f, lambda);
    CHECK(std::fabs(repeated) < 1e-6f);
    CHECK(physlab::core::RestitutionTarget(-0.1f, 1.0f, 0.5f) == 0.0f);

    // e 를 클램프 범위 밖으로 줘도 목표는 [0, |v|] 안에 머문다.
    CHECK(std::fabs(physlab::core::RestitutionTarget(-2.0f, 4.0f, 0.0f)
                    - 2.0f) < 1e-6f);
    CHECK(physlab::core::RestitutionTarget(-2.0f, -1.0f, 0.0f) == 0.0f);
}

// λ >= 0 클램프 전용 케이스. 위 TestNormalImpulse 의 두 번째 호출은 이미
// 수렴 상태(delta == 0)라 클램프 분기를 지나지 않는다.
void TestNormalImpulseClamp() {
    float lambda = 0.0f;

    // 이미 분리 중인 접촉(법선 속도 +2)에 임펄스를 요구하면 음수 λ 가 되므로
    // 0 으로 잘려야 한다 — 접촉은 당길 수 없다.
    const float separating = physlab::core::AccumulateNormalImpulse(
        2.0f, 0.0f, 2.0f, lambda);
    CHECK(separating == 0.0f);
    CHECK(lambda == 0.0f);

    // 접근(-2)으로 λ 를 양수로 만든다.
    const float approaching = physlab::core::AccumulateNormalImpulse(
        -2.0f, 0.0f, 2.0f, lambda);
    CHECK(std::fabs(approaching - 1.0f) < 1e-6f);
    CHECK(std::fabs(lambda - 1.0f) < 1e-6f);

    // 그 뒤 큰 분리(+10)가 오면 누적 λ 만큼만 되돌리고 0 에서 멈춘다.
    const float previousLambda = lambda;
    const float released = physlab::core::AccumulateNormalImpulse(
        10.0f, 0.0f, 2.0f, lambda);
    CHECK(lambda == 0.0f);
    CHECK(std::fabs(released - (-previousLambda)) < 1e-6f);

    // 질량이 없는(inverseMassSum == 0) 접촉은 임펄스를 만들지 않는다.
    float staticLambda = 0.0f;
    CHECK(physlab::core::AccumulateNormalImpulse(
              -5.0f, 0.0f, 0.0f, staticLambda) == 0.0f);
    CHECK(staticLambda == 0.0f);
}

// 조화 진동자의 에너지 E = 0.5 v^2 + 0.5 w^2 x^2 로 적분기의 *성질* 을 본다.
// 수렴 차수만 보는 TestIntegratorOrder 는 Explicit / Semi-implicit 둘 다
// 전역 1차라 서로 뒤바뀌어도 통과한다.
float HarmonicEnergy(float omega, float x, float v) {
    return 0.5f * v * v + 0.5f * omega * omega * x * x;
}

void TestIntegratorEnergy() {
    using physlab::core::HarmonicIntegrator;
    using physlab::core::StepHarmonic;

    const float omega = 1.0f;
    const float dt    = 0.1f;
    const int   steps = 200;
    const float e0    = HarmonicEnergy(omega, 1.0f, 0.0f);

    // (a) Explicit Euler 는 매 스텝 에너지가 (1 + w^2 dt^2) 배로 커진다.
    {
        float x = 1.0f, v = 0.0f;
        float prevE = e0;
        bool  monotone = true;
        for (int i = 0; i < steps; ++i) {
            StepHarmonic(HarmonicIntegrator::ExplicitEuler, omega, dt, x, v);
            const float e = HarmonicEnergy(omega, x, v);
            if (!(e >= prevE - 1e-6f)) monotone = false;
            prevE = e;
        }
        CHECK(monotone);
        CHECK(prevE / e0 > 1.5f);
    }

    // (b) Semi-implicit Euler / Velocity Verlet 은 에너지가 유계로 진동한다.
    for (int kind = 0; kind < 2; ++kind) {
        const HarmonicIntegrator integrator =
            (kind == 0) ? HarmonicIntegrator::SemiImplicitEuler
                        : HarmonicIntegrator::VelocityVerlet;
        float x = 1.0f, v = 0.0f;
        float worst = 0.0f;
        float minX  = x;
        for (int i = 0; i < steps; ++i) {
            StepHarmonic(integrator, omega, dt, x, v);
            const float e = HarmonicEnergy(omega, x, v);
            worst = std::max(worst, std::fabs(e / e0 - 1.0f));
            minX  = std::min(minX, x);
        }
        CHECK(worst < 0.15f);
        // 실제로 진동해야 한다 — 아무것도 하지 않는 적분기도 에너지는 보존한다.
        CHECK(minX < -0.9f);
    }

    // (c) 순서 뒤바뀜 검출. 1 스텝 뒤 위치가 갈라진다:
    //     Explicit 은 옛 속도(0)로 x 를 옮겨 1.0, Semi-implicit 은 갱신된
    //     속도(-w^2 x dt = -0.1)로 옮겨 0.99.
    {
        float xExplicit = 1.0f, vExplicit = 0.0f;
        StepHarmonic(HarmonicIntegrator::ExplicitEuler, omega, dt,
                     xExplicit, vExplicit);
        float xSemi = 1.0f, vSemi = 0.0f;
        StepHarmonic(HarmonicIntegrator::SemiImplicitEuler, omega, dt,
                     xSemi, vSemi);
        CHECK(std::fabs(xExplicit - 1.0f) < 1e-6f);
        CHECK(std::fabs(xSemi - 0.99f) < 1e-6f);
        CHECK(std::fabs(xExplicit - xSemi) > 1e-3f);
        CHECK(std::fabs(vExplicit - vSemi) < 1e-6f);
    }
}

void TestDegenerateNormal() {
    const auto a = physlab::core::SafeUnitNormal(0.0f, 0.0f, 0);
    const auto b = physlab::core::SafeUnitNormal(0.0f, 0.0f, 1);
    CHECK(a.x == 1.0f && a.y == 0.0f);
    CHECK(b.x == 0.0f && b.y == 1.0f);

    const auto n = physlab::core::SafeUnitNormal(3.0f, 4.0f);
    CHECK(std::fabs(n.x - 0.6f) < 1e-6f);
    CHECK(std::fabs(n.y - 0.8f) < 1e-6f);
}

void TestAabbOverlap() {
    using physlab::core::Aabb2;
    CHECK(physlab::core::AabbOverlap(
        Aabb2{0.0f, 0.0f, 1.0f, 1.0f},
        Aabb2{2.0f, 0.0f, 1.0f, 1.0f}));
    CHECK(!physlab::core::AabbOverlap(
        Aabb2{-4.0f, -3.0f, 0.5f, 0.5f},
        Aabb2{-2.9f, -3.0f, 0.5f, 0.5f}));
    CHECK(physlab::core::AabbOverlap(
        Aabb2{0.0f, 0.0f, -1.0f, -1.0f},
        Aabb2{0.0f, 0.0f, 0.0f, 0.0f}));
}

void TestDistanceProjection() {
    float ax = 0.0f, ay = 0.0f;
    float bx = 3.0f, by = 4.0f;
    CHECK(physlab::core::ProjectDistance(
        ax, ay, 0.0f, bx, by, 1.0f, 2.0f));
    CHECK(ax == 0.0f && ay == 0.0f);
    CHECK(std::fabs(std::sqrt(bx * bx + by * by) - 2.0f) < 1e-5f);

    ax = -2.0f; ay = 0.0f;
    bx = 2.0f; by = 0.0f;
    CHECK(physlab::core::ProjectDistance(
        ax, ay, 1.0f, bx, by, 1.0f, 2.0f));
    CHECK(std::fabs(ax + 1.0f) < 1e-6f);
    CHECK(std::fabs(bx - 1.0f) < 1e-6f);

    ax = bx = 1.0f;
    ay = by = 1.0f;
    CHECK(!physlab::core::ProjectDistance(
        ax, ay, 1.0f, bx, by, 1.0f, 1.0f));
}

}  // namespace

int main() {
    TestIntegratorOrder();
    TestIntegratorEnergy();
    TestNormalImpulse();
    TestNormalImpulseClamp();
    TestDegenerateNormal();
    TestAabbOverlap();
    TestDistanceProjection();
    if (failures != 0) {
        std::cerr << failures << " physics core test(s) failed\n";
        return 1;
    }
    std::cout << "physics core tests passed\n";
    return 0;
}
