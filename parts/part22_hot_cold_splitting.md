## 들어가며

정상 경로가 대부분이고 에러 처리는 드문 함수를 생각해보자. 그 에러 처리 코드가 정상 경로의 성능까지 떨어뜨리고 있다면 어떻게 할 것인가. 프로그램의 실행 시간 대부분을 차지하는 hot path와 거의 실행되지 않는 cold path를 분리하면, 실행조차 되지 않는 코드가 만드는 배치 비용을 줄일 수 있다.

이 비용의 원인은 현대 CPU의 구조에 있다. 명령어 캐시(I-cache), 분기 예측(branch prediction), 코드 레이아웃이 모두 연결되어 있다. 이 글은 hot/cold 코드 분리의 원리부터 PGO와 BOLT 같은 실전 도구까지 다룬다.


---

## 목차

1. [Hot Path vs Cold Path: 파레토 법칙](#1-hot-path-vs-cold-path-파레토-법칙)
2. [Instruction Cache의 본질](#2-instruction-cache의-본질)
3. [분기 예측과 코드 레이아웃](#3-분기-예측과-코드-레이아웃)
4. [`__attribute__((hot))`과 `__attribute__((cold))`](#4-__attribute__hot-과-__attribute__cold)
5. [함수 아웃라이닝](#5-함수-아웃라이닝-function-outlining)
6. [링크 타임 최적화와 코드 레이아웃](#6-링크-타임-최적화와-코드-레이아웃)
7. [실전 예제: JSON 파서 최적화](#7-실전-예제-json-파서-최적화)
8. [실전 팁과 안티패턴](#8-실전-팁과-안티패턴)
9. [측정과 프로파일링](#9-측정과-프로파일링)
10. [고급 주제: Code Layout 알고리즘](#10-고급-주제-code-layout-알고리즘)


---

## 1. Hot Path vs Cold Path: 파레토 법칙

### 집중도는 법칙이 아니라 측정값이다

파레토식 집중은 hot path를 설명하는 직관이지 프로그램에 적용되는 90/10 정리가 아니다. 실행 시간의 집중도는 입력 분포, phase와 build마다 달라진다. 다음은 프로파일 결과를 읽는 *형식 예*다.

```text
함수별 exclusive(self) CPU time:
Top 10 함수: X%
Top 100 함수: Y%
나머지 함수: 100-Y%
```

inclusive 시간은 호출자와 피호출자에 중복 집계되므로, 이처럼 합계를 100%로 두는 비중 분류는 exclusive sample이나 중복을 제거한 call-tree 기준으로 계산한다.

이는 **hot/cold path**의 개념으로 이어진다:

- **Hot path**: 대상 workload에서 실행 빈도나 CPU 비용이 높은 경로
- **Cold path**: 같은 workload에서 드물거나 전체 비용 기여가 작은 경로

전형적인 예:

```cpp
int read_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) {                         // Cold path (에러)
        log_error("Failed to open");
        show_error_dialog("Cannot open file");
        cleanup_resources();
        return -1;
    }

    // Hot path (정상 동작)
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), f)) {
        process_line(buffer);
    }

    fclose(f);
    return 0;
}
```

파일 열기 성공률은 환경에 따라 달라진다. 에러 경로가 실제 profile에서 드물더라도 hot basic block과 같은 cache line·함수에 배치되면 instruction working set과 inlining 결정에 영향을 줄 수 있다.

## 2. Instruction Cache의 본질

### 메모리 계층에서의 코드

데이터 캐시(D-cache)와 마찬가지로, CPU는 명령어 캐시(I-cache)를 가지고 있다.

```text
예시 CPU:
L1 I-cache: C_I bytes, A_I-way
L1 D-cache: C_D bytes, A_D-way
L2/L3: 모델별 용량·공유 범위
```

현대 CPU는 흔히 L1 instruction/data cache를 분리하지만 정확한 용량과 associativity는 모델별로 다르다. 함수의 정적 코드 크기가 L1 I-cache 용량을 넘었다고 모든 fetch가 miss하는 것도 아니고, 작다고 hit가 보장되는 것도 아니다. 한 시간 구간의 실행 working set, set 충돌, 다른 코드와의 경쟁, uop cache와 prefetch가 실제 miss를 결정한다.

### 캐시 라인과 코드 배치

```text
캐시 라인 크기: 64 바이트 (x86-64)
명령어 크기: 1-15 바이트 (가변 길이)

하나의 캐시 라인 예:
[64 bytes]
│ mov rax, [rdi]     ; 3 bytes
│ add rax, [rsi]     ; 3 bytes
│ test rax, rax      ; 3 bytes
│ jz .error          ; 2 bytes
│ mov [rdx], rax     ; 3 bytes
│ ...
└─ 들어가는 명령어 수는 실제 인코딩 길이에 따라 달라짐
```

캐시 라인을 읽으면 그 안의 모든 명령어가 로드된다. 만약 그 안에 **절대 실행되지 않는 cold path 코드**가 있다면?

```cpp
// 나쁜 예: Hot과 cold가 섞여 있음
int process(int* data, int n) {
    if (n < 0) {                              // Cold: 거의 발생 안 함
        log_error("Negative size");
        dump_stack_trace();
        send_telemetry("process_error", n);
        abort();                              // 20-30줄의 에러 처리
    }

    // Hot path
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += data[i];
    }
    return sum;
}
```

컴파일러가 생성한 어셈블리:

```asm
process:
    test esi, esi           ; n < 0?
    js .error               ; 음수면 점프
.hot_path:
    xor eax, eax            ; sum = 0
    test esi, esi           ; n == 0?
    je .done                ; 0이면 루프를 건너뜀
    xor ecx, ecx            ; i = 0
.loop:
    add eax, [rdi+rcx*4]
    inc ecx
    cmp ecx, esi
    jl .loop
.done:
    ret

.error:                     ; Cold path 코드
    ; 100+ 바이트의 에러 처리 코드
    push rbp
    mov rbp, rsp
    ; log_error 호출
    ; dump_stack_trace 호출
    ; send_telemetry 호출
    call abort
```

`.error`가 hot 코드 사이에 끼어 있거나 같은 cache line·page를 차지하면 순차 fetch와 code layout의 locality를 악화시킬 수 있다. 단순히 함수 주소상 뒤에 있다는 이유만으로 오류 처리 전체가 I-cache에 자동 로드되는 것은 아니다. 문제는 자주 함께 실행되는 basic block이 cold block 때문에 서로 멀어지고 더 많은 line·page·BTB entry를 점유하는 경우다.

### I-cache 압력 (I-cache Pressure)

L1 I-cache와 uop cache, iTLB 용량은 제한적이지만 함수 소스 크기를 더해 고정 성능 저하율을 계산할 수는 없다.

```text
측정할 항목:
- hot 시간 창에서 실제 실행된 instruction footprint
- `L1-icache-load-misses`, iTLB miss와 front-end stall
- 함수·basic block별 실행 빈도와 section 배치
- 정렬 padding, set 충돌과 shared-library 경계
```

Intel의 "Intel 64 and IA-32 Architectures Optimization Reference Manual"[^1]은 hot 코드의 정렬과 배치를 다루며, 분기 대상과 루프 진입점을 캐시 라인 경계에 맞추라고 권고한다.

## 3. 분기 예측과 코드 레이아웃

### 분기 예측기의 동작

현대 CPU는 조건 분기의 방향과 target을 예측해 fetch를 계속한다. 예측이 틀리면 잘못 가져온 작업을 버리고 올바른 경로에서 다시 시작한다. 복구 비용은 CPU, branch가 해소되는 시각과 target의 I-cache/uop-cache 상태에 따라 달라진다.

예측 전략:
1. **정적·초기 정책**: history가 없을 때의 기본 정책은 ISA와 microarchitecture마다 다름
2. **동적 예측**: 분기 히스토리 기반 (2-bit counter, global history, etc.)

```cpp
for (int i = 0; i < 1000; i++) {    // 뒤로 점프 → taken 예측
    if (rare_condition) {            // 앞으로 점프 → not taken 예측
        handle_rare_case();
    }
}
```

대부분의 경우 `rare_condition`은 거짓이므로, 정적 예측이 맞다. 하지만 **코드 배치**가 중요하다:

```asm
; 좋은 배치: Cold path가 뒤에 (fall-through가 hot path)
loop:
    ; 루프 본문
    test condition
    jnz .cold              ; rare_condition이 참이면 점프 (rare)
    ; Hot path 계속
.continue:
    inc i
    cmp i, 1000
    jl loop
    ret

.cold:                     ; 멀리 떨어진 곳에 배치
    call handle_rare_case
    jmp .continue          ; 같은 원소를 다시 처리하지 않고 증분으로 복귀
```

```asm
; 나쁜 배치: Hot path가 점프해야 함
loop:
    ; 루프 본문
    test condition
    jz .hot                ; 대부분의 경우 점프 (overhead)
.cold:
    call handle_rare_case
.hot:
    inc i
    cmp i, 1000
    jl loop
    ret
```

hot path를 fall-through로 두면 드문 경로로 갈 때만 taken branch가 필요하고, 연속된 hot block의 fetch·layout이 단순해지는 경우가 많다. 그러나 동적 predictor는 taken branch도 잘 예측할 수 있으므로 이것은 정확성 규칙이나 고정 cycle 절감 공식이 아니다. PGO가 관측한 edge frequency와 생성된 block layout으로 검증한다.

### Branch Misprediction의 비용

`branch-misses × 고정 penalty`는 거친 상한 모델일 뿐이다. 독립 작업과 memory stall이 일부 시간을 가릴 수 있고, 한 miss가 front-end와 reorder window에 미치는 영향도 지점마다 다르다. 예측된 분기 역시 fetch bandwidth, taken-target lookup과 코드 배치 비용이 완전히 0이라고 할 수 없다.

실제 비용은 동일 입력에서 branch 구조를 바꾼 두 바이너리의 cycles, instructions, branch miss와 front-end stall을 비교한다. 전체 miss rate보다 hot branch 주소별 miss 수와 그 경로가 차지하는 CPU 시간이 의사결정에 더 유용하다.

## 4. `__attribute__((hot))` 과 `__attribute__((cold))`

### GCC/Clang의 함수 속성

GCC와 Clang은 함수에 "온도"를 명시할 수 있는 속성을 제공한다[^3]:

```cpp
__attribute__((hot))
void frequently_called() {
    // 이 함수는 자주 호출됨
    // 컴파일러가 적극적으로 최적화
}

__attribute__((cold))
void error_handler() {
    // 이 함수는 거의 호출되지 않음
    // 최적화보다 코드 크기 우선
}
```

### `__attribute__((hot))`의 효과

1. **속도 우선 비용 모델**: 크기 우선 빌드에서도 인라이닝·루프 변환 후보 판단에 영향을 줄 수 있음
2. **코드 배치**: hot section에 배치해 다른 hot 코드와의 locality를 높일 수 있음
3. **호출 edge의 빈도 정보**: 컴파일러가 이 함수로 들어오는 경로를 hot한 것으로 모델링할 수 있음

예:

```cpp
__attribute__((hot))
int inner_loop_kernel(int* data, int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += data[i] * data[i];
    }
    return sum;
}
```

정확히 말하면 `hot` 속성 자체가 벡터화를 보장하거나 모든 최적화 레벨을 속도 우선으로 바꾸는 것은 아니다. GCC는 더 적극적인 최적화와 hot subsection 배치에 이 정보를 사용할 수 있지만, 적용되는 변환은 빌드 옵션과 타깃에 달려 있다. `-fprofile-use`로 실제 빈도 정보가 있으면 GCC는 hot/cold를 자동 판정하고 수동 속성을 무시한다. 위 정수 합·곱 예제는 결과가 `int` 범위 안에 드는 입력을 전제로 한다.

### `__attribute__((cold))`의 효과

1. **크기 우선 비용 모델**: cold 함수의 code size를 줄이는 판단에 영향을 줄 수 있음
2. **코드 배치**: cold section에 분리해 hot working set에서 멀리 둘 수 있음
3. **호출 edge의 빈도 정보**: 컴파일러가 이 함수로 들어오는 경로를 cold한 것으로 모델링할 수 있음

예:

```cpp
__attribute__((cold, noreturn))
void fatal_error(const char* msg) {
    fprintf(stderr, "Fatal: %s\n", msg);
    abort();
}

int process(int* data, int n) {
    if (data == nullptr) [[unlikely]] {
        fatal_error("Null pointer");
    }

    // Hot path
    return compute(data, n);
}
```

어셈블리:

```asm
process:
    test rdi, rdi
    je .error           ; Cold path는 점프
    ; Hot path 코드 (fall-through)
    call compute
    ret

.error:                 ; 섹션 끝으로 이동
    lea rdi, [msg]
    call fatal_error
```

### C++20 `[[likely]]` / `[[unlikely]]`

C++20부터 표준 속성으로 제공:

```cpp
int process(int value) {
    if (value > 0) [[likely]] {
        return value * 2;
    } else [[unlikely]] {
        log_error("Unexpected negative value");
        return -1;
    }
}
```

GCC 9+, Clang 12+에서 이를 코드 배치와 분기 예측에 반영한다.

Compiler Explorer 예제 (https://godbolt.org/):

```cpp
// [[likely]] 없을 때
test edi, edi
jle .else_block         ; 별도 source hint 없음
mov eax, edi
add eax, eax
ret
.else_block:
; ...

// [[likely]] 있을 때
test edi, edi
jle .else_block         ; 명령 자체에 정적 hint bit가 생기는 것은 아님
mov eax, edi            ; Hot path가 fall-through
add eax, eax
ret
.else_block:            ; 실제 배치는 compiler와 주변 CFG에 따라 달라짐
; ...
```

두 소스가 같은 어셈블리를 만들 수도 있다. 속성은 hardware predictor를 설정하지 않으며, 관찰 가능한 차이는 block order, fall-through, outlining과 compiler의 비용 모델에 나타날 수 있다. 따라서 고정된 주석 대신 실제 대상 빌드의 CFG와 어셈블리를 비교한다.

## 5. 함수 아웃라이닝 (Function Outlining)

### 아웃라이닝이란?

**Outlining**: Cold path 코드를 별도 함수로 분리하는 최적화.

```cpp
// Before: 인라인된 에러 처리
int parse(const char* str) {
    if (!str) {
        fprintf(stderr, "Null string\n");
        log_stack_trace();
        send_crash_report();
        return -1;
    }

    return atoi(str);  // Hot path
}
```

```cpp
// After: Outlined 에러 처리
__attribute__((cold, noinline))
static int handle_null_error() {
    fprintf(stderr, "Null string\n");
    log_stack_trace();
    send_crash_report();
    return -1;
}

int parse(const char* str) {
    if (!str) [[unlikely]] {
        return handle_null_error();
    }

    return atoi(str);
}
```

효과:
- `parse` 함수의 크기 감소 → 인라인 가능성 증가
- Hot path의 I-cache 점유 감소
- Cold path 코드가 별도 섹션에 배치

### LLVM의 자동 아웃라이닝

LLVM은 지원 타깃에서 `-Oz`(크기 최적화) 옵션의 machine outliner로 중복된 machine code를 함수로 추출한다. 이는 IR 수준의 hot/cold block 분리와는 다른 기법이다:

```bash
clang -Oz -c code.c
```

공통된 코드 패턴을 찾아 함수로 추출:

```cpp
// 원본
void func1() {
    if (error1) {
        log("Error");
        cleanup();
        exit(1);
    }
    // ...
}

void func2() {
    if (error2) {
        log("Error");
        cleanup();
        exit(1);
    }
    // ...
}
```

```cpp
// Outlined
static void outlined_error_handler() {
    log("Error");
    cleanup();
    exit(1);
}

void func1() {
    if (error1) outlined_error_handler();
    // ...
}

void func2() {
    if (error2) outlined_error_handler();
    // ...
}
```

아웃라이닝 뒤 hot 함수의 machine-code 크기, I-cache·iTLB miss와 branch 수를 측정한다. 컴파일러가 이미 cold block을 분리했다면 수동 변경으로 코드 크기가 거의 줄지 않을 수 있다.

## 6. 링크 타임 최적화와 코드 레이아웃

### LTO (Link-Time Optimization)

LTO는 링크 시점에 전체 프로그램을 최적화한다.

```bash
# GCC
gcc -flto -O3 *.c -o program

# Clang
clang -flto=thin -O3 *.c -o program
```

LTO는 함수 간 분석을 가능하게 하지만, 아래 최적화가 모두 자동으로 적용된다는 보장은 없다.

1. **Interprocedural analysis**: 함수 호출 그래프 분석
2. **Hot function clustering**: 자주 함께 호출되는 함수를 가까이 배치
3. **Cold code sinking**: Cold 함수를 섹션 끝으로 이동

예:

```text
Before LTO (링크 순서대로):
[func_a] [func_b] [error_handler_a] [func_c] [error_handler_b] ...

After LTO:
[func_a] [func_b] [func_c] ... [error_handler_a] [error_handler_b] ...
 ← Hot path →              ← Cold path →
```

실제 배치는 compiler·linker 옵션과 profile 정보에 달려 있다. map file과 disassembly로 hot function clustering과 cold section 분리가 일어났는지 확인한다.

### Profile-Guided Optimization (PGO)

LTO가 함수 간 정보를 쓰는 것과 별개로, 실제 실행 프로파일을 기반으로 배치와 인라이닝을 정하는 **PGO**가 있다[^5].

과정:

```bash
# 1. 계측 빌드 (instrumentation)
gcc -fprofile-generate -O2 code.c -o program

# 2. 대표적인 워크로드 실행
./program < typical_input.txt
# 여러 번 실행하여 프로파일 수집

# 3. 프로파일 기반 재컴파일 (계측 빌드와 같은 출력 이름을 유지해야 profile을 찾는다)
gcc -fprofile-use -O2 code.c -o program
```

PGO가 수집하는 정보:
- 각 함수 호출 빈도
- 각 분기의 실제 taken 확률
- 루프 반복 횟수
- 간접 호출 대상 (가상 함수)

최적화 적용:
1. **Hot 함수 인라이닝**: 관측한 빈도를 인라이닝 비용 판단에 반영
2. **Cold 함수 아웃라이닝**: 드물게 호출되는 것은 분리
3. **분기 예측 힌트**: 실제 확률 기반
4. **코드 레이아웃**: 실행 흐름에 맞춰 재배치
5. **간접 호출 최적화**: 가장 흔한 타겟을 직접 호출로 변환

PGO의 이득은 profile 대표성, 기존 최적화 수준과 병목에 달려 있다. 평균 시간만 비교하지 말고 동일 binary 옵션에서 CPU time, instruction 수, I-cache·branch miss, binary 크기와 p95를 함께 비교한다. 잘못된 profile은 드문 실제 경로의 배치를 악화시킬 수 있다.

### BOLT: Binary-Level 최적화

Facebook(Meta)이 개발한 BOLT(Binary Optimization and Layout Tool)는 **컴파일된 바이너리**를 최적화한다[^6].

아래는 지원되는 x86-64/AArch64 ELF 환경의 예다. symbol table을 보존하고 함수 재배치용 relocation을 남긴 바이너리를 사용한다(예: 최종 링크에 `-Wl,--emit-relocs`). branch-stack 수집도 대상 CPU·OS에서 지원되어야 한다.

```bash
# 1. Perf로 프로파일 수집 (실제 실행)
perf record -e cycles:u -j any,u -- ./program

# 2. perf 데이터를 BOLT가 읽는 profile로 변환
perf2bolt -p perf.data -o perf.fdata ./program

# 3. BOLT로 바이너리 재배치
llvm-bolt ./program -o program.bolt \
  -data=perf.fdata \
  -reorder-blocks=ext-tsp \
  -reorder-functions=hfsort \
  -split-functions \
  -split-all-cold
```

BOLT의 기법:
1. **Basic block 재배치**: Hot path가 연속되도록
2. **함수 재배치**: 호출 그래프 기반
3. **함수 분할**: 하나의 함수를 hot/cold 부분으로 분리

```text
Original function layout:
[func_start]
  hot_path_1
  cold_error_handling_1
  hot_path_2
  cold_error_handling_2
[func_end]

BOLT optimized:
[func_start.hot]
  hot_path_1
  hot_path_2
  jmp .continue
[func_start.cold]  ; 별도 섹션
  cold_error_handling_1
  cold_error_handling_2
.continue:
[func_end]
```

BOLT 논문과 공개 사례에는 데이터센터 바이너리별 개선치가 보고되어 있다. 이 값은 각 실험의 CPU, binary layout, profile 대표성, baseline linker 최적화에 종속된다. 숫자를 제품 일반값으로 재사용하지 않고 원 논문의 workload 표와 측정 조건을 함께 인용해야 한다.

**중요**: BOLT는 지원되는 형식과 재배치·심볼 정보 등 필요한 메타데이터를 갖춘 바이너리를 대상으로 post-link 최적화를 수행한다. 단순히 임의의 서드파티 실행 파일이면 모두 적용할 수 있다는 뜻은 아니다.

### Propeller: Google의 접근

Google은 LLVM에 Propeller를 통합했다[^7]. BOLT와 유사하지만 컴파일러와 더 깊이 통합됨:

```bash
# 컴파일 시 기본 블록 라벨 생성
clang -fbasic-block-sections=labels -O2 code.c -o program

# Perf 프로파일 수집
perf record -e cycles -j any,u -- ./program

# perf 프로파일을 컴파일러/링커 프로파일로 변환 (원시 perf.data를 그대로 받는다)
generate_propeller_profiles --binary=program --profile=perf.data \
  --cc_profile=cc_profile.txt --ld_profile=ld_profile.txt

# Propeller로 재컴파일 + 재링크
clang -fbasic-block-sections=list=cc_profile.txt -O2 code.c -o program_opt \
  -fuse-ld=lld -Wl,--symbol-ordering-file=ld_profile.txt
```

BOLT vs Propeller:
- BOLT: 바이너리 수준, 소스 불필요, 빠른 적용
- Propeller: 컴파일러 수준, 더 정밀한 제어, 디버그 정보 유지

Propeller 논문이 보고한 개선치는 논문의 서비스·바이너리·프로파일 수집 조건에서 얻은 사례다. 이 수치를 다른 프로그램의 기대값으로 옮기지 말고, 동일 배포 workload에서 baseline과 instruction·I-cache·branch 지표를 직접 비교한다.

## 7. 실전 예제: JSON 파서 최적화

실제 코드에 hot/cold 분리를 적용해보자.

### 초기 버전

```cpp
enum class JsonToken {
    Object, Array, String, Number, True, False, Null, Error
};

JsonToken parse_value(const char*& p) {
    skip_whitespace(p);

    if (*p == '{') {
        return JsonToken::Object;
    } else if (*p == '[') {
        return JsonToken::Array;
    } else if (*p == '"') {
        return JsonToken::String;
    } else if (*p == 't') {
        if (strncmp(p, "true", 4) == 0) {
            p += 4;
            return JsonToken::True;
        } else {
            log_error("Expected 'true'");
            return JsonToken::Error;
        }
    } else if (*p == 'f') {
        if (strncmp(p, "false", 5) == 0) {
            p += 5;
            return JsonToken::False;
        } else {
            log_error("Expected 'false'");
            return JsonToken::Error;
        }
    } else if (*p == 'n') {
        if (strncmp(p, "null", 4) == 0) {
            p += 4;
            return JsonToken::Null;
        } else {
            log_error("Expected 'null'");
            return JsonToken::Error;
        }
    } else if (isdigit(static_cast<unsigned char>(*p)) || *p == '-') {
        return JsonToken::Number;
    } else {
        log_error("Unexpected character");
        return JsonToken::Error;
    }
}
```

이 함수는 유효한 NUL 종료 문자열 안의 위치를 입력받아 토큰의 시작 문자를 분류하는 스케치로, 문자열·숫자 본문의 소비와 전체 입력 소비·suffix 검사는 생략했다. 길이만 주어진 네트워크 버퍼라면 end 포인터와 범위 검사도 필요하다. 완전한 parser는 여기에 본문 소비와 실패 위치 반환을 추가해야 한다.

문제:
- 에러 처리 코드가 hot path와 섞여 있음
- 함수가 큼 → 인라인 비용이 커져 후보에서 밀리기 쉬움
- I-cache 낭비

### 최적화 버전

```cpp
// Cold: 에러 처리 함수
__attribute__((cold, noinline))
static JsonToken report_parse_error(const char* msg, const char* p) {
    log_error(msg);
    log_context(p);
    return JsonToken::Error;
}

// Hot: 메인 파서
__attribute__((hot))
JsonToken parse_value(const char*& p) {
    skip_whitespace(p);

    char c = *p;

    // Fast path: 가장 흔한 케이스 먼저
    if (c == '"') return JsonToken::String;
    if (c == '{') return JsonToken::Object;
    if (c == '[') return JsonToken::Array;
    if (isdigit(static_cast<unsigned char>(c)) || c == '-') return JsonToken::Number;

    // Less common cases
    if (c == 't') {
        if (strncmp(p, "true", 4) == 0) {
            p += 4;
            return JsonToken::True;
        }
        return report_parse_error("Expected 'true'", p);
    }

    if (c == 'f') {
        if (strncmp(p, "false", 5) == 0) {
            p += 5;
            return JsonToken::False;
        }
        return report_parse_error("Expected 'false'", p);
    }

    if (c == 'n') {
        if (strncmp(p, "null", 4) == 0) {
            p += 4;
            return JsonToken::Null;
        }
        return report_parse_error("Expected 'null'", p);
    }

    return report_parse_error("Unexpected character", p);
}
```

개선점:
1. 에러 처리 → `report_parse_error` (cold, noinline)
2. 흔한 케이스를 앞에 배치 (string, object, array)
3. corpus에서 실제로 흔한 토큰 순서에 맞춘 분기 배치
4. 함수 크기 감소 → 인라인 가능성 증가

측정 표에는 다음과 같이 환경과 결과를 채운다.

```text
CPU·compiler·flags:
입력 corpus와 크기:
baseline median/p95:
hot-cold median/p95:
```

I-cache 미스 측정 (perf):

```bash
perf stat -e L1-icache-load-misses ./parser

baseline: instructions, cycles, L1-icache-load-misses
variant:  instructions, cycles, L1-icache-load-misses
```

### 추가 최적화: PGO 적용

```bash
# 1. 계측 빌드
clang++ -fprofile-generate -O2 parser.cpp -o parser_instr

# 2. 대표 입력으로 실행
./parser_instr typical.json
./parser_instr large.json
./parser_instr nested.json

# 3. raw profile을 indexed profile로 변환
llvm-profdata merge -o parser.profdata *.profraw

# 4. PGO 빌드
clang++ -fprofile-use=parser.profdata -O2 parser.cpp -o parser_pgo
```

PGO 적용 여부는 최종 binary의 layout과 optimization record에서 확인한다. 적용될 수 있는 변환은 다음과 같다.
- `skip_whitespace` 인라이닝 (빈도수 기반)
- Object/Array 파싱 함수의 코드 재배치
- 가상 함수 호출 devirtualization

## 8. 실전 팁과 안티패턴

### DO: Hot Path 최소화

```cpp
// Good: 에러 체크를 밖으로
void process_batch(int* data, int n) {
    if (data == nullptr || n <= 0) [[unlikely]] {
        handle_error();
        return;
    }

    // Hot path: 에러 체크 없음
    for (int i = 0; i < n; i++) {
        data[i] = expensive_computation(data[i]);
    }
}
```

```cpp
// Bad: 루프 안에서 매번 체크
void process_batch(int* data, int n) {
    for (int i = 0; i < n; i++) {
        if (data == nullptr) return;  // 매번 체크!
        data[i] = expensive_computation(data[i]);
    }
}
```

### DO: 어설션은 Cold

```cpp
// Good: 어설션은 cold 함수로
__attribute__((cold, noreturn))
void assertion_failed(const char* expr, const char* file, int line) {
    fprintf(stderr, "Assertion failed: %s at %s:%d\n", expr, file, line);
    abort();
}

#define ASSERT(x) do { \
    if (!(x)) [[unlikely]] \
        assertion_failed(#x, __FILE__, __LINE__); \
} while (false)
```

### DON'T: 과도한 인라이닝

```cpp
// Bad: 큰 함수를 강제 인라이닝
__attribute__((always_inline))
inline void huge_function() {
    // 500 줄의 코드...
}

// 여러 곳에서 호출 → 코드 크기 폭발 → I-cache 압력
```

대신:

```cpp
// Good: 컴파일러가 결정하게 (또는 hot만 인라이닝)
inline void huge_function() {
    // ...
}
```

### DO: 초기화와 핫 루프 분리

```cpp
// Good: 초기화는 한 번만
void init() {
    // Cold: 한 번만 실행
    load_config();
    allocate_buffers();
    setup_callbacks();
}

void main_loop() {
    // Hot: 매 프레임 실행
    while (running) {
        update();
        render();
    }
}
```

### DON'T: Hot Path에 로깅

```cpp
// Bad: Hot path에 로깅
for (int i = 0; i < 1000000; i++) {
    log_debug("Processing item %d", i);  // 매번 호출!
    process(items[i]);
}
```

```cpp
// Good: 조건부 로깅 (컴파일 타임)
#ifdef DEBUG
    #define LOG_DEBUG(...) log_debug(__VA_ARGS__)
#else
    #define LOG_DEBUG(...) do {} while(0)
#endif

for (int i = 0; i < 1000000; i++) {
    LOG_DEBUG("Processing item %d", i);  // Release에서 제거됨
    process(items[i]);
}
```

## 9. 측정과 프로파일링

### Perf를 이용한 I-cache 분석

```bash
# I-cache 미스 측정
perf stat -e L1-icache-loads,L1-icache-load-misses ./program

# 함수별 I-cache 미스
perf record -e L1-icache-load-misses ./program
perf report
```

출력 예:

```text
Performance counter stats for './program':

    1,523,847,293      L1-icache-loads
        4,234,891      L1-icache-load-misses     # 0.28% of all L1-icache hits

      2.384512 seconds time elapsed
```

miss 비율 하나에 보편적인 양호 기준은 없다. frontend stall이 전체 cycles에서 차지하는 비중, miss 한 번의 실제 손실, 실행 빈도와 wall time 변화를 함께 본다.

### Cachegrind로 상세 분석

```bash
valgrind --tool=cachegrind --I1=32768,8,64 ./program
cg_annotate cachegrind.out.<pid>
```

함수별 I-cache 참조/미스 확인 가능.

### LLVM의 -ftime-trace

```bash
clang++ -O2 -ftime-trace code.cpp
```

`code.json` 파일 생성 → Chrome의 `chrome://tracing`에서 열기

컴파일 시간과 단계별 시각화. 어떤 함수가 인라인되고 어떤 최적화가 적용되었는지는 `-Rpass`/`-Rpass-missed`나 `-fsave-optimization-record`로 확인한다.

### PGO 프로파일 분석

```bash
# Clang: 프로파일 요약
llvm-profdata show --topn=20 default.profraw

# GCC: 커버리지 계측(--coverage)으로 만든 .gcno/.gcda가 있어야 동작한다
gcov -b code.c
```

가장 자주 실행되는 함수/블록 확인.

## 10. 고급 주제: Code Layout 알고리즘

### Basic Block Chaining

컴파일러는 basic block(분기 없는 명령어 시퀀스)을 어떻게 배치할까?

알고리즘 (LLVM의 MachineBlockPlacement):

1. **빈도수 수집**: PGO 또는 정적 예측
2. **Chain 구성**: 자주 이어지는 블록을 체인으로 묶음
3. **Chain 배치**: Hot chain을 앞에, cold chain을 뒤에

예:

```text
CFG (Control Flow Graph):
  A (100%) → B (99%) → D (98%)
           ↘ C (1%)

Chain 1: A → B → D (hot)
Chain 2: C (cold)

Layout: [A][B][D] ... [C]
```

### TSP (Traveling Salesman Problem) 기반

BOLT의 ExtTSP는 **함수 내부 basic block reordering**을 weighted path 문제로 모델링한다[^8]:

- 노드: basic block
- 엣지 가중치: block 사이 control-flow 빈도
- 목표: 자주 이어서 실행되는 block을 가까이 놓아 fall-through와 instruction locality를 개선

NP-hard 문제이므로 휴리스틱 사용 (탐욕 알고리즘 + 지역 최적화).

함수와 함수 사이의 배치를 call graph로 최적화하는 문제는 다음의 HFSort 같은 **function layout** 단계가 담당한다. Basic-block ExtTSP와 function sorting을 같은 그래프로 설명하면 최적화 단위가 뒤섞인다.

### HFSort (Hot Function Sorting)

HFSort 계열은 profile의 call graph에서 함께 호출되는 함수들을 가까이 배치하여 iTLB와 instruction-cache locality를 개선한다. 단순 실행 횟수 내림차순은 직관을 보여 주는 baseline일 뿐이며, 실제 알고리즘은 caller-callee affinity와 cluster 병합 점수를 사용한다[^4].

```python
def hotness_baseline(functions):
    # 교육용 baseline: call_graph affinity를 쓰지 않는다.
    return sorted(functions, key=lambda f: f.count, reverse=True)
```

페이지 경계를 고려하려면 단순 카운터를 0으로 되돌리는 것으로는 부족하다. 링커가 실제 주소에 padding·alignment를 반영해야 하며, 그 비용도 layout objective에 포함해야 한다. 이 배치가 나은지는 같은 profile로 binary를 만든 뒤 실제 주소 배치와 frontend stall을 비교해 판단한다.


---

## 결론

Hot/Cold 코드 분리는 의미를 보존하면서 instruction working set과 layout을 바꾸는 최적화다. 빌드 복잡도, profile 수집·갱신, 디버그 symbol과 stack trace 품질, 코드 중복·과도한 아웃라이닝이라는 비용이 있다.

핵심은 실제 profile에서 자주 실행되는 basic block과 드문 block을 구분하는 것이다. Cold path를 분리하면 hot working set과 inlining 판단이 개선될 수 있다. `[[likely]]`/`[[unlikely]]`와 `__attribute__((hot/cold))`는 compiler의 정적 비용 모델과 block 배치에 주는 힌트이지 hardware predictor를 설정하는 명령이 아니다. PGO와 post-link optimizer는 관측한 edge frequency로 배치를 바꾸지만, 이득은 workload 대표성과 대상 CPU에서 재측정해야 한다.

성능 최적화의 본질은 코드를 빠르게 만드는 것이 아니라, 코드를 느리게 만드는 요소를 치우는 것이다. Cold path 분리는 빠른 코드를 만드는 것이 아니라, 빠른 코드를 방해하는 요소를 치우는 작업이다.

## 더 읽을거리

### 공식 문서 및 매뉴얼

[^1]: Intel Corporation. "Intel 64 and IA-32 Architectures Optimization Reference Manual". https://www.intel.com/content/www/us/en/architecture-and-technology/64-ia-32-architectures-optimization-manual.html (Section 3.4.1.4: Code Alignment)

[^2]: Fog, Agner. "The microarchitecture of Intel, AMD and VIA CPUs". https://www.agner.org/optimize/ (Chapter 3: Branch Prediction)

[^3]: GCC Documentation. "Common Function Attributes". https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html (`hot`, `cold` 속성 설명)

### 학술 논문

[^4]: Ottoni, Guilherme and Maher, Bertrand. "Optimizing Function Placement for Large-Scale Data-Center Applications". CGO 2017. (Facebook의 HFSort 함수 배치 최적화)

[^5]: Chen, Dehao, et al. "AutoFDO: Automatic Feedback-Directed Optimization for Warehouse-Scale Applications". CGO 2016. https://research.google/pubs/pub45290/ (Google의 샘플링 기반 PGO)

[^6]: Panchenko, Maksim, et al. "BOLT: A Practical Binary Optimizer for Data Centers and Beyond". CGO 2019. https://research.facebook.com/publications/bolt-a-practical-binary-optimizer-for-data-centers-and-beyond/

[^7]: Shen, Han, et al. "Propeller: A Profile Guided, Relinking Optimizer for Warehouse-Scale Applications". ASPLOS 2023. (Google)

[^8]: Newell, Andy and Pupyrev, Sergey. "Improved Basic Block Reordering". IEEE Transactions on Computers, 2020. (ExtTSP 알고리즘)

### 기술 블로그

- Majkowski, Marek. ["Branch predictor: How many 'if's are too many?"](https://blog.cloudflare.com/branch-predictor/). Cloudflare.
- Clang Documentation. "Profile Guided Optimization". https://clang.llvm.org/docs/UsersManual.html#profile-guided-optimization
- Phoronix. "GCC vs LLVM: PGO Performance". https://www.phoronix.com/ (다양한 워크로드에서 PGO 벤치마크)

### LLVM/컴파일러 자료

- LLVM Developer Meeting. "Profile Guided Optimization in LLVM". https://llvm.org/devmtg/ (여러 해의 발표 자료)
- Ayers, Grant, et al. "AsmDB: Understanding and Mitigating Front-End Stalls in Warehouse-Scale Computers". ISCA 2019. (I-cache와 프론트엔드 병목)
- Ottoni, Guilherme. "HHVM JIT: A Profile-Guided, Region-Based Compiler for PHP and Hack". PLDI 2018. (Facebook의 JIT 컴파일러에서 hot/cold 분리)

### 실무 사례 연구

- Facebook Engineering. BOLT 도입 사례. https://engineering.fb.com/ (원문 제목·직접 링크 미확인)
- Google Chrome Blog. PGO·LTO 적용 사례. https://blog.chromium.org/ (원문 제목·직접 링크 미확인)
- Microsoft Visual C++ Team Blog. "Profile Guided Optimization with Visual Studio". https://devblogs.microsoft.com/cppblog/ (MSVC의 PGO 기능)

### 도구 및 벤치마크

- Linux Perf Wiki. https://perf.wiki.kernel.org/ (perf 사용법과 이벤트 설명)
- Valgrind Cachegrind Manual. https://valgrind.org/docs/manual/cg-manual.html
- LLVM Machine Code Analyzer (llvm-mca). https://llvm.org/docs/CommandGuide/llvm-mca.html (명령어 레벨 성능 분석)
- Compiler Explorer (Godbolt). https://godbolt.org/ (코드 레이아웃 시각화)

### 서적

- Drepper, Ulrich. "What Every Programmer Should Know About Memory". 2007. https://people.freebsd.org/~lstewart/articles/cpumemory.pdf (Section 3.4: Instruction Cache)
- Fog, Agner. "Optimizing software in C++". https://www.agner.org/optimize/optimizing_cpp.pdf (메모리 접근과 코드 배치 관련 장)
- Marwedel, Peter, & Goossens, Gert (eds.). *Code Generation for Embedded Processors*. Kluwer Academic Publishers, 1995.
