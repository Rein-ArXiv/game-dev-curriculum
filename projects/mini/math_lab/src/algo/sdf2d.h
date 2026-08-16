// 2D 해석적 SDF — Circle / Box / Capsule + smin 합성.
// math part14 의 식들을 2D 로.

#pragma once

#include "raylib.h"

#include <algorithm>
#include <cmath>

namespace mathlab {
namespace sdf {

// snippet-begin: sdf-shapes
inline float Length2(Vector2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }

// SDF: outside > 0, inside < 0, surface = 0.

// 원 — center 가 원점, radius r.
inline float Circle(Vector2 p, Vector2 center, float r) {
    Vector2 d = {p.x - center.x, p.y - center.y};
    return Length2(d) - r;
}

// AABB — center + half size.
inline float Box(Vector2 p, Vector2 center, Vector2 half) {
    Vector2 q = {std::fabs(p.x - center.x) - half.x,
                  std::fabs(p.y - center.y) - half.y};
    float outside = Length2({std::max(q.x, 0.0f), std::max(q.y, 0.0f)});
    float inside  = std::min(std::max(q.x, q.y), 0.0f);
    return outside + inside;
}

// 캡슐 — 두 끝점 + 반지름.
inline float Capsule(Vector2 p, Vector2 a, Vector2 b, float r) {
    Vector2 ab = {b.x - a.x, b.y - a.y};
    Vector2 ap = {p.x - a.x, p.y - a.y};
    float L2  = ab.x * ab.x + ab.y * ab.y;
    float t   = (L2 < 1e-8f) ? 0.0f
                                : std::clamp((ap.x * ab.x + ap.y * ab.y) / L2,
                                              0.0f, 1.0f);
    Vector2 c = {a.x + ab.x * t, a.y + ab.y * t};
    return Length2({p.x - c.x, p.y - c.y}) - r;
}

// Boolean: union / intersect / subtract.
inline float OpUnion(float a, float b)     { return std::min(a, b); }
inline float OpIntersect(float a, float b) { return std::max(a, b); }
inline float OpSubtract(float a, float b)  { return std::max(a, -b); }
// snippet-end: sdf-shapes

// Smooth Min (polynomial). math part14 의 정정된 형태:
//   h = max(k - |a-b|, 0) / k
//   smin = min(a, b) - h*h*k * 0.25
inline float SminPoly(float a, float b, float k) {
    if (k <= 1e-6f) return OpUnion(a, b);
    float h = std::max(k - std::fabs(a - b), 0.0f) / k;
    return std::min(a, b) - h * h * k * 0.25f;
}

}  // namespace sdf
}  // namespace mathlab
