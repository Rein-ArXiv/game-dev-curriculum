// world — PhysicsWorld.Step(dt): 적분 → broad-phase → narrow-phase → 솔버 → 적분.
//
// 한 스텝의 표준 파이프라인이다. 동적-동적 후보는 수학 모듈의 균일 격자로 추리고,
// 정적 바디(벽·바닥)는 수가 적어 모든 동적 바디와 직접 짝짓는다. 좁은 단계에서
// 실제 접촉을 만들고, Sequential Impulse 를 velocityIterations 회 반복해 응답을
// 수렴시킨 뒤 위치를 적분한다.
//
// 휴면(sleeping)·warm starting(프레임 간 임펄스 캐싱)은 확장 지점으로 남긴다.

#pragma once

#include "math/spatial/uniform_grid.hpp"
#include "physics/integrate.hpp"
#include "physics/narrow_phase/collide.hpp"
#include "physics/solver/sequential_impulse.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace mega {
namespace physics {

class PhysicsWorld {
public:
    math::Vec2  gravity{0, -9.81f};
    int         velocityIterations = 8;
    Integrator  integrator = Integrator::Symplectic;
    SolverParams solverParams;

    // 바디를 전부 버린다. 매치가 다시 시작되거나 맵이 바뀌면 정적 기하가 통째로
    // 달라지므로, 지우지 못하는 세계는 재사용할 수 없다.
    void Clear() {
        bodies_.clear();
        staticIdx_.clear();
        dynIdx_.clear();
        pairs_.clear();
        constraints_.clear();
    }
    void Reserve(size_t n) { bodies_.reserve(n); }

    int AddBody(const RigidBody2D& b) {
        bodies_.push_back(b);
        int idx = (int)bodies_.size() - 1;
        if (b.IsStatic()) staticIdx_.push_back(idx);
        return idx;
    }

    // 동적 바디를 정적으로 굳힌다(잔해 안착). 색인을 유지해야 하므로 여기서만.
    void Freeze(int i) {
        if (bodies_[(size_t)i].IsStatic()) return;
        bodies_[(size_t)i].SetStatic();
        bodies_[(size_t)i].velocity = {0, 0};
        bodies_[(size_t)i].angularVel = 0;
        staticIdx_.push_back(i);
    }
    RigidBody2D&       Body(int i)       { return bodies_[i]; }
    const RigidBody2D& Body(int i) const { return bodies_[i]; }
    size_t Count() const { return bodies_.size(); }
    size_t ContactCount() const { return constraints_.size(); }

    // 격자 broad-phase 설정. **이 호출 여부가 곧 broadphase=grid|naive 스위치다** —
    // 부르지 않으면 전수 짝짓기, 부르면 격자다. 규모 문턱(예전의 "동적 64개
    // 이상")을 안에 두지 않는다: 문턱이 있으면 스위치를 grid 로 놓고도 실제로는
    // naive 가 돌아 두 팔의 수치가 같아지고, 그 상태가 "차이가 없다"로 읽힌다.
    void SetBroadphase(const math::Aabb2& bounds, float cell) {
        bounds_ = bounds; cell_ = cell; haveBounds_ = true;
    }
    bool GridBroadphase() const { return haveBounds_; }
    size_t PairCount() const { return pairs_.size(); }

    // snippet-begin: physics-step
    void Step(float dt) {
        for (auto& b : bodies_) IntegrateForces(b, gravity, dt);   // 1) 힘 → 속도
        BuildPairs();                                              // 2) broad-phase
        constraints_.clear();                                      // 3) narrow-phase
        for (auto& pr : pairs_) {
            RigidBody2D& A = bodies_[pr.first];
            RigidBody2D& B = bodies_[pr.second];
            if (A.IsStatic() && B.IsStatic()) continue;
            Contact c = Collide(A, B);
            if (c.hit) constraints_.push_back(MakeConstraint(A, B, c, dt, solverParams));
        }
        for (int it = 0; it < velocityIterations; ++it)            // 4) 솔버 반복
            for (auto& k : constraints_) SolveVelocity(k);
        for (auto& b : bodies_) IntegrateVelocity(b, dt, integrator);  // 5) 속도 → 위치
    }
    // snippet-end: physics-step

    // 이번 스텝의 최대 관통(솔버 판정용). 제약이 없으면 0.
    float MaxPenetration() const {
        float worst = 0.0f;
        for (const auto& k : constraints_)
            if (k.penetration > worst) worst = k.penetration;
        return worst;
    }

private:
    float BoundingRadius(const RigidBody2D& b) const {
        return b.shape.type == ShapeType::Circle ? b.shape.radius : math::Length(b.shape.half);
    }

    // 두 팔이 **같은 제약 집합을 같은 순서로** 만들어야 A/B 가 비교로 성립한다.
    // 격자 팔은 naive 팔의 부분집합을 내되(겹치지 않는 쌍은 어차피 접촉이 없다),
    // 순서는 격자 순회 순서라 다르다. 그래서 마지막에 정렬한다 — 정렬 비용은
    // 격자 팔이 치르는 값이고, 그 값을 치르지 않으면 잴 수 있는 것이 없다.
    // snippet-begin: build-pairs
    void BuildPairs() {
        pairs_.clear();
        dynIdx_.clear();
        float maxR = 0.0f;
        for (int i = 0; i < (int)bodies_.size(); ++i) {
            float r = BoundingRadius(bodies_[i]);
            if (r > maxR) maxR = r;
            if (!bodies_[i].IsStatic()) dynIdx_.push_back(i);
        }

        if (haveBounds_) {
            const float cell = cell_ > 0 ? cell_ : maxR * 2.0f;
            grid_.Init(bounds_, cell);
            for (int i = 0; i < (int)bodies_.size(); ++i)
                grid_.Insert(bodies_[i].position, i);           // 동적·정적 모두
            const float queryR = maxR * 2.0f + 1e-3f;
            for (int i : dynIdx_)
                grid_.QueryRadius(bodies_[i].position, queryR, [&](int j) {
                    if (j == i) return;
                    if (bodies_[j].IsStatic()) pairs_.emplace_back(i, j);
                    else if (j > i)            pairs_.emplace_back(i, j);
                });
            std::sort(pairs_.begin(), pairs_.end());
            pairs_.erase(std::unique(pairs_.begin(), pairs_.end()), pairs_.end());
            return;
        }

        for (size_t a = 0; a < dynIdx_.size(); ++a)
            for (size_t b = a + 1; b < dynIdx_.size(); ++b)
                pairs_.emplace_back(dynIdx_[a], dynIdx_[b]);
        for (int i : dynIdx_)
            for (int s : staticIdx_)
                pairs_.emplace_back(i, s);
        std::sort(pairs_.begin(), pairs_.end());
    }
    // snippet-end: build-pairs

    std::vector<RigidBody2D>           bodies_;
    std::vector<int>                   staticIdx_, dynIdx_;
    std::vector<std::pair<int, int>>   pairs_;
    std::vector<ContactConstraint>     constraints_;
    math::UniformGrid<int>             grid_;
    math::Aabb2                        bounds_;
    float                              cell_       = 0;
    bool                               haveBounds_ = false;
};

}  // namespace physics
}  // namespace mega
