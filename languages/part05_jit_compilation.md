## 들어가며

프로그램을 실행하는 방법은 크게 두 가지로 나뉜다. 미리 기계어로 번역해두는 AOT 컴파일과, 실행 상태를 해석하는 인터프리터다. 그 중간에 자리한 기법이 JIT(Just-In-Time) 컴파일이다. 실행 중에 기계어를 생성하고, 만든 기계어를 즉시 실행한다. 런타임에 코드를 만든다는 사실은 단순한 기술적 호기심에 그치지 않는다. 메모리에 실행 가능한 바이트를 쓴다는 것은 운영체제의 보호 모델, CPU 캐시 일관성, 보안 모델 모두와 부딪히는 일이다.

이 글에서는 JIT의 동기와 구조를 인터프리터와 AOT 컴파일러의 비교에서부터 시작한다. 그 다음 메모리에 코드를 쓰는 기초 메커니즘, W^X 보호와 mprotect, 간단한 수식 컴파일러의 실제 구현을 거쳐, tracing JIT와 method JIT의 트레이드오프, inline caching, deoptimization, 그리고 LuaJIT, V8,.NET 같은 실전 시스템의 구현 차이까지 단계적으로 살펴본다.


---

## 목차
1. [인터프리터 vs 컴파일러: 그 사이의 JIT](#1-인터프리터-vs-컴파일러-그-사이의-jit)
2. [기계어 생성: 메모리에 코드를 쓴다](#2-기계어-생성-메모리에-코드를-쓴다)
3. [W^X 보호와 mprotect](#3-wx-보호와-mprotect)
4. [기본 JIT: 간단한 수식 컴파일러](#4-기본-jit-간단한-수식-컴파일러)
5. [Tracing JIT vs Method JIT](#5-tracing-jit-vs-method-jit)
6. [Inline Caching: 동적 타입의 최적화](#6-inline-caching-동적-타입의-최적화)
7. [디옵티마이제이션: 추측이 틀렸을 때](#7-디옵티마이제이션-추측이-틀렸을-때)
8. [실전: LuaJIT, V8,.NET의 구현](#8-실전-luajit-v8net의-구현)


---

## 1. 인터프리터 vs 컴파일러: 그 사이의 JIT

### 전통적인 이분법

프로그래밍 언어 구현은 오랫동안 두 진영으로 나뉘어 있었다:

**인터프리터**: AST나 바이트코드 같은 프로그램 표현을 해석하며 실행
- 빠른 시작 시간
- 명령 해독·디스패치 비용이 들 수 있음 (비용은 표현과 구현에 따라 달라짐)
- 예: 초기 Python, Ruby, JavaScript(V8 이전)

**AOT 컴파일러**: 실행 전에 기계어로 완전히 번역
- 긴 컴파일 시간
- 런타임 명령 해독을 줄이고 정적 최적화를 적용할 수 있음 (실제 비용은 생성 코드와 workload에 따라 달라짐)
- 예: C, C++, Rust, Go

1960년대 McCarthy의 LISP 인터프리터부터 시작된 이 이분법은, 1984년 Deutsch와 Schiffman의 "Efficient Implementation of the Smalltalk-80 System" 논문에서 무너지기 시작했다.

### JIT의 등장: SELF의 혁명

1987년 OOPSLA에서 Stanford의 David Ungar와 Xerox PARC의 Randall Smith가 발표한 "SELF: The Power of Simplicity" 논문은 프로그래밍 언어 구현의 패러다임을 바꾸기 시작했다. 이후 1991~1994년에 걸쳐 SELF 컴파일러(SELF-91, SELF-93)가 dynamic deoptimization과 type feedback을 도입하면서 적응적 최적화의 기초가 완성된다. SELF 언어는 다음과 같은 특징을 가진다.

- 동적 타입 (모든 것이 객체)
- 프로토타입 기반 (클래스 없음)
- Pure OOP (모든 연산이 메시지 전달)

이론상 인터프리터로는 절망적으로 느렸어야 했다. 하지만 SELF-93은 이런 극단적 동적성을 가지고도 최적화 C 코드의 절반 수준 성능에 도달했다(Hölzle, "Adaptive Optimization for SELF", PhD thesis, Stanford, 1994). 비밀은 **adaptive optimization**에 있었다:

```text
시작: 빠른 컴파일러가 생성한 초기 코드로 실행
  ↓
hot spot 발견 (자주 실행되는 코드)
  ↓
최적화 컴파일러로 재컴파일 → 특수화된 네이티브 기계어
  ↓
이후 실행은 네이티브 코드
```

이 아이디어는 Sun의 HotSpot JVM(1999), V8(2008), LuaJIT 2.0(2009), PyPy(2007)의 기반이 되었다.

### Tiered Compilation: 현대의 표준

현대 JIT은 여러 단계(tier)로 나뉜다. V8(2024 기준)의 4-tier 파이프라인이 대표적이다.

**Tier 0**: 바이트코드 인터프리터
- 즉시 실행 가능
- 프로파일링 수집 (어떤 코드가 hot한가?)
- 예: **V8 Ignition** (인터프리터, baseline compiler가 아니다)

**Tier 1**: 빠른 baseline JIT
- 인터프리터를 떠나 바이트코드를 직선적으로 기계어로 옮긴다
- 최적화는 거의 하지 않고, 컴파일 속도와 코드 시작 지연을 우선
- 예: **V8 Sparkplug** (2021 도입, 바이트코드 → 비최적화 기계어)

**Tier 2**: 중간 단계 최적화 JIT
- 타입 피드백 기반의 가벼운 SSA 최적화
- 컴파일 비용은 적당, 실행 속도는 baseline보다 크게 개선
- 예: **V8 Maglev** (2023 도입)

**Tier 3**: 공격적 최적화 JIT
- 인라이닝, 레지스터 할당, 루프 최적화, escape analysis 등 무거운 최적화
- 컴파일 시간 매우 김, 실행 속도 최고
- 예: **V8 TurboFan**,.NET Tier1

V8의 파이프라인 변화는 시작 지연, 메모리 사용, 최종 실행 속도 사이의 균형을 계속 바꾼 결과다.

- Ignition + TurboFan(2017): 당시 V8 팀이 사용한 모바일 장치와 웹 워크로드에서 메모리와 시작 컴파일 비용을 크게 줄인 재설계였다. 발표 수치는 특정 버전·벤치마크의 결과이지 현재 모든 JavaScript 프로그램의 고정 비율이 아니다.
- Sparkplug 추가(2021): TurboFan 최적화를 기다리는 동안 인터프리터보다 빠르게 실행할 baseline 계층을 추가했다.
- Maglev 추가(2023): Sparkplug와 TurboFan 사이에 컴파일 비용이 더 낮은 최적화 계층을 두었다.

이 흐름은 "인터프리터 하나 + baseline 하나 + 최적화 하나"의 3-tier 모델이 더 세분화되는 과정이다.

### 왜 JIT이 필요한가?

정적 컴파일러가 알 수 없는 정보를 JIT은 안다:

1. **실제 타입 정보**: JavaScript `x + y`에서 x와 y가 정수인지, 문자열인지, 객체인지
2. **실제 실행 경로**: 분기문 중 어느 쪽이 90% 실행되는지
3. **실제 호출 대상**: 다형성 메서드 호출에서 실제로는 한 타입만 오는지

이 정보로 **speculative optimization**이 가능하다. "지금까지는 이랬으니, 앞으로도 그럴 것"이라고 가정하고 최적화한다.

## 2. 기계어 생성: 메모리에 코드를 쓴다

### 기계어의 본질

CPU는 텍스트를 읽지 못한다. 오직 바이트 시퀀스만 이해한다. x86-64에서 `1 + 2`를 계산하는 기계어:

```assembly
; 어셈블리 (인간이 읽는 형식)
mov eax, 1
add eax, 2
ret

; 기계어 (CPU가 실행하는 형식)
B8 01 00 00 00    ; mov eax, 1
83 C0 02          ; add eax, 2
C3                ; ret
```

JIT 컴파일러는 이 바이트 시퀀스를 **런타임에 메모리에 쓴다**:

```c
// 개념적 코드
unsigned char code[] = {
    0xB8, 0x01, 0x00, 0x00, 0x00,  // mov eax, 1
    0x83, 0xC0, 0x02,              // add eax, 2
    0xC3                           // ret
};

// 이 메모리를 함수로 실행!
int (*func)() = (int(*)())code;
int result = func();  // result = 3
```

간단해 보이지만, 여기에는 두 가지 거대한 문제가 있다.

### 문제 1: 데이터 vs 코드

폰 노이만 아키텍처에서 코드와 데이터는 같은 메모리에 있다. 하지만 현대 보안 메커니즘은 이 둘을 엄격히 구분한다:

- **읽기 가능한 메모리** (R): 데이터 읽기
- **쓰기 가능한 메모리** (W): 데이터 쓰기
- **실행 가능한 메모리** (X): 기계어 실행

**W^X 정책**: 쓰기 가능하면서 동시에 실행 가능한 메모리를 금지한다. 이것은 보안 취약점(코드 인젝션 공격)을 막기 위한 핵심 방어선이다.

일반 `malloc`으로 할당한 메모리는:
```text
[ R | W |   ]  ← 읽기/쓰기 가능, 실행 불가
```

기계어를 실행하려면:
```text
[ R |   | X ]  ← 읽기/실행 가능, 쓰기 불가
```

**딜레마**: 기계어를 쓰려면 W가 필요하고, 실행하려면 X가 필요한데, 동시에는 불가능하다!

### 해법: mprotect와 VirtualAlloc

운영체제는 메모리 권한을 동적으로 변경하는 시스템 콜을 제공한다:

**POSIX (Linux/macOS):**
```c
#include <sys/mman.h>
#include <string.h>

// 1. 실행 가능한 메모리 할당
void* code = mmap(
    NULL,                      // 주소는 OS가 선택
    4096,                      // 4KB 페이지
    PROT_READ | PROT_WRITE,    // 읽기/쓰기 권한
    MAP_PRIVATE | MAP_ANONYMOUS, // 파일 없음
    -1, 0
);
if (code == MAP_FAILED) {
    perror("mmap");
    exit(EXIT_FAILURE);   // 할당 실패이므로 memcpy/mprotect로 진행하지 않는다
}

// 2. 기계어 쓰기
unsigned char machine_code[] = {
    0xB8, 0x01, 0x00, 0x00, 0x00,  // mov eax, 1
    0x83, 0xC0, 0x02,              // add eax, 2
    0xC3                           // ret
};
memcpy(code, machine_code, sizeof(machine_code));

// 3. 권한 변경: 쓰기 제거, 실행 추가
if (mprotect(code, 4096, PROT_READ | PROT_EXEC) != 0) {
    perror("mprotect");
    munmap(code, 4096);   // 권한 전환이 실패하면 매핑을 해제하고 진행하지 않는다
    exit(EXIT_FAILURE);
}

// 4. 함수로 실행
int (*func)() = (int(*)())code;
printf("Result: %d\n", func());  // 3

// 5. 해제
munmap(code, 4096);
```

**Windows:**
```c
#include <windows.h>

// 1. 실행 가능한 메모리 할당
LPVOID code = VirtualAlloc(
    NULL,
    4096,
    MEM_COMMIT | MEM_RESERVE,
    PAGE_READWRITE
);
if (!code) {
    exit(EXIT_FAILURE);   // VirtualAlloc 실패이므로 이후 단계로 진행하지 않는다
}

// 2. 기계어 쓰기
unsigned char machine_code[] = { /* ... */ };
memcpy(code, machine_code, sizeof(machine_code));

// 3. 권한 변경 (RW → RX)
DWORD old_protect;
if (!VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &old_protect)) {
    VirtualFree(code, 0, MEM_RELEASE);   // 권한 전환 실패 시 매핑을 해제한다
    exit(EXIT_FAILURE);
}

// 4. 명령어 캐시 플러시
// MSDN: "Applications should call FlushInstructionCache if they
// generate or modify code in memory. The CPU cannot detect the
// change, and may execute the old code it cached."
FlushInstructionCache(GetCurrentProcess(), code, 4096);

// 5. 실행
int (*func)() = (int(*)())code;
printf("Result: %d\n", func());

// 6. 해제
VirtualFree(code, 0, MEM_RELEASE);
```

### 문제 2: 명령어 캐시 일관성

현대 CPU는 **명령어 캐시(I-cache)**와 **데이터 캐시(D-cache)**를 분리한다 (Harvard 아키텍처). 메모리에 기계어를 쓰면:

1. D-cache에만 업데이트됨
2. I-cache는 여전히 이전 내용 (또는 비어있음)
3. CPU가 실행하려 하면 I-cache 읽음 → **잘못된 코드 실행!**

**해법: 캐시 플러시**

```c
// ARM
__builtin___clear_cache(code, code + size);

// x86-64
// 대부분 불필요 (self-modifying code에 대한 하드웨어 지원)
// 하지만 멀티코어에서는 필요할 수 있음
```

x86-64는 SMC(Self-Modifying Code) 탐지 하드웨어가 있어 대부분 자동으로 처리된다. 하지만 ARM, RISC-V 등은 명시적 플러시가 필수다.

### 실전 예제: 간단한 수식 JIT

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// JIT: "x + constant" 함수를 생성
typedef int (*jit_func)(int);

jit_func compile_add(int constant) {
    // Linux x86-64 System V ABI: int add(int x) { return x + constant; }
    //   mov eax, edi       ; 첫 인자(x)를 eax로
    //   add eax, constant  ; constant 더하기
    //   ret

    unsigned char template[] = {
        0x89, 0xF8,              // mov eax, edi
        0x05, 0x00, 0x00, 0x00, 0x00,  // add eax, imm32
        0xC3                     // ret
    };

    // imm32를 호스트의 little-endian 표현으로 삽입한다.
    // Linux x86-64 System V ABI 호스트를 전제하며, 정렬·별칭 UB를 피하려고 memcpy를 쓴다.
    // Windows x64는 첫 정수 인자가 rcx라 이 레지스터 규약을 그대로 쓸 수 없다.
    memcpy(template + 3, &constant, sizeof(constant));

    // 실행 가능한 메모리 할당
    void* code = mmap(NULL, 4096,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      -1, 0);
    if (code == MAP_FAILED)
        return NULL;

    memcpy(code, template, sizeof(template));
    if (mprotect(code, 4096, PROT_READ | PROT_EXEC) != 0) {
        munmap(code, 4096);
        return NULL;
    }

    return (jit_func)code;
}

int main() {
    jit_func add5 = compile_add(5);
    jit_func add100 = compile_add(100);

    if (!add5 || !add100)
        return 1;

    printf("add5(10) = %d\n", add5(10));      // 15
    printf("add100(23) = %d\n", add100(23));  // 123

    munmap((void*)add5, 4096);
    munmap((void*)add100, 4096);
    return 0;
}
```

이 16줄의 코드가 JIT의 핵심이다. 런타임에 기계어를 생성하고 실행한다.

## 3. W^X 보호와 mprotect

### DEP/NX Bit의 역사

**NX가 보편화되기 전의 주류 32비트 x86 환경**에서는 페이지 단위로 데이터의 실행을 막는 보호가 부족했다. 이를 모든 과거 CPU에 일반화할 수는 없지만, 당시 스택에 주입한 코드를 실행하는 공격은 큰 문제였다:

```c
// 전형적인 버퍼 오버플로우 공격
char buffer[100];
strcpy(buffer, user_input);  // 공격자가 200바이트 전송
// 스택 오버플로우 → 리턴 주소 덮어씀
// → 공격자가 주입한 shellcode 실행
```

스택에 악성 기계어를 쓰고, 리턴 주소를 조작하여 그곳으로 점프시키는 공격이 난무했다.

**2003~2004년**: AMD가 AMD64에 NX bit(No-eXecute)를 도입하고(2003, Opteron/Athlon 64), Intel이 XD bit로 뒤따랐으며, Windows XP SP2(2004)가 이를 DEP(Data Execution Prevention)로 활성화했다:

- 페이지 테이블의 각 엔트리에 1비트 추가
- NX=1이면 해당 페이지 실행 시 CPU 예외 발생
- OS가 스택과 힙을 NX=1로 설정

DEP가 적용되는 환경에서는 일반 `malloc` 메모리를 그대로 실행하던 JIT 구현을 바꿔야 했다. 실행 권한을 명시적으로 요청하던 JIT까지 모두 작동을 멈춘 것은 아니다.

### mprotect의 작동 원리

```c
int mprotect(void *addr, size_t len, int prot);
```

- **addr**: OS가 요구하는 페이지 크기에 정렬된 주소 (4KB로 고정되지 않음)
- **len**: 보호할 크기
- **prot**: `PROT_READ | PROT_WRITE | PROT_EXEC`의 조합

운영체제는 **페이지 테이블**을 수정한다:

```text
가상 주소 0x1000:
페이지 테이블 엔트리: [물리 주소 | Present | R/W | U/S | ... | NX]
                                   ↑         ↑                 ↑
mprotect가 이 비트들을 변경
```

**성능 비용**: `mprotect`는 시스템 콜이며, TLB(Translation Lookaside Buffer) 플러시를 유발한다. 시스템 콜 진입과 TLB 무효화(멀티코어에서는 TLB shootdown IPI 포함) 때문에 수백~수천 사이클 규모의 비용이 든다.

### JIT에서의 전략

**전략 1: 쓰기 후 보호 (Write-then-protect)**
```c
// 초기: RW
void* code = mmap(..., PROT_READ | PROT_WRITE, ...);
write_machine_code(code);
// 보호: RX
mprotect(code, size, PROT_READ | PROT_EXEC);
```

가장 안전하지만, 코드 패치(runtime patching)가 필요할 때마다 mprotect를 왕복해야 한다.

**전략 2: Dual Mapping**
```c
// 같은 물리 메모리에 두 개의 가상 주소 매핑
void* write_view = mmap(..., PROT_READ | PROT_WRITE, ...);
void* exec_view = mmap(..., PROT_READ | PROT_EXEC, ...);
// write_view로 쓰고, exec_view로 실행
```

Apple Silicon macOS에서는 임의의 RWX/dual mapping이 제한되며, JIT은 `mmap(MAP_JIT)`으로 할당한 영역을 `pthread_jit_write_protect_np`로 스레드별 W↔X 전환하며 써야 한다. iOS는 일반 게임 앱처럼 entitlement 없는 배포에서 third-party JIT을 강하게 제한하며, JavaScriptCore 같은 시스템 프레임워크는 예외다. 다만 Apple은 EU의 iOS 17.4+/iPadOS 18+에서 승인된 alternative browser engine 개발자에게는 JIT 기능을 제공하므로, 이를 임의 앱의 일반 허용으로 일반화하지 않는다(2026-09-21 정책 기준).

**전략 3: 플랫폼 메커니즘 (macOS MAP_JIT + Entitlement)**
```objective-c
// Entitlements.plist
<key>com.apple.security.cs.allow-jit</key>
<true/>
```

macOS 11+ (Apple Silicon)에서는 entitlement 없이는 W^X를 우회할 수 없다.

### 보안 고려사항: JIT Spraying

**공격 시나리오**: 공격자가 JIT 컴파일러를 악용하여 원하는 기계어 패턴을 생성하도록 유도:

```javascript
// JavaScript로 NOP 슬라이드 생성 (Blazakis, 2010)
var spray = [];
for (var i = 0; i < 1000; i++) {
    spray[i] = x ^ 0x3C909090;  // 엔진이 xor eax, 0x3C909090 으로 컴파일
}
```

JIT이 `x ^ 0x3C909090`을 `xor eax, 0x3C909090`(바이트열 `35 90 90 90 3C`)으로 컴파일하면, 한 바이트 어긋난 주소로 진입할 경우 상수 안의 `0x90`(NOP)들이 슬라이드로 동작한다. 이를 다른 익스플로잇 프리미티브와 결합하면 공격이 가능하다.

**방어책**:
1. **Constant Blinding**: 상수를 직접 삽입하지 않고 XOR로 난독화
2. **Guard Pages**: JIT 코드 영역 사이에 실행 불가 페이지 삽입
3. **Randomization**: JIT 코드 레이아웃 무작위화

Blazakis의 원 논문(BlackHat DC 2010)과 Niu & Tan의 RockJIT(CCS 2014) 같은 JIT spraying 방어 문헌이 이러한 기법들을 다룬다. V8 블로그 "Untrusted code mitigations"(2018)는 별개 위협인 Spectre 부채널의 완화(인덱스 마스킹, 프로세스 격리)를 다룬다.

## 4. 기본 JIT: 간단한 수식 컴파일러

### 목표: 수식 → 기계어

간단한 수식 언어를 JIT 컴파일하자:

```text
Input:  "x * 2 + 3"
Output: 기계어 함수 int f(int x) { return x * 2 + 3; }
```

### 1단계: 파싱 → AST

```c
typedef enum { ADD, MUL, NUM, VAR } NodeType;

typedef struct Node {
    NodeType type;
    int value;           // NUM일 때
    struct Node* left;
    struct Node* right;
} Node;

// "x * 2 + 3" → AST
//       +
//      / \
//     *   3
//    / \
//   x   2
```

### 2단계: 코드 생성

이 예제는 Linux x86-64의 System V ABI를 전제한다. 같은 x86-64라도 Windows x64의 첫 정수 인자는 `rcx`이므로 아래 레지스터 규약을 그대로 옮기지 않는다. C의 `int` 수식과 결과를 비교할 때는 모든 중간 결과가 32비트 부호 있는 정수 범위 안에 있다고 가정한다. 생성된 `add`·`imul`은 하위 32비트를 남기므로, 범위를 넘는 입력까지 허용하려면 언어의 wrap 또는 overflow 검사 규칙을 따로 정해야 한다.

x86-64 System V calling convention:
- 첫 번째 인자: `rdi` (32비트는 `edi`)
- 반환값: `rax` (32비트는 `eax`)

**레지스터 머신 접근**:
```c
typedef struct {
    unsigned char* base;
    size_t used;
    size_t capacity;
} Emitter;

#define MAX_AST_DEPTH 256

// subtraction 기반 검사: used > capacity이거나 n > capacity - used이면 실패
static bool emit_bytes(Emitter* e, const unsigned char* bytes, size_t n) {
    if (e->used > e->capacity || n > e->capacity - e->used)
        return false;
    memcpy(e->base + e->used, bytes, n);
    e->used += n;
    return true;
}

bool codegen(Node* node, Emitter* e, int depth) {
    if (depth > MAX_AST_DEPTH)
        return false;

    switch (node->type) {
    case NUM:
        // mov eax, immediate
        return emit_mov_eax_imm(e, node->value);

    case VAR:
        // mov eax, edi (x는 첫 인자)
        return emit_mov_eax_edi(e);

    case MUL:
        if (!codegen(node->left, e, depth + 1)) return false;   // 좌변 → eax
        if (!emit_push_eax(e)) return false;   // 저장
        if (!codegen(node->right, e, depth + 1)) return false;   // 우변 → eax
        if (!emit_pop_ecx(e)) return false;    // 좌변 → ecx
        return emit_imul_eax_ecx(e);           // eax *= ecx

    case ADD:
        if (!codegen(node->left, e, depth + 1)) return false;   // 좌변 → eax
        if (!emit_push_eax(e)) return false;
        if (!codegen(node->right, e, depth + 1)) return false;   // 우변 → eax
        if (!emit_pop_ecx(e)) return false;
        return emit_add_eax_ecx(e);
    }
    return false;
}
```

### x86-64 기계어 인코딩

```c
bool emit_mov_eax_imm(Emitter* e, int imm) {
    unsigned char op = 0xB8;  // opcode
    if (!emit_bytes(e, &op, 1)) return false;
    unsigned char bytes[4];
    memcpy(bytes, &imm, sizeof(imm));  // 정렬되지 않은 주소에 int lvalue로 쓰면 UB다
    return emit_bytes(e, bytes, sizeof(bytes));
}

bool emit_mov_eax_edi(Emitter* e) {
    unsigned char bytes[] = { 0x89, 0xF8 };  // MOV r/m32, r32; ModR/M: edi → eax
    return emit_bytes(e, bytes, sizeof(bytes));
}

bool emit_push_eax(Emitter* e) {
    unsigned char bytes[] = { 0x50 };  // PUSH rax
    return emit_bytes(e, bytes, sizeof(bytes));
}

bool emit_pop_ecx(Emitter* e) {
    unsigned char bytes[] = { 0x59 };  // POP rcx
    return emit_bytes(e, bytes, sizeof(bytes));
}

bool emit_imul_eax_ecx(Emitter* e) {
    unsigned char bytes[] = { 0x0F, 0xAF, 0xC1 };  // two-byte opcode; ModR/M: ecx
    return emit_bytes(e, bytes, sizeof(bytes));
}

bool emit_add_eax_ecx(Emitter* e) {
    unsigned char bytes[] = { 0x01, 0xC8 };  // ADD r/m32, r32; ModR/M: ecx → eax
    return emit_bytes(e, bytes, sizeof(bytes));
}

bool emit_ret(Emitter* e) {
    unsigned char bytes[] = { 0xC3 };
    return emit_bytes(e, bytes, sizeof(bytes));
}
```

### 3단계: 실행

```c
typedef int (*jit_func)(int);

jit_func compile_expr(Node* ast) {
    void* code = mmap(NULL, 4096,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      -1, 0);
    if (code == MAP_FAILED)
        return NULL;

    Emitter e = { (unsigned char*)code, 0, 4096 };
    if (!codegen(ast, &e, 0) || !emit_ret(&e)) {
        // 경계를 넘기 전에 중단하고 부분 생성 코드는 실행하지 않는다.
        munmap(code, 4096);
        return NULL;
    }

    if (mprotect(code, 4096, PROT_READ | PROT_EXEC) != 0) {
        munmap(code, 4096);
        return NULL;
    }
    return (jit_func)code;
}

int main() {
    // AST for "x * 2 + 3"
    Node* ast = make_add(
        make_mul(make_var(), make_num(2)),
        make_num(3)
    );

    jit_func f = compile_expr(ast);
    if (!f) {
        fprintf(stderr, "compile failed\n");
        return 1;
    }
    printf("f(5) = %d\n", f(5));   // 13
    printf("f(10) = %d\n", f(10)); // 23
}
```

이것이 JIT의 기본 골격이다. 실전 JIT은 여기에 최적화, 레지스터 할당, 에러 처리를 더한다.

## 5. Tracing JIT vs Method JIT

JIT 컴파일러는 무엇을 컴파일 단위로 삼을까? 크게 두 가지 접근이 있다.

### Method JIT: 함수 단위 컴파일

Java HotSpot,.NET, V8 TurboFan의 접근:

```java
int compute(int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        if (isPrime(i)) {
            sum += i;
        }
    }
    return sum;
}
```

**컴파일 단위**: 전체 메서드 `compute`
- 프로파일링: "이 메서드가 1000번 호출됨"
- 컴파일: 메서드 전체를 최적화된 기계어로
- 장점: 함수 경계가 명확, 인라이닝 용이
- 단점: 큰 함수에서 cold code도 컴파일

### Tracing JIT: 실행 경로 단위 컴파일

LuaJIT, PyPy, TraceMonkey(구 Firefox)의 접근:

**컴파일 단위**: 자주 실행되는 "trace" (hot loop)

```lua
function compute(n)
    local sum = 0
    for i = 1, n do          -- ← 루프 시작: trace 기록 시작
        if is_prime(i) then
            sum = sum + i
        end
    end                       -- ← 루프 끝: trace 기록 종료
    return sum
end
```

Tracing JIT은 실행을 관찰하다가 "루프가 hot하다"고 판단하면:

1. **Recording phase**: 루프를 한 이터레이션만 실행하며 실제로 실행된 경로를 기록
 ```
 i=2: is_prime(2) → true → sum = sum + 2
 i = i + 1 → 루프 시작점 복귀 → 기록 종료
 ```

2. **Compilation**: 기록된 linear trace를 최적화
 ```
 ; 분기 제거됨 (기록된 경로에서는 true였으므로 그 경로만 남기고 guard 삽입)
 sum = sum + i
 i = i + 1
 if i <= n goto loop
 ```

3. **Guard insertion**: 가정이 깨지면 탈출
 ```
 guard (type(i) == number) ; 타입 가정
 guard (is_prime(i) == true) ; 분기 가정
 ```

**장점**:
- Type specialization: trace 내에서 타입이 확정됨
- 분기 제거: 실제 실행 경로만 컴파일
- 인라이닝 자동: 함수 호출이 trace에 포함되면 자연스럽게 인라인

**단점**:
- Guard overhead: 가정이 자주 깨지면 느려짐
- 코드 폭발: 다양한 경로마다 trace 생성
- 함수 호출이 많으면 trace가 짧아짐

### LuaJIT의 천재성

Mike Pall의 LuaJIT 2.0는 tracing JIT의 정점이다. Pall이 lua-l 메일링 리스트에 올린 "LuaJIT 2.0 intellectual property disclosure and research opportunities"(2009)는 trace를 분기 없는 선형 연산 시퀀스로 기록하고, 이를 SSA IR로 최적화해 기계어로 내리는 설계를 설명한다.

LuaJIT이 수치 루프에서 C에 근접한 성능을 내는 비결:

1. **SSA-based IR**: 중간 표현이 최적화에 이상적
2. **Register allocator**: Linear scan으로 빠르게 레지스터 할당
3. **Trace stitching**: 여러 trace를 연결하여 긴 실행 유지
4. **NYI (Not Yet Implemented) handling**: 컴파일 불가능한 부분은 인터프리터로 폴백

```lua
-- 예: FFI 호출이 섞인 루프
ffi = require("ffi")
ffi.cdef[[
    double sqrt(double x);
]]

local sum = 0
for i = 1, 1e6 do
    sum = sum + ffi.C.sqrt(i)  -- C 함수 직접 호출
end
```

LuaJIT은 FFI 호출의 ABI 경계와 마샬링 비용을 줄이는 방향으로 `sqrt` 호출을 trace에 통합한다. 다만 특정 builtin 인식·intrinsic 치환이 있는 함수의 본체 인라인과 임의 C 함수 인라인은 다르며, 이 예제를 특정 버전에서 machine-code dump로 확인한 것은 아니다.

## 6. Inline Caching: 동적 타입의 최적화

동적 언어에서 메서드 호출은 비싸다:

```javascript
function area(shape) {
    return shape.getArea();  // shape의 타입을 모름
}
```

정적 타입 언어라면:
```c++
// C++: vtable 간접 호출 1회
return shape->getArea();  // vtable[offset] 호출
```

동적 언어라면 (naive):
```c
// 1. shape의 타입 확인 (hash table lookup)
// 2. 타입의 메서드 테이블 검색
// 3. "getArea" 문자열 비교
// 4. 메서드 포인터 획득
// 5. 호출
```

매 호출마다 이 과정을 반복하면 너무 느리다.

### Inline Cache의 발견

1984년 Deutsch와 Schiffman의 "Efficient Implementation of the Smalltalk-80 System" 논문(POPL 1984)에서 등장한 핵심 관찰: **한 호출 지점(call site)에서 받는 리시버의 타입은 거의 변하지 않는다.** 이 지역성을 활용하자.

### Monomorphic Inline Cache

```javascript
function area(shape) {
    return shape.getArea();  // 호출 지점 A
}

area(new Circle(5));
area(new Circle(10));
area(new Circle(15));  // 계속 Circle만 옴
```

**최적화**: 호출 지점에 "이전 타입"과 "메서드 포인터"를 캐시:

```c
// 가상의 런타임 구조: IC는 호출 지점(call-site)마다 하나씩 존재한다.
struct CallSite {
    const char* selector;        // 이 call-site에 고정된 method 이름
    unsigned method_table_epoch; // method table이 바뀔 때 증가
    void* cached_type;           // 마지막으로 본 타입
    void* cached_method;         // 해당 메서드 포인터
};

void* inline_cache_lookup(CallSite* site, Object* obj, const char* name) {
    // Fast path: selector, 타입, method table epoch가 모두 같을 때만 hit
    if (name == site->selector
        && obj->type == site->cached_type
        && site->method_table_epoch == current_method_table_epoch) {
        return site->cached_method;  // ← O(1)
    }

    // Slow path: selector, 타입, epoch 중 하나가 바뀜
    void* method = lookup_method(obj->type, name);  // ← O(log n) or O(n)

    // 캐시 업데이트
    site->selector = name;
    site->method_table_epoch = current_method_table_epoch;
    site->cached_type = obj->type;
    site->cached_method = method;

    return method;
}
```

fast path의 hit 조건에는 이 call-site의 selector, 타입, method table epoch가 모두 들어가야 한다. cache는 호출 지점별로 고정된 selector를 가지며, hot reload처럼 같은 type이라도 method table의 lookup 의미가 바뀌면 epoch 검사나 전역 invalidation으로 기존 항목을 폐기한다. part06의 hot reload 교체 절차에도 이 무효화 단계가 포함되어야 한다.

대부분의 호출 지점이 monomorphic으로 유지된다는 관찰은 Hölzle, Chambers, Ungar의 PIC 논문(ECOOP 1991)부터 반복적으로 보고되어 왔고, 이것이 IC가 실효성을 갖는 근거다.

### Polymorphic Inline Cache (PIC)

실제로는 2~3개 타입이 번갈아 오는 경우가 많다:

```javascript
area(new Circle(5));
area(new Rectangle(3, 4));
area(new Circle(10));
area(new Rectangle(5, 6));
```

**PIC**: 여러 타입을 캐시:

```c
struct PolymorphicCache {
    struct {
        void* type;
        void* method;
    } entries[4];  // 보통 4개까지 캐시
    int count;
};
```

탐색은 linear search (4개뿐이므로 빠름).

### Megamorphic State

위처럼 네 항목만 보관하는 예에서는 더 많은 타입이 오면 **megamorphic**으로 전환할 수 있다. 실제 임계값과 캐시 정책은 엔진·연산 종류·버전에 따라 다르다:

```javascript
// 수십 개의 서로 다른 타입
for (let s of allShapes) {
    s.getArea();  // 호출 지점이 megamorphic
}
```

더 이상 inline cache를 쓰지 않고, global method cache나 hash table로 폴백.

V8은 다음 상태 전이를 거친다:
```text
Uninitialized → Monomorphic → Polymorphic → Megamorphic
```

이 상태는 해당 지점에 여러 타입이 관찰됐다는 피드백이다. 최적화된 실행 상태를 복구하는 deoptimization과 IC 피드백을 초기화하는 일은 별개이며, deopt가 발생한다고 자동으로 monomorphic 상태로 돌아가는 것은 아니다.

### JIT과 IC의 결합

JIT 컴파일러는 IC 상태를 보고 코드를 특화한다:

**Monomorphic일 때**:
```assembly
; guard: obj의 타입이 Circle인가?
cmp [obj], CircleType
jne slow_path

; 직접 호출
call Circle_getArea
ret

slow_path:
    call inline_cache_miss
```

**Polymorphic일 때**:
```assembly
cmp [obj], CircleType
je call_circle
cmp [obj], RectangleType
je call_rect
jmp slow_path

call_circle:
    call Circle_getArea
    jmp done
call_rect:
    call Rectangle_getArea
done:
```

**Megamorphic일 때**: 해당 호출에 generic lookup이나 공용 캐시를 사용할 수 있다. 이 호출 하나의 상태 때문에 함수 전체가 반드시 인터프리터로 돌아가는 것은 아니다.

## 7. 디옵티마이제이션: 추측이 틀렸을 때

JIT은 추측에 기반한다. "이 변수는 항상 정수일 것", "이 분기는 항상 true일 것". 추측이 틀리면?

### Speculation과 Guard

```javascript
function add(a, b) {
    return a + b;
}

add(1, 2);
add(5, 10);
add(100, 200);  // 프로파일링: a, b는 항상 정수
```

JIT 컴파일:
```assembly
; Guard: a가 SMI(Small Integer)인가?
test eax, 1
jnz slow_path

; Guard: b가 SMI인가?
test edx, 1
jnz slow_path

; 원래 operand a는 add가 파괴하므로 느린 경로가 쓸 값을 먼저 보존한다.
; SMI type guard만으로는 (2^30-1)+1처럼 범위를 넘는 값을 막지 못한다.
mov r8, rax        ; 원래 a 보존
add eax, edx
jo  overflow_or_deopt
ret

overflow_or_deopt:
    mov rax, r8    ; 보존한 원래 operand 복원
    ; slow path: wider/number 덧셈 또는 deopt frame state 복구
    jmp slow_path

slow_path:
    call deoptimize
```

SMI type guard는 표현 범위를 보장하지 않으므로 overflow 시에는 원래 operand와 deopt frame state를 복구해야 한다. 보존되지 않은 operand로 slow path를 계산하면 잘못된 값이 나온다.

나중에:
```javascript
add("hello", "world");  // 가정 위반!
```

### Deoptimization의 과정

1. **Guard failure**: 기계어 실행 중 guard 조건 불만족
2. **Bailout**: 최적화된 코드 탈출
3. **State reconstruction**: 인터프리터 상태로 복구
 - 스택 프레임 재구성
 - 변수들을 박싱된 형태로 변환
4. **Fallback**: 인터프리터나 baseline JIT으로 계속 실행
5. **Invalidation**: 최적화된 코드 폐기

Benedikt Meurer의 JSKongress 2017 발표 "A Tale of TurboFan"이 강조하듯, deoptimization은 추측 기반 최적화를 가능하게 만드는 안전망이다. 되돌아갈 길이 보장되기 때문에 컴파일러가 과감하게 가정할 수 있다.

### On-Stack Replacement (OSR)

더 어려운 경우: 루프 **실행 중에** JIT 컴파일이 완료되면?

```javascript
function compute() {
    let sum = 0;
    for (let i = 0; i < 1000000; i++) {
        sum += i;  // ← 루프 중간에 JIT 완료
    }
    return sum;
}
```

**문제**: 이미 인터프리터로 반복문을 50000번 돌렸는데, 나머지 950000번을 JIT 코드로 실행하려면?

**OSR**: 실행 중인 스택 프레임을 그대로 옮긴다:

```text
인터프리터 스택:
  i = 50000
  sum = 1249975000

↓ OSR

JIT 스택:
  i = 50000  (레지스터 rbx)
  sum = 1249975000  (레지스터 rax)
```

HotSpot JVM은 루프 back edge에 카운터를 두고, 임계값을 넘으면 OSR을 수행한다.

### Deoptimization의 비용

경험적으로 알려진 비용 구조는 다음과 같다 (tracing JIT의 guard 설계는 Gal et al., PLDI 2009 참고):

- **Guard overhead**: 추측이 유지되는 동안에도 guard 검사 자체가 소량의 상시 비용을 만든다
- **Deopt cost**: 한 번 deopt하면 상태 재구성에 수천 사이클 단위 비용
- **Recompilation cost**: 다시 최적화 컴파일하려면 그보다 훨씬 큰 비용

이 때문에 **deopt loop**가 치명적이다:

```javascript
function bad(x) {
    return x + 1;
}

for (let i = 0; i < 100000; i++) {
    bad(i);           // 정수 → JIT 컴파일
    if (i % 100 == 0) {
        bad("x");     // 정수 전용 코드라면 deopt를 유발할 수 있음
    }
}
```

이 코드가 매번 deopt와 재컴파일을 반복한다는 뜻은 아니다. 엔진은 문자열 입력까지 피드백에 반영해 더 일반적인 코드를 만들 수 있다. 실제 deopt loop인지는 대상 버전의 최적화·deopt 로그로 확인해야 하며, 잦은 실패에 대한 재최적화 정책도 엔진과 버전에 따라 달라진다.

## 8. 실전: LuaJIT, V8,.NET의 구현

### LuaJIT: Tracing JIT의 걸작

**아키텍처**:
```text
Lua bytecode → Trace Recorder → SSA IR →
  Backend (x64/ARM/PPC/MIPS) → Machine code
```

**핵심 특징**:

1. **Fast interpreter**: 어셈블리로 작성된 인터프리터
 ```assembly
 ; LuaJIT interpreter loop (simplified)
.Ldispatch:
 movzx eax, byte [esi] ; opcode fetch
 jmp [dispatch_table + rax*8]
 ```

2. **Trace compiler**: 루프가 hot해지면:
 ```
 for i=1,1e3 do
 sum = sum + i * i
 end
 ```

 Recorded trace:
 ```
 [1] int SLOAD #1 ; load i
 [2] > int LE i 1000
 [3] int MUL i i
 [4] int ADD sum [3]
 [5] int ADD i 1
 [6] JMP [1]
 ```

 SSA optimization → register allocation → x64:
 ```assembly
 ; 1e3까지의 제곱합은 signed 32-bit 범위에 들어간다.
 ; 원래 1e6 범위라면 sum이 2^31을 넘으므로 wide/number 표현과 overflow·widening 경로가 필요하다.
 cmp ebx, 1000
 jg exit
 mov eax, ebx
 imul eax, ebx
 add esi, eax
 inc ebx
 jmp loop
 ```

3. **FFI integration**: C 구조체를 직접 조작
 ```lua
 ffi.cdef[[
 typedef struct { int x, y; } Point;
 ]]

 local p = ffi.new("Point", 10, 20)
 p.x = p.x + 5 -- 컴파일되면 단순 메모리 접근
 ```

FFI 구조체 접근이 trace에서 단순 로드/스토어로 내려가는 것은 FFI 메모리 접근 최적화의 예다. 이 코드 역시 특정 LuaJIT 버전의 machine-code dump로 확인한 것이 아니며, 임의 C 함수 본체의 인라인 여부와는 구분한다.

LuaJIT은 단일 개발자(Mike Pall)가 만들었음에도, 특정 워크로드(수치 계산 루프, FFI를 통한 C 데이터 직접 접근)에서 매우 강한 성능을 보인다. 다만 이는 워크로드별 결과이고, JS 엔진과의 일반적인 우열을 매기는 비교는 언어·벤치마크·버전에 따라 결과가 크게 달라지므로 단정하기 어렵다.

### V8: Method JIT의 현대 표준

**Tier system** (2024 기준):

1. **Ignition** (Interpreter)
 - Bytecode 해석
 - Inline cache 수집
 - 프로파일링 데이터 생성

2. **Sparkplug** (Baseline JIT, 2021 추가)
 - Bytecode → 단순 기계어 (1:1 대응)
 - 레지스터 할당 없음
 - 컴파일 속도 극대화

3. **Maglev** (Mid-tier Optimizing JIT, 2023 추가)
 - 타입 피드백 기반의 가벼운 SSA 최적화
 - Sparkplug와 TurboFan 사이의 균형점

4. **TurboFan** (Optimizing JIT)
 - Sea of Nodes IR
 - 공격적 인라이닝
 - Escape analysis
 - Loop peeling, unrolling

**예제**: 간단한 함수의 여정

```javascript
function add(a, b) {
    return a + b;
}
```

**Ignition bytecode**:
```text
Ldar a1          ; Load argument 1 (b) into the accumulator
Add a0, [0]      ; Add a (register lhs) + accumulator (rhs), feedback slot 0
Return
```

**Sparkplug (x64)**:
```assembly
mov rax, [rdi+0x10]  ; load a
mov rcx, [rdi+0x18]  ; load b
call Builtin_Add      ; generic add
ret
```

**TurboFan (최적화, a와 b가 항상 SMI)**:
아래는 정수를 두 배로 태깅해 64비트 레지스터에 담는 개념 코드다. 실제 V8의 SMI 폭과 태그 배치는 빌드·pointer compression 설정에 따라 다르며, 이 코드를 특정 버전의 dump로 제시하는 것은 아니다.

```assembly
; Guard
test rdi, 1
jnz deopt
test rsi, 1
jnz deopt
; Integer add
mov rax, rdi
add rax, rsi
jo  overflow_or_deopt
ret
overflow_or_deopt:
    ; rdi/rsi의 원래 SMI 값은 아직 보존돼 있다. wider number 덧셈이나
    ; deopt frame state 복구로 넘긴다.
    jmp slow_path_or_deopt
```

SMI type guard는 값 범위를 보장하지 않는다. 덧셈이 SMI 범위를 넘으면 overflow guard로 slow path/deopt에 진입해 원래 operand와 복구된 frame state로 wide/number 계산을 수행한다.

V8 팀의 "Ignition and TurboFan" 블로그(2017)에 자세한 설명이 있다.

###.NET Core: Tiered Compilation

**2018년.NET Core 2.1**에 도입된 tiered compilation:

```text
C# source → Roslyn → IL (bytecode) →
  → Tier 0 (Quick JIT) →
  → Tier 1 (Optimizing JIT)
```

**Tier 0**: Method 처음 호출 시
- 최소 최적화
- 빠른 컴파일 (startup time 개선)
- 프로파일링 계측 코드 삽입

**Tier 1**: Method가 hot해지면
- Full optimization
- Inlining, loop optimization
- SIMD vectorization

**예제**:
```csharp
int Sum(int[] arr) {
    int sum = 0;
    for (int i = 0; i < arr.Length; i++)
        sum += arr[i];
    return sum;
}
```

**Tier 0** (simplified):
```assembly
xor eax, eax
mov ecx, 0
.loop:
    cmp ecx, [rdx+8]   ; arr.Length
    jge .done
    add eax, [rdx+rcx*4+16]
    inc ecx
    jmp .loop
.done:
    ret
```

**SIMD로 벡터화한 합산의 개념 코드** (위 C# 스칼라 루프가 Tier 1에서 반드시 이렇게 변환된다는 뜻은 아니다. 명시적 `Vector<T>`/intrinsic 사용 여부와 대상 JIT의 지원을 확인한다):
```assembly
vxorps xmm0, xmm0, xmm0
xor eax, eax
mov ecx, [rdx+8]
and ecx, -4                            ; Length를 4의 배수로 내림 — 벡터 루프의 상한
jz .tail
.vector_loop:
    vpaddd xmm0, xmm0, [rdx+rax*4+16]  ; 4개 동시 덧셈
    add eax, 4
    cmp eax, ecx
    jl .vector_loop
.tail:
; 나머지 0~3개 원소는 스칼라 루프로 처리(생략)
; Horizontal sum
vpshufd xmm1, xmm0, 0x4E
vpaddd xmm0, xmm0, xmm1
...
```

.NET은 RyuJIT 컴파일러를 사용하며, 소스가 공개되어 있다 (dotnet/runtime 리포지토리).

### 비교: 설계 철학의 차이

| | LuaJIT | V8 |.NET |
|---|--------|-----|------|
| **접근** | Tracing | Method + IC | Method |
| **Tier** | 2 (Int + JIT) | 4 (Ign + Spark + Maglev + Turbo) | 2 (Quick + Opt) |
| **최적화 기준** | Trace | Call site | Method |
| **Deopt** | Guard failure → interp | Bailout → Ignition | 미확인(Dynamic PGO/GDV는 fast/slow 경로) |
| **특화** | Type+path | Type | Type |

.NET의 deopt 동작은 일반 Dynamic PGO/GDV guard 실패의 fast/slow 경로 및 일반 호출 폴백과 tier 전환/OSR/deoptimization을 구분해야 한다. 표의 .NET Deopt 항목은 정확한 runtime 버전의 JIT dump가 확인되기 전까지 일반 규칙으로 두지 않는다.

바이너리나 런타임의 크기를 한 줄로 비교하는 것은 의미가 약하다. 정적/동적 링크, ICU·표준 라이브러리·GC·디버그 심볼 포함 여부와 배포 방식이 서로 다르기 때문이다. LuaJIT은 비교적 작은 런타임과 trace 특화 설계를 택하고, V8은 JavaScript 의미론과 여러 최적화 계층을 구현하며, .NET JIT은 IL의 정적 타입·런타임 프로파일 정보를 함께 활용한다. 어느 쪽이 더 공격적인지는 최적화 종류와 워크로드별로 비교해야 한다.


---

## 결론

JIT 컴파일은 하드웨어와 소프트웨어, 보안과 성능, 정적 정보와 동적 정보가 교차하는 지점에 있는 시스템이다. 이 글에서 다룬 핵심 도구는 다음 여섯 가지로 정리된다. 메모리에 바이트를 쓰고 실행하는 기계어 생성의 기초, 보안과 실행 가능성을 함께 만족시키기 위한 W^X 보호와 dual mapping, 컴파일 단위를 무엇으로 잡을지를 결정하는 method JIT와 tracing JIT의 트레이드오프, 동적 타입 언어의 호출 비용을 줄이는 inline caching, 추측 기반 최적화가 틀렸을 때 인터프리터로 안전하게 되돌아가는 deoptimization, 그리고 같은 원리를 다른 비율로 결합한 LuaJIT, V8,.NET의 실전 구현이다.

이 결합 위에서 동적 언어는 정적 언어와의 격차를 좁힌다. Python이 PyPy로, JavaScript가 V8로, Lua가 LuaJIT로 빨라지는 것은 한 가지 기법의 결과가 아니라 프로파일링, 특수화, inline cache, deoptimization, 코드 캐시가 함께 작동한 결과다. JIT은 GC 루트, safepoint, 메모리 모델, 스레드 동기화와 계약을 공유하므로 단독 최적화기가 아니라 런타임 시스템의 한 축이다.

## 더 읽을거리

### 기초 논문
- David Ungar and Randall B. Smith, ["SELF: The Power of Simplicity"](https://bibliography.selflanguage.org/_static/self-power.pdf), OOPSLA 1987
- L. Peter Deutsch and Allan M. Schiffman, ["Efficient Implementation of the Smalltalk-80 System"](https://dl.acm.org/doi/10.1145/800017.800542), POPL 1984
- Andreas Gal et al., ["Trace-based Just-in-Time Type Specialization for Dynamic Languages"](https://dl.acm.org/doi/10.1145/1542476.1542528), PLDI 2009
- Urs Hölzle, ["Adaptive Optimization for SELF: Reconciling High Performance with Exploratory Programming"](https://bibliography.selflanguage.org/_static/urs-thesis.pdf), PhD thesis, Stanford, 1994

### 실전 시스템 문서
- Mike Pall, ["LuaJIT 2.0 Intellectual Property Disclosure"](http://lua-users.org/lists/lua-l/2009-11/msg00089.html), 2009
- V8 Team, ["Firing up the Ignition interpreter"](https://v8.dev/blog/ignition-interpreter), 2016
- V8 Team, ["Launching Ignition and TurboFan"](https://v8.dev/blog/launching-ignition-and-turbofan), 2017
- V8 Team, ["Sparkplug — a non-optimizing JavaScript compiler"](https://v8.dev/blog/sparkplug), 2021
- V8 Team, ["Maglev - V8's Fastest Optimizing JIT"](https://v8.dev/blog/maglev), 2023
- V8 Team, ["Untrusted code mitigations"](https://v8.dev/docs/untrusted-code-mitigations), 2018 — JIT spraying 방어가 아니라 별개 위협(Spectre) 완화 자료다.
- Microsoft, [".NET Core 2.1 Tiered Compilation"](https://devblogs.microsoft.com/dotnet/tiered-compilation-preview-in-net-core-2-1/), 2018

### 최적화 기법
- Urs Hölzle, Craig Chambers, and David Ungar, ["Optimizing Dynamically-Typed Object-Oriented Languages With Polymorphic Inline Caches"](https://bibliography.selflanguage.org/_static/pics.pdf), ECOOP 1991
- Thomas Kotzmann et al., ["Design of the Java HotSpot Client Compiler for Java 6"](https://dl.acm.org/doi/10.1145/1369396.1370017), ACM TACO 5(1), 2008
- Vasanth Bala et al., ["Dynamo: A Transparent Dynamic Optimization System"](https://dl.acm.org/doi/10.1145/349299.349303), PLDI 2000

### 보안
- Dion Blazakis, ["Interpreter Exploitation: Pointer Inference and JIT Spraying"](https://www.semanticscholar.org/paper/Interpreter-Exploitation-%3A-Pointer-Inference-and-Blazakis/6c1ce97d987e234eb7b5c801b582a1b8c11c2a30), BlackHat DC 2010
- Ben Niu and Gang Tan, ["RockJIT: Securing Just-In-Time Compilation Using Modular Control-Flow Integrity"](https://dl.acm.org/doi/10.1145/2660267.2660281), CCS 2014

### 시스템 프로그래밍
- Intel, ["Intel 64 and IA-32 Architectures Software Developer's Manual"](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html), Volume 2 (Instruction Set Reference) — 명령 인코딩 자료이며 메모리 보호·NX는 System Programming 쪽 volume 소관이다. 링크는 매뉴얼 문서 포털이다.
- AMD, ["AMD64 Architecture Programmer's Manual"](https://www.amd.com/en/support/tech-docs), Volume 3 (General Purpose and System Instructions) — tech-docs는 문서 묶음 포털이므로 인용할 때는 개별 volume/revision/절을 고정한다.
- Linux man pages: [`mmap(2)`](https://man7.org/linux/man-pages/man2/mmap.2.html), [`mprotect(2)`](https://man7.org/linux/man-pages/man2/mprotect.2.html)

### 오픈소스 구현
- [LuaJIT source code](https://github.com/LuaJIT/LuaJIT) (특히 `src/lj_trace.c`, `src/lj_asm_x86.h`) — 저장소는 구현 위치 안내이며, 이 문서가 확인한 것은 allocator/FFI 관련 일부 source·docs다.
- [V8 source code](https://chromium.googlesource.com/v8/v8.git) (특히 `src/compiler/`, `src/interpreter/`) — Ignition operand 순서는 interpreter-generator.cc 같은 파일/commit을 특정하면 직접 근거가 된다.
- [.NET Runtime](https://github.com/dotnet/runtime) (특히 `src/coreclr/jit/`) — 저장소 root는 JIT 구현 위치 안내이며 Tier0 복귀 규칙의 근거는 아니다.
- [LLVM ORC JIT](https://llvm.org/docs/ORCv2.html)
- [PyPy](https://github.com/pypy/pypy) (RPython JIT generator) — 저장소 전체 실행·설계를 확인한 것은 아니다.

### 튜토리얼
- Eli Bendersky, ["Adventures in JIT compilation"](https://eli.thegreenplace.net/2017/adventures-in-jit-compilation-part-1-an-interpreter/), 2017-2018 (4부작 시리즈)
- Chris Seaton, [TruffleRuby 블로그·논문·강연 모음](https://chrisseaton.com/truffleruby/) — "Understanding How Graal Works", "Deoptimizing Ruby" 등 여러 포스트
