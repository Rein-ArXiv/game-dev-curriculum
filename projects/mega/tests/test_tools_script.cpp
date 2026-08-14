// 스크립트 인터프리터 + 에셋 파이프라인 검증.
#include "tools/asset_pipeline.hpp"
#include "tools/script.hpp"
#include "test_util.h"

#include <cmath>
#include <string>

using namespace mega::tools;

static bool Near(double a, double b, double e = 1e-9) { return std::fabs(a - b) <= e; }

static void TestArithmetic() {
    Script s;
    CHECK(s.Run("x = 2 + 3 * 4"));         // 우선순위: 2 + 12
    CHECK(Near(s.GetOr("x", -1), 14));
    CHECK(s.Run("y = (2 + 3) * 4"));       // 괄호
    CHECK(Near(s.GetOr("y", -1), 20));
    CHECK(s.Run("z = 10 / 4"));            // 부동소수
    CHECK(Near(s.GetOr("z", -1), 2.5));
    CHECK(s.Run("n = -5 + 2"));            // 단항 음수
    CHECK(Near(s.GetOr("n", 0), -3));
}

static void TestVariables() {
    Script s;
    CHECK(s.Run("a = 5\nb = a * 2\nc = a + b"));   // 변수 참조
    CHECK(Near(s.GetOr("a", 0), 5));
    CHECK(Near(s.GetOr("b", 0), 10));
    CHECK(Near(s.GetOr("c", 0), 15));
}

static void TestComparison() {
    Script s;
    CHECK(s.Run("p = 5 > 3\nq = 2 == 2\nr = 1 != 1\nt = 4 <= 4"));
    CHECK(Near(s.GetOr("p", -1), 1));      // true
    CHECK(Near(s.GetOr("q", -1), 1));
    CHECK(Near(s.GetOr("r", -1), 0));      // false
    CHECK(Near(s.GetOr("t", -1), 1));
}

static void TestIfElse() {
    Script s1; s1.Run("x = 10\nif x > 5 { y = 1 } else { y = 0 }");
    CHECK(Near(s1.GetOr("y", -1), 1));
    Script s2; s2.Run("x = 2\nif x > 5 { y = 1 } else { y = 0 }");
    CHECK(Near(s2.GetOr("y", -1), 0));
    // else 없는 if(거짓이면 미실행).
    Script s3; s3.Run("z = 9\nif z > 100 { z = 0 }");
    CHECK(Near(s3.GetOr("z", -1), 9));
}

static void TestWhile() {
    Script s;
    // 1..5 합 = 15.
    CHECK(s.Run("i = 1\nsum = 0\nwhile i <= 5 { sum = sum + i\ni = i + 1 }"));
    CHECK(Near(s.GetOr("sum", -1), 15));
    CHECK(Near(s.GetOr("i", -1), 6));

    // 팩토리얼 5! = 120.
    Script f;
    f.Run("n = 5\nfact = 1\nwhile n > 0 { fact = fact * n\nn = n - 1 }");
    CHECK(Near(f.GetOr("fact", -1), 120));
}

static void TestScriptError() {
    Script s;
    CHECK(!s.Run("x = 2 +"));              // 불완전 표현식
    Script s2;
    CHECK(!s2.Run("x = @bad"));            // 알 수 없는 문자
}

// --- 에셋 파이프라인 ---
static void TestIncrementalBuild() {
    AssetPipeline ap;
    ap.Register("texture.png", 0x100);
    ap.Register("material.mat", 0x200, {"texture.png"});   // material 이 texture 에 의존
    ap.Register("mesh.obj", 0x300);

    CHECK_EQ(ap.Cook(), 3);                // 처음엔 전부 굽는다
    CHECK_EQ(ap.Cook(), 0);                // 변경 없으면 0(증분)
    CHECK(ap.IsCooked("material.mat"));

    // texture 변경 → texture 와 그것에 의존하는 material 재굽기(mesh 는 그대로).
    ap.UpdateSource("texture.png", 0x999);
    CHECK_EQ(ap.Cook(), 2);
    CHECK_EQ(ap.Cook(), 0);                // 다시 안정

    // 독립 에셋 변경 → 그것만.
    ap.UpdateSource("mesh.obj", 0x301);
    CHECK_EQ(ap.Cook(), 1);
}

static void RunTests() {
    TestArithmetic();
    TestVariables();
    TestComparison();
    TestIfElse();
    TestWhile();
    TestScriptError();
    TestIncrementalBuild();
}

TEST_MAIN("tools_script")
