// 보간·스플라인 evaluator. 모두 2D Vector2 위에서 동작.
// math part07 (보간) + part08 (스플라인) 의 식들을 그대로.

#pragma once

#include "raylib.h"

#include <cmath>
#include <vector>

namespace mathlab {

inline Vector2 V2Add(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vector2 V2Sub(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vector2 V2Mul(Vector2 a, float s)   { return {a.x * s,   a.y * s  }; }

inline Vector2 Lerp2(Vector2 a, Vector2 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

// 3차 Bezier — 4 control point. P0, P1, P2, P3.
inline Vector2 CubicBezier(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float t) {
    float u = 1.0f - t;
    float b0 = u * u * u;
    float b1 = 3.0f * u * u * t;
    float b2 = 3.0f * u * t * t;
    float b3 = t * t * t;
    return {
        p0.x * b0 + p1.x * b1 + p2.x * b2 + p3.x * b3,
        p0.y * b0 + p1.y * b1 + p2.y * b2 + p3.y * b3,
    };
}

// Cubic Hermite — 두 점 + 두 접선.
inline Vector2 CubicHermite(Vector2 p0, Vector2 m0, Vector2 p1, Vector2 m1, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    float h00 = 2 * t3 - 3 * t2 + 1;
    float h10 = t3 - 2 * t2 + t;
    float h01 = -2 * t3 + 3 * t2;
    float h11 = t3 - t2;
    return {
        p0.x * h00 + m0.x * h10 + p1.x * h01 + m1.x * h11,
        p0.y * h00 + m0.y * h10 + p1.y * h01 + m1.y * h11,
    };
}

// Catmull-Rom (uniform). 4 점 P0..P3 사이의 P1→P2 구간을 t∈[0,1] 로.
// 접선: m1 = 0.5 * (P2 - P0), m2 = 0.5 * (P3 - P1).
inline Vector2 CatmullRom(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float t) {
    Vector2 m1 = V2Mul(V2Sub(p2, p0), 0.5f);
    Vector2 m2 = V2Mul(V2Sub(p3, p1), 0.5f);
    return CubicHermite(p1, m1, p2, m2, t);
}

// 곡선 길이의 LUT 기반 arc-length parameterization.
// param 't' 가 진짜 *진행 거리* 비율이 되도록 LUT 의 길이 누적을 역참조.
//
// 사용:
//   ArcLengthLUT lut;
//   lut.Build(64, [&](float u){ return CubicBezier(p0,p1,p2,p3, u); });
//   Vector2 p = lut.Sample(s, [&](float u){ return CubicBezier(p0,p1,p2,p3, u); });
class ArcLengthLUT {
public:
    void Reset() {
        samples_.clear();
        cumLen_.clear();
        totalLen_ = 0.0f;
    }

    template <class F>
    void Build(int N, F&& sampler) {
        samples_.clear();
        cumLen_.clear();
        totalLen_ = 0.0f;
        if (N <= 0) {
            samples_.push_back(sampler(0.0f));
            cumLen_.push_back(0.0f);
            return;
        }
        samples_.reserve(N + 1);
        cumLen_.reserve(N + 1);

        Vector2 prev = sampler(0.0f);
        samples_.push_back(prev);
        cumLen_.push_back(0.0f);

        float total = 0.0f;
        for (int i = 1; i <= N; ++i) {
            float u  = (float)i / (float)N;
            Vector2 p = sampler(u);
            float dx = p.x - prev.x;
            float dy = p.y - prev.y;
            total += std::sqrt(dx * dx + dy * dy);
            samples_.push_back(p);
            cumLen_.push_back(total);
            prev = p;
        }
        totalLen_ = total;
    }

    float TotalLength() const { return totalLen_; }

    // s ∈ [0, 1] 거리 비율 → 곡선 위 (대략) 점.
    template <class F>
    Vector2 Sample(float s, F&& sampler) const {
        if (cumLen_.empty()) return {0, 0};
        if (samples_.size() == 1 || totalLen_ <= 1e-8f) {
            return samples_.front();
        }
        if (s <= 0) return samples_.front();
        if (s >= 1) return samples_.back();
        float target = s * totalLen_;
        // binary search 로 cumLen_ 에서 target 직전 인덱스
        int lo = 0, hi = (int)cumLen_.size() - 1;
        while (lo + 1 < hi) {
            int mid = (lo + hi) / 2;
            if (cumLen_[mid] <= target) lo = mid;
            else                          hi = mid;
        }
        float seg     = cumLen_[hi] - cumLen_[lo];
        float local   = (seg > 1e-8f) ? (target - cumLen_[lo]) / seg : 0.0f;
        float u       = ((float)lo + local) / (float)(samples_.size() - 1);
        return sampler(u);   // 정확한 점은 LUT 가 아니라 sampler 다시 호출
    }

private:
    std::vector<Vector2> samples_;
    std::vector<float>   cumLen_;
    float                totalLen_ = 0.0f;
};

}  // namespace mathlab
