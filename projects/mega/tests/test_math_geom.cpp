// transform / interp / sdf 검증.
#include "math/interp.hpp"
#include "math/sdf.hpp"
#include "math/transform.hpp"
#include "test_util.h"

#include <cmath>

using namespace mega::math;

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
static bool VNear(const Vec3& a, const Vec3& b, float eps = 1e-4f) {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

static void TestTransform() {
    // scale → rotate → translate 순서.
    Transform t;
    t.position = {10, 0, 0};
    t.rotation = Quat::FromAxisAngle({0, 0, 1}, kHalfPi);   // 90° Z
    t.scale    = {2, 2, 2};
    // {1,0,0} → scale {2,0,0} → rot90Z {0,2,0} → +pos {10,2,0}
    CHECK(VNear(t.TransformPoint({1, 0, 0}), {10, 2, 0}));

    // ToMatrix 가 TransformPoint 와 일치.
    Vec3 p{0.5f, -1.3f, 2.1f};
    CHECK(VNear(t.ToMatrix().TransformPoint(p), t.TransformPoint(p)));

    // 역변환 왕복.
    CHECK(VNear(t.InverseTransformPoint(t.TransformPoint(p)), p));

    // 균일 스케일 합성: ToMatrix(부모*자식) == ToMatrix(부모)*ToMatrix(자식).
    Transform parent;
    parent.position = {1, 2, 3};
    parent.rotation = Quat::FromAxisAngle({0, 1, 0}, 0.5f);
    parent.scale    = {1.5f, 1.5f, 1.5f};
    Transform child;
    child.position = {4, 0, -2};
    child.rotation = Quat::FromAxisAngle({1, 0, 0}, 0.3f);
    child.scale    = {1, 1, 1};
    Mat4 composed = (parent * child).ToMatrix();
    Mat4 product  = parent.ToMatrix() * child.ToMatrix();
    bool same = true;
    for (int i = 0; i < 16; ++i) if (!Near(composed.m[i], product.m[i], 1e-3f)) same = false;
    CHECK(same);
}

static void TestInterp() {
    CHECK(Near(Lerp(0.0f, 10.0f, 0.5f), 5.0f));
    CHECK(Near(SmoothStep(0.0f), 0.0f));
    CHECK(Near(SmoothStep(1.0f), 1.0f));
    CHECK(Near(SmoothStep(0.5f), 0.5f));

    // Hermite 끝점.
    CHECK(Near(Hermite(0.0f, 5.0f, 10.0f, 2.0f, 0.0f), 0.0f));
    CHECK(Near(Hermite(0.0f, 5.0f, 10.0f, 2.0f, 1.0f), 10.0f));

    // Catmull-Rom 은 p1,p2 를 지난다.
    CHECK(Near(CatmullRom(0.0f, 1.0f, 2.0f, 3.0f, 0.0f), 1.0f));
    CHECK(Near(CatmullRom(0.0f, 1.0f, 2.0f, 3.0f, 1.0f), 2.0f));
    // 등간격 공선점 → 선형: 중점 1.5.
    CHECK(Near(CatmullRom(0.0f, 1.0f, 2.0f, 3.0f, 0.5f), 1.5f));

    // Bezier 끝점 + 공선 중점.
    CHECK(Near(Bezier(0.0f, 1.0f, 2.0f, 3.0f, 0.0f), 0.0f));
    CHECK(Near(Bezier(0.0f, 1.0f, 2.0f, 3.0f, 1.0f), 3.0f));
    CHECK(Near(Bezier(0.0f, 1.0f, 2.0f, 3.0f, 0.5f), 1.5f));

    // Vec3 에도 동일 적용(템플릿).
    Vec3 r = CatmullRom(Vec3{0,0,0}, Vec3{1,0,0}, Vec3{2,0,0}, Vec3{3,0,0}, 0.5f);
    CHECK(VNear(r, {1.5f, 0, 0}));
}

static void TestSdf() {
    // 구: 중심 -r, 표면 0, 밖 dist-r.
    CHECK(Near(SdSphere({0, 0, 0}, 2.0f), -2.0f));
    CHECK(Near(SdSphere({2, 0, 0}, 2.0f), 0.0f));
    CHECK(Near(SdSphere({5, 0, 0}, 2.0f), 3.0f));

    // 박스(반쪽 1): 중심 -1, 면 0, 모서리 밖.
    Vec3 b{1, 1, 1};
    CHECK(Near(SdBox({0, 0, 0}, b), -1.0f));
    CHECK(Near(SdBox({1, 0, 0}, b), 0.0f));
    CHECK(Near(SdBox({2, 0, 0}, b), 1.0f));
    CHECK(Near(SdBox({2, 2, 2}, b), std::sqrt(3.0f)));   // 모서리 대각

    // 캡슐: x축 선분 [-1,1], r=0.5.
    CHECK(Near(SdCapsule({0, 0, 0}, {-1, 0, 0}, {1, 0, 0}, 0.5f), -0.5f));
    CHECK(Near(SdCapsule({0, 1, 0}, {-1, 0, 0}, {1, 0, 0}, 0.5f),  0.5f));
    CHECK(Near(SdCapsule({3, 0, 0}, {-1, 0, 0}, {1, 0, 0}, 0.5f),  1.5f));

    // 합성 연산.
    float s = SdSphere({1.5f, 0, 0}, 2.0f);   // = -0.5 (안)
    float x = SdBox({1.5f, 0, 0}, {1, 1, 1}); // = 0.5  (밖)
    CHECK(Near(OpUnion(s, x), -0.5f));        // 둘 중 더 안쪽
    CHECK(Near(OpIntersect(s, x), 0.5f));     // 둘 중 더 바깥
    CHECK(Near(OpSubtract(s, x), 0.5f));      // x 에서 s 를 깎음: max(-(-0.5),0.5)=0.5
}

static void RunTests() {
    TestTransform();
    TestInterp();
    TestSdf();
}

TEST_MAIN("math_geom")
