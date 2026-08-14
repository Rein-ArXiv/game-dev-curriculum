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
    SolverParams solverParams;

    int AddBody(const RigidBody2D& b) {
        bodies_.push_back(b);
        int idx = (int)bodies_.size() - 1;
        if (b.IsStatic()) staticIdx_.push_back(idx);
        return idx;
    }
    RigidBody2D&       Body(int i)       { return bodies_[i]; }
    const RigidBody2D& Body(int i) const { return bodies_[i]; }
    size_t Count() const { return bodies_.size(); }
    size_t ContactCount() const { return constraints_.size(); }

    // 격자 broad-phase 설정(동적-동적). 미설정이면 소규모는 브루트포스로 폴백.
    void SetBroadphase(const math::Aabb2& bounds, float cell) {
        bounds_ = bounds; cell_ = cell; haveBounds_ = true;
    }

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
        for (auto& b : bodies_) IntegrateVelocity(b, dt);          // 5) 속도 → 위치
    }

private:
    float BoundingRadius(const RigidBody2D& b) const {
        return b.shape.type == ShapeType::Circle ? b.shape.radius : math::Length(b.shape.half);
    }

    void BuildPairs() {
        pairs_.clear();
        dynIdx_.clear();
        float maxR = 0.0f;
        for (int i = 0; i < (int)bodies_.size(); ++i)
            if (!bodies_[i].IsStatic()) {
                dynIdx_.push_back(i);
                float r = BoundingRadius(bodies_[i]);
                if (r > maxR) maxR = r;
            }

        // 동적-동적: 격자(대규모) 또는 브루트포스(소규모/미설정).
        if (haveBounds_ && dynIdx_.size() >= 64) {
            float cell = cell_ > 0 ? cell_ : maxR * 2.0f;
            grid_.Init(bounds_, cell);
            for (int i : dynIdx_) grid_.Insert(bodies_[i].position, i);
            float queryR = maxR * 2.0f + 1e-3f;
            for (int i : dynIdx_)
                grid_.QueryRadius(bodies_[i].position, queryR, [&](int j) {
                    if (j > i) pairs_.emplace_back(i, j);
                });
        } else {
            for (size_t a = 0; a < dynIdx_.size(); ++a)
                for (size_t b = a + 1; b < dynIdx_.size(); ++b)
                    pairs_.emplace_back(dynIdx_[a], dynIdx_[b]);
        }

        // 동적-정적: 정적 수가 적으므로 전부 짝짓는다.
        for (int i : dynIdx_)
            for (int s : staticIdx_)
                pairs_.emplace_back(i, s);
    }

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
