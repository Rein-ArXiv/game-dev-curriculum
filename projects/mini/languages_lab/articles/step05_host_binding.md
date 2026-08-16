## 들어가며

게임 스크립트는 엔진과 통신해야 쓸모가 있지만 C++ 객체를 그대로 노출하면 수명, 권한,
스레드 안전성이 스크립트 문법 밖으로 새어 나온다. 시작 상태는 순수 계산 VM이고, 완료
상태는 등록된 이름과 하나의 값 변환을 통해서만 호스트 기능을 호출하는 경계다.

이 실험은 기능 수보다 권한의 폐쇄성을 확인한다. 소스에 이름이 등장했다는 이유만으로
호스트 함수가 호출되어서는 안 된다.

---

## 목차

1. 등록제 API
2. 호출 의미론
3. 수명과 권한
4. 결정론
5. 검증

---

## 1. 등록제 API

호스트는 허용할 함수만 이름과 함께 등록한다.

<!-- snippet: src/language.hpp#VirtualMachine -->
```cpp
// language.hpp: VirtualMachine 의 공개 바인딩 면 전문
using NativeFunction = std::function<double(double)>;

void Bind(std::string name, NativeFunction function) {
    natives_[std::move(name)] = std::move(function);
}
```

현재 ABI는 `double -> double` 하나다. 제한적이지만 변환 규칙이 완전히 명시된다. 임의
포인터, 참조, 예외 객체가 언어 경계를 넘지 않는다.

데모는 밸런스 계산 스크립트에 상한·하한 한 개만 노출한다.

<!-- snippet: src/main.cpp#main -->
```cpp
vm.Bind("clamp", [](double value) { return std::clamp(value, 1.0, 1000.0); });
```

## 2. 호출 의미론

컴파일러는 `clamp(hp)`를 `CallNative`와 이름 테이블 인덱스로 바꾼다. 함수의 존재 여부는
VM 인스턴스의 등록표에 달려 있다. 같은 바이트코드를 도구 환경과 Shipping 환경에서 서로
다른 권한 집합으로 실행할 수 있다.

<!-- snippet: src/language.hpp#Run -->
```cpp
// language.hpp: VirtualMachine::Run 의 CallNative 처리 발췌
const std::string& name =
    program.names[static_cast<size_t>(instruction.operand)];
const auto found = natives_.find(name);
if (found == natives_.end())
    return Failure(instruction.location,
                   "등록되지 않은 호스트 함수: " + name, executed);
const double argument = pop();
const double value = found->second(argument);
if (auto failure = push(value, instruction.location)) return *failure;
```

호스트 반환값도 유한성 및 스택 한도 검사를 통과한다. C++ 쪽 함수라고 신뢰해 검사를
우회하지 않는다.

## 3. 수명과 권한

람다가 외부 객체의 참조를 캡처하면 VM보다 객체가 오래 살아야 한다. 실전 바인딩은 원시
포인터 대신 세대 핸들이나 호출 시점 조회를 사용해야 한다. 파일, 네트워크, 월드 변경처럼
부작용이 있는 기능은 읽기 전용 질의와 명령 제출을 분리하는 편이 안전하다.

```cpp
vm.Bind("enemy_count", [worldHandle](double) {
    const World* world = worlds.Get(worldHandle);
    return world ? static_cast<double>(world->EnemyCount()) : 0.0;
});
```

함수 목록 자체가 capability 집합이다. 로딩용 VM에는 파일 읽기 함수를 주지 않고, 밸런스
계산 VM에는 월드 변경 함수를 주지 않는 식으로 최소 권한을 구성한다.

## 4. 결정론

호스트 함수가 현재 시간, 난수 장치, 파일 시스템 순서에 의존하면 같은 바이트코드도 다른
결과를 낸다. 결정적 시뮬레이션에서 허용할 함수는 입력이 모두 인자로 드러나고 부작용 순서가
고정되어야 한다.

현재 VM은 `double`을 사용하므로 CPU와 컴파일 옵션을 넘는 비트 결정론을 약속하지 않는다.
권위 서버에서 한 번 계산한 결과를 복제하거나 정수 기반 API만 노출하는 것이 더 강한
계약이다.

## 5. 검증

등록된 함수는 결과를 만들고, 등록되지 않은 같은 이름은 런타임 오류가 되어야 한다.

<!-- snippet: src/tests.cpp#TestHostBoundary -->
```cpp
auto compiled = lab::lang::Compiler{}.Compile("let result = twice(21);");
// ...
lab::lang::VirtualMachine vm;
vm.Bind("twice", [](double value) { return value * 2.0; });
const auto run = vm.Run(*compiled.program);
CHECK(run.ok);
CHECK(vm.Global(*compiled.program, "result").value_or(0.0) == 42.0);
```

성공 경로만 보면 미등록 이름 거부를 항등 함수로 바꿔도 통과한다. 실패 경로는 메시지와 소스
위치까지 함께 못 박는다. 프로세스 크래시나 기본값 대체가 아니라 소스 위치가 붙은 오류여야
한다.

<!-- snippet: src/tests.cpp#TestHostBoundaryRejectsUnregisteredName -->
```cpp
auto compiled = lab::lang::Compiler{}.Compile("let x = ghost(1);");
// ...
lab::lang::VirtualMachine vm;
const auto run = vm.Run(*compiled.program);
CHECK(!run.ok);
// ...
CHECK(run.diagnostic->message == "등록되지 않은 호스트 함수: ghost");
CHECK(run.diagnostic->location.line == 1);
CHECK(run.diagnostic->location.column == 9);  // `ghost`의 시작 열
```

'같은 바이트코드, 다른 권한 집합'이라는 주장도 한 테스트 안에서 대조한다. 같은
`compiled.program`을 `ghost`를 바인딩한 VM과 아무것도 바인딩하지 않은 VM에 각각 넘기면
전자만 성공한다. 권한이 프로그램이 아니라 VM 인스턴스에 있다는 뜻이다.

두 사례는 등록표에서 `step05` 라벨을 달고 있다. 성공 경로와 거부 경로를 한 타깃 안에
두는 것이 요점이다 — 한쪽만 있으면 경계가 아니라 편의 함수다.

```powershell
ctest --test-dir build -C Release -R languages_step05 --output-on-failure
.\build\Release\languages_tests.exe step05
```

```text
1/1 Test #6: languages_step05_host_binding ....   Passed
100% tests passed out of 1
[step05] host_boundary ok
[step05] host_boundary_rejects_unregistered_name ok
languages_tests [step05]: all checks passed (2 case(s), 12 check(s))
```

12개 단언 중 9개가 `host_boundary_rejects_unregistered_name`에 있다. 거부 경로가 성공
경로보다 확인할 것이 많다는 뜻이다 — 실패 여부, 진단 존재, 메시지, 줄, 열, 그리고 같은
바이트코드를 권한이 다른 VM 두 개에 넘긴 대조까지다.

---

## 결론

호스트 바인딩은 편의 API가 아니라 권한과 수명의 경계다. 등록된 이름만 호출하고 값 변환을
좁게 유지하면 VM이 C++ 객체 구조와 직접 결합하지 않는다. 반환값도 동일한 수치 검사를
거치고, 결정적 실행이 필요한 환경에서는 시간·난수·I/O 같은 비결정 입력을 capability
목록에서 제외해야 한다.
