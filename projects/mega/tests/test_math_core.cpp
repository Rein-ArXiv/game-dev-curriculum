// 수학 코어 검증: scalar/vec/mat/quat. 부동소수점은 허용 오차로만 비교.
#include "math/mat.hpp"
#include "math/quat.hpp"
#include "math/scalar.hpp"
#include "math/vec.hpp"
#include "test_util.h"

#include <cmath>

using namespace mega::math;

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
static bool VNear(const Vec3& a, const Vec3& b, float eps = 1e-4f) {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}
static bool MNear(const Mat4& a, const Mat4& b, float eps = 1e-4f) {
    for (int i = 0; i < 16; ++i) if (!Near(a.m[i], b.m[i], eps)) return false;
    return true;
}

static void TestScalar() {
    CHECK(AlmostEqual(1.0f, 1.0f + 1e-7f));
    CHECK(!AlmostEqual(1.0f, 1.1f));
    CHECK(AlmostEqualUlp(1.0f, 1.0f));
    CHECK(!AlmostEqualUlp(1.0f, 1.001f));
    CHECK(Near(Degrees(Radians(90.0f)), 90.0f));
    CHECK_EQ(Clamp(5.0f, 0.0f, 1.0f), 1.0f);
    CHECK_EQ(Clamp(-5.0f, 0.0f, 1.0f), 0.0f);
    CHECK(Near(Lerp(0.0f, 10.0f, 0.25f), 2.5f));
    // FastInvSqrt 는 ~0.2% 이내 근사.
    CHECK(std::fabs(FastInvSqrt(16.0f) - 0.25f) < 0.25f * 0.01f);
}

static void TestVec() {
    Vec3 a{1, 2, 3}, b{4, 5, 6};
    CHECK(Near(Dot(a, b), 32.0f));                  // 4+10+18
    Vec3 c = Cross(a, b);
    CHECK(Near(Dot(c, a), 0.0f));                   // 외적은 두 벡터에 직교
    CHECK(Near(Dot(c, b), 0.0f));
    CHECK(Near(Length(Normalize(a)), 1.0f));        // 정규화 길이 1
    CHECK(VNear(Lerp(a, b, 0.5f), {2.5f, 3.5f, 4.5f}));
    // 표준 기저 외적: x × y = z
    CHECK(VNear(Cross(Vec3{1,0,0}, Vec3{0,1,0}), {0, 0, 1}));
    CHECK(Near(Distance(Vec3{0,0,0}, Vec3{3,4,0}), 5.0f));
}

static void TestMat() {
    // 단위 행렬은 벡터를 그대로 둔다.
    Vec3 p{2, 3, 4};
    CHECK(VNear(Mat4::Identity().TransformPoint(p), p));

    // 평행이동.
    Mat4 T = Mat4::Translation({10, 20, 30});
    CHECK(VNear(T.TransformPoint(p), {12, 23, 34}));
    CHECK(VNear(T.TransformDir(p), p));             // 방향(w=0)은 평행이동 무시

    // 합성 M = T*R*S 의 역행렬: M * M^-1 = I.
    Mat4 R = ToMat4(Quat::FromAxisAngle({0, 1, 0}, 0.7f));
    Mat4 S = Mat4::Scale({2, 3, 0.5f});
    Mat4 M = T * R * S;
    Mat4 Minv;
    CHECK(Inverse(M, Minv));
    CHECK(MNear(M * Minv, Mat4::Identity()));
    CHECK(MNear(Minv * M, Mat4::Identity()));

    // 역변환으로 점 왕복.
    Vec3 q = M.TransformPoint(p);
    CHECK(VNear(Minv.TransformPoint(q), p));

    // LookAt: eye 는 뷰 공간 원점으로.
    Mat4 V = LookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    CHECK(VNear(V.TransformPoint({0, 0, 5}), {0, 0, 0}));

    // Perspective: -zNear → ndc.z=-1, -zFar → ndc.z=+1.
    Mat4 P = Perspective(Radians(60.0f), 1.0f, 1.0f, 100.0f);
    Vec4 cn = P * Vec4{0, 0, -1.0f, 1};   CHECK(Near(cn.z / cn.w, -1.0f));
    Vec4 cf = P * Vec4{0, 0, -100.0f, 1}; CHECK(Near(cf.z / cf.w,  1.0f));
}

static void TestQuat() {
    // Z축 90° 회전: x → y.
    Quat qz = Quat::FromAxisAngle({0, 0, 1}, kHalfPi);
    CHECK(VNear(Rotate(qz, {1, 0, 0}), {0, 1, 0}));

    // ToMat3 가 Rotate 와 일치.
    Vec3 v{0.3f, -0.7f, 1.2f};
    CHECK(VNear(ToMat3(qz) * v, Rotate(qz, v)));

    // 단위 길이 + 켤레가 역원.
    Quat q = Quat::FromAxisAngle({1, 2, 3}, 1.1f);
    CHECK(Near(Length(q), 1.0f));
    Quat qi = q * Conjugate(q);
    CHECK(VNear({qi.x, qi.y, qi.z}, {0, 0, 0}));
    CHECK(Near(qi.w, 1.0f));

    // Slerp 끝점.
    Quat qid = Quat::Identity();
    Quat s0 = Slerp(qid, qz, 0.0f);
    Quat s1 = Slerp(qid, qz, 1.0f);
    CHECK(VNear(Rotate(s0, {1, 0, 0}), {1, 0, 0}));
    CHECK(VNear(Rotate(s1, {1, 0, 0}), {0, 1, 0}));

    // Slerp 중점: 0→90° 의 절반은 45°. x → (cos45, sin45, 0).
    Quat sm = Slerp(qid, qz, 0.5f);
    float r2 = std::sqrt(0.5f);
    CHECK(VNear(Rotate(sm, {1, 0, 0}), {r2, r2, 0}));

    // 회전 합성: 두 번의 45°(Z) = 한 번의 90°.
    Quat q45 = Quat::FromAxisAngle({0, 0, 1}, kHalfPi * 0.5f);
    CHECK(VNear(Rotate(q45 * q45, {1, 0, 0}), {0, 1, 0}));
}

static void RunTests() {
    TestScalar();
    TestVec();
    TestMat();
    TestQuat();
}

TEST_MAIN("math_core")
