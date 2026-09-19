// math_lab 코어 검증. GUI 없이 algo/ 헤더만 컴파일해 돌린다.
//
// 이 파일의 규칙: "그 알고리즘만이 만족하는 성질" 을 검사한다.
// 퇴화 입력(t=0, k=0, octaves=0, a==b)만 보는 CHECK 는 알고리즘을 우회하므로
// 반드시 비퇴화 케이스를 짝으로 둔다. 독립 오라클(brute force / 다른 알고리즘)
// 과의 대조는 제품 함수를 다시 호출하지 않고 이 파일 안에서 따로 구현한다.
//
// 두 층이 있다. 위쪽은 *고정 사례* — 알려진 중요 입력을 못 박는다. 아래쪽은
// *property 층* — 고정 seed 에서 생성한 수천 개 입력이 지켜야 하는 관계를 건다
// (`property.h` 의 하네스). 두 층은 서로를 보완한다: 고정 사례는 회귀를 잡고,
// property 는 표를 만들 수 없는 입력 공간을 덮는다.
//
// 실행 단위: 파일 끝의 kCases 표가 각 검증을 그것을 들여온 step 에 묶는다.
// 인자 없이 부르면 전부 돌고, `math_core_tests step05` 처럼 step 키를 주면
// 그 step 이 추가한 것만 돈다. CMake 의 add_test 가 step 마다 하나씩 걸려 있다.
//
// property 층 전용 플래그.
//   --properties     property 사례만 돈다 (ctest 의 math_property_layer).
//   --scale=N        표본 수를 N 배로 (야간 확장 캠페인).
//   --seed=N         master seed 교체. 반례 보고가 이 값을 그대로 찍는다.
//   --only=NAME      이름이 NAME 을 포함하는 property 만.
//   --sample=I       그 표본 하나만 (반례 재현 모드 — 비자명성 검사는 건너뛴다).

#include "algo/bvh2d.h"
#include "algo/collision2d.h"
#include "algo/curves.h"
#include "algo/depth.h"
#include "algo/kahan.h"
#include "algo/mat.h"
#include "algo/noise.h"
#include "algo/quaternion.h"
#include "algo/sdf2d.h"
#include "ui/scene_chrome.h"

#include "property.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks   = 0;   // 실행된 CHECK 수 — step 별 출력에 그대로 찍힌다.

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++checks;                                                             \
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

// ==================== layout (골격의 화면 분할) ====================

// ComputeLayout 은 raylib 호출이 없는 순수 산술이라 창 없이 그대로 검사된다.
// 계약은 하나다 — 탭 바 / (캔버스 + 패널) / 푸터가 화면을 빈틈도 겹침도 없이
// 나눠 갖고, chrome 높이는 고정이라 화면이 커지면 캔버스만 커진다.
void TestLayout() {
    constexpr int kW = 1280, kH = 720, kPanel = 300;
    const mathlab::LayoutRects L = mathlab::ComputeLayout(kW, kH, kPanel);

    // snippet-begin: step01-layout-partition
    // 세로: 탭 바 아래가 곧 캔버스, 캔버스 아래가 곧 푸터, 푸터 끝이 화면 끝.
    CHECK(Near(L.tabBar.y + L.tabBar.height, L.canvas.y));
    CHECK(Near(L.canvas.y + L.canvas.height, L.footer.y));
    CHECK(Near(L.footer.y + L.footer.height, (float)kH));
    // 가로: 캔버스 + 패널 = 화면 폭이고 패널이 오른쪽 끝에 붙는다.
    CHECK(Near(L.canvas.width + L.panel.width, (float)kW));
    CHECK(Near(L.canvas.x + L.canvas.width, L.panel.x));
    CHECK(Near(L.panel.width, (float)kPanel));
    // snippet-end: step01-layout-partition

    // 패널은 캔버스와 같은 세로 구간을 쓴다.
    CHECK(Near(L.panel.y, L.canvas.y));
    CHECK(Near(L.panel.height, L.canvas.height));

    // chrome 높이는 화면 크기와 무관한 상수다 — 화면을 키우면 캔버스만 커진다.
    const mathlab::LayoutRects tall = mathlab::ComputeLayout(kW, kH + 400, kPanel);
    CHECK(Near(tall.tabBar.height, L.tabBar.height));
    CHECK(Near(tall.footer.height, L.footer.height));
    CHECK(Near(tall.canvas.height, L.canvas.height + 400.0f));
    CHECK(Near(tall.footer.y, L.footer.y + 400.0f));

    // 패널을 넓히면 캔버스가 정확히 그만큼 좁아진다 (총합 보존).
    const mathlab::LayoutRects wide = mathlab::ComputeLayout(kW, kH, kPanel + 120);
    CHECK(Near(wide.canvas.width, L.canvas.width - 120.0f));
    CHECK(Near(wide.canvas.width + wide.panel.width, (float)kW));
    CHECK(Near(wide.canvas.height, L.canvas.height));

    // 모든 영역이 양수 크기이고 탭 바는 화면 전폭을 쓴다.
    CHECK(L.tabBar.width > 0.0f && L.canvas.width > 0.0f && L.panel.width > 0.0f);
    CHECK(L.canvas.height > 0.0f && L.footer.height > 0.0f);
    CHECK(Near(L.tabBar.width, (float)kW) && Near(L.footer.width, (float)kW));
    CHECK(Near(L.tabBar.x, 0.0f) && Near(L.canvas.x, 0.0f));
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

    // snippet-begin: step02-slerp-uniform-speed
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
    // snippet-end: step02-slerp-uniform-speed

    // 끝점 보존 + 중간점이 단위 노름.
    CHECK(Near(QuatAbsDot(mathlab::QuatSlerp(a, b, 0.0f), a), 1.0f, 1e-6f));
    CHECK(Near(QuatAbsDot(mathlab::QuatSlerp(a, b, 1.0f), b), 1.0f, 1e-6f));
    CHECK(Near(mathlab::QuatNorm(mathlab::QuatSlerp(a, b, 0.37f)), 1.0f, 1e-5f));
    // Nlerp 도 정규화된 결과를 낸다 (M03: 최종 정규화 제거).
    CHECK(Near(mathlab::QuatNorm(mathlab::QuatNlerp(a, b, 0.5f)), 1.0f, 1e-5f));
}

// 짐벌락의 실제 불변식: 가운데 축(yaw)이 +90° 면 회전이 (pitch-roll) 에만
// 의존해 바깥 두 축이 한 자유도로 축약된다 — 둘을 같은 양만큼 밀어도 회전이
// 변하지 않는다. (-90° 는 (pitch+roll) 의존이라 반대 부호로 밀어야 불변이다.)
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

    // snippet-begin: step02-gimbal-lock
    const float locked = worstMismatch(halfPi);
    const float free   = worstMismatch(0.0f);
    if (!(locked < 1e-5f) || !(free > 1e-3f)) {
        std::cerr << "  gimbal worst(1-|dot|): yaw=pi/2 -> " << locked
                  << ", yaw=0 -> " << free << "\n";
    }
    CHECK(locked < 1e-5f);
    CHECK(free > 1e-3f);
    // snippet-end: step02-gimbal-lock
}

void TestGjkIterationLimit() {
    const mathlab::OBB2D a{{0, 0}, 1, 1, 0};
    const mathlab::OBB2D b{{0.5f, 0.25f}, 1, 1, 0};
    CHECK(mathlab::OBBOBBSat2D(a, b).intersect);
    for (int budget : {0, 1}) {
        const auto trace = mathlab::GJK2DTraced(a, b, budget);
        CHECK(!trace.empty());
        CHECK(!trace.back().done);
    }
    const auto converged = mathlab::GJK2DTraced(a, b);
    CHECK(converged.back().done && converged.back().intersect);
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
    // snippet-begin: step03-arc-length-uniform
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
    // snippet-end: step03-arc-length-uniform

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
        // snippet-begin: step05-sat-gjk-agree
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
    // snippet-end: step05-sat-gjk-agree
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
    // snippet-begin: step07-smin-blend-band
    CHECK(mathlab::sdf::SminPoly(1.0f, 1.2f, 1.0f) < 1.0f);
    CHECK(Near(mathlab::sdf::SminPoly(1.0f, 1.2f, 1.0f), 1.0f - 0.16f, 1e-5f));
    CHECK(Near(mathlab::sdf::SminPoly(1.0f, 5.0f, 1.0f), 1.0f, 1e-6f));
    CHECK(Near(mathlab::sdf::SminPoly(2.0f, 2.0f, 1.0f), 2.0f - 0.25f, 1e-5f));
    // k → 0 이면 min 으로 수렴한다.
    CHECK(Near(mathlab::sdf::SminPoly(1.0f, 1.2f, 1e-3f), 1.0f, 1e-3f));
    // snippet-end: step07-smin-blend-band
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

void TestBvhBoundaryContract() {
    const mathlab::AABB2D box{{1, 1}, {3, 3}};
    float tn = 0.0f, tf = 0.0f;
    CHECK(mathlab::RayAABB2D({0, 2}, {1e-9f, 0}, box, tn, tf));
    CHECK(NearRel(tn, 1e9) && NearRel(tf, 3e9));
    CHECK(mathlab::RayAABB2D({4, 2}, {-1e-9f, -0.0f}, box, tn, tf));
    CHECK(NearRel(tn, 1e9) && NearRel(tf, 3e9));
    CHECK(mathlab::RayAABB2D({1, 2}, {0, 1}, box, tn, tf));
    CHECK(!mathlab::RayAABB2D({0, 2}, {0, 1}, box, tn, tf));

    const std::vector<mathlab::AABB2D> boxes{box};
    mathlab::BVH2D bvh;
    bvh.Build(boxes);
    const auto endpoint = bvh.Trace(boxes, {0, 2}, {1, 0}, 1.0f);
    CHECK(endpoint.hit && endpoint.hitObjIdx == 0 && Near(endpoint.hitT, 1.0f));
    CHECK(!bvh.Trace(boxes, {0, 2}, {1, 0}, 0.5f).hit);
    CHECK(bvh.Trace(boxes, {2, 2}, {1, 0}, 0.0f).hit);
    CHECK(!bvh.Trace(boxes, {2, 2}, {1, 0}, -1.0f).hit);
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
    // snippet-begin: step06-bvh-vs-brute-force
    CHECK(mismatch == 0);
    CHECK(hitCount > kRays / 4);   // 대부분 빗나가면 위 대조가 자명해진다
    // 가지치기가 실제로 동작하면 광선당 방문 노드는 전체 노드 수보다 훨씬 적다.
    const double avgVisited = (double)visitedTotal / (double)kRays;
    if (!(avgVisited < 0.5 * (double)big.Nodes().size())) {
        std::cerr << "  bvh avg visited = " << avgVisited << " of "
                  << big.Nodes().size() << " nodes\n";
    }
    CHECK(avgVisited < 0.5 * (double)big.Nodes().size());
    // snippet-end: step06-bvh-vs-brute-force

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
        // snippet-begin: step04-perlin-quintic-fade
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
        // snippet-end: step04-perlin-quintic-fade
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
    // snippet-begin: step08-depth-reverse
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
    // snippet-end: step08-depth-reverse
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
    // snippet-begin: step08-kahan-absorption
    mathlab::KahanAccumulator acc(1.0f);
    acc.Add(1e-8f);
    CHECK(Near(acc.Sum(), 1.0f, 1e-9f));      // float 1.0 에 흡수되고
    CHECK(std::fabs(acc.Compensation()) > 1e-9f);  // 보상항에 남는다
    for (int i = 0; i < 1000; ++i) acc.Add(1e-8f);
    CHECK(acc.Sum() > 1.0f);   // 모이면 되살아난다
    // snippet-end: step08-kahan-absorption
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
    // snippet-begin: step08-matrix-identities
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
    // snippet-end: step08-matrix-identities
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

// ==================== property 층 ====================
//
// 위의 고정 사례가 "이 입력에서 이 값" 을 못 박는다면, 이 층은 "생성된 *모든*
// 입력에서 이 관계" 를 못 박는다. 표본은 master seed 에서 결정론적으로 나오고,
// 반례가 나오면 축소한 파라미터와 재현 명령 한 줄이 stderr 로 나온다.
//
// oracle 은 세 가지만 쓴다.
//   (a) 수학적 property — 단위 노름, 결합성, 이중 덮개, 항등식.
//   (b) 독립 구현 대조(differential) — SAT ⇔ GJK, 행렬 ⇔ 쿼터니언 샌드위치,
//       제품의 Corners()/SATEvaluateAxis 를 쓰지 않는 이 파일 안의 double 투영.
//   (c) 입력 변환 전후 관계(metamorphic) — 강체 변환·균등 스케일에서 판정 불변.
//
// 계약(허용 오차)은 테스트를 통과시키려고 키운 값이 아니라 부동소수 반올림
// 횟수에서 유도한 값이다. 각 property 의 contract 문자열에 근거를 적는다.

// ---- 공용 파라미터 도우미 ----

Quaternion QuatScaleQ(Quaternion q, float k) {
    return {q.x * k, q.y * k, q.z * k, q.w * k};
}

Quaternion QuatNeg(Quaternion q) { return {-q.x, -q.y, -q.z, -q.w}; }

float QuatDotSigned(Quaternion a, Quaternion b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

double QuatMaxDiff(Quaternion a, Quaternion b) {
    double d = std::fabs((double)a.x - b.x);
    d = std::max(d, std::fabs((double)a.y - b.y));
    d = std::max(d, std::fabs((double)a.z - b.z));
    return std::max(d, std::fabs((double)a.w - b.w));
}

double MatMaxDiff(const Matrix& a, const Matrix& b) {
    float A[16], B[16];
    mathlab::MatToArray(a, A);
    mathlab::MatToArray(b, B);
    double worst = 0.0;
    for (int i = 0; i < 16; ++i) worst = std::max(worst, std::fabs((double)A[i] - B[i]));
    return worst;
}

double MatMaxAbs(const Matrix& m) {
    float A[16];
    mathlab::MatToArray(m, A);
    double worst = 0.0;
    for (int i = 0; i < 16; ++i) worst = std::max(worst, std::fabs((double)A[i]));
    return worst;
}

bool MatBitEqual(const Matrix& a, const Matrix& b) {
    float A[16], B[16];
    mathlab::MatToArray(a, A);
    mathlab::MatToArray(b, B);
    for (int i = 0; i < 16; ++i) {
        if (!(A[i] == B[i])) return false;
    }
    return true;
}

// float 의 단위 반올림 (2^-24). 모든 허용 오차 상계가 이 값에서 유도된다.
constexpr double kUnitRoundoff = 5.9604645e-8;

// 생성기의 구성적 보정 — 스케일이 0 을 지나가지 않게 만든다. 표본을 버리는
// rejection 이 아니라 값을 domain 안으로 옮기는 것이라, shrink 후보도 언제나
// 유효하다(사전조건이 축소 중에 깨지지 않는다).
float RepairScale(float s) {
    constexpr float kMin = 0.1f;
    if (s >= 0.0f) return (s < kMin) ? kMin : s;
    return (s > -kMin) ? -kMin : s;
}

const prop::ParamSpec kAxisAngleSpec[4] = {
    {"axis.x", -1.0f, 1.0f, 0.0f},
    {"axis.y", -1.0f, 1.0f, 0.0f},
    {"axis.z", -1.0f, 1.0f, 1.0f},
    {"angle",  -6.5f, 6.5f, 0.0f},
};

// ---- step02: 쿼터니언 ----

// (1) 정규화 불변. 축-각 생성기는 언제나 단위 쿼터니언을 내고, slerp/nlerp 는
//     비단위 입력을 받아도 단위 결과를 낸다. 단위 쿼터니언의 샌드위치 회전은
//     벡터 길이를 보존한다.
const prop::ParamSpec kQuatNormParams[] = {
    {"axisA.x", -1.0f,  1.0f, 0.0f}, {"axisA.y", -1.0f,  1.0f, 0.0f},
    {"axisA.z", -1.0f,  1.0f, 1.0f}, {"angleA",  -6.5f,  6.5f, 0.0f},
    {"axisB.x", -1.0f,  1.0f, 0.0f}, {"axisB.y", -1.0f,  1.0f, 0.0f},
    {"axisB.z", -1.0f,  1.0f, 1.0f}, {"angleB",  -6.5f,  6.5f, 0.0f},
    {"scaleA",   0.05f, 20.0f, 1.0f}, {"scaleB",  0.05f, 20.0f, 1.0f},
    {"t",        0.0f,  1.0f, 0.5f},
    {"v.x",     -5.0f,  5.0f, 1.0f}, {"v.y", -5.0f, 5.0f, 0.0f},
    {"v.z",     -5.0f,  5.0f, 0.0f},
};

bool PropQuatNormPreserved(const float* p, prop::Observation& obs) {
    const Quaternion qa = mathlab::QuatFromAxisAngle({p[0], p[1], p[2]}, p[3]);
    const Quaternion qb = mathlab::QuatFromAxisAngle({p[4], p[5], p[6]}, p[7]);
    const Quaternion sa = QuatScaleQ(qa, p[8]);
    const Quaternion sb = QuatScaleQ(qb, p[9]);
    const float      t  = p[10];
    const Vector3    v{p[11], p[12], p[13]};
    const float      vLen = mathlab::V3Length(v);

    bool ok = true;
    if (!obs.Bound("|norm(axis-angle) - 1|",
                   std::fabs((double)mathlab::QuatNorm(qa) - 1.0), 8.0 * kUnitRoundoff)) ok = false;
    if (!obs.Bound("|norm(normalize(k*q)) - 1|",
                   std::fabs((double)mathlab::QuatNorm(mathlab::QuatNormalize(sa)) - 1.0),
                   8.0 * kUnitRoundoff)) ok = false;
    if (!obs.Bound("|norm(slerp) - 1|",
                   std::fabs((double)mathlab::QuatNorm(mathlab::QuatSlerp(sa, sb, t)) - 1.0),
                   16.0 * kUnitRoundoff)) ok = false;
    if (!obs.Bound("|norm(nlerp) - 1|",
                   std::fabs((double)mathlab::QuatNorm(mathlab::QuatNlerp(sa, sb, t)) - 1.0),
                   16.0 * kUnitRoundoff)) ok = false;

    const Vector3 rotated = mathlab::QuatRotateVec(qa, v);
    if (!obs.Bound("|len(q v q*) - len(v)| / len(v)",
                   std::fabs((double)mathlab::V3Length(rotated) - vLen) /
                       std::max(1e-3, (double)vLen),
                   64.0 * kUnitRoundoff)) ok = false;

    obs.interesting = std::fabs(p[3]) > 0.1f && vLen > 0.5f &&
                      (std::fabs(p[0]) + std::fabs(p[1]) + std::fabs(p[2])) > 1e-3f;
    return ok;
}

// (2) Hamilton 곱의 결합성. 대조군은 *비가환성* 이다 — 곱이 상수나 가환 연산으로
//     바뀌면 결합성은 자명하게 참이 되므로, 비퇴화 표본의 정의에 비가환을 넣는다.
const prop::ParamSpec kQuatAssocParams[] = {
    {"axis1.x", -1.0f, 1.0f, 0.0f}, {"axis1.y", -1.0f, 1.0f, 0.0f},
    {"axis1.z", -1.0f, 1.0f, 1.0f}, {"angle1",  -6.5f, 6.5f, 0.0f},
    {"axis2.x", -1.0f, 1.0f, 0.0f}, {"axis2.y", -1.0f, 1.0f, 0.0f},
    {"axis2.z", -1.0f, 1.0f, 1.0f}, {"angle2",  -6.5f, 6.5f, 0.0f},
    {"axis3.x", -1.0f, 1.0f, 0.0f}, {"axis3.y", -1.0f, 1.0f, 0.0f},
    {"axis3.z", -1.0f, 1.0f, 1.0f}, {"angle3",  -6.5f, 6.5f, 0.0f},
};

bool PropQuatComposeAssoc(const float* p, prop::Observation& obs) {
    const Quaternion q1 = mathlab::QuatFromAxisAngle({p[0], p[1], p[2]},   p[3]);
    const Quaternion q2 = mathlab::QuatFromAxisAngle({p[4], p[5], p[6]},   p[7]);
    const Quaternion q3 = mathlab::QuatFromAxisAngle({p[8], p[9], p[10]},  p[11]);

    const Quaternion left  = mathlab::QuatMul(mathlab::QuatMul(q1, q2), q3);
    const Quaternion right = mathlab::QuatMul(q1, mathlab::QuatMul(q2, q3));

    bool ok = true;
    // 성분 하나는 4 곱 + 3 합이고 입력이 단위라 각 항의 크기가 1 이하다.
    // 두 번 겹치면 반올림이 성분당 16 회 이하 → 16u. 여유 2 배로 32u 를 쓴다.
    if (!obs.Bound("max|(q1 q2) q3 - q1 (q2 q3)|", QuatMaxDiff(left, right),
                   32.0 * kUnitRoundoff)) ok = false;
    if (!obs.Bound("1 - |dot(left, right)|",
                   1.0 - (double)QuatAbsDot(left, right), 64.0 * kUnitRoundoff)) ok = false;

    obs.interesting =
        QuatMaxDiff(mathlab::QuatMul(q1, q2), mathlab::QuatMul(q2, q1)) > 1e-3;
    return ok;
}

// (3) 이중 덮개. q 와 -q 는 같은 회전이다. 샌드위치와 행렬 변환은 성분이 부호에
//     대해 짝수 차수라 *비트 단위로* 같아야 하고, slerp 은 짧은 호 뒤집기 덕에
//     끝점 부호에 불변이다. |dot| 이 0 근처면 뒤집을 방향이 정해지지 않으므로
//     사전조건에서 뺀다.
const prop::ParamSpec kQuatCoverParams[] = {
    {"axisA.x", -1.0f, 1.0f, 0.0f}, {"axisA.y", -1.0f, 1.0f, 0.0f},
    {"axisA.z", -1.0f, 1.0f, 1.0f}, {"angleA",  -6.5f, 6.5f, 0.0f},
    {"axisB.x", -1.0f, 1.0f, 0.0f}, {"axisB.y", -1.0f, 1.0f, 0.0f},
    {"axisB.z", -1.0f, 1.0f, 1.0f}, {"angleB",  -6.5f, 6.5f, 0.0f},
    {"t",        0.0f, 1.0f, 0.5f},
    {"v.x",     -5.0f, 5.0f, 1.0f}, {"v.y", -5.0f, 5.0f, 0.0f},
    {"v.z",     -5.0f, 5.0f, 0.0f},
};

bool PropQuatDoubleCover(const float* p, prop::Observation& obs) {
    const Quaternion qa = mathlab::QuatFromAxisAngle({p[0], p[1], p[2]}, p[3]);
    const Quaternion qb = mathlab::QuatFromAxisAngle({p[4], p[5], p[6]}, p[7]);
    const float      t  = p[8];
    const Vector3    v{p[9], p[10], p[11]};

    bool ok = true;
    // snippet-begin: step02-property-double-cover
    // 부호는 샌드위치 안에서 정확히 상쇄된다 — tolerance 가 아니라 == 로 잡는다.
    const Vector3 r1 = mathlab::QuatRotateVec(qa, v);
    const Vector3 r2 = mathlab::QuatRotateVec(QuatNeg(qa), v);
    const bool sameVec = (r1.x == r2.x) && (r1.y == r2.y) && (r1.z == r2.z);
    if (!obs.Bound("q v q* vs (-q) v (-q)* (exact)", sameVec ? 0.0 : 1.0, 0.0)) ok = false;

    const bool sameMat = MatBitEqual(mathlab::QuatToMatrix(qa),
                                     mathlab::QuatToMatrix(QuatNeg(qa)));
    if (!obs.Bound("QuatToMatrix(q) vs QuatToMatrix(-q) (exact)", sameMat ? 0.0 : 1.0, 0.0)) {
        ok = false;
    }

    const float d = QuatAbsDot(qa, qb);
    if (d < 1e-4f) {   // 사전조건: 두 끝점이 직교에 너무 가까우면 판정하지 않는다.
        obs.skipped = true;
        return ok;
    }
    const Quaternion s1 = mathlab::QuatSlerp(qa, qb, t);
    const Quaternion s2 = mathlab::QuatSlerp(qa, QuatNeg(qb), t);
    const Quaternion s3 = mathlab::QuatSlerp(QuatNeg(qa), qb, t);
    if (!obs.Bound("1 - |dot(slerp(a,b), slerp(a,-b))|",
                   1.0 - (double)QuatAbsDot(s1, s2), 32.0 * kUnitRoundoff)) ok = false;
    if (!obs.Bound("1 - |dot(slerp(a,b), slerp(-a,b))|",
                   1.0 - (double)QuatAbsDot(s1, s3), 32.0 * kUnitRoundoff)) ok = false;
    // snippet-end: step02-property-double-cover

    obs.interesting = std::fabs(p[3]) > 0.1f && mathlab::V3Length(v) > 0.5f && d < 0.999f;
    return ok;
}

// (4) 등각속도. 각도를 acos 로 재면 dot 이 1 근처라 조건이 나빠지므로 *현 길이*
//     로 잰다 — 등속이면 모든 인접 표본의 현 길이가 같다. 생성기는 a 에서
//     정확히 arc 라디안 떨어진 b 를 *구성* 한다(의존 생성기).
const prop::ParamSpec kQuatSlerpParams[] = {
    {"base.x", -1.0f, 1.0f, 0.0f}, {"base.y", -1.0f, 1.0f, 0.0f},
    {"base.z", -1.0f, 1.0f, 1.0f}, {"baseAngle", -6.5f, 6.5f, 0.0f},
    {"arc.x",  -1.0f, 1.0f, 0.0f}, {"arc.y",  -1.0f, 1.0f, 0.0f},
    {"arc.z",  -1.0f, 1.0f, 1.0f}, {"arcAngle", 0.5f, 2.9f, 1.0f},
};

// 등속은 *단위 구 위* 의 성질이다. 그래서 표본을 구 위로 올린 뒤 현 길이를 잰다
// — 구 위에서 현이 일정하면 호도 일정하다. 이 정규화가 없으면 "직선(현)을 따라
// 등간격으로 가는" 구현도 통과한다(실제로 변이 MP04 가 그 구멍으로 살아남았다).
// 정규화는 제품의 QuatNormalize 를 부르지 않고 여기서 직접 한다 — 측정 자가 검사
// 대상에 의존하면 오라클이 아니다. 부산물로 원본 표본의 노름 이탈도 돌려준다.
float QuatChordRatio(Quaternion (*interp)(Quaternion, Quaternion, float),
                     Quaternion a, Quaternion b, int steps, float& worstNormError) {
    auto unit = [](Quaternion q, float& normError) {
        const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        normError = std::max(normError, std::fabs(n - 1.0f));
        if (n < 1e-12f) return Quaternion{0, 0, 0, 1};
        const float inv = 1.0f / n;
        return Quaternion{q.x * inv, q.y * inv, q.z * inv, q.w * inv};
    };

    worstNormError = 0.0f;
    float maxChord = 0.0f;
    float minChord = 1e30f;
    Quaternion prev = unit(interp(a, b, 0.0f), worstNormError);
    for (int i = 1; i <= steps; ++i) {
        const float t = (float)i / (float)steps;
        Quaternion cur = unit(interp(a, b, t), worstNormError);
        if (QuatDotSigned(prev, cur) < 0.0f) cur = QuatNeg(cur);
        const float dx = cur.x - prev.x, dy = cur.y - prev.y;
        const float dz = cur.z - prev.z, dw = cur.w - prev.w;
        const float chord = std::sqrt(dx * dx + dy * dy + dz * dz + dw * dw);
        maxChord = std::max(maxChord, chord);
        minChord = std::min(minChord, chord);
        prev = cur;
    }
    return (minChord > 1e-9f) ? (maxChord / minChord) : 1e30f;
}

bool PropQuatSlerpUniform(const float* p, prop::Observation& obs) {
    const Quaternion a = mathlab::QuatFromAxisAngle({p[0], p[1], p[2]}, p[3]);
    Vector3 arcAxis{p[4], p[5], p[6]};
    if (mathlab::V3Length(arcAxis) < 1e-3f) arcAxis = {0.0f, 0.0f, 1.0f};
    const float arc = p[7];
    const Quaternion b = mathlab::QuatMul(a, mathlab::QuatFromAxisAngle(arcAxis, arc));

    constexpr int kSteps = 32;
    float slerpNormError = 0.0f;
    const float slerpRatio =
        QuatChordRatio(&mathlab::QuatSlerp, a, b, kSteps, slerpNormError);

    bool ok = true;
    // snippet-begin: step02-property-slerp-uniform
    // 등속의 사전조건 — 결과가 애초에 단위 구 위에 있어야 "호를 등속으로 지난다"
    // 는 말이 성립한다.
    if (!obs.Bound("max|norm(slerp sample) - 1| over the arc", (double)slerpNormError,
                   32.0 * kUnitRoundoff)) ok = false;
    // 현 = 2 sin(dTheta/2). 등속이면 모든 구간의 dTheta 가 같아 비가 1 이다.
    // 회전각 0.5 rad (쿼터니언 호 0.25 rad) 를 32 등분하면 현이 0.0078 이고 성분 오차는 1e-7 대라 여유가
    // 세 자릿수 있다. 계약은 "비 - 1 <= 0.02".
    if (!obs.Bound("slerp chord max/min - 1", (double)slerpRatio - 1.0, 0.02)) ok = false;
    if (!obs.Bound("1 - |dot(slerp(a,b,0), a)|",
                   1.0 - (double)QuatAbsDot(mathlab::QuatSlerp(a, b, 0.0f), a),
                   32.0 * kUnitRoundoff)) ok = false;
    if (!obs.Bound("1 - |dot(slerp(a,b,1), b)|",
                   1.0 - (double)QuatAbsDot(mathlab::QuatSlerp(a, b, 1.0f), b),
                   32.0 * kUnitRoundoff)) ok = false;

    // 대조군 — 같은 호에서 nlerp 는 균일하지 않아야 한다. 이게 없으면 상한이
    // 자명해질 수 있다(slerp 를 nlerp 로 바꿔치기해도 둘이 같이 움직인다).
    if (arc >= 1.5f) {
        float nlerpNormError = 0.0f;
        const float nlerpRatio =
            QuatChordRatio(&mathlab::QuatNlerp, a, b, kSteps, nlerpNormError);
        if (!obs.AtLeast("nlerp chord max/min - 1 (control floor)",
                         (double)nlerpRatio - 1.0, 0.05)) {
            ok = false;
        }
        obs.interesting = true;
    }
    // snippet-end: step02-property-slerp-uniform
    return ok;
}

// ---- step04: 노이즈 ----

// Perlin 의 범위 상계를 *독립으로* 계산한다. Grad2 는 축 정렬 단위 벡터 4 종만
// 내므로 한 셀의 네 모서리 그라디언트 조합은 4^4 = 256 가지뿐이다. 그 전부를
// 셀 안 격자에서 quintic 블렌드해 최대 |값| 을 구하면, 해시가 무엇을 내든
// Perlin2D 의 값은 그 안에 있다. 헤더 주석의 실측 [-0.535, 0.535] 을 확인하는
// 오라클이자 property 의 상계다.
double PerlinScanBound() {
    static double cached = -1.0;
    if (cached >= 0.0) return cached;
    // snippet-begin: step04-property-perlin-scan
    const float gx[4] = {1.0f, -1.0f, 0.0f, 0.0f};
    const float gy[4] = {0.0f, 0.0f, 1.0f, -1.0f};
    constexpr int kGrid = 96;
    double best = 0.0;
    for (int a = 0; a < 4; ++a) {
        for (int b = 0; b < 4; ++b) {
            for (int c = 0; c < 4; ++c) {
                for (int d = 0; d < 4; ++d) {
                    for (int i = 0; i <= kGrid; ++i) {
                        const float fx = (float)i / (float)kGrid;
                        const float u  = mathlab::nz::Quintic(fx);
                        for (int j = 0; j <= kGrid; ++j) {
                            const float fy = (float)j / (float)kGrid;
                            const float v  = mathlab::nz::Quintic(fy);
                            const float n00 = gx[a] * fx + gy[a] * fy;
                            const float n10 = gx[b] * (fx - 1.0f) + gy[b] * fy;
                            const float n01 = gx[c] * fx + gy[c] * (fy - 1.0f);
                            const float n11 = gx[d] * (fx - 1.0f) + gy[d] * (fy - 1.0f);
                            const float lo  = n00 + (n10 - n00) * u;
                            const float hi  = n01 + (n11 - n01) * u;
                            best = std::max(best, std::fabs((double)(lo + (hi - lo) * v)));
                        }
                    }
                }
            }
        }
    }
    // snippet-end: step04-property-perlin-scan
    cached = best;
    return cached;
}

// Simplex 쪽은 헤더 주석의 실측 상계를 그대로 property 로 승격한다. 격자 조합을
// 다 세는 오라클이 없으므로 여유 1% 를 붙이고, 대신 "관측 최댓값이 상계의 70%
// 이상" 이라는 비자명성 하한을 함께 건다.
constexpr double kSimplexBound = 0.684 * 1.01;

const prop::ParamSpec kNoiseRangeParams[] = {
    {"x",    -256.0f, 256.0f, 0.0f},
    {"y",    -256.0f, 256.0f, 0.0f},
    {"seed",    0.0f,  64.0f, 7.0f},
};

bool PropNoiseRange(const float* p, prop::Observation& obs) {
    const float    x    = p[0];
    const float    y    = p[1];
    const uint32_t seed = (uint32_t)std::lround((double)p[2]);

    const float value   = mathlab::nz::Value2D(x, y, seed);
    const float perlin  = mathlab::nz::Perlin2D(x, y, seed);
    const float simplex = mathlab::nz::Simplex2D(x, y, seed);

    bool ok = true;
    // snippet-begin: step04-property-range-bounds
    // Value 는 [0,1) 해시들의 볼록 결합이므로 [0,1] 을 벗어날 수 없다.
    if (!obs.Bound("Value2D outside [0,1]",
                   std::max(0.0 - (double)value, (double)value - 1.0),
                   2.0 * kUnitRoundoff)) ok = false;
    if (!obs.Bound("|Perlin2D| over scanned bound", std::fabs((double)perlin),
                   PerlinScanBound() + 1e-3)) ok = false;
    if (!obs.Bound("|Simplex2D| over measured bound", std::fabs((double)simplex),
                   kSimplexBound)) ok = false;
    // snippet-end: step04-property-range-bounds
    // 결정성: 같은 (x, y, seed) 는 같은 비트를 내야 한다.
    const bool stable = mathlab::nz::Perlin2D(x, y, seed) == perlin &&
                        mathlab::nz::Value2D(x, y, seed) == value &&
                        mathlab::nz::Simplex2D(x, y, seed) == simplex;
    if (!obs.Bound("noise is not deterministic (exact)", stable ? 0.0 : 1.0, 0.0)) ok = false;

    obs.interesting = std::fabs(perlin) > 0.2f || std::fabs(simplex) > 0.2f;
    return ok;
}

// FBM 은 합이 아니라 *진폭 가중 평균* 이다. 그래서 (a) 상수 기저를 넣으면 그
// 상수를 그대로 돌려주고, (b) Perlin 기저의 FBM 은 Perlin 의 범위 상계를 넘지
// 않는다. 정규화를 빼면 둘 다 깨진다.
const prop::ParamSpec kNoiseFbmParams[] = {
    {"x",          -128.0f, 128.0f, 0.0f},
    {"y",          -128.0f, 128.0f, 0.0f},
    {"seed",          0.0f,  64.0f, 9.0f},
    {"octaves",       0.0f,   8.0f, 4.0f},
    {"lacunarity",    1.2f,   4.0f, 2.0f},
    {"gain",          0.05f,  1.0f, 0.5f},
    {"constant",     -3.0f,   3.0f, 1.0f},
};

bool PropNoiseFbmAverage(const float* p, prop::Observation& obs) {
    const float    x       = p[0];
    const float    y       = p[1];
    const uint32_t seed    = (uint32_t)std::lround((double)p[2]);
    const int      octaves = (int)std::lround((double)p[3]);
    const float    lac     = p[4];
    const float    gain    = p[5];
    const float    konst   = p[6];

    const float constFbm = mathlab::nz::FBM2D(
        x, y, seed, octaves, lac, gain,
        [konst](float, float, uint32_t) { return konst; });
    const float perlinFbm = mathlab::nz::FBM2D(
        x, y, seed, octaves, lac, gain,
        [](float px, float py, uint32_t s) { return mathlab::nz::Perlin2D(px, py, s); });

    const double expected = (octaves > 0) ? (double)konst : 0.0;

    bool ok = true;
    if (!obs.Bound("|FBM(const) - const|", std::fabs((double)constFbm - expected),
                   32.0 * kUnitRoundoff * (std::fabs((double)konst) + 1.0))) ok = false;
    if (!obs.Bound("|FBM(Perlin)| over base bound", std::fabs((double)perlinFbm),
                   PerlinScanBound() + 1e-3)) ok = false;

    obs.interesting = octaves >= 3 && gain < 0.95f;
    return ok;
}

// 격자 정점의 정확값 + 셀 경계 연속성. Perlin 은 정점에서 오프셋이 0 이라 정확히
// 0 이고, 셀 경계에서는 양쪽 셀이 같은 그라디언트를 쓰므로 값이 이어져야 한다.
const prop::ParamSpec kNoiseSeamParams[] = {
    {"ix",    -64.0f, 64.0f, 0.0f},
    {"iy",    -64.0f, 64.0f, 0.0f},
    {"seed",    0.0f, 64.0f, 7.0f},
    {"fy",     0.05f, 0.95f, 0.5f},
    {"delta", 1e-4f,  1e-2f, 1e-3f},
};

bool PropNoiseLatticeSeam(const float* p, prop::Observation& obs) {
    const int      ix    = (int)std::lround((double)p[0]);
    const int      iy    = (int)std::lround((double)p[1]);
    const uint32_t seed  = (uint32_t)std::lround((double)p[2]);
    const float    fy    = p[3];
    const float    delta = p[4];

    bool ok = true;
    // 격자 정점: 네 오프셋 중 하나가 (0,0) 이고 나머지 가중치가 0 이다.
    if (!obs.Bound("Perlin2D at lattice vertex (exact)",
                   std::fabs((double)mathlab::nz::Perlin2D((float)ix, (float)iy, seed)),
                   0.0)) ok = false;
    const float latticeValue = mathlab::nz::Value2D((float)ix, (float)iy, seed);
    const float latticeHash  = mathlab::nz::HashTo01(ix, iy, seed);
    if (!obs.Bound("Value2D at lattice vertex != HashTo01 (exact)",
                   (latticeValue == latticeHash) ? 0.0 : 1.0, 0.0)) ok = false;

    // 셀 경계 연속성. quintic fade 의 도함수는 30 t^2 (1-t)^2 <= 1.875 이고
    // 격자 값은 [-1,1] 이라 |d/dx| <= 1 + 2*1.875 < 5. 여유 2 배로 L = 10.
    const float x0 = (float)(ix + 1);
    const float y  = (float)iy + fy;
    const float lo = mathlab::nz::Perlin2D(x0 - delta, y, seed);
    const float hi = mathlab::nz::Perlin2D(x0 + delta, y, seed);
    if (!obs.Bound("|Perlin across cell seam|", std::fabs((double)hi - lo),
                   10.0 * 2.0 * (double)delta + 1e-5)) ok = false;

    obs.interesting =
        std::fabs((double)mathlab::nz::Perlin2D(x0 - 0.5f, y, seed)) > 0.05;
    return ok;
}

// ---- step05: 충돌 ----

const prop::ParamSpec kObbPairSpec[10] = {
    {"A.cx",   -4.0f, 4.0f, 0.0f}, {"A.cy",   -4.0f, 4.0f, 0.0f},
    {"A.hx",    0.3f, 1.5f, 1.0f}, {"A.hy",    0.3f, 1.5f, 1.0f},
    {"A.angle", 0.0f, 3.14159265f, 0.0f},
    {"B.cx",   -4.0f, 4.0f, 0.0f}, {"B.cy",   -4.0f, 4.0f, 0.0f},
    {"B.hx",    0.3f, 1.5f, 1.0f}, {"B.hy",    0.3f, 1.5f, 1.0f},
    {"B.angle", 0.0f, 3.14159265f, 0.0f},
};

mathlab::OBB2D ObbFromParams(const float* p) {
    return mathlab::OBB2D{{p[0], p[1]}, p[2], p[3], p[4]};
}

// 판정을 가르는 값 — 후보 4 축 중 최소 overlap. 음수면 분리다.
double SatMargin(const mathlab::SATResult& sat) {
    double margin = 1e30;
    for (const mathlab::SATAxisInfo& info : sat.axes) {
        margin = std::min(margin, (double)info.overlap);
    }
    return margin;
}

// 경계 두께. 좌표 크기에 비례한 부동소수 오차 두께다. 판정이 이 안에 있으면
// 두 구현이 서로 다른 쪽으로 반올림해도 버그가 아니다.
double BoundaryEps(const mathlab::OBB2D& a, const mathlab::OBB2D& b) {
    const double span = std::fabs((double)a.center.x) + std::fabs((double)a.center.y) +
                        std::fabs((double)b.center.x) + std::fabs((double)b.center.y) +
                        a.halfX + a.halfY + b.halfX + b.halfY;
    return 1e-5 * std::max(1.0, span);
}

bool GjkIntersect(const mathlab::OBB2D& a, const mathlab::OBB2D& b) {
    const std::vector<mathlab::GJKStep> trace = mathlab::GJK2DTraced(a, b);
    return !trace.empty() && trace.back().done && trace.back().intersect;
}

// 제품의 Corners()/Axes() 를 쓰지 않는 독립 꼭짓점 계산 — 분리면 실증의 오라클.
void RefCorners(const mathlab::OBB2D& o, double out[4][2]) {
    const double c = std::cos((double)o.angle);
    const double s = std::sin((double)o.angle);
    const double ex[2] = {c * o.halfX, s * o.halfX};
    const double ey[2] = {-s * o.halfY, c * o.halfY};
    const double cx = o.center.x, cy = o.center.y;
    out[0][0] = cx - ex[0] - ey[0];  out[0][1] = cy - ex[1] - ey[1];
    out[1][0] = cx + ex[0] - ey[0];  out[1][1] = cy + ex[1] - ey[1];
    out[2][0] = cx + ex[0] + ey[0];  out[2][1] = cy + ex[1] + ey[1];
    out[3][0] = cx - ex[0] + ey[0];  out[3][1] = cy - ex[1] + ey[1];
}

void ProjectCorners(const double corners[4][2], double dx, double dy,
                    double& lo, double& hi) {
    lo = 1e30;
    hi = -1e30;
    for (int i = 0; i < 4; ++i) {
        const double t = corners[i][0] * dx + corners[i][1] * dy;
        lo = std::min(lo, t);
        hi = std::max(hi, t);
    }
}

// (5) 두 독립 구현의 판정 일치. 경계 두께 안의 표본은 판정하지 않는다.
bool PropCollisionAgree(const float* p, prop::Observation& obs) {
    const mathlab::OBB2D A = ObbFromParams(p);
    const mathlab::OBB2D B = ObbFromParams(p + 5);
    const mathlab::SATResult sat = mathlab::OBBOBBSat2D(A, B);
    // snippet-begin: step05-property-boundary-band
    // 경계 두께 규칙 — 판정을 가르는 최소 overlap 이 부동소수 오차 두께 안이면
    // 두 구현이 서로 다른 쪽으로 반올림해도 버그가 아니다. 이 표본은 판정하지
    // 않고 skipped 로 센다(탈락이 5% 를 넘으면 그 자체가 실패다).
    const double eps = BoundaryEps(A, B);
    if (std::fabs(SatMargin(sat)) < eps) {
        obs.skipped = true;
        return true;
    }
    const bool gjk = GjkIntersect(A, B);
    bool ok = true;
    if (!obs.Bound("SAT verdict != GJK verdict", (sat.intersect == gjk) ? 0.0 : 1.0, 0.0)) {
        ok = false;
    }
    // snippet-end: step05-property-boundary-band
    obs.interesting = sat.intersect;
    return ok;
}

// (6) 분리면 실증. SAT 가 "분리" 라고 하면 그 축으로 *실제 분리 평면* 을 만들어
//     A 의 네 꼭짓점과 B 의 네 꼭짓점이 서로 반대편에 있음을 보인다. "교차"
//     라고 하면 후보 4 축 밖의 64 방향까지 훑어 분리 방향이 없음을 보인다.
bool PropCollisionWitness(const float* p, prop::Observation& obs) {
    const mathlab::OBB2D A = ObbFromParams(p);
    const mathlab::OBB2D B = ObbFromParams(p + 5);
    const mathlab::SATResult sat = mathlab::OBBOBBSat2D(A, B);
    const double eps = BoundaryEps(A, B);
    if (std::fabs(SatMargin(sat)) < eps) {
        obs.skipped = true;
        return true;
    }

    double ca[4][2], cb[4][2];
    RefCorners(A, ca);
    RefCorners(B, cb);
    bool ok = true;

    if (!sat.intersect) {
        int    best    = -1;
        double bestGap = 0.0;
        for (int i = 0; i < (int)sat.axes.size(); ++i) {
            if (!sat.axes[(size_t)i].separating) continue;
            const double gap = -(double)sat.axes[(size_t)i].overlap;
            if (gap > bestGap) {
                bestGap = gap;
                best    = i;
            }
        }
        if (!obs.Bound("SAT says separated but reports no separating axis",
                       (best >= 0) ? 0.0 : 1.0, 0.0)) {
            return false;
        }
        const Vector2 axis = sat.axes[(size_t)best].axis;
        const double  len  = std::sqrt((double)axis.x * axis.x + (double)axis.y * axis.y);
        // overlap 이 실제 *거리* 이려면 축이 단위여야 한다.
        if (!obs.Bound("|separating axis| - 1", std::fabs(len - 1.0), 1e-5)) return false;
        const double dx = axis.x / len, dy = axis.y / len;

        double aLo, aHi, bLo, bHi;
        ProjectCorners(ca, dx, dy, aLo, aHi);
        ProjectCorners(cb, dx, dy, bLo, bHi);

        // snippet-begin: step05-property-witness-plane
        double plane = 0.0;
        double sign  = 0.0;                  // +1 이면 A 가 평면 아래쪽이다.
        if (aHi < bLo)      { plane = 0.5 * (aHi + bLo); sign =  1.0; }
        else if (bHi < aLo) { plane = 0.5 * (bHi + aLo); sign = -1.0; }
        if (!obs.Bound("reported axis does not actually separate the corner sets",
                       (sign == 0.0) ? 1.0 : 0.0, 0.0)) {
            return false;
        }
        // 평면 실증 — A 의 네 점은 한쪽, B 의 네 점은 반대쪽.
        double violation = 0.0;
        for (int i = 0; i < 4; ++i) {
            const double pa = sign * (ca[i][0] * dx + ca[i][1] * dy - plane);
            const double pb = sign * (cb[i][0] * dx + cb[i][1] * dy - plane);
            violation = std::max(violation, pa);
            violation = std::max(violation, -pb);
        }
        if (!obs.Bound("witness plane violation", violation, 0.0)) ok = false;
        // snippet-end: step05-property-witness-plane
        obs.interesting = true;
    } else {
        // 볼록 분리 정리의 역: 교차한다면 *어떤* 방향으로도 분리되지 않는다.
        double worstGap = 0.0;
        for (int k = 0; k < 64; ++k) {
            const double theta = 3.14159265358979 * (double)k / 64.0;
            const double dx = std::cos(theta), dy = std::sin(theta);
            double aLo, aHi, bLo, bHi;
            ProjectCorners(ca, dx, dy, aLo, aHi);
            ProjectCorners(cb, dx, dy, bLo, bHi);
            worstGap = std::max(worstGap, std::max(bLo - aHi, aLo - bHi));
        }
        if (!obs.Bound("gap found in 64 directions while SAT says intersect",
                       worstGap, eps)) ok = false;
    }
    return ok;
}

// (7) metamorphic — 강체 변환과 균등 스케일에서 판정이 변하지 않는다.
const prop::ParamSpec kCollisionRigidParams[] = {
    {"A.cx",   -4.0f, 4.0f, 0.0f}, {"A.cy",   -4.0f, 4.0f, 0.0f},
    {"A.hx",    0.3f, 1.5f, 1.0f}, {"A.hy",    0.3f, 1.5f, 1.0f},
    {"A.angle", 0.0f, 3.14159265f, 0.0f},
    {"B.cx",   -4.0f, 4.0f, 0.0f}, {"B.cy",   -4.0f, 4.0f, 0.0f},
    {"B.hx",    0.3f, 1.5f, 1.0f}, {"B.hy",    0.3f, 1.5f, 1.0f},
    {"B.angle", 0.0f, 3.14159265f, 0.0f},
    {"phi",    -3.14159265f, 3.14159265f, 0.0f},
    {"tx",    -30.0f, 30.0f, 0.0f}, {"ty", -30.0f, 30.0f, 0.0f},
    {"scale",   0.2f, 5.0f, 1.0f},
};

mathlab::OBB2D ApplyRigid(const mathlab::OBB2D& o, float phi, float tx, float ty) {
    const float c = std::cos(phi), s = std::sin(phi);
    return mathlab::OBB2D{{c * o.center.x - s * o.center.y + tx,
                           s * o.center.x + c * o.center.y + ty},
                          o.halfX, o.halfY, o.angle + phi};
}

mathlab::OBB2D ApplyScale(const mathlab::OBB2D& o, float k) {
    return mathlab::OBB2D{{o.center.x * k, o.center.y * k},
                          o.halfX * k, o.halfY * k, o.angle};
}

bool PropCollisionRigidInvariance(const float* p, prop::Observation& obs) {
    const mathlab::OBB2D A = ObbFromParams(p);
    const mathlab::OBB2D B = ObbFromParams(p + 5);
    const float phi = p[10], tx = p[11], ty = p[12], k = p[13];

    const mathlab::OBB2D rA = ApplyRigid(A, phi, tx, ty);
    const mathlab::OBB2D rB = ApplyRigid(B, phi, tx, ty);
    const mathlab::OBB2D sA = ApplyScale(A, k);
    const mathlab::OBB2D sB = ApplyScale(B, k);

    const mathlab::SATResult base  = mathlab::OBBOBBSat2D(A, B);
    const mathlab::SATResult rigid = mathlab::OBBOBBSat2D(rA, rB);
    const mathlab::SATResult scaled = mathlab::OBBOBBSat2D(sA, sB);

    // 세 배치 모두 경계 두께 밖이어야 판정을 비교할 수 있다.
    if (std::fabs(SatMargin(base))   < BoundaryEps(A, B) ||
        std::fabs(SatMargin(rigid))  < BoundaryEps(rA, rB) ||
        std::fabs(SatMargin(scaled)) < BoundaryEps(sA, sB)) {
        obs.skipped = true;
        return true;
    }

    bool ok = true;
    // snippet-begin: step05-property-metamorphic
    if (!obs.Bound("SAT verdict changed under rigid transform",
                   (base.intersect == rigid.intersect) ? 0.0 : 1.0, 0.0)) ok = false;
    if (!obs.Bound("SAT verdict changed under uniform scale",
                   (base.intersect == scaled.intersect) ? 0.0 : 1.0, 0.0)) ok = false;
    const bool gjkBase = GjkIntersect(A, B);
    if (!obs.Bound("GJK verdict changed under rigid transform",
                   (gjkBase == GjkIntersect(rA, rB)) ? 0.0 : 1.0, 0.0)) ok = false;
    if (!obs.Bound("GJK verdict changed under uniform scale",
                   (gjkBase == GjkIntersect(sA, sB)) ? 0.0 : 1.0, 0.0)) ok = false;
    // snippet-end: step05-property-metamorphic

    obs.interesting = base.intersect;
    return ok;
}

// ---- step08: 행렬 ----

// 생성기가 조건수를 상계한다. |s| 를 [0.1, 4] 로 보정하므로 선형부의 조건수는
// kappa = max|s| / min|s| <= 40 이다. 표본을 버리는 규칙이 아니라 *구성하는*
// 규칙이라 축소 뒤에도 조건이 유지된다. 그래도 규칙을 코드에 남겨 둔다 —
// 생성기를 넓히면 이 문이 먼저 걸린다.
constexpr double kMaxConditionNumber = 1e3;

const prop::ParamSpec kTrsSpec[10] = {
    {"t.x",   -8.0f, 8.0f, 0.0f}, {"t.y", -8.0f, 8.0f, 0.0f}, {"t.z", -8.0f, 8.0f, 0.0f},
    {"axis.x", -1.0f, 1.0f, 0.0f}, {"axis.y", -1.0f, 1.0f, 0.0f},
    {"axis.z", -1.0f, 1.0f, 1.0f}, {"angle", -6.5f, 6.5f, 0.0f},
    {"s.x",   -4.0f, 4.0f, 1.0f}, {"s.y", -4.0f, 4.0f, 1.0f}, {"s.z", -4.0f, 4.0f, 1.0f},
};

const prop::ParamSpec kMatrixInverseParams[] = {
    {"t.x",   -8.0f, 8.0f, 0.0f}, {"t.y", -8.0f, 8.0f, 0.0f}, {"t.z", -8.0f, 8.0f, 0.0f},
    {"axis.x", -1.0f, 1.0f, 0.0f}, {"axis.y", -1.0f, 1.0f, 0.0f},
    {"axis.z", -1.0f, 1.0f, 1.0f}, {"angle", -6.5f, 6.5f, 0.0f},
    {"s.x",   -4.0f, 4.0f, 1.0f}, {"s.y", -4.0f, 4.0f, 1.0f}, {"s.z", -4.0f, 4.0f, 1.0f},
    {"p.x",   -8.0f, 8.0f, 1.0f}, {"p.y", -8.0f, 8.0f, 0.0f}, {"p.z", -8.0f, 8.0f, 0.0f},
};

Matrix TrsFromParams(const float* p) {
    return mathlab::MatComposeTRS(
        {p[0], p[1], p[2]},
        mathlab::QuatFromAxisAngle({p[3], p[4], p[5]}, p[6]),
        {RepairScale(p[7]), RepairScale(p[8]), RepairScale(p[9])});
}

bool PropMatrixInverseRoundtrip(const float* p, prop::Observation& obs) {
    const Vector3 t{p[0], p[1], p[2]};
    const Vector3 s{RepairScale(p[7]), RepairScale(p[8]), RepairScale(p[9])};
    const Vector3 pt{p[10], p[11], p[12]};

    const double maxS = std::max(std::fabs((double)s.x),
                                 std::max(std::fabs((double)s.y), std::fabs((double)s.z)));
    const double minS = std::min(std::fabs((double)s.x),
                                 std::min(std::fabs((double)s.y), std::fabs((double)s.z)));
    const double kappa = maxS / minS;
    if (kappa > kMaxConditionNumber) {   // 조건수 규칙. 생성기상 여기 오지 않는다.
        obs.skipped = true;
        return true;
    }

    const Matrix M = TrsFromParams(p);
    Matrix inv = mathlab::MatIdentity();
    const bool invertible = mathlab::MatInvert(M, inv);
    bool ok = true;
    if (!obs.Bound("MatInvert failed on a well-conditioned TRS",
                   invertible ? 0.0 : 1.0, 0.0)) {
        return false;
    }

    // snippet-begin: step08-property-inverse-tolerance
    // 오차 증폭의 근거: M p + t 를 계산할 때 잃는 자릿수가 |t| + maxS*|p| 규모이고
    // M^-1 을 적용하면 1/minS 배 커진다 → u * kappa * (|p| + |t|/maxS).
    // 두 번의 행렬-벡터 곱과 Gauss-Jordan 을 합쳐 여유 계수 64 를 붙인다.
    const double tol = 64.0 * kUnitRoundoff * kappa *
                       ((double)mathlab::V3Length(pt) +
                        (double)mathlab::V3Length(t) / maxS + 1.0);

    const Vector3 back = mathlab::MatTransformPoint(inv, mathlab::MatTransformPoint(M, pt));
    if (!obs.Bound("|M^-1 (M p) - p|",
                   (double)mathlab::V3Length(mathlab::V3Sub(back, pt)), tol)) ok = false;
    // snippet-end: step08-property-inverse-tolerance
    const Vector3 fwd = mathlab::MatTransformPoint(M, mathlab::MatTransformPoint(inv, pt));
    if (!obs.Bound("|M (M^-1 p) - p|",
                   (double)mathlab::V3Length(mathlab::V3Sub(fwd, pt)), tol)) ok = false;

    // 아핀 TRS 의 행렬식은 스케일 곱이다(회전은 부피를 보존한다). 3x3 전개는
    // 크기 maxS^3 인 항들이 상쇄되며 만들어지므로 상계도 절대값 기준이다.
    const double detExpect = (double)s.x * s.y * s.z;
    if (!obs.Bound("|det(M) - sx sy sz|",
                   std::fabs((double)mathlab::MatDeterminant(M) - detExpect),
                   64.0 * kUnitRoundoff * maxS * maxS * maxS)) ok = false;

    obs.interesting = kappa > 5.0 && mathlab::V3Length(pt) > 1.0;
    return ok;
}

// (12) 합성 대 개별 적용. 그리고 전치·항등원은 *비트 단위로* 같아야 한다 —
//      MatMul 이 같은 곱을 같은 순서로 더하기 때문이다.
const prop::ParamSpec kMatrixComposeParams[] = {
    {"A.t.x", -8.0f, 8.0f, 0.0f}, {"A.t.y", -8.0f, 8.0f, 0.0f}, {"A.t.z", -8.0f, 8.0f, 0.0f},
    {"A.axis.x", -1.0f, 1.0f, 0.0f}, {"A.axis.y", -1.0f, 1.0f, 0.0f},
    {"A.axis.z", -1.0f, 1.0f, 1.0f}, {"A.angle", -6.5f, 6.5f, 0.0f},
    {"A.s.x", -4.0f, 4.0f, 1.0f}, {"A.s.y", -4.0f, 4.0f, 1.0f}, {"A.s.z", -4.0f, 4.0f, 1.0f},
    {"B.t.x", -8.0f, 8.0f, 0.0f}, {"B.t.y", -8.0f, 8.0f, 0.0f}, {"B.t.z", -8.0f, 8.0f, 0.0f},
    {"B.axis.x", -1.0f, 1.0f, 0.0f}, {"B.axis.y", -1.0f, 1.0f, 0.0f},
    {"B.axis.z", -1.0f, 1.0f, 1.0f}, {"B.angle", -6.5f, 6.5f, 0.0f},
    {"B.s.x", -4.0f, 4.0f, 1.0f}, {"B.s.y", -4.0f, 4.0f, 1.0f}, {"B.s.z", -4.0f, 4.0f, 1.0f},
    {"p.x", -8.0f, 8.0f, 1.0f}, {"p.y", -8.0f, 8.0f, 0.0f}, {"p.z", -8.0f, 8.0f, 0.0f},
};

bool PropMatrixComposeVsApply(const float* p, prop::Observation& obs) {
    const Matrix A = TrsFromParams(p);
    const Matrix B = TrsFromParams(p + 10);
    const Vector3 pt{p[20], p[21], p[22]};

    const Matrix AB = mathlab::MatMul(A, B);
    bool ok = true;

    const double scale = (1.0 + MatMaxAbs(A)) * (1.0 + MatMaxAbs(B)) *
                         (1.0 + (double)mathlab::V3Length(pt));
    const double tol = 64.0 * kUnitRoundoff * scale;

    const Vector3 once  = mathlab::MatTransformPoint(AB, pt);
    const Vector3 twice = mathlab::MatTransformPoint(A, mathlab::MatTransformPoint(B, pt));
    if (!obs.Bound("|(A B) p - A (B p)|",
                   (double)mathlab::V3Length(mathlab::V3Sub(once, twice)), tol)) ok = false;

    const Vector3 vOnce  = mathlab::MatTransformVector(AB, pt);
    const Vector3 vTwice = mathlab::MatTransformVector(A, mathlab::MatTransformVector(B, pt));
    if (!obs.Bound("|(A B) v - A (B v)| (direction)",
                   (double)mathlab::V3Length(mathlab::V3Sub(vOnce, vTwice)), tol)) ok = false;

    // snippet-begin: step08-property-exact-transpose
    // (AB)^T 와 B^T A^T 는 같은 곱을 같은 순서로 더하므로 비트가 같다.
    if (!obs.Bound("(A B)^T != B^T A^T (exact)",
                   MatBitEqual(mathlab::MatTranspose(AB),
                               mathlab::MatMul(mathlab::MatTranspose(B),
                                               mathlab::MatTranspose(A)))
                       ? 0.0 : 1.0,
                   0.0)) ok = false;
    if (!obs.Bound("A * I != A (exact)",
                   MatBitEqual(mathlab::MatMul(A, mathlab::MatIdentity()), A) ? 0.0 : 1.0,
                   0.0)) ok = false;

    // 대조군 — 합성은 비가환이다. 곱이 가환으로 바뀌면 위 관계가 자명해진다.
    obs.interesting = MatMaxDiff(AB, mathlab::MatMul(B, A)) > 1e-3;
    // snippet-end: step08-property-exact-transpose
    return ok;
}

// (13) 독립 구현 대조 — 행렬 회전과 쿼터니언 샌드위치는 같은 결과를 내야 한다.
const prop::ParamSpec kMatrixQuatParams[] = {
    {"axis.x", -1.0f,  1.0f, 0.0f}, {"axis.y", -1.0f, 1.0f, 0.0f},
    {"axis.z", -1.0f,  1.0f, 1.0f}, {"angle",  -6.5f, 6.5f, 0.0f},
    {"scale",   0.05f, 20.0f, 1.0f},
    {"v.x",    -8.0f,  8.0f, 1.0f}, {"v.y", -8.0f, 8.0f, 0.0f},
    {"v.z",    -8.0f,  8.0f, 0.0f},
};

bool PropMatrixQuatAgreement(const float* p, prop::Observation& obs) {
    const Quaternion raw = QuatScaleQ(
        mathlab::QuatFromAxisAngle({p[0], p[1], p[2]}, p[3]), p[4]);
    const Vector3 v{p[5], p[6], p[7]};
    const double  vLen = (double)mathlab::V3Length(v);

    const Matrix R = mathlab::MatRotate(raw);
    bool ok = true;
    if (!obs.Bound("max|R^T R - I|",
                   MatMaxDiff(mathlab::MatMul(mathlab::MatTranspose(R), R),
                              mathlab::MatIdentity()),
                   64.0 * kUnitRoundoff)) ok = false;
    if (!obs.Bound("|det(R) - 1|", std::fabs((double)mathlab::MatDeterminant(R) - 1.0),
                   256.0 * kUnitRoundoff)) ok = false;

    const Vector3 byMat  = mathlab::MatTransformVector(R, v);
    const Vector3 byQuat = mathlab::QuatRotateVec(mathlab::QuatNormalize(raw), v);
    if (!obs.Bound("|R v - q v q*|",
                   (double)mathlab::V3Length(mathlab::V3Sub(byMat, byQuat)),
                   128.0 * kUnitRoundoff * (vLen + 1.0))) ok = false;
    if (!obs.Bound("| |R v| - |v| |",
                   std::fabs((double)mathlab::V3Length(byMat) - vLen),
                   128.0 * kUnitRoundoff * (vLen + 1.0))) ok = false;

    obs.interesting = std::fabs(p[3]) > 0.1f && vLen > 0.5;
    return ok;
}

// ---- property 등록부 ----
//
// 표본 수는 CI 부담과의 절충이다. 기본값으로 property 층 전체가 0.5 초 안쪽에서
// 돌고, `--scale=N` 이 표본을 N 배로 늘린다(야간 확장). seed 는 `--seed=` 로
// 바꿀 수 있고, 반례 보고가 그대로 재현 명령을 찍는다.

#define PROP_COUNT(array) (int)(sizeof(array) / sizeof((array)[0]))

const prop::Property kProperties[] = {
    {"step02", "quat/norm-preserved", kQuatNormParams, PROP_COUNT(kQuatNormParams),
     PropQuatNormPreserved, 4000, 25, 100, 2, 0,
     "unit quaternion in, unit quaternion out; sandwich rotation preserves |v|"},
    {"step02", "quat/compose-assoc", kQuatAssocParams, PROP_COUNT(kQuatAssocParams),
     PropQuatComposeAssoc, 4000, 40, 100, 2, 0,
     "(q1 q2) q3 == q1 (q2 q3) within 32u; the product itself stays non-commutative"},
    {"step02", "quat/double-cover", kQuatCoverParams, PROP_COUNT(kQuatCoverParams),
     PropQuatDoubleCover, 4000, 25, 100, 5, 0,
     "q and -q are the same rotation; slerp is invariant to negating an endpoint"},
    {"step02", "quat/slerp-uniform", kQuatSlerpParams, PROP_COUNT(kQuatSlerpParams),
     PropQuatSlerpUniform, 800, 20, 100, 2, 0,
     "slerp walks the arc at constant speed (chord max/min <= 1.02); nlerp does not"},
    {"step04", "noise/range-bounds", kNoiseRangeParams, PROP_COUNT(kNoiseRangeParams),
     PropNoiseRange, 6000, 20, 100, 2, 70,
     "Value in [0,1]; |Perlin| within the scanned gradient bound; |Simplex| within 0.691"},
    {"step04", "noise/fbm-average", kNoiseFbmParams, PROP_COUNT(kNoiseFbmParams),
     PropNoiseFbmAverage, 2000, 20, 100, 2, 0,
     "FBM is an amplitude-weighted mean: constant in, constant out; base bound kept"},
    {"step04", "noise/lattice-seam", kNoiseSeamParams, PROP_COUNT(kNoiseSeamParams),
     PropNoiseLatticeSeam, 3000, 40, 100, 2, 0,
     "Perlin is exactly 0 at lattice vertices and continuous across cell seams"},
    {"step05", "collision/sat-gjk-agree", kObbPairSpec, PROP_COUNT(kObbPairSpec),
     PropCollisionAgree, 5000, 10, 90, 5, 0,
     "SAT and GJK agree outside a float-thickness band around the touching case"},
    {"step05", "collision/axis-witness", kObbPairSpec, PROP_COUNT(kObbPairSpec),
     PropCollisionWitness, 2500, 10, 95, 5, 0,
     "a separated verdict comes with a plane that really splits the corner sets"},
    {"step05", "collision/rigid-invariance", kCollisionRigidParams,
     PROP_COUNT(kCollisionRigidParams), PropCollisionRigidInvariance, 3000, 10, 90, 10, 0,
     "the verdict is invariant under rigid motion and uniform scaling"},
    {"step08", "matrix/inverse-roundtrip", kMatrixInverseParams,
     PROP_COUNT(kMatrixInverseParams), PropMatrixInverseRoundtrip, 3000, 20, 100, 2, 0,
     "M^-1 (M p) == p within 64u * kappa * scale; kappa is capped by the generator"},
    {"step08", "matrix/compose-vs-apply", kMatrixComposeParams,
     PROP_COUNT(kMatrixComposeParams), PropMatrixComposeVsApply, 2000, 40, 100, 2, 0,
     "(A B) p == A (B p); (A B)^T == B^T A^T bit for bit; A B != B A"},
    {"step08", "matrix/quat-agreement", kMatrixQuatParams, PROP_COUNT(kMatrixQuatParams),
     PropMatrixQuatAgreement, 3000, 25, 100, 2, 0,
     "MatRotate(q) v == q v q* and R stays orthogonal with det 1"},
};

#undef PROP_COUNT

// ---- property 실행 ----

prop::RunConfig gPropertyConfig;
const char*     gPropertyOnly = nullptr;
std::string     gCaseSummary;

void PrintParams(const prop::Property& property, const std::vector<float>& values) {
    for (int k = 0; k < property.paramCount && k < (int)values.size(); ++k) {
        if (k) std::cerr << ' ';
        std::cerr << property.params[k].name << '=' << std::setprecision(7)
                  << values[(size_t)k];
    }
}

void RunPropertyCase(const prop::Property& property) {
    const prop::RunResult result = prop::Run(property, gPropertyConfig);

    if (result.failed) {
        std::cerr << "\n  property '" << property.name << "' found a counterexample\n";
        std::cerr << "    contract : " << property.contract << '\n';
        std::cerr << "    sample   : " << result.failedSample << " of " << result.planned
                  << "   (master seed " << gPropertyConfig.masterSeed
                  << ", scale " << gPropertyConfig.scale << ")\n";
        std::cerr << "    broke    : " << result.failure.worstName << " = "
                  << std::setprecision(6) << result.failure.worstObserved
                  << "   (bound " << result.failure.worstBound << ")\n";
        std::cerr << "    generated: ";
        PrintParams(property, result.generated);
        std::cerr << '\n';
        std::cerr << "    shrunk   : ";
        PrintParams(property, result.shrunk);
        std::cerr << "   (" << result.shrinkSteps << " shrink step(s))\n";
        std::cerr << "    repro    : math_core_tests " << property.step
                  << " --only=" << property.name
                  << " --seed=" << gPropertyConfig.masterSeed
                  << " --sample=" << result.failedSample << "\n\n";
    }
    CHECK(!result.failed);

    // 반례가 나오면 표본 루프가 거기서 멈추므로 아래 통계는 의미가 없다.
    // `--sample=` 재현 모드도 표본이 하나뿐이라 마찬가지다.
    const int judged = result.samples - result.skipped;
    if (gPropertyConfig.onlySample < 0 && !result.failed) {
        // 비자명성. 퇴화 표본만 모이면 property 는 아무것도 강제하지 않는다.
        const bool enough =
            judged > 0 && result.interesting * 100 >= judged * property.minInterestPct;
        if (!enough) {
            std::cerr << "  property '" << property.name << "': only " << result.interesting
                      << " of " << judged << " judged sample(s) were non-degenerate "
                      << "(need " << property.minInterestPct << "%)\n";
        }
        CHECK(enough);

        if (property.maxInterestPct < 100) {
            const bool balanced =
                result.interesting * 100 <= judged * property.maxInterestPct;
            if (!balanced) {
                std::cerr << "  property '" << property.name << "': " << result.interesting
                          << " of " << judged << " sample(s) hit the same branch\n";
            }
            CHECK(balanced);
        }

        // 사전조건 탈락이 많으면 대부분의 표본이 판정되지 않은 것이다.
        const bool fewSkips = result.skipped * 100 <= result.samples * property.maxSkipPct;
        if (!fewSkips) {
            std::cerr << "  property '" << property.name << "': " << result.skipped
                      << " of " << result.samples << " sample(s) fell outside the "
                      << "precondition\n";
        }
        CHECK(fewSkips);

        if (property.minWorstPct > 0) {
            // 실측 상계를 승격한 property 는 상계가 헐거우면 자명해진다.
            const bool tight = result.worst * 100.0f >= (float)property.minWorstPct;
            if (!tight) {
                std::cerr << "  property '" << property.name
                          << "': worst observation is only " << (result.worst * 100.0f)
                          << "% of the declared bound\n";
            }
            CHECK(tight);
        }
    }

    // 요약 한 줄. `worst` 는 가장 상계에 가까웠던 관측의 비율이다 — 1.00 에
    // 붙으면 계약이 빠듯하다는 뜻이고, 0.00 이면 그 property 의 모든 조항이
    // 정확 일치(상계 0)였다는 뜻이다.
    std::ostringstream summary;
    summary << "(" << result.samples << " samples, " << result.interesting
            << " non-degenerate, " << result.skipped << " skipped, worst "
            << std::fixed << std::setprecision(3) << result.worst << "x";
    if (result.worst > 0.0f && result.worstName[0]) {
        summary << " on '" << result.worstName << "'";
    }
    if (property.minWorstPct > 0) {
        // 실측 상계를 승격한 property 는 실제 관측과 선언한 상계를 같이 찍는다 —
        // 이 두 수가 아티클에 그대로 실려야 상계가 어디서 왔는지 추적된다.
        summary << " = " << std::setprecision(6) << result.worstObserved << " of "
                << result.worstBound;
    }
    summary << ")";
    gCaseSummary = summary.str();
}

// ==================== 실행 단위 ====================

// 검증을 *그것을 들여온 step* 에 묶는다. step 키가 곧 ctest 타깃의 인자다.
// 고정 사례는 fn 을, property 사례는 property 를 가진다(둘은 배타적이다).
struct TestCase {
    const char*           step;
    const char*           name;      // property 사례면 nullptr
    void                (*fn)();
    const prop::Property* property;
};

const TestCase kCases[] = {
    {"step01", "layout/tab_canvas_footer",        TestLayout,                      nullptr},
    {"step02", "quaternion/identities",           TestQuaternion,                  nullptr},
    {"step02", "quaternion/slerp_uniform_speed",  TestQuaternionSlerpUniformSpeed, nullptr},
    {"step02", "quaternion/gimbal_lock",          TestQuaternionGimbalLock,        nullptr},
    {"step02", nullptr, nullptr, &kProperties[0]},
    {"step02", nullptr, nullptr, &kProperties[1]},
    {"step02", nullptr, nullptr, &kProperties[2]},
    {"step02", nullptr, nullptr, &kProperties[3]},
    {"step03", "curves/basis_and_arc_length",     TestCurves,                      nullptr},
    {"step04", "noise/value_perlin_simplex_fbm",  TestNoise,                       nullptr},
    {"step04", nullptr, nullptr, &kProperties[4]},
    {"step04", nullptr, nullptr, &kProperties[5]},
    {"step04", nullptr, nullptr, &kProperties[6]},
    {"step05", "collision/sat_vs_gjk",            TestCollision,                   nullptr},
    {"step05", "collision/iteration_limit",       TestGjkIterationLimit,           nullptr},
    {"step05", nullptr, nullptr, &kProperties[7]},
    {"step05", nullptr, nullptr, &kProperties[8]},
    {"step05", nullptr, nullptr, &kProperties[9]},
    {"step06", "bvh/median_split_and_traversal",  TestBvh,                         nullptr},
    {"step06", "bvh/ray_boundary_contract",       TestBvhBoundaryContract,         nullptr},
    {"step07", "sdf/primitives_and_smin",         TestSdf,                         nullptr},
    {"step08", "depth/std_vs_reverse",            TestDepth,                       nullptr},
    {"step08", "kahan/compensated_sum",           TestKahan,                       nullptr},
    {"step08", "vector/cross_project_reflect",    TestVectorAlgebra,               nullptr},
    {"step08", "matrix/inverse_orthogonal_trs",   TestMatrix,                      nullptr},
    {"step08", nullptr, nullptr, &kProperties[10]},
    {"step08", nullptr, nullptr, &kProperties[11]},
    {"step08", nullptr, nullptr, &kProperties[12]},
};

const char* CaseName(const TestCase& testCase) {
    return testCase.fn ? testCase.name : testCase.property->name;
}

}  // namespace

// 인자 없이 부르면 표 전체, `step05` 처럼 키를 주면 그 step 이 추가한 것만 돈다.
// `--` 로 시작하는 인자는 property 층의 플래그다(파일 머리 주석 참고).
int main(int argc, char** argv) {
    const char* filter        = nullptr;
    bool        propertiesOnly = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strncmp(arg, "--", 2) != 0) {
            if (!filter) filter = arg;
            continue;
        }
        if (std::strcmp(arg, "--properties") == 0) {
            propertiesOnly = true;
        } else if (std::strncmp(arg, "--only=", 7) == 0) {
            gPropertyOnly  = arg + 7;
            propertiesOnly = true;
        } else if (std::strncmp(arg, "--seed=", 7) == 0) {
            gPropertyConfig.masterSeed = (uint32_t)std::strtoul(arg + 7, nullptr, 10);
        } else if (std::strncmp(arg, "--scale=", 8) == 0) {
            const long value = std::strtol(arg + 8, nullptr, 10);
            gPropertyConfig.scale = (value > 1) ? (int)value : 1;
        } else if (std::strncmp(arg, "--sample=", 9) == 0) {
            gPropertyConfig.onlySample = (int)std::strtol(arg + 9, nullptr, 10);
            propertiesOnly             = true;
        } else {
            std::cerr << "unknown option '" << arg << "'\n";
            return 2;
        }
    }

    int selected = 0;
    for (const TestCase& testCase : kCases) {
        if (filter && std::strcmp(filter, testCase.step) != 0) continue;
        if (propertiesOnly && !testCase.property) continue;
        if (gPropertyOnly && (!testCase.property ||
                              !std::strstr(testCase.property->name, gPropertyOnly))) {
            continue;
        }
        ++selected;

        const int checksBefore   = checks;
        const int failuresBefore = failures;
        gCaseSummary.clear();
        if (testCase.fn) {
            testCase.fn();
        } else {
            RunPropertyCase(*testCase.property);
        }

        const char* name = CaseName(testCase);
        std::cout << '[' << testCase.step << "] " << name << ' ';
        for (size_t pad = std::strlen(name); pad < 32; ++pad) {
            std::cout << '.';
        }
        std::cout << ' ' << (checks - checksBefore) << " checks, "
                  << (failures - failuresBefore) << " failed";
        if (!gCaseSummary.empty()) std::cout << "  " << gCaseSummary;
        std::cout << '\n';
    }

    if (selected == 0) {
        std::cerr << "no test case for '" << (filter ? filter : "") << "'\n";
        return 2;
    }

    std::cout << (filter ? filter : "all") << ": " << selected
              << " case(s), " << checks << " check(s), " << failures
              << " failure(s)\n";
    if (failures != 0) {
        std::cerr << failures << " math core test(s) failed\n";
        return 1;
    }
    return 0;
}
