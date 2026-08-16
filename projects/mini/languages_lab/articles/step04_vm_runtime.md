## 들어가며

검증된 바이트코드도 무한히 실행되거나 비유한 값을 만들 수 있다. 시작 상태는 구조적으로
안전한 명령열이고, 완료 상태는 값 스택과 전역 환경을 갱신하면서도 실행 시간·메모리·수치
오류를 명시적으로 제한하는 VM이다.

실행기의 정확성은 opcode 결과뿐 아니라 실패했을 때 어디서 멈추고 어떤 상태를 노출하는지에
달려 있다.

---

## 목차

1. 디스패치 루프
2. 값 스택과 전역 환경
3. 수치 오류
4. 자원 예산
5. 검증

---

## 1. 디스패치 루프

VM은 프로그램 카운터로 명령을 가져오고 opcode에 따라 상태를 전이한다.

<!-- snippet: src/language.hpp#Run -->
```cpp
// language.hpp: VirtualMachine::Run 의 디스패치 루프 발췌
while (pc < program.code.size()) {
    const Instruction instruction = program.code[pc++];
    if (++executed > instructionBudget)
        return Failure(instruction.location, "명령 실행 예산을 초과했다", executed);

    switch (instruction.op) {
        case OpCode::Constant:
            if (auto failure = push(
                    program.constants[static_cast<size_t>(instruction.operand)],
                    instruction.location))
                return *failure;
            break;
        // ...
        case OpCode::Jump:
            pc = static_cast<size_t>(instruction.operand);
            break;
        // ...
        case OpCode::Halt:
            return {true, executed, std::nullopt};
        // ...
    }
}
```

점프 피연산자는 바이트 오프셋이 아니라 명령 인덱스다. 명령이 고정 크기 C++ 구조체인 현재
표현에서는 디코딩 경계 문제가 없지만, 파일용 가변 길이 인코딩을 사용하면 명령 시작
오프셋인지도 검증해야 한다.

## 2. 값 스택과 전역 환경

이항 연산은 오른쪽 값을 먼저 꺼내고 왼쪽 값을 꺼낸다. 뺄셈과 나눗셈은 순서를 바꾸면
결과가 달라진다.

<!-- snippet: src/language.hpp#Run -->
```cpp
// language.hpp: 이항 연산 처리 발췌
const double rhs = pop();
const double lhs = pop();
double value = 0.0;
switch (instruction.op) {
    case OpCode::Add: value = lhs + rhs; break;
    case OpCode::Subtract: value = lhs - rhs; break;
    // ...
}
```

전역 슬롯은 `optional<double>`이다. 기본값 0과 초기화되지 않은 상태를 구분해야 오타 난
이름이 조용히 0으로 실행되지 않는다.

<!-- snippet: src/language.hpp#Run -->
```cpp
// language.hpp: VirtualMachine::Run 의 LoadGlobal 처리 발췌
const size_t index = static_cast<size_t>(instruction.operand);
if (!globals_[index])
    return Failure(instruction.location,
                   "초기화되지 않은 변수: " + program.names[index],
                   executed);
if (auto failure = push(*globals_[index], instruction.location))
    return *failure;
```

VM 인스턴스를 실행할 때마다 전역 슬롯과 스택을 초기화한다. 한 실행의 상태가 다른 실행에
새어 들어가면 같은 프로그램과 입력이 다른 결과를 내는 숨은 상태가 생긴다.

## 3. 수치 오류

IEEE 754는 0 나눗셈과 오버플로에서 `inf`나 `NaN`을 만들 수 있다. 게임 밸런스 값에
비유한 수가 들어가면 비교가 항상 거짓이 되거나 직렬화가 플랫폼별로 달라질 수 있다.

<!-- snippet: src/language.hpp#Run -->
```cpp
// language.hpp: Divide 처리 발췌
case OpCode::Divide:
    if (rhs == 0.0)
        return Failure(instruction.location, "0으로 나눌 수 없다",
                       executed);
    value = lhs / rhs;
    break;
```

0 나눗셈은 명령별 가드지만 비유한 값은 명령별로 막지 않는다. 스택에 값을 올리는 모든
경로가 하나의 람다를 지나고, 그 람다가 유한성과 스택 한도를 함께 본다.

<!-- snippet: src/language.hpp#Run -->
```cpp
// language.hpp: 모든 적재 경로가 지나는 push 람다 전문
auto push = [&](double value, SourceLocation location) -> std::optional<RunResult> {
    if (!std::isfinite(value))
        return Failure(location, "연산 결과가 유한하지 않다", executed);
    if (stack_.size() >= stackLimit)
        return Failure(location, "VM 스택 한도를 초과했다", executed);
    stack_.push_back(value);
    return std::nullopt;
};
```

부동소수점 결과를 비트 단위 결정론으로 간주하지 않는다. 네트워크 권위 시뮬레이션에
스크립트 결과를 넣는다면 호스트에서만 실행하거나 고정소수점·정수 의미론을 별도로
정의해야 한다.

## 4. 자원 예산

`while (true)`에 해당하는 프로그램은 구조적으로 유효하다. VM은 명령 하나를 실행할 때마다
카운터를 올리고 한도를 넘으면 중단한다.

<!-- snippet: src/language.hpp#Run -->
```cpp
// language.hpp: VirtualMachine::Run 의 명령 예산 검사 발췌
if (++executed > instructionBudget)
    return Failure(instruction.location, "명령 실행 예산을 초과했다", executed);
```

스택에도 별도 상한을 둔다. 현재 문법은 재귀 함수가 없어 깊이가 작지만, 손상된 바이트코드나
기능 확장 뒤의 재귀가 프로세스 메모리를 제한 없이 소비하지 못하게 한다. 시간 예산과
메모리 예산은 서로 대체되지 않는다.

VM 예산은 실행 시점의 값 스택만 막는다. 컴파일 시점의 C++ 호출 스택은 별도 한도가
필요하므로 `Compiler`가 중첩 깊이를 `kMaxNestingDepth`(128)로 제한하고 초과하면 진단으로
거부한다. 이 한도가 없으면 깊게 중첩된 괄호만으로 VM 예산이 적용되기 전에 프로세스가
스택 오버플로로 죽는다.

## 5. 검증

무한 반복 프로그램을 낮은 명령 예산으로 실행한다.

<!-- snippet: src/tests.cpp#TestBudgetStopsUntrustedLoop -->
```cpp
auto execution = Execute("let x = 1; while (x > 0) { x = x + 1; }", 250);
CHECK(!execution.run.ok);
CHECK(execution.run.instructions == 251);
```

스택 상한은 `Run`의 세 번째 인자로만 관측된다. 기본값으로 도는 테스트만 있으면 상한 검사를
통째로 지워도 통과하므로, 같은 소스를 낮은 상한으로 한 번 더 실행해 대조한다.

<!-- snippet: src/tests.cpp#TestStackLimitStopsDeepExpression -->
```cpp
const char* source = "let x = 1 + (2 + (3 + (4 + (5 + 6))));";
// ...
auto ok = Execute(source);                  // 대조군: 기본 상한 256
CHECK(ok.compiled.program.has_value());
CHECK(ok.run.ok);
CHECK(Global(ok, "x") == 21.0);
// ...
auto limited = Execute(source, 100000, 4);  // 상한 4
CHECK(limited.compiled.program.has_value());
CHECK(!limited.run.ok);
CHECK(limited.run.diagnostic.has_value());
if (limited.run.diagnostic)
    CHECK(limited.run.diagnostic->message == "VM 스택 한도를 초과했다");
```

컴파일 시점 한도는 VM을 거치지 않으므로 실행 예산으로는 관측되지 않는다. 괄호 20,000겹을
넣어 `Program`이 만들어지지 않는 것과 진단 문구를 함께 확인한다. 한도를 지우면 이 사례는
실패가 아니라 프로세스 스택 오버플로로 죽는다.

<!-- snippet: src/tests.cpp#TestParserRejectsDeepNesting -->
```cpp
const int depth = 20000;
std::string source = "let x = ";
source.append(static_cast<size_t>(depth), '(');
// ...
const auto compiled = lab::lang::Compiler{}.Compile(source);
CHECK(!compiled.program.has_value());
CHECK(!compiled.diagnostics.empty());
CHECK(compiled.diagnostics.front().message == "중첩 깊이가 파서 한도를 초과했다");
```

0 나눗셈과 비유한 값은 서로를 가릴 수 있다. 0 나눗셈 가드를 지워도 결과가 무한대라 실행은
여전히 실패하기 때문에, `!run.ok`만 보는 단언은 두 정책 중 어느 쪽도 검증하지 못한다. 그래서
두 경로를 메시지까지 구분하고, 0 나눗셈을 거치지 않고 오버플로만으로 무한대에 도달하는
프로그램을 따로 둔다.

<!-- snippet: src/tests.cpp#TestRuntimeValueDiagnostics -->
```cpp
auto division = Execute("let x = 1 / 0;");
// ...
CHECK(division.run.diagnostic->message == "0으로 나눌 수 없다");

auto overflow = Execute("let x = 2; while (x > 0) { x = x * x; }");
// ...
CHECK(overflow.run.diagnostic->message == "연산 결과가 유한하지 않다");
```

두 번째 프로그램은 열 번 제곱해 `2^1024`에서 무한대가 된다. 유한성 검사를 지우면 무한 루프가
되어 메시지가 명령 예산 초과로 바뀌므로, 메시지 비교가 반드시 필요하다. 초기화되지 않은
전역 변수도 같은 방식으로 실패해야 한다.

네 사례는 등록표에서 `step04` 라벨을 달고 있다. 실행기의 정책 — 값 진단, 시간 예산,
스택 상한, 컴파일 시점 재귀 한도 — 만 이 타깃에 모인다.

```powershell
ctest --test-dir build -C Release -R languages_step04 --output-on-failure
.\build\Release\languages_tests.exe step04
.\build\Release\languages_demo.exe
```

```text
1/1 Test #5: languages_step04_vm_runtime ......   Passed
100% tests passed out of 1
[step04] runtime_value_diagnostics ok
[step04] budget_stops_untrusted_loop ok
[step04] stack_limit_stops_deep_expression ok
[step04] parser_rejects_deep_nesting ok
languages_tests [step04]: all checks passed (4 case(s), 20 check(s))
instructions=96 hp=270.271
```

네 이름이 네 가지 한도에 하나씩 대응한다. 어느 하나를 지우면 `ok`가 `FAIL`로 바뀌는
줄이 그 이름 하나뿐이므로, 어떤 정책이 무너졌는지가 출력에서 바로 읽힌다.

데모는 유한한 웨이브 체력 계산을 실행하고 실제 명령 수와 결과를 출력한다. 두 값은 소스가
바뀌지 않는 한 환경과 무관하게 같다.

---

## 결론

VM은 opcode를 계산하는 루프인 동시에 자원과 오류의 정책 집행자다. 초기화 여부를 값과
분리하고, 비유한 수를 차단하며, 명령 수와 스택 크기를 제한해야 신뢰하지 않는 스크립트가
게임 프로세스 전체를 점유하지 못한다. 검증기는 구조를 보장하고 VM은 실행 중에만 알 수
있는 값과 비용을 보장한다.
