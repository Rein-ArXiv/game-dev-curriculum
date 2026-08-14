#pragma once

#include <algorithm>
#include <cmath>
#include <utility>

namespace physlab::core {

enum class HarmonicIntegrator {
    ExplicitEuler,
    SemiImplicitEuler,
    VelocityVerlet,
    RK4,
};

inline void StepHarmonic(HarmonicIntegrator kind, float omega,
                         float dt, float& x, float& v) {
    const float w2 = omega * omega;
    switch (kind) {
    case HarmonicIntegrator::ExplicitEuler: {
        const float newV = v - w2 * x * dt;
        const float newX = x + v * dt;
        x = newX;
        v = newV;
        break;
    }
    case HarmonicIntegrator::SemiImplicitEuler:
        v -= w2 * x * dt;
        x += v * dt;
        break;
    case HarmonicIntegrator::VelocityVerlet: {
        const float oldA = -w2 * x;
        x += v * dt + 0.5f * oldA * dt * dt;
        const float newA = -w2 * x;
        v += 0.5f * (oldA + newA) * dt;
        break;
    }
    case HarmonicIntegrator::RK4: {
        auto derivative = [w2](float px, float pv) {
            return std::pair<float, float>{pv, -w2 * px};
        };
        const auto k1 = derivative(x, v);
        const auto k2 = derivative(x + 0.5f * dt * k1.first,
                                   v + 0.5f * dt * k1.second);
        const auto k3 = derivative(x + 0.5f * dt * k2.first,
                                   v + 0.5f * dt * k2.second);
        const auto k4 = derivative(x + dt * k3.first,
                                   v + dt * k3.second);
        x += dt / 6.0f *
             (k1.first + 2.0f * k2.first + 2.0f * k3.first + k4.first);
        v += dt / 6.0f *
             (k1.second + 2.0f * k2.second + 2.0f * k3.second + k4.second);
        break;
    }
    }
}

inline float RestitutionTarget(float initialNormalVelocity,
                               float restitution,
                               float speedThreshold) {
    if (initialNormalVelocity >= -speedThreshold) return 0.0f;
    return -std::clamp(restitution, 0.0f, 1.0f) *
           initialNormalVelocity;
}

inline float AccumulateNormalImpulse(float normalVelocity,
                                     float targetVelocity,
                                     float inverseMassSum,
                                     float& accumulated) {
    if (inverseMassSum <= 1e-9f) return 0.0f;
    const float delta =
        -(normalVelocity - targetVelocity) / inverseMassSum;
    const float next = std::max(0.0f, accumulated + delta);
    const float applied = next - accumulated;
    accumulated = next;
    return applied;
}

struct Aabb2 {
    float centerX;
    float centerY;
    float halfX;
    float halfY;
};

inline bool AabbOverlap(const Aabb2& a, const Aabb2& b) {
    const float aHalfX = std::max(0.0f, a.halfX);
    const float aHalfY = std::max(0.0f, a.halfY);
    const float bHalfX = std::max(0.0f, b.halfX);
    const float bHalfY = std::max(0.0f, b.halfY);
    return std::fabs(a.centerX - b.centerX) <= aHalfX + bHalfX &&
           std::fabs(a.centerY - b.centerY) <= aHalfY + bHalfY;
}

inline bool ProjectDistance(float& ax, float& ay, float inverseMassA,
                            float& bx, float& by, float inverseMassB,
                            float restLength) {
    const float dx = bx - ax;
    const float dy = by - ay;
    const float lengthSquared = dx * dx + dy * dy;
    const float inverseMassSum = inverseMassA + inverseMassB;
    if (lengthSquared <= 1e-12f || inverseMassSum <= 1e-9f) return false;

    const float length = std::sqrt(lengthSquared);
    const float scale =
        (length - restLength) / (length * inverseMassSum);
    ax += scale * inverseMassA * dx;
    ay += scale * inverseMassA * dy;
    bx -= scale * inverseMassB * dx;
    by -= scale * inverseMassB * dy;
    return true;
}

struct UnitNormal2 {
    float x;
    float y;
};

inline UnitNormal2 SafeUnitNormal(float x, float y, int fallbackAxis = 0) {
    const float lengthSquared = x * x + y * y;
    if (lengthSquared <= 1e-12f) {
        return (fallbackAxis & 1) ? UnitNormal2{0.0f, 1.0f}
                                  : UnitNormal2{1.0f, 0.0f};
    }
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    return {x * inverseLength, y * inverseLength};
}

}  // namespace physlab::core
