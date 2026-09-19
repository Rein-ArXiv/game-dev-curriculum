## 들어가며

토큰열만으로는 `2 + 3 * 4`가 14인지 20인지 결정되지 않는다. 시작 상태는 토큰의 선형
순서이고, 완료 상태는 우선순위와 결합 법칙이 반영된 바이트코드다. 이 구현은 별도 AST를
보관하지 않고 파싱하면서 명령을 방출하는 구문 지향 컴파일 방식을 사용한다.

AST를 생략했다고 문법이 사라지는 것은 아니다. 각 접두 표현과 중위 표현이 어떤 우선순위에서
소비되는지, 문장이 스택에 값을 남기는지 제거하는지가 컴파일러 구조에 명시된다.

---

## 목차

1. 문법
2. 우선순위 상승 파싱
3. 선언과 할당
4. 조건과 반복의 백패치
5. 검증
6. 문법 유도 fuzz

---

## 1. 문법

실험 언어의 핵심 문법은 다음과 같다.

```text
program     -> statement* EOF
statement   -> "let" IDENT "=" expression ";"
             | IDENT "=" expression ";"
             | "if" "(" expression ")" block ("else" block)?
             | "while" "(" expression ")" block
             | expression ";"
block       -> "{" statement* "}"
expression  -> equality
equality    -> comparison (("==" | "!=") comparison)*
comparison  -> term (("<" | "<=" | ">" | ">=") term)*
term        -> factor (("+" | "-") factor)*
factor      -> unary (("*" | "/") unary)*
unary       -> "-" unary | primary
primary     -> NUMBER | "true" | "false" | IDENT
             | IDENT "(" expression ")" | "(" expression ")"
```

문장 끝의 세미콜론은 선택이 아니다. 줄바꿈이 의미를 갖지 않으므로 문장 경계를 명시해야
오류 복구 지점과 소스 포매팅이 분리된다.

## 2. 우선순위 상승 파싱

파서는 현재 허용하는 최소 우선순위를 받고, 그보다 강한 중위 연산자를 계속 소비한다.

<!-- snippet: src/language.hpp@parse-precedence mode=exact -->
```cpp
// language.hpp: Compiler::ParsePrecedence 전문
void ParsePrecedence(Precedence minimum) {
    if (!EnterNesting(Peek().location)) {
        LeaveNesting();
        return;
    }
    Prefix();
    while (diagnostics_->empty() &&
           static_cast<int>(InfixPrecedence(Peek().kind)) >=
               static_cast<int>(minimum) &&
           InfixPrecedence(Peek().kind) != Precedence::None) {
        const Token op = tokens_[current_++];
        const auto precedence = InfixPrecedence(op.kind);
        ParsePrecedence(static_cast<Precedence>(static_cast<int>(precedence) + 1));
        Emit(BinaryOp(op.kind), 0, op.location);
    }
    LeaveNesting();
}
```

오른쪽 피연산자를 한 단계 높은 우선순위로 파싱하므로 같은 우선순위의 `-`와 `/`는
왼쪽 결합이 된다. `8 - 3 - 2`는 `(8 - 3) - 2`로 컴파일된다. 괄호는 내부 표현식을
최저 우선순위부터 다시 파싱해 이 규칙을 명시적으로 덮는다.

## 3. 선언과 할당

이름은 프로그램의 문자열 테이블에 한 번만 보관하고 명령에는 정수 인덱스를 넣는다.

<!-- snippet: src/language.hpp#VariableDeclaration -->
```cpp
// language.hpp: Compiler::VariableDeclaration 중 이름 인턴 부분 발췌
Expression();
// ...
Emit(OpCode::StoreGlobal, InternName(name.text), name.location);
Emit(OpCode::Pop, 0, name.location);
```

`StoreGlobal`은 대입식의 값을 스택에 유지한다. 문장은 그 값을 사용하지 않으므로 `Pop`으로
스택 효과를 0으로 맞춘다. 이 규칙 덕분에 모든 정상적인 문장 경계에서 스택 깊이가 같다.

`let`은 가독성 표지이며 bare 할당과 완전히 동일한 바이트코드를 낸다. 심볼 테이블이 없어
중복 `let` 선언도, `let` 없는 미선언 대입도 거부하지 않는다. 즉 오타 난 대입은 진단 없이
새 전역을 만든다. 선언을 실제 계약으로 만들려면 컴파일 시점 이름 집합이 필요하다.

## 4. 조건과 반복의 백패치

분기 대상을 알기 전에 점프 명령을 방출해야 한다. 컴파일러는 임시 대상 `-1`을 기록하고
블록의 끝을 알게 된 시점에 절대 명령 인덱스로 고친다.

<!-- snippet: src/language.hpp#IfStatement -->
```cpp
// language.hpp: Compiler::IfStatement 중 조건 뒤 부분 발췌
const int32_t falseJump = Emit(OpCode::JumpIfFalse, -1, Previous().location);
Emit(OpCode::Pop, 0, Previous().location);
Block();
const int32_t endJump = Emit(OpCode::Jump, -1, Previous().location);
PatchJump(falseJump);
Emit(OpCode::Pop, 0, Previous().location);
if (Match(TokenKind::Else)) Block();
PatchJump(endJump);
```

`JumpIfFalse`는 조건값을 읽되 제거하지 않는다. 참 경로와 거짓 경로가 각각 하나의 `Pop`을
실행해야 합류점의 스택 깊이가 같아진다. 반복문은 조건 위치를 기록하고 본문 끝에서 그
위치로 역방향 점프한다.

## 5. 검증

테스트는 우선순위와 괄호가 다른 결과를 내는지 확인한다.

<!-- snippet: src/tests.cpp#TestPrecedenceAndVariables -->
```cpp
auto execution = Execute("let a = 2 + 3 * 4; let b = (2 + 3) * 4;");
// ...
CHECK(Global(execution, "a") == 14.0);
CHECK(Global(execution, "b") == 20.0);
```

`+`와 `*`는 위처럼 정확히 표현되는 작은 정수에서는 결합 방향이 값에 드러나지 않는다(부동소수점 일반에서는 결합 법칙이 성립하지 않는다). 위 두 줄만으로는
`ParsePrecedence(precedence + 1)`을 `ParsePrecedence(precedence)`로 바꿔 파서를 우결합으로
만들어도 통과한다. 결합 방향을 고정하려면 비결합 연산자를 써야 한다.

<!-- snippet: src/tests.cpp#TestAssociativityAndUnaryPrecedence -->
```cpp
auto execution = Execute(
    "let a = 8 - 3 - 2;"     // 좌결합 3, 우결합이면 7
    "let b = 16 / 4 / 2;"    // 좌결합 2, 우결합이면 8
    "let c = 1 < 2 == 1;"    // 비교가 상등보다 강하다
    "let d = -2 - 3;");      // 단항 마이너스의 우선순위
// ...
CHECK(Global(execution, "a") == 3.0);
CHECK(Global(execution, "b") == 2.0);
CHECK(Global(execution, "c") == 1.0);
CHECK(Global(execution, "d") == -5.0);
```

팩토리얼 프로그램은 선언, 비교, 반복, 할당, 조건 분기를 한 번에 통과한다. 백패치가
어긋나면 반복이 끝나지 않거나 `else` 경로가 실행되어 값이 121이 아니게 된다.

<!-- snippet: src/tests.cpp#TestControlFlow -->
```cpp
auto execution = Execute(
    "let n = 5; let fact = 1;"
    "while (n > 0) { fact = fact * n; n = n - 1; }"
    "if (fact == 120) { fact = fact + 1; } else { fact = 0; }");
CHECK(execution.run.ok);
CHECK(Global(execution, "fact") == 121.0);
```

컴파일 오류에는 `Program`이 없어야 하며 진단 위치가 소스 범위 안이어야 한다. 실행 산출물과
오류 산출물이 동시에 나오지 않는다는 불변식이 여기서 확인된다.

<!-- snippet: src/tests.cpp#TestParserRejectsIncompleteExpression -->
```cpp
const auto syntax = lab::lang::Compiler{}.Compile("let x = 1 + ;");
CHECK(!syntax.program.has_value());
CHECK(!syntax.diagnostics.empty());
CHECK(syntax.diagnostics.front().location.line == 1);
```

네 사례는 등록표에서 `step02` 라벨을 달고 있고, 같은 라벨의 ctest 타깃이 그것만 부른다.
같은 타깃에 생성된 입력을 도는 사례가 하나 더 있는데, 그것은 아래에서 따로 설명한다.

```powershell
ctest --test-dir build -C Release -R languages_step02 --output-on-failure
.\build\Release\languages_tests.exe step02
```

```text
1/1 Test #3: languages_step02_precedence_compiler ...   Passed    0.66 sec
100% tests passed out of 1
[step02] precedence_and_variables ok
[step02] associativity_and_unary_precedence ok
[step02] control_flow ok
[step02] parser_rejects_incomplete_expression ok
  [fuzz] grammar: 표본 20000 · 평균토큰 72 · 여러줄 20000 · 루프 10874 · 호스트호출 14592 · 중첩블록 7437
[step02] fuzz_grammar_directed_compile ok
languages_tests [step02]: all checks passed (5 case(s), 21 check(s))
```

출력의 처음 네 이름이 이 절의 네 주장 — 우선순위, 결합 방향, 백패치된 제어 흐름, 구문 오류
거부 — 과 하나씩 짝을 이룬다. 렉서 계약은 이 타깃에 없다. 어휘 계약은 자기 라벨로
따로 돌아가므로, 여기서 실패가 나면 원인은 파서·컴파일러 쪽이다.

## 6. 문법 유도 fuzz

위의 네 사례는 사람이 고른 프로그램이다. 각 사례가 무엇을 못 박는지는 분명하지만, 문법이
만들 수 있는 프로그램의 *조합*은 덮지 못한다. 선언과 반복과 호스트 호출이 각각 옳아도
셋이 겹친 어떤 모양에서만 어긋날 수 있고, 그런 모양은 손으로 고르기 어렵다.

그래서 같은 계약을 생성된 입력에서 다시 검사한다. 여기서 쓰는 구분은 넷이다.

- **property**: 생성된 모든 입력이 지켜야 하는 관계. 특정 입력의 기대 출력이 아니다.
- **oracle**: 그 관계를 판정하는 근거.
- **shrink**: 실패를 유지한 채 입력을 줄여 읽을 수 있는 반례로 만드는 것.
- **fuzz**: 무작위 생성과 변형으로 property를 깨려는 탐색.

### 생성기

생성기는 이 글 첫 절의 문법 표를 그대로 따라간다. 무효 입력은 만들지 않는다 — 유효와
무효를 한 생성기에 섞으면 실패가 났을 때 계약 위반인지 생성기 사고인지 구분되지 않는다.

<!-- snippet: src/fuzz.hpp@fuzz-generator -->
```cpp
// fuzz.hpp: ProgramGenerator 의 표현식 생성 발췌
void Expression(TokenList& out, int depth) {
    if (depth >= limits_.maxExpressionDepth) {
        Primary(out, depth);
        return;
    }
    const uint32_t roll = rng_.Below(100);
    if (roll < 45) {
        Expression(out, depth + 1);
        const std::string op = BinaryOperator();
        out.push_back(op);
        if (op == "/" && rng_.Chance(12)) {
            out.push_back("0");
            sample_.hasLiteralZeroDivisor = true;
        } else {
            Expression(out, depth + 1);
        }
    } else if (roll < 60) {
        out.push_back("-");
        Expression(out, depth + 1);
    // ...
    }
}
```

세 가지가 균등 난수와 다르다. 첫째, 이름은 앞에서 선언한 것 중에서 고른다 — 피연산자가
앞 문맥에 의존하는 생성기다. 둘째, 숫자 리터럴은 균등 분포가 아니라 `0`·`1`과 자릿수가
매우 큰 값에 비중을 명시적으로 준다. 균등 난수는 경계를 거의 만들지 못한다. 셋째, `/`의
오른쪽에 리터럴 `0`을 넣는 확률을 따로 둔다.

생성된 소스는 `;`, `{`, `}` 뒤에서 줄을 바꾼다. 한 줄짜리 소스만 만들면 `SourceLocation`의
기본값이 `{1, 1}`이라 위치 추적을 통째로 지워도 통과한다.

### property와 oracle

이 언어에는 "정답 바이트코드"의 표가 없다. 그래서 고정 기대값 대신 부분 oracle 넷을 쓴다.

<!-- snippet: src/tests.cpp#GrammarCompileViolation -->
```cpp
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
```

네 번째가 특히 강하다. 검증기는 컴파일러와 독립적으로 스택 깊이를 계산하므로, 컴파일러가
스택 효과를 어긋나게 방출하면 값 하나 비교하지 않고도 잡힌다. 판정기를 한 함수로 둔 이유는
본 루프와 축소기가 같은 실패 정의를 쓰게 하기 위해서다.

### 시드와 재현

난수는 전부 소스에 박힌 master 시드에서 나온다. 표본 `i`의 시드는 master를 그냥 증가시킨
값이 아니라 섞은 값이다 — 인접 표본이 상관되면 같은 모양이 반복된다.

<!-- snippet: src/fuzz.hpp@fuzz-rng -->
```cpp
// fuzz.hpp: 표본 시드 유도
inline uint64_t SampleSeed(uint64_t master, uint64_t index) {
    uint64_t z = master + 0x9E3779B97F4A7C15ull * (index + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
```

실행 파일은 `--seed=`, `--samples=`, `--only=` 세 인자를 받는다. 기본값으로 도는 CI 실행과,
시드를 바꿔 넓게 도는 탐색과, 반례 하나만 다시 도는 재현이 같은 실행 파일에서 갈린다.

### 축소가 실패의 부류를 바꾸면 안 된다

축소기는 실패를 유지하는 토큰 삭제만 남기는 구간 삭제 delta debugging이다. 처음 판본은
"아무 실패나 유지"였는데, 컴파일러 결함을 주입해 보니 축소된 반례가 `;` 한 토큰이었다.
토큰을 지우다 문법이 깨져 결함과 무관한 구문 오류에 도달한 것이다. 그래서 판정기가 내는
문자열의 가변부 앞을 실패의 *부류*로 보고, 축소는 부류가 같은 후보만 받는다.

### 표본 수의 근거

이 기계에서 표본 하나가 30~60마이크로초다. 사례당 20,000표본이 0.6~1.1초이고, 사례
열아홉 개를 전부 도는 실행이 3~4초다. ctest는 전체 타깃과 계약별 타깃을 따로 돌아 배터리를
두 번 실행하므로 타깃 일곱 개의 합계가 6초 안팎이다. 커버리지 하한은 500표본에서 이미
채워지므로 나머지는 드문 조합을 만날 확률에 쓰는 몫이고, 시간은 표본 수에 선형이라 한
사례를 200,000표본으로 돌리면 9~11초다.

출력의 `[fuzz] grammar` 줄이 이 배터리가 무엇을 실제로 쳤는지 센다. 20,000표본 중 루프를
가진 것이 10,874개, 호스트 호출이 14,592개, 블록이 겹친 것이 7,437개다. 이 수가 0이 되면
property는 여전히 통과하지만 아무것도 검사하지 않은 것이므로, 배터리는 이 비율에도
단언을 건다.

### 무력화 변이

검증이 실제로 힘을 갖는지는 결함을 주입해 확인한다. `Prefix`의 `true` 처리에서
`Emit(OpCode::Constant, ...)` 한 줄을 지웠다. 기존 사례 중 `true`나 `false`를 쓰는 것은
하나도 없어서, 열다섯 개 사례가 전부 `ok`인 채 생성 배터리만 죽는다 — 이 배터리와, `true`를 변형
어휘에 두는 토큰 변형 배터리다.

```text
[step02] parser_rejects_incomplete_expression ok
[step02] fuzz_grammar_directed_compile FAIL

FUZZ 반례 [fuzz_grammar_directed_compile] 표본 2
  실패: 컴파일러가 검증을 통과하지 못하는 바이트코드를 냈다: 스택 언더플로가 가능한 바이트코드다
  축소: 토큰 30 -> 10
  재현: languages_tests.exe fuzz_grammar_directed_compile --seed=0x5EED10CC1A2B3C4D --only=2
  소스:
    ( true ) == ( ( 4 ) ) ;
```

30토큰짜리 생성 프로그램이 10토큰으로 줄고, 남은 것이 정확히 결함이 있는 구문이다. 마지막
줄의 명령은 그 표본 하나만 다시 돌린다. 결함을 되돌리면 같은 명령이 다시 통과한다.

```powershell
.\build\Release\languages_tests.exe fuzz_grammar_directed_compile
```

```text
  [fuzz] grammar: 표본 20000 · 평균토큰 72 · 여러줄 20000 · 루프 10874 · 호스트호출 14592 · 중첩블록 7437
[step02] fuzz_grammar_directed_compile ok
languages_tests [fuzz_grammar_directed_compile]: all checks passed (1 case(s), 6 check(s))
```

배터리가 덮지 못하는 것도 분명하다. 생성기는 이 문법이 허용하는 모양만 만들므로 문법
자체의 공백 — 예를 들어 심볼 테이블이 없어 오타 난 대입이 새 전역을 만드는 것 — 은
어떤 표본으로도 드러나지 않는다. 그것은 생성기가 아니라 계약을 고쳐야 보인다.

---

## 결론

우선순위 파서는 토큰 순서를 실행 순서로 바꾸는 핵심 계약이다. 구문 지향 방출은 작은
언어에서 AST 메모리를 없애지만, 의미 분석과 최적화용 중간 표현을 보관하기 어렵다는
대가가 있다. 이 구현은 문장 경계의 스택 효과를 0으로 만들고 점프를 백패치해, 이후 검증기가
제어 흐름을 정적으로 검사할 수 있는 바이트코드를 만든다.
