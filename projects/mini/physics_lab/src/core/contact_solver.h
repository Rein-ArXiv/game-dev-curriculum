// Step 6 (Response) 이 가르치는 접촉 파이프라인의 *단일 구현*.
//
// 왜 헤더 하나로 올렸는가. Step 8 의 결정성 측정은 "Step 6 이 가르친 SI 를
// 두 번 돌리면 같은 결과가 나오는가" 를 답해야 한다. 측정이 자기만의 SI 사본을
// 들고 있으면 그 답은 *사본* 에 대한 답이지 교재의 SI 에 대한 답이 아니다.
// 그래서 접촉 수집 / Sequential Impulse / 위치 보정 세 단계를 여기 한 벌만 둔다.
//
// Body 는 템플릿 파라미터다 — `pos` / `vel` / `radius` / `invMass` 만 있으면
// 된다 (ResponseScene 의 CircleRb, MeasureScene 의 MeasureBody 가 둘 다 만족).
// 유효질량은 언제나 body 의 invMass 에서 계산한다. `2.0f` / `1.0f` 같은 리터럴을
// 박아 두면 invMass 가 1 이 아닌 순간 조용히 틀린 임펄스를 낸다 — 그리고 그
// 틀림은 "질량이 1 인 워크로드" 에서는 끝까지 드러나지 않는다.
//
// 이 헤더는 raylib 의 Vector2 를 쓴다. 순수 스칼라 커널(적분기 / restitution
// target / 누적 impulse 클램프) 은 core/physics_core.h 에 그대로 남는다.

#pragma once

#include "core/physics_core.h"

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace physlab {

struct Contact {
    int     i, j;          // body 인덱스. j = -1 이면 벽 / 바닥 contact
    Vector2 normal;        // i → j 방향 (벽 contact 면 body 가 밀려나는 방향)
    float   depth;
    float   lambda;        // SI 의 accumulated impulse
};

// 캔버스 안쪽 4 px 에 놓인 좌우 벽과 바닥. 두 Scene 이 같은 식으로 만든다.
struct WorldBounds {
    float left   = 0.0f;
    float right  = 0.0f;
    float floorY = 0.0f;
};

inline WorldBounds BoundsFromCanvas(int x, int y, int w, int h) {
    WorldBounds b;
    b.left   = (float)x + 4.0f;
    b.right  = (float)(x + w) - 4.0f;
    b.floorY = (float)(y + h) - 4.0f;
    return b;
}

// 접촉 응답의 물리 파라미터. 두 Scene 이 같은 기본값에서 출발한다 —
// Response 는 슬라이더로 restitution 을 흔들고, Measure 는 결정성 비교를 위해
// 기본값을 고정한 채로 쓴다.
struct SolverParams {
    float restitution          = 0.1f;
    float restitutionThreshold = 50.0f;  // px/s — Box2D 의 b2_velocityThreshold
    float slop                 = 0.5f;   // px, 허용 침투
    float posCorrectFactor     = 0.2f;   // 한 pass 에 되미는 비율
};

// 두 Scene 이 같은 stacking 시나리오를 돌린다 — 중력도 같은 값에서 출발한다.
inline constexpr float kDefaultGravity = 600.0f;  // px/s²

namespace core {

inline float DotV(Vector2 a, Vector2 b) { return a.x * b.x + a.y * b.y; }
inline Vector2 SubV(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }

// semi-implicit Euler — 속도를 먼저 갱신하고 그 속도로 위치를 옮긴다.
template <class Body>
void IntegrateSemiImplicitEuler(std::vector<Body>& bodies, float gravity,
                                float dt) {
    for (auto& b : bodies) {
        if (b.invMass <= 0.0f) continue;   // 정적 body 는 중력을 받지 않는다
        b.vel.y += gravity * dt;
        b.pos.x += b.vel.x * dt;
        b.pos.y += b.vel.y * dt;
    }
}

// body-body 는 brute O(n²) — Broad Phase 가 아니라 *응답* 이 주제라서 단순.
// 순서는 (i < j) 쌍 전부 → body 순서대로 바닥 / 좌벽 / 우벽. 이 고정 순서가
// Gauss-Seidel 풀이의 bit-exact 재현성을 떠받친다.
// snippet-begin: gather-contacts
template <class Body>
void GatherCircleContacts(const std::vector<Body>& bodies,
                          const WorldBounds& bounds,
                          std::vector<Contact>& out) {
    out.clear();
    const int n = (int)bodies.size();

    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            const Vector2 d =
                SubV(bodies[(size_t)j].pos, bodies[(size_t)i].pos);
            const float r =
                bodies[(size_t)i].radius + bodies[(size_t)j].radius;
            const float dist = std::sqrt(d.x * d.x + d.y * d.y);
            if (dist < r) {
                const UnitNormal2 nrm = SafeUnitNormal(d.x, d.y, i ^ j);
                out.push_back({i, j, {nrm.x, nrm.y}, r - dist, 0.0f});
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        const Body& b = bodies[(size_t)i];
        if (b.pos.y + b.radius > bounds.floorY) {
            out.push_back({i, -1, {0.0f, -1.0f},
                           (b.pos.y + b.radius) - bounds.floorY, 0.0f});
        }
        if (b.pos.x - b.radius < bounds.left) {
            out.push_back({i, -1, {1.0f, 0.0f},
                           bounds.left - (b.pos.x - b.radius), 0.0f});
        }
        if (b.pos.x + b.radius > bounds.right) {
            out.push_back({i, -1, {-1.0f, 0.0f},
                           (b.pos.x + b.radius) - bounds.right, 0.0f});
        }
    }
}
// snippet-end: gather-contacts

// restitution 은 *첫 iter 전에 한 번만* 계산한다. iter 안에서 재주입하면
// 분리 속도가 계속 가산되어 정지한 body 가 다시 튀어오른다.
template <class Body>
void ComputeRestitutionTargets(const std::vector<Body>& bodies,
                               const std::vector<Contact>& contacts,
                               const SolverParams& params,
                               std::vector<float>& outTargets) {
    outTargets.assign(contacts.size(), 0.0f);
    for (size_t k = 0; k < contacts.size(); ++k) {
        const Contact& c  = contacts[k];
        const Body&    bi = bodies[(size_t)c.i];
        const float    vnInit =
            (c.j >= 0)
                ? DotV(SubV(bodies[(size_t)c.j].vel, bi.vel), c.normal)
                : DotV(bi.vel, c.normal);
        // 임계 이하의 접근은 resting contact — RestitutionTarget 이 0 을 준다.
        outTargets[k] = RestitutionTarget(vnInit, params.restitution,
                                          params.restitutionThreshold);
    }
}

// contact 하나에 대한 한 번의 normal impulse. 유효질량은 invMass 합에서 나온다.
// snippet-begin: si-effective-mass
template <class Body>
void ApplyContactImpulse(std::vector<Body>& bodies, Contact& c,
                         float targetVelocity) {
    Body& bi = bodies[(size_t)c.i];
    if (c.j >= 0) {
        Body&       bj = bodies[(size_t)c.j];
        const float vn = DotV(SubV(bj.vel, bi.vel), c.normal);
        const float applied = AccumulateNormalImpulse(
            vn, targetVelocity, bi.invMass + bj.invMass, c.lambda);
        bi.vel.x -= applied * bi.invMass * c.normal.x;
        bi.vel.y -= applied * bi.invMass * c.normal.y;
        bj.vel.x += applied * bj.invMass * c.normal.x;
        bj.vel.y += applied * bj.invMass * c.normal.y;
    } else {
        // 벽은 무한 질량 — 유효질량은 body 쪽 invMass 하나뿐이다.
        const float vn = DotV(bi.vel, c.normal);
        const float applied = AccumulateNormalImpulse(
            vn, targetVelocity, bi.invMass, c.lambda);
        bi.vel.x += applied * bi.invMass * c.normal.x;
        bi.vel.y += applied * bi.invMass * c.normal.y;
    }
}
// snippet-end: si-effective-mass

// contact 순서를 그대로 두는 기본 정책. 결정성의 전제 그 자체다.
struct KeepContactOrder {
    void operator()(std::vector<int>&) const {}
};

// Sequential Impulse. reorder 는 *매 iter 시작마다* 처리 순서 배열을 손댈 기회를
// 준다 — Measure Scene 의 shuffle 토글이 여기에 붙어 순서 의존을 실증한다.
template <class Body, class ReorderFn>
void SolveSequentialImpulse(std::vector<Body>& bodies,
                            std::vector<Contact>& contacts,
                            const SolverParams& params, int iterations,
                            ReorderFn&& reorder) {
    std::vector<float> targets;
    ComputeRestitutionTargets(bodies, contacts, params, targets);

    std::vector<int> order(contacts.size());
    for (size_t k = 0; k < order.size(); ++k) order[k] = (int)k;

    for (int iter = 0; iter < iterations; ++iter) {
        reorder(order);
        for (size_t k = 0; k < order.size(); ++k) {
            const size_t idx = (size_t)order[k];
            ApplyContactImpulse(bodies, contacts[idx], targets[idx]);
        }
    }
}

template <class Body>
void SolveSequentialImpulse(std::vector<Body>& bodies,
                            std::vector<Contact>& contacts,
                            const SolverParams& params, int iterations) {
    SolveSequentialImpulse(bodies, contacts, params, iterations,
                           KeepContactOrder{});
}

// velocity 를 푼 뒤 남은 침투를 위치로 되민다. slop 이하는 건드리지 않고
// (지터 방지), 넘는 만큼의 posCorrectFactor 배만 질량 비율로 나눠 민다.
template <class Body>
void ApplyPositionCorrection(std::vector<Body>& bodies,
                             const std::vector<Contact>& contacts,
                             const SolverParams& params) {
    for (const Contact& c : contacts) {
        if (c.depth <= params.slop) continue;
        const float effDepth = (c.depth - params.slop) * params.posCorrectFactor;
        Body& bi = bodies[(size_t)c.i];
        if (c.j >= 0) {
            Body&       bj   = bodies[(size_t)c.j];
            const float wsum = bi.invMass + bj.invMass;
            if (wsum < 1e-9f) continue;
            const float k = effDepth / wsum;
            bi.pos.x -= k * bi.invMass * c.normal.x;
            bi.pos.y -= k * bi.invMass * c.normal.y;
            bj.pos.x += k * bj.invMass * c.normal.x;
            bj.pos.y += k * bj.invMass * c.normal.y;
        } else {
            // 벽 쪽 유효질량도 invMass 하나 — 몫이 1 이라 그대로 밀린다.
            if (bi.invMass <= 0.0f) continue;
            bi.pos.x += effDepth * c.normal.x;
            bi.pos.y += effDepth * c.normal.y;
        }
    }
}

// 위치 보정 pass — 위치가 바뀌면 depth 도 바뀌므로 iter 마다 다시 수집한다.
template <class Body>
void SolvePositionCorrection(std::vector<Body>& bodies,
                             const WorldBounds& bounds,
                             const SolverParams& params, int iterations) {
    std::vector<Contact> contacts;
    for (int iter = 0; iter < std::max(1, iterations); ++iter) {
        GatherCircleContacts(bodies, bounds, contacts);
        ApplyPositionCorrection(bodies, contacts, params);
    }
}

}  // namespace core
}  // namespace physlab
