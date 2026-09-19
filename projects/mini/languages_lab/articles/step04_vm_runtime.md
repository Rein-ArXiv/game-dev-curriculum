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
6. 생성 입력에서의 자원 상한

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
스택 상한, 컴파일 시점 재귀 한도 — 만 이 타깃에 모인다. 같은 타깃에 생성된 입력을 도는
사례가 둘 더 있고, 그것은 아래에서 따로 설명한다.

```powershell
ctest --test-dir build -C Release -R languages_step04 --output-on-failure
.\build\Release\languages_tests.exe step04
.\build\Release\languages_demo.exe
```

```text
1/1 Test #5: languages_step04_vm_runtime ......   Passed    1.76 sec
100% tests passed out of 1
[step04] runtime_value_diagnostics ok
[step04] budget_stops_untrusted_loop ok
[step04] stack_limit_stops_deep_expression ok
[step04] parser_rejects_deep_nesting ok
  [fuzz] execution: 표본 20000 · 정상종료 7799 · 예산 794 · 비유한 635 · 0나눗셈 1847 · 미초기화 7393 · 미등록호출 1532 · 좁은스택 1044 · 좁은예산 4882
[step04] fuzz_execution_determinism_and_budget ok
  [fuzz] mutation: 표본 20000 · 거부 19184 · 여전히컴파일 816 · 2줄이후진단 15876 · 깊은중첩거부 772 · 미지문자거부 4165 · 진단종류 30
[step04] fuzz_token_mutation_diagnostics ok
languages_tests [step04]: all checks passed (6 case(s), 35 check(s))
instructions=96 hp=270.271
```

처음 네 이름이 네 가지 한도에 하나씩 대응한다. 한도 하나를 지우면 값 진단과 스택 상한은 그
이름의 줄이 `FAIL`로 바뀌고(값 진단은 fuzz 커버리지 줄도 함께 죽는다), 시간 예산과 재귀 한도는
실행이 멈추거나 프로세스가 죽어 그 줄에 도달하지 못하므로, 어떤 정책이 무너졌는지가 출력에서 읽힌다.

데모는 유한한 웨이브 체력 계산을 실행하고 실제 명령 수와 결과를 출력한다. 두 값은 소스가
바뀌지 않는 한 환경과 무관하게 같다.

## 6. 생성 입력에서의 자원 상한

위의 네 사례는 각 한도마다 그 한도를 정확히 밟는 프로그램을 하나씩 손으로 준다. 그것은
한도가 *존재한다*는 것을 증명하지만, 임의의 프로그램에서 그 한도가 계속 유지된다는 것은
증명하지 않는다. 실행기 정책은 하나가 아니라 여섯 개이고, 어느 정책이 다른 정책을 가릴 수
있다. 그래서 생성된 프로그램에 같은 정책을 다시 걸어 본다.

### 실행 property

<!-- snippet: src/tests.cpp#ExecutionViolation -->
```cpp
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
// ...
```

같은 프로그램을 두 자원 한도로 각각 돌린다. 넓은 쪽은 명령 4,000·스택 256이고 좁은 쪽은
명령 64·스택 3이다. 좁은 쪽이 없으면 스택 상한 경로가 생성 표본에서 거의 밟히지 않는다 —
실제로 좁은 한도에서 1,044개 표본이 스택 상한에, 4,882개가 예산에 걸린다.

무한 반복을 시간으로 재지 않는다는 점이 중요하다. 벽시계를 읽는 순간 결과가 기계에 따라
흔들린다. 대신 명령 수가 예산 + 1을 넘지 않는다는 것을 값으로 확인하고, 진짜로 멈추지
않는 경우는 ctest의 타임아웃이 잡는다. 테스트 안에는 시계가 없다.

### 관측이 없으면 property도 없다

무력화 변이로 `Run`의 `stack_.clear()`를 지웠는데 배터리가 살아남았다. 이유는 실행 사이에
남은 스택 값이 *아무 명령에도 읽히지 않기* 때문이다. `Pop`은 맨 위를 꺼내고 `StoreGlobal`은
방금 올린 값을 읽으므로, 아래에 쌓인 찌꺼기는 결과를 바꾸지 않는다.

이것은 equivalent mutant처럼 보이지만 아니었다. 관측이 부족했던 것이다. `VirtualMachine`에
값 스택 높이를 읽는 접근자를 더하고 관측에 포함하자 변이가 죽었다. 스택 높이는 원래
계약의 일부다 — 컴파일러가 내는 모든 문장이 스택 중립이므로 `Halt`에 도달한 실행은 높이
0으로 끝나야 하고, 실행 사이에 이월되어서도 안 된다.

```text
FUZZ 반례 [fuzz_execution_determinism_and_budget] 표본 4
  실패: 같은 VM으로 다시 실행하니 결과가 달라졌다(상태 누수)
  축소: 토큰 127 -> 21
  재현: languages_tests.exe fuzz_execution_determinism_and_budget --seed=0x5EED10CC1A2B3C4D --only=4
```

### 토큰 변형과 실패의 방식

두 번째 배터리는 유효 프로그램을 토큰 단위로 깨뜨린다. 바이트 단위 변형과 달리 토큰 경계를
유지하므로 렉서에서 바로 죽지 않고 파서 안쪽까지 도달하는 입력이 나온다.

<!-- snippet: src/fuzz.hpp@fuzz-mutator -->
```cpp
if (roll < 20) {
    kind = Mutation::Delete;
    tokens.erase(tokens.begin() + static_cast<std::ptrdiff_t>(position));
} else if (roll < 34) {
    kind = Mutation::Duplicate;
    tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(position),
                  tokens[position]);
// ...
} else {
    kind = Mutation::DeepNesting;
    tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(position),
                  std::string(static_cast<size_t>(rng.Between(160, 600)), '('));
}
```

거부는 실패가 아니다. 거부의 *방식*이 계약이다.

<!-- snippet: src/tests.cpp#MutationViolation -->
```cpp
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
// ...
```

진단 어휘를 닫는 것 자체가 property다. 통제되지 않은 새 실패 경로가 생기면 목록에 없는
문자열이 나오고 배터리가 그것을 잡는다. 위치는 줄 번호와 그 줄의 길이를 모두 본다. 생성
소스가 여러 줄이므로, 20,000표본 중 거부된 19,184개 가운데 15,876개의 진단이 두 번째 줄
이후를 가리킨다. 위치 추적을 상수로 바꾸면 이 비율이 0이 되어 단언이 먼저 죽는다.

### 과거 실결함의 부류를 실제로 치는가

이 lab이 깊게 중첩된 괄호에서 프로세스 스택으로 죽었던 적이 있다. 그래서 손으로 만든
사례가 괄호 20,000겹을 넣어 파서 한도를 확인한다. 그 사례는 정확히 한 모양만 본다 —
`let x = ((((1))));` 형태다.

변형기는 괄호 160~600겹을 표본의 임의 위치에 끼운다. 문장 시작이든 표현식 중간이든
블록 안이든 파서는 결국 같은 재귀에 들어가므로 한도가 걸려야 한다. 20,000표본 중 772개가
`중첩 깊이가 파서 한도를 초과했다`로 거부되고, 배터리는 그 수가 0이 아니라는 것에 단언을
건다. 한도를 지우면 이 표본들은 실패가 아니라 프로세스 종료가 된다.

### 무력화 변이

`Consume`이 내는 진단 문구를 등록되지 않은 문자열로 바꿨다. 기존 사례 중 이 경로의 문구를
비교하는 것이 하나도 없어서, 열다섯 개가 전부 `ok`인 채 이 배터리만 죽는다.

```text
[step04] parser_rejects_deep_nesting ok
[step04] fuzz_execution_determinism_and_budget ok
[step04] fuzz_token_mutation_diagnostics FAIL

FUZZ 반례 [fuzz_token_mutation_diagnostics] 표본 0
  실패: 닫힌 진단 어휘 밖의 메시지: 구문 오류
  축소: 토큰 27 -> 1
  재현: languages_tests.exe fuzz_token_mutation_diagnostics --seed=0x5EED10CC1A2B3C4D --only=0
  소스:
    while
```

27토큰이 한 토큰으로 줄었다. `while` 하나만으로 그 경로가 재현된다는 것이 반례의 전부다.

```powershell
.\build\Release\languages_tests.exe fuzz_execution_determinism_and_budget
.\build\Release\languages_tests.exe fuzz_token_mutation_diagnostics
```

```text
  [fuzz] execution: 표본 20000 · 정상종료 7799 · 예산 794 · 비유한 635 · 0나눗셈 1847 · 미초기화 7393 · 미등록호출 1532 · 좁은스택 1044 · 좁은예산 4882
[step04] fuzz_execution_determinism_and_budget ok
languages_tests [fuzz_execution_determinism_and_budget]: all checks passed (1 case(s), 8 check(s))
  [fuzz] mutation: 표본 20000 · 거부 19184 · 여전히컴파일 816 · 2줄이후진단 15876 · 깊은중첩거부 772 · 미지문자거부 4165 · 진단종류 30
[step04] fuzz_token_mutation_diagnostics ok
languages_tests [fuzz_token_mutation_diagnostics]: all checks passed (1 case(s), 7 check(s))
```

`여전히컴파일 816`이 중요한 숫자다. 깨뜨린 소스의 4%는 여전히 유효한 프로그램이고, 그것들은
거부 계약이 아니라 실행 계약으로 넘어간다. 이 수가 0이면 배터리는 "깨진 것은 거부된다"만
확인하는 것이라 실행 경로를 한 번도 밟지 않는다.

---

## 결론

VM은 opcode를 계산하는 루프인 동시에 자원과 오류의 정책 집행자다. 초기화 여부를 값과
분리하고, 비유한 수를 차단하며, 명령 수와 스택 크기를 제한해야 신뢰하지 않는 스크립트가
게임 프로세스 전체를 점유하지 못한다. 검증기는 구조를 보장하고 VM은 실행 중에만 알 수
있는 값과 비용을 보장한다.
