#include "bench_program.hpp"
#include "fuzz.hpp"
#include "language.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        ++checks;                                                               \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,       \
                         #condition);                                            \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

struct Execution {
    lab::lang::CompileResult compiled;
    lab::lang::VirtualMachine vm;
    lab::lang::RunResult run;
};

Execution Execute(const std::string& source, uint64_t budget = 100000,
                  size_t stackLimit = 256) {
    Execution result;
    result.compiled = lab::lang::Compiler{}.Compile(source);
    if (result.compiled.program)
        result.run = result.vm.Run(*result.compiled.program, budget, stackLimit);
    return result;
}

double Global(const Execution& execution, const char* name) {
    return execution.vm.Global(*execution.compiled.program, name).value_or(-9999.0);
}

void TestPrecedenceAndVariables() {
    auto execution = Execute("let a = 2 + 3 * 4; let b = (2 + 3) * 4;");
    CHECK(execution.compiled.program.has_value());
    CHECK(execution.run.ok);
    CHECK(Global(execution, "a") == 14.0);
    CHECK(Global(execution, "b") == 20.0);
}

// 좌결합 연산자(`-`, `/`)는 결합 방향이 값에 그대로 드러난다. 우결합 파서라면
// a는 8-(3-2)=7, b는 16/(4/2)=8이 되어 아래 두 단언이 동시에 깨진다.
void TestAssociativityAndUnaryPrecedence() {
    auto execution = Execute(
        "let a = 8 - 3 - 2;"
        "let b = 16 / 4 / 2;"
        "let c = 1 < 2 == 1;"
        "let d = -2 - 3;");
    CHECK(execution.compiled.program.has_value());
    CHECK(execution.run.ok);
    CHECK(Global(execution, "a") == 3.0);
    CHECK(Global(execution, "b") == 2.0);
    CHECK(Global(execution, "c") == 1.0);
    CHECK(Global(execution, "d") == -5.0);
}

void TestControlFlow() {
    auto execution = Execute(
        "let n = 5; let fact = 1;"
        "while (n > 0) { fact = fact * n; n = n - 1; }"
        "if (fact == 120) { fact = fact + 1; } else { fact = 0; }");
    CHECK(execution.run.ok);
    CHECK(Global(execution, "fact") == 121.0);
}

void TestHostBoundary() {
    auto compiled = lab::lang::Compiler{}.Compile("let result = twice(21);");
    CHECK(compiled.program.has_value());
    lab::lang::VirtualMachine vm;
    vm.Bind("twice", [](double value) { return value * 2.0; });
    const auto run = vm.Run(*compiled.program);
    CHECK(run.ok);
    CHECK(vm.Global(*compiled.program, "result").value_or(0.0) == 42.0);
}

// 미등록 이름은 실행 시점에 거부되어야 하고, 진단은 소스 위치를 그대로 가리켜야 한다.
void TestHostBoundaryRejectsUnregisteredName() {
    auto compiled = lab::lang::Compiler{}.Compile("let x = ghost(1);");
    CHECK(compiled.program.has_value());
    if (!compiled.program) return;

    lab::lang::VirtualMachine vm;
    const auto run = vm.Run(*compiled.program);
    CHECK(!run.ok);
    CHECK(run.diagnostic.has_value());
    if (run.diagnostic) {
        CHECK(run.diagnostic->message == "등록되지 않은 호스트 함수: ghost");
        CHECK(run.diagnostic->location.line == 1);
        CHECK(run.diagnostic->location.column == 9);  // `ghost`의 시작 열
    }

    // 같은 바이트코드, 다른 권한 집합: 바인딩한 VM만 성공한다.
    lab::lang::VirtualMachine permitted;
    permitted.Bind("ghost", [](double value) { return value + 1.0; });
    const auto allowed = permitted.Run(*compiled.program);
    CHECK(allowed.ok);
    CHECK(permitted.Global(*compiled.program, "x").value_or(-1.0) == 2.0);

    lab::lang::VirtualMachine denied;
    const auto refused = denied.Run(*compiled.program);
    CHECK(!refused.ok);
}

// 렉서가 만드는 진단. 위치 보존은 진단의 줄만 보면 검증되지 않는다 —
// `SourceLocation`의 기본값이 `{1, 1}`이라 한 줄짜리 소스는 위치 추적을 지워도
// 통과한다. 그래서 세 줄짜리 소스에서 줄과 열을 함께 못 박는다.
void TestLexerRejectsUnknownCharacter() {
    const auto multiline =
        lab::lang::Compiler{}.Compile("let a = 1;\nlet b = 2;\n   let c = @;\n");
    CHECK(!multiline.program.has_value());
    CHECK(!multiline.diagnostics.empty());
    if (!multiline.diagnostics.empty()) {
        const auto& d = multiline.diagnostics.front();
        CHECK(d.message == "알 수 없는 문자: @");
        CHECK(d.location.line == 3);
        CHECK(d.location.column == 12);
        CHECK(lab::lang::FormatDiagnostic(d) == "3:12: 알 수 없는 문자: @");
    }
}

// 토큰 위치가 상수로 고정되면 실패한다. 최대 길이 일치(`==`가 두 개의 `=`로
// 쪼개지지 않음)도 토큰 수와 시작 열로 함께 고정한다.
void TestLexerTokenLocations() {
    std::vector<lab::lang::Diagnostic> diagnostics;
    const auto tokens = lab::lang::Lexer("a == b").Scan(diagnostics);
    CHECK(diagnostics.empty());
    CHECK(tokens.size() == 4);  // Identifier, EqualEqual, Identifier, End
    if (tokens.size() == 4) {
        CHECK(tokens[0].kind == lab::lang::TokenKind::Identifier);
        CHECK(tokens[0].location.column == 1);
        CHECK(tokens[1].kind == lab::lang::TokenKind::EqualEqual);
        CHECK(tokens[1].text == "==");
        CHECK(tokens[1].location.line == 1);
        CHECK(tokens[1].location.column == 3);  // `=`의 열이 아니라 토큰 시작 열
        CHECK(tokens[2].kind == lab::lang::TokenKind::Identifier);
        CHECK(tokens[2].location.column == 6);
    }

    std::vector<lab::lang::Diagnostic> lineDiagnostics;
    const auto lines = lab::lang::Lexer("1;\n  22;\n   333;").Scan(lineDiagnostics);
    CHECK(lineDiagnostics.empty());
    CHECK(lines.size() == 7);  // 숫자·세미콜론 3쌍 + End
    if (lines.size() == 7) {
        CHECK(lines[0].location.line == 1);
        CHECK(lines[0].location.column == 1);
        CHECK(lines[2].location.line == 2);
        CHECK(lines[2].location.column == 3);
        CHECK(lines[4].location.line == 3);
        CHECK(lines[4].location.column == 4);
    }
}

// 파서가 만드는 진단. 컴파일 오류에는 `Program`이 없어야 하고, 진단 위치가
// 소스 범위 안이어야 한다.
void TestParserRejectsIncompleteExpression() {
    const auto syntax = lab::lang::Compiler{}.Compile("let x = 1 + ;");
    CHECK(!syntax.program.has_value());
    CHECK(!syntax.diagnostics.empty());
    CHECK(syntax.diagnostics.front().location.line == 1);
}

// 실행 시점의 값 진단 세 종류. 하나로 묶으면 서로를 가린다.
void TestRuntimeValueDiagnostics() {
    auto undefined = Execute("let x = missing + 1;");
    CHECK(!undefined.run.ok);
    CHECK(undefined.run.diagnostic.has_value());

    // 0 나눗셈은 비유한 값 차단과 구분되는 고유 진단을 내야 한다.
    auto division = Execute("let x = 1 / 0;");
    CHECK(!division.run.ok);
    CHECK(division.run.diagnostic.has_value());
    if (division.run.diagnostic)
        CHECK(division.run.diagnostic->message == "0으로 나눌 수 없다");

    // 0 나눗셈을 거치지 않고 오버플로만으로 비유한 값에 도달하는 경로.
    // isfinite 가드를 지우면 무한 루프가 되어 메시지가 예산 초과로 바뀐다.
    auto overflow = Execute("let x = 2; while (x > 0) { x = x * x; }");
    CHECK(!overflow.run.ok);
    CHECK(overflow.run.diagnostic.has_value());
    if (overflow.run.diagnostic)
        CHECK(overflow.run.diagnostic->message == "연산 결과가 유한하지 않다");
}

void TestBudgetStopsUntrustedLoop() {
    auto execution = Execute("let x = 1; while (x > 0) { x = x + 1; }", 250);
    CHECK(!execution.run.ok);
    CHECK(execution.run.instructions == 251);
}

void TestParserRejectsDeepNesting() {
    const int depth = 20000;
    std::string source = "let x = ";
    source.append(static_cast<size_t>(depth), '(');
    source += "1";
    source.append(static_cast<size_t>(depth), ')');
    source += ";";

    const auto compiled = lab::lang::Compiler{}.Compile(source);
    CHECK(!compiled.program.has_value());
    CHECK(!compiled.diagnostics.empty());
    CHECK(compiled.diagnostics.front().message == "중첩 깊이가 파서 한도를 초과했다");
}

void TestVerifierRejectsMalformedProgram() {
    lab::lang::Program malformed;
    malformed.code.push_back({lab::lang::OpCode::Pop, 0, {1, 1}});
    malformed.code.push_back({lab::lang::OpCode::Halt, 0, {1, 2}});
    std::string error;
    CHECK(!lab::lang::Verify(malformed, error));
    CHECK(!error.empty());

    lab::lang::Program unreachableOperand;
    unreachableOperand.code.push_back({lab::lang::OpCode::Jump, 2, {1, 1}});
    unreachableOperand.code.push_back({lab::lang::OpCode::Constant, 99, {1, 2}});
    unreachableOperand.code.push_back({lab::lang::OpCode::Halt, 0, {1, 3}});
    CHECK(!lab::lang::Verify(unreachableOperand, error));
}

// 검증기가 실제로 무엇을 왜 거부하는지 사유 문자열까지 고정한다.
// 분기가 없는 선형 프로그램만으로는 합류 깊이 검사가 한 번도 실행되지 않는다.
void TestVerifierBranchContracts() {
    using lab::lang::OpCode;
    using lab::lang::Program;
    std::string error;

    // 대조군: 두 경로의 합류 깊이가 같은 정상 분기 프로그램은 통과해야 한다.
    // 이 대조군이 없으면 아래 (a)가 다른 이유로 거부돼도 눈치채지 못한다.
    Program balanced;
    balanced.constants.push_back(1.0);
    balanced.code.push_back({OpCode::Constant, 0, {1, 1}});     // 0: depth 0 -> 1
    balanced.code.push_back({OpCode::JumpIfFalse, 4, {1, 2}});  // 1: depth 1 -> 1
    balanced.code.push_back({OpCode::Pop, 0, {1, 3}});          // 2: depth 1 -> 0
    balanced.code.push_back({OpCode::Jump, 5, {1, 4}});         // 3: depth 0 -> 0
    balanced.code.push_back({OpCode::Pop, 0, {1, 5}});          // 4: depth 1 -> 0
    balanced.code.push_back({OpCode::Halt, 0, {1, 6}});         // 5: depth 0
    error.clear();
    CHECK(lab::lang::Verify(balanced, error));
    CHECK(error.empty());

    // (a) 합류 불일치: 거짓 경로(JumpIfFalse의 대상 3)는 Pop으로 깊이를 1 줄이고
    // 참 경로(낙하 2 -> Jump 4)는 그대로 통과해 인덱스 4에서 깊이 0과 1이 만난다.
    Program mismatched;
    mismatched.constants.push_back(1.0);
    mismatched.code.push_back({OpCode::Constant, 0, {1, 1}});     // 0: depth 0 -> 1
    mismatched.code.push_back({OpCode::JumpIfFalse, 3, {1, 2}});  // 1: depth 1
    mismatched.code.push_back({OpCode::Jump, 4, {1, 3}});         // 2: depth 1 -> 4
    mismatched.code.push_back({OpCode::Pop, 0, {1, 4}});          // 3: depth 1 -> 0
    mismatched.code.push_back({OpCode::Halt, 0, {1, 5}});         // 4: 0과 1이 합류
    error.clear();
    CHECK(!lab::lang::Verify(mismatched, error));
    CHECK(error == "분기 합류점의 스택 깊이가 일치하지 않는다");

    // (b) 음수 분기 대상은 도달 가능성과 무관하게 정적으로 걸러야 한다.
    Program negativeJump;
    negativeJump.code.push_back({OpCode::Jump, -1, {1, 1}});
    negativeJump.code.push_back({OpCode::Halt, 0, {1, 2}});
    error.clear();
    CHECK(!lab::lang::Verify(negativeJump, error));
    CHECK(error == "분기 인덱스가 범위를 벗어났다");

    // (c) Halt로 끝나지 않는 프로그램.
    Program noHalt;
    noHalt.constants.push_back(1.0);
    noHalt.code.push_back({OpCode::Constant, 0, {1, 1}});
    noHalt.code.push_back({OpCode::Pop, 0, {1, 2}});
    error.clear();
    CHECK(!lab::lang::Verify(noHalt, error));
    CHECK(error == "프로그램은 Halt로 끝나야 한다");
}

// Run의 3번째 인자(스택 상한)를 실제로 넘기는 유일한 테스트다.
void TestStackLimitStopsDeepExpression() {
    const char* source = "let x = 1 + (2 + (3 + (4 + (5 + 6))));";

    // 대조군: 기본 상한이면 같은 소스가 정상 실행된다.
    auto ok = Execute(source);
    CHECK(ok.compiled.program.has_value());
    CHECK(ok.run.ok);
    CHECK(Global(ok, "x") == 21.0);

    // 상한 4: 상수 5개를 쌓기 전에 거부되어야 한다.
    auto limited = Execute(source, 100000, 4);
    CHECK(limited.compiled.program.has_value());
    CHECK(!limited.run.ok);
    CHECK(limited.run.diagnostic.has_value());
    if (limited.run.diagnostic)
        CHECK(limited.run.diagnostic->message == "VM 스택 한도를 초과했다");
}

// 측정이 비교하는 두 경로가 *같은 작업*을 한다는 전제를 못 박는다. 측정 루프는
// 결과를 읽지 않으므로(전역 조회 비용이 시간에 섞인다) 그 불변식은 여기가 맡는다.
void TestBenchProgramInvariants() {
    lab::lang::Compiler compiler;
    const auto compiled = compiler.Compile(lab::lang::bench::kSource);
    CHECK(compiled.program.has_value());
    if (!compiled.program) return;

    lab::lang::VirtualMachine vm;
    const auto run = vm.Run(*compiled.program);
    CHECK(run.ok);
    CHECK(vm.Global(*compiled.program, "sum").value_or(-1.0) == 5050.0);
    CHECK(vm.Global(*compiled.program, "x").value_or(-1.0) == 101.0);
    CHECK(run.instructions == 1612);

    // 캐시 경로: 같은 Program을 새 VM으로 다시 실행해도 명령 수와 결과가 같아야
    // 한다. 실행이 `Program`을 변경하거나 결과가 VM 인스턴스에 묶여 있으면 두 번째 실행이 달라진다.
    lab::lang::VirtualMachine second;
    const auto cached = second.Run(*compiled.program);
    CHECK(cached.ok);
    CHECK(cached.instructions == run.instructions);
    CHECK(second.Global(*compiled.program, "sum").value_or(-1.0) == 5050.0);

    // 컴파일 포함 경로: 다시 컴파일한 바이트코드가 첫 컴파일과 같아야 두 경로의
    // 시간 비교가 성립한다. 컴파일러가 호출마다 다른 코드를 내면 비교가 무의미하다.
    const auto recompiled = compiler.Compile(lab::lang::bench::kSource);
    CHECK(recompiled.program.has_value());
    if (recompiled.program) {
        bool identical =
            recompiled.program->code.size() == compiled.program->code.size();
        for (size_t i = 0; identical && i < recompiled.program->code.size(); ++i) {
            identical = recompiled.program->code[i].op == compiled.program->code[i].op &&
                        recompiled.program->code[i].operand ==
                            compiled.program->code[i].operand;
        }
        CHECK(identical);
        CHECK(recompiled.program->constants == compiled.program->constants);
        CHECK(recompiled.program->names == compiled.program->names);
    }

    // 벤치마크가 출력하는 checksum은 두 경로가 각각 kIterations번 돈 명령 수의
    // 합이다. 이 값이 환경과 무관한 고정값이라는 주장이 여기서 산술로 닫힌다.
    CHECK(2 * static_cast<uint64_t>(lab::lang::bench::kIterations) * run.instructions ==
          32240000);
}

// ── 문법 유도 fuzz 배터리 ────────────────────────────────────────────────────
//
// 위의 사례들은 사람이 고른 입력으로 계약을 못 박는다. 배터리는 같은 계약을 *생성된*
// 입력에서 검사한다. 사람이 고른 입력이 덮지 못하는 것은 조합이다 — 렉서·파서·검증기·VM이
// 각각 옳아도 특정 조합에서만 어긋날 수 있다.
//
// 규칙 셋을 지킨다.
//   1. 난수는 전부 고정 시드에서 나온다. 벽시계와 프로세스 시간을 읽지 않는다.
//   2. 실패는 반례를 축소해 보고하고, 재현 명령 한 줄을 함께 찍는다.
//   3. 표본마다 CHECK를 세지 않는다. property 하나가 CHECK 하나다 — 표본 수가 검증의
//      강도라고 말하는 출력은 지표를 망가뜨린다.

namespace fz = lab::lang::fuzz;

// 배터리의 기본 시드. 실행마다 다른 시드를 뽑으면 통과가 무엇을 뜻하는지 알 수 없고
// 실패가 재현되지 않는다. 넓은 탐색은 시드를 바꿔 *명시적으로* 돌린다.
constexpr uint64_t kDefaultMasterSeed = 0x5EED10CC1A2B3C4Dull;

// 실행 상한. 예산의 일은 "멈춘다"를 증명하는 것이지 오래 도는 것이 아니므로 기본값
// 100000보다 작게 잡아 같은 주장을 더 빨리 한다.
constexpr uint64_t kFuzzBudget = 4000;
constexpr size_t kFuzzStack = 256;
// 좁은 상한. 같은 프로그램을 두 자원 한도로 돌려 한도 자체가 관측되게 한다.
constexpr uint64_t kTightBudget = 64;
constexpr size_t kTightStack = 3;

// 표본 수는 측정으로 정했다. 이 기계에서 표본 하나가 30~60 마이크로초이므로 사례당
// 20,000표본이면 네 사례 합계가 4초 안쪽이다. 커버리지 하한은 500표본에서 이미 채워지므로
// 나머지는 드문 조합을 만날 확률에 쓰는 몫이다. 근거 수치는 step 문서에 있다.
constexpr uint64_t kGrammarSamples = 20000;
constexpr uint64_t kBytecodeSamples = 20000;
constexpr uint64_t kExecutionSamples = 20000;
constexpr uint64_t kMutationSamples = 20000;

uint64_t masterSeed = kDefaultMasterSeed;
long long sampleOverride = -1;
long long onlyIndex = -1;
int reportedCounterexamples = 0;
constexpr int kMaxReports = 3;

struct SampleRange {
    uint64_t begin;
    uint64_t end;
};

SampleRange Samples(uint64_t defaultCount) {
    if (onlyIndex >= 0) {
        const uint64_t only = static_cast<uint64_t>(onlyIndex);
        return {only, only + 1};
    }
    return {0, sampleOverride >= 0 ? static_cast<uint64_t>(sampleOverride) : defaultCount};
}

// 커버리지 단언은 표본이 충분할 때만 뜻이 있다. `--only` 재현이나 소표본 실행에서는
// property 단언만 남긴다 — 재현 명령이 "커버리지 부족"으로 실패하면 반례를 가린다.
bool CoverageEnforced(uint64_t samples) { return onlyIndex < 0 && samples >= 500; }

std::string Indent(const std::string& text) {
    std::string out = "    ";
    for (const char c : text) {
        out.push_back(c);
        if (c == '\n') out += "    ";
    }
    return out;
}

// 반례 보고. 시드와 표본 번호, 축소된 소스, 그리고 그 하나만 다시 도는 명령을 낸다.
// 상한은 사례마다 따로 센다 — 한 사례가 상한을 다 쓰면 다른 사례의 반례가 사라진다.
void BeginCase() { reportedCounterexamples = 0; }

void ReportCounterexample(const char* caseName, uint64_t index, const fz::TokenList& original,
                          const fz::TokenList& shrunk, const std::string& detail) {
    if (reportedCounterexamples++ >= kMaxReports) return;
    std::fprintf(stderr,
                 "\nFUZZ 반례 [%s] 표본 %llu\n"
                 "  실패: %s\n"
                 "  축소: 토큰 %zu -> %zu\n"
                 "  재현: languages_tests.exe %s --seed=0x%016llX --only=%llu\n"
                 "  소스:\n%s\n",
                 caseName, static_cast<unsigned long long>(index), detail.c_str(),
                 original.size(), shrunk.size(), caseName,
                 static_cast<unsigned long long>(masterSeed),
                 static_cast<unsigned long long>(index), Indent(fz::Render(shrunk)).c_str());
}

// ── property 판정기 ─────────────────────────────────────────────────────────
// 빈 문자열은 통과다. 판정기를 하나로 두면 본 루프와 축소기가 *같은* 실패 정의를 쓴다.
// 정의가 갈리면 축소된 반례가 원래 실패를 재현하지 않는다.

// 축소는 "아무 실패나 유지"로 하면 안 된다. 토큰을 지우다 보면 문법이 깨져 *다른* 이유로
// 실패하는 입력에 도달하고, 그것을 반례로 보고하면 원인을 엉뚱한 곳에서 찾게 된다.
// 실제로 이 배터리의 첫 판본이 그랬다 — 컴파일러 결함을 주입했더니 축소된 반례가
// `;` 한 토큰이었고, 그것은 결함과 무관한 구문 오류였다. 그래서 축소는 실패의 *부류*를
// 보존한다. 부류는 진단 문자열의 가변부(`: ` 뒤) 앞까지다.
std::string ViolationClass(const std::string& detail) {
    const size_t separator = detail.find(": ");
    return separator == std::string::npos ? detail : detail.substr(0, separator);
}

// snippet-begin: fuzz-grammar-property
// 문법 유도 생성기가 만든 소스가 지켜야 하는 것.
//   (1) 파스·컴파일이 성공한다.               생성기가 이 lab의 문법만 만들기 때문이다.
//   (2) 산출물과 진단이 동시에 나오지 않는다.  CompileResult의 불변식이다.
//   (3) 두 번 컴파일하면 같은 바이트코드다.    결정성. 위치까지 비교한다.
//   (4) 그 바이트코드는 검증기를 통과한다.     컴파일러는 검증 불가 코드를 내지 않는다.
std::string GrammarCompileViolation(const std::string& source) {
    const auto first = lab::lang::Compiler{}.Compile(source);
    if (!first.program) {
        if (first.diagnostics.empty()) return "컴파일 실패(진단조차 없다)";
        return "컴파일 실패(" + first.diagnostics.front().message + "): " +  // 메시지가 부류, 위치는 가변부
               lab::lang::FormatDiagnostic(first.diagnostics.front());
    }
    if (!first.diagnostics.empty()) return "프로그램과 진단이 동시에 나왔다";

    const auto second = lab::lang::Compiler{}.Compile(source);
    if (!second.program) return "같은 소스의 두 번째 컴파일이 실패했다";
    if (!fz::SameBytecode(*first.program, *second.program))
        return "같은 소스의 두 컴파일이 다른 바이트코드를 냈다";

    std::string error;
    if (!lab::lang::Verify(*first.program, error))
        return "컴파일러가 검증을 통과하지 못하는 바이트코드를 냈다: " + error;
    return {};
}
// snippet-end: fuzz-grammar-property

// snippet-begin: fuzz-execution-property
// 컴파일된 프로그램을 실행할 때 지켜야 하는 것.
//   (1) 같은 VM으로 두 번 돌려도 결과가 같다.   실행 사이에 상태가 새지 않는다.
//   (2) 새 VM으로 돌려도 결과가 같다.           인스턴스에 숨은 상태가 없다.
//   (3) 성공한 실행은 값 스택을 비운 채 끝난다. 모든 문장이 스택 중립이기 때문이다.
//   (4) 명령 수가 예산 + 1을 넘지 않는다.       예산은 초과 명령 하나에서 끊는다.
//   (5) 실패 메시지가 닫힌 어휘 안에 있다.      통제되지 않은 실패 경로가 없다.
//   (6) 실패 위치가 소스 안을 가리킨다.
//   (7) 좁은 자원 한도에서도 같은 것이 성립한다.
std::string ExecutionViolation(const std::string& source) {
    const auto compiled = lab::lang::Compiler{}.Compile(source);
    if (!compiled.program) return "실행 전 컴파일이 실패했다";

    lab::lang::VirtualMachine reused;
    fz::BindStandardNatives(reused);
    const auto first = fz::Observe(reused, *compiled.program, kFuzzBudget, kFuzzStack);
    const auto again = fz::Observe(reused, *compiled.program, kFuzzBudget, kFuzzStack);
    if (!(first == again)) return "같은 VM으로 다시 실행하니 결과가 달라졌다(상태 누수)";

    lab::lang::VirtualMachine fresh;
    fz::BindStandardNatives(fresh);
    const auto other = fz::Observe(fresh, *compiled.program, kFuzzBudget, kFuzzStack);
    if (!(first == other)) return "새 VM의 실행 결과가 다르다";

    if (first.ok && first.stackDepth != 0) return "성공한 실행이 값 스택을 비우지 않았다";
    if (first.instructions > kFuzzBudget + 1) return "명령 수가 예산을 넘었다";
    if (!first.ok) {
        if (!fz::IsKnownRuntimeMessage(first.message))
            return "닫힌 실패 어휘 밖의 메시지: " + first.message;
        if (!fz::LocationWithinSource(source, first.location))
            return "실패 위치가 소스 밖을 가리킨다: " + lab::lang::FormatDiagnostic(
                                                            {first.location, first.message});
    }

    lab::lang::VirtualMachine tight;
    fz::BindStandardNatives(tight);
    const auto limited = fz::Observe(tight, *compiled.program, kTightBudget, kTightStack);
    if (limited.instructions > kTightBudget + 1) return "좁은 예산에서 명령 수가 넘었다";
    if (!limited.ok && !fz::IsKnownRuntimeMessage(limited.message))
        return "좁은 한도에서 닫힌 어휘 밖의 메시지: " + limited.message;
    return {};
}
// snippet-end: fuzz-execution-property

// snippet-begin: fuzz-mutation-property
// 토큰 단위로 깨뜨린 소스가 지켜야 하는 것. 거부는 실패가 아니다 — 거부의 *방식*이
// 계약이다. 크래시, 무한 루프, 산출물과 진단의 동시 발생, 어휘 밖 메시지, 소스 밖
// 위치가 실패다.
std::string MutationViolation(const std::string& source) {
    const auto compiled = lab::lang::Compiler{}.Compile(source);
    if (compiled.program && !compiled.diagnostics.empty())
        return "프로그램과 진단이 동시에 나왔다";
    if (!compiled.program && compiled.diagnostics.empty())
        return "프로그램도 진단도 없이 거부했다";

    for (const auto& diagnostic : compiled.diagnostics) {
        if (!fz::IsKnownCompileMessage(diagnostic.message))
            return "닫힌 진단 어휘 밖의 메시지: " + diagnostic.message;
        if (!fz::LocationWithinSource(source, diagnostic.location))
            return "진단 위치가 소스 밖을 가리킨다: " + lab::lang::FormatDiagnostic(diagnostic);
    }

    const auto again = lab::lang::Compiler{}.Compile(source);
    if (again.program.has_value() != compiled.program.has_value())
        return "같은 소스를 두 번 컴파일했는데 성공 여부가 달라졌다";
    if (again.diagnostics.size() != compiled.diagnostics.size())
        return "같은 소스의 두 컴파일이 다른 수의 진단을 냈다";
    for (size_t i = 0; i < again.diagnostics.size(); ++i) {
        if (again.diagnostics[i].message != compiled.diagnostics[i].message ||
            again.diagnostics[i].location.line != compiled.diagnostics[i].location.line ||
            again.diagnostics[i].location.column != compiled.diagnostics[i].location.column)
            return "같은 소스의 두 컴파일이 다른 진단을 냈다";
    }

    if (!compiled.program) return {};

    std::string error;
    if (!lab::lang::Verify(*compiled.program, error))
        return "깨진 소스가 검증 불가 바이트코드로 컴파일됐다: " + error;
    return ExecutionViolation(source);
}
// snippet-end: fuzz-mutation-property

void PrintCoverage(const char* label, const std::string& text) {
    std::printf("  [fuzz] %s: %s\n", label, text.c_str());
}

std::string Count(const char* name, uint64_t value) {
    return std::string(name) + " " + std::to_string(value);
}

// ── 사례 (1) 문법 유도 생성기 → 컴파일 계약 ──────────────────────────────────
void TestFuzzGrammarDirectedCompile() {
    BeginCase();
    const SampleRange range = Samples(kGrammarSamples);
    uint64_t samples = 0, violations = 0;
    uint64_t multiLine = 0, loops = 0, natives = 0, nestedBlocks = 0, tokens = 0;

    for (uint64_t index = range.begin; index < range.end; ++index) {
        fz::Rng rng(fz::SampleSeed(masterSeed, index));
        fz::ProgramGenerator generator(rng, fz::GeneratorLimits{});
        const fz::Sample sample = generator.Generate();
        const std::string source = fz::Render(sample.tokens);

        ++samples;
        tokens += sample.tokens.size();
        if (fz::LineCount(source) > 2) ++multiLine;
        if (sample.hasLoop) ++loops;
        if (sample.hasNativeCall) ++natives;
        if (sample.hasNestedBlock) ++nestedBlocks;

        const std::string detail = GrammarCompileViolation(source);
        if (detail.empty()) continue;
        ++violations;
        const std::string wanted = ViolationClass(detail);
        const auto shrunk = fz::Shrink(sample.tokens, [&wanted](const fz::TokenList& candidate) {
            return ViolationClass(GrammarCompileViolation(fz::Render(candidate))) == wanted;
        });
        ReportCounterexample("fuzz_grammar_directed_compile", index, sample.tokens, shrunk,
                             detail);
    }

    CHECK(violations == 0);

    // 커버리지. 표본 수가 아니라 *무엇을 쳤는지*가 배터리의 강도다. 생성기가 한쪽으로
    // 쏠려 문장 한 종류만 만들면 위의 property는 여전히 통과하므로 여기서 막는다.
    CHECK(samples > 0);
    if (CoverageEnforced(samples)) {
        CHECK(loops * 4 >= samples);
        CHECK(natives * 4 >= samples);
        CHECK(nestedBlocks * 8 >= samples);
        CHECK(multiLine * 2 >= samples);
    }

    PrintCoverage("grammar",
                  Count("표본", samples) + " · " + Count("평균토큰", tokens / (samples ? samples : 1)) +
                      " · " + Count("여러줄", multiLine) + " · " + Count("루프", loops) + " · " +
                      Count("호스트호출", natives) + " · " + Count("중첩블록", nestedBlocks));
}

// ── 사례 (2) 바이트코드 변형 → 검증기 계약 ───────────────────────────────────
void TestFuzzBytecodeMutationVerifier() {
    BeginCase();
    const SampleRange range = Samples(kBytecodeSamples);
    uint64_t samples = 0, rejected = 0, accepted = 0;
    uint64_t opcodeMutations = 0, unknownOpcodeRejections = 0;
    uint64_t silentRejections = 0, badVerifyMessages = 0, badRuntimeMessages = 0, overBudget = 0;
    uint64_t generatorFailures = 0, unknownOpcodeAccepted = 0;

    for (uint64_t index = range.begin; index < range.end; ++index) {
        fz::Rng rng(fz::SampleSeed(masterSeed, index));
        fz::ProgramGenerator generator(rng, fz::GeneratorLimits{});
        const fz::Sample sample = generator.Generate();
        const auto compiled = lab::lang::Compiler{}.Compile(fz::Render(sample.tokens));
        if (!compiled.program) {
            ++generatorFailures;
            continue;
        }
        ++samples;

        bool touchedOpCode = false;
        const lab::lang::Program mutated =
            fz::MutateBytecode(*compiled.program, rng, touchedOpCode);
        if (touchedOpCode) ++opcodeMutations;

        std::string error;
        if (!lab::lang::Verify(mutated, error)) {
            ++rejected;
            if (error.empty()) ++silentRejections;
            else if (!fz::IsKnownVerifyMessage(error)) ++badVerifyMessages;
            if (error == "알 수 없는 명령 코드다") ++unknownOpcodeRejections;
            continue;
        }

        // 검증기가 통과시킨 손상 바이트코드는 VM이 안전하게 끝내야 한다. 여기서 프로세스가
        // 죽으면 그것이 곧 검증기의 구멍이다.
        ++accepted;
        // 통과한 프로그램에 미지의 명령 코드가 남아 있으면 VM의 이항 연산 분기가 빈 스택을
        // 읽는다. 실행 결과를 보기 전에 그 사실 자체를 단언한다 — 릴리스 빌드에서는 빈
        // 스택 접근이 조용히 지나갈 수 있어 결과만 봐서는 구멍이 보이지 않는다.
        for (const auto& instruction : mutated.code)
            if (static_cast<uint8_t>(instruction.op) > static_cast<uint8_t>(lab::lang::OpCode::Halt)) ++unknownOpcodeAccepted;  // 제품 술어를 되부르지 않는 독립 판정

        lab::lang::VirtualMachine vm;
        fz::BindStandardNatives(vm);
        const auto observation = fz::Observe(vm, mutated, kFuzzBudget, kFuzzStack);
        if (observation.instructions > kFuzzBudget + 1) ++overBudget;
        if (!observation.ok && !fz::IsKnownRuntimeMessage(observation.message))
            ++badRuntimeMessages;
    }

    CHECK(generatorFailures == 0);
    CHECK(silentRejections == 0);
    CHECK(badVerifyMessages == 0);
    CHECK(badRuntimeMessages == 0);
    CHECK(overBudget == 0);
    CHECK(unknownOpcodeAccepted == 0);

    // 이 배터리가 실제로 opcode 범위 밖 값을 만들고, 검증기가 그것을 잡는지. 두 카운터가
    // 0이면 위의 단언들은 아무것도 검사하지 않은 채 통과한 것이다.
    if (CoverageEnforced(samples)) {
        CHECK(opcodeMutations * 5 >= samples);
        CHECK(unknownOpcodeRejections * 20 >= samples);
        CHECK(rejected > 0);
        CHECK(accepted > 0);
    }

    PrintCoverage("bytecode", Count("표본", samples) + " · " + Count("거부", rejected) + " · " +
                                  Count("통과", accepted) + " · " +
                                  Count("opcode변형", opcodeMutations) + " · " +
                                  Count("미지opcode거부", unknownOpcodeRejections));
}

// ── 사례 (3) 실행 결정성과 자원 상한 ─────────────────────────────────────────
void TestFuzzExecutionDeterminismAndBudget() {
    BeginCase();
    const SampleRange range = Samples(kExecutionSamples);
    uint64_t samples = 0, violations = 0;
    uint64_t ranToHalt = 0, budgetStopped = 0, nonFinite = 0, dividedByZero = 0;
    uint64_t uninitialized = 0, unregistered = 0, tightStackStopped = 0, tightBudgetStopped = 0;

    for (uint64_t index = range.begin; index < range.end; ++index) {
        fz::Rng rng(fz::SampleSeed(masterSeed, index));
        fz::ProgramGenerator generator(rng, fz::GeneratorLimits{});
        const fz::Sample sample = generator.Generate();
        const std::string source = fz::Render(sample.tokens);
        ++samples;

        const std::string detail = ExecutionViolation(source);
        if (!detail.empty()) {
            ++violations;
            const std::string wanted = ViolationClass(detail);
            const auto shrunk =
                fz::Shrink(sample.tokens, [&wanted](const fz::TokenList& candidate) {
                    return ViolationClass(ExecutionViolation(fz::Render(candidate))) == wanted;
                });
            ReportCounterexample("fuzz_execution_determinism_and_budget", index, sample.tokens,
                                 shrunk, detail);
            continue;
        }

        // 관측 버킷. 실패 메시지의 종류가 곧 "어떤 방어가 실제로 발동했는가"다.
        const auto compiled = lab::lang::Compiler{}.Compile(source);
        lab::lang::VirtualMachine vm;
        fz::BindStandardNatives(vm);
        const auto wide = fz::Observe(vm, *compiled.program, kFuzzBudget, kFuzzStack);
        if (wide.ok) ++ranToHalt;
        else if (wide.message == "명령 실행 예산을 초과했다") ++budgetStopped;
        else if (wide.message == "연산 결과가 유한하지 않다") ++nonFinite;
        else if (wide.message == "0으로 나눌 수 없다") ++dividedByZero;
        else if (fz::StartsWith(wide.message, "초기화되지 않은 변수: ")) ++uninitialized;
        else if (fz::StartsWith(wide.message, "등록되지 않은 호스트 함수: ")) ++unregistered;

        lab::lang::VirtualMachine tight;
        fz::BindStandardNatives(tight);
        const auto limited = fz::Observe(tight, *compiled.program, kTightBudget, kTightStack);
        if (limited.message == "VM 스택 한도를 초과했다") ++tightStackStopped;
        else if (limited.message == "명령 실행 예산을 초과했다") ++tightBudgetStopped;
    }

    CHECK(violations == 0);

    // 여섯 실패 정책이 모두 실제로 발동해야 배터리가 그 정책들을 검사했다고 말할 수 있다.
    if (CoverageEnforced(samples)) {
        CHECK(ranToHalt * 4 >= samples);
        CHECK(budgetStopped > 0);
        CHECK(nonFinite > 0);
        CHECK(dividedByZero > 0);
        CHECK(uninitialized > 0);
        CHECK(unregistered > 0);
        CHECK(tightStackStopped + tightBudgetStopped > 0);
    }

    PrintCoverage("execution",
                  Count("표본", samples) + " · " + Count("정상종료", ranToHalt) + " · " +
                      Count("예산", budgetStopped) + " · " + Count("비유한", nonFinite) + " · " +
                      Count("0나눗셈", dividedByZero) + " · " + Count("미초기화", uninitialized) +
                      " · " + Count("미등록호출", unregistered) + " · " +
                      Count("좁은스택", tightStackStopped) + " · " +
                      Count("좁은예산", tightBudgetStopped));
}

// ── 사례 (4) 토큰 변형 → 실패 방식 계약 ──────────────────────────────────────
void TestFuzzTokenMutationDiagnostics() {
    BeginCase();
    const SampleRange range = Samples(kMutationSamples);
    uint64_t samples = 0, violations = 0, rejectedByCompiler = 0, stillCompiles = 0;
    uint64_t beyondFirstLine = 0, deepNestingRejections = 0, unknownCharacterRejections = 0;
    uint64_t distinctMessages = 0;
    std::vector<std::string> seenMessages;

    for (uint64_t index = range.begin; index < range.end; ++index) {
        fz::Rng rng(fz::SampleSeed(masterSeed, index));
        fz::ProgramGenerator generator(rng, fz::GeneratorLimits{});
        const fz::Sample sample = generator.Generate();
        unsigned kinds = 0;
        const fz::TokenList broken = fz::ApplyMutations(sample.tokens, rng, kinds);
        const std::string source = fz::Render(broken);
        ++samples;

        const std::string detail = MutationViolation(source);
        if (!detail.empty()) {
            ++violations;
            const std::string wanted = ViolationClass(detail);
            const auto shrunk = fz::Shrink(broken, [&wanted](const fz::TokenList& candidate) {
                return ViolationClass(MutationViolation(fz::Render(candidate))) == wanted;
            });
            ReportCounterexample("fuzz_token_mutation_diagnostics", index, broken, shrunk,
                                 detail);
            continue;
        }

        const auto compiled = lab::lang::Compiler{}.Compile(source);
        if (compiled.program) {
            ++stillCompiles;
            continue;
        }
        ++rejectedByCompiler;
        const auto& first = compiled.diagnostics.front();
        if (first.location.line > 1) ++beyondFirstLine;
        if (first.message == "중첩 깊이가 파서 한도를 초과했다") ++deepNestingRejections;
        if (fz::StartsWith(first.message, "알 수 없는 문자: ")) ++unknownCharacterRejections;

        bool seen = false;
        for (const std::string& message : seenMessages)
            if (message == first.message) seen = true;
        if (!seen) {
            seenMessages.push_back(first.message);
            ++distinctMessages;
        }
    }

    CHECK(violations == 0);

    // 커버리지. 특히 중첩 괄호는 이 lab이 과거에 실제로 죽었던 부류다. 그 표본이 0이면
    // 위의 property는 그 부류를 한 번도 치지 않은 것이다.
    if (CoverageEnforced(samples)) {
        CHECK(rejectedByCompiler * 2 >= samples);
        CHECK(stillCompiles > 0);
        CHECK(deepNestingRejections * 60 >= samples);
        CHECK(unknownCharacterRejections * 20 >= samples);
        CHECK(distinctMessages >= 10);
        // 진단 위치를 상수로 고정하면 이 줄이 먼저 죽는다.
        CHECK(beyondFirstLine * 4 >= rejectedByCompiler);
    }

    PrintCoverage("mutation",
                  Count("표본", samples) + " · " + Count("거부", rejectedByCompiler) + " · " +
                      Count("여전히컴파일", stillCompiles) + " · " +
                      Count("2줄이후진단", beyondFirstLine) + " · " +
                      Count("깊은중첩거부", deepNestingRejections) + " · " +
                      Count("미지문자거부", unknownCharacterRejections) + " · " +
                      Count("진단종류", distinctMessages));
}

// 등록표. 어느 계약이 어느 step에서 추가됐는지가 여기 한 곳에 적히고,
// CMake의 step별 ctest 타깃은 첫 칸의 라벨을 인자로 넘겨 이 표를 걸러낸다.
// snippet-begin: test-registry
struct TestCase {
    const char* step;
    const char* name;
    void (*run)();
};

constexpr TestCase kTestCases[] = {
    {"step01", "lexer_token_locations", TestLexerTokenLocations},
    {"step01", "lexer_rejects_unknown_character", TestLexerRejectsUnknownCharacter},
    {"step02", "precedence_and_variables", TestPrecedenceAndVariables},
    {"step02", "associativity_and_unary_precedence", TestAssociativityAndUnaryPrecedence},
    {"step02", "control_flow", TestControlFlow},
    {"step02", "parser_rejects_incomplete_expression", TestParserRejectsIncompleteExpression},
    {"step02", "fuzz_grammar_directed_compile", TestFuzzGrammarDirectedCompile},
    {"step03", "verifier_rejects_malformed_program", TestVerifierRejectsMalformedProgram},
    {"step03", "verifier_branch_contracts", TestVerifierBranchContracts},
    {"step03", "fuzz_bytecode_mutation_verifier", TestFuzzBytecodeMutationVerifier},
    {"step04", "runtime_value_diagnostics", TestRuntimeValueDiagnostics},
    {"step04", "budget_stops_untrusted_loop", TestBudgetStopsUntrustedLoop},
    {"step04", "stack_limit_stops_deep_expression", TestStackLimitStopsDeepExpression},
    {"step04", "parser_rejects_deep_nesting", TestParserRejectsDeepNesting},
    {"step04", "fuzz_execution_determinism_and_budget", TestFuzzExecutionDeterminismAndBudget},
    {"step04", "fuzz_token_mutation_diagnostics", TestFuzzTokenMutationDiagnostics},
    {"step05", "host_boundary", TestHostBoundary},
    {"step05", "host_boundary_rejects_unregistered_name", TestHostBoundaryRejectsUnregisteredName},
    {"step06", "bench_program_invariants", TestBenchProgramInvariants},
};
// snippet-end: test-registry

}  // namespace

// 인자가 없으면 전부, 있으면 step 라벨(`step01`) 또는 사례 이름과 정확히 일치하는
// 것만 실행한다. 필터가 아무것도 고르지 못하면 조용히 통과하지 않고 실패한다 —
// 오타 난 ctest 타깃이 "통과"로 보이는 것이 가장 나쁜 결과다.
//
// fuzz 배터리는 세 개의 선택 인자를 더 받는다. 기본값으로 도는 CI 실행과, 시드를 바꿔
// 오래 도는 탐색과, 반례 하나만 다시 도는 재현이 같은 실행 파일로 갈린다.
//   --seed=0x...     master 시드. 기본값은 소스에 박힌 상수다.
//   --samples=N      표본 수를 덮어쓴다.
//   --only=N         N번 표본 하나만 돈다(반례 보고가 찍어 주는 번호).
int main(int argc, char** argv) {
    const char* filter = nullptr;
    for (int i = 1; i < argc; ++i) {
        const char* argument = argv[i];
        if (std::strncmp(argument, "--seed=", 7) == 0) {
            masterSeed = std::strtoull(argument + 7, nullptr, 0);
        } else if (std::strncmp(argument, "--samples=", 10) == 0) {
            sampleOverride = std::strtoll(argument + 10, nullptr, 0);
            if (sampleOverride <= 0) {  // 0이나 오타(`--samples=abc`)는 "검사를 안 한 통과"가 된다
                std::fprintf(stderr, "표본 수는 양수여야 한다: %s\n", argument);
                return EXIT_FAILURE;
            }
        } else if (std::strncmp(argument, "--only=", 7) == 0) {
            onlyIndex = std::strtoll(argument + 7, nullptr, 0);
        } else if (std::strncmp(argument, "--", 2) == 0) {
            std::fprintf(stderr, "알 수 없는 옵션: %s\n", argument);
            return EXIT_FAILURE;
        } else if (!filter) {
            filter = argument;
        } else {
            std::fprintf(stderr, "필터는 하나만 받는다: %s\n", argument);
            return EXIT_FAILURE;
        }
    }

    int selected = 0;
    int failedCases = 0;

    for (const TestCase& test : kTestCases) {
        if (filter && std::strcmp(filter, test.step) != 0 &&
            std::strcmp(filter, test.name) != 0) {
            continue;
        }
        ++selected;
        const int before = failures;
        test.run();
        const bool ok = failures == before;
        if (!ok) ++failedCases;
        std::printf("[%s] %s %s\n", test.step, test.name, ok ? "ok" : "FAIL");
    }

    if (selected == 0) {
        std::fprintf(stderr, "선택된 테스트가 없다: %s\n", filter ? filter : "");
        return EXIT_FAILURE;
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed in %d case(s)\n", failures, failedCases);
        return EXIT_FAILURE;
    }
    if (filter)
        std::printf("languages_tests [%s]: all checks passed (%d case(s), %d check(s))\n",
                    filter, selected, checks);
    else
        std::printf("languages_tests: all checks passed (%d case(s), %d check(s))\n",
                    selected, checks);
    return EXIT_SUCCESS;
}
