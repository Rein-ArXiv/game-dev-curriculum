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

```cpp
Expression();
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

```cpp
auto execution =
    Execute("let a = 2 + 3 * 4; let b = (2 + 3) * 4;");
CHECK(Global(execution, "a") == 14.0);
CHECK(Global(execution, "b") == 20.0);
```

`+`와 `*`는 결합 법칙이 성립해 결합 방향이 값에 드러나지 않는다. 위 두 줄만으로는
`ParsePrecedence(precedence + 1)`을 `ParsePrecedence(precedence)`로 바꿔 파서를 우결합으로
만들어도 통과한다. 결합 방향을 고정하려면 비결합 연산자를 써야 한다.

```cpp
auto execution = Execute(
    "let a = 8 - 3 - 2;"     // 좌결합 3, 우결합이면 7
    "let b = 16 / 4 / 2;"    // 좌결합 2, 우결합이면 8
    "let c = 1 < 2 == 1;"    // 비교가 상등보다 강하다
    "let d = -2 - 3;");      // 단항 마이너스의 우선순위
CHECK(Global(execution, "a") == 3.0);
CHECK(Global(execution, "b") == 2.0);
CHECK(Global(execution, "c") == 1.0);
CHECK(Global(execution, "d") == -5.0);
```

팩토리얼 프로그램은 선언, 비교, 반복, 할당, 조건 분기를 한 번에 통과한다. 컴파일 오류에는
`Program`이 없어야 하며 진단 위치가 소스 범위 안이어야 한다.

```powershell
.\build\Release\languages_tests.exe
```

```text
languages_tests: all checks passed
```

---

## 결론

우선순위 파서는 토큰 순서를 실행 순서로 바꾸는 핵심 계약이다. 구문 지향 방출은 작은
언어에서 AST 메모리를 없애지만, 의미 분석과 최적화용 중간 표현을 보관하기 어렵다는
대가가 있다. 이 구현은 문장 경계의 스택 효과를 0으로 만들고 점프를 백패치해, 이후 검증기가
제어 흐름을 정적으로 검사할 수 있는 바이트코드를 만든다.
