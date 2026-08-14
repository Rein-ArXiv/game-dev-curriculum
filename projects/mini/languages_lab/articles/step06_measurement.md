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

벤치마크는 같은 합계 계산 프로그램을 10,000번 실행한다.

```cpp
constexpr const char* source =
    "let x = 1; let sum = 0;"
    "while (x <= 100) { sum = sum + x; x = x + 1; }";
```

첫 경로는 반복마다 렉싱, 파싱, 실행을 모두 수행한다. 두 번째 경로는 컴파일된 `Program`을
재사용하고 VM 상태만 새로 만든다. 바이트코드 검증은 `Run`이 매 호출 수행하므로 캐시
경로에서도 상각되지 않는다. 두 경로 모두 10,000번 검증하며, 별도 측정에서 검증은 캐시
경로 시간의 약 5%였다.

```cpp
for (int i = 0; i < iterations; ++i) {
    auto program = compiler.Compile(source);
    VirtualMachine vm;
    vm.Run(*program.program);
}

auto program = compiler.Compile(source);
for (int i = 0; i < iterations; ++i) {
    VirtualMachine vm;
    vm.Run(*program.program);
}
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

```cpp
checksum += result.instructions;
std::printf("speedup=%.2fx checksum=%llu\n",
            compileEveryTime / reuseBytecode, checksum);
```

두 경로의 명령 수가 같은 프로그램 실행 횟수에 비례해야 한다. 더 강한 검증은 최종 전역
`sum`이 5050인지 매 실행 확인하는 것이지만, 측정 루프 안의 조회 비용이 포함되므로 별도
정확성 테스트가 그 불변식을 맡는다.

## 4. 결과 해석

캐시 경로가 줄이는 것은 VM 디스패치 비용이 아니라 렉서·파서·벡터 할당·이름 인턴 비용이다.
스크립트를 에셋 로딩 때 한 번 컴파일하고 프레임마다 실행한다면 이 비용은 상각된다.
개발 중 핫 리로드처럼 소스가 자주 바뀌면 컴파일 지연과 진단 품질도 사용자 경험의 일부다.

바이트코드 캐시를 파일로 저장하려면 버전, 엔디언, opcode 집합, 상수 표현, 소스 해시를
헤더에 포함해야 한다. 검증기는 손상된 캐시를 막지만, 오래된 의미론으로 컴파일된 정상
바이트코드를 새 VM에서 실행하는 호환성 문제까지 해결하지는 않는다.

## 5. 전체 검증

정확성과 비용을 분리해 실행한다.

```powershell
ctest --test-dir build -C Release --output-on-failure
.\build\Release\languages_demo.exe
.\build\Release\languages_bench.exe
```

```text
1/1 Test #1: languages_tests ..................   Passed
100% tests passed out of 1
instructions=96 hp=270.271
iterations=10000
compile+run_ms=163.950
run_cached_bytecode_ms=102.055
speedup=1.61x checksum=32240000
```

`Passed`, `instructions=96 hp=270.271`, `checksum=32240000`은 환경과 무관한 고정값이다.
벤치마크의 두 밀리초 값은 기계·부하에 따라 달라지므로 위 수치는 한 번의 실행 예시일 뿐이며,
대조할 기준은 `speedup > 1`, 즉 캐시 경로가 더 빠르다는 방향성이다.

테스트는 우선순위, 제어 흐름, 호스트 호출, 구문 오류, 초기화되지 않은 변수, 0 나눗셈,
실행 예산, 파서 중첩 깊이 한도, 잘못된 바이트코드를 검사한다. 데모는 실제 밸런스 계산 형태의 프로그램을
실행한다. 벤치마크는 동일 작업에서 컴파일 수명을 바꿨을 때의 비용 차이를 출력한다.

---

## 결론

언어 런타임의 비용은 컴파일과 실행의 수명에 따라 나뉜다. 같은 소스를 반복 실행할 때
바이트코드를 캐시하면 프런트엔드 비용을 상각할 수 있지만, 캐시 형식에는 버전과 검증
계약이 추가된다. 정확성 테스트, 실패 주입, 성능 측정을 분리해야 빠른 경로가 실제로 같은
프로그램을 실행하는지 확인할 수 있다.
