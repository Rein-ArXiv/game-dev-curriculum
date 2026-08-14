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
    CHECK(!s.Error().empty());             // 실패는 진단을 남긴다
    Script s2;
    CHECK(!s2.Run("x = @bad"));            // 알 수 없는 문자
    CHECK(!s2.Error().empty());
}

// --- 실행 예산 ---
//
// 이 스크립트를 읽는 파일(assets/balance.script)은 재빌드 없이 교체할 수 있다고
// 광고돼 있다. 그래서 "끝나지 않는 프로그램"은 가정이 아니라 입력이다. 아래는
// 전부 그 입력이 프로세스를 세우지 못한다는 단언이다 — 세우면 이 테스트가
// 실패하는 게 아니라 **끝나지 않는다**. 그래서 CMake 가 이 테스트에 TIMEOUT 을
// 건다(끝나지 않는 테스트는 타임아웃 실패로 보고된다).

// 감사 REVIEW_MEGA_2026-08.md #8 이 실증한 소스 그대로. 지역 guard 만 있던
// 시절 이 소스는 Run() 에서 돌아오지 않았다(중첩으로 상한이 곱해져 10^12 회).
static const char* const kNestedForever =
    "i = 0\n"
    "while i < 1000000 { j = 0 while j < 1000000 { j = j + 1 } i = i + 1 }\n";

static void TestNestedWhileHitsGlobalBudget() {
    Script s;
    CHECK(!s.Run(kNestedForever));            // 무한 루프가 아니라 오류로 끝난다
    CHECK(s.BudgetExceeded());                // 그리고 그 오류는 예산 초과다
    CHECK(s.StepsUsed() > s.StepBudget());    // 전역 상한에서 잘렸다
    CHECK(!s.Error().empty());                // 조용한 실패가 아니다

    // 예산은 While 노드가 아니라 실행 전체에 걸린다: 중첩 깊이를 한 겹 더 늘려도
    // 상한은 그대로 하나다(곱해지지 않는다).
    Script deeper;
    CHECK(!deeper.Run("a = 0\nwhile a < 1000 { b = 0 while b < 1000 { c = 0 "
                      "while c < 1000 { c = c + 1 } b = b + 1 } a = a + 1 }\n"));
    CHECK(deeper.BudgetExceeded());
    CHECK(deeper.StepsUsed() <= deeper.StepBudget() + 1);
}

static void TestBudgetDiagnosticNamesScriptAndLine() {
    Script s;
    s.SetName("balance.script");
    s.SetStepBudget(5000);                    // 작은 예산으로도 같은 경로를 지난다
    CHECK_EQ(s.StepBudget(), 5000);
    CHECK(!s.Run(kNestedForever));
    CHECK(s.BudgetExceeded());
    // 어느 스크립트의 어느 줄에서 멈췄는지가 진단에 들어 있다. 중첩 while 은
    // 소스 2행에 있으므로 접두사는 "balance.script:2:" 다.
    CHECK(s.Error().rfind("balance.script:2:", 0) == 0);
}

static void TestBudgetResetsPerRun() {
    Script s;
    CHECK(!s.Run(kNestedForever));            // 예산 소진
    CHECK(s.BudgetExceeded());
    // 같은 객체로 정상 스크립트를 다시 돌리면 카운터가 0 에서 시작한다.
    CHECK(s.Run("k = 1\nwhile k < 10 { k = k + 1 }"));
    CHECK(!s.BudgetExceeded());
    CHECK(s.Error().empty());
    CHECK(Near(s.GetOr("k", -1), 10));
    CHECK(s.StepsUsed() > 0);
    CHECK(s.StepsUsed() < s.StepBudget());    // 정상 스크립트는 예산 근처도 안 간다
}

static void TestBudgetLeavesLegitimateScriptsAlone() {
    // 제품이 실제로 싣는 balance.script 와 같은 모양.
    Script s;
    CHECK(s.Run("start_gold = 420\ncore_hp = 260\nenemy_hp_multiplier = 1.0\n"
                "enemy_speed_multiplier = 1.0\nreward_multiplier = 1.0\n"));
    CHECK(s.Error().empty());
    CHECK(!s.BudgetExceeded());
    CHECK(Near(s.GetOr("start_gold", -1), 420));
    CHECK(s.StepsUsed() < 100);               // 실제 사용량은 예산의 1만분의 1 이하
}

// 예산은 실행에만 걸린다. 파싱에서 멈추는 경로가 남아 있으면 예산은 도달조차
// 하지 못하므로, 그 두 경로도 여기서 함께 못박는다.
static void TestMalformedSourceCannotHangParser() {
    // 수정 전: ParsePrimary 가 '}' 를 소비하지 않고 돌아와 최상위 문 루프가
    // 전진하지 않았다 — 한 글자짜리 스크립트가 노드를 무한 할당했다.
    Script s;
    CHECK(!s.Run("}"));
    CHECK(!s.Error().empty());

    // 수정 전: 재귀 하강에 깊이 제한이 없어 스택이 넘쳤다(폴백이 아니라 죽음).
    Script deep;
    std::string nested(4096, '(');
    nested += "1";
    CHECK(!deep.Run(nested));
    CHECK(!deep.Error().empty());

    // 단항 '-' 사슬도 같은 재귀 경로다.
    Script unary;
    CHECK(!unary.Run("x = " + std::string(4096, '-') + "1"));
    CHECK(!unary.Error().empty());
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
    TestNestedWhileHitsGlobalBudget();
    TestBudgetDiagnosticNamesScriptAndLine();
    TestBudgetResetsPerRun();
    TestBudgetLeavesLegitimateScriptsAlone();
    TestMalformedSourceCannotHangParser();
    TestIncrementalBuild();
}

TEST_MAIN("tools_script")
