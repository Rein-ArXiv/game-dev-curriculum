## 들어가며

스크립트 실행기의 첫 경계는 문자를 토큰으로 바꾸는 렉서다. 시작 상태는 입력이 단순한
문자열이라는 사실뿐이다. 완료 상태에서는 숫자, 식별자, 키워드, 구두점이 종류와 원문,
줄·열 위치를 가진 토큰열이 되며 알 수 없는 문자는 진단으로 거부된다.

이 실험의 핵심은 성공 입력보다 실패 입력이다. 숫자를 어디까지 소비하는지, `=`와 `==`를
어떤 순서로 구분하는지, 주석과 나눗셈을 어떻게 나누는지 결정하지 않으면 파서가 같은
소스를 서로 다른 언어로 해석할 수 있다.

---

## 목차

1. 어휘 계약
2. 최대 길이 일치와 위치
3. 숫자와 식별자
4. 오류를 토큰으로 숨기지 않기
5. 검증

---

## 1. 어휘 계약

렉서는 문법의 가장 낮은 층이다. 이 구현은 다음 토큰을 인정한다.

<!-- snippet: src/language.hpp#TokenKind mode=exact -->
```cpp
enum class TokenKind {
    Number,
    Identifier,
    Let,
    If,
    Else,
    While,
    True,
    False,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    Semicolon,
    Plus,
    Minus,
    Star,
    Slash,
    Equal,
    EqualEqual,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    End,
};
```

숫자와 식별자, 여섯 개의 키워드, 구두점, 산술·비교 연산자, 그리고 입력 끝을 나타내는
`End`가 전부다.

토큰은 종류만 저장하지 않는다. 파서와 VM의 오류를 원래 소스로 되돌리려면 위치가 끝까지
보존되어야 한다.

<!-- snippet: src/language.hpp#Token mode=exact -->
```cpp
struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    double number = 0.0;
    SourceLocation location;
};
```

`text`는 진단과 이름 테이블에 사용하고, `number`는 숫자 리터럴을 다시 파싱하지 않도록
캐시한다.

## 2. 최대 길이 일치와 위치

`=`를 먼저 확정하고 뒤의 `=`를 별도 토큰으로 만들면 `==`가 할당 두 번으로 깨진다. 렉서는
같은 접두사를 가진 후보 중 가장 긴 것을 우선해야 한다.

<!-- snippet: src/language.hpp#ScanPunctuation -->
```cpp
// language.hpp: Lexer::ScanPunctuation 의 두 글자 후보 처리 발췌
auto oneOrTwo = [&](char expected, TokenKind oneKind, TokenKind twoKind) {
    if (Peek() == expected) {
        Advance();
        tokens.push_back({twoKind, std::string() + c + expected, 0.0, start});
    } else {
        one(oneKind);
    }
};
// ...
case '=': oneOrTwo('=', TokenKind::Equal, TokenKind::EqualEqual); break;
```

위치는 토큰 소비가 아니라 문자 소비와 함께 갱신한다. 줄바꿈은 줄을 하나 올리고 열을 1로
되돌린다. 그러면 공백과 주석을 버려도 뒤 토큰의 원래 좌표가 유지된다.

## 3. 숫자와 식별자

숫자는 정수부와, 뒤에 숫자가 실제로 존재하는 소수부만 소비한다. `1.`을 허용할지 여부는
언어 계약이며 여기서는 거부한다. 변환은 `std::from_chars`의 종료 포인터와 오류 코드를
둘 다 검사한다.

<!-- snippet: src/language.hpp#ScanNumber -->
```cpp
const auto result = std::from_chars(begin, end, value);
if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value))
    diagnostics.push_back({start, "유한한 숫자 리터럴이 아니다"});
```

식별자는 ASCII 문자와 밑줄로 시작하고 뒤에는 숫자도 허용한다. 스캔 뒤 키워드 표에서
`let`, `if`, `else`, `while`, `true`, `false`를 재분류한다. 키워드를 문자별 분기로
하드코딩하지 않아 예약어 집합과 스캔 규칙을 분리한다.

## 4. 오류를 토큰으로 숨기지 않기

알 수 없는 문자를 식별자로 취급하거나 건너뛰면 잘못된 프로그램이 다른 의미로 실행된다.
`@` 같은 문자는 위치가 붙은 진단이 된다.

<!-- snippet: src/language.hpp#ScanPunctuation -->
```cpp
diagnostics.push_back({start, std::string("알 수 없는 문자: ") + c});
```

렉서는 가능한 범위까지 계속 스캔해 여러 오류를 모을 수 있지만, 컴파일러는 진단이 하나라도
있으면 `Program`을 만들지 않는다. 실행 가능한 산출물과 오류 산출물을 동시에 내놓지 않는
것이 이 경계의 불변식이다.

## 5. 검증

빌드한 뒤 어휘 계약만 골라 실행한다. 테스트 실행 파일은 등록표에서 `step01` 라벨이
붙은 사례만 걸러 돌리고, 같은 라벨의 ctest 타깃이 그 인자로 실행 파일을 부른다.

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -R languages_step01 --output-on-failure
.\build\Release\languages_tests.exe step01
```

```text
1/1 Test #2: languages_step01_lexical_contract ...   Passed
100% tests passed out of 1
[step01] lexer_token_locations ok
[step01] lexer_rejects_unknown_character ok
languages_tests [step01]: all checks passed (2 case(s), 24 check(s))
```

출력의 두 사례 이름이 이 절이 주장하는 것과 그대로 대응한다 — 토큰 위치 보존과 알 수 없는
문자 거부다. 24라는 단언 수는 두 사례가 실제로 무엇을 몇 개 못 박았는지를 보여 준다.
전체를 한 번에 돌리려면 인자 없이 `ctest --test-dir build -C Release --output-on-failure`를
실행한다. 이 어휘 계약은 그중 두 사례다.

위치 보존은 진단의 줄만 보면 검증되지 않는다. `SourceLocation`의 기본값이 `{1, 1}`이라
한 줄짜리 소스에서 줄 번호를 확인하는 단언은 위치 추적을 지워도 통과한다. 그래서 테스트는
세 줄짜리 소스에서 줄과 열을 함께 못 박는다.

<!-- snippet: src/tests.cpp#TestLexerRejectsUnknownCharacter -->
```cpp
const auto multiline =
    lab::lang::Compiler{}.Compile("let a = 1;\nlet b = 2;\n   let c = @;\n");
// ...
const auto& d = multiline.diagnostics.front();
CHECK(d.message == "알 수 없는 문자: @");
CHECK(d.location.line == 3);
CHECK(d.location.column == 12);
```

최대 길이 일치는 `Lexer::Scan`을 직접 호출해 토큰열 자체를 본다. `a == b`는 네 토큰이어야
하고, `==`의 열은 두 번째 `=`의 열이 아니라 토큰이 시작한 열이어야 한다.

<!-- snippet: src/tests.cpp#TestLexerTokenLocations -->
```cpp
std::vector<lab::lang::Diagnostic> diagnostics;
const auto tokens = lab::lang::Lexer("a == b").Scan(diagnostics);
CHECK(diagnostics.empty());
CHECK(tokens.size() == 4);  // Identifier, EqualEqual, Identifier, End
// ...
CHECK(tokens[1].kind == lab::lang::TokenKind::EqualEqual);
CHECK(tokens[1].text == "==");
CHECK(tokens[1].location.line == 1);
CHECK(tokens[1].location.column == 3);  // `=`의 열이 아니라 토큰 시작 열
```

`start`를 `{1, 1}` 상수로 고정하거나 두 글자 분기를 지우면 위 단언들이 실패한다. 추가 수동
실패 주입은 숫자 중간의 문자, 단독 `!`, 닫히지 않은 괄호를 조합하면 된다.

---

## 결론

렉서의 결과는 단순 문자열 분할이 아니라 위치를 보존한 의미 계약이다. 최대 길이 일치,
유한 숫자 검사, 키워드 재분류, 알 수 없는 문자 거부가 확정되어야 파서가 안정된 토큰열만
받는다. 오류를 조용히 보정하지 않고 실행 산출물 생성을 중단하는 정책이 스크립트가 다른
규칙으로 실행되는 실패를 막는다.
