#include "scenes/narrow_phase_scene.h"

#include "step_cases.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr)                                                          \
    do {                                                                     \
        if (!(expr)) {                                                       \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':' \
                      << __LINE__ << ")\n";                                  \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

// 반크기 half 정사각형.
physlab::Polygon2D MakeSquare(float x, float y, float angle, float half) {
    physlab::Polygon2D box;
    box.local = {{-half, -half}, {half, -half},
                 {half, half}, {-half, half}};
    box.pos = {x, y};
    box.angle = angle;
    return box;
}

// 반크기 1 정사각형. 중심거리 1.5 면 침투 깊이가 해석적으로 정확히 0.5 다.
physlab::Polygon2D MakeBox(float x, float y, float angle = 0.0f) {
    return MakeSquare(x, y, angle, 1.0f);
}

physlab::Polygon2D Translated(const physlab::Polygon2D& p, Vector2 t) {
    physlab::Polygon2D r = p;
    r.pos = {p.pos.x + t.x, p.pos.y + t.y};
    return r;
}

// 테스트 안의 독립 참조 구현. SAT / GJK+EPA 어느 쪽 코드도 쓰지 않고 두 값을
// 따로 낸다.
//   intersect : 축 위 두 구간의 *교집합 길이* 의 최소  (예전 SATCollide 의 식)
//   push      : 두 도형을 분리하는 데 필요한 최소 이동 거리 = 진짜 MTD
// 한 구간이 다른 구간을 *포함* 하면 (깊은 겹침) 둘이 갈라진다.
struct AxisReference {
    bool  hit       = false;
    float intersect = 0.0f;
    float push      = 0.0f;
};

AxisReference ReferenceDepths(const physlab::Polygon2D& a,
                              const physlab::Polygon2D& b) {
    const auto va = a.WorldVerts();
    const auto vb = b.WorldVerts();
    AxisReference r;
    r.hit       = true;
    r.intersect = 1e30f;
    r.push      = 1e30f;

    auto project = [](const std::vector<Vector2>& v, Vector2 axis,
                      float& mn, float& mx) {
        mn = mx = v[0].x * axis.x + v[0].y * axis.y;
        for (size_t i = 1; i < v.size(); ++i) {
            const float d = v[i].x * axis.x + v[i].y * axis.y;
            mn = std::min(mn, d);
            mx = std::max(mx, d);
        }
    };

    auto runAxes = [&](const std::vector<Vector2>& v) {
        const int n = (int)v.size();
        for (int i = 0; i < n && r.hit; ++i) {
            const Vector2 p = v[(size_t)i];
            const Vector2 q = v[(size_t)((i + 1) % n)];
            Vector2 axis{-(q.y - p.y), q.x - p.x};
            const float len = std::sqrt(axis.x * axis.x + axis.y * axis.y);
            if (len < 1e-9f) continue;
            axis = {axis.x / len, axis.y / len};

            float amn, amx, bmn, bmx;
            project(va, axis, amn, amx);
            project(vb, axis, bmn, bmx);
            if (amx < bmn || bmx < amn) { r.hit = false; return; }

            r.intersect = std::min(r.intersect,
                                   std::min(amx, bmx) - std::max(amn, bmn));
            r.push = std::min(r.push, std::min(amx - bmn, bmx - amn));
        }
    };

    runAxes(va);
    if (r.hit) runAxes(vb);
    if (!r.hit) { r.intersect = 0.0f; r.push = 0.0f; }
    return r;
}

// (normal, depth) 계약의 양면 검사 — 참조 구현만 쓰므로 SAT / EPA 어느 쪽도
// 자기 자신을 채점하지 않는다.
//   normal 로 depth 보다 조금 더 밀면 반드시 떨어진다  → depth 가 과소평가 아님
//   normal 로 depth 보다 조금 덜 밀면 반드시 아직 겹친다 → depth 가 과대평가 아님
// depth 만 맞고 normal 이 엉뚱한 축이면 앞쪽에서 걸린다.
void CheckPushSeparates(const physlab::Polygon2D& a,
                        const physlab::Polygon2D& b,
                        Vector2 normal, float depth, const char* tag) {
    const float eps = 2e-3f;
    const auto over = ReferenceDepths(
        a, Translated(b, {normal.x * (depth + eps), normal.y * (depth + eps)}));
    if (over.hit) {
        std::cerr << "[push did not separate] " << tag << " depth=" << depth
                  << " n=(" << normal.x << ',' << normal.y << ")\n";
    }
    CHECK(!over.hit);

    if (depth > 2.0f * eps) {
        const auto under = ReferenceDepths(
            a, Translated(b, {normal.x * (depth - eps),
                              normal.y * (depth - eps)}));
        if (!under.hit) {
            std::cerr << "[depth overestimated] " << tag << " depth=" << depth
                      << '\n';
        }
        CHECK(under.hit);
    }
}

// expectedDepth < 0 이면 해석적 기대값 검사를 생략한다.
void CheckAlgorithmsAgree(const physlab::Polygon2D& a,
                          const physlab::Polygon2D& b,
                          bool expectedCollision,
                          float expectedDepth = -1.0f) {
    Vector2 satNormal{}, gjkNormal{};
    float satDepth = 0.0f, gjkDepth = 0.0f;
    const bool sat =
        physlab::SATCollide(a, b, satNormal, satDepth);
    const bool gjk =
        physlab::GjkEpaCollide(a, b, gjkNormal, gjkDepth);
    CHECK(sat == expectedCollision);
    CHECK(gjk == expectedCollision);
    if (!expectedCollision) return;

    CHECK(satDepth >= 0.0f);
    CHECK(std::isfinite(satDepth));
    CHECK(std::isfinite(gjkDepth));
    CHECK(std::fabs(satNormal.x * satNormal.x +
                    satNormal.y * satNormal.y - 1.0f) < 1e-4f);
    // EPA 가 깊이 0 을 돌려주면 (degenerate simplex) 그 자체가 결함이다.
    // 예전처럼 `if (gjkDepth > 0)` 가드 안에 숨기지 않고 여기서 못 박는다.
    CHECK(gjkDepth > 0.0f);
    CHECK(std::fabs(gjkNormal.x * gjkNormal.x +
                    gjkNormal.y * gjkNormal.y - 1.0f) < 1e-4f);

    // (a) 해석적 깊이 리터럴. 두 구현을 서로만 비교하면 depth 를 통째로 0 으로
    //     만들어도 통과한다.
    if (expectedDepth >= 0.0f) {
        CHECK(std::fabs(satDepth - expectedDepth) < 1e-4f);
        CHECK(std::fabs(gjkDepth - expectedDepth) < 1e-3f);
    }

    // (b) 두 독립 구현의 깊이 일치 — GUI 라벨이 아니라 ctest 가 본다.
    CHECK(std::fabs(satDepth - gjkDepth) < 0.05f);

    // (c) 법선 부호 규약: a -> b 방향 (step05 가 명시한 규약).
    const float dx = b.pos.x - a.pos.x;
    const float dy = b.pos.y - a.pos.y;
    if (std::fabs(dx) + std::fabs(dy) > 1e-4f) {
        CHECK(satNormal.x * dx + satNormal.y * dy > 0.0f);
        CHECK(gjkNormal.x * dx + gjkNormal.y * dy > 0.0f);
    }
}

void TestSeparatedAndOverlappingBoxes() {
    const auto a = MakeBox(0.0f, 0.0f);
    CheckAlgorithmsAgree(a, MakeBox(3.0f, 0.0f), false);
    CheckAlgorithmsAgree(a, MakeBox(1.5f, 0.0f), true, 0.5f);       // 정확히 0.5
    CheckAlgorithmsAgree(a, MakeBox(0.0f, 1.25f), true, 0.75f);     // 정확히 0.75
    CheckAlgorithmsAgree(a, MakeBox(1.0f, 0.5f, 0.4f), true);
    // 경계 접촉 — 정확히 맞닿으면 깊이 0 의 충돌로 본다 (SAT 의 <= 규약).
    Vector2 n{}; float d = -1.0f;
    CHECK(physlab::SATCollide(a, MakeBox(2.0f, 0.0f), n, d));
    CHECK(std::fabs(d) < 1e-6f);
    std::cout << "  analytic depths 0.5 / 0.75 matched; boundary touch depth="
              << d << " normal=(" << n.x << ',' << n.y << ")\n";
}

// 깊은 겹침에서 SAT 와 EPA 가 갈라지는 지점을 고정 케이스로 못 박는다.
// 같은 중심에 45도 돌아간 정사각형: 분리에 필요한 최소 이동은 1 + sqrt(2).
void TestDeepOverlapReference() {
    const auto a = MakeBox(0.0f, 0.0f, 0.0f);
    const auto b = MakeBox(0.0f, 0.0f, 0.7853981634f);  // 45도
    Vector2 satNormal{}, gjkNormal{};
    float satDepth = 0.0f, gjkDepth = 0.0f;
    CHECK(physlab::SATCollide(a, b, satNormal, satDepth));
    CHECK(physlab::GjkEpaCollide(a, b, gjkNormal, gjkDepth));

    const AxisReference ref = ReferenceDepths(a, b);
    CHECK(ref.hit);
    CHECK(std::fabs(ref.push - (1.0f + std::sqrt(2.0f))) < 1e-4f);
    CHECK(std::fabs(ref.intersect - 2.0f) < 1e-4f);

    // snippet-begin: mtd-vs-intersection
    // 두 구현 모두 진짜 MTD 를 낸다 — 해석해와 직접 비교.
    CHECK(std::fabs(gjkDepth - (1.0f + std::sqrt(2.0f))) < 1e-3f);
    CHECK(std::fabs(satDepth - (1.0f + std::sqrt(2.0f))) < 1e-4f);

    // 교집합 길이(2.0) 를 쓰던 옛 식이면 여기서 걸린다. 두 값이 0.41 만큼
    // 떨어져 있으므로 이 검사는 허용 오차 문제가 아니라 식 자체를 고정한다.
    CHECK(std::fabs(satDepth - ref.intersect) > 0.4f);
    CHECK(satDepth > 0.0f);
    // snippet-end: mtd-vs-intersection
    CheckPushSeparates(a, b, satNormal, satDepth, "deep-overlap SAT");
    CheckPushSeparates(a, b, gjkNormal, gjkDepth, "deep-overlap EPA");
    std::cout << "  45deg cocentric: sat=" << satDepth << " epa=" << gjkDepth
              << " mtd=" << ref.push << " intersection=" << ref.intersect
              << '\n';
}

// 포함(containment) 배치 모음 — 한 축 투영 구간이 다른 쪽을 통째로 품는 경우.
// 교집합 길이 식은 여기서 짧은 구간 길이로 잘려 MTD 를 과소평가하므로,
// SAT 를 옛 식으로 되돌리면 이 테스트는 반드시 실패한다.
void TestSatMatchesEpaOnDeepOverlap() {
    struct Case {
        const char* name;
        physlab::Polygon2D a, b;
    };
    const Case cases[] = {
        // 같은 중심, 상대 회전만 다른 같은 크기 정사각형.
        {"cocentric 15deg", MakeBox(0, 0, 0.0f), MakeBox(0, 0, 0.2617993878f)},
        {"cocentric 30deg", MakeBox(0, 0, 0.0f), MakeBox(0, 0, 0.5235987756f)},
        {"cocentric 45deg", MakeBox(0, 0, 0.0f), MakeBox(0, 0, 0.7853981634f)},
        {"cocentric 60deg", MakeBox(0, 0, 0.0f), MakeBox(0, 0, 1.0471975512f)},
        // 큰 정사각형이 작은 정사각형을 완전히 삼킨 배치.
        {"small inside big",
         MakeSquare(0, 0, 0.0f, 2.0f), MakeSquare(0, 0, 0.0f, 0.5f)},
        {"small inside big, offset",
         MakeSquare(0, 0, 0.0f, 2.0f), MakeSquare(0.4f, -0.3f, 0.3f, 0.5f)},
        {"big inside small(swapped)",
         MakeSquare(0, 0, 0.0f, 0.5f), MakeSquare(0.2f, 0.1f, 0.0f, 2.0f)},
    };

    float worstGap = 0.0f;
    float worstSpread = 0.0f;
    for (const auto& c : cases) {
        Vector2 satNormal{}, gjkNormal{};
        float satDepth = 0.0f, gjkDepth = 0.0f;
        CHECK(physlab::SATCollide(c.a, c.b, satNormal, satDepth));
        CHECK(physlab::GjkEpaCollide(c.a, c.b, gjkNormal, gjkDepth));

        const AxisReference ref = ReferenceDepths(c.a, c.b);
        CHECK(ref.hit);
        // 이 배치가 정말 '두 식이 갈리는' 배치인지부터 확인한다. 아니라면
        // 케이스 선정이 잘못된 것이고 회귀를 잡아내지 못한다.
        if (ref.push - ref.intersect <= 1e-2f) {
            std::cerr << "[not a containment case] " << c.name
                      << " push=" << ref.push
                      << " intersect=" << ref.intersect << '\n';
        }
        CHECK(ref.push - ref.intersect > 1e-2f);

        // 본 검사 — SAT 깊이가 EPA 깊이와 일치해야 한다.
        if (std::fabs(satDepth - gjkDepth) >= 1e-3f) {
            std::cerr << "[sat != epa] " << c.name << " sat=" << satDepth
                      << " epa=" << gjkDepth << " push=" << ref.push
                      << " intersect=" << ref.intersect << '\n';
        }
        CHECK(std::fabs(satDepth - gjkDepth) < 1e-3f);
        CHECK(std::fabs(satDepth - ref.push) < 1e-3f);
        CheckPushSeparates(c.a, c.b, satNormal, satDepth, c.name);
        CheckPushSeparates(c.a, c.b, gjkNormal, gjkDepth, c.name);

        worstGap    = std::max(worstGap, std::fabs(satDepth - gjkDepth));
        worstSpread = std::max(worstSpread, ref.push - ref.intersect);
    }
    std::cout << "  7 containment cases: max |sat-epa|=" << worstGap
              << ", max (mtd - intersection)=" << worstSpread << '\n';
}

// 결정적 LCG — 시드 고정이라 어디서든 같은 배치가 나온다.
uint32_t Lcg(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s;
}
float Rand01(uint32_t& s) {
    return (float)(Lcg(s) >> 8) / (float)(1u << 24);
}

// 무작위 회전 + 중심 오프셋 100 조합. hit/miss 는 항상 일치해야 하고, 깊이는
// 테스트 안의 독립 참조값과 대조한다.
void TestRandomizedAgreement() {
    uint32_t rng = 20260813u;
    int hits = 0, misses = 0, containment = 0;
    for (int k = 0; k < 100; ++k) {
        const float angleA = Rand01(rng) * 6.2831853f;
        const float angleB = Rand01(rng) * 6.2831853f;
        const float dir    = Rand01(rng) * 6.2831853f;
        const float dist   = Rand01(rng) * 3.0f;
        const auto  a = MakeBox(0.0f, 0.0f, angleA);
        const auto  b = MakeBox(std::cos(dir) * dist, std::sin(dir) * dist,
                                angleB);

        Vector2 satNormal{}, gjkNormal{};
        float satDepth = 0.0f, gjkDepth = 0.0f;
        const bool sat = physlab::SATCollide(a, b, satNormal, satDepth);
        const bool gjk = physlab::GjkEpaCollide(a, b, gjkNormal, gjkDepth);
        const AxisReference ref = ReferenceDepths(a, b);

        if (sat != gjk || sat != ref.hit) {
            std::cerr << "[disagree] k=" << k << " dist=" << dist
                      << " sat=" << sat << " gjk=" << gjk
                      << " ref=" << ref.hit << '\n';
        }
        CHECK(sat == gjk);
        CHECK(sat == ref.hit);
        if (!sat || !gjk) { ++misses; continue; }
        ++hits;

        // EPA 는 진짜 MTD 와 일치해야 한다.
        if (std::fabs(gjkDepth - ref.push) >= 1e-3f) {
            std::cerr << "[epa != mtd] k=" << k << " gjk=" << gjkDepth
                      << " ref.push=" << ref.push << '\n';
        }
        CHECK(std::fabs(gjkDepth - ref.push) < 1e-3f);

        // SAT 도 진짜 MTD 를 내야 한다 — 포함 배치도 예외가 아니다.
        if (std::fabs(satDepth - ref.push) >= 1e-3f) {
            std::cerr << "[sat depth] k=" << k << " sat=" << satDepth
                      << " intersect=" << ref.intersect
                      << " push=" << ref.push << '\n';
        }
        CHECK(std::fabs(satDepth - ref.push) < 1e-3f);
        CHECK(satDepth > 0.0f);

        // 두 구현이 *모든* hit 에서 붙어야 한다. 예전에는 포함 배치를 이 검사에서
        // 빼 뒀는데, 그게 바로 SAT 의 과소평가를 눈감아 주던 자리다.
        CHECK(std::fabs(satDepth - gjkDepth) < 0.05f);
        CheckPushSeparates(a, b, satNormal, satDepth, "randomized SAT");

        // 배치 표본이 정말 포함 케이스를 포함하는지 세어 둔다 (아래에서 검사).
        const bool isContainment = (ref.push - ref.intersect) > 1e-3f;
        if (isContainment) ++containment;

        const float dx = b.pos.x - a.pos.x;
        const float dy = b.pos.y - a.pos.y;
        if (std::fabs(dx) + std::fabs(dy) > 1e-3f) {
            CHECK(satNormal.x * dx + satNormal.y * dy > 0.0f);
        }
    }
    // 배치가 한쪽으로 쏠려 사실상 한 분기만 도는 것을 막는다.
    CHECK(hits > 40);
    CHECK(misses > 10);
    CHECK(containment > 0);
    std::cout << "  100 random placements: hits=" << hits
              << " misses=" << misses << " containment=" << containment
              << '\n';
}

const physlab::testkit::StepCase kCases[] = {
    {"step05", "separated_and_overlapping", TestSeparatedAndOverlappingBoxes},
    {"step05", "deep_overlap_reference",    TestDeepOverlapReference},
    {"step05", "sat_matches_epa",           TestSatMatchesEpaOnDeepOverlap},
    {"step05", "randomized_agreement",      TestRandomizedAgreement},
};

}  // namespace

int main(int argc, char** argv) {
    return physlab::testkit::RunStepCases(
        argc, argv, kCases, (int)(sizeof(kCases) / sizeof(kCases[0])),
        "physics collision", &failures);
}
