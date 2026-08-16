## 들어가며

언어 구현의 성능은 실행 루프만 재서 판단할 수 없다. 스크립트를 한 번 컴파일해 여러 번
실행하는 제품과 매번 소스를 파싱하는 제품은 비용 구조가 다르다. 시작 상태는 기능적으로
동일한 두 실행 경로이고, 완료 상태는 컴파일 비용과 캐시된 바이트코드 실행 비용을 분리한
측정 결과다.

측정의 목적은 특정 배율을 보장하는 것이 아니라 비용이 어느 수명 구간에 속하는지 확인하는
것이다.

---

## 목차

1. 비교할 두 경로
2. 통제 변수
3. 정확성 체크섬
4. 결과 해석
5. 전체 검증

---

## 1. 비교할 두 경로

벤치마크는 같은 합계 계산 프로그램을 10,000번 실행한다. 소스 문자열은 측정 파일이 아니라
`src/bench_program.hpp`가 소유한다. 측정 루프가 자기 파일 안에 문자열을 들고 있으면
"두 경로가 같은 프로그램을 돌린다"는 전제를 테스트가 확인할 방법이 없다.

<!-- snippet: src/bench_program.hpp@bench-source mode=exact -->
```cpp
constexpr const char* kSource =
    "let x = 1; let sum = 0;"
    "while (x <= 100) { sum = sum + x; x = x + 1; }";
```

첫 경로는 반복마다 렉싱, 파싱, 실행을 모두 수행한다. 두 번째 경로는 컴파일된 `Program`을
재사용하고 VM 상태만 새로 만든다. 바이트코드 검증은 `Run`이 매 호출 수행하므로 캐시
경로에서도 상각되지 않는다. 두 경로 모두 10,000번 검증하며, 별도 측정에서 검증은 캐시
경로 시간의 약 5%였다.

<!-- snippet: src/bench.cpp#main -->
```cpp
// bench.cpp: 두 경로의 측정 구간 발췌
const double compileEveryTime = Milliseconds([&] {
    for (int i = 0; i < iterations; ++i) {
        auto program = compiler.Compile(kSource);
        lab::lang::VirtualMachine vm;
        const auto result = vm.Run(*program.program);
        checksum += result.instructions;
    }
});

const double reuseBytecode = Milliseconds([&] {
    for (int i = 0; i < iterations; ++i) {
        lab::lang::VirtualMachine vm;
        const auto result = vm.Run(*compiled.program);
        checksum += result.instructions;
    }
});
```

## 2. 통제 변수

두 경로는 소스, 반복 횟수, 컴파일러 최적화, VM 생성 정책, 명령 예산을 같게 유지한다.
Release 빌드가 아니면 컨테이너와 함수 호출 디버그 비용이 결과를 지배할 수 있다.

```powershell
cmake --build build --config Release
.\build\Release\languages_bench.exe
```

단일 측정값은 운영체제 스케줄링과 CPU 주파수 영향을 받는다. 프로세스를 여러 번 실행하고
중앙값을 비교하며, 절대 시간보다 두 경로의 비율과 입력 크기에 따른 추세를 본다.

## 3. 정확성 체크섬

최적화 실험은 작업이 사라지지 않았는지 확인해야 한다. 벤치마크는 각 실행의 명령 수를
누적해 출력한다.

<!-- snippet: src/bench.cpp#main -->
```cpp
checksum += result.instructions;
// ...
std::printf("speedup=%.2fx checksum=%llu\n",
            compileEveryTime / reuseBytecode,
            static_cast<unsigned long long>(checksum));
```

두 경로의 명령 수가 같은 프로그램 실행 횟수에 비례해야 한다. 더 강한 검증은 최종 전역
`sum`이 5050인지 매 실행 확인하는 것이지만, 측정 루프 안의 조회 비용이 포함되므로 별도
정확성 테스트가 그 불변식을 맡는다. 그 테스트는 같은 헤더에서 소스를 읽어 한 번만 실행하고,
결과와 명령 수를 상수로 못 박는다.

<!-- snippet: src/tests.cpp#TestBenchProgramInvariants -->
```cpp
lab::lang::VirtualMachine vm;
const auto run = vm.Run(*compiled.program);
CHECK(run.ok);
CHECK(vm.Global(*compiled.program, "sum").value_or(-1.0) == 5050.0);
CHECK(vm.Global(*compiled.program, "x").value_or(-1.0) == 101.0);
CHECK(run.instructions == 1612);
```

명령 수를 고정하면 출력의 `checksum`이 산술로 닫힌다. 두 경로가 각각 10,000번 돌고 한 번에
1,612개 명령을 쓰므로 누적값은 `2 × 10000 × 1612 = 32240000`이다. 벤치마크가 어느 한 경로를
빠뜨리거나 프로그램이 조기 종료하면 이 값이 먼저 달라진다.

<!-- snippet: src/tests.cpp#TestBenchProgramInvariants -->
```cpp
CHECK(2 * static_cast<uint64_t>(lab::lang::bench::kIterations) * run.instructions ==
      32240000);
```

캐시 경로가 컴파일 경로와 같은 작업을 한다는 전제도 확인 대상이다. 같은 `Program`을 새 VM으로
다시 실행했을 때 명령 수와 전역이 같아야 하고, 다시 컴파일한 바이트코드가 첫 컴파일과 명령
단위로 같아야 두 시간을 나란히 놓을 수 있다.

<!-- snippet: src/tests.cpp#TestBenchProgramInvariants -->
```cpp
lab::lang::VirtualMachine second;
const auto cached = second.Run(*compiled.program);
CHECK(cached.ok);
CHECK(cached.instructions == run.instructions);
CHECK(second.Global(*compiled.program, "sum").value_or(-1.0) == 5050.0);
```

## 4. 결과 해석

캐시 경로가 줄이는 것은 VM 디스패치 비용이 아니라 렉서·파서·벡터 할당·이름 인턴 비용이다.
스크립트를 에셋 로딩 때 한 번 컴파일하고 프레임마다 실행한다면 이 비용은 상각된다.
개발 중 핫 리로드처럼 소스가 자주 바뀌면 컴파일 지연과 진단 품질도 사용자 경험의 일부다.

바이트코드 캐시를 파일로 저장하려면 버전, 엔디언, opcode 집합, 상수 표현, 소스 해시를
헤더에 포함해야 한다. 검증기는 손상된 캐시를 막지만, 오래된 의미론으로 컴파일된 정상
바이트코드를 새 VM에서 실행하는 호환성 문제까지 해결하지는 않는다.

## 5. 전체 검증

정확성과 비용을 분리해 실행한다. 인자 없는 `ctest`는 전체 실행 타깃 하나와 계약별 타깃
여섯 개를 함께 돌린다. 같은 실행 파일을 라벨 인자로 부르면 그중 한 묶음만 돈다.

```powershell
ctest --test-dir build -C Release --output-on-failure
.\build\Release\languages_tests.exe step06
.\build\Release\languages_demo.exe
.\build\Release\languages_bench.exe
```

```text
1/7 Test #1: languages_tests ........................   Passed
2/7 Test #2: languages_step01_lexical_contract ......   Passed
3/7 Test #3: languages_step02_precedence_compiler ...   Passed
4/7 Test #4: languages_step03_bytecode_verifier .....   Passed
5/7 Test #5: languages_step04_vm_runtime ............   Passed
6/7 Test #6: languages_step05_host_binding ..........   Passed
7/7 Test #7: languages_step06_measurement ...........   Passed
100% tests passed out of 7
[step06] bench_program_invariants ok
languages_tests [step06]: all checks passed (1 case(s), 13 check(s))
instructions=96 hp=270.271
iterations=10000
compile+run_ms=163.950
run_cached_bytecode_ms=102.055
speedup=1.61x checksum=32240000
```

`Passed` 일곱 줄, `instructions=96 hp=270.271`, `checksum=32240000`은 환경과 무관한
고정값이다. 벤치마크의 두 밀리초 값은 기계·부하에 따라 달라지므로 위 수치는 한 번의 실행
예시일 뿐이며, 대조할 기준은 `speedup > 1`, 즉 캐시 경로가 더 빠르다는 방향성이다.

타깃 이름이 검사 대상을 그대로 말한다. 어휘 계약, 우선순위와 제어 흐름, 바이트코드 검증,
실행기 한도, 호스트 경계, 측정 불변식이 각각 자기 타깃으로 분리돼 있어, 실패한 타깃 이름만
보고도 무엇이 깨졌는지 좁혀진다. `languages_tests` 한 줄은 그 여섯 묶음을 한 번에 돌린
결과이므로 개수를 세는 데 쓴다. 데모는 실제 밸런스 계산 형태의 프로그램을 실행하고,
벤치마크는 동일 작업에서 컴파일 수명을 바꿨을 때의 비용 차이를 출력한다.

---

## 결론

언어 런타임의 비용은 컴파일과 실행의 수명에 따라 나뉜다. 같은 소스를 반복 실행할 때
바이트코드를 캐시하면 프런트엔드 비용을 상각할 수 있지만, 캐시 형식에는 버전과 검증
계약이 추가된다. 정확성 테스트, 실패 주입, 성능 측정을 분리해야 빠른 경로가 실제로 같은
프로그램을 실행하는지 확인할 수 있다.
