// 불변식 property 층 — 무작위 볼록쌍 배터리 + 무작위 강체 장면 배터리.
//
// 이 파일이 고정 사례 테스트와 다른 점은 *입력을 손으로 고르지 않는다* 는 것
// 하나뿐이다. 계약(사전조건 / 사후조건 / 불변식 / 허용 오차)을 적고, 생성기가
// 만든 입력 공간에서 그 계약을 깨는 반례를 찾는다. 찾으면 shrink 로 줄여 시드와
// 함께 찍는다 — 재현 명령 한 줄이 나오지 않으면 property 의 가치는 절반이다.
//
// oracle 은 셋이다.
//   - 독립 참조 구현 : `tests/sat_reference.h` 의 축 투영 참조 (differential)
//   - 수학적 property: 대칭성 · 강체 변환 불변 · 에너지 비증가 (metamorphic)
//   - 운영 불변식    : NaN/Inf 부재 · 관통 잔량 상계 · bit 결정성
//
// 검사 대상은 전부 제품 코드다. 볼록쌍은 `src/scenes/narrow_phase_scene.cpp` 의
// `SATCollide` / `GjkEpaCollide`, 강체 장면은 `src/core/contact_solver.h` 의
// 접촉 파이프라인 — 그리고 그 파이프라인이 정말 Response Scene 이 도는 것과 같은
// 프레임인지도 property 하나가 직접 대조한다.
//
// 실행:
//   physics_property_tests                     # 전체
//   physics_property_tests step05              # step 태그 필터
//   physics_property_tests convex_pair 12345   # 그 시드의 표본 하나만 (반례 재현)

#include "core/contact_solver.h"
#include "scenes/narrow_phase_scene.h"
#include "scenes/response_scene.h"

#include "property_kit.h"
#include "sat_reference.h"
#include "step_cases.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr)                                                          \
    do {                                                                     \
        if (!(expr)) {                                                       \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':'  \
                      << __LINE__ << ")\n";                                  \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

using physlab::testkit::AxisReference;
using physlab::testkit::Format;
using physlab::testkit::ReferenceDepths;
using physlab::testkit::Rng;
using physlab::testkit::RunProperty;

constexpr float kTau = 6.28318530718f;

// ─────────────────────────────────────────────────────────────────────────────
// 1. 볼록쌍 배터리
// ─────────────────────────────────────────────────────────────────────────────

// 생성기는 domain 구조를 알아야 한다. 무작위 점 n 개를 뽑아 볼록인 것만 남기면
// 대부분의 표본을 버리고 남은 것도 한쪽으로 쏠린다. 타원 위의 *정렬된* 각도
// n 개는 언제나 볼록 배치다 — constructive generator 라 버리는 표본이 없다.
// `aspect` 로 y 축을 눌러 얇은 도형까지 같은 방식으로 만든다.
struct ConvexShape {
    std::vector<float> angles;      // 오름차순, [0, 2π)
    float radius   = 1.0f;
    float aspect   = 1.0f;
    float rotation = 0.0f;
};

struct ConvexInput {
    int         bucket     = 0;
    ConvexShape a;
    ConvexShape b;
    float       offsetDir  = 0.0f;
    float       offsetDist = 0.0f;
};

enum ConvexBucket {
    kConvexGeneral = 0,
    kConvexBoundary,
    kConvexContainment,
    kConvexSliver,
    kConvexLargeScale,
    kConvexCorpus,
    kConvexBucketCount,
};

const char* const kConvexBucketNames[kConvexBucketCount] = {
    "general", "boundary", "containment", "sliver", "large-scale", "corpus",
};

int convexBucketHits[kConvexBucketCount] = {0, 0, 0, 0, 0, 0};

physlab::Polygon2D BuildPolygon(const ConvexShape& shape, Vector2 pos) {
    physlab::Polygon2D polygon;
    polygon.local.reserve(shape.angles.size());
    for (const float t : shape.angles) {
        polygon.local.push_back({std::cos(t) * shape.radius,
                                 std::sin(t) * shape.radius * shape.aspect});
    }
    polygon.pos   = pos;
    polygon.angle = shape.rotation;
    return polygon;
}

// 각도 *간격* 을 만들어 정규화한다. 균등 표본을 뽑아 정렬하면 최소 간격이라는
// 사전조건이 깨진 표본이 섞이고, 그걸 버리면 분포가 틀어진다.
// minGapRatio 가 작을수록 정점이 서로 붙는다 (퇴화 버킷이 쓴다).
ConvexShape GenerateShape(Rng& rng, int verts, float minGapRatio, float radius,
                          float aspect) {
    ConvexShape shape;
    shape.radius   = radius;
    shape.aspect   = aspect;
    shape.rotation = rng.Range(0.0f, kTau);

    std::vector<float> gaps((size_t)verts);
    float total = 0.0f;
    for (int i = 0; i < verts; ++i) {
        gaps[(size_t)i] = minGapRatio + (1.0f - minGapRatio) * rng.Unit();
        total += gaps[(size_t)i];
    }
    float t = 0.0f;
    for (int i = 0; i < verts; ++i) {
        shape.angles.push_back(t);
        t += gaps[(size_t)i] / total * kTau;
    }
    return shape;
}

// 정점이 (±half, ±half) 에 오는 정사각형. 회귀 corpus 가 쓴다.
ConvexShape Square(float half, float rotation) {
    ConvexShape shape;
    shape.radius   = half * 1.41421356f;
    shape.aspect   = 1.0f;
    shape.rotation = rotation;
    for (int i = 0; i < 4; ++i) {
        shape.angles.push_back(0.78539816f + (float)i * 1.57079633f);
    }
    return shape;
}

// 과거 실패에서 온 고정 배치. SAT 가 깊이를 *교집합 길이* 로 재던 시절 포함
// 배치에서만 EPA 와 갈라졌다 — 그 자리를 corpus 로 보존한다.
ConvexInput CorpusEntry(int index) {
    ConvexInput in;
    in.bucket = kConvexCorpus;
    switch (index % 4) {
    case 0:   // 같은 중심, 45도 — MTD 1+√2, 교집합 길이 2
        in.a = Square(1.0f, 0.0f);
        in.b = Square(1.0f, 0.78539816f);
        break;
    case 1:   // 큰 정사각형이 작은 것을 삼킨 배치
        in.a = Square(2.0f, 0.0f);
        in.b = Square(0.5f, 0.0f);
        break;
    case 2:   // 삼킨 배치 + 오프셋 + 회전
        in.a = Square(2.0f, 0.0f);
        in.b = Square(0.5f, 0.3f);
        in.offsetDir  = 0.6435011f;
        in.offsetDist = 0.5f;
        break;
    default:  // 경계 접촉 — 정확히 맞닿는다
        in.a = Square(1.0f, 0.0f);
        in.b = Square(1.0f, 0.0f);
        in.offsetDir  = 0.0f;
        in.offsetDist = 2.0f;
        break;
    }
    return in;
}

int PickConvexBucket(Rng& rng) {
    const float u = rng.Unit();
    if (u < 0.45f) return kConvexGeneral;
    if (u < 0.60f) return kConvexBoundary;
    if (u < 0.72f) return kConvexContainment;
    if (u < 0.84f) return kConvexSliver;
    if (u < 0.95f) return kConvexLargeScale;
    return kConvexCorpus;
}

ConvexInput GenerateConvexPair(Rng& rng) {
    ConvexInput in;
    in.bucket = PickConvexBucket(rng);
    ++convexBucketHits[in.bucket];

    switch (in.bucket) {
    case kConvexGeneral: {
        in.a = GenerateShape(rng, rng.Int(3, 8), 0.45f, rng.Range(0.6f, 1.6f),
                             rng.Range(0.5f, 1.0f));
        in.b = GenerateShape(rng, rng.Int(3, 8), 0.45f, rng.Range(0.6f, 1.6f),
                             rng.Range(0.5f, 1.0f));
        in.offsetDir  = rng.Range(0.0f, kTau);
        in.offsetDist = rng.Range(0.0f, 3.2f);
        break;
    }
    case kConvexBoundary: {
        // 두 도형이 *거의 정확히* 맞닿는 거리. hit / miss 판정이 갈리는 띠다.
        const float ra = rng.Range(0.7f, 1.4f);
        const float rb = rng.Range(0.7f, 1.4f);
        in.a = GenerateShape(rng, rng.Int(3, 7), 0.5f, ra, rng.Range(0.7f, 1.0f));
        in.b = GenerateShape(rng, rng.Int(3, 7), 0.5f, rb, rng.Range(0.7f, 1.0f));
        in.offsetDir  = rng.Range(0.0f, kTau);
        in.offsetDist = (ra + rb) * rng.Range(0.97f, 1.03f);
        break;
    }
    case kConvexContainment: {
        // 한 쪽이 다른 쪽을 통째로 품는 배치. 교집합 길이 식과 MTD 가 갈린다.
        in.a = GenerateShape(rng, rng.Int(3, 8), 0.5f, rng.Range(2.0f, 3.0f),
                             rng.Range(0.8f, 1.0f));
        in.b = GenerateShape(rng, rng.Int(3, 8), 0.5f, rng.Range(0.2f, 0.6f),
                             rng.Range(0.6f, 1.0f));
        in.offsetDir  = rng.Range(0.0f, kTau);
        in.offsetDist = rng.Range(0.0f, 0.6f);
        break;
    }
    case kConvexSliver: {
        // 얇은 조각 + 거의 붙은 정점 + 거의 평행한 두 도형.
        const float rotation = rng.Range(0.0f, kTau);
        in.a = GenerateShape(rng, rng.Int(3, 6), 0.08f, rng.Range(0.8f, 1.5f),
                             rng.Range(0.03f, 0.12f));
        in.b = GenerateShape(rng, rng.Int(3, 6), 0.08f, rng.Range(0.8f, 1.5f),
                             rng.Range(0.03f, 0.12f));
        in.a.rotation = rotation;
        in.b.rotation = rotation + rng.Range(-0.02f, 0.02f);
        in.offsetDir  = rng.Range(0.0f, kTau);
        in.offsetDist = rng.Range(0.0f, 1.6f);
        break;
    }
    case kConvexLargeScale: {
        // 화면 스케일 (반지름 60~140 px). 같은 상대 오차가 절대값으로 커진다.
        const float ra = rng.Range(60.0f, 140.0f);
        const float rb = rng.Range(60.0f, 140.0f);
        in.a = GenerateShape(rng, rng.Int(3, 8), 0.45f, ra, rng.Range(0.5f, 1.0f));
        in.b = GenerateShape(rng, rng.Int(3, 8), 0.45f, rb, rng.Range(0.5f, 1.0f));
        in.offsetDir  = rng.Range(0.0f, kTau);
        in.offsetDist = rng.Range(0.0f, (ra + rb) * 1.1f);
        break;
    }
    default:
        in = CorpusEntry((int)(rng.NextU64() % 4u));
        break;
    }
    return in;
}

std::string DumpShape(const ConvexShape& shape) {
    std::string text =
        Format("n=%d r=%.5f aspect=%.5f rot=%.5f th=[", (int)shape.angles.size(),
               (double)shape.radius, (double)shape.aspect,
               (double)shape.rotation);
    for (size_t i = 0; i < shape.angles.size(); ++i) {
        text += Format("%s%.5f", (i == 0) ? "" : " ", (double)shape.angles[i]);
    }
    return text + "]";
}

std::string DumpConvex(const ConvexInput& in) {
    return Format("bucket=%s offset(dir=%.5f dist=%.5f) A{%s} B{%s}",
                  kConvexBucketNames[in.bucket], (double)in.offsetDir,
                  (double)in.offsetDist, DumpShape(in.a).c_str(),
                  DumpShape(in.b).c_str());
}

physlab::Polygon2D TranslatedBy(const physlab::Polygon2D& p, Vector2 t) {
    physlab::Polygon2D r = p;
    r.pos = {p.pos.x + t.x, p.pos.y + t.y};
    return r;
}

// 같은 rigid transform 을 두 도형에 적용한다. 교차 여부와 침투 깊이는 유지되고
// 법선은 같은 각도만큼 돌아야 한다 — metamorphic relation.
physlab::Polygon2D RigidTransform(const physlab::Polygon2D& p, float theta,
                                  Vector2 t) {
    physlab::Polygon2D r = p;
    const float c = std::cos(theta), s = std::sin(theta);
    r.pos   = {c * p.pos.x - s * p.pos.y + t.x, s * p.pos.x + c * p.pos.y + t.y};
    r.angle = p.angle + theta;
    return r;
}

// 관측 통계 — 실패가 0 이면 표본과 정확히 일대일이다.
struct ConvexStats {
    int    hits = 0, misses = 0, containment = 0, uniqueAxis = 0;
    double worstSatVsMtd = 0.0;
    double worstEpaVsMtd = 0.0;
    double worstSatVsEpa = 0.0;
};
ConvexStats convexStats;

// (normal, depth) 계약의 양면. normal 로 depth 보다 조금 *더* 밀면 반드시
// 떨어지고, 조금 *덜* 밀면 반드시 아직 겹친다. 참조 구현만 쓰므로 SAT / EPA 어느
// 쪽도 자기 자신을 채점하지 않는다.
std::string CheckPushContract(const physlab::Polygon2D& a,
                              const physlab::Polygon2D& b, Vector2 normal,
                              float depth, float eps, const char* tag) {
    const AxisReference over = ReferenceDepths(
        a, TranslatedBy(b, {normal.x * (depth + eps), normal.y * (depth + eps)}));
    if (over.hit) {
        return Format("%s: 법선으로 depth+%.5f (=%.5f) 만큼 밀었는데 아직 겹친다",
                      tag, (double)eps, (double)(depth + eps));
    }
    if (depth > 2.0f * eps) {
        const AxisReference under = ReferenceDepths(
            a, TranslatedBy(b, {normal.x * (depth - eps),
                                normal.y * (depth - eps)}));
        if (!under.hit) {
            return Format("%s: depth-%.5f (=%.5f) 만 밀었는데 벌써 떨어졌다 "
                          "— 깊이 과대평가",
                          tag, (double)eps, (double)(depth - eps));
        }
    }
    return std::string();
}

std::string CheckConvexPair(const ConvexInput& in) {
    const physlab::Polygon2D a = BuildPolygon(in.a, {0.0f, 0.0f});
    const physlab::Polygon2D b = BuildPolygon(
        in.b, {std::cos(in.offsetDir) * in.offsetDist,
               std::sin(in.offsetDir) * in.offsetDist});

    // 허용 오차는 알고리즘의 계약에서 나온다. EPA 는 지지점이 현재 면까지의
    // 거리와 1e-4 안으로 붙으면 멈추고, float32 의 상대 오차는 좌표 크기에
    // 비례한다. 통과시키려고 키우는 값이 아니다.
    const float scale = std::max(in.a.radius, in.b.radius) + in.offsetDist;
    const float tol   = 2e-3f + 2e-4f * scale;

    Vector2 satNormal{}, epaNormal{};
    float   satDepth = 0.0f, epaDepth = 0.0f;
    const bool sat = physlab::SATCollide(a, b, satNormal, satDepth);
    const bool epa = physlab::GjkEpaCollide(a, b, epaNormal, epaDepth);
    const AxisReference ref = ReferenceDepths(a, b);

    if (sat != ref.hit) {
        return Format("SAT hit=%d, 독립 참조는 %d (참조 MTD %.5f)", (int)sat,
                      (int)ref.hit, (double)ref.push);
    }
    if (epa != ref.hit) {
        return Format("EPA hit=%d, 독립 참조는 %d (참조 MTD %.5f)", (int)epa,
                      (int)ref.hit, (double)ref.push);
    }
    if (!ref.hit) {
        ++convexStats.misses;
        return std::string();
    }
    ++convexStats.hits;
    if (ref.push - ref.intersect > 1e-3f) ++convexStats.containment;

    if (!std::isfinite(satDepth) || !std::isfinite(epaDepth)) {
        return Format("깊이가 유한하지 않다 (sat=%.5f epa=%.5f)", (double)satDepth,
                      (double)epaDepth);
    }
    if (satDepth < 0.0f) {
        return Format("SAT 깊이가 음수다 (%.5f)", (double)satDepth);
    }
    if (epaDepth <= 0.0f) {
        return Format("EPA 가 충돌이라면서 깊이 %.6f 를 냈다 (degenerate simplex)",
                      (double)epaDepth);
    }

    const float satLen =
        std::sqrt(satNormal.x * satNormal.x + satNormal.y * satNormal.y);
    const float epaLen =
        std::sqrt(epaNormal.x * epaNormal.x + epaNormal.y * epaNormal.y);
    if (std::fabs(satLen - 1.0f) > 1e-4f) {
        return Format("SAT 법선이 단위 길이가 아니다 (|n|=%.6f)", (double)satLen);
    }
    if (std::fabs(epaLen - 1.0f) > 1e-4f) {
        return Format("EPA 법선이 단위 길이가 아니다 (|n|=%.6f)", (double)epaLen);
    }

    // (a) differential oracle — 두 구현 각각이 독립 참조의 MTD 와 붙는가.
    // snippet-begin: property-convex-oracle
    convexStats.worstSatVsMtd =
        std::max(convexStats.worstSatVsMtd, (double)std::fabs(satDepth - ref.push));
    convexStats.worstEpaVsMtd =
        std::max(convexStats.worstEpaVsMtd, (double)std::fabs(epaDepth - ref.push));
    convexStats.worstSatVsEpa =
        std::max(convexStats.worstSatVsEpa, (double)std::fabs(satDepth - epaDepth));
    if (std::fabs(satDepth - ref.push) > tol) {
        return Format("SAT 깊이 %.5f 가 참조 MTD %.5f 와 %.5f 벌어졌다 "
                      "(허용 %.5f, 교집합 길이 %.5f)",
                      (double)satDepth, (double)ref.push,
                      (double)std::fabs(satDepth - ref.push), (double)tol,
                      (double)ref.intersect);
    }
    if (std::fabs(epaDepth - ref.push) > tol) {
        return Format("EPA 깊이 %.5f 가 참조 MTD %.5f 와 %.5f 벌어졌다 (허용 %.5f)",
                      (double)epaDepth, (double)ref.push,
                      (double)std::fabs(epaDepth - ref.push), (double)tol);
    }
    // snippet-end: property-convex-oracle

    // (b) (normal, depth) 계약의 양면.
    const float eps = 2.0f * tol + 1e-3f;
    std::string push = CheckPushContract(a, b, satNormal, satDepth, eps, "SAT");
    if (!push.empty()) return push;
    push = CheckPushContract(a, b, epaNormal, epaDepth, eps, "EPA");
    if (!push.empty()) return push;

    // (c) 대칭성 — 인자를 바꾸면 깊이는 같고 법선은 뒤집힌다.
    //     최소 축이 유일할 때만 법선을 본다. 정사각형처럼 여러 축이 같은 값을
    //     내는 배치에서는 어느 쪽을 골라도 계약 위반이 아니다.
    Vector2 swapNormal{};
    float   swapDepth = 0.0f;
    const bool swap = physlab::SATCollide(b, a, swapNormal, swapDepth);
    if (!swap) return "SAT(a,b) 는 충돌인데 SAT(b,a) 는 분리라고 답했다";
    if (std::fabs(swapDepth - satDepth) > tol) {
        return Format("SAT 깊이가 인자 순서에 따라 다르다 (%.5f vs %.5f)",
                      (double)satDepth, (double)swapDepth);
    }
    const bool axisIsUnique = (ref.push2 - ref.push) > 4.0f * tol;
    if (axisIsUnique) {
        ++convexStats.uniqueAxis;
        const float dot =
            satNormal.x * swapNormal.x + satNormal.y * swapNormal.y;
        if (dot > -0.99f) {
            return Format("최소 축이 유일한데(2nd %.5f vs %.5f) SAT(b,a) 법선이 "
                          "뒤집히지 않았다 (n·n' = %.5f)",
                          (double)ref.push2, (double)ref.push, (double)dot);
        }
    }

    // (d) 강체 변환 불변 — 같은 회전·평행이동을 둘 다에 적용해도 교차 여부와
    //     깊이는 그대로고, 법선은 같은 각도만큼 돈다.
    const float theta = 0.7853981634f;
    const Vector2 shift{scale * 1.5f, -scale * 0.75f};
    const physlab::Polygon2D ta = RigidTransform(a, theta, shift);
    const physlab::Polygon2D tb = RigidTransform(b, theta, shift);
    Vector2 movedNormal{};
    float   movedDepth = 0.0f;
    const bool moved = physlab::SATCollide(ta, tb, movedNormal, movedDepth);
    const float movedTol = 2e-3f + 2e-4f * (scale * 3.0f);
    if (moved != true) {
        return "같은 rigid transform 을 두 도형에 적용했더니 충돌이 사라졌다";
    }
    if (std::fabs(movedDepth - satDepth) > movedTol) {
        return Format("rigid transform 뒤 깊이가 %.5f -> %.5f 로 바뀌었다 (허용 %.5f)",
                      (double)satDepth, (double)movedDepth, (double)movedTol);
    }
    if (axisIsUnique) {
        const float c = std::cos(theta), s = std::sin(theta);
        const Vector2 expected{c * satNormal.x - s * satNormal.y,
                               s * satNormal.x + c * satNormal.y};
        const float dot =
            expected.x * movedNormal.x + expected.y * movedNormal.y;
        if (dot < 0.99f) {
            return Format("rigid transform 뒤 법선이 같이 돌지 않았다 (n·n' = %.5f)",
                          (double)dot);
        }
    }

    Vector2 movedEpaNormal{};
    float   movedEpaDepth = 0.0f;
    if (!physlab::GjkEpaCollide(ta, tb, movedEpaNormal, movedEpaDepth)) {
        return "rigid transform 뒤 EPA 만 분리라고 답했다";
    }
    if (std::fabs(movedEpaDepth - epaDepth) > movedTol) {
        return Format("rigid transform 뒤 EPA 깊이가 %.5f -> %.5f 로 바뀌었다",
                      (double)epaDepth, (double)movedEpaDepth);
    }
    return std::string();
}

// shrink 후보. 사전조건(볼록 · 정점 3개 이상)을 깨는 축소는 만들지 않는다 —
// 축소 뒤 사전조건이 깨지면 실패의 원인이 다른 것으로 바뀐다.
std::vector<ConvexInput> ShrinkConvex(const ConvexInput& in) {
    std::vector<ConvexInput> out;

    auto regular = [](int n) {
        std::vector<float> angles;
        for (int i = 0; i < n; ++i) {
            angles.push_back((float)i / (float)n * kTau);
        }
        return angles;
    };

    if (in.a.angles.size() > 3) {
        ConvexInput c = in;
        c.a.angles.erase(c.a.angles.begin() +
                         (long long)(c.a.angles.size() / 2));
        out.push_back(c);
    }
    if (in.b.angles.size() > 3) {
        ConvexInput c = in;
        c.b.angles.erase(c.b.angles.begin() +
                         (long long)(c.b.angles.size() / 2));
        out.push_back(c);
    }
    if (in.a.angles != regular((int)in.a.angles.size())) {
        ConvexInput c = in;
        c.a.angles = regular((int)in.a.angles.size());
        out.push_back(c);
    }
    if (in.b.angles != regular((int)in.b.angles.size())) {
        ConvexInput c = in;
        c.b.angles = regular((int)in.b.angles.size());
        out.push_back(c);
    }
    if (in.a.aspect != 1.0f) {
        ConvexInput c = in;
        c.a.aspect = 1.0f;
        out.push_back(c);
    }
    if (in.b.aspect != 1.0f) {
        ConvexInput c = in;
        c.b.aspect = 1.0f;
        out.push_back(c);
    }
    if (in.a.rotation != 0.0f) {
        ConvexInput c = in;
        c.a.rotation = 0.0f;
        out.push_back(c);
    }
    if (in.b.rotation != 0.0f) {
        ConvexInput c = in;
        c.b.rotation = 0.0f;
        out.push_back(c);
    }
    if (in.offsetDist != 0.0f) {
        ConvexInput zero = in;
        zero.offsetDist = 0.0f;
        out.push_back(zero);
        ConvexInput half = in;
        half.offsetDist *= 0.5f;
        out.push_back(half);
    }
    if (in.offsetDir != 0.0f) {
        ConvexInput c = in;
        c.offsetDir = 0.0f;
        out.push_back(c);
    }
    if (in.a.radius != 1.0f || in.b.radius != 1.0f) {
        ConvexInput c = in;
        const float scale = std::max(in.a.radius, in.b.radius);
        c.a.radius /= scale;
        c.b.radius /= scale;
        c.offsetDist /= scale;
        out.push_back(c);
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. 강체 장면 배터리
// ─────────────────────────────────────────────────────────────────────────────

constexpr float kDt = 1.0f / 120.0f;

struct SceneInput {
    int bucket     = 0;
    int canvasW    = 400;
    int canvasH    = 320;
    int iterations = 8;
    std::vector<physlab::CircleRb> bodies;
};

enum SceneBucket {
    kSceneGeneral = 0,
    kSceneResting,
    kSceneMassSpread,
    kSceneDense,
    kSceneFast,
    kSceneDegenerate,
    kSceneBucketCount,
};

const char* const kSceneBucketNames[kSceneBucketCount] = {
    "general", "resting", "mass-spread", "dense", "fast", "degenerate",
};

// property 마다 자기 히스토그램을 든다. 한 배열을 공유하면 앞선 property 가
// 뒤의 커버리지를 채워 주고, 그러면 "이 property 가 어느 분기를 봤는가" 라는
// 질문에 답할 수 없다.
int energySceneBuckets[kSceneBucketCount]  = {0, 0, 0, 0, 0, 0};
int settleSceneBuckets[kSceneBucketCount]  = {0, 0, 0, 0, 0, 0};
int determinismBuckets[kSceneBucketCount]  = {0, 0, 0, 0, 0, 0};
int mirrorSceneBuckets[kSceneBucketCount]  = {0, 0, 0, 0, 0, 0};

physlab::WorldBounds BoundsOf(const SceneInput& in) {
    return physlab::BoundsFromCanvas(0, 0, in.canvasW, in.canvasH);
}

// Step 6 의 SequentialImpulse 분기와 같은 네 줄. property 층이 자기만의 프레임을
// 들고 있으면 여기서 하는 모든 단언은 *사본* 에 대한 단언이 된다.
// snippet-begin: property-frame
void PropertyFrame(std::vector<physlab::CircleRb>& bodies,
                   const physlab::WorldBounds& bounds,
                   const physlab::SolverParams& params, float gravity,
                   int iterations, float dt) {
    physlab::core::IntegrateSemiImplicitEuler(bodies, gravity, dt);
    std::vector<physlab::Contact> contacts;
    physlab::core::GatherCircleContacts(bodies, bounds, contacts);
    physlab::core::SolveSequentialImpulse(bodies, contacts, params, iterations);
    physlab::core::SolvePositionCorrection(bodies, bounds, params,
                                           iterations / 2);
}
// snippet-end: property-frame

// contact 처리 순서를 매 iter 한 칸씩 돌린다. 결정성 property 의 음성 대조군 —
// 순서를 바꾸면 Gauss-Seidel 풀이가 갈라진다는 사실을 같은 배터리로 확인한다.
struct RotateContactOrder {
    void operator()(std::vector<int>& order) const {
        if (order.size() > 1) {
            std::rotate(order.begin(), order.begin() + 1, order.end());
        }
    }
};

void RotatedFrame(std::vector<physlab::CircleRb>& bodies,
                  const physlab::WorldBounds& bounds,
                  const physlab::SolverParams& params, float gravity,
                  int iterations, float dt) {
    physlab::core::IntegrateSemiImplicitEuler(bodies, gravity, dt);
    std::vector<physlab::Contact> contacts;
    physlab::core::GatherCircleContacts(bodies, bounds, contacts);
    physlab::core::SolveSequentialImpulse(bodies, contacts, params, iterations,
                                          RotateContactOrder{});
    physlab::core::SolvePositionCorrection(bodies, bounds, params,
                                           iterations / 2);
}

double KineticEnergy(const std::vector<physlab::CircleRb>& bodies) {
    double energy = 0.0;
    for (const auto& b : bodies) {
        if (b.invMass <= 0.0f) continue;
        const double mass = 1.0 / (double)b.invMass;
        energy += 0.5 * mass *
                  ((double)b.vel.x * (double)b.vel.x +
                   (double)b.vel.y * (double)b.vel.y);
    }
    return energy;
}

struct SceneMetrics {
    bool   finite            = true;
    int    contacts          = 0;
    float  maxPenetration    = 0.0f;
    float  avgPenetration    = 0.0f;
    float  maxRelativeDepth  = 0.0f;   // depth / min(r_i, r_j)
    float  worstApproach     = 0.0f;   // 가장 빠른 접근 (음수)
    double kinetic           = 0.0;
};

SceneMetrics Measure(const std::vector<physlab::CircleRb>& bodies,
                     const physlab::WorldBounds& bounds) {
    SceneMetrics m;
    for (const auto& b : bodies) {
        if (!std::isfinite(b.pos.x) || !std::isfinite(b.pos.y) ||
            !std::isfinite(b.vel.x) || !std::isfinite(b.vel.y)) {
            m.finite = false;
        }
    }
    if (!m.finite) return m;

    std::vector<physlab::Contact> contacts;
    physlab::core::GatherCircleContacts(bodies, bounds, contacts);
    m.contacts = (int)contacts.size();
    float sum = 0.0f;
    for (const auto& c : contacts) {
        const float radius =
            (c.j >= 0) ? std::min(bodies[(size_t)c.i].radius,
                                  bodies[(size_t)c.j].radius)
                       : bodies[(size_t)c.i].radius;
        sum = sum + c.depth;
        m.maxPenetration = std::max(m.maxPenetration, c.depth);
        if (radius > 0.0f) {
            m.maxRelativeDepth = std::max(m.maxRelativeDepth, c.depth / radius);
        }
        const float vn =
            (c.j >= 0)
                ? physlab::core::DotV(
                      physlab::core::SubV(bodies[(size_t)c.j].vel,
                                          bodies[(size_t)c.i].vel), c.normal)
                : physlab::core::DotV(bodies[(size_t)c.i].vel, c.normal);
        m.worstApproach = std::min(m.worstApproach, vn);
    }
    m.avgPenetration = contacts.empty() ? 0.0f : sum / (float)contacts.size();
    m.kinetic = KineticEnergy(bodies);
    return m;
}

int PickSceneBucket(Rng& rng) {
    const float u = rng.Unit();
    if (u < 0.40f) return kSceneGeneral;
    if (u < 0.55f) return kSceneResting;
    if (u < 0.70f) return kSceneMassSpread;
    if (u < 0.82f) return kSceneDense;
    if (u < 0.95f) return kSceneFast;
    return kSceneDegenerate;
}

// 무작위 배치는 겹침을 만든다. 사전조건(초기 상대 침투 ≤ 0.5)을 만족하는 배치를
// *직접 구성* 한다 — 뽑고 버리면 밀도가 높은 버킷에서 표본을 거의 다 버린다.
void SeparatePlacement(std::vector<physlab::CircleRb>& bodies,
                       const physlab::WorldBounds& bounds, float allowedRatio,
                       int passes) {
    for (int pass = 0; pass < passes; ++pass) {
        for (size_t i = 1; i < bodies.size(); ++i) {
            for (size_t j = 0; j < i; ++j) {
                const float dx = bodies[i].pos.x - bodies[j].pos.x;
                const float dy = bodies[i].pos.y - bodies[j].pos.y;
                const float minRadius =
                    std::min(bodies[i].radius, bodies[j].radius);
                const float wanted = bodies[i].radius + bodies[j].radius -
                                     allowedRatio * minRadius;
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (distance >= wanted) continue;
                const physlab::core::UnitNormal2 n = physlab::core::SafeUnitNormal(
                    dx, dy, (int)(i + j));
                bodies[i].pos = {bodies[j].pos.x + n.x * wanted,
                                 bodies[j].pos.y + n.y * wanted};
            }
        }
        for (auto& b : bodies) {
            const float slack = allowedRatio * b.radius;
            b.pos.x = std::clamp(b.pos.x, bounds.left + b.radius - slack,
                                 bounds.right - b.radius + slack);
            b.pos.y = std::min(b.pos.y, bounds.floorY - b.radius + slack);
            b.prevPos = b.pos;
        }
    }
}

SceneInput GenerateScene(Rng& rng, int* bucketHistogram) {
    SceneInput in;
    in.bucket = PickSceneBucket(rng);
    ++bucketHistogram[in.bucket];

    int   count       = 8;
    float radiusLo    = 10.0f, radiusHi = 18.0f;
    float speed       = 300.0f;
    float invMassLo   = 1.0f, invMassHi = 1.0f;
    int   staticCount = 0;
    float allowed     = 0.5f;

    switch (in.bucket) {
    case kSceneGeneral:
        count = rng.Int(4, 14);
        break;
    case kSceneResting:
        count = rng.Int(4, 14);
        speed = 0.0f;
        break;
    case kSceneMassSpread:
        count     = rng.Int(4, 12);
        invMassLo = 0.2f;
        invMassHi = 5.0f;
        staticCount = rng.Int(0, 2);
        break;
    case kSceneDense:
        count    = rng.Int(14, 22);
        radiusLo = 10.0f;
        radiusHi = 14.0f;
        break;
    case kSceneFast:
        // dt·v 가 반지름을 넘으면 한 프레임에 상대를 지나칠 수 있다. 자유낙하로
        // 얻는 속도까지 더해도 (300 + √(2gH) ≈ 920 px/s) 프레임당 이동은 7.7 px —
        // 반지름 14 px 아래로 내려가지 않게 잡아 tunneling 을 사전조건에서 막는다.
        count    = rng.Int(4, 12);
        radiusLo = 14.0f;
        radiusHi = 20.0f;
        speed    = 600.0f;
        break;
    default:
        // 퇴화 — 거의 같은 위치의 두 body (SafeUnitNormal 의 fallback 경로),
        // 반지름 격차가 큰 쌍.
        count   = rng.Int(2, 5);
        allowed = 1.6f;
        break;
    }

    const physlab::WorldBounds bounds = BoundsOf(in);
    for (int i = 0; i < count; ++i) {
        physlab::CircleRb body;
        body.radius = rng.Range(radiusLo, radiusHi);
        body.pos    = {rng.Range(bounds.left + body.radius,
                                 bounds.right - body.radius),
                       rng.Range(4.0f + body.radius,
                                 bounds.floorY - body.radius)};
        if (in.bucket == kSceneDegenerate && i > 0) {
            body.pos = {in.bodies[0].pos.x + rng.Range(-1.0f, 1.0f),
                        in.bodies[0].pos.y + rng.Range(-1.0f, 1.0f)};
            body.radius = rng.Range(4.0f, 24.0f);
        }
        body.prevPos = body.pos;
        body.vel     = {rng.Range(-speed, speed), rng.Range(-speed, speed)};
        body.invMass = rng.Range(invMassLo, invMassHi);
        if (i < staticCount) {
            body.invMass = 0.0f;
            body.vel     = {0.0f, 0.0f};
        }
        body.mass = (body.invMass > 0.0f) ? 1.0f / body.invMass : 0.0f;
        in.bodies.push_back(body);
    }
    if (in.bucket != kSceneDegenerate) {
        SeparatePlacement(in.bodies, bounds, allowed, 3);
    }
    return in;
}

// 중력을 끈 배터리에서 정지 장면은 접촉을 하나도 만들지 않는다 — 소산을 검사할
// 것이 없는 공허한 표본이다. 에너지 property 의 생성기는 최소 속도를 사전조건에
// 넣는다. 유효·무효 분포를 섞어 놓고 어떤 계약을 검사하는지 흐리지 않는다.
SceneInput GenerateMovingScene(Rng& rng, int* bucketHistogram) {
    SceneInput in = GenerateScene(rng, bucketHistogram);
    for (auto& b : in.bodies) {
        if (b.invMass <= 0.0f) continue;
        const float speed =
            std::sqrt(b.vel.x * b.vel.x + b.vel.y * b.vel.y);
        if (speed >= 60.0f) continue;
        const float direction = rng.Range(0.0f, kTau);
        b.vel = {std::cos(direction) * 120.0f, std::sin(direction) * 120.0f};
    }
    return in;
}

std::string DumpScene(const SceneInput& in) {
    std::string text = Format("bucket=%s canvas=%dx%d iters=%d bodies=%d",
                              kSceneBucketNames[in.bucket], in.canvasW,
                              in.canvasH, in.iterations, (int)in.bodies.size());
    for (size_t i = 0; i < in.bodies.size() && i < 24; ++i) {
        const auto& b = in.bodies[i];
        text += Format("\n              [%d] pos=(%.4f,%.4f) vel=(%.4f,%.4f) "
                       "r=%.4f invMass=%.4f",
                       (int)i, (double)b.pos.x, (double)b.pos.y,
                       (double)b.vel.x, (double)b.vel.y, (double)b.radius,
                       (double)b.invMass);
    }
    return text;
}

// 장면 shrink — 구간 삭제 delta debugging 의 최소판. body 를 하나씩 빼 보고
// 실패가 유지되면 채택한다. 그 다음 속도를 0 으로, 위치를 격자로 단순화한다.
std::vector<SceneInput> ShrinkScene(const SceneInput& in) {
    std::vector<SceneInput> out;
    if (in.bodies.size() > 2) {
        // 앞 절반 / 뒤 절반 삭제부터 — 한 개씩 빼는 것보다 빨리 줄어든다.
        SceneInput half = in;
        half.bodies.resize(in.bodies.size() / 2);
        out.push_back(half);
        SceneInput tail = in;
        tail.bodies.erase(tail.bodies.begin(),
                          tail.bodies.begin() +
                              (long long)(in.bodies.size() / 2));
        out.push_back(tail);
        for (size_t i = 0; i < in.bodies.size(); ++i) {
            SceneInput c = in;
            c.bodies.erase(c.bodies.begin() + (long long)i);
            out.push_back(c);
        }
    }
    bool moving = false;
    for (const auto& b : in.bodies) {
        if (b.vel.x != 0.0f || b.vel.y != 0.0f) moving = true;
    }
    if (moving) {
        SceneInput c = in;
        for (auto& b : c.bodies) b.vel = {0.0f, 0.0f};
        out.push_back(c);
        SceneInput halfSpeed = in;
        for (auto& b : halfSpeed.bodies) {
            b.vel = {b.vel.x * 0.5f, b.vel.y * 0.5f};
        }
        out.push_back(halfSpeed);
    }
    // 위치·반지름을 정수로 — 반례를 fixture 로 옮겨 적기 쉬워진다.
    SceneInput rounded = in;
    bool changed = false;
    for (auto& b : rounded.bodies) {
        const Vector2 snapped{std::round(b.pos.x), std::round(b.pos.y)};
        const float   radius = std::round(b.radius);
        if (snapped.x != b.pos.x || snapped.y != b.pos.y ||
            radius != b.radius) {
            changed = true;
        }
        b.pos     = snapped;
        b.prevPos = snapped;
        b.radius  = std::max(1.0f, radius);
    }
    if (changed) out.push_back(rounded);
    return out;
}

// ── property 1: 에너지 비증가 ────────────────────────────────────────────────
// 감쇠 규칙을 먼저 못 박는다. 중력 0 · 반발계수 0 이면 이 파이프라인에서 운동
// 에너지를 *올릴* 수 있는 항이 없다.
//   - semi-implicit Euler 는 중력이 0 이므로 속도를 건드리지 않는다.
//   - 위치 보정은 위치만 옮긴다. 중력이 없으니 위치에너지도 없다.
//   - 접촉 임펄스 하나의 에너지 변화는 a·vn + ½a²w 이고, a = -vn/w (또는 누적
//     λ 를 0 으로 되돌리는 clamp) 이므로 언제나 ≤ 0 이다.
// 중력을 켜면 위치 보정이 물체를 중력에 거슬러 밀어 올리므로 이 계약은 성립하지
// 않는다 — 그래서 이 property 는 중력 0 에서만 단언한다.
constexpr int kEnergyFrames = 180;

struct EnergyObservation {
    double worstGrowthRatio = 0.0;
    double worstDropRatio   = 1.0;
    int    scenesWithImpact = 0;
};
EnergyObservation energyObservation;

std::string CheckEnergyDissipation(const SceneInput& in) {
    std::vector<physlab::CircleRb> bodies = in.bodies;
    const physlab::WorldBounds bounds = BoundsOf(in);
    physlab::SolverParams params;
    params.restitution = 0.0f;   // 감쇠 규칙 — 반발을 끄면 접촉은 소산만 한다

    const double initial = KineticEnergy(bodies);
    double previous = initial;
    bool   impacted = false;

    for (int frame = 0; frame < kEnergyFrames; ++frame) {
        // 충돌이 *일어나는 순간* 을 프레임 시작 시점의 접촉 집합으로 보면 놓친다.
        // 그 시점엔 아직 닿지 않았고, 닿은 다음 프레임엔 이미 임펄스가 접근
        // 속도를 없앤 뒤다. 그래서 적분만 한 사본으로 이번 프레임이 풀 접촉을
        // 미리 본다 — 중력이 0 이라 적분은 속도를 건드리지 않는다.
        std::vector<physlab::CircleRb> probe = bodies;
        physlab::core::IntegrateSemiImplicitEuler(probe, 0.0f, kDt);
        if (Measure(probe, bounds).worstApproach < -5.0f) impacted = true;

        PropertyFrame(bodies, bounds, params, 0.0f, in.iterations, kDt);

        const SceneMetrics after = Measure(bodies, bounds);
        if (!after.finite) {
            return Format("프레임 %d 에서 NaN/Inf 가 나왔다", frame);
        }
        if (previous > 0.0) {
            energyObservation.worstGrowthRatio = std::max(
                energyObservation.worstGrowthRatio, after.kinetic / previous);
        }
        if (after.kinetic > previous * (1.0 + 1e-6) + 1e-6) {
            return Format("프레임 %d 에서 운동에너지가 %.9g -> %.9g 로 늘었다 "
                          "(접촉 %d 개)",
                          frame, previous, after.kinetic, after.contacts);
        }
        previous = after.kinetic;
    }

    // property 의 강도. '비증가' 만 보면 *아무것도 하지 않는* 솔버도 통과한다.
    // 실제로 부딪힌 장면이면 에너지는 반드시 줄어 있어야 한다.
    if (impacted) {
        ++energyObservation.scenesWithImpact;
        if (initial > 0.0) {
            energyObservation.worstDropRatio =
                std::min(energyObservation.worstDropRatio, previous / initial);
        }
        if (!(previous < initial)) {
            return Format("접근 속도 5 px/s 를 넘는 충돌이 있었는데 운동에너지가 "
                          "%.9g -> %.9g 로 그대로다",
                          initial, previous);
        }
    }
    return std::string();
}

// ── property 2: 관통 잔량 상계 · 유한성 · 정착 ───────────────────────────────
constexpr int kSettleFrames = 600;

struct SettleObservation {
    float worstRelativeDepth = 0.0f;
    float worstSettledAvg    = 0.0f;
    float worstSettledMax    = 0.0f;
    double worstEnergyRatio  = 0.0;
};
SettleObservation settleObservation;

std::string CheckPenetrationBound(const SceneInput& in) {
    std::vector<physlab::CircleRb> bodies = in.bodies;
    const physlab::WorldBounds bounds = BoundsOf(in);
    const physlab::SolverParams params;   // Step 6 의 기본 파라미터 그대로

    // 에너지 상계. 중력을 켜면 위치에너지가 운동에너지로 바뀔 수 있지만, 낙하
    // 가능한 높이는 캔버스 높이를 넘지 못한다. 임펄스도 위치 보정도 에너지를
    // 넣지 않으므로 KE ≤ KE₀ + g·H·Σm 이다.
    double totalMass = 0.0;
    for (const auto& b : bodies) {
        if (b.invMass > 0.0f) totalMass += 1.0 / (double)b.invMass;
    }
    const double energyBound =
        KineticEnergy(bodies) +
        (double)physlab::kDefaultGravity * (double)in.canvasH * totalMass;

    const SceneMetrics start = Measure(bodies, bounds);
    const float allowedDepth = std::max(1.0f, start.maxRelativeDepth);

    SceneMetrics last;
    for (int frame = 0; frame < kSettleFrames; ++frame) {
        PropertyFrame(bodies, bounds, params, physlab::kDefaultGravity,
                      in.iterations, kDt);
        last = Measure(bodies, bounds);
        if (!last.finite) {
            return Format("프레임 %d 에서 NaN/Inf 가 나왔다", frame);
        }
        settleObservation.worstRelativeDepth =
            std::max(settleObservation.worstRelativeDepth, last.maxRelativeDepth);
        if (energyBound > 0.0) {
            settleObservation.worstEnergyRatio = std::max(
                settleObservation.worstEnergyRatio, last.kinetic / energyBound);
        }
        if (last.maxRelativeDepth > allowedDepth + 1e-3f) {
            return Format("프레임 %d 에서 상대 침투가 %.5f 로 초기값 %.5f 를 "
                          "넘었다 (절대 %.5f px)",
                          frame, (double)last.maxRelativeDepth,
                          (double)allowedDepth, (double)last.maxPenetration);
        }
        if (last.kinetic > energyBound * 1.001 + 1e-3) {
            return Format("프레임 %d 에서 운동에너지 %.9g 가 상계 %.9g 를 넘었다",
                          frame, last.kinetic, energyBound);
        }
    }

    settleObservation.worstSettledAvg =
        std::max(settleObservation.worstSettledAvg, last.avgPenetration);
    settleObservation.worstSettledMax =
        std::max(settleObservation.worstSettledMax, last.maxPenetration);

    // 정착 뒤 계약 — 고정 워크로드의 iteration 수렴 검사가 쓰는 것과 같은 값
    // (slop + 1 px). 무작위 배치로 그 계약을 확장한다.
    if (last.avgPenetration > params.slop + 1.0f) {
        return Format("%d 프레임 뒤에도 평균 침투가 %.5f px 다 (slop %.2f + 1)",
                      kSettleFrames, (double)last.avgPenetration,
                      (double)params.slop);
    }
    if (last.maxRelativeDepth > 1.0f) {
        return Format("%d 프레임 뒤에도 상대 침투가 %.5f 다 (반지름을 넘는 겹침)",
                      kSettleFrames, (double)last.maxRelativeDepth);
    }
    return std::string();
}

// ── property 3: 같은 시드 두 번 실행이 bit 동일 ──────────────────────────────
constexpr int kDeterminismFrames = 300;

bool BitIdentical(const std::vector<physlab::CircleRb>& a,
                  const std::vector<physlab::CircleRb>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::memcmp(&a[i].pos, &b[i].pos, sizeof(Vector2)) != 0) return false;
        if (std::memcmp(&a[i].vel, &b[i].vel, sizeof(Vector2)) != 0) return false;
    }
    return true;
}

float WorstDelta(const std::vector<physlab::CircleRb>& a,
                 const std::vector<physlab::CircleRb>& b) {
    float worst = 0.0f;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        worst = std::max(worst, std::fabs(a[i].pos.x - b[i].pos.x));
        worst = std::max(worst, std::fabs(a[i].pos.y - b[i].pos.y));
    }
    return worst;
}

float WorstVelocityDelta(const std::vector<physlab::CircleRb>& a,
                         const std::vector<physlab::CircleRb>& b) {
    float worst = 0.0f;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        worst = std::max(worst, std::fabs(a[i].vel.x - b[i].vel.x));
        worst = std::max(worst, std::fabs(a[i].vel.y - b[i].vel.y));
    }
    return worst;
}

std::vector<physlab::CircleRb> RunFrames(const SceneInput& in, int frames,
                                         bool rotateOrder) {
    std::vector<physlab::CircleRb> bodies = in.bodies;
    const physlab::WorldBounds bounds = BoundsOf(in);
    const physlab::SolverParams params;
    for (int frame = 0; frame < frames; ++frame) {
        if (rotateOrder) {
            RotatedFrame(bodies, bounds, params, physlab::kDefaultGravity,
                         in.iterations, kDt);
        } else {
            PropertyFrame(bodies, bounds, params, physlab::kDefaultGravity,
                          in.iterations, kDt);
        }
    }
    return bodies;
}

int determinismDiverged = 0;
int determinismContended = 0;
float determinismWorstShuffleDelta = 0.0f;

std::string CheckBitExactRepeat(const SceneInput& in) {
    const auto first  = RunFrames(in, kDeterminismFrames, false);
    const auto second = RunFrames(in, kDeterminismFrames, false);
    if (!BitIdentical(first, second)) {
        return Format("같은 입력을 두 번 돌렸는데 bit 가 갈렸다 "
                      "(위치 최대 %.9g px, 속도 최대 %.9g px/s)",
                      (double)WorstDelta(first, second),
                      (double)WorstVelocityDelta(first, second));
    }
    for (const auto& b : first) {
        if (!std::isfinite(b.pos.x) || !std::isfinite(b.pos.y)) {
            return "결정적이긴 한데 위치가 NaN/Inf 다";
        }
    }

    // 음성 대조군. 접촉이 둘 이상 얽힌 장면이면 순서를 바꿨을 때 갈라져야 한다 —
    // 갈라지지 않으면 위의 bit 동일은 '아무 일도 안 일어났다' 는 뜻일 수 있다.
    // snippet-begin: property-negative-control
    const physlab::WorldBounds bounds = BoundsOf(in);
    std::vector<physlab::Contact> contacts;
    physlab::core::GatherCircleContacts(first, bounds, contacts);
    if (contacts.size() >= 2) {
        ++determinismContended;
        const auto rotated = RunFrames(in, kDeterminismFrames, true);
        const float delta = WorstDelta(first, rotated);
        determinismWorstShuffleDelta =
            std::max(determinismWorstShuffleDelta, delta);
        if (delta > 1e-4f) ++determinismDiverged;
    }
    // snippet-end: property-negative-control
    return std::string();
}

// ── property 4: property 층의 프레임 == Response Scene 의 프레임 ─────────────
constexpr int kMirrorFrames = 120;

float mirrorWorstPosDelta = 0.0f;
float mirrorWorstVelDelta = 0.0f;

std::string CheckFrameMatchesResponseScene(const SceneInput& in) {
    const auto mine = RunFrames(in, kMirrorFrames, false);

    physlab::ResponseScene scene;
    scene.SetCanvas(0, 0, in.canvasW, in.canvasH);
    scene.SetAlgo(physlab::ResponseAlgo::SequentialImpulse);
    scene.SetIterations(in.iterations);
    scene.SetBodies(in.bodies);
    for (int frame = 0; frame < kMirrorFrames; ++frame) scene.Step(kDt);

    const auto& theirs = scene.Bodies();
    if (theirs.size() != mine.size()) {
        return Format("body 수가 다르다 (%d vs %d)", (int)mine.size(),
                      (int)theirs.size());
    }
    float worstPos = 0.0f, worstVel = 0.0f;
    for (size_t i = 0; i < mine.size(); ++i) {
        worstPos = std::max(worstPos, std::fabs(mine[i].pos.x - theirs[i].pos.x));
        worstPos = std::max(worstPos, std::fabs(mine[i].pos.y - theirs[i].pos.y));
        worstVel = std::max(worstVel, std::fabs(mine[i].vel.x - theirs[i].vel.x));
        worstVel = std::max(worstVel, std::fabs(mine[i].vel.y - theirs[i].vel.y));
    }
    mirrorWorstPosDelta = std::max(mirrorWorstPosDelta, worstPos);
    mirrorWorstVelDelta = std::max(mirrorWorstVelDelta, worstVel);
    if (worstPos != 0.0f || worstVel != 0.0f) {
        return Format("property 층의 프레임이 Response Scene 과 갈라졌다 "
                      "(pos %.9g px, vel %.9g px/s)",
                      (double)worstPos, (double)worstVel);
    }
    return std::string();
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. 케이스
// ─────────────────────────────────────────────────────────────────────────────

void ReportBuckets(const char* label, const char* const* names,
                   const int* counts, int bucketCount) {
    std::cout << "  " << label << " 버킷:";
    for (int i = 0; i < bucketCount; ++i) {
        std::cout << ' ' << names[i] << '=' << counts[i];
    }
    std::cout << '\n';
}

constexpr int kConvexSamples = 1500;

void TestConvexPairProperty() {
    const auto result = RunProperty<ConvexInput>(
        "convex_pair", "무작위 볼록쌍: SAT 깊이 = EPA 깊이 = 참조 MTD, "
                       "(normal, depth) 양면 계약, 대칭성, 강체 변환 불변",
        0x5EEDC0FFEEull, kConvexSamples, GenerateConvexPair, CheckConvexPair,
        ShrinkConvex, DumpConvex);
    CHECK(result.failures == 0);

    // hits / misses 는 *검사 호출* 단위다. 실패가 0 이면 shrink 후보를 하나도
    // 시험하지 않으므로 표본 수와 정확히 일대일이다.
    std::cout << "  " << result.samples << " 표본: hits=" << convexStats.hits
              << " misses=" << convexStats.misses
              << " containment=" << convexStats.containment
              << " unique-axis=" << convexStats.uniqueAxis << '\n';
    std::cout << "  최대 |SAT-MTD|=" << convexStats.worstSatVsMtd
              << " |EPA-MTD|=" << convexStats.worstEpaVsMtd
              << " |SAT-EPA|=" << convexStats.worstSatVsEpa << '\n';
    ReportBuckets("볼록쌍", kConvexBucketNames, convexBucketHits,
                  kConvexBucketCount);

    // semantic bucket coverage — 표본 수보다 이쪽이 검증 지표다. 한 버킷이라도
    // 비면 그 분기는 아무것도 검사하지 않은 것이다. hit / miss 가 한쪽으로
    // 쏠려도 마찬가지 — 전부 miss 인 배터리는 깊이를 한 번도 재지 않는다.
    if (physlab::testkit::SingleSeedMode()) return;
    for (int i = 0; i < kConvexBucketCount; ++i) {
        CHECK(convexBucketHits[i] > 0);
    }
    CHECK(convexStats.hits > 200);
    CHECK(convexStats.misses > 200);
    CHECK(convexStats.containment > 20);
    CHECK(convexStats.uniqueAxis > 100);
}

constexpr int kEnergySamples = 120;

void TestEnergyDissipationProperty() {
    const auto result = RunProperty<SceneInput>(
        "energy_dissipation",
        "무작위 강체 장면: 중력 0 · 반발 0 이면 운동에너지는 비증가",
        0xA11CE5EEDull, kEnergySamples,
        [](Rng& rng) { return GenerateMovingScene(rng, energySceneBuckets); },
        CheckEnergyDissipation, ShrinkScene, DumpScene);
    CHECK(result.failures == 0);
    std::cout << "  " << result.samples << " 장면 x " << kEnergyFrames
              << " 프레임: 최대 프레임간 KE 비율=" << energyObservation.worstGrowthRatio
              << " (1 이하가 계약)\n";
    std::cout << "  충돌이 있던 장면 " << energyObservation.scenesWithImpact
              << " 개, 그 중 최소 소산 후 KE/KE0="
              << energyObservation.worstDropRatio << '\n';
    ReportBuckets("장면", kSceneBucketNames, energySceneBuckets,
                  kSceneBucketCount);
    CHECK(energyObservation.worstGrowthRatio <= 1.0 + 1e-6);
    if (physlab::testkit::SingleSeedMode()) return;
    for (int i = 0; i < kSceneBucketCount; ++i) {
        CHECK(energySceneBuckets[i] > 0);
    }
    CHECK(energyObservation.scenesWithImpact > kEnergySamples / 2);
}

constexpr int kSettleSamples = 120;

void TestPenetrationBoundProperty() {
    const auto result = RunProperty<SceneInput>(
        "penetration_bound",
        "무작위 강체 장면: 관통 잔량 상계 · 에너지 상계 · NaN/Inf 부재 · 정착",
        0xBEEF5CEEDull, kSettleSamples,
        [](Rng& rng) { return GenerateScene(rng, settleSceneBuckets); },
        CheckPenetrationBound, ShrinkScene, DumpScene);
    CHECK(result.failures == 0);
    std::cout << "  " << result.samples << " 장면 x " << kSettleFrames
              << " 프레임: 최대 상대 침투=" << settleObservation.worstRelativeDepth
              << " (1 이 반지름)\n";
    std::cout << "  정착 뒤 최대 평균 침투=" << settleObservation.worstSettledAvg
              << " px, 최대 침투=" << settleObservation.worstSettledMax
              << " px, KE/상계=" << settleObservation.worstEnergyRatio << '\n';
    ReportBuckets("장면", kSceneBucketNames, settleSceneBuckets,
                  kSceneBucketCount);
    if (physlab::testkit::SingleSeedMode()) return;
    for (int i = 0; i < kSceneBucketCount; ++i) {
        CHECK(settleSceneBuckets[i] > 0);
    }
}

constexpr int kDeterminismSamples = 48;

void TestDeterminismProperty() {
    const auto result = RunProperty<SceneInput>(
        "determinism", "무작위 강체 장면: 같은 입력 두 번 실행이 bit 동일",
        0xD37E4E11ull, kDeterminismSamples,
        [](Rng& rng) { return GenerateScene(rng, determinismBuckets); },
        CheckBitExactRepeat, ShrinkScene, DumpScene);
    CHECK(result.failures == 0);
    std::cout << "  " << result.samples << " 장면 x " << kDeterminismFrames
              << " 프레임 x 2 회: bit 동일\n";
    std::cout << "  접촉이 둘 이상 얽힌 장면 " << determinismContended
              << " 개 중 순서를 돌렸을 때 갈라진 것 " << determinismDiverged
              << " 개 (최대 " << determinismWorstShuffleDelta << " px)\n";
    // 음성 대조군 — 순서를 바꿔도 아무것도 안 갈라지면 위의 bit 동일은 공허하다.
    if (physlab::testkit::SingleSeedMode()) return;
    CHECK(determinismContended > kDeterminismSamples / 2);
    CHECK(determinismDiverged * 2 > determinismContended);
}

constexpr int kMirrorSamples = 24;

void TestFrameMirrorsProductProperty() {
    const auto result = RunProperty<SceneInput>(
        "frame_mirrors_response",
        "property 층이 도는 프레임이 Response Scene 의 프레임과 bit 동일",
        0x1117707Bull, kMirrorSamples,
        [](Rng& rng) { return GenerateScene(rng, mirrorSceneBuckets); },
        CheckFrameMatchesResponseScene, ShrinkScene, DumpScene);
    CHECK(result.failures == 0);
    std::cout << "  " << result.samples << " 장면 x " << kMirrorFrames
              << " 프레임: max pos delta=" << mirrorWorstPosDelta
              << " max vel delta=" << mirrorWorstVelDelta << '\n';
}

const physlab::testkit::StepCase kCases[] = {
    {"step05", "convex_pair",            TestConvexPairProperty},
    {"step06", "energy_dissipation",     TestEnergyDissipationProperty},
    {"step06", "penetration_bound",      TestPenetrationBoundProperty},
    {"step06", "frame_mirrors_response", TestFrameMirrorsProductProperty},
    {"step08", "determinism",            TestDeterminismProperty},
};

}  // namespace

int main(int argc, char** argv) {
    // argv[1] 은 케이스 필터(step_cases.h), argv[2] 는 단일 시드다. 반례 보고가
    // 찍는 재현 명령이 이 두 인자를 그대로 쓴다.
    if (argc > 2) {
        physlab::testkit::SetSeedOverride(std::strtoull(argv[2], nullptr, 10));
        std::cout << "seed override = " << argv[2]
                  << " — 이 시드의 표본 하나만 돈다\n";
    }
    return physlab::testkit::RunStepCases(
        argc, argv, kCases, (int)(sizeof(kCases) / sizeof(kCases[0])),
        "physics property", &failures);
}
