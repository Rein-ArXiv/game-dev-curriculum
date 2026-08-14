#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace ailab {

struct GridDirection {
    int dx = 0;
    int dy = 0;
};

struct ModelVector2 {
    float x = 0.0f;
    float y = 0.0f;
};

inline ModelVector2 LimitSpeed(ModelVector2 velocity,
                               float minimumSpeed,
                               float maximumSpeed) {
    minimumSpeed = std::max(0.0f, minimumSpeed);
    maximumSpeed = std::max(minimumSpeed, maximumSpeed);
    const float speedSquared =
        velocity.x * velocity.x + velocity.y * velocity.y;
    if (speedSquared <= 1e-12f) return {};

    const float speed = std::sqrt(speedSquared);
    const float target = std::clamp(speed, minimumSpeed, maximumSpeed);
    const float scale = target / speed;
    return {velocity.x * scale, velocity.y * scale};
}

enum class InfluenceView {
    Control,
    Tension,
    Vulnerability,
};

inline float CombineInfluence(float friendly, float enemy,
                              InfluenceView view) {
    const float f = std::clamp(friendly, 0.0f, 1.0f);
    const float e = std::clamp(enemy, 0.0f, 1.0f);
    switch (view) {
    case InfluenceView::Control:
        return f - e;
    case InfluenceView::Tension:
        return 2.0f * std::min(f, e);
    case InfluenceView::Vulnerability:
        return e * (1.0f - f);
    }
    return 0.0f;
}

// An integration field stores the remaining cost to the goal. The local policy
// must minimize edge_cost(current, neighbor) + integration(neighbor), not just
// the neighbor value, because diagonal and cardinal edges have different costs.
template <typename Walkable, typename Cost>
GridDirection SelectFlowDirection(int x, int y,
                                  Walkable&& walkable, Cost&& cost) {
    constexpr float kDiagonalCost = 1.41421356f;
    constexpr float kTolerance = 1e-4f;
    static constexpr int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static constexpr int kDy[8] = {0, 0, 1, -1, 1, -1, 1, -1};

    const float currentCost = cost(x, y);
    if (!std::isfinite(currentCost) || currentCost <= kTolerance) return {};

    float bestCandidate = std::numeric_limits<float>::infinity();
    GridDirection best{};
    for (int i = 0; i < 8; ++i) {
        const int nx = x + kDx[i];
        const int ny = y + kDy[i];
        if (!walkable(nx, ny)) continue;

        const float neighborCost = cost(nx, ny);
        if (!std::isfinite(neighborCost)) continue;
        const float edgeCost =
            (kDx[i] != 0 && kDy[i] != 0) ? kDiagonalCost : 1.0f;
        const float candidate = edgeCost + neighborCost;
        if (candidate < bestCandidate) {
            bestCandidate = candidate;
            best = {kDx[i], kDy[i]};
        }
    }

    // A stale or inconsistent field must not steer an agent uphill.
    return bestCandidate <= currentCost + kTolerance ? best : GridDirection{};
}

inline void ReflectAxis(float& position, float& velocity,
                        float minimum, float maximum) {
    if (!(minimum < maximum)) {
        position = minimum;
        velocity = 0.0f;
        return;
    }

    // Handle more than one boundary crossing so a large frame time cannot
    // leave the object outside the simulation domain.
    while (position < minimum || position > maximum) {
        if (position < minimum) {
            position = minimum + (minimum - position);
            velocity = std::fabs(velocity);
        }
        if (position > maximum) {
            position = maximum - (position - maximum);
            velocity = -std::fabs(velocity);
        }
    }
}

}  // namespace ailab
