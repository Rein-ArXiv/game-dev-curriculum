// math_lab 코어 검증. GUI 없이 algo/ 헤더만 컴파일해 돌린다.
//
// 이 파일의 규칙: "그 알고리즘만이 만족하는 성질" 을 검사한다.
// 퇴화 입력(t=0, k=0, octaves=0, a==b)만 보는 CHECK 는 알고리즘을 우회하므로
// 반드시 비퇴화 케이스를 짝으로 둔다. 독립 오라클(brute force / 다른 알고리즘)
// 과의 대조는 제품 함수를 다시 호출하지 않고 이 파일 안에서 따로 구현한다.

#include "algo/bvh2d.h"
#include "algo/collision2d.h"
#include "algo/curves.h"
#include "algo/depth.h"
#include "algo/kahan.h"
#include "algo/mat.h"
#include "algo/noise.h"
#include "algo/quaternion.h"
#include "algo/sdf2d.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
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

bool NearV2(Vector2 a, Vector2 b, float epsilon = 1e-5f) {
    return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon);
}

bool NearRel(double a, double b, double epsilon = 1e-4) {
    double scale = std::max(1.0, std::max(std::fabs(a), std::fabs(b)));
    return std::fabs(a - b) <= epsilon * scale;
}

// 결정론적 LCG — 무작위 교차 검증을 재현 가능하게 만든다.
class Lcg {
public:
    explicit Lcg(uint32_t seed) : s_(seed ? seed : 1u) {}
    uint32_t Next() {
        s_ = s_ * 1664525u + 1013904223u;
        return s_;
    }
    float Unit() { return (float)(Next() >> 8) * (1.0f / 16777216.0f); }
    float Range(float a, float b) { return a + (b - a) * Unit(); }

private:
    uint32_t s_;
};

double SampleVariance(const std::vector<float>& v) {
    if (v.size() < 2) return 0.0;
    double mean = 0.0;
    for (float x : v) mean += (double)x;
    mean /= (double)v.size();
    double acc = 0.0;
    for (float x : v) {
        double d = (double)x - mean;
        acc += d * d;
    }
    return acc / (double)(v.size() - 1);
}

// ==================== quaternion ====================

float QuatAbsDot(Quaternion a, Quaternion b) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    d = std::fabs(d);
    return (d > 1.0f) ? 1.0f : d;
}

// 두 회전 사이 각 (짧은 호). 등각속도 판정에 쓴다.
float QuatAngleBetween(Quaternion a, Quaternion b) {
    return 2.0f * std::acos(QuatAbsDot(a, b));
}

void TestQuaternion() {
    const Quaternion identity = mathlab::QuatFromAxisAngle({0, 0, 0}, 2.0f);
    CHECK(Near(identity.w, 1.0f));

    constexpr float halfPi = 1.57079632679f;
    const Quaternion z90 =
        mathlab::QuatFromAxisAngle({0, 0, 4}, halfPi);
    const Vector3 rotated = mathlab::QuatRotateVec(z90, {1, 0, 0});
    CHECK(Near(rotated.x, 0.0f, 1e-4f));
    CHECK(Near(rotated.y, 1.0f, 1e-4f));

    const Quaternion scaled = {z90.x * 3, z90.y * 3,
                               z90.z * 3, z90.w * 3};
    const Quaternion mid =
        mathlab::QuatSlerp(mathlab::QuatIdentity(), scaled, 0.5f);
    CHECK(Near(mathlab::QuatNorm(mid), 1.0f, 1e-5f));
    CHECK(mathlab::EulerNearGimbalLock(halfPi));
    CHECK(!mathlab::EulerNearGimbalLock(0.0f));

    // Hamilton 곱의 규약: (a ⊗ b) 는 b 를 먼저 적용한다.
    const Quaternion x90 = mathlab::QuatFromAxisAngle({1, 0, 0}, halfPi);
    const Vector3 seq = mathlab::QuatRotateVec(mathlab::QuatMul(z90, x90),
                                               {0, 0, 1});
    const Vector3 manual =
        mathlab::QuatRotateVec(z90, mathlab::QuatRotateVec(x90, {0, 0, 1}));
    CHECK(Near(seq.x, manual.x, 1e-5f) && Near(seq.y, manual.y, 1e-5f) &&
          Near(seq.z, manual.z, 1e-5f));
    // z 축 90° ∘ x 축 90° 를 (0,0,1) 에 적용하면 (0,-1,0) → 회전 후 (1,0,0).
    CHECK(Near(seq.x, 1.0f, 1e-5f) && Near(seq.y, 0.0f, 1e-5f) &&
          Near(seq.z, 0.0f, 1e-5f));

    // QuatFromEulerXYZ 직접 검증: roll(π/2) 만 주면 z 축 90° 와 같아야 한다.
    const Quaternion rollOnly = mathlab::QuatFromEulerXYZ(0.0f, 0.0f, halfPi);
    CHECK(Near(QuatAbsDot(rollOnly, z90), 1.0f, 1e-5f));
}

// Slerp 는 등각속도(호를 따라 균일), Nlerp 는 아니다.
// M01(QuatSlerp 본문을 QuatNlerp 로 치환)을 잡는 판별 테스트.
void TestQuaternionSlerpUniformSpeed() {
    const Quaternion a = mathlab::QuatIdentity();
    const Quaternion b = mathlab::QuatFromAxisAngle({0, 0, 1}, 2.6f);

    auto stepRatio = [&](Quaternion (*interp)(Quaternion, Quaternion, float)) {
        float maxStep = 0.0f;
        float minStep = 1e30f;
        Quaternion prev = interp(a, b, 0.0f);
        for (int i = 1; i <= 100; ++i) {
            const float t = (float)i / 100.0f;
            const Quaternion cur = interp(a, b, t);
            const float step = QuatAngleBetween(prev, cur);
            maxStep = std::max(maxStep, step);
            minStep = std::min(minStep, step);
            prev = cur;
        }
        return (minStep > 1e-9f) ? (maxStep / minStep) : 1e30f;
    };

    const float slerpRatio = stepRatio(&mathlab::QuatSlerp);
    const float nlerpRatio = stepRatio(&mathlab::QuatNlerp);
    if (!(slerpRatio < 1.05f) || !(nlerpRatio > 1.3f)) {
        std::cerr << "  slerp step ratio = " << slerpRatio
                  << ", nlerp step ratio = " << nlerpRatio << "\n";
    }
    CHECK(slerpRatio < 1.05f);
    // 대조군 — nlerp 는 같은 호에서 균일하지 않아야 한다.
    // (이 CHECK 가 없으면 위 상한이 자명해질 수 있다)
    CHECK(nlerpRatio > 1.3f);

    // 끝점 보존 + 중간점이 단위 노름.
    CHECK(Near(QuatAbsDot(mathlab::QuatSlerp(a, b, 0.0f), a), 1.0f, 1e-6f));
    CHECK(Near(QuatAbsDot(mathlab::QuatSlerp(a, b, 1.0f), b), 1.0f, 1e-6f));
    CHECK(Near(mathlab::QuatNorm(mathlab::QuatSlerp(a, b, 0.37f)), 1.0f, 1e-5f));
    // Nlerp 도 정규화된 결과를 낸다 (M03: 최종 정규화 제거).
    CHECK(Near(mathlab::QuatNorm(mathlab::QuatNlerp(a, b, 0.5f)), 1.0f, 1e-5f));
}

// 짐벌락의 실제 불변식: 가운데 축(yaw)이 ±90° 면 바깥 두 축(pitch/roll)이
// 한 자유도로 축약된다 — 둘을 같은 양만큼 밀어도 회전이 변하지 않는다.
// M02(QuatFromEulerXYZ 곱 순서 반전)를 함께 잡는다.
void TestQuaternionGimbalLock() {
    constexpr float halfPi = 1.57079632679f;

    auto worstMismatch = [](float yaw) {
        float worst = 0.0f;
        for (int pi = -2; pi <= 2; ++pi) {
            for (int ri = -2; ri <= 2; ++ri) {
                for (int ci = -3; ci <= 3; ++ci) {
                    const float p = 0.5f * (float)pi;
                    const float r = 0.5f * (float)ri;
                    const float c = 0.3f * (float)ci;
                    const Quaternion q1 =
                        mathlab::QuatFromEulerXYZ(p + c, yaw, r + c);
                    const Quaternion q2 =
                        mathlab::QuatFromEulerXYZ(p, yaw, r);
                    worst = std::max(worst, 1.0f - QuatAbsDot(q1, q2));
                }
            }
        }
        return worst;
    };

    const float locked = worstMismatch(halfPi);
    const float free   = worstMismatch(0.0f);
    if (!(locked < 1e-5f) || !(free > 1e-3f)) {
        std::cerr << "  gimbal worst(1-|dot|): yaw=pi/2 -> " << locked
                  << ", yaw=0 -> " << free << "\n";
    }
    CHECK(locked < 1e-5f);
    CHECK(free > 1e-3f);
}

// ==================== curves ====================

void TestCurves() {
    const Vector2 p0{0, 0}, p1{1, 3}, p2{3, 3}, p3{4, 0};
    Vector2 start = mathlab::CubicBezier(p0, p1, p2, p3, 0.0f);
    Vector2 end = mathlab::CubicBezier(p0, p1, p2, p3, 1.0f);
    CHECK(Near(start.x, p0.x) && Near(start.y, p0.y));
    CHECK(Near(end.x, p3.x) && Near(end.y, p3.y));

    // t=0.5 해석해: (P0 + 3P1 + 3P2 + P3) / 8 — 중간 기저를 고정한다.
    const Vector2 halfExpect{(p0.x + 3 * p1.x + 3 * p2.x + p3.x) / 8.0f,
                             (p0.y + 3 * p1.y + 3 * p2.y + p3.y) / 8.0f};
    CHECK(NearV2(mathlab::CubicBezier(p0, p1, p2, p3, 0.5f), halfExpect, 1e-5f));
    // 기저 합 = 1 (아핀 불변) — 모든 제어점이 같으면 그 점을 낸다.
    CHECK(NearV2(mathlab::CubicBezier(p1, p1, p1, p1, 0.31f), p1, 1e-6f));

    // Hermite 기저: t=0.5 에서 h00=0.5, h10=0.125, h01=0.5, h11=-0.125.
    const Vector2 m0{2, 0}, m1{0, -4};
    const Vector2 hermiteExpect{
        0.5f * p0.x + 0.125f * m0.x + 0.5f * p3.x - 0.125f * m1.x,
        0.5f * p0.y + 0.125f * m0.y + 0.5f * p3.y - 0.125f * m1.y};
    CHECK(NearV2(mathlab::CubicHermite(p0, m0, p3, m1, 0.5f), hermiteExpect,
                 1e-5f));
    CHECK(NearV2(mathlab::CubicHermite(p0, m0, p3, m1, 0.0f), p0, 1e-6f));
    CHECK(NearV2(mathlab::CubicHermite(p0, m0, p3, m1, 1.0f), p3, 1e-6f));

    // Catmull-Rom: 구간 끝점 보간 + 접선 계수 0.5 고정.
    const Vector2 c0{0, 0}, c1{1, 2}, c2{4, 3}, c3{6, -1};
    CHECK(NearV2(mathlab::CatmullRom(c0, c1, c2, c3, 0.0f), c1, 1e-6f));
    CHECK(NearV2(mathlab::CatmullRom(c0, c1, c2, c3, 1.0f), c2, 1e-6f));
    const Vector2 tan1{0.5f * (c2.x - c0.x), 0.5f * (c2.y - c0.y)};
    const Vector2 tan2{0.5f * (c3.x - c1.x), 0.5f * (c3.y - c1.y)};
    const Vector2 crExpect{
        0.5f * c1.x + 0.125f * tan1.x + 0.5f * c2.x - 0.125f * tan2.x,
        0.5f * c1.y + 0.125f * tan1.y + 0.5f * c2.y - 0.125f * tan2.y};
    CHECK(NearV2(mathlab::CatmullRom(c0, c1, c2, c3, 0.5f), crExpect, 1e-5f));

    // Lerp2 — 헤더 공개 API. 중간값과 끝점.
    CHECK(NearV2(mathlab::Lerp2({0, 0}, {4, 2}, 0.25f), Vector2{1.0f, 0.5f},
                 1e-6f));
    CHECK(NearV2(mathlab::Lerp2({-1, 5}, {3, -3}, 1.0f), Vector2{3.0f, -3.0f},
                 1e-6f));

    mathlab::ArcLengthLUT lut;
    auto line = [](float t) { return Vector2{10.0f * t, 0.0f}; };
    lut.Build(16, line);
    CHECK(Near(lut.TotalLength(), 10.0f));
    CHECK(Near(lut.Sample(0.25f, line).x, 2.5f));

    // 등속 재매개화: 비선형 곡선에서 s 간격당 이동 거리가 균일해야 한다.
    auto accel = [](float t) { return Vector2{100.0f * t * t, 0.0f}; };
    mathlab::ArcLengthLUT alut;
    alut.Build(512, accel);
    CHECK(Near(alut.TotalLength(), 100.0f, 1e-2f));
    float maxSeg = 0.0f, minSeg = 1e30f;
    Vector2 prev = alut.Sample(0.0f, accel);
    for (int i = 1; i <= 20; ++i) {
        Vector2 cur = alut.Sample((float)i / 20.0f, accel);
        float seg = std::fabs(cur.x - prev.x);
        maxSeg = std::max(maxSeg, seg);
        minSeg = std::min(minSeg, seg);
        prev = cur;
    }
    CHECK(maxSeg / minSeg < 1.1f);
    // 대조군: 같은 곡선을 t 로 균일 샘플하면 전혀 균일하지 않다.
    float rawMax = 0.0f, rawMin = 1e30f;
    Vector2 rawPrev = accel(0.0f);
    for (int i = 1; i <= 20; ++i) {
        Vector2 cur = accel((float)i / 20.0f);
        float seg = std::fabs(cur.x - rawPrev.x);
        rawMax = std::max(rawMax, seg);
        rawMin = std::min(rawMin, seg);
        rawPrev = cur;
    }
    CHECK(rawMax / rawMin > 5.0f);

    lut.Build(0, line);
    CHECK(Near(lut.TotalLength(), 0.0f));
    CHECK(Near(lut.Sample(0.5f, line).x, 0.0f));
}

// ==================== collision ====================

void TestCollision() {
    const mathlab::OBB2D a{{0, 0}, 1, 1, 0};
    const mathlab::OBB2D overlap{{1.5f, 0}, 1, 1, 0.2f};
    const mathlab::OBB2D separate{{4, 0}, 1, 1, 0};

    CHECK(mathlab::OBBOBBSat2D(a, overlap).intersect);
    CHECK(!mathlab::OBBOBBSat2D(a, separate).intersect);

    const auto gjkHit = mathlab::GJK2DTraced(a, overlap);
    const auto gjkMiss = mathlab::GJK2DTraced(a, separate);
    CHECK(!gjkHit.empty() && gjkHit.back().done && gjkHit.back().intersect);
    CHECK(!gjkMiss.empty() && gjkMiss.back().done && !gjkMiss.back().intersect);

    // 오직 B 의 면 법선에서만 분리되는 배치. A 의 두 축과 B 의 긴 축에서는
    // 투영이 겹치므로, 후보축에서 B 의 법선을 빼면 "충돌"로 잘못 판정한다.
    //   A: 반크기 1 정사각형, B: 45° 기울인 길고 얇은 막대를 A 의 대각선
    //   투영 반경(√2) 바깥으로 법선 방향만큼 밀어 둔 것.
    const mathlab::OBB2D square{{0, 0}, 1.0f, 1.0f, 0.0f};
    const mathlab::OBB2D slab{{-1.2119f, 1.2119f}, 3.0f, 0.1f, 0.7853982f};
    const auto slabSat = mathlab::OBBOBBSat2D(square, slab);
    CHECK(!slabSat.intersect);
    // A 의 두 축만으로는 분리되지 않는다 — 즉 B 축이 판정을 결정한다.
    CHECK(slabSat.axes.size() == 4);
    if (slabSat.axes.size() == 4) {
        CHECK(!slabSat.axes[0].separating);
        CHECK(!slabSat.axes[1].separating);
        CHECK(!slabSat.axes[2].separating);
        CHECK(slabSat.axes[3].separating);
    }
    const auto slabGjk = mathlab::GJK2DTraced(square, slab);
    CHECK(!slabGjk.empty() && slabGjk.back().done &&
          !slabGjk.back().intersect);

    // 무작위 20000 쌍 — SAT 와 GJK 는 서로 독립 구현이므로 판정이 일치해야 한다.
    Lcg rng(20260813u);
    int disagree = 0;
    int hits = 0;
    const int kPairs = 20000;
    for (int i = 0; i < kPairs; ++i) {
        const mathlab::OBB2D A{{rng.Range(-4.0f, 4.0f), rng.Range(-4.0f, 4.0f)},
                               rng.Range(0.3f, 1.5f), rng.Range(0.3f, 1.5f),
                               rng.Range(0.0f, 3.14159265f)};
        const mathlab::OBB2D B{{rng.Range(-4.0f, 4.0f), rng.Range(-4.0f, 4.0f)},
                               rng.Range(0.3f, 1.5f), rng.Range(0.3f, 1.5f),
                               rng.Range(0.0f, 3.14159265f)};
        const bool sat = mathlab::OBBOBBSat2D(A, B).intersect;
        const auto trace = mathlab::GJK2DTraced(A, B);
        const bool gjk =
            !trace.empty() && trace.back().done && trace.back().intersect;
        if (sat) ++hits;
        if (sat != gjk) {
            if (disagree < 3) {
                std::cerr << "  SAT/GJK disagree at pair " << i << ": sat="
                          << sat << " gjk=" << gjk << "\n";
            }
            ++disagree;
        }
    }
    CHECK(disagree == 0);
    // 표본이 한쪽으로 쏠려 위 CHECK 가 자명해지지 않았는지 확인.
    CHECK(hits > kPairs / 10);
    CHECK(hits < kPairs - kPairs / 10);
}

// ==================== sdf ====================

void TestSdf() {
    CHECK(Near(mathlab::sdf::Circle({2, 0}, {0, 0}, 2), 0.0f));
    CHECK(mathlab::sdf::Circle({0, 0}, {0, 0}, 2) < 0.0f);
    CHECK(mathlab::sdf::Box({3, 0}, {0, 0}, {1, 1}) > 0.0f);
    CHECK(Near(mathlab::sdf::Capsule({0, 2}, {0, 0}, {0, 0}, 2), 0.0f));
    CHECK(Near(mathlab::sdf::SminPoly(1.0f, 2.0f, 0.0f), 1.0f));

    // Box 내부 — inside 항이 없으면 0 이 된다.
    CHECK(Near(mathlab::sdf::Box({0, 0}, {0, 0}, {1, 1}), -1.0f));
    CHECK(Near(mathlab::sdf::Box({0, 0.5f}, {0, 0}, {1, 1}), -0.5f));
    // 모서리 바깥은 유클리드 거리.
    CHECK(Near(mathlab::sdf::Box({4, 5}, {0, 0}, {1, 1}), 5.0f));

    // Capsule — 선분 clamp 경로. 투영이 [0,1] 밖이면 끝점까지의 거리다.
    const Vector2 ca{-1, 0}, cb{1, 0};
    CHECK(Near(mathlab::sdf::Capsule({0, 3}, ca, cb, 1.0f), 2.0f, 1e-5f));
    CHECK(Near(mathlab::sdf::Capsule({5, 0}, ca, cb, 1.0f), 3.0f, 1e-5f));
    CHECK(Near(mathlab::sdf::Capsule({-4, 0}, ca, cb, 1.0f), 2.0f, 1e-5f));
    CHECK(Near(mathlab::sdf::Capsule({1, 4}, ca, cb, 1.0f), 3.0f, 1e-5f));

    // Boolean 연산 — 헤더 공개 API 3종.
    CHECK(Near(mathlab::sdf::OpUnion(1.0f, 2.0f), 1.0f));
    CHECK(Near(mathlab::sdf::OpIntersect(1.0f, 2.0f), 2.0f));
    CHECK(Near(mathlab::sdf::OpSubtract(1.0f, 2.0f), 1.0f));
    CHECK(Near(mathlab::sdf::OpSubtract(1.0f, -2.0f), 2.0f));
    // union ≤ intersect, subtract 는 b 안쪽을 파낸다.
    CHECK(mathlab::sdf::OpUnion(-3.0f, 1.0f) <
          mathlab::sdf::OpIntersect(-3.0f, 1.0f));
    CHECK(mathlab::sdf::OpSubtract(-3.0f, -1.0f) > 0.0f);

    // Smooth min — k>0 이어야 실제 blend 식이 실행된다.
    //   |a-b| < k 구간에서는 min 보다 *작아야* 하고,
    //   |a-b| > k 구간에서는 min 과 같아야 한다.
    CHECK(mathlab::sdf::SminPoly(1.0f, 1.2f, 1.0f) < 1.0f);
    CHECK(Near(mathlab::sdf::SminPoly(1.0f, 1.2f, 1.0f), 1.0f - 0.16f, 1e-5f));
    CHECK(Near(mathlab::sdf::SminPoly(1.0f, 5.0f, 1.0f), 1.0f, 1e-6f));
    CHECK(Near(mathlab::sdf::SminPoly(2.0f, 2.0f, 1.0f), 2.0f - 0.25f, 1e-5f));
    // k → 0 이면 min 으로 수렴한다.
    CHECK(Near(mathlab::sdf::SminPoly(1.0f, 1.2f, 1e-3f), 1.0f, 1e-3f));
    // 대칭성과 하한: smin 은 min 보다 크지 않고 min - k/4 보다 작지 않다.
    Lcg rng(77u);
    for (int i = 0; i < 500; ++i) {
        const float a = rng.Range(-3.0f, 3.0f);
        const float b = rng.Range(-3.0f, 3.0f);
        const float k = rng.Range(0.01f, 2.0f);
        const float s = mathlab::sdf::SminPoly(a, b, k);
        CHECK(Near(s, mathlab::sdf::SminPoly(b, a, k), 1e-6f));
        CHECK(s <= std::min(a, b) + 1e-6f);
        CHECK(s >= std::min(a, b) - 0.25f * k - 1e-6f);
    }
}

// ==================== bvh ====================

struct ObjRange {
    int begin;
    int end;
};

ObjRange BvhNodeRange(const std::vector<mathlab::BVHNode2D>& nodes, int idx) {
    const mathlab::BVHNode2D& n = nodes[(size_t)idx];
    if (n.leftChild < 0) return {n.firstObj, n.firstObj + n.objCount};
    const ObjRange l = BvhNodeRange(nodes, n.leftChild);
    const ObjRange r = BvhNodeRange(nodes, n.rightChild);
    return {std::min(l.begin, r.begin), std::max(l.end, r.end)};
}

// 제품의 RayAABB2D 를 쓰지 않는 독립 slab 구현 — brute force 오라클용.
bool RefRayBox(Vector2 ro, Vector2 rd, const mathlab::AABB2D& b, float& tHit) {
    const double o[2] = {(double)ro.x, (double)ro.y};
    const double d[2] = {(double)rd.x, (double)rd.y};
    const double lo[2] = {(double)b.min.x, (double)b.min.y};
    const double hi[2] = {(double)b.max.x, (double)b.max.y};
    double tmin = -1e30, tmax = 1e30;
    for (int i = 0; i < 2; ++i) {
        if (std::fabs(d[i]) < 1e-12) {
            if (o[i] < lo[i] || o[i] > hi[i]) return false;
            continue;
        }
        double t0 = (lo[i] - o[i]) / d[i];
        double t1 = (hi[i] - o[i]) / d[i];
        if (t0 > t1) std::swap(t0, t1);
        tmin = std::max(tmin, t0);
        tmax = std::min(tmax, t1);
        if (tmin > tmax) return false;
    }
    if (tmax < 0.0) return false;
    tHit = (float)std::max(tmin, 0.0);
    return true;
}

// median split 이 실제로 일어났는지 — 노드 구조 자체의 불변식.
void CheckBvhStructure(const std::vector<mathlab::AABB2D>& boxes,
                       const mathlab::BVH2D& bvh, int leafThreshold) {
    const std::vector<mathlab::BVHNode2D>& nodes = bvh.Nodes();
    const std::vector<int>& order = bvh.ObjectIndex();
    CHECK(nodes.size() > 1);
    CHECK(order.size() == boxes.size());
    if (nodes.empty() || order.size() != boxes.size()) return;

    std::vector<int> covered(boxes.size(), 0);
    int internalNodes = 0;
    int splitOnY = 0;
    int badBounds = 0;
    int badPartition = 0;
    int badLeafSize = 0;

    for (int i = 0; i < (int)nodes.size(); ++i) {
        const mathlab::BVHNode2D& n = nodes[(size_t)i];
        const ObjRange rg = BvhNodeRange(nodes, i);

        // 노드 박스는 그 아래 모든 객체를 포함해야 한다 (AABBUnion 계약).
        for (int k = rg.begin; k < rg.end; ++k) {
            const mathlab::AABB2D& b = boxes[(size_t)order[(size_t)k]];
            if (b.min.x < n.bounds.min.x - 1e-4f ||
                b.min.y < n.bounds.min.y - 1e-4f ||
                b.max.x > n.bounds.max.x + 1e-4f ||
                b.max.y > n.bounds.max.y + 1e-4f) {
                ++badBounds;
            }
        }

        if (n.leftChild < 0) {
            if (n.objCount < 1 || n.objCount > leafThreshold) ++badLeafSize;
            for (int k = rg.begin; k < rg.end; ++k) {
                ++covered[(size_t)order[(size_t)k]];
            }
            continue;
        }

        ++internalNodes;
        const ObjRange l = BvhNodeRange(nodes, n.leftChild);
        const ObjRange r = BvhNodeRange(nodes, n.rightChild);
        CHECK(l.end == r.begin);

        // 분할축은 노드 박스의 긴 변, 그리고 좌/우 부분집합은 그 축에서
        // 완전히 갈라져 있어야 한다 (nth_element 의 계약).
        const int axis = (n.bounds.Width() >= n.bounds.Height()) ? 0 : 1;
        if (axis == 1) ++splitOnY;
        float maxLeft = -1e30f;
        float minRight = 1e30f;
        for (int k = l.begin; k < l.end; ++k) {
            const Vector2 c = boxes[(size_t)order[(size_t)k]].Center();
            maxLeft = std::max(maxLeft, (axis == 0) ? c.x : c.y);
        }
        for (int k = r.begin; k < r.end; ++k) {
            const Vector2 c = boxes[(size_t)order[(size_t)k]].Center();
            minRight = std::min(minRight, (axis == 0) ? c.x : c.y);
        }
        if (maxLeft > minRight + 1e-4f) ++badPartition;
    }

    if (badBounds || badPartition || badLeafSize) {
        std::cerr << "  bvh structure: badBounds=" << badBounds
                  << " badPartition=" << badPartition
                  << " badLeafSize=" << badLeafSize
                  << " internal=" << internalNodes << "\n";
    }
    CHECK(badBounds == 0);
    CHECK(badPartition == 0);
    CHECK(badLeafSize == 0);
    CHECK(internalNodes > 5);
    CHECK(splitOnY > 0);   // 축 선택이 고정돼 있지 않은지
    for (size_t i = 0; i < covered.size(); ++i) CHECK(covered[i] == 1);
}

void TestBvh() {
    float tn = 0, tf = 0;
    const mathlab::AABB2D box{{1, 1}, {3, 3}};
    CHECK(mathlab::RayAABB2D({0, 2}, {1, 0}, box, tn, tf));
    CHECK(Near(tn, 1.0f) && Near(tf, 3.0f));
    CHECK(!mathlab::RayAABB2D({0, 4}, {1, 0}, box, tn, tf));
    // 박스가 광선 뒤에 통째로 있으면 hit 이 아니다.
    CHECK(!mathlab::RayAABB2D({6, 2}, {1, 0}, box, tn, tf));

    std::vector<mathlab::AABB2D> boxes = {
        {{1, 1}, {2, 2}},
        {{4, 1}, {5, 2}},
        {{7, 1}, {8, 2}},
    };
    mathlab::BVH2D bvh;
    bvh.Build(boxes, 0);
    CHECK(!bvh.Nodes().empty());
    auto hit = bvh.Trace(boxes, {0, 1.5f}, {1, 0});
    CHECK(hit.hit);
    CHECK(hit.hitObjIdx == 0);
    CHECK(Near(hit.hitT, 1.0f));

    // 가지치기: 이미 찾은 hit 보다 먼 노드는 방문하지 않는다.
    const std::vector<mathlab::AABB2D> far2 = {
        {{0, 0}, {1, 1}},
        {{100, 0}, {101, 1}},
    };
    mathlab::BVH2D prune;
    prune.Build(far2, 1);
    CHECK(prune.Nodes().size() == 3);
    const auto pruned = prune.Trace(far2, {-1.0f, 0.5f}, {1, 0});
    CHECK(pruned.hit);
    CHECK(pruned.hitObjIdx == 0);
    CHECK(Near(pruned.hitT, 1.0f));
    if (pruned.visitedNodes.size() != 2) {
        std::cerr << "  bvh visited " << pruned.visitedNodes.size()
                  << " nodes (expected 2 — far node must be pruned)\n";
    }
    CHECK(pruned.visitedNodes.size() == 2);

    // 무작위 40 박스 — 구조 불변식 + 3000 광선 brute force 대조.
    Lcg rng(4242u);
    std::vector<mathlab::AABB2D> many;
    many.reserve(40);
    for (int i = 0; i < 40; ++i) {
        const float x = rng.Range(0.0f, 50.0f);
        const float y = rng.Range(0.0f, 50.0f);
        const float w = rng.Range(0.5f, 3.0f);
        const float h = rng.Range(0.5f, 3.0f);
        many.push_back({{x, y}, {x + w, y + h}});
    }
    mathlab::BVH2D big;
    big.Build(many, 2);
    CheckBvhStructure(many, big, 2);

    int mismatch = 0;
    int hitCount = 0;
    long long visitedTotal = 0;
    const int kRays = 3000;
    for (int i = 0; i < kRays; ++i) {
        const Vector2 ro{rng.Range(-5.0f, 55.0f), rng.Range(-5.0f, 55.0f)};
        const float ang = rng.Range(0.0f, 6.2831853f);
        const Vector2 rd{std::cos(ang), std::sin(ang)};

        // brute force 오라클 — 독립 구현.
        bool refHit = false;
        float refT = 1e30f;
        int refIdx = -1;
        float secondT = 1e30f;
        for (int b = 0; b < (int)many.size(); ++b) {
            float t = 0.0f;
            if (!RefRayBox(ro, rd, many[(size_t)b], t)) continue;
            if (t < refT) {
                secondT = refT;
                refT = t;
                refIdx = b;
                refHit = true;
            } else if (t < secondT) {
                secondT = t;
            }
        }

        const auto res = big.Trace(many, ro, rd);
        visitedTotal += (long long)res.visitedNodes.size();
        if (res.hit) ++hitCount;

        bool bad = false;
        if (res.hit != refHit) bad = true;
        else if (refHit) {
            if (!Near(res.hitT, refT, 1e-3f)) bad = true;
            // 최근접이 유일할 때만 인덱스까지 비교 (동률은 순서 의존).
            else if (secondT - refT > 1e-3f && res.hitObjIdx != refIdx) bad = true;
        }
        if (bad) {
            if (mismatch < 3) {
                std::cerr << "  bvh ray " << i << ": bvh(hit=" << res.hit
                          << ", t=" << res.hitT << ", idx=" << res.hitObjIdx
                          << ") vs ref(hit=" << refHit << ", t=" << refT
                          << ", idx=" << refIdx << ")\n";
            }
            ++mismatch;
        }
    }
    CHECK(mismatch == 0);
    CHECK(hitCount > kRays / 4);   // 대부분 빗나가면 위 대조가 자명해진다
    // 가지치기가 실제로 동작하면 광선당 방문 노드는 전체 노드 수보다 훨씬 적다.
    const double avgVisited = (double)visitedTotal / (double)kRays;
    if (!(avgVisited < 0.5 * (double)big.Nodes().size())) {
        std::cerr << "  bvh avg visited = " << avgVisited << " of "
                  << big.Nodes().size() << " nodes\n";
    }
    CHECK(avgVisited < 0.5 * (double)big.Nodes().size());

    bvh.Build({}, 0);
    CHECK(bvh.Nodes().empty());
}

// ==================== noise ====================

void TestNoise() {
    const float a = mathlab::nz::Value2D(1.25f, -2.5f, 42);
    const float b = mathlab::nz::Value2D(1.25f, -2.5f, 42);
    CHECK(a == b);
    CHECK(a >= 0.0f && a <= 1.0f);

    const float p = mathlab::nz::Perlin2D(0.25f, 0.75f, 7);
    const float s = mathlab::nz::Simplex2D(0.25f, 0.75f, 7);
    CHECK(std::isfinite(p));
    CHECK(std::isfinite(s));

    // 보간 커널 — Smoothstep 과 Quintic 은 서로 다른 곡선이다.
    CHECK(Near(mathlab::nz::Smoothstep(0.0f), 0.0f));
    CHECK(Near(mathlab::nz::Smoothstep(1.0f), 1.0f));
    CHECK(Near(mathlab::nz::Smoothstep(0.5f), 0.5f));
    CHECK(Near(mathlab::nz::Smoothstep(0.25f), 0.15625f));
    CHECK(Near(mathlab::nz::Quintic(0.0f), 0.0f));
    CHECK(Near(mathlab::nz::Quintic(1.0f), 1.0f));
    CHECK(Near(mathlab::nz::Quintic(0.5f), 0.5f));
    CHECK(Near(mathlab::nz::Quintic(0.25f), 0.103515625f));

    // Value2D 는 격자 해시를 Smoothstep 가중으로 섞는다 (선형 보간이 아니다).
    {
        const uint32_t seed = 11u;
        const int ix = 3, iy = -2;
        const float v00 = mathlab::nz::HashTo01(ix, iy, seed);
        const float v10 = mathlab::nz::HashTo01(ix + 1, iy, seed);
        const float u = mathlab::nz::Smoothstep(0.25f);
        CHECK(Near(mathlab::nz::Value2D((float)ix + 0.25f, (float)iy, seed),
                   v00 + (v10 - v00) * u, 1e-5f));
        // 격자 정점에서는 해시값 자체.
        CHECK(Near(mathlab::nz::Value2D((float)ix, (float)iy, seed), v00,
                   1e-6f));
    }

    // Perlin 은 Value 노이즈와 달리 *quintic* fade 로 섞는다.
    // 격자선 위(fy=0)에서는 x 방향 1차원 보간만 남으므로 가중치를 직접 고정할 수 있다.
    {
        const uint32_t seed = 5u;
        const float fx = 0.25f;
        const float u = mathlab::nz::Quintic(fx);
        int discriminating = 0;
        for (int ix = 0; ix < 8; ++ix) {
            for (int iy = 0; iy < 8; ++iy) {
                float g0x, g0y, g1x, g1y;
                mathlab::nz::Grad2(ix, iy, seed, g0x, g0y);
                mathlab::nz::Grad2(ix + 1, iy, seed, g1x, g1y);
                const float n00 = g0x * fx;
                const float n10 = g1x * (fx - 1.0f);
                if (std::fabs(n10 - n00) > 0.2f) ++discriminating;
                CHECK(Near(mathlab::nz::Perlin2D((float)ix + fx, (float)iy,
                                                 seed),
                           n00 + (n10 - n00) * u, 1e-6f));
            }
        }
        // 가중치가 결과에 실제로 드러나는 셀이 있어야 위 CHECK 가 의미를 갖는다.
        CHECK(discriminating > 4);
    }

    // 세 노이즈 모두 상수가 아니어야 한다 (해시/그라디언트를 상수로 바꾸면 0).
    {
        Lcg rng(1234u);
        std::vector<float> vs, ps, ss;
        vs.reserve(1000);
        ps.reserve(1000);
        ss.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            const float x = rng.Range(-40.0f, 40.0f);
            const float y = rng.Range(-40.0f, 40.0f);
            vs.push_back(mathlab::nz::Value2D(x, y, 7));
            ps.push_back(mathlab::nz::Perlin2D(x, y, 7));
            ss.push_back(mathlab::nz::Simplex2D(x, y, 7));
        }
        const double vVar = SampleVariance(vs);
        const double pVar = SampleVariance(ps);
        const double sVar = SampleVariance(ss);
        if (vVar < 0.001 || pVar < 0.001 || sVar < 0.001) {
            std::cerr << "  noise variance: value=" << vVar << " perlin="
                      << pVar << " simplex=" << sVar << "\n";
        }
        CHECK(vVar > 0.001);
        CHECK(pVar > 0.001);
        CHECK(sVar > 0.001);
        // 격자 정점에서 Perlin 은 정확히 0 (그라디언트·오프셋 내적이 0).
        CHECK(Near(mathlab::nz::Perlin2D(3.0f, -5.0f, 7), 0.0f, 1e-6f));
    }

    const float zeroOctaves = mathlab::nz::FBM2D(
        1.0f, 2.0f, 3, 0, 2.0f, 0.5f,
        [](float x, float y, uint32_t seed) {
            return mathlab::nz::Perlin2D(x, y, seed);
        });
    CHECK(Near(zeroOctaves, 0.0f));

    // FBM 은 진폭 가중 *평균* 이다 — 상수 노이즈를 넣으면 그 상수를 돌려준다.
    // 정규화(norm 나눗셈)를 빼면 1.875 가 나온다.
    auto constNoise = [](float, float, uint32_t) { return 1.0f; };
    CHECK(Near(mathlab::nz::FBM2D(0.3f, 0.7f, 5, 4, 2.0f, 0.5f, constNoise),
               1.0f, 1e-6f));
    CHECK(Near(mathlab::nz::FBM2D(0.3f, 0.7f, 5, 1, 2.0f, 0.5f, constNoise),
               1.0f, 1e-6f));
    CHECK(Near(mathlab::nz::FBM2D(0.3f, 0.7f, 5, 6, 2.0f, 0.7f, constNoise),
               1.0f, 1e-6f));

    // 옥타브를 쌓아도 진폭 범위는 단일 옥타브 범위를 넘지 않는다.
    {
        Lcg rng(555u);
        auto perlin = [](float x, float y, uint32_t seed) {
            return mathlab::nz::Perlin2D(x, y, seed);
        };
        std::vector<float> one, four;
        one.reserve(600);
        four.reserve(600);
        float maxAbs4 = 0.0f;
        for (int i = 0; i < 600; ++i) {
            const float x = rng.Range(-20.0f, 20.0f);
            const float y = rng.Range(-20.0f, 20.0f);
            one.push_back(mathlab::nz::FBM2D(x, y, 9, 1, 2.0f, 0.5f, perlin));
            const float f4 =
                mathlab::nz::FBM2D(x, y, 9, 4, 2.0f, 0.5f, perlin);
            four.push_back(f4);
            maxAbs4 = std::max(maxAbs4, std::fabs(f4));
        }
        CHECK(maxAbs4 < 0.6f);   // 정규화가 빠지면 Perlin 범위를 넘어선다
        const double var1 = SampleVariance(one);
        const double var4 = SampleVariance(four);
        if (!(var1 > var4)) {
            std::cerr << "  fbm variance: octaves1=" << var1
                      << " octaves4=" << var4 << "\n";
        }
        CHECK(var1 > 0.001);
        CHECK(var4 > 0.001);
        // gain<1 이면 고주파 옥타브가 감쇠되어 총 분산이 줄어든다.
        CHECK(var4 < var1);
    }
}

// ==================== depth (math part18) ====================

void TestDepth() {
    const float n = 0.1f;
    const float f = 1000.0f;

    CHECK(Near(mathlab::StdNdcZ(-n, n, f), 0.0f, 1e-5f));
    CHECK(Near(mathlab::StdNdcZ(-f, n, f), 1.0f, 1e-5f));
    CHECK(Near(mathlab::ReverseNdcZ(-n, n, f), 1.0f, 1e-5f));
    CHECK(Near(mathlab::ReverseNdcZ(-f, n, f), 0.0f, 1e-5f));

    // 두 매핑은 서로 뒤집힌 관계다: reverse = 1 - std. 그리고 std 는 단조 증가.
    float prev = -1.0f;
    for (int i = 0; i <= 40; ++i) {
        const float z = -(n + (f - n) * (float)i / 40.0f);
        const float sv = mathlab::StdNdcZ(z, n, f);
        const float rv = mathlab::ReverseNdcZ(z, n, f);
        CHECK(sv >= prev - 1e-6f);
        CHECK(Near(rv, 1.0f - sv, 1e-5f));
        prev = sv;
    }

    // 표준 depth 는 근거리에 극단적으로 몰린다 — 절반 거리에서 이미 0.999 이상.
    CHECK(mathlab::StdNdcZ(-(n + (f - n) * 0.5f), n, f) > 0.99f);
    // 반대로 reverse-Z 는 같은 지점에서 0.01 미만이다.
    CHECK(mathlab::ReverseNdcZ(-(n + (f - n) * 0.5f), n, f) < 0.01f);
}

// ==================== kahan (math part20) ====================

void TestKahan() {
    const float big = 100000.0f;
    const float small = 0.0001f;
    const int N = 100000;
    const double truth = (double)big + (double)small * (double)N;

    const float naive = mathlab::NaiveSum(big, small, N);
    const float kahan = mathlab::KahanSum(big, small, N);
    if (!(std::fabs((double)kahan - truth) < 1e-3) ||
        !(std::fabs((double)naive - truth) > 1.0)) {
        std::cerr << "  kahan=" << kahan << " naive=" << naive
                  << " truth=" << truth << "\n";
    }
    // Kahan 은 double 기준값과 사실상 같고,
    CHECK(std::fabs((double)kahan - truth) < 1e-3);
    // naive 는 작은 항이 통째로 ULP 아래로 묻혀 big 에서 한 발짝도 못 간다.
    CHECK(std::fabs((double)naive - truth) > 1.0);
    CHECK(Near(naive, big, 1e-3f));

    // 큰 수가 없어도 100만 항 누적에서 Kahan 이 더 정확하다.
    const float k2 = mathlab::KahanSum(0.0f, 0.1f, 1000000);
    const float n2 = mathlab::NaiveSum(0.0f, 0.1f, 1000000);
    const double truth2 = (double)0.1f * 1000000.0;
    CHECK(std::fabs((double)k2 - truth2) <= std::fabs((double)n2 - truth2));
    CHECK(std::fabs((double)k2 - truth2) < 1e-2);

    // 누적기 자체의 계약: 보상항이 실제로 잘린 몫을 담는다.
    mathlab::KahanAccumulator acc(1.0f);
    acc.Add(1e-8f);
    CHECK(Near(acc.Sum(), 1.0f, 1e-9f));      // float 1.0 에 흡수되고
    CHECK(std::fabs(acc.Compensation()) > 1e-9f);  // 보상항에 남는다
    for (int i = 0; i < 1000; ++i) acc.Add(1e-8f);
    CHECK(acc.Sum() > 1.0f);   // 모이면 되살아난다
}

// ==================== vectors (math part02) ====================

void TestVectorAlgebra() {
    // 외적은 두 입력에 수직이고, Lagrange 항등식을 만족한다.
    Lcg rng(31337u);
    for (int i = 0; i < 300; ++i) {
        const Vector3 a{rng.Range(-2.0f, 2.0f), rng.Range(-2.0f, 2.0f),
                        rng.Range(-2.0f, 2.0f)};
        const Vector3 b{rng.Range(-2.0f, 2.0f), rng.Range(-2.0f, 2.0f),
                        rng.Range(-2.0f, 2.0f)};
        const Vector3 c = mathlab::V3Cross(a, b);
        CHECK(std::fabs(mathlab::V3Dot(c, a)) < 1e-4f);
        CHECK(std::fabs(mathlab::V3Dot(c, b)) < 1e-4f);
        // |a×b|² + (a·b)² = |a|²|b|²
        const double lhs = (double)mathlab::V3Dot(c, c) +
                           (double)mathlab::V3Dot(a, b) * mathlab::V3Dot(a, b);
        const double rhs = (double)mathlab::V3Dot(a, a) * mathlab::V3Dot(b, b);
        CHECK(NearRel(lhs, rhs, 1e-4));
        // 반대칭
        const Vector3 c2 = mathlab::V3Cross(b, a);
        CHECK(Near(c.x, -c2.x, 1e-5f) && Near(c.y, -c2.y, 1e-5f) &&
              Near(c.z, -c2.z, 1e-5f));
    }

    CHECK(Near(mathlab::V3Length(mathlab::V3Normalize({3, 4, 0})), 1.0f, 1e-6f));
    CHECK(Near(mathlab::V3Length({3, 4, 0}), 5.0f, 1e-6f));

    // 투영: b 방향 성분만 남고, 두 번 적용해도 같다(멱등).
    const Vector3 a{3, 4, 5};
    const Vector3 b{0, 2, 0};
    const Vector3 proj = mathlab::V3Project(a, b);
    CHECK(Near(proj.x, 0.0f, 1e-6f) && Near(proj.y, 4.0f, 1e-6f) &&
          Near(proj.z, 0.0f, 1e-6f));
    const Vector3 proj2 = mathlab::V3Project(proj, b);
    CHECK(Near(proj2.y, proj.y, 1e-6f));
    // 잔차는 b 와 수직.
    CHECK(std::fabs(mathlab::V3Dot(mathlab::V3Sub(a, proj), b)) < 1e-5f);

    // 반사: 길이 보존 + 법선 성분 부호 반전.
    const Vector3 nrm = mathlab::V3Normalize({0, 1, 0});
    const Vector3 d{1, -1, 0};
    const Vector3 r = mathlab::V3Reflect(d, nrm);
    CHECK(Near(r.x, 1.0f, 1e-6f) && Near(r.y, 1.0f, 1e-6f));
    CHECK(Near(mathlab::V3Length(r), mathlab::V3Length(d), 1e-5f));
    CHECK(Near(mathlab::V3Dot(r, nrm), -mathlab::V3Dot(d, nrm), 1e-6f));
}

// ==================== matrices (math part03 / part06) ====================

bool NearMat(const Matrix& a, const Matrix& b, float eps) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!Near(mathlab::MatAt(a, r, c), mathlab::MatAt(b, r, c), eps)) {
                return false;
            }
        }
    }
    return true;
}

void TestMatrix() {
    const Quaternion q =
        mathlab::QuatFromAxisAngle({0.3f, -0.5f, 0.8f}, 1.1f);
    const Vector3 t{3.0f, -2.0f, 5.0f};
    const Vector3 s{2.0f, 0.5f, 1.5f};
    const Matrix trs = mathlab::MatComposeTRS(t, q, s);

    // (a) M·M⁻¹ = I (양쪽 모두).
    const Matrix inv = mathlab::MatInverse(trs);
    CHECK(NearMat(mathlab::MatMul(trs, inv), mathlab::MatIdentity(), 1e-5f));
    CHECK(NearMat(mathlab::MatMul(inv, trs), mathlab::MatIdentity(), 1e-5f));
    // 항등원과 결합법칙.
    CHECK(NearMat(mathlab::MatMul(trs, mathlab::MatIdentity()), trs, 1e-6f));
    const Matrix sc = mathlab::MatScale({1.5f, -2.0f, 0.25f});
    const Matrix tr = mathlab::MatTranslate({-1.0f, 4.0f, 2.0f});
    CHECK(NearMat(mathlab::MatMul(mathlab::MatMul(trs, sc), tr),
                  mathlab::MatMul(trs, mathlab::MatMul(sc, tr)), 1e-4f));

    // (b) 직교행렬: Rᵀ = R⁻¹, det(R) = 1.
    const Matrix R = mathlab::MatRotate(q);
    CHECK(NearMat(mathlab::MatTranspose(R), mathlab::MatInverse(R), 1e-5f));
    CHECK(NearMat(mathlab::MatMul(R, mathlab::MatTranspose(R)),
                  mathlab::MatIdentity(), 1e-5f));
    CHECK(Near(mathlab::MatDeterminant(R), 1.0f, 1e-4f));
    // 아핀 TRS 의 행렬식은 스케일 곱.
    CHECK(Near(mathlab::MatDeterminant(trs), s.x * s.y * s.z, 1e-3f));
    // 좌표계 반전(스케일 음수)은 행렬식 부호를 뒤집는다 — 핸드니스 판정.
    CHECK(mathlab::MatDeterminant(mathlab::MatScale({1, 1, -1})) < 0.0f);

    // (c) QuatToMatrix(q)·v == QuatRotateVec(q, v).
    Lcg rng(8080u);
    for (int i = 0; i < 100; ++i) {
        const Vector3 v{rng.Range(-3.0f, 3.0f), rng.Range(-3.0f, 3.0f),
                        rng.Range(-3.0f, 3.0f)};
        const Vector3 byMat = mathlab::MatTransformVector(R, v);
        const Vector3 byQuat = mathlab::QuatRotateVec(mathlab::QuatNormalize(q), v);
        CHECK(Near(byMat.x, byQuat.x, 1e-4f));
        CHECK(Near(byMat.y, byQuat.y, 1e-4f));
        CHECK(Near(byMat.z, byQuat.z, 1e-4f));
    }
    // 회전은 길이를 보존한다.
    CHECK(Near(mathlab::V3Length(mathlab::MatTransformVector(R, {1, 2, 3})),
               mathlab::V3Length({1, 2, 3}), 1e-4f));

    // TRS 의 의미: 스케일 → 회전 → 평행이동 순.
    const Vector3 p{1.0f, 2.0f, 3.0f};
    const Vector3 expect = mathlab::V3Add(
        t, mathlab::QuatRotateVec(mathlab::QuatNormalize(q),
                                  {p.x * s.x, p.y * s.y, p.z * s.z}));
    const Vector3 got = mathlab::MatTransformPoint(trs, p);
    CHECK(Near(got.x, expect.x, 1e-4f) && Near(got.y, expect.y, 1e-4f) &&
          Near(got.z, expect.z, 1e-4f));
    // 방향 벡터에는 평행이동이 붙지 않는다.
    const Vector3 dir = mathlab::MatTransformVector(trs, p);
    CHECK(!Near(dir.x, got.x, 1e-3f) || !Near(dir.y, got.y, 1e-3f) ||
          !Near(dir.z, got.z, 1e-3f));
    CHECK(Near(mathlab::V3Length(mathlab::V3Sub(got, dir)),
               mathlab::V3Length(t), 1e-4f));

    // (d) 합성은 비가환 — TRS 와 SRT 는 다른 변환이다.
    const Matrix srt = mathlab::MatMul(
        mathlab::MatScale(s),
        mathlab::MatMul(mathlab::MatRotate(q), mathlab::MatTranslate(t)));
    CHECK(!NearMat(trs, srt, 1e-3f));
    CHECK(mathlab::V3Length(mathlab::V3Sub(mathlab::MatTransformPoint(trs, p),
                                           mathlab::MatTransformPoint(srt, p)))
          > 1e-2f);

    // (e) 전치 왕복은 항등, 그리고 (AB)ᵀ = BᵀAᵀ.
    CHECK(NearMat(mathlab::MatTranspose(mathlab::MatTranspose(trs)), trs,
                  1e-6f));
    CHECK(NearMat(mathlab::MatTranspose(mathlab::MatMul(trs, sc)),
                  mathlab::MatMul(mathlab::MatTranspose(sc),
                                  mathlab::MatTranspose(trs)),
                  1e-4f));
    // 열 우선 규약 확인: 평행이동 항은 네 번째 열(m12..m14)이다.
    CHECK(Near(mathlab::MatAt(mathlab::MatTranslate(t), 0, 3), t.x, 1e-6f));
    CHECK(Near(mathlab::MatAt(mathlab::MatTranslate(t), 1, 3), t.y, 1e-6f));
    CHECK(Near(mathlab::MatAt(mathlab::MatTranslate(t), 2, 3), t.z, 1e-6f));
    CHECK(Near(mathlab::MatAt(mathlab::MatTranslate(t), 3, 0), 0.0f, 1e-6f));

    // 특이행렬은 역행렬을 만들 수 없다.
    Matrix dummy = mathlab::MatIdentity();
    CHECK(!mathlab::MatInvert(mathlab::MatScale({1.0f, 0.0f, 1.0f}), dummy));
}

}  // namespace

int main() {
    TestQuaternion();
    TestQuaternionSlerpUniformSpeed();
    TestQuaternionGimbalLock();
    TestCurves();
    TestCollision();
    TestSdf();
    TestBvh();
    TestNoise();
    TestDepth();
    TestKahan();
    TestVectorAlgebra();
    TestMatrix();
    if (failures != 0) {
        std::cerr << failures << " math core test(s) failed\n";
        return 1;
    }
    std::cout << "math core tests passed\n";
    return 0;
}
