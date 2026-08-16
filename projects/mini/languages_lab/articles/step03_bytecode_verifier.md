## 들어가며

컴파일러가 만들었다는 이유만으로 바이트코드를 신뢰할 수는 없다. 캐시 파일이 손상되거나
컴파일러 결함이 있거나 외부에서 명령열을 주입할 수 있다. 시작 상태는 명령과 피연산자의
배열이고, 완료 상태는 모든 도달 가능한 경로에서 인덱스와 스택 깊이가 안전하다고 확인된
프로그램이다.

검증의 중심은 명령 하나를 보는 것이 아니라 제어 흐름 그래프를 따라 추상 스택 깊이를
전파하는 것이다.

---

## 목차

1. 바이트코드 표현
2. 피연산자 범위
3. 스택 효과
4. 분기 합류
5. 실패 주입

---

## 1. 바이트코드 표현

프로그램은 명령열, 숫자 상수 풀, 이름 테이블로 구성된다.

<!-- snippet: src/language.hpp@bytecode-structs mode=exact -->
```cpp
struct Instruction {
    OpCode op = OpCode::Halt;
    int32_t operand = 0;
    SourceLocation location;
};

struct Program {
    std::vector<Instruction> code;
    std::vector<double> constants;
    std::vector<std::string> names;
};
```

`Constant`의 피연산자는 상수 인덱스이고, `LoadGlobal`, `StoreGlobal`, `CallNative`는 이름
인덱스다. `Jump`와 `JumpIfFalse`는 명령 인덱스다. 같은 정수라도 opcode에 따라 해석과
허용 범위가 다르다.

## 2. 피연산자 범위

검증기는 명령을 실행하기 전에 모든 인덱스를 대응 테이블 크기와 비교한다.

<!-- snippet: src/language.hpp#Verify -->
```cpp
// language.hpp: Verify 안의 requireOperand 람다 중 범위 검사 발췌
if (instruction.operand < 0 ||
    static_cast<size_t>(instruction.operand) >= limit) {
    error = std::string(label) + " 인덱스가 범위를 벗어났다";
    return false;
}
```

음수를 `size_t`로 먼저 변환하면 매우 큰 양수가 되므로 부호 검사가 앞에 와야 한다. 점프
대상도 코드 끝 다음이 아니라 실제 명령 하나를 가리켜야 한다.

## 3. 스택 효과

각 opcode는 필요한 입력 깊이와 실행 뒤 변화량을 가진다.

| 명령 | 최소 깊이 | 변화 |
|---|---:|---:|
| `Constant`, `LoadGlobal` | 0 | +1 |
| 이항 산술·비교 | 2 | -1 |
| `Negate`, `StoreGlobal`, `CallNative` | 1 | 0 |
| `Pop` | 1 | -1 |
| `JumpIfFalse` | 1 | 0 |
| `Jump`, `Halt` | 0 | 0 |

현재 깊이가 최소보다 작으면 그 경로는 스택 언더플로를 일으킨다. 이 검사는 런타임의
`vector::back()`이 빈 벡터를 읽기 전에 구조적 오류를 차단한다.

## 4. 분기 합류

검증기는 프로그램 카운터별 예상 깊이를 `unknown`으로 초기화하고 진입점의 깊이를 0으로
둔다. 작업 큐로 도달 가능한 후속 명령에 깊이를 전파한다.

<!-- snippet: src/language.hpp#Verify -->
```cpp
// language.hpp: Verify 안의 enqueue 람다 전문
auto enqueue = [&](size_t target) -> bool {
    if (depth[target] == unknown) {
        depth[target] = nextDepth;
        work.push(target);
        return true;
    }
    if (depth[target] != nextDepth) {
        error = "분기 합류점의 스택 깊이가 일치하지 않는다";
        return false;
    }
    return true;
};
```

조건 분기는 대상과 낙하 경로 둘 다 전파한다. 무조건 점프는 대상만 전파하고 `Halt`는
후속 간선을 만들지 않는다. 서로 다른 경로가 같은 명령에 도달했는데 깊이가 다르면 이후
명령의 피연산자 의미가 경로에 따라 달라지므로 프로그램 전체를 거부한다.

이 추상 해석은 값의 종류까지 추적하지 않는다. 값이 `double` 하나뿐이므로 깊이만으로
충분하다. 정수, 문자열, 객체가 추가되면 슬롯별 타입 또는 동적 타입 검사가 필요하다.

## 5. 실패 주입

테스트는 컴파일러를 거치지 않고 잘못된 프로그램을 직접 만든다.

<!-- snippet: src/tests.cpp#TestVerifierRejectsMalformedProgram -->
```cpp
lab::lang::Program malformed;
malformed.code.push_back({lab::lang::OpCode::Pop, 0, {1, 1}});
malformed.code.push_back({lab::lang::OpCode::Halt, 0, {1, 2}});
std::string error;
CHECK(!lab::lang::Verify(malformed, error));
```

분기 없는 선형 프로그램만 넣으면 합류 깊이 검사는 한 번도 실행되지 않는다. 그래서 손으로
만든 분기 프로그램 네 개를 넣고, 거부 여부뿐 아니라 사유 문자열까지 못 박는다. 먼저 두 경로의
합류 깊이가 같은 정상 프로그램이 통과하는지 확인하는 대조군을 둔다. 대조군이 없으면 불일치
케이스가 엉뚱한 이유로 거부돼도 알아채지 못한다.

<!-- snippet: src/tests.cpp#TestVerifierBranchContracts -->
```cpp
// 참 경로는 Pop으로 깊이를 줄이고 거짓 경로는 그대로 통과해 인덱스 4에서 0과 1이 만난다.
Program mismatched;
mismatched.constants.push_back(1.0);
mismatched.code.push_back({OpCode::Constant, 0, {1, 1}});
mismatched.code.push_back({OpCode::JumpIfFalse, 3, {1, 2}});
mismatched.code.push_back({OpCode::Jump, 4, {1, 3}});
mismatched.code.push_back({OpCode::Pop, 0, {1, 4}});
mismatched.code.push_back({OpCode::Halt, 0, {1, 5}});
// ...
CHECK(!lab::lang::Verify(mismatched, error));
CHECK(error == "분기 합류점의 스택 깊이가 일치하지 않는다");
```

나머지 두 개는 음수 점프 대상(`분기 인덱스가 범위를 벗어났다`)과 `Halt`로 끝나지 않는
프로그램(`프로그램은 Halt로 끝나야 한다`)이다. 사유까지 비교하기 때문에 합류 검사를 항상
성공으로 바꾸거나 종결 검사를 지우면 곧바로 실패한다. 모두 VM 실행 전에 걸러진다.

두 사례는 등록표에서 `step03` 라벨을 달고 있다. VM을 한 번도 만들지 않고 `Verify`만
직접 부르므로, 실행기가 통째로 망가져 있어도 이 타깃의 결과는 바뀌지 않는다.

```powershell
ctest --test-dir build -C Release -R languages_step03 --output-on-failure
.\build\Release\languages_tests.exe step03
```

```text
1/1 Test #4: languages_step03_bytecode_verifier ...   Passed
100% tests passed out of 1
[step03] verifier_rejects_malformed_program ok
[step03] verifier_branch_contracts ok
languages_tests [step03]: all checks passed (2 case(s), 11 check(s))
```

11개의 단언 중 절반 이상이 `verifier_branch_contracts` 안의 사유 문자열 비교다. 거부
여부만 세는 검증이었다면 같은 사례가 4~5개 단언으로 끝났을 것이고, 합류 검사를 항상
성공으로 바꿔도 다른 이유로 거부되어 통과했을 것이다.

---

## 결론

바이트코드 검증기는 컴파일러와 실행기 사이의 신뢰 경계다. 피연산자 범위만 검사해서는
분기 경로의 스택 불균형을 찾지 못한다. 제어 흐름 그래프에 추상 스택 깊이를 전파하면
언더플로와 불일치 합류를 실행 전에 거부할 수 있다. 값 종류가 늘어날수록 같은 틀에 타입과
수명 정보를 추가할 수 있다.
