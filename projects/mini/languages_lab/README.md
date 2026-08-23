# languages_lab

게임 스크립트 언어의 프런트엔드와 실행 런타임을 직접 만드는 C++20 실험실이다. 최종
코드는 하나뿐이며, `articles/`는 렉서에서 VM까지 설계가 확장된 과정과 검증 근거를
기록한다.

## 확인하는 주장

1. 문법은 표기법이 아니라 허용할 프로그램과 거부할 프로그램을 정하는 계약이다.
2. 연산자 우선순위는 파서 구조에 명시되어야 하며, 괄호와 결합 법칙으로 검증해야 한다.
3. 바이트코드는 실행 전에 피연산자·분기 대상·스택 깊이를 검증할 수 있다.
4. 신뢰하지 않는 스크립트에는 명령 수와 스택 크기의 자원 한도가 필요하다.
5. 호스트 바인딩은 임의의 C++ 호출이 아니라 등록된 이름과 값 변환만 노출해야 한다.
6. 컴파일 결과를 재사용하면 매 실행마다 렉싱·파싱하는 비용을 제거할 수 있다.
7. 위 계약들은 손으로 고른 입력뿐 아니라 문법이 만들어 내는 임의의 프로그램과 그것을
   토큰·명령 단위로 깨뜨린 입력에서도 유지되어야 한다.

## 최종 구현

- 줄·열 정보를 보존하는 렉서와 오류 진단
- 우선순위 상승 방식의 표현식 파서
- 선언·할당·`if/else`·`while`·단항·산술·비교 문법
- 상수 풀과 이름 테이블을 가진 스택 바이트코드
- 제어 흐름 그래프를 따라 스택 깊이를 검사하는 검증기
- 초기화되지 않은 변수, 0 나눗셈, 비유한 값의 런타임 오류
- 등록제 단항 호스트 함수
- 파서 중첩 깊이 한도, 명령 실행 예산과 VM 스택 상한
- 컴파일 포함 실행과 캐시된 바이트코드 실행의 비용 비교
- 문법 유도 생성기와 토큰·명령 단위 변형기로 도는 property 배터리, 반례 축소와 시드 재현

## 빌드

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\languages_demo.exe
.\build\Release\languages_bench.exe
```

Linux와 macOS의 단일 구성 생성기에서는 실행 파일이 `build/`에 생성된다.

## 계약별 테스트 타깃

`ctest`는 전체 실행 타깃 `languages_tests` 하나와 계약별 타깃 여섯 개를 등록한다.
계약별 타깃은 같은 실행 파일에 라벨을 인자로 넘긴 것이며, 라벨은 `src/tests.cpp`의
등록표 첫 칸이다.

<!-- snippet: src/tests.cpp@test-registry -->
```cpp
struct TestCase {
    const char* step;
    const char* name;
    void (*run)();
};

constexpr TestCase kTestCases[] = {
    {"step01", "lexer_token_locations", TestLexerTokenLocations},
    {"step01", "lexer_rejects_unknown_character", TestLexerRejectsUnknownCharacter},
    // ...
    {"step06", "bench_program_invariants", TestBenchProgramInvariants},
};
```

한 묶음만 돌리려면 ctest 이름 필터나 실행 파일 인자를 쓴다. 사례 이름 하나만 넘겨도 된다.

```powershell
ctest --test-dir build -C Release -R languages_step03 --output-on-failure
.\build\Release\languages_tests.exe step03
.\build\Release\languages_tests.exe verifier_branch_contracts
```

```text
[step03] verifier_rejects_malformed_program ok
[step03] verifier_branch_contracts ok
  [fuzz] bytecode: 표본 20000 · 거부 17597 · 통과 2403 · opcode변형 8030 · 미지opcode거부 5908
[step03] fuzz_bytecode_mutation_verifier ok
languages_tests [step03]: all checks passed (3 case(s), 21 check(s))
```

라벨이 등록표에 없으면 실행 파일은 통과가 아니라 실패로 끝난다. 오타 난 필터가 "통과"로
보이는 것이 가장 나쁜 결과이기 때문이다.

## fuzz 배터리

등록표의 사례 넷은 생성된 입력에서 계약을 다시 검사한다. 난수는 전부 소스에 박힌 시드에서
나오고 벽시계를 읽지 않으므로, 통과도 실패도 같은 명령으로 재현된다. 실행 파일은 세 개의
선택 인자를 더 받는다.

```powershell
.\build\Release\languages_tests.exe fuzz_token_mutation_diagnostics
.\build\Release\languages_tests.exe fuzz_token_mutation_diagnostics --samples=200000
.\build\Release\languages_tests.exe fuzz_token_mutation_diagnostics --seed=0xC0FFEE --samples=50000
.\build\Release\languages_tests.exe fuzz_token_mutation_diagnostics --seed=0x5EED10CC1A2B3C4D --only=0
```

```text
  [fuzz] mutation: 표본 20000 · 거부 19184 · 여전히컴파일 816 · 2줄이후진단 15876 · 깊은중첩거부 772 · 미지문자거부 4165 · 진단종류 30
[step04] fuzz_token_mutation_diagnostics ok
languages_tests [fuzz_token_mutation_diagnostics]: all checks passed (1 case(s), 7 check(s))
```

기본값은 사례당 20,000표본이고, 사례 열아홉 개를 전부 도는 실행이 이 기계에서 3~4초다.
`ctest`는 전체 타깃과 계약별 타깃을 따로 돌아 배터리를 두 번 실행하므로 6초 안팎이다. 실패하면
반례를 토큰 단위로 축소해 시드·표본 번호와 함께 찍고, 그 표본 하나만 다시 도는 명령을
같이 낸다. `--only`로 도는 재현 실행에서는 커버리지 단언을 걸지 않는다 — 표본 하나짜리
실행이 "커버리지 부족"으로 실패하면 반례가 가려진다.

## 디렉터리

```text
languages_lab/
  CMakeLists.txt
  README.md
  articles/
  src/
    language.hpp       렉서, 컴파일러, 바이트코드 검증기, VM
    main.cpp           게임 밸런스 식 실행 예제
    tests.cpp          문법·실행·실패 주입 테스트, fuzz property, 계약별 등록표
    fuzz.hpp           문법 유도 생성기, 토큰·명령 변형기, 반례 축소기, 실패 어휘
    bench.cpp          컴파일 비용 상각 실험
    bench_program.hpp  측정과 정확성 테스트가 공유하는 벤치마크 소스
```

## 범위

값은 `double` 하나로 제한하고 전역 변수와 단항 호스트 함수만 지원한다. 문자열, 객체,
함수, 클로저, GC, 디버그 심볼 파일, JIT는 이 실험의 범위가 아니다. 이 제한은 기능 누락을
숨기기 위한 것이 아니라 프런트엔드, IR 검증, 실행기, 샌드박스 경계를 한 번에 관찰하기
위한 것이다. 메모리 관리와 최적화 계층은 이 계약 위에서 독립적으로 확장할 수 있다.

이 축의 `part03`(가비지 컬렉션), `part05`(JIT 컴파일), `part07`(리플렉션·메타데이터)은
대응하는 실험실 요소 없이 이론 문서로만 다룬다.
