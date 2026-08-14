#include "language.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
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

void TestDiagnostics() {
    const auto syntax = lab::lang::Compiler{}.Compile("let x = 1 + ;");
    CHECK(!syntax.program.has_value());
    CHECK(!syntax.diagnostics.empty());
    CHECK(syntax.diagnostics.front().location.line == 1);

    // 여러 줄 소스: 줄과 열이 모두 기본값 {1,1}이 아니어야 한다.
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

    // (a) 합류 불일치: 참 경로는 Pop으로 깊이를 1 줄이고 거짓 경로는 그대로
    // 통과해 인덱스 4에서 깊이 0과 1이 만난다.
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

}  // namespace

int main() {
    TestPrecedenceAndVariables();
    TestAssociativityAndUnaryPrecedence();
    TestControlFlow();
    TestHostBoundary();
    TestHostBoundaryRejectsUnregisteredName();
    TestDiagnostics();
    TestLexerTokenLocations();
    TestBudgetStopsUntrustedLoop();
    TestStackLimitStopsDeepExpression();
    TestParserRejectsDeepNesting();
    TestVerifierRejectsMalformedProgram();
    TestVerifierBranchContracts();
    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("languages_tests: all checks passed");
    return EXIT_SUCCESS;
}
