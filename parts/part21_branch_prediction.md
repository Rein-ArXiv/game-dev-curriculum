## 들어가며

if문 순서가 성능을 바꿀 수 있는가?

같은 로직, 같은 데이터, 단지 if문의 순서만 바꿨는데 성능이 2배 차이 난다면 믿겠는가? Intel Pentium 4는 20단계(2000년 Willamette)에서 31단계(2004년 Prescott)에 이르는 긴 파이프라인을 자랑했다. 하지만 이 긴 파이프라인은 양날의 검이었다. 분기 예측이 실패하면 31단계를 모두 비워야 했고, 이는 수십 사이클의 손실을 의미했다.

현대 CPU는 놀라운 속도로 동작하지만, 그 속도는 "다음에 무슨 일이 일어날지 예측"할 수 있을 때만 유지된다. **Branch Prediction**(분기 예측)은 CPU가 미래를 내다보는 수정 구슬이다. 예측이 맞으면 파이프라인이 막힘없이 흐르지만, 틀리면 모든 추측 작업이 물거품이 된다.

이 글은 CPU가 어떻게 분기를 예측하는지, 예측 실패의 비용이 얼마나 되는지, 그리고 프로그래머가 어떻게 이를 돕거나 회피할 수 있는지 다룬다.


---

## 목차
1. [파이프라인과 분기 예측의 필연성](#1-파이프라인과-분기-예측의-필연성)
2. [Branch Prediction의 진화](#2-branch-prediction의-진화)
3. [Branch Misprediction의 비용](#3-branch-misprediction의-비용)
4. [컴파일러 힌트: likely와 unlikely](#4-컴파일러-힌트-likely와-unlikely)
5. [Profile-Guided Optimization](#5-profile-guided-optimization)
6. [Branchless Programming](#6-branchless-programming)
7. [실전 벤치마크](#7-실전-벤치마크)
8. [하드웨어별 분기 예측 특성](#8-하드웨어별-분기-예측-특성)
9. [실전 체크리스트](#9-실전-체크리스트)
10. [미래: 분기 예측의 진화](#10-미래-분기-예측의-진화)


---

## 1. 파이프라인과 분기 예측의 필연성

### 명령어 파이프라이닝: 속도의 비밀

현대 CPU는 명령어를 여러 단계로 나누어 동시에 처리한다. 고전적인 5단계 파이프라인:

1. **IF (Instruction Fetch)**: 명령어 가져오기
2. **ID (Instruction Decode)**: 명령어 해석
3. **EX (Execute)**: 실행
4. **MEM (Memory Access)**: 메모리 접근
5. **WB (Write Back)**: 결과 저장

이상적인 경우, 매 사이클마다 하나의 명령어가 완료된다.

```text
Clock:  1    2    3    4    5    6    7
Inst1:  IF   ID   EX   MEM  WB
Inst2:       IF   ID   EX   MEM  WB
Inst3:            IF   ID   EX   MEM  WB
Inst4:                 IF   ID   EX   MEM  WB
```

5단계가 걸리는 작업을 매 사이클마다 하나씩 완료할 수 있다. 이것이 파이프라이닝의 마법이다.

### 분기가 파이프라인을 막는다

문제는 분기 명령어(if, switch, loop)다.

```c
if (x > 0) {
    // Path A
    result = x * 2;
} else {
    // Path B
    result = x * -2;
}
```

CPU는 조건이 평가되기 전까지 어느 경로로 갈지 모른다. 조건 평가는 EX 단계에서야 완료된다. 매번 분기 결과를 기다린다면 파이프라인의 이점이 사라진다. 이것이 Branch Prediction이 필요한 이유다.

### 추측 실행 (Speculative Execution)

CPU는 기다리지 않는다. 대신 "예측"한다.

```text
Clock:  1    2    3    4    5    6    7
if:     IF   ID   EX   MEM  WB
예측:        IF   ID   EX   MEM  WB   // Path A라고 가정
```

예측이 맞으면 손실이 없다. 틀리면 파이프라인의 모든 추측 작업이 폐기되고, 올바른 경로부터 다시 시작한다. 이것이 **Branch Misprediction Penalty**(분기 예측 실패 비용)다.

## 2. Branch Prediction의 진화

### 정적 예측 (Static Prediction): 초기의 단순함

1980년대 초기 RISC 프로세서는 간단한 규칙을 사용했다.

- **후방 분기(Backward Branch)는 taken**: 루프는 보통 여러 번 반복
- **전방 분기(Forward Branch)는 not taken**: if문은 보통 false

```asm
loop:
    ; 루프 본문
    cmp rcx, 0
    jnz loop        ; 후방 분기 -> 예측: taken
```

이 단순한 규칙만으로도 60-70%의 정확도를 달성했다. 하지만 그것만으로는 부족했다.

### 동적 예측 (Dynamic Prediction): 역사로부터 배우기

현대 CPU는 과거의 분기 결과를 기억하고 학습한다. 핵심 구조 세 가지가 있다.

#### Branch History Table (BHT)

가장 단순한 형태로, 각 분기마다 1비트를 저장한다.

```text
분기 주소    마지막 결과
0x400100  -> Taken (1)
0x400108  -> Not Taken (0)
0x400110  -> Taken (1)
```

문제는 1비트로는 패턴을 놓친다는 점이다.

```c
// 패턴: T T N T T N T T N...
for (int i = 0; i < n; i++) {
    if (i % 3 != 0) {  // 2번 taken, 1번 not taken
        // ...
    }
}
```

#### 2-Bit Saturating Counter (PHT)

해결책은 4가지 상태를 사용하는 것이다.

```text
00: Strongly Not Taken
01: Weakly Not Taken
10: Weakly Taken
11: Strongly Taken
```

상태 전이는 T이면 +1(최대 11), N이면 -1(최소 00)이다. occasional misprediction에 강하다.

```c
// 패턴: N N N N N N N T N N N N...
if (rare_condition) {  // 99% false (대부분 Not Taken)
    // ...
}
```

`rare_condition`이 99% 거짓이라면 분기는 거의 항상 Not Taken이다. 상태는 보통 `00 Strongly Not Taken`에 머문다. 1%의 예외(조건이 참)가 발생해도 상태는 `00 → 01`로만 바뀌므로 다음 예측은 여전히 Not Taken이다. 1-bit 예측기였다면 매 예외마다 방향이 뒤집혀 두 번 연속 미스를 냈겠지만, 2-bit 카운터는 이 노이즈를 흡수한다.

#### Branch Target Buffer (BTB)

분기가 어디로 점프하는지도 기억한다.

```text
분기 주소      타겟 주소      예측
0x400100  ->  0x400200   Taken (11)
0x400108  ->  0x400500   Not Taken (01)
```

BTB가 없으면 예측이 맞더라도 타겟 주소를 계산하기 위해 대기해야 한다. BTB는 "어디로"까지 미리 알려준다.

### 고급 예측기: Two-Level Adaptive Predictor

Intel Core 시리즈와 AMD Zen은 더 정교한 예측기를 사용한다.

**Global History Register (GHR)** 는 최근 N개 분기의 결과를 기록한다.

```text
GHR: [T T N T N N T T]  (최근 8개 분기)
```

이 히스토리를 인덱스로 사용하여 PHT에 접근한다.

```c
// 의사 코드
uint8_t ghr = global_history;
uint8_t index = (branch_pc ^ ghr) & TABLE_MASK;
Prediction pred = PHT[index];
```

GHR이 강력한 이유는 **Correlated branches**(상관 분기)를 잡아내기 때문이다.

```c
if (a < 10) {       // Branch 1
    // ...
}

if (a < 20) {       // Branch 2
    // Branch 1이 taken이면 Branch 2도 항상 taken
}
```

### Perceptron 기반 예측기

Perceptron predictor는 긴 전역 history의 선형 상관을 작은 가중치 표로 학습하는 대표적인 연구 기법이다. 상용 CPU의 정확한 predictor 구성은 대개 공개되지 않으므로 특정 Intel·AMD 세대가 이 형태를 그대로 사용한다고 단정하지 않는다.

```text
Prediction = sign(w0 + w1*h1 + w2*h2 + ... + wN*hN)
```

- `hi`: GHR의 i번째 비트 (-1 또는 +1)
- `wi`: 학습된 가중치
- 결과가 양수면 Taken, 음수면 Not Taken

예측이 틀릴 때마다 가중치를 업데이트한다.

```c
if (misprediction) {
    for (int i = 0; i < N; i++) {
        weights[i] += (actual == Taken ? 1 : -1) * history[i];
    }
}
```

이 방식은 복잡한 패턴도 학습할 수 있다.

### 예측 정확도는 장르가 아니라 분기 지점별 속성이다

“서버는 90%, AI는 70%” 같은 애플리케이션 종류별 고정 정확도는 의미가 없다. 같은 프로그램 안에도 거의 항상 같은 방향인 오류 검사, 반복 횟수가 안정적인 루프, 데이터에 따라 무작위에 가까운 분기가 함께 있다. 전체 miss rate는 실행 빈도가 높은 분기가 가중된 평균이라 한 문제 지점을 숨길 수도 있다. branch instruction 주소별 counter와 source mapping으로 hot miss 지점을 찾는다.

## 3. Branch Misprediction의 비용

### 복구 비용은 파이프라인 단계 수 하나가 아니다

예측이 틀리면 잘못된 경로의 younger uop을 버리고 올바른 target에서 fetch·decode를 다시 시작해야 한다. 비용은 branch resolution 시각, front-end와 uop cache 상태, target의 I-cache/TLB hit, 뒤의 독립 작업에 따라 달라진다. 공개된 “파이프라인 단계 수”와 프로그램이 관측하는 고정 cycle 수를 일대일 대응시키면 안 된다. 대상 CPU에서 dependent branch chain 같은 microbenchmark와 hardware counter로 측정한다.

### 실제 프로그램에서의 영향

간단한 예제로 측정해보자.

```c
// 벤치마크: 배열에서 값 찾기
int sum = 0;
for (int i = 0; i < SIZE; i++) {
    if (data[i] >= 128) {
        sum += data[i];
    }
}
```

**시나리오 1: 정렬된 데이터** - 분기 패턴이 예측 가능하다. 앞부분은 계속 Not Taken, 뒤부분은 계속 Taken.

**시나리오 2: 랜덤 데이터** - 분기 패턴이 무작위다. 예측 불가능.

정렬·랜덤 입력 비교에서는 compiler가 분기를 CMOV나 SIMD mask로 if-convert했는지 먼저 assembly와 vectorization report로 확인한다. 두 버전의 비교 횟수와 memory traffic을 같게 만들고 `branches`, `branch-misses`, cycles, instructions를 함께 기록해야 차이를 misprediction에 귀속할 수 있다. 정렬 자체의 비용도 end-to-end 판단에는 포함한다.

### Perf를 이용한 측정

Linux의 `perf` 도구로 실제 misprediction을 측정할 수 있다.

```bash
perf stat -e branches,branch-misses ./program

# 출력:
#  100,523,421  branches
#   15,234,567  branch-misses  # 15.15% miss rate
```

15%라는 비율만으로 심각도를 판정할 수 없다. 분기 수가 적으면 영향이 작을 수 있고, hot loop의 매 iteration 분기라면 큰 frontend 손실이 될 수 있다. `branch-misses / branches`와 함께 miss로 잃은 cycles, 함수별 실행 비중과 전체 wall time을 본다.

```bash
perf record -e branch-misses ./program
perf report

# 어느 함수에서 misprediction이 많은지 확인
```

## 4. 컴파일러 힌트: likely와 unlikely

### GCC/Clang의 __builtin_expect

프로그래머가 예측을 돕는 방법.

```c
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// 사용 예:
if (unlikely(ptr == NULL)) {
    handle_error();
}

if (likely(size > 0)) {
    process_data();
}
```

`__builtin_expect(expr, expected_value)`는 컴파일러에게 힌트를 준다.

- `expected_value = 1`: expr이 참일 가능성이 높음
- `expected_value = 0`: expr이 거짓일 가능성이 높음

#### 코드 배치 최적화

컴파일러는 이 정보를 사용해 코드를 재배치한다.

```c
// 원본:
if (unlikely(error)) {
    handle_error();
    return -1;
}
process_normal_case();

// 컴파일러가 생성하는 어셈블리 (개념적):
    test rax, rax
    jnz error_path       ; Not Taken으로 예측
    ; process_normal_case 코드 (fall-through)
    ...
    ret

error_path:              ; 멀리 배치됨
    ; handle_error 코드
    ...
    ret
```

장점은 두 가지다.

1. **I-Cache 친화적**: 자주 실행되는 경로가 연속되어 명령어 캐시 히트율 증가
2. **분기 예측 힌트**: CPU의 정적 예측(전방 분기 = not taken)과 일치

### C++20의 [[likely]]와 [[unlikely]]

표준 방식.

```cpp
int process(int x) {
    if (x > 0) [[likely]] {
        return x * 2;
    } else [[unlikely]] {
        return handle_negative(x);
    }
}

// switch문에서도 사용 가능:
switch (type) {
    case TYPE_COMMON: [[likely]]
        return fast_path();
    case TYPE_RARE: [[unlikely]]
        return slow_path();
}
```

컴파일러 지원: GCC 9+, Clang 12+, MSVC 19.26+.

#### 효과 측정

```cpp
void benchmark() {
    for (int i = 0; i < 10000000; i++) {
        int x = (rand() % 100 < 95) ? 1 : -1;  // 95% 양수
        process(x);
    }
}
```

`rand()` 호출이 지배하는 이 루프의 고정 시간으로 attribute 효과를 판단할 수 없다. 입력을 미리 만들고 힌트 전후 assembly, section 배치, branch miss와 I-cache miss를 비교한다.

### 주의: 잘못된 힌트는 역효과

```c
// 잘못된 사용
if (unlikely(data[i] > threshold)) {  // 실제로는 50% 확률
    // ...
}
```

힌트는 주로 compiler의 block 배치, inlining과 정적 비용 모델에 영향을 준다. CPU의 동적 predictor 상태를 지속적으로 강제하는 표준 C++ 명령은 아니다. 잘못된 힌트는 hot path를 불리하게 배치하거나 코드 크기 결정을 왜곡할 수 있다. 임의의 95% 규칙보다 PGO가 수집한 edge frequency를 우선하고 수동 힌트는 안정적인 오류·예외 경로에 제한한다.

## 5. Profile-Guided Optimization

### PGO의 원리

"추측하지 말고, 측정하라." PGO는 실제 실행 데이터를 기반으로 최적화한다.

#### 1단계: 계측 빌드

```bash
# GCC
gcc -fprofile-generate -O2 program.c -o program

# Clang
clang -fprofile-instr-generate -O2 program.c -o program

# MSVC
cl /GL program.c /link /LTCG:PGINSTRUMENT
```

계측 코드가 삽입되어 실행 정보를 수집한다.

#### 2단계: 프로파일링 실행

```bash
./program < typical_workload.txt
# 실행 후 .gcda (GCC) 또는 .profraw (Clang) 파일 생성
```

대표적인 워크로드를 사용해야 한다. 테스트 데이터가 실제 사용과 다르면 역효과다.

#### 3단계: 최적화 빌드

```bash
# GCC
gcc -fprofile-use -O2 program.c -o program_optimized

# Clang
llvm-profdata merge -output=default.profdata default.profraw
clang -fprofile-instr-use=default.profdata -O2 program.c -o program_optimized
```

컴파일러는 프로파일 데이터를 읽고 Hot path 인라인, Cold code 멀리 배치, 분기 예측 힌트 자동 삽입, 레지스터 할당 최적화를 적용한다.

### PGO의 효과를 검증하는 법

제품명과 고정 향상률을 나열해도 현재 코드의 이득을 예측할 수 없다. PGO는 edge frequency, 간접 호출 대상과 loop trip count를 제공해 inlining, devirtualization과 layout 결정을 바꿀 수 있다. baseline과 PGO binary에서 compiler·linker·flags를 고정하고 대표 workload 여러 개의 CPU time, instruction 수, branch·I-cache miss, binary 크기를 비교한다. 하나의 훈련 입력에서만 좋아지고 다른 맵·게임 모드에서 나빠지는지 별도 holdout workload로 확인한다.

### PGO의 한계와 주의사항

한계는 빌드 복잡도 증가(3단계 프로세스), 대표성 문제(프로파일 데이터가 실제 사용과 달라질 수 있음), 유지보수(코드 변경 시 재프로파일링 필요)다.

```c
// 위험: 프로파일 데이터가 편향됨
void process(int type) {
    switch (type) {
        case TYPE_A:  // 프로파일링 시 100% 이것만 실행
            fast_path();
            break;
        case TYPE_B:  // 실제 사용자는 주로 이걸 사용
            // 최적화에서 제외됨!
            slow_path();
            break;
    }
}
```

해결책은 다양한 시나리오를 포함하는 프로파일링이다.

## 6. Branchless Programming

### 조건부 이동 (CMOV)

분기 없이 조건을 처리하는 기법.

```cpp
// 분기 버전
int max_branch(int a, int b) {
    if (a > b)
        return a;
    else
        return b;
}

// Branchless 버전
int max_branchless(int a, int b) {
    return a > b ? a : b;  // 컴파일러가 CMOV를 선택할 수 있음
}
```

어셈블리 비교:

```asm
; 분기 버전
max_branch:
    cmp edi, esi
    jle .L2
    mov eax, edi
    ret
.L2:
    mov eax, esi
    ret

; Branchless 버전
max_branchless:
    cmp edi, esi
    mov eax, esi
    cmovg eax, edi  ; a > b이면 eax = edi
    ret
```

`cmovg`는 조건부로 값을 복사하지만 분기하지 않는다. 파이프라인이 막히지 않는다.

#### 언제 유리한가

**CMOV가 좋은 경우:**
- 분기 예측이 어려움 (50/50 확률)
- 분기 본문이 짧음 (단순 대입)

**분기가 나은 경우:**
- 예측이 쉬움 (95% 한쪽)
- 분기 본문이 김 (함수 호출 등)

이유는 CMOV로 변환되려면 컴파일러가 양쪽 식의 값을 미리 계산해 두 레지스터에 담아야 하기 때문이다. 양쪽이 부수효과 없는 단순 연산이면 두 값을 모두 계산해도 되지만, 함수 호출 같은 부수효과가 있으면 C/C++ 의미상 한쪽만 실행되어야 하므로 컴파일러는 CMOV를 만들지 않고 분기를 유지한다. 즉 다음 코드는 ternary로 보이더라도 CMOV가 되지 않고 일반 분기로 컴파일된다.

```c
// 함수 호출이 들어 있어 CMOV로 바뀌지 않는다.
// 호출 양쪽이 부수효과를 가질 수 있으므로 컴파일러는 분기를 유지한다.
int result = some_condition ? expensive_call_a() : expensive_call_b();
```

CMOV의 양쪽 계산 비용이 의미를 갖는 경우는 다음처럼 양쪽이 모두 단순 식인 경우다.

```c
// 양쪽이 부수효과 없는 단순식: CMOV 후보
int result = (a > b) ? a + 1 : a - 1;
```

컴파일러는 이 식을 CMOV, setcc와 산술, 또는 분기로 낮출 수 있다. 양쪽 계산이 싸고 조건이 예측하기 어렵다면 if-conversion이 유리할 수 있지만, dependency chain을 늘리거나 예측이 잘 되는 분기를 없애 오히려 느려질 수도 있다. 생성 코드를 확인하고 실제 입력 분포로 측정한다.

### 비트 조작 트릭

조건부 비트 설정:

```c
// 분기 버전
if (flag) {
    value |= BIT_MASK;
}

// Branchless 버전
value |= -flag & BIT_MASK;  // flag가 1이면 BIT_MASK, 0이면 0
```

부호 확장 활용:

```c
// flag가 0 또는 1일 때
int mask = -flag;  // flag=1 -> 0xFFFFFFFF, flag=0 -> 0x00000000
value = (value & ~mask) | (new_value & mask);
```

절댓값 branchless:

```c
// 분기 버전
int abs_branch(int x) {
    if (x < 0)
        return -x;
    else
        return x;
}

// Branchless 버전
uint32_t abs_magnitude(int x) {
    uint32_t ux = static_cast<uint32_t>(x);
    uint32_t mask = 0u - (ux >> 31);  // 음수면 UINT32_MAX
    return (ux ^ mask) + (mask & 1u);
}
```

부호 있는 음수의 right shift에 기대지 않고 unsigned modulo arithmetic으로 절댓값의 **크기**를 계산한다. 반환형이 `uint32_t`인 이유는 `INT_MIN`의 크기가 `int`로 표현되지 않기 때문이다.

### 룩업 테이블 (LUT)

복잡한 조건을 배열로 대체.

```cpp
// 분기 버전
int classify(int score) {
    if (score >= 90) return GRADE_A;
    else if (score >= 80) return GRADE_B;
    else if (score >= 70) return GRADE_C;
    else if (score >= 60) return GRADE_D;
    else return GRADE_F;
}

// 표준 C++ constexpr LUT. [0 ... 59] 문법은 GNU C 확장이다.
constexpr auto make_grade_table() {
    std::array<int, 101> table{};
    for (int score = 0; score <= 100; ++score) {
        table[score] = score >= 90 ? GRADE_A
                     : score >= 80 ? GRADE_B
                     : score >= 70 ? GRADE_C
                     : score >= 60 ? GRADE_D : GRADE_F;
    }
    return table;
}

constexpr auto GRADE_TABLE = make_grade_table();

int classify_lut(int score) {
    return GRADE_TABLE[std::clamp(score, 0, 100)];
}
```

테이블은 범위 매핑을 한 번의 index lookup으로 바꾸지만 `std::clamp`와 테이블 생성식이 실제 기계어에서 완전히 branchless라는 보장은 없다. 메모리 사용과 cache miss 가능성도 늘어나므로 assembly와 입력 분포를 함께 측정한다.

### SIMD를 이용한 Branchless

```c
// 조건부 덧셈: 값이 양수면 더하기
for (int i = 0; i < n; i++) {
    if (data[i] > 0) {
        result[i] += data[i];
    }
}
```

SIMD Branchless:

```c
// AVX2 사용
for (int i = 0; i < n; i += 8) {
    __m256i v = _mm256_loadu_si256((__m256i*)&data[i]);
    __m256i mask = _mm256_cmpgt_epi32(v, _mm256_setzero_si256());
    __m256i to_add = _mm256_and_si256(v, mask);
    __m256i res = _mm256_loadu_si256((__m256i*)&result[i]);
    res = _mm256_add_epi32(res, to_add);
    _mm256_storeu_si256((__m256i*)&result[i], res);
}
```

마스크를 사용해 조건부로 값을 선택한다. 8개를 동시에 처리하면서 분기도 없다.

## 7. 실전 벤치마크

### 정렬 vs 비정렬 데이터

테스트 코드:

```c
int sum_if(int threshold) {
    int sum = 0;
    for (int i = 0; i < SIZE; i++) {
        if (data[i] >= threshold) {
            sum += data[i];
        }
    }
    return sum;
}

int sum_branchless(int threshold) {
    int sum = 0;
    for (int i = 0; i < SIZE; i++) {
        int mask = -(data[i] >= threshold);
        sum += data[i] & mask;
    }
    return sum;
}
```

랜덤 조건은 분기 버전에 불리하고 긴 run으로 정렬된 조건은 predictor에 유리하다는 방향은 예상할 수 있다. 그러나 compiler가 두 루프를 같은 SIMD mask 코드로 바꾸면 차이가 사라진다. branchless 식도 항상 모든 대안보다 빠른 것이 아니며 추가 산술, dependency와 vectorization 가능성을 함께 본다.

### Binary Search: 분기의 함정

```cpp
// 전통적 Binary Search
int binary_search(int* arr, int n, int target) {
    int left = 0, right = n - 1;
    while (left <= right) {
        int mid = (left + right) / 2;
        if (arr[mid] == target)
            return mid;
        else if (arr[mid] < target)  // 예측 불가능
            left = mid + 1;
        else
            right = mid - 1;
    }
    return -1;
}

// Binary lifting 형태. 모든 n에서 마지막 <= target 위치를 찾는다.
int binary_search_branch_reduced(const int* arr, int n, int target) {
    if (n <= 0) return -1;

    const size_t count = static_cast<size_t>(n);
    size_t prefix = 0;  // target 이하인 prefix 길이

    for (size_t step = std::bit_floor(count); step != 0; step >>= 1) {
        const size_t next = prefix + step;
        if (next <= count && arr[next - 1] <= target) {
            prefix = next;
        }
    }

    return prefix != 0 && arr[prefix - 1] == target
         ? static_cast<int>(prefix - 1) : -1;
}
```

이 함수에도 C++ `if`가 있으므로 실제 branchless 기계어가 된다는 보장은 없다. `std::bit_floor`를 사용한 binary lifting은 power-of-two가 아닌 크기도 처리하지만 중복 키가 있으면 마지막 일치 위치를 반환한다. binary search는 비교 횟수가 적어도 각 접근이 데이터 의존적이라 cache miss가 지배할 수 있다. 생성 어셈블리와 함께 Eytzinger layout, prefetch, batched search를 같은 hit/miss 질의 분포로 비교한다.

### likely/unlikely의 실전 효과

Linux Kernel의 사례:

```c
// include/linux/compiler.h
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// fs/namei.c
if (unlikely(!inode->i_op->permission)) {
    return -EACCES;
}
```

커널의 `unlikely`는 드문 오류 경로라는 정적 정보를 compiler에 주어 block 배치와 최적화 판단을 돕는다. 실제 효과는 compiler·PGO·함수 크기에 따라 달라지며 attribute 하나가 일정 비율의 개선을 보장하지 않는다.

## 8. 하드웨어별 차이를 다루는 법

BTB 계층, history 길이, 간접 분기 predictor와 return stack의 세부 크기는 CPU 세대마다 다르고 일부는 공식 문서가 아니라 microbenchmark로 추정된다. “Intel”, “AMD”, “ARM” 한 행으로 고정하면 같은 회사의 다음 세대에서 바로 틀린다. 제품별 optimization manual, performance counter와 공개 microarchitecture 분석을 날짜·모델과 함께 사용한다.

`__builtin_expect_with_probability` 같은 확률 힌트는 지원 compiler의 중간 표현에 edge probability를 전달할 수 있다.

```c
if (__builtin_expect_with_probability(rare_case, 0, 0.01)) {
    // 1% 확률임을 명시
}
```

이 역시 hardware predictor를 설정하는 API가 아니며 compiler가 지원하지 않거나 PGO 정보가 있으면 효과가 달라질 수 있다.

## 9. 실전 체크리스트

### 분기 최적화가 필요한 상황

1. **프로파일러가 분기 미스를 보고할 때**
 ```bash
 perf stat -e branches,branch-misses,cycles,instructions ./program
 ```

전체 miss rate의 보편적인 위험 임계값은 없다. 총 cycles에서 해당 분기들이 차지하는 손실과 최적화 가능한 hot instruction 주소를 찾는다.

2. **Hot loop 내부의 데이터 의존적 분기**
 ```c
 for (int i = 0; i < HUGE_SIZE; i++) {
 if (unpredictable_condition(data[i])) {
 //...
 }
 }
 ```

3. **예측 불가능한 입력을 처리할 때** (사용자 입력, 네트워크 패킷, 압축/암호화 데이터)

### 최적화 우선순위

1. **알고리즘 변경** (가장 큰 효과)
 - 분기가 적은 알고리즘 선택
 - 데이터 정렬로 예측 가능성 높이기

2. **likely/unlikely 힌트** (중간 효과)
 - 명확한 경우에만 사용
 - 프로파일 데이터 기반

3. **Branchless 기법** (까다로움)
 - 간단한 경우에만
 - 벤치마크 필수

4. **PGO** (장기 투자)
 - 릴리즈 빌드 프로세스에 통합
 - 대표적 워크로드 확보

### 피해야 할 함정

**함정 1: 과도한 최적화**

```c
// 나쁜 예: 가독성을 희생
int process(int x) {
    return ((x >> 31) ^ x) - (x >> 31);  // abs()의 비트 해킹 버전
}

// 좋은 예: 명확함
int process(int x) {
    return abs(x);  // 컴파일러가 알아서 최적화
}
```

**함정 2: 잘못된 힌트**

```c
// 위험: 실제로는 50/50인데
if (unlikely(coin_flip())) {
    // ...
}
```

**함정 3: 시대착오적 최적화**

```c
// 2000년대에는 유용했지만 지금은?
int divide_by_2(int x) {
    return x >> 1;  // 컴파일러가 x / 2를 이미 이렇게 바꿈
}
```

현대 컴파일러는 이미 충분히 똑똑하다.

## 10. 미래: 분기 예측의 진화

### 머신러닝 기반 예측기

최신 연구는 LSTM 기반 예측기를 탐구한다. 더 긴 히스토리(수백 개 분기), 복잡한 패턴 인식이 가능하지만, 하드웨어 복잡도와 전력 소비가 문제다.

학계 연구들은 신경망 기반 예측기가 기존 TAGE 계열보다 높은 정확도를 달성할 수 있음을 보고하지만, 실리콘 면적과 전력, 예측 지연이 수 배로 늘어난다는 비용도 함께 보고한다. 상용화는 아직 멀었다.

### Software-Guided Prediction

프로그래머가 더 많은 정보를 제공하는 방향:

```c
// 미래의 C++ (가상)?
[[predict(pattern={T, T, N, T, T, N})]]
for (int i = 0; i < n; i++) {
    if (condition[i]) {
        // ...
    }
}
```

패턴을 명시적으로 알려주면 예측기가 학습한다.

### 분기 없는 아키텍처

NVIDIA GPU와 같은 SIMT(Single Instruction Multiple Threads) 아키텍처는 분기를 다르게 처리한다.

- 모든 경로를 실행하고 마스킹
- 분기 예측이 필요 없음
- 대신 Divergence 비용 발생

CPU가 이 방향으로 갈까? 범용 워크로드에서는 분기가 필수적이다.


---

## 결론

분기 예측은 현대 CPU의 숨겨진 마법이다. 프로그래머가 의식하지 않아도 작동하지만, 이해하고 활용하면 놀라운 성능 향상을 얻을 수 있다.

핵심 교훈은 다섯 가지다. 추측하지 말고 perf로 측정한다. 데이터 정렬, 패턴화로 예측 가능하게 만든다. 힌트는 확신이 있을 때만 준다. Branchless는 만능이 아니며 상황에 따라 선택한다. PGO는 강력하며 릴리즈 빌드에 투자할 가치가 있다.

Mike Acton이 반복해서 강조하는 원칙을 다시 새긴다. 갖고 싶은 문제가 아니라 실제로 가진 문제를 풀어라. 분기 예측이 실제 성능 병목이 맞는지 먼저 확인하고, 그때 적절한 도구를 사용한다.

## 더 읽을거리

### 공식 문서 및 매뉴얼

- Intel. "Intel 64 and IA-32 Architectures Optimization Reference Manual". https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
- AMD. "Software Optimization Guide for AMD Family 19h Processors". https://www.amd.com/en/support/tech-docs
- ARM. "Arm Cortex-A78 Core Technical Reference Manual". https://developer.arm.com/documentation/

### 학술 논문

- Yeh, T.-Y., & Patt, Y. N. (1993). "A Comparison of Dynamic Branch Predictors that use Two Levels of Branch History". ISCA.
- Jiménez, D. A., & Lin, C. (2001). "Dynamic Branch Prediction with Perceptrons". HPCA.
- Seznec, A. (2007). ["The L-TAGE Branch Predictor"](https://jilp.org/vol9/). Journal of Instruction-Level Parallelism, Vol. 9.
- Ball, T., & Larus, J. R. (1993). "Branch Prediction for Free". PLDI.

### 벤치마크 및 분석

- Fog, A. (2023). "The microarchitecture of Intel, AMD, and VIA CPUs". https://www.agner.org/optimize/
- Fog, A. (2023). "Optimizing software in C++". https://www.agner.org/optimize/optimizing_cpp.pdf
- Lemire, D. (2019). "Faster sorted array unions by reducing branches". https://lemire.me/blog/
- Preshing, J. (2012). "A Look Back at Single-Threaded CPU Performance". https://preshing.com/

### 도구와 관찰

- [Linux perf](https://perf.wiki.kernel.org/)
- [Intel VTune Profiler](https://www.intel.com/content/www/us/en/developer/tools/oneapi/vtune-profiler.html)
- [AMD μProf](https://www.amd.com/en/developer/uprof.html)
- [Compiler Explorer (Godbolt)](https://godbolt.org/)
- [LLVM-MCA](https://llvm.org/docs/CommandGuide/llvm-mca.html)

### 블로그 글 및 실전 사례

- Rohou, Swamy, Seznec. "Branch Prediction and the Performance of Interpreters" (CGO 2015).
- Stack Overflow. ["Why is processing a sorted array faster than processing an unsorted array?"](https://stackoverflow.com/q/11227809) — Mysticial의 고전 답변.
- Michael Abrash. *Graphics Programming Black Book* — 저수준 최적화의 고전.

### 언어 및 컴파일러 문서

- [GCC: Built-in Functions for Branch Prediction](https://gcc.gnu.org/onlinedocs/)
- [Clang: Profile-Guided Optimization](https://clang.llvm.org/docs/)
- [MSVC: Profile-Guided Optimizations](https://docs.microsoft.com/en-us/cpp/)
- [C++20 Likelihood attributes](https://en.cppreference.com/)

### 비디오 강의

- Chandler Carruth. "Tuning C++: Benchmarks, and CPUs, and Compilers! Oh My!". CppCon 2015. https://www.youtube.com/watch?v=nXaxk27zwlk
- Matt Godbolt. "What Has My Compiler Done for Me Lately?". CppCon 2017. https://www.youtube.com/watch?v=bSkpMdDe4g4

### 참고 서적

- Hennessy, J. L., & Patterson, D. A. "Computer Architecture: A Quantitative Approach", 6th ed.
- Fog, A. "Optimizing subroutines in assembly language".
- Warren, H. S. "Hacker's Delight", 2nd ed.
