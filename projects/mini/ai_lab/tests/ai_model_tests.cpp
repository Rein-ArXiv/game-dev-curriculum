#include "model/ai_model.h"
#include "model/flow_field.h"
#include "model/influence.h"
#include "model/steering.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':'  \
                      << __LINE__ << ")\n";                                   \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

bool Near(float a, float b, float epsilon = 1e-5f) {
    return std::fabs(a - b) <= epsilon;
}

void TestWeightedFlowDirection() {
    // The diagonal neighbor has a smaller integration value, but its sqrt(2)
    // edge makes the cardinal route cheaper.
    const float costs[3][3] = {
        {4.0f, 4.0f, 0.8f},
        {4.0f, 2.0f, 1.0f},
        {4.0f, 4.0f, 4.0f},
    };
    const auto direction = ailab::SelectFlowDirection(
        1, 1,
        [](int x, int y) { return x >= 0 && y >= 0 && x < 3 && y < 3; },
        [&](int x, int y) { return costs[y][x]; });
    CHECK(direction.dx == 1);
    CHECK(direction.dy == 0);

    const auto atGoal = ailab::SelectFlowDirection(
        2, 0,
        [](int x, int y) { return x >= 0 && y >= 0 && x < 3 && y < 3; },
        [&](int x, int y) { return costs[y][x]; });
    CHECK(atGoal.dx == 0 && atGoal.dy == 0);

    const float infinity = std::numeric_limits<float>::infinity();
    const auto unreachable = ailab::SelectFlowDirection(
        0, 0, [](int, int) { return true; },
        [&](int, int) { return infinity; });
    CHECK(unreachable.dx == 0 && unreachable.dy == 0);
}

void TestReflectAxis() {
    float position = -2.0f;
    float velocity = -4.0f;
    ailab::ReflectAxis(position, velocity, 0.0f, 10.0f);
    CHECK(Near(position, 2.0f));
    CHECK(Near(velocity, 4.0f));

    position = 27.0f;
    velocity = 4.0f;
    ailab::ReflectAxis(position, velocity, 0.0f, 10.0f);
    CHECK(Near(position, 7.0f));
    CHECK(Near(velocity, 4.0f));

    position = 5.0f;
    velocity = 3.0f;
    ailab::ReflectAxis(position, velocity, 2.0f, 2.0f);
    CHECK(Near(position, 2.0f));
    CHECK(Near(velocity, 0.0f));
}

void TestSpeedLimits() {
    auto fast = ailab::LimitSpeed({3.0f, 4.0f}, 2.0f, 4.0f);
    CHECK(Near(std::sqrt(fast.x * fast.x + fast.y * fast.y), 4.0f));
    CHECK(Near(fast.x / fast.y, 3.0f / 4.0f));

    auto slow = ailab::LimitSpeed({0.3f, 0.4f}, 2.0f, 4.0f);
    CHECK(Near(std::sqrt(slow.x * slow.x + slow.y * slow.y), 2.0f));

    auto zero = ailab::LimitSpeed({0.0f, 0.0f}, 2.0f, 4.0f);
    CHECK(zero.x == 0.0f && zero.y == 0.0f);
}

void TestInfluenceCombination() {
    using ailab::InfluenceView;
    CHECK(Near(ailab::CombineInfluence(
        0.8f, 0.2f, InfluenceView::Control), 0.6f));
    CHECK(Near(ailab::CombineInfluence(
        0.8f, 0.2f, InfluenceView::Tension), 0.4f));
    CHECK(Near(ailab::CombineInfluence(
        0.8f, 0.2f, InfluenceView::Vulnerability), 0.04f));
    CHECK(Near(ailab::CombineInfluence(
        0.5f, 0.5f, InfluenceView::Vulnerability), 0.25f));
    CHECK(Near(ailab::CombineInfluence(
        1.0f, 0.0f, InfluenceView::Vulnerability), 0.0f));
    CHECK(Near(ailab::CombineInfluence(
        0.0f, 1.0f, InfluenceView::Vulnerability), 1.0f));
}

// 결정적 LCG — 테스트 입력을 시드 고정으로 만든다.
struct Lcg {
    uint32_t state = 1u;
    uint32_t Next() {
        state = state * 1664525u + 1013904223u;
        return state;
    }
    // [lo, hi) 구간의 float.
    float Range(float lo, float hi) {
        return lo + (hi - lo) * ((float)(Next() >> 8) / 16777216.0f);
    }
};

// ---------------------------------------------------------------- steering

// step04 §5-5 가 요구하는 불변식: 어떤 목표·dt 를 줘도 속도는 maxSpeed 를
// 넘지 않는다.
//
// dt 범위가 중요하다. dt <= 1 이면 v' = (1-dt)*v + dt*desired 가 볼록결합이라
// |v'| <= max(|v|, maxSpeed) 가 절단 없이도 성립한다 — 즉 작은 dt 만 쓰는
// 테스트는 Truncate(maxSpeed) 를 지워도 통과한다(실측 확인). 프레임 스톨을
// 흉내내는 큰 dt 를 반드시 섞어야 절단이 실제로 일하는 구간을 밟는다.
void TestSteeringSpeedCap() {
    const ailab::SteerParams params{260.0f, 520.0f, 120.0f};
    const ailab::SteerBounds bounds{0.0f, 0.0f, 640.0f, 480.0f};

    for (int behaviorIndex = 0; behaviorIndex < 4; ++behaviorIndex) {
        const auto behavior = static_cast<ailab::SteerBehavior>(behaviorIndex);
        Lcg rng{(uint32_t)(behaviorIndex + 1)};
        ailab::SteerState state{{320.0f, 240.0f}, {0.0f, 0.0f}, 0.0f};
        float worstSpeed = 0.0f;
        for (int step = 0; step < 1000; ++step) {
            const ailab::ModelVector2 goal{rng.Range(-200.0f, 840.0f),
                                           rng.Range(-200.0f, 680.0f)};
            const ailab::ModelVector2 targetVelocity{rng.Range(-300.0f, 300.0f),
                                                     rng.Range(-300.0f, 300.0f)};
            // 60fps 프레임과 2 초짜리 스톨을 섞는다.
            const float dt = (step % 4 == 3) ? rng.Range(1.0f, 2.0f)
                                             : rng.Range(0.001f, 0.2f);
            state = ailab::StepSteering(state, behavior, goal, targetVelocity, params,
                                        dt, bounds, rng.Range(-0.3f, 0.3f));
            worstSpeed = std::max(worstSpeed, ailab::VecLen(state.vel));
            CHECK(state.pos.x >= bounds.minX && state.pos.x <= bounds.maxX);
            CHECK(state.pos.y >= bounds.minY && state.pos.y <= bounds.maxY);
        }
        CHECK(worstSpeed <= params.maxSpeed + 1e-4f);
        // 절단이 자명하게 만족되는(=한 번도 도달 못 한) 상태면 검증력이 없다.
        CHECK(worstSpeed > 0.5f * params.maxSpeed);
    }

    // 결정적 최악 케이스: 최고 속도로 달리는 중에 목표가 정반대로 뒤집히고
    // 프레임이 1.5 초 걸린다. 절단이 없으면 |v| 가 520 까지 튄다.
    // (경계 반사가 결과를 가리지 않도록 넓은 캔버스를 쓴다.)
    const ailab::SteerBounds wide{0.0f, 0.0f, 10000.0f, 10000.0f};
    ailab::SteerState state{{5000.0f, 5000.0f}, {params.maxSpeed, 0.0f}, 0.0f};
    state = ailab::StepSteering(state, ailab::SteerBehavior::Seek, {-100000.0f, 5000.0f},
                                {}, params, 1.5f, wide);
    CHECK(ailab::VecLen(state.vel) <= params.maxSpeed + 1e-4f);
    CHECK(state.vel.x < 0.0f);
}

// Arrive: slowRadius 안에서 목표에 가까워질수록 desired 속력이 단조 감소하고,
// 밖에서는 maxSpeed 로 포화한다.
void TestSteeringArriveSlowdown() {
    const ailab::SteerParams params{260.0f, 520.0f, 120.0f};
    ailab::SteerState state{{100.0f, 100.0f}, {0.0f, 0.0f}, 0.0f};

    float previous = std::numeric_limits<float>::infinity();
    for (int i = 0; i <= 20; ++i) {
        const float distance = 120.0f * (float)(20 - i) / 20.0f;  // 120 → 0
        const ailab::ModelVector2 goal{state.pos.x + distance, state.pos.y};
        const float speed = ailab::VecLen(ailab::DesiredVelocity(
            ailab::SteerBehavior::Arrive, state, goal, {}, params));
        CHECK(speed < previous + 1e-4f);
        if (i > 0) CHECK(speed < previous - 1e-3f);
        previous = speed;
    }
    CHECK(Near(previous, 0.0f, 1e-3f));

    // slowRadius 밖은 전부 maxSpeed.
    for (float distance : {121.0f, 200.0f, 1000.0f}) {
        const ailab::ModelVector2 goal{state.pos.x + distance, state.pos.y};
        CHECK(Near(ailab::VecLen(ailab::DesiredVelocity(
                       ailab::SteerBehavior::Arrive, state, goal, {}, params)),
                   params.maxSpeed, 1e-3f));
    }
    // 정확히 slowRadius 의 절반이면 maxSpeed 의 절반.
    const ailab::ModelVector2 half{state.pos.x + 60.0f, state.pos.y};
    CHECK(Near(ailab::VecLen(ailab::DesiredVelocity(ailab::SteerBehavior::Arrive,
                                                    state, half, {}, params)),
               130.0f, 1e-3f));
}

// Pursue: lookahead 는 dist / (maxSpeed + |targetVel| + 1) 이고, desired 는
// 그 시간만큼 외삽한 미래 위치를 향한다.
void TestSteeringPursueLookahead() {
    CHECK(Near(ailab::PursueLookahead(0.0f, 260.0f, 0.0f), 0.0f));
    CHECK(Near(ailab::PursueLookahead(261.0f, 260.0f, 0.0f), 1.0f));
    CHECK(Near(ailab::PursueLookahead(100.0f, 0.0f, 0.0f), 100.0f));
    CHECK(Near(ailab::PursueLookahead(600.0f, 200.0f, 99.0f), 2.0f));

    const ailab::SteerParams params{260.0f, 520.0f, 120.0f};
    ailab::SteerState state{{0.0f, 0.0f}, {0.0f, 0.0f}, 0.0f};
    const ailab::ModelVector2 goal{261.0f, 0.0f};
    const ailab::ModelVector2 targetVelocity{0.0f, 40.0f};
    // lookahead = 261 / (260 + 40 + 1) = 0.866887...
    const float lookahead = ailab::PursueLookahead(261.0f, params.maxSpeed, 40.0f);
    const ailab::ModelVector2 future{261.0f, 40.0f * lookahead};
    const ailab::ModelVector2 expected =
        ailab::VecMul(ailab::VecNorm(future), params.maxSpeed);
    const ailab::ModelVector2 desired = ailab::DesiredVelocity(
        ailab::SteerBehavior::Pursue, state, goal, targetVelocity, params);
    CHECK(Near(desired.x, expected.x, 1e-2f));
    CHECK(Near(desired.y, expected.y, 1e-2f));
    // 미래 위치를 쓰지 않고 현재 위치를 향하면 y 성분이 0 이 된다.
    CHECK(desired.y > 1.0f);
}

// 경계 처리: 밖으로 나가면 안으로 clamp 되고 해당 축 속도의 부호가 뒤집힌다.
void TestSteeringBounds() {
    const ailab::SteerParams params{260.0f, 520.0f, 120.0f};
    const ailab::SteerBounds bounds{0.0f, 0.0f, 640.0f, 480.0f};

    // 오른쪽 벽으로 돌진 (목표를 캔버스 밖 오른쪽에 둔다).
    ailab::SteerState state{{639.0f, 240.0f}, {260.0f, 0.0f}, 0.0f};
    ailab::SteerState next = ailab::StepSteering(
        state, ailab::SteerBehavior::Seek, {5000.0f, 240.0f}, {}, params, 0.1f, bounds);
    CHECK(Near(next.pos.x, bounds.maxX, 1e-4f));
    CHECK(next.vel.x < 0.0f);
    CHECK(Near(next.vel.x, -0.5f * 260.0f, 1e-3f));

    // 위쪽 벽.
    state = ailab::SteerState{{320.0f, 1.0f}, {0.0f, -260.0f}, 0.0f};
    next = ailab::StepSteering(state, ailab::SteerBehavior::Seek, {320.0f, -5000.0f}, {},
                               params, 0.1f, bounds);
    CHECK(Near(next.pos.y, bounds.minY, 1e-4f));
    CHECK(next.vel.y > 0.0f);
    CHECK(Near(next.vel.y, 0.5f * 260.0f, 1e-3f));
}

// -------------------------------------------------------------- flow field

void TestIntegrationField() {
    // (a) 빈 8x6 격자: octile 거리와 정확히 일치해야 한다.
    const auto open = ailab::BuildIntegrationField(
        8, 6, [](int x, int y) { return x >= 0 && y >= 0 && x < 8 && y < 6; }, 0, 0);
    CHECK(open.size() == 48);
    auto at = [&](const std::vector<float>& f, int x, int y) {
        return f[(size_t)(y * 8 + x)];
    };
    CHECK(Near(at(open, 0, 0), 0.0f));
    CHECK(Near(at(open, 3, 0), 3.0f, 1e-4f));
    CHECK(Near(at(open, 0, 4), 4.0f, 1e-4f));
    CHECK(Near(at(open, 2, 2), 2.0f * 1.41421356f, 1e-4f));
    CHECK(Near(at(open, 5, 2), 3.0f + 2.0f * 1.41421356f, 1e-4f));
    CHECK(Near(at(open, 7, 5), 2.0f + 5.0f * 1.41421356f, 1e-4f));

    // (b) 도달 불가 셀은 무한대로 남는다: x=4 열을 통째로 막는다.
    const auto blocked = ailab::BuildIntegrationField(
        8, 6,
        [](int x, int y) {
            if (x < 0 || y < 0 || x >= 8 || y >= 6) return false;
            return x != 4;
        },
        0, 0);
    CHECK(std::isfinite(at(blocked, 3, 3)));
    CHECK(!std::isfinite(at(blocked, 4, 3)));  // 벽 자신
    CHECK(!std::isfinite(at(blocked, 5, 0)));  // 벽 너머
    CHECK(!std::isfinite(at(blocked, 7, 5)));

    // (c) 좁은 통로 우회 비용은 직선 거리보다 크다: x=4 열에 y=5 만 구멍.
    const auto corridor = ailab::BuildIntegrationField(
        8, 6,
        [](int x, int y) {
            if (x < 0 || y < 0 || x >= 8 || y >= 6) return false;
            return x != 4 || y == 5;
        },
        0, 0);
    const float detour = at(corridor, 7, 0);
    CHECK(std::isfinite(detour));
    CHECK(detour > at(open, 7, 0) + 1e-3f);
    // 구멍 (4,5) 를 반드시 지난다. 해석해:
    //   (0,0)→(4,5) = 1 + 4√2,  (4,5)→(7,0) = 2 + 3√2,  합 = 3 + 7√2.
    CHECK(Near(at(corridor, 4, 5), 1.0f + 4.0f * 1.41421356f, 1e-4f));
    CHECK(Near(detour, 3.0f + 7.0f * 1.41421356f, 1e-3f));

    // (d) 필드와 국소 정책의 결합: 통로 앞 셀은 통로 쪽으로 내려가야 한다.
    const auto step = ailab::SelectFlowDirection(
        5, 0,
        [](int x, int y) {
            if (x < 0 || y < 0 || x >= 8 || y >= 6) return false;
            return x != 4 || y == 5;
        },
        [&](int x, int y) { return corridor[(size_t)(y * 8 + x)]; });
    CHECK(step.dy > 0);  // 아래(통로)로
}

// --------------------------------------------------------------- influence

void TestInfluenceField() {
    const int W = 7, H = 7;
    const auto openGrid = [](int x, int y) {
        return x >= 0 && y >= 0 && x < 7 && y < 7;
    };
    auto at = [&](const std::vector<float>& f, int x, int y) {
        return f[(size_t)(y * W + x)];
    };

    // (a) 확산 반복 1회 = 이웃까지, 2회 = 거리 2 까지. 감쇠는 spread^거리.
    {
        std::vector<float> field((size_t)(W * H), 0.0f);
        const std::vector<ailab::GridPoint> stamps{{3, 3}};
        ailab::StepInfluenceField(W, H, field, stamps,
                                  ailab::InfluenceParams{1.0f, 0.5f, 1}, openGrid);
        CHECK(Near(at(field, 3, 3), 1.0f));
        CHECK(Near(at(field, 4, 3), 0.5f));
        CHECK(Near(at(field, 4, 4), 0.5f));
        CHECK(Near(at(field, 5, 3), 0.0f));
    }
    {
        std::vector<float> field((size_t)(W * H), 0.0f);
        const std::vector<ailab::GridPoint> stamps{{3, 3}};
        ailab::StepInfluenceField(W, H, field, stamps,
                                  ailab::InfluenceParams{1.0f, 0.5f, 2}, openGrid);
        CHECK(Near(at(field, 4, 3), 0.5f));
        CHECK(Near(at(field, 5, 3), 0.25f));
        CHECK(Near(at(field, 6, 3), 0.0f));
    }

    // (b) decay: 스탬프를 걷어낸 다음 tick 은 정확히 decay 배로 줄어든다.
    {
        std::vector<float> field((size_t)(W * H), 0.0f);
        const std::vector<ailab::GridPoint> stamps{{3, 3}};
        const ailab::InfluenceParams params{0.5f, 0.5f, 1};
        ailab::StepInfluenceField(W, H, field, stamps, params, openGrid);
        const float before = at(field, 4, 3);
        ailab::StepInfluenceField(W, H, field, std::vector<ailab::GridPoint>{}, params,
                                  openGrid);
        // decay 후 0.25*0.5=0.125, 이웃 (3,3)=0.5 에서 0.25 가 들어와 0.25.
        CHECK(Near(at(field, 3, 3), 0.5f));
        CHECK(Near(at(field, 4, 3), 0.25f));
        CHECK(at(field, 4, 3) < before);
    }

    // (c) 벽 셀은 항상 0 이고, 벽으로 완전히 갈린 영역에는 전파되지 않는다.
    {
        const auto walled = [](int x, int y) {
            if (x < 0 || y < 0 || x >= 7 || y >= 7) return false;
            return x != 3;  // x=3 열 전체가 벽
        };
        std::vector<float> field((size_t)(W * H), 0.0f);
        // (3,2) 는 벽 위의 유닛이다. stamp 는 InBounds 만 보므로 값이 실제로
        // 찍히고, spread 단계의 벽 셀 0 초기화가 그것을 지워야 한다.
        const std::vector<ailab::GridPoint> stamps{{1, 3}, {3, 2}};
        for (int tick = 0; tick < 10; ++tick) {
            ailab::StepInfluenceField(W, H, field, stamps,
                                      ailab::InfluenceParams{1.0f, 0.9f, 3}, walled);
        }
        CHECK(at(field, 1, 3) > 0.9f);
        CHECK(at(field, 2, 3) > 0.0f);
        for (int y = 0; y < H; ++y) CHECK(at(field, 3, y) == 0.0f);   // 벽 셀
        for (int y = 0; y < H; ++y) CHECK(at(field, 4, y) == 0.0f);   // 벽 너머
        CHECK(at(field, 6, 6) == 0.0f);
    }

    // (d) 결정성: 같은 배치·설정으로 두 번 돌리면 완전히 동일한 벡터.
    {
        const std::vector<ailab::GridPoint> stamps{{1, 1}, {5, 4}};
        const ailab::InfluenceParams params{0.85f, 0.7f, 3};
        std::vector<float> a((size_t)(W * H), 0.0f), b((size_t)(W * H), 0.0f);
        for (int tick = 0; tick < 12; ++tick) {
            ailab::StepInfluenceField(W, H, a, stamps, params, openGrid);
            ailab::StepInfluenceField(W, H, b, stamps, params, openGrid);
        }
        CHECK(a == b);
        bool anyPositive = false;
        for (float v : a) {
            CHECK(v >= 0.0f && v <= 1.0f);
            if (v > 0.0f) anyPositive = true;
        }
        CHECK(anyPositive);
    }
}

}  // namespace

int main() {
    TestWeightedFlowDirection();
    TestReflectAxis();
    TestSpeedLimits();
    TestInfluenceCombination();
    TestSteeringSpeedCap();
    TestSteeringArriveSlowdown();
    TestSteeringPursueLookahead();
    TestSteeringBounds();
    TestIntegrationField();
    TestInfluenceField();
    if (failures != 0) {
        std::cerr << failures << " AI model test(s) failed\n";
        return 1;
    }
    std::cout << "AI model tests passed\n";
    return 0;
}
