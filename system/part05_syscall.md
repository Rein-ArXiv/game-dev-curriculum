## 들어가며

> "The system call is the fundamental interface between an application and the Linux kernel."
> — `man 2 syscalls` (Linux man-pages)


사용자 프로그램이 파일을 읽거나, 네트워크 패킷을 보내거나, 새로운 프로세스를 생성할 때, 어떻게 이 모든 일이 가능할까? 답은 시스템 콜(system call)에 있다. 시스템 콜은 사용자 공간(user space)과 커널 공간(kernel space) 사이의 유일하고 엄격하게 통제된 관문이다.

하지만 이 관문을 통과하는 과정은 단순하지 않다. CPU의 특권 레벨이 변경되고, 스택이 전환되며, 수많은 보안 검사가 수행된다. 그렇다면 정확히 무슨 일이 벌어지는 걸까? `write`를 호출했을 때, CPU는 어떻게 사용자 모드에서 커널 모드로 전환하는가? 레지스터에는 무엇이 담기고, 스택은 어떻게 바뀌는가?

이 글은 시스템 콜의 내부 메커니즘을 깊이 탐구한다. x86-64 아키텍처를 중심으로, Intel의 SYSCALL 명령어부터 Linux 커널의 entry point까지, 그리고 시스템 콜 테이블에서 실제 커널 함수 실행까지의 전체 여정을 따라간다.


---

## 목차
1. [시스템 콜이란: 커널과의 계약](#1-시스템-콜이란-커널과의-계약)
2. [특권 레벨: Ring 0에서 Ring 3까지](#2-특권-레벨-ring-0에서-ring-3까지)
3. [전통적 방식: int 0x80과 sysenter](#3-전통적-방식-int-0x80과-sysenter)
4. [현대적 방식: syscall 명령어](#4-현대적-방식-syscall-명령어)
5. [시스템 콜 테이블: sys_call_table의 구조](#5-시스템-콜-테이블-sys_call_table의-구조)
6. [시스템 콜의 전체 흐름: write를 따라가며](#6-시스템-콜의-전체-흐름-write를-따라가며)
7. [매개변수 전달: 레지스터 컨벤션](#7-매개변수-전달-레지스터-컨벤션)
8. [커널 스택과 사용자 스택](#8-커널-스택과-사용자-스택)
9. [vDSO: 시스템 콜 없이 커널 기능 사용](#9-vdso-시스템-콜-없이-커널-기능-사용)
10. [시스템 콜 추가하기: 커널 해킹 입문](#10-시스템-콜-추가하기-커널-해킹-입문)


---

## 1. 시스템 콜이란: 커널과의 계약

### 왜 시스템 콜이 필요한가?

현대 운영체제는 **보호 모드(protected mode)**에서 동작한다. 이는 사용자 프로그램이 하드웨어에 직접 접근하거나, 다른 프로세스의 메모리를 마음대로 읽거나, 특권 명령어를 실행할 수 없다는 의미다. 만약 이런 제약이 없다면 어떻게 될까?

- 악의적인 프로그램이 시스템 전체를 장악할 수 있다
- 버그가 있는 프로그램이 전체 시스템을 다운시킬 수 있다
- 프로세스 간 격리가 불가능해진다

따라서 운영체제는 하드웨어와 사용자 프로그램 사이에 추상화 계층을 둔다. 이 계층이 바로 **시스템 콜 인터페이스**다.

Ritchie와 Thompson이 1970년대 초 Unix를 설계할 때 세운 원칙 — 시스템 콜의 수는 작게 유지하고, 각 시스템 콜은 한 가지 일을 잘 하게 한다 — 은 Unix 철학("do one thing well")의 핵심으로 남아 있다.

이 철학은 오늘날까지 이어진다. Linux는 300개가 넘는 시스템 콜을 제공하지만, 대부분의 애플리케이션은 그 중 일부만 사용한다.

### 시스템 콜의 특성

시스템 콜은 다음과 같은 특성을 가진다:

**1) 동기적(Synchronous) 실행**
시스템 콜은 호출한 프로세스의 컨텍스트에서 실행된다. `read`를 호출하면, 그 프로세스는 데이터를 읽을 때까지 블록된다.

**2) 원자적(Atomic) 인터페이스**
시스템 콜은 불가분의 작업 단위다. 중간에 중단되더라도, 커널은 일관된 상태를 유지한다.

**3) 최소 권한 원칙**
시스템 콜은 요청된 작업에 필요한 최소한의 권한만 부여받는다.

**4) 버전 호환성**
한 번 정의된 시스템 콜 인터페이스는 하위 호환성을 유지한다. 이것이 Linus Torvalds가 가장 중요하게 여기는 원칙 중 하나다.

### 시스템 콜 vs 라이브러리 함수

많은 개발자들이 `printf`나 `malloc`을 시스템 콜로 착각한다. 하지만 이들은 **라이브러리 함수(library function)**다. 차이점은 무엇일까?

```c
// printf는 라이브러리 함수
printf("Hello\n");  // 내부적으로 write() 시스템 콜을 호출

// write는 시스템 콜
write(1, "Hello\n", 6);  // 커널로 직접 진입
```

**시스템 콜의 특징:**
- 사용자 모드에서 커널 모드로의 mode transition이 일어난다 (다른 task로의 context switch와는 다르다)
- mode transition 자체에 더해 인자 검증, 자격 검사, 메모리 접근 안전성 등 추가 비용이 들어 일반 함수 호출보다 비싸다
- man 섹션 2에 문서화된다 (`man 2 write`)
- 개수가 제한적이다 (Linux: 약 350개)

**라이브러리 함수의 특징:**
- 사용자 모드에서 실행된다
- 여러 시스템 콜을 조합할 수 있다
- man 섹션 3에 문서화된다 (`man 3 printf`)
- 개수가 무제한이다

### 시스템 콜의 분류

Linux의 시스템 콜은 크게 다음과 같이 분류할 수 있다:

**1) 프로세스 관리**
- `fork`, `execve`, `exit`, `wait`
- `clone`, `getpid`, `kill`

**2) 파일 I/O**
- `open`, `read`, `write`, `close`
- `lseek`, `stat`, `chmod`

**3) 메모리 관리**
- `brk`, `mmap`, `munmap`
- `mprotect`, `madvise`

**4) 네트워크**
- `socket`, `bind`, `listen`, `accept`
- `connect`, `send`, `recv`

**5) 시그널**
- `signal`, `sigaction`, `kill`
- `sigprocmask`, `sigsuspend`

**6) 시간**
- `time`, `gettimeofday`, `clock_gettime`
- `nanosleep`, `timer_create`

**7) IPC (Inter-Process Communication)**
- `pipe`, `shmget`, `semget`, `msgget`
- `mq_open`, `eventfd`

## 2. 특권 레벨: Ring 0에서 Ring 3까지

### CPU의 보호 링

Intel x86 아키텍처는 **4개의 특권 레벨(privilege level)**을 정의한다. 이를 **링(ring)**이라고 부른다:

```text
Ring 0 (커널 모드)       - 가장 높은 특권
Ring 1 (드라이버)        - 사용 안 함
Ring 2 (드라이버)        - 사용 안 함
Ring 3 (사용자 모드)     - 가장 낮은 특권
```

주요 x86 운영체제의 일반 프로세스 모델은 커널에 Ring 0, 사용자 코드에 Ring 3을 쓰고 Ring 1·2는 보통 사용하지 않는다. 이것은 x86 보호 모델 전체가 두 단계뿐이라는 뜻은 아니다. VMX root/non-root, SMM, 펌웨어와 hypervisor는 ring 바깥의 추가 실행 경계를 사용한다.

### 특권 레벨의 의미

각 링에서 실행되는 코드는 서로 다른 권한을 가진다:

**Ring 0 (커널 모드)에서 가능한 것:**
- 모든 메모리 영역 접근
- I/O 포트 직접 제어
- 인터럽트 활성화/비활성화
- 페이지 테이블 수정
- 특권 명령어 실행 (HLT, LGDT, LIDT, INVLPG 등)

**Ring 3 (사용자 모드)에서 제한되는 것:**
- 커널 메모리 접근 불가 (접근 시 Segmentation Fault)
- I/O 포트 접근 불가 (접근 시 General Protection Fault)
- 특권 명령어 실행 불가 (실행 시 #GP 예외)

### 현재 특권 레벨 확인

현재 실행의 **CPL(Current Privilege Level)**은 x86 보호 검사에 쓰이며, 정상적인 code segment selector의 `CS.RPL` 하위 2비트와 일치한다. selector의 RPL과 descriptor의 DPL은 별도 개념이므로 임의 selector의 하위 비트를 곧 CPL이라고 일반화하면 안 된다.

```text
CS 레지스터 구조 (16비트):
+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
|      세그먼트 셀렉터 (13비트)              |TI | RPL (2비트)|
+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                                                    ^^
                                                    ||
                                           현재 특권 레벨 (CPL)
```

일반적인 x86-64 Linux GDT 구성의 예:
- 사용자 모드: CS = 0x33 (CPL = 3)
- 커널 모드: CS = 0x10 (CPL = 0)

### 특권 레벨 전환

특권 레벨을 변경하는 방법은 엄격하게 제한된다. CPU는 다음 경우에만 Ring 3에서 Ring 0으로 전환을 허용한다:

**1) 인터럽트(Interrupt)**
하드웨어가 CPU에 신호를 보내면, CPU는 자동으로 Ring 0으로 전환하고 IDT(Interrupt Descriptor Table)에 정의된 핸들러를 실행한다.

**2) 예외(Exception)**
프로그램이 0으로 나누기, 페이지 폴트 등의 예외를 발생시키면, CPU는 Ring 0으로 전환하고 예외 핸들러를 실행한다.

**3) 시스템 콜**
사용자 프로그램이 명시적으로 커널 서비스를 요청할 때 사용한다. 이 글이 다루는 주제다.

### 왜 Ring 1과 2는 사용하지 않는가?

Ring 1과 2는 x86이 제공하는 중간 privilege level이다. 특정 운영체제가 서비스나 driver 격리에 사용할 수 있지만, 주요 범용 x86-64 운영체제는 보통 kernel Ring 0과 user Ring 3의 두 경계를 중심으로 설계한다.

**1) 복잡성**
여러 특권 레벨을 관리하는 것은 복잡하고 버그를 유발하기 쉽다.

**2) 이식 가능한 보호 모델**
다른 CPU architecture가 x86과 같은 네 ring을 제공하지 않으므로 kernel/user 두 경계와 page permission을 중심으로 설계하기 쉽다.

**3) 가상화**
VT-x는 VMX root/non-root, AMD-V는 host/guest 실행 상태라는 ring과 직교하는 가상화 경계를 제공한다. “Ring -1”은 비공식 비유일 뿐 실제 CPL 값이 -1인 것이 아니다. 게스트 kernel은 non-root/guest 상태 안에서 CPL 0으로 실행할 수 있다.

**4) 마이크로커널 vs 모놀리식 커널**
microkernel은 더 많은 service와 일부 driver를 user mode process로 분리할 수 있고, monolithic kernel은 많은 driver를 kernel mode에 포함한다. 실제 경계는 운영체제마다 다르지만 어느 경우에도 Ring 1·2를 반드시 써야 하는 것은 아니다.

### 관찰: 현재 특권 레벨 확인

다음 코드로 현재 특권 레벨을 확인할 수 있다:

```c
#include <stdio.h>

int main(void) {
    unsigned short cs;

    // CS 레지스터 읽기
    __asm__ __volatile__("mov %%cs, %0" : "=r" (cs));

    printf("CS register: 0x%x\n", cs);
    printf("CPL (Current Privilege Level): %d\n", cs & 0x3);

    return 0;
}
```

실행 결과:
```text
CS register: 0x33
CPL (Current Privilege Level): 3
```

사용자 모드에서 실행되므로 CPL은 3이다.

## 3. 전통적 방식: int 0x80과 sysenter

### int 0x80: 소프트웨어 인터럽트

초기 Linux는 시스템 콜을 위해 **소프트웨어 인터럽트(software interrupt)**를 사용했다. 구체적으로는 **int 0x80** 명령어다.

#### 동작 원리

1. 사용자 프로그램이 `int 0x80`을 실행
2. CPU가 IDT(Interrupt Descriptor Table)의 128번(0x80) 엔트리를 찾음
3. 해당 엔트리에 저장된 커널 함수(`system_call`)로 점프
4. CPU가 자동으로 Ring 3에서 Ring 0으로 전환
5. 커널 스택으로 스택 포인터 변경
6. 이전 SS, ESP, EFLAGS, CS, EIP를 커널 스택에 저장

#### 예제 코드 (32비트)

```asm
; write(1, "Hello\n", 6) 시스템 콜
section .data
    msg db 'Hello', 0x0a    ; 문자열 + 개행

section .text
    global _start

_start:
    ; write 시스템 콜 준비
    mov eax, 4              ; sys_write의 시스템 콜 번호
    mov ebx, 1              ; 파일 디스크립터 (stdout)
    mov ecx, msg            ; 버퍼 주소
    mov edx, 6              ; 길이
    int 0x80                ; 시스템 콜 실행

    ; exit 시스템 콜
    mov eax, 1              ; sys_exit의 시스템 콜 번호
    xor ebx, ebx            ; 종료 코드 0
    int 0x80
```

레지스터 컨벤션:
- `EAX`: 시스템 콜 번호
- `EBX`, `ECX`, `EDX`, `ESI`, `EDI`, `EBP`: 인자 (순서대로 1~6번째)
- `EAX`: 반환값

#### int 0x80의 문제점

`int 0x80`은 간단하지만, 성능 면에서 치명적인 단점이 있다:

**1) 느린 실행 속도**
소프트웨어 인터럽트는 다음 작업을 수행해야 한다:
- IDT 테이블 조회
- 디스크립터 로드
- 세그먼트 체크
- 스택 전환
- 레지스터 저장

이 경로는 descriptor lookup, privilege transition과 entry/exit 처리를 거친다. 실제 cycle 수는 CPU 세대, cache·TLB 상태, kernel entry code와 보안 완화책에 따라 달라지므로 고정 범위로 일반화하지 않는다.

**2) 캐시 오염**
IDT 테이블 접근은 캐시 미스를 유발할 수 있다.

**3) 파이프라인 플러시**
인터럽트는 CPU의 명령어 파이프라인을 비운다.

### sysenter/sysexit: Intel의 해결책

Intel은 Pentium II 프로세서(1997년)부터 **sysenter/sysexit** 명령어를 도입했다. 이는 시스템 콜 전용 명령어로, `int` 명령어보다 훨씬 빠르다.

#### MSR 레지스터 설정

`sysenter` 명령어는 다음 MSR(Model-Specific Register)을 참조한다:

```text
MSR_SYSENTER_CS  (0x174): 커널 코드 세그먼트
MSR_SYSENTER_ESP (0x175): 커널 스택 포인터
MSR_SYSENTER_EIP (0x176): 커널 진입점 주소
```

커널 부팅 시, 이 MSR들을 초기화한다:

```c
// arch/x86/kernel/cpu/common.c
void syscall_init(void)
{
    /* SYSENTER 설정 (32비트용) */
    wrmsrl(MSR_SYSENTER_CS, __KERNEL_CS);
    wrmsrl(MSR_SYSENTER_ESP, &cpu_tss.sp0);
    wrmsrl(MSR_SYSENTER_EIP, entry_SYSENTER_32);
}
```

#### sysenter 명령어의 동작

1. CPL을 0으로 변경 (Ring 0 진입)
2. CS를 MSR_SYSENTER_CS 값으로 설정
3. ESP를 MSR_SYSENTER_ESP 값으로 설정
4. EIP를 MSR_SYSENTER_EIP 값으로 설정
5. **자동으로 레지스터 저장을 하지 않음** (커널이 직접 해야 함)

#### sysexit 명령어의 동작

1. CPL을 3으로 변경 (Ring 3 복귀)
2. CS와 SS를 복원
3. ESP와 EIP를 복원
4. 사용자 모드로 복귀

#### sysenter의 장점

- **전용 fast-entry 경로**: 범용 interrupt gate보다 system call 진입에 필요한 상태 전환을 단순화
- **단순성**: IDT 조회 불필요
- **파이프라인**: 파이프라인 플러시 최소화

#### sysenter의 단점

**1) 복귀 주소 문제**
`sysenter`는 복귀 주소를 자동으로 저장하지 않는다. 따라서 glibc가 다음과 같은 트릭을 사용한다:

```asm
__kernel_vsyscall:
    push %ecx                  ; ECX 저장 (복귀 주소용)
    push %edx                  ; EDX 저장
    push %ebp                  ; EBP 저장
    mov  %esp, %ebp            ; EBP에 스택 포인터 저장
    sysenter                   ; 시스템 콜 진입

    ; 커널이 여기로 돌아옴
    pop  %ebp
    pop  %edx
    pop  %ecx
    ret
```

**2) 64비트 미지원**
`sysenter`는 32비트 전용이다. 64비트에서는 다른 방법을 사용해야 한다.

### AMD의 대안: syscall/sysret

AMD는 AMD64 아키텍처(2003년)에서 **syscall/sysret** 명령어를 도입했다. Intel도 나중에 이를 채택했다.

`syscall`은 `sysenter`보다 더 우아한 설계를 가진다:
- 복귀 주소를 RCX에 자동 저장
- RFLAGS를 R11에 자동 저장
- 64비트 네이티브 지원

현대 Linux는 64비트 시스템에서 `syscall`을 사용하고, 32비트 호환 모드에서는 `int 0x80` 또는 `sysenter`를 사용한다.

## 4. 현대적 방식: syscall 명령어

### syscall 개요

**syscall** 명령어는 x86-64 아키텍처의 표준 시스템 콜 메커니즘이다. AMD가 설계하고 Intel이 채택했다.

#### 핵심 특징

- **전용 진입 계약**: MSR에 설정한 entry point와 정해진 레지스터를 사용
- **x86-64 네이티브 ABI**: 64비트 Linux의 기본 system call 진입 명령
- **자동 저장**: RIP와 RFLAGS를 레지스터에 저장
- **전용 복귀 경로**: 조건이 맞으면 `sysret`을 사용할 수 있고, 상태·보안 조건에 따라 kernel entry code가 `iret` 계열 경로를 선택할 수도 있음

### 관련 MSR 레지스터

`syscall` 명령어는 다음 MSR을 사용한다:

```text
MSR_LSTAR  (0xC0000082): 64비트 모드 진입점 (Long Mode STAR)
MSR_CSTAR  (0xC0000083): 32비트 호환 모드 진입점 (Compatibility STAR)
MSR_STAR   (0xC0000081): 세그먼트 셀렉터 정보
MSR_SFMASK (0xC0000084): RFLAGS 마스크
```

#### MSR_LSTAR: 커널 진입점

가장 중요한 MSR이다. 커널 부팅 시 설정된다:

```c
// arch/x86/kernel/cpu/common.c
void syscall_init(void)
{
    /* 64비트 syscall 진입점 설정 */
    wrmsrl(MSR_LSTAR, (unsigned long)entry_SYSCALL_64);

    /* 32비트 호환 모드 진입점 설정 */
    wrmsrl(MSR_CSTAR, (unsigned long)entry_SYSCALL_compat);

    /* 세그먼트 설정 */
    wrmsrl(MSR_STAR,
           ((u64)__USER32_CS) << 48  |  // Sysret userspace CS
           ((u64)__KERNEL_CS) << 32);    // Syscall kernel CS

    /* RFLAGS 마스크: IF, DF, TF, NT, AC 비트 클리어 */
    wrmsrl(MSR_SFMASK,
           X86_EFLAGS_TF | X86_EFLAGS_DF | X86_EFLAGS_IF |
           X86_EFLAGS_IOPL | X86_EFLAGS_AC | X86_EFLAGS_NT);
}
```

#### MSR_STAR: 세그먼트 셀렉터

MSR_STAR는 64비트 레지스터로, 다음과 같이 구성된다:

```text
+--------+--------+--------+--------+--------+--------+--------+--------+
| 63..48 | 47..32 | 31..16 | 15..0                                      |
+--------+--------+--------+--------+------------------------------------+
| User CS| Kernel | (Reserved)                                         |
| +16    | CS     |                                                    |
+--------+--------+----------------------------------------------------+
```

- **[47:32]**: Syscall 시 사용할 커널 CS (Code Segment)
- **[63:48]**: Sysret 시 사용할 사용자 CS

#### MSR_SFMASK: RFLAGS 제어

Syscall 진입 시, RFLAGS의 특정 비트를 자동으로 클리어한다:
- **IF (Interrupt Flag)**: 인터럽트 비활성화
- **DF (Direction Flag)**: 문자열 연산 방향 초기화
- **TF (Trap Flag)**: 싱글 스텝 디버깅 비활성화
- **IOPL (I/O Privilege Level)**: I/O 권한 제거

### syscall 명령어의 동작

`syscall` 명령어를 실행하면 CPU는 다음을 수행한다:

```text
1. RCX ← RIP                 ; 복귀 주소 저장
2. R11 ← RFLAGS              ; 플래그 저장
3. RFLAGS ← RFLAGS & ~SFMASK ; 플래그 마스킹
4. CPL ← 0                   ; Ring 0으로 전환
5. CS ← MSR_STAR[47:32]      ; 커널 코드 세그먼트
6. RIP ← MSR_LSTAR           ; 커널 진입점으로 점프
7. SS ← MSR_STAR[47:32] + 8  ; 커널 스택 세그먼트
```

**주목할 점:**
- **RSP는 변경되지 않음**: 커널이 직접 스택을 전환해야 함
- **레지스터는 보존됨**: RAX, RDI, RSI, RDX, R10, R8, R9
- **자동 인터럽트 비활성화**: IF 플래그가 클리어됨

### sysret 명령어의 동작

`sysret` 명령어는 시스템 콜에서 복귀할 때 사용한다:

```text
1. CPL ← 3                    ; Ring 3으로 복귀
2. CS ← MSR_STAR[63:48] + 16  ; 사용자 코드 세그먼트
3. SS ← MSR_STAR[63:48] + 8   ; 사용자 스택 세그먼트
4. RFLAGS ← R11               ; 플래그 복원
5. RIP ← RCX                  ; 복귀 주소 복원
```

**주목할 점:**
- **RSP는 커널이 복원해야 함**
- **RAX에 반환값 저장**

### 관찰: syscall 직접 사용

다음은 어셈블리로 `syscall`을 직접 사용하는 예제다:

```asm
; write(1, "Hello, syscall!\n", 16)
section .data
    msg db 'Hello, syscall!', 0x0a
    len equ $ - msg

section .text
    global _start

_start:
    ; write 시스템 콜
    mov rax, 1          ; sys_write (시스템 콜 번호 1)
    mov rdi, 1          ; fd = 1 (stdout)
    mov rsi, msg        ; buf = msg
    mov rdx, len        ; count = len
    syscall             ; 커널 호출

    ; exit 시스템 콜
    mov rax, 60         ; sys_exit (시스템 콜 번호 60)
    xor rdi, rdi        ; status = 0
    syscall
```

컴파일 및 실행:
```bash
nasm -f elf64 syscall_test.asm -o syscall_test.o
ld syscall_test.o -o syscall_test
./syscall_test
```

출력:
```text
Hello, syscall!
```

### 레지스터 컨벤션

x86-64 시스템 콜은 다음 레지스터를 사용한다:

| 레지스터 | 용도 | 보존 여부 |
|---------|---------------------|----------|
| RAX | 시스템 콜 번호 / 반환값 | 아니오 |
| RDI | 1번째 인자 | 예 |
| RSI | 2번째 인자 | 예 |
| RDX | 3번째 인자 | 예 |
| R10 | 4번째 인자 (RCX 대신) | 예 |
| R8 | 5번째 인자 | 예 |
| R9 | 6번째 인자 | 예 |
| RCX | 복귀 주소 (syscall이 저장) | 아니오 |
| R11 | RFLAGS (syscall이 저장) | 아니오 |

**중요**: RCX는 원래 4번째 인자로 사용되어야 하지만, `syscall` 명령어가 복귀 주소를 RCX에 저장하므로, **R10**을 4번째 인자로 사용한다.

#### 예제: 6개 인자를 가진 시스템 콜

```c
// mmap의 시스템 콜 시그니처
// void *mmap(void *addr, size_t length, int prot, int flags,
//            int fd, off_t offset);

long syscall_mmap(void *addr, size_t length, int prot, int flags,
                   int fd, off_t offset)
{
    long ret;
    register long r10 asm("r10") = flags;  // RCX 대신 R10 사용!
    register long r8 asm("r8") = fd;
    register long r9 asm("r9") = offset;

    asm volatile(
        "syscall"
        : "=a" (ret)
        : "a" (9),              // sys_mmap = 9
          "D" (addr),           // RDI
          "S" (length),         // RSI
          "d" (prot),           // RDX
          "r" (r10),            // R10
          "r" (r8),             // R8
          "r" (r9)              // R9
        : "rcx", "r11", "memory"
    );

    return ret;
}
```

### 진입 메커니즘을 비교할 때의 주의점

`int 0x80`, `sysenter`, `syscall`을 한 표의 숫자로만 비교하면 서로 다른 ABI를 섞기 쉽다.

| 메커니즘 | 주된 x86 Linux 맥락 | 핵심 특성 |
|---------|-------------------|-----------|
| `int 0x80` | 전통적인 i386 ABI, x86-64의 호환 경로 | IDT를 통한 범용 소프트웨어 인터럽트 |
| `sysenter` | 32비트 fast system call ABI | 복귀 상태를 소프트웨어가 더 많이 구성 |
| `syscall` | x86-64 네이티브 ABI | MSR에 등록한 진입점과 정해진 레지스터 계약 사용 |
| vDSO | 일부 읽기 전용 커널 정보를 사용자 모드 함수로 제공 | 커널 진입 메커니즘이 아니라 우회 가능한 별도 인터페이스 |

현대 x86-64 Linux가 `syscall`을 사용하는 이유는 64비트용 fast-entry 명령과 ABI가 함께 설계되어 있기 때문이다. 실제 사이클 수는 보안 완화책, 커널 버전, CPU와 측정한 시스템 콜 본문에 따라 달라진다. 서로 다른 ABI의 경로를 비교하려면 같은 커널 작업과 인자 검증을 수행하는지부터 맞춰야 한다.

## 5. 시스템 콜 테이블: sys_call_table의 구조

### 시스템 콜 번호

모든 시스템 콜은 고유한 **번호(number)**를 가진다. 사용자 프로그램은 이 번호를 RAX 레지스터에 저장하고 `syscall`을 호출한다.

예를 들어:
- `read` = 0
- `write` = 1
- `open` = 2
- `close` = 3
- `fork` = 57
- `execve` = 59
- `exit` = 60

이 번호들은 어디에 정의되어 있을까?

#### 시스템 콜 번호 정의

Linux 커널 소스의 `arch/x86/entry/syscalls/syscall_64.tbl` 파일에 정의되어 있다:

```bash
# <number> <abi> <name> <entry point>
0       common  read            sys_read
1       common  write           sys_write
2       common  open            sys_open
3       common  close           sys_close
4       common  stat            sys_newstat
...
57      common  fork            sys_fork
59      common  execve          sys_execve
60      common  exit            sys_exit
...
```

이 파일은 빌드 시 파싱되어 다음 헤더 파일들을 생성한다:

**커널용**:
- `arch/x86/include/generated/asm/syscalls_64.h`

**유저스페이스용**:
- `/usr/include/asm/unistd_64.h`

```c
// /usr/include/asm/unistd_64.h
#define __NR_read 0
#define __NR_write 1
#define __NR_open 2
#define __NR_close 3
// ...
```

### sys_call_table 구조

시스템 콜 번호에서 실제 커널 함수로의 매핑은 **sys_call_table** 배열에 저장된다.

```c
// arch/x86/entry/syscall_64.c
asmlinkage const sys_call_ptr_t sys_call_table[__NR_syscall_max+1] = {
    [0 ... __NR_syscall_max] = &sys_ni_syscall,  // 기본값: 미구현
    [0] = sys_read,
    [1] = sys_write,
    [2] = sys_open,
    [3] = sys_close,
    // ...
};
```

#### sys_call_ptr_t 타입

```c
// arch/x86/include/asm/syscall.h
typedef asmlinkage long (*sys_call_ptr_t)(unsigned long, unsigned long,
                                          unsigned long, unsigned long,
                                          unsigned long, unsigned long);
```

모든 시스템 콜 핸들러는 동일한 시그니처를 가진다:
- 최대 6개의 `unsigned long` 인자
- `long` 타입 반환값
- `asmlinkage` 속성 (레지스터에서 인자를 읽음)

#### sys_ni_syscall: 미구현 시스템 콜

```c
// kernel/sys_ni.c
asmlinkage long sys_ni_syscall(void)
{
    return -ENOSYS;  // Function not implemented
}
```

존재하지 않는 시스템 콜 번호를 호출하면 `-ENOSYS` 에러가 반환된다.

### 시스템 콜 테이블 조회 과정

사용자가 `write(1, "Hi", 2)`를 호출하면:

```c
1. glibc의 write() 래퍼 함수 호출
   ↓
2. 레지스터 설정:
   RAX = 1 (sys_write 번호)
   RDI = 1 (fd)
   RSI = "Hi" (buf)
   RDX = 2 (count)
   ↓
3. syscall 명령어 실행
   ↓
4. entry_SYSCALL_64로 진입 (커널)
   ↓
5. sys_call_table[1] 조회 → sys_write 함수 포인터
   ↓
6. sys_write() 실행
   ↓
7. 반환값을 RAX에 저장
   ↓
8. sysret로 사용자 공간 복귀
```

### 시스템 콜 래퍼 매크로

커널 개발자들은 매번 함수 시그니처를 쓰는 대신 **매크로**를 사용한다:

```c
// include/linux/syscalls.h

/* 0개 인자 */
#define SYSCALL_DEFINE0(sname)                                  \
    asmlinkage long sys_##sname(void)

/* 1개 인자 */
#define SYSCALL_DEFINE1(name, ...) SYSCALL_DEFINEx(1, _##name, __VA_ARGS__)

/* 2개 인자 */
#define SYSCALL_DEFINE2(name, ...) SYSCALL_DEFINEx(2, _##name, __VA_ARGS__)

// ... 6개까지
```

#### 실제 사용 예

```c
// fs/read_write.c
SYSCALL_DEFINE3(write, unsigned int, fd, const char __user *, buf,
                size_t, count)
{
    struct fd f = fdget_pos(fd);
    ssize_t ret = -EBADF;

    if (f.file) {
        loff_t pos = file_pos_read(f.file);
        ret = vfs_write(f.file, buf, count, &pos);
        if (ret >= 0)
            file_pos_write(f.file, pos);
        fdput_pos(f);
    }

    return ret;
}
```

이 매크로는 전처리 후 다음과 같이 확장된다:

```c
asmlinkage long sys_write(unsigned int fd, const char __user *buf, size_t count)
{
    // ... (동일한 본문)
}
```

### 32비트 호환 레이어

64비트 Linux는 32비트 프로그램도 실행할 수 있다. 이를 위해 **별도의 시스템 콜 테이블**을 유지한다:

```c
// arch/x86/entry/syscall_32.c
__visible const sys_call_ptr_t ia32_sys_call_table[__NR_syscall_compat_max+1] = {
    [0 ... __NR_syscall_compat_max] = &sys_ni_syscall,
    [0] = sys_read,
    [1] = sys_write,
    // ...
};
```

32비트와 64비트 시스템 콜 번호는 **다를 수 있다**. 예를 들어:

| 시스템 콜 | 64비트 번호 | 32비트 번호 |
|----------|-----------|-----------|
| read | 0 | 3 |
| write | 1 | 4 |
| open | 2 | 5 |
| exit | 60 | 1 |
| fork | 57 | 2 |

32비트 프로그램이 `int 0x80`을 사용하면, 커널은 `ia32_sys_call_table`을 조회한다.

### 시스템 콜 테이블 보호

시스템 콜 테이블은 **읽기 전용(read-only)** 메모리에 배치된다. 이는 루트킷(rootkit)이 시스템 콜을 후킹하는 것을 방지한다.

```c
// arch/x86/entry/syscall_64.c
asmlinkage const sys_call_ptr_t sys_call_table[__NR_syscall_max+1]
    __attribute__((section(".rodata"))) = {
    // ...
};
```

**`.rodata`** 섹션은 쓰기 금지이므로, 런타임에 sys_call_table을 수정하려고 하면 커널 패닉이 발생한다.

### 시스템 콜 테이블 조회 관찰

시스템 콜 테이블의 주소는 `/proc/kallsyms`에서 확인할 수 있다 (루트 권한 필요):

```bash
sudo grep sys_call_table /proc/kallsyms
```

출력 예:
```text
ffffffff82000300 R sys_call_table
ffffffff82001580 R ia32_sys_call_table
```

`sys_call_table`의 내용을 읽으려면 커널 모듈을 작성해야 한다:

```c
// syscall_table_reader.c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>

MODULE_LICENSE("GPL");

static int __init reader_init(void)
{
    unsigned long *syscall_table;
    int i;

    // 주의: kallsyms_lookup_name은 Linux 5.7부터 모듈에 export되지 않는다.
    // 따라서 다음 코드는 수정 없이 빌드되면 EXPORT 미해결 오류가 나거나,
    // 빌드되더라도 안정 인터페이스가 아니다. 실험용 커스텀 빌드/디버그 환경에서만 가능하다.
    // 정상 경로는 kprobe로 심볼을 잡아 주소를 얻거나, 처음부터 시스콜 추적용으로 ftrace/eBPF를 사용하는 것이다.
    syscall_table = (unsigned long *)kallsyms_lookup_name("sys_call_table");

    if (!syscall_table) {
        pr_err("sys_call_table not found (또는 kallsyms_lookup_name이 export 안 됨)\n");
        return -1;
    }

    pr_info("sys_call_table address: %px\n", syscall_table);

    // 처음 10개 엔트리 출력
    for (i = 0; i < 10; i++) {
        pr_info("sys_call_table[%d] = %px\n", i, (void *)syscall_table[i]);
    }

    return 0;
}

static void __exit reader_exit(void)
{
    pr_info("Unloading module\n");
}

module_init(reader_init);
module_exit(reader_exit);
```

## 6. 시스템 콜의 전체 흐름: write를 따라가며

이제 `write(1, "Hello\n", 6)` 시스템 콜의 전체 여정을 따라가 보자. 사용자 공간에서 커널로, 그리고 다시 사용자 공간으로 돌아오는 과정을 단계별로 살펴본다.

### 단계 0: glibc 래퍼 함수

사용자 프로그램이 `write`를 호출하면, 실제로는 glibc의 래퍼 함수를 호출한다.

```c
// glibc/sysdeps/unix/sysv/linux/write.c
ssize_t write(int fd, const void *buf, size_t nbytes)
{
    return INLINE_SYSCALL_CALL(write, fd, buf, nbytes);
}
```

`INLINE_SYSCALL_CALL` 매크로는 다음과 같이 확장된다:

```c
// glibc/sysdeps/unix/sysv/linux/x86_64/sysdep.h
#define INLINE_SYSCALL_CALL(name, ...)                    \
    ({                                                    \
        long int _ret;                                    \
        INTERNAL_SYSCALL(name, ##__VA_ARGS__);           \
        _ret;                                             \
    })
```

최종적으로 어셈블리로 확장된다:

```asm
; write 시스템 콜
mov rax, 1          ; __NR_write = 1
mov rdi, [fd]       ; 첫 번째 인자
mov rsi, [buf]      ; 두 번째 인자
mov rdx, [nbytes]   ; 세 번째 인자
syscall             ; 커널 진입
; RAX에 반환값
```

### 단계 1: syscall 명령어 실행

CPU가 `syscall` 명령어를 만나면:

```text
1. RCX ← RIP + 2              ; 다음 명령어 주소 저장
2. R11 ← RFLAGS               ; 플래그 저장
3. RFLAGS ← RFLAGS & ~SFMASK  ; IF 등 클리어
4. CPL ← 0                    ; Ring 0으로
5. CS ← __KERNEL_CS           ; 커널 코드 세그먼트
6. RIP ← entry_SYSCALL_64     ; 커널 진입점
```

이제 CPU는 커널 모드에서 `entry_SYSCALL_64` 함수를 실행한다.

### 단계 2: entry_SYSCALL_64 진입

`entry_SYSCALL_64`는 어셈블리로 작성된 저수준 진입점이다.

```asm
; arch/x86/entry/entry_64.S
ENTRY(entry_SYSCALL_64)
    UNWIND_HINT_EMPTY

    /*
     * 커널 스택으로 전환
     * syscall은 RSP를 바꾸지 않으므로, 여기서 수동으로 전환
     */
    swapgs                      ; GS 레지스터를 커널용으로 전환
    movq %rsp, PER_CPU_VAR(cpu_tss_rsp0)  ; 사용자 RSP 저장
    movq PER_CPU_VAR(cpu_current_top_of_stack), %rsp  ; 커널 스택으로 전환

    /* pt_regs 구조체를 스택에 구성 */
    pushq $__USER_DS                    ; SS
    pushq PER_CPU_VAR(cpu_tss_rsp0)     ; RSP (사용자)
    pushq %r11                          ; RFLAGS
    pushq $__USER_CS                    ; CS
    pushq %rcx                          ; RIP (복귀 주소)

    pushq %rax                          ; orig_rax (시스템 콜 번호)

    /* 나머지 레지스터 저장 */
    PUSH_REGS                           ; 범용 레지스터 저장

    /* 시스템 콜 번호 검증 */
    cmpq $__NR_syscall_max, %rax
    ja   1f                             ; 범위 밖이면 에러 처리

    /* 시스템 콜 핸들러 호출 */
    movq sys_call_table(, %rax, 8), %rax    ; 함수 포인터 로드
    call *%rax                              ; 시스템 콜 실행

    movq %rax, RAX(%rsp)                    ; 반환값을 pt_regs에 저장

    /* 복귀 준비 */
    POP_REGS                                ; 레지스터 복원

    swapgs                                  ; GS 복원
    sysretq                                 ; 사용자 공간으로 복귀

1:  /* 에러 처리 */
    movq $-ENOSYS, RAX(%rsp)
    jmp return_from_syscall
END(entry_SYSCALL_64)
```

#### swapgs 명령어

**swapgs**는 x86-64 전용 명령어로, GS 레지스터와 MSR_KERNEL_GS_BASE를 교환한다.

왜 필요한가?
- 사용자 공간에서는 GS가 TLS(Thread-Local Storage)를 가리킨다
- 커널 공간에서는 GS가 per-CPU 데이터를 가리킨다
- `swapgs`로 빠르게 전환할 수 있다

```text
사용자 모드:
    GS → TLS 영역
    MSR_KERNEL_GS_BASE → per-CPU 데이터

swapgs 실행:

커널 모드:
    GS → per-CPU 데이터
    MSR_KERNEL_GS_BASE → TLS 영역
```

### 단계 3: pt_regs 구조체 구성

`entry_SYSCALL_64`는 스택에 **pt_regs** 구조체를 구성한다. 이는 모든 레지스터 상태를 보존한다.

```c
// arch/x86/include/asm/ptrace.h
struct pt_regs {
    unsigned long r15;
    unsigned long r14;
    unsigned long r13;
    unsigned long r12;
    unsigned long rbp;
    unsigned long rbx;
    /* 인자들 */
    unsigned long r11;
    unsigned long r10;
    unsigned long r9;
    unsigned long r8;
    unsigned long rax;
    unsigned long rcx;
    unsigned long rdx;
    unsigned long rsi;
    unsigned long rdi;
    /* 시스템 콜 관련 */
    unsigned long orig_rax;  // 시스템 콜 번호
    /* 복귀 정보 */
    unsigned long rip;       // 복귀 주소
    unsigned long cs;        // 코드 세그먼트
    unsigned long eflags;    // RFLAGS
    unsigned long rsp;       // 사용자 스택 포인터
    unsigned long ss;        // 스택 세그먼트
};
```

이 구조체는 다음 용도로 사용된다:
- 시그널 처리
- ptrace (디버거)
- 스케줄러 (컨텍스트 스위칭)
- 시스템 콜 재시작

### 단계 4: 시스템 콜 번호 검증

```asm
cmpq $__NR_syscall_max, %rax
ja   1f
```

RAX에 저장된 시스템 콜 번호가 유효한 범위인지 확인한다. 범위를 벗어나면 `-ENOSYS` 에러를 반환한다.

`__NR_syscall_max`는 컴파일 타임에 결정된다:

```c
// arch/x86/entry/syscall_64.c
#define __NR_syscall_max 547  // 예시 값 (커널 버전에 따라 다름; 5.15는 448 부근)
```

### 단계 5: sys_call_table 조회

```asm
movq sys_call_table(, %rax, 8), %rax
```

이 한 줄의 어셈블리는 다음을 수행한다:

```c
// C로 표현하면:
rax = sys_call_table[rax];
```

- `sys_call_table`: 시스템 콜 테이블의 베이스 주소
- `%rax`: 시스템 콜 번호 (1 = write)
- `8`: 포인터 크기 (64비트)

결과: RAX에 `sys_write` 함수의 주소가 로드된다.

### 단계 6: 시스템 콜 핸들러 실행

```asm
call *%rax
```

`sys_write` 함수를 호출한다. 이제 C 코드 영역으로 진입한다.

```c
// fs/read_write.c
SYSCALL_DEFINE3(write, unsigned int, fd, const char __user *, buf,
                size_t, count)
{
    struct fd f = fdget_pos(fd);
    ssize_t ret = -EBADF;

    if (f.file) {
        loff_t pos = file_pos_read(f.file);
        ret = vfs_write(f.file, buf, count, &pos);
        if (ret >= 0)
            file_pos_write(f.file, pos);
        fdput_pos(f);
    }

    return ret;
}
```

`sys_write`는 다음 작업을 수행한다:

**1) 파일 디스크립터 검증**
```c
struct fd f = fdget_pos(fd);
```
`fd`가 유효한지, 쓰기 권한이 있는지 확인한다.

**2) VFS 계층 호출**
```c
ret = vfs_write(f.file, buf, count, &pos);
```
VFS(Virtual File System)를 통해 실제 파일 시스템으로 요청을 전달한다.

**3) 파일 위치 업데이트**
```c
file_pos_write(f.file, pos);
```
쓰기 성공 시 파일 오프셋을 갱신한다.

#### vfs_write의 동작

```c
// fs/read_write.c
ssize_t vfs_write(struct file *file, const char __user *buf,
                  size_t count, loff_t *pos)
{
    ssize_t ret;

    /* 권한 검사 */
    if (!(file->f_mode & FMODE_WRITE))
        return -EBADF;
    if (!(file->f_mode & FMODE_CAN_WRITE))
        return -EINVAL;

    /* 사용자 공간 포인터 검증 */
    if (unlikely(!access_ok(buf, count)))
        return -EFAULT;

    /* 파일 시스템별 write 함수 호출 */
    if (file->f_op->write)
        ret = file->f_op->write(file, buf, count, pos);
    else if (file->f_op->write_iter)
        ret = new_sync_write(file, buf, count, pos);
    else
        ret = -EINVAL;

    /* 보안 후킹 (LSM) */
    if (ret > 0) {
        fsnotify_modify(file);
        add_wchar(current, ret);
    }
    inc_syscw(current);

    return ret;
}
```

### 단계 7: 반환값 저장

```asm
movq %rax, RAX(%rsp)
```

시스템 콜 핸들러가 반환한 값을 스택의 `pt_regs.rax`에 저장한다. 나중에 레지스터를 복원할 때, 이 값이 RAX에 로드되어 사용자 공간으로 전달된다.

### 단계 8: 레지스터 복원

```asm
POP_REGS
```

`PUSH_REGS`로 저장했던 모든 레지스터를 복원한다:

```asm
; 매크로 확장
popq %r15
popq %r14
popq %r13
popq %r12
popq %rbp
popq %rbx
popq %r11
popq %r10
popq %r9
popq %r8
popq %rax     ; 반환값이 들어있음
popq %rcx     ; 복귀 주소
popq %rdx
popq %rsi
popq %rdi
```

### 단계 9: sysretq로 복귀

```asm
swapgs                  ; GS를 사용자용으로 복원
sysretq                 ; 사용자 공간으로 복귀
```

`sysretq` 명령어는:
```text
1. RIP ← RCX            ; 복귀 주소 복원
2. RFLAGS ← R11         ; 플래그 복원
3. CPL ← 3              ; Ring 3으로
4. CS ← __USER_CS       ; 사용자 코드 세그먼트
5. SS ← __USER_DS       ; 사용자 데이터 세그먼트
```

이제 CPU는 다시 사용자 모드로 돌아왔고, glibc의 `syscall` 명령어 다음 줄을 실행한다.

### 단계 10: glibc로 복귀

```c
// glibc/sysdeps/unix/sysv/linux/write.c
ssize_t write(int fd, const void *buf, size_t nbytes)
{
    long ret;
    // syscall 명령어가 실행됨
    ret = /* RAX의 값 */;

    // 에러 처리
    if ((unsigned long)ret >= (unsigned long)-4095L) {
        errno = -ret;
        return -1;
    }

    return ret;
}
```

### 전체 흐름 요약

```text
[사용자 공간]
write(1, "Hello\n", 6)
    ↓
glibc 래퍼
    ↓
RAX=1, RDI=1, RSI="Hello\n", RDX=6
    ↓
syscall 명령어
    ↓
===========================================
[커널 공간]
    ↓
entry_SYSCALL_64
    ↓
swapgs + 스택 전환
    ↓
pt_regs 구성
    ↓
시스템 콜 번호 검증
    ↓
sys_call_table[1] → sys_write
    ↓
sys_write 실행
    ↓
vfs_write
    ↓
파일 시스템 드라이버 (ext4_write, etc.)
    ↓
블록 디바이스 드라이버
    ↓
하드웨어 (디스크/SSD)
    ↓
반환값을 RAX에 저장
    ↓
레지스터 복원
    ↓
sysretq
    ↓
===========================================
[사용자 공간]
    ↓
glibc로 복귀
    ↓
errno 설정 (에러 시)
    ↓
애플리케이션으로 반환
```

## 7. 매개변수 전달: 레지스터 컨벤션

### 왜 레지스터를 사용하는가?

시스템 콜은 스택이 아닌 **레지스터**로 인자를 전달한다. 그 이유는:

**1) 고정된 ABI와 검증 경계**
정해진 수의 scalar 인자를 register에 두면 entry code가 user stack layout을 해석하거나 fault 가능한 user memory에서 먼저 읽을 필요가 없다. register rename과 dependency 때문에 register access 자체도 항상 “1 cycle”이라는 단일 비용으로 표현되지 않는다.

**2) 스택 전환 문제**
`syscall` 명령어는 스택을 자동으로 전환하지 않는다. 사용자 스택을 그대로 사용하면 보안 문제가 발생한다.

**3) 단순성**
레지스터는 CPU에 내장되어 있어 접근이 간단하다.

### x86-64 시스템 콜 컨벤션

| 인자 위치 | 레지스터 | 비고 |
|---------|---------|------|
| 시스템 콜 번호 | RAX | 필수 |
| 1번째 인자 | RDI | |
| 2번째 인자 | RSI | |
| 3번째 인자 | RDX | |
| 4번째 인자 | R10 | RCX 대신! |
| 5번째 인자 | R8 | |
| 6번째 인자 | R9 | |
| 반환값 | RAX | |
| 복귀 주소 | RCX | syscall이 저장 |
| RFLAGS | R11 | syscall이 저장 |

**중요**: 일반 함수 호출(ABI)과 다르게, 4번째 인자가 **R10**인 점에 유의하라. 이는 `syscall` 명령어가 RCX를 복귀 주소로 사용하기 때문이다.

### 일반 함수 호출(ABI)과의 차이

System V AMD64 ABI (일반 함수 호출):
```text
RDI, RSI, RDX, RCX, R8, R9, (스택)
```

Linux 시스템 콜:
```text
RDI, RSI, RDX, R10, R8, R9
```

**차이점**:
- 4번째 인자: RCX → R10
- 7번째 이상 인자: 불가능 (시스템 콜은 최대 6개)

### 왜 6개로 제한되는가?

**1) syscall ABI의 레지스터 계약**
x86-64가 범용 레지스터 16개를 가진다는 사실에서 6이라는 수를 뺄셈으로 유도할 수는 없다. Linux x86-64 syscall ABI가 번호·반환값에 RAX를 쓰고 인자 레지스터를 RDI, RSI, RDX, R10, R8, R9 여섯 개로 정했기 때문에 최대 인자가 6개다. `SYSCALL`은 RCX와 R11을 덮어쓰며, RBX·RBP·R12~R15의 일반 함수 호출 보존 규칙과 syscall 인자 수는 별개다.

**2) 설계 철학**
"인자가 많다면 구조체를 쓰라"는 Unix 철학을 따른다.

**3) 성능**
레지스터 수가 많아지면 저장/복원 오버헤드가 증가한다.

### 7개 이상의 인자가 필요하면?

구조체 포인터를 사용한다.

**나쁜 예** (불가능):
```c
long my_syscall(int a, int b, int c, int d, int e, int f, int g);
```

**좋은 예**:
```c
struct my_args {
    int a, b, c, d, e, f, g;
};

long my_syscall(struct my_args __user *args);
```

실제 예: `clone3` 시스템 콜
```c
// kernel/fork.c
SYSCALL_DEFINE2(clone3, struct clone_args __user *, uargs, size_t, size)
{
    struct kernel_clone_args kargs;

    /* 버전별 최소 크기, 확장 영역의 0, 포인터 배열까지 검증·변환한다. */
    int err = copy_clone_args_from_user(&kargs, uargs, size);
    if (err)
        return err;

    return kernel_clone(&kargs);
}
```

### 인자 타입과 크기

x86-64 syscall ABI는 인자를 64비트 범용 레지스터로 전달하지만, 각 시스템 콜의 선언 타입과 ABI가 값의 의미를 결정한다. 32비트 signed 인자는 부호 확장 의미를 보존해야 하고 unsigned 인자는 zero extension을 사용한다. compat syscall은 32비트 포인터와 구조체 레이아웃도 별도로 변환한다.

```c
// 예: open 시스템 콜
SYSCALL_DEFINE3(open, const char __user *, filename,
                int, flags, umode_t, mode)
```

- `filename`: 64비트 포인터
- `flags`: 선언은 `int`이며 wrapper가 ABI 값에서 해당 타입으로 변환
- `mode`: `umode_t`가 표현하는 권한 비트만 유효

### 포인터 인자와 __user 어노테이션

커널로 전달되는 포인터는 **__user** 어노테이션을 붙인다:

```c
const char __user *buf
```

이는 스파스(Sparse) 정적 분석 도구에게 "이 포인터는 사용자 공간을 가리킨다"고 알려준다.

#### 왜 중요한가?

커널은 사용자 포인터를 직접 역참조할 수 없다:

```c
// 잘못된 코드
char c = *buf;  // 버그! 사용자 포인터를 직접 접근
```

올바른 방법:

```c
// 올바른 코드
char c;
if (copy_from_user(&c, buf, 1))
    return -EFAULT;
```

`copy_from_user`는 다음을 수행한다:
- 주소가 사용자 공간 범위인지 검증
- 페이지 폴트 처리 (페이지가 스왑 아웃되었을 수 있음)
- 안전하게 데이터 복사

### 관찰: 커널 모듈에서 시스템 콜 인자 확인

다음 커널 모듈은 시스템 콜 인자를 로깅한다:

```c
// syscall_logger.c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>

static int handler_pre(struct kprobe *p, struct pt_regs *regs)
{
    pr_info("sys_write called:\n");
    pr_info("  RAX (syscall#) = %lu\n", regs->orig_rax);
    pr_info("  RDI (fd)       = %lu\n", regs->di);
    pr_info("  RSI (buf)      = 0x%lx\n", regs->si);
    pr_info("  RDX (count)    = %lu\n", regs->dx);

    return 0;
}

static struct kprobe kp = {
    .symbol_name = "sys_write",
    .pre_handler = handler_pre,
};

static int __init logger_init(void)
{
    register_kprobe(&kp);
    pr_info("Kprobe registered\n");
    return 0;
}

static void __exit logger_exit(void)
{
    unregister_kprobe(&kp);
    pr_info("Kprobe unregistered\n");
}

module_init(logger_init);
module_exit(logger_exit);
MODULE_LICENSE("GPL");
```

테스트:
```bash
make
sudo insmod syscall_logger.ko
echo "test" > /tmp/test.txt
dmesg | tail
```

출력:
```text
sys_write called:
  RAX (syscall#) = 1
  RDI (fd)       = 1
  RSI (buf)      = 0x7ffc12345678
  RDX (count)    = 5
```

## 8. 커널 스택과 사용자 스택

### 왜 두 개의 스택이 필요한가?

각 프로세스는 **두 개의 스택**을 가진다:

**1) 사용자 스택 (User Stack)**
- 사용자 공간에 위치
- 크기: process의 `RLIMIT_STACK`, executable/runtime와 thread 생성 정책에 따름. 현재 shell limit은 `ulimit -s`로 확인
- 용도: 일반 함수 호출, 지역 변수

**2) 커널 스택 (Kernel Stack)**
- 커널 공간에 위치
- 크기: 사용자 스택보다 작고 고정된 compile-time 값. 흔한 x86-64 Linux 구성에서는 16KiB지만 아키텍처·구성에 따라 다름
- 용도: 시스템 콜, 인터럽트 처리

#### 왜 분리하는가?

**보안**
사용자 스택을 커널에서 사용하면:
- 사용자가 스택을 조작해 권한 상승 가능
- 스택 오버플로우 공격에 취약

**안정성**
사용자 프로그램이 스택을 소진해도, 커널은 안전하게 동작한다.

**격리**
프로세스가 여러 개여도, 커널 스택은 독립적이다.

### 커널 스택의 구조

각 프로세스의 `task_struct`는 커널 스택을 가리킨다:

```c
// include/linux/sched.h
struct task_struct {
    // ...
    void *stack;  // 커널 스택의 베이스
    // ...
};
```

#### 커널 스택 레이아웃

x86-64에서 커널 스택은 다음과 같이 구성된다:

```text
[높은 주소]
+-------------------------+
|   thread_info (옛날)    | ← 최근 커널에서는 task_struct로 이동
+-------------------------+
|      (사용 가능)        |
|                         |
|      커널 스택          | ← 아래로 성장
|                         |
|                         |
+-------------------------+
|     pt_regs             | ← 시스템 콜/인터럽트 시 생성
+-------------------------+ ← RSP (스택 포인터)
[낮은 주소]
```

#### 커널 스택 크기 예시

```c
// 한 x86-64 Linux 구성의 개념 예
#define THREAD_SIZE_ORDER 2  // 4 페이지
#define THREAD_SIZE (PAGE_SIZE << THREAD_SIZE_ORDER)  // 16KB
```

**왜 작은가?**
- 커널 실행 중 임의로 확장할 수 없는 제한 자원이다
- 시스템에 프로세스가 수천 개 있을 수 있음
- 깊은 재귀와 큰 지역 배열을 금지하고 stack usage를 정적으로 검사하기 쉬움

이 값은 Linux ABI가 아니다. 실제 크기와 vmapped stack 사용 여부는 대상 kernel build에서 확인한다.

### 스택 전환 과정

시스템 콜 진입 시 스택 전환:

```asm
; entry_SYSCALL_64에서
swapgs  ; GS를 커널용으로 전환

; 현재 사용자 스택 포인터 저장
movq %rsp, PER_CPU_VAR(cpu_tss_rsp0)

; 커널 스택으로 전환
movq PER_CPU_VAR(cpu_current_top_of_stack), %rsp
```

**TSS (Task State Segment)**:
- CPU별 데이터 구조
- 커널 스택 포인터 저장
- 스택 전환 시 사용

```c
// arch/x86/include/asm/processor.h
struct tss_struct {
    // ...
    unsigned long sp0;  // Ring 0 스택 포인터
    // ...
} __aligned(PAGE_SIZE);

DECLARE_PER_CPU(struct tss_struct, cpu_tss);
```

### 스택 오버플로우 방지

커널 스택은 작으므로, 오버플로우를 조심해야 한다.

#### 방지 기법

**1) 스택 가드 (CONFIG_VMAP_STACK)**
커널 스택을 vmalloc 영역에 할당하고, 끝에 가드 페이지를 둔다:

```text
+-------------------+
|  커널 스택 (크기는 build별) |
+-------------------+
|  가드 영역 (구성 시) | ← 넘침 탐지
+-------------------+
```

오버플로우 발생 시:
```text
general protection fault: 0000 [#1] SMP
Kernel stack overflow detected.
```

**2) 스택 사용량 경고**
```c
// include/linux/sched/task_stack.h
static inline unsigned long stack_not_used(struct task_struct *p)
{
    unsigned long *end = end_of_stack(p);
    unsigned long *stack = task_stack_page(p);

    while (*stack == STACK_MAGIC)
        stack++;

    return (end - stack) * sizeof(*stack);
}
```

`/proc/<pid>/stack`은 커널 call stack을 보여 주는 관측 인터페이스이지 사용한 stack byte 수를 제공하지 않는다. 실제 stack 사용량은 `stack_not_used()` 같은 watermark 계측, `CONFIG_DEBUG_STACK_USAGE`, trace와 대상 아키텍처의 stack 진단 기능으로 측정한다.

**3) 큰 배열 금지**
커널 함수에서 큰 지역 변수는 금지된다:

```c
// 나쁜 예
void kernel_func(void)
{
    char buf[8192];  // 제한된 kernel stack에서 지나치게 큰 지역 배열
}

// 좋은 예
void kernel_func(void)
{
    char *buf = kmalloc(8192, GFP_KERNEL);
    // ...
    kfree(buf);
}
```

### per-CPU 변수와 스택

`PER_CPU_VAR` 매크로는 CPU별 변수를 참조한다:

```c
// include/linux/percpu-defs.h
#define PER_CPU_VAR(var) %gs:var
```

예:
```asm
movq PER_CPU_VAR(cpu_current_top_of_stack), %rsp
```

이는 다음과 같이 번역된다:
```text
RSP = GS:[cpu_current_top_of_stack의 오프셋]
```

GS 레지스터는 현재 CPU의 per-CPU 영역을 가리킨다. 이를 통해 락 없이 CPU별 데이터에 접근할 수 있다.

### 스택 백트레이스

커널 패닉 시, 스택 백트레이스가 출력된다:

```text
Call Trace:
 dump_stack+0x5c/0x80
 warn_slowpath_common+0x8a/0xc0
 sys_write+0x123/0x140
 entry_SYSCALL_64_after_hwframe+0x44/0xa9
```

이는 다음 방법으로 생성된다:

```c
// arch/x86/kernel/dumpstack.c
void dump_stack(void)
{
    unsigned long bp;
    unsigned long stack;

    asm("movq %%rbp, %0" : "=r" (bp));
    stack = (unsigned long)&stack;

    show_stack(NULL, NULL, KERN_EMERG);
}
```

RBP (Frame Pointer) 체인을 따라가며 호출 스택을 재구성한다.

### 관찰: 커널 스택 크기 확인

```c
// stack_size.c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>

static int __init stack_init(void)
{
    pr_info("THREAD_SIZE = %lu bytes\n", THREAD_SIZE);
    pr_info("Current kernel stack at %px\n", current->stack);
    pr_info("Stack usage: %lu bytes\n",
            THREAD_SIZE - stack_not_used(current));

    return 0;
}

static void __exit stack_exit(void) {}

module_init(stack_init);
module_exit(stack_exit);
MODULE_LICENSE("GPL");
```

출력:
```text
THREAD_SIZE = 16384 bytes
Current kernel stack at ffff8880012a8000
Stack usage: 2048 bytes
```

## 9. vDSO: 시스템 콜 없이 커널 기능 사용

### vDSO란?

**vDSO (Virtual Dynamic Shared Object)**는 사용자 공간에 매핑된 특수한 공유 라이브러리다. 커널이 제공하지만, **사용자 모드에서 실행**된다.

목적: 자주 호출되는 시스템 콜을 Ring 0 전환 없이 실행하여 성능을 극대화한다.

#### vDSO로 제공되는 함수

- `gettimeofday` - 현재 시간 조회
- `clock_gettime` - 고해상도 시간 조회
- `time` - 초 단위 시간 조회
- `getcpu` - 현재 CPU 번호 조회

### 왜 vDSO가 필요한가?

`clock_gettime`처럼 상태를 변경하지 않는 일부 질의는 커널이 사용자 공간에 게시한 시간 변환 파라미터와 하드웨어 counter를 읽어 계산할 수 있다. 이 경우 entry/exit, 사용자 포인터 검증과 scheduler-visible 커널 실행을 피한다. 다만 함수 종류와 clock source에 따라 vDSO가 지원되지 않거나 내부에서 실제 시스템 콜로 폴백할 수 있다.

### vDSO의 동작 원리

#### 커널이 vDSO를 매핑

프로세스 생성 시, 커널은 vDSO를 메모리에 매핑한다:

```c
// arch/x86/entry/vdso/vma.c
static int __init init_vdso(void)
{
    /* vDSO 이미지를 메모리에 로드 */
    vdso_image_64.data = ...;
    vdso_image_64.size = ...;

    return 0;
}
```

#### 매핑 위치 확인

```bash
cat /proc/self/maps | grep vdso
```

출력:
```text
7ffff7ffa000-7ffff7ffc000 r-xp 00000000 00:00 0  [vdso]
```

#### vDSO 함수 확인

```bash
ldd $(which ls) | grep vdso
```

출력:
```text
linux-vdso.so.1 (0x00007ffff7ffa000)
```

vDSO 내부 함수 목록:
```bash
# 현재 프로세스에서 vDSO 이미지를 덤프하거나 debugger로 매핑된
# linux-vdso.so.1의 dynamic symbol table을 확인한다.
gdb -q /bin/true -ex 'starti' -ex 'info functions __vdso_' -ex quit
```

```text
__vdso_gettimeofday
__vdso_time
__vdso_clock_gettime
__vdso_getcpu
```

### vDSO의 구현

#### gettimeofday의 vDSO 버전

```c
// arch/x86/entry/vdso/vclock_gettime.c
notrace int __vdso_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    if (likely(tv != NULL)) {
        struct timespec ts;

        /* 공유 메모리에서 시간 읽기 (시스템 콜 없음!) */
        if (do_realtime(&ts) == VCLOCK_NONE)
            return vdso_fallback_gettimeofday(tv, tz);

        tv->tv_sec = ts.tv_sec;
        tv->tv_usec = ts.tv_nsec / 1000;
    }

    if (unlikely(tz != NULL)) {
        tz->tz_minuteswest = gtod->tz_minuteswest;
        tz->tz_dsttime = gtod->tz_dsttime;
    }

    return 0;
}
```

**핵심**: 커널이 주기적으로 업데이트하는 **공유 메모리 영역**에서 시간을 읽는다.

#### 공유 데이터 구조

```c
// arch/x86/include/asm/vgtod.h
struct vgtod_ts {
    u64 sec;
    u64 nsec;
};

struct vdso_data {
    int vclock_mode;
    u64 cycle_last;
    u64 mask;
    u32 mult;
    u32 shift;

    struct vgtod_ts realtime;    // 실시간 시계
    struct vgtod_ts monotonic;   // 단조 시계

    // ...
};
```

커널은 타이머 인터럽트마다 이 구조체를 업데이트한다:

```c
// kernel/time/timekeeping.c
static void update_vsyscall(struct timekeeper *tk)
{
    struct vdso_data *vdata = __arch_get_k_vdso_data();

    vdata->cycle_last = tk->tkr_mono.cycle_last;
    vdata->mask = tk->tkr_mono.mask;
    vdata->mult = tk->tkr_mono.mult;
    vdata->shift = tk->tkr_mono.shift;

    vdata->realtime.sec = tk->xtime_sec;
    vdata->realtime.nsec = tk->tkr_mono.xtime_nsec;

    // ...
}
```

### vDSO vs 일반 시스템 콜

#### 일반 시스템 콜 (sys_gettimeofday)

```c
SYSCALL_DEFINE2(gettimeofday, struct timeval __user *, tv,
                struct timezone __user *, tz)
{
    if (likely(tv != NULL)) {
        struct timespec64 ts;
        ktime_get_real_ts64(&ts);

        if (put_user(ts.tv_sec, &tv->tv_sec) ||
            put_user(ts.tv_nsec / 1000, &tv->tv_usec))
            return -EFAULT;
    }

    // ...
    return 0;
}
```

일반 시스템 콜 경로에는 entry/exit, 레지스터 상태와 보안 경계 처리, 사용자 메모리 검증, 실제 시간 조회가 포함된다. 각 비용은 겹치거나 CPU·커널 설정에 따라 달라지므로 구성 요소를 고정 사이클로 단순 합산하지 않는다.

#### vDSO (사용자 모드)

```c
int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    do {
        seq = read_begin(vdata);          /* 커널 갱신과 일관된 snapshot */
        cycles = read_clocksource();      /* 예: invariant TSC */
        base = read_time_parameters(vdata);
        result = convert_cycles(base, cycles);
    } while (read_retry(vdata, seq));

    return copy_result_in_user_mode(tv, result);
}
```

실제 구현은 clock mode와 아키텍처에 따라 다르다. 핵심은 단순히 1ms마다 갱신된 값을 복사하는 것이 아니라, 커널이 게시한 기준값과 현재 hardware counter의 차이를 조합하면서 sequence counter로 일관성을 검증한다는 점이다.

### vDSO의 한계

**1) 제한된 기능**
모든 시스템 콜이 vDSO로 구현될 수 없다. 하드웨어 접근이나 권한 변경이 필요한 경우 일반 시스템 콜을 사용해야 한다.

**2) clock source와 폴백**
vDSO가 안전하게 읽을 수 없는 clock source를 사용하거나 요청한 clock ID를 지원하지 않으면 시스템 콜로 폴백할 수 있다. vDSO라는 이유만으로 시간 해상도가 timer tick으로 제한되지는 않는다.

**3) 보안 고려사항**
vDSO는 읽기 전용이지만, 일부 공격 벡터가 존재한다 (ASLR 우회 등).

### 측정: vDSO 성능

```c
// vdso_bench.c
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <x86intrin.h>

#define ITERATIONS 10000000

static inline unsigned long long rdtsc_begin(void)
{
    _mm_lfence();
    return __rdtsc();
}

static inline unsigned long long rdtsc_end(void)
{
    unsigned int aux;
    unsigned long long t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}

int main(void)
{
    struct timeval tv;
    unsigned long long start, end;
    int i;

    /* vDSO 버전 (glibc 래퍼) */
    for (i = 0; i < 10000; ++i) gettimeofday(&tv, NULL); /* warm-up */
    start = rdtsc_begin();
    for (i = 0; i < ITERATIONS; i++) {
        gettimeofday(&tv, NULL);
    }
    end = rdtsc_end();
    printf("gettimeofday (glibc path): %llu cycles per call\n",
           (end - start) / ITERATIONS);

    /* 직접 시스템 콜 호출 */
    start = rdtsc_begin();
    for (i = 0; i < ITERATIONS; i++) {
        syscall(SYS_gettimeofday, &tv, NULL);
    }
    end = rdtsc_end();
    printf("gettimeofday (forced syscall): %llu cycles per call\n",
           (end - start) / ITERATIONS);

    return 0;
}
```

컴파일 및 실행:
```bash
gcc -O2 -march=native vdso_bench.c -o vdso_bench
./vdso_bench
```

측정 전에는 프로세스를 한 CPU에 고정하고, 여러 표본의 중앙값과 분위수를 기록하며, `strace` 또는 `perf trace`로 glibc 경로가 실제 시스템 콜로 폴백하지 않았는지 확인한다. `RDTSC` 자체의 순서 제약과 측정 오버헤드도 빈 루프로 보정해야 한다. 결과는 CPU·clocksource·커널·보안 완화 설정에 종속되므로 예시 숫자를 이식 가능한 상수로 사용하지 않는다.

### vDSO와 time namespaces

최근 Linux (5.6+)는 **time namespaces**를 도입했다. 이는 vDSO와 함께 작동하여, 컨테이너별로 다른 시간을 제공한다.

```c
// kernel/time/namespace.c
void timens_add_monotonic(struct timespec64 *ts)
{
    struct timens_offsets *ns_offsets = &current->nsproxy->time_ns->offsets;

    ts->tv_sec += ns_offsets->monotonic.tv_sec;
    ts->tv_nsec += ns_offsets->monotonic.tv_nsec;

    if (ts->tv_nsec >= NSEC_PER_SEC) {
        ts->tv_sec++;
        ts->tv_nsec -= NSEC_PER_SEC;
    }
}
```

## 10. 새로운 시스템 콜의 ABI 설계

새 시스템 콜은 단순히 커널 함수를 하나 더 만드는 작업이 아니다. 배포된 번호, 인자 레이아웃, 에러 의미론은 사용자 공간 ABI가 되어 장기간 호환성을 유지해야 한다. 기존 파일 descriptor, `ioctl`, `netlink`, `sysfs` 같은 인터페이스로 모델링할 수 없는 새로운 커널 기능인지 먼저 검토해야 한다.

아래 `hello`는 진입 경로를 설명하기 위한 최소 예일 뿐 좋은 실제 시스템 콜 설계는 아니다. 사용자 문자열을 커널 로그에 쓰는 기능은 사용자 공간에서 해결할 수 있고, rate limit과 로그 주입 문제도 만든다.

### 최소 형태

다음 기능을 가진 시스템 콜을 추가한다:

```c
long sys_hello(const char __user *name);
```

- 인자: 사용자 이름 (문자열)
- 동작: 커널 로그에 "Hello, [name]!" 출력
- 반환값: 문자열 길이

### ABI 번호와 아키텍처

시스템 콜 번호는 대상으로 삼는 커널 트리의 ABI 테이블에서 할당한다.

```bash
# arch/x86/entry/syscalls/syscall_64.tbl의 개념적 형식
<NR>    common    hello    sys_hello
```

`<NR>`은 문서에 고정할 수 없다. 번호 공간에는 이미 할당되었거나 예약된 값이 있고 x86-64, x32, i386 등 ABI별 테이블도 다르다. 실제 변경은 해당 커널 트리의 `Documentation/process/adding-syscalls.rst`와 현재 테이블을 기준으로 충돌 없이 구성해야 한다. 새 UAPI 헤더를 설치하면 사용자 프로그램은 하드 코딩한 숫자 대신 생성된 `__NR_hello`를 사용한다.

### 커널 진입 함수와 사용자 포인터

`kernel/sys.c` 파일에 추가:

```c
// kernel/sys.c
#include <linux/syscalls.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>

SYSCALL_DEFINE1(hello, const char __user *, name)
{
    char kname[256];
    long len;

    /* 사용자 공간에서 커널 공간으로 문자열 복사 */
    len = strncpy_from_user(kname, name, sizeof(kname));
    if (len < 0)
        return len;  /* -EFAULT */

    /* 널 종료 보장 */
    kname[sizeof(kname) - 1] = '\0';

    /* 커널 로그에 출력 */
    printk(KERN_INFO "Hello, %s!\n", kname);

    return len;
}
```

#### 핵심 함수 설명

**strncpy_from_user**:
- 사용자 포인터에서 안전하게 문자열 복사
- 페이지 폴트 처리
- 반환값: 복사된 바이트 수 (에러 시 음수)

```c
// lib/strncpy_from_user.c
long strncpy_from_user(char *dst, const char __user *src, long count)
{
    if (unlikely(count <= 0))
        return 0;

    /* 주소 범위 검증 */
    if (!access_ok(src, count))
        return -EFAULT;

    /* 실제 복사 (페이지 폴트 가능) */
    return do_strncpy_from_user(dst, src, count);
}
```

### 빌드 구성과 미지원 stub

`include/linux/syscalls.h`의 프로토타입만 추가한다고 새 ABI가 완성되지는 않는다.

```c
asmlinkage long sys_hello(const char __user *name);
```

기능이 `CONFIG` 옵션에 의존한다면 `Kconfig`와 `Makefile`, `kernel/sys_ni.c`의 `-ENOSYS` fallback을 함께 설계해야 한다. 32비트 호환 ABI에서 64비트 인자의 정렬이나 구조체 레이아웃이 달라지면 compat entry도 필요하다. 커널 트리 자체 테스트와 UAPI 헤더 갱신도 변경의 일부다.

### 사용자 공간 호출 계약

```c
// test_hello.c
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
#include <errno.h>

#include <asm/unistd.h>  /* 커스텀 커널의 설치된 UAPI 헤더 */

int main(int argc, char *argv[])
{
    long ret;
    const char *name;

    if (argc < 2) {
        name = "World";
    } else {
        name = argv[1];
    }

    /* 시스템 콜 호출 */
    ret = syscall(__NR_hello, name);

    if (ret < 0) {
        perror("sys_hello failed");
        return 1;
    }

    printf("sys_hello returned: %ld\n", ret);
    return 0;
}
```

glibc의 `syscall()` 래퍼는 커널의 `-errno`를 그대로 반환하지 않는다. 오류이면 `-1`을 반환하고 양의 오류 번호를 `errno`에 기록한다. 따라서 성공값과 오류를 구분할 때는 위 코드처럼 `ret == -1`을 검사하고 `errno` 또는 `perror`를 사용한다.

### 에러 처리

사용자 포인터가 NULL이거나 잘못된 주소를 가리키면 어떻게 될까?

```c
// test_invalid.c
#include <sys/syscall.h>
#include <stdio.h>

#include <asm/unistd.h>
#include <errno.h>

int main(void)
{
    long ret;

    /* NULL 포인터 전달 */
    errno = 0;
    ret = syscall(__NR_hello, NULL);
    printf("NULL: ret = %ld, errno = %d\n", ret, errno);

    /* 잘못된 주소 전달 */
    errno = 0;
    ret = syscall(__NR_hello, (void *)0xdeadbeef);
    printf("Invalid: ret = %ld, errno = %d\n", ret, errno);

    return 0;
}
```

출력:
```text
NULL: ret = -1, errno = 14
Invalid: ret = -1, errno = 14
```

커널 내부 반환값은 `-EFAULT`지만 libc 경계에서 위 형태로 변환된다. 어셈블리로 직접 진입하거나 커널 내부 코드를 읽을 때만 raw 음수 errno를 보게 된다.

### 보안 고려사항

**1) 버퍼 오버플로우 방지**
```c
/* 나쁜 예 */
char kname[32];
strncpy_from_user(kname, name, 1024);  /* 버퍼 오버플로우! */

/* 좋은 예 */
char kname[32];
strncpy_from_user(kname, name, sizeof(kname));
kname[sizeof(kname) - 1] = '\0';
```

**2) 정수 오버플로우 검사**
```c
/* 나쁜 예 */
void *buf = kmalloc(count * size, GFP_KERNEL);  /* count * size 오버플로우 가능 */

/* 좋은 예 */
if (check_mul_overflow(count, size, &total))
    return -EINVAL;
void *buf = kmalloc(total, GFP_KERNEL);
```

**3) 권한 검사**
```c
/* 예시: 기능에 정확히 대응하는 capability가 있을 때 */
if (!capable(CAP_SYS_RESOURCE))
    return -EPERM;
```

`CAP_SYS_ADMIN`은 범위가 매우 넓으므로 새 기능의 편의상 기본값처럼 사용하면 안 된다. 가능한 경우 이미 열린 파일 descriptor의 권한, namespace 경계, LSM hook을 이용하고, 정말 필요한 최소 capability를 선택한다.

### 시스템 콜 설계 원칙

커널 커뮤니티의 설계 원칙 (1번은 Torvalds의 유명한 철칙이다):

> **1) Don't break userspace**
> 기존 시스템 콜의 동작을 절대 변경하지 마라.

> **2) System calls are forever**
> 한 번 추가된 시스템 콜은 영원히 유지되어야 한다.

> **3) Keep it simple**
> 복잡한 기능은 여러 개의 단순한 시스템 콜로 분리하라.

> **4) 확장 가능한 구조체와 flags**
> 구조체 크기를 함께 받고, 알 수 없는 flag를 거부하며, 새 필드는 0일 때 기존 의미를 유지한다.

예: `openat2` 시스템 콜
```c
struct open_how {
    __u64 flags;
    __u64 mode;
    __u64 resolve;  /* 향후 확장 가능 */
};

long openat2(int dfd, const char __user *filename,
             struct open_how __user *how, size_t size);
```

크기 인자는 커널이 사용자가 제공한 구조체 버전을 판별하게 한다. 고정 폭 정수와 명시적 padding은 32/64비트 ABI 차이를 줄인다. 포인터가 가리키는 메모리는 호출 중 다른 스레드가 바꿀 수 있으므로 한 번 안전하게 복사한 snapshot을 검증해 사용해야 한다.

### 기존 커널 인터페이스를 이용하는 대안

커널 모듈의 `/proc` 파일은 시스템 콜 테이블을 변경하지 않고도 write 기반 인터페이스를 만들 수 있다. 그러나 이것은 "모듈로 시스템 콜을 추가"하는 것이 아니라 VFS의 `open/write/close` ABI를 재사용하는 별도 설계다.

```c
// hello_module.c
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

static ssize_t hello_write(struct file *file, const char __user *buf,
                           size_t count, loff_t *ppos)
{
    char kname[256];
    size_t len = min(count, sizeof(kname) - 1);

    if (copy_from_user(kname, buf, len))
        return -EFAULT;

    kname[len] = '\0';
    pr_info("Hello, %s!\n", kname);

    return len;
}

static const struct proc_ops hello_ops = {
    .proc_write = hello_write,
};

static int __init hello_init(void)
{
    return proc_create("hello", 0200, NULL, &hello_ops) ? 0 : -ENOMEM;
}

static void __exit hello_exit(void)
{
    remove_proc_entry("hello", NULL);
}

module_init(hello_init);
module_exit(hello_exit);
MODULE_LICENSE("GPL");
```

실제 인터페이스라면 `/proc`가 맞는 데이터인지, 장치 파일·`sysfs`·`configfs`·`netlink` 중 어느 객체 모델이 적합한지 선택해야 한다. 수명 관리, 동시 write, namespace 가시성, 권한과 LSM 검사를 포함해야 하며, 사용자 입력을 그대로 커널 로그에 출력하지 않는다.


---

## 결론

시스템 콜은 운영체제의 근간이다. 사용자 공간과 커널 공간 사이의 명확한 경계이자, 하드웨어 보호의 핵심 메커니즘이다.

이 글에서 다룬 내용은 다음과 같다.

**1) 역사적 진화**
- `int 0x80`: 초기의 느린 방법
- `sysenter/sysexit`: Intel의 최적화
- `syscall/sysret`: 현대의 표준

**2) 하드웨어 메커니즘**
- 특권 레벨 (Ring 0 vs Ring 3)
- MSR 레지스터 (LSTAR, STAR, SFMASK)
- 레지스터 컨벤션 (RAX, RDI, RSI, RDX, R10, R8, R9)

**3) 커널 내부**
- `entry_SYSCALL_64`: 진입점
- `sys_call_table`: 디스패치 테이블
- `pt_regs`: 레지스터 상태 보존
- 스택 전환: 사용자 스택 → 커널 스택

**4) 성능 최적화**
- vDSO: 시스템 콜 없는 커널 기능
- sequence counter와 hardware counter를 이용한 사용자 모드 질의

**5) 실전**
- 시스템 콜 추가 방법
- 보안 고려사항
- 설계 원칙

시스템 콜을 이해하면, 운영체제의 작동 방식을 더 깊이 이해할 수 있다. `strace`의 출력이 더 이상 신비롭지 않고, 성능 프로파일링에서 시스템 콜 오버헤드를 정확히 판단할 수 있으며, 커널 패닉 메시지를 읽고 디버깅할 수 있게 된다.

## 더 읽을거리

### 공식 문서

**1) Intel Software Developer's Manual (SDM)**
- Volume 2: Instruction Set Reference (SYSCALL, SYSRET 설명)
- Volume 3: System Programming Guide (특권 레벨, MSR)
- 다운로드: https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html

**2) Linux 커널 문서**
- `Documentation/x86/entry_64.rst`: Entry 코드 설명
- `Documentation/process/adding-syscalls.rst`: 시스템 콜 추가 가이드
- 온라인: https://www.kernel.org/doc/html/latest/

**3) Linux man-pages**
- `man 2 syscalls`: 모든 시스템 콜 목록
- `man 2 syscall`: 직접 시스템 콜 호출 방법
- 온라인: https://man7.org/linux/man-pages/

### 책

**1) "Linux Kernel Development" - Robert Love**
- 3장: Process Management
- 5장: System Calls
- 가장 읽기 쉬운 커널 입문서

**2) "Understanding the Linux Kernel" - Daniel Bovet, Marco Cesati**
- 10장: System Calls
- 더 깊은 내부 구조 설명

**3) "Linux System Programming" - Robert Love**
- 1장: Introduction and Essential Concepts
- 시스템 콜을 사용하는 방법

**4) "The Linux Programming Interface" - Michael Kerrisk**
- 가장 포괄적인 시스템 프로그래밍 바이블
- 1500페이지 분량

### 온라인 자료

**1) LWN.net 기사**
- "Anatomy of a system call" (2014): https://lwn.net/Articles/604287/
- "Adding a new system call" (2016): https://lwn.net/Articles/604515/
- "The vDSO on arm64" (2015): https://lwn.net/Articles/615809/

**2) Brendan Gregg's Blog**
- Linux Performance: http://www.brendangregg.com/linuxperf.html
- FlameGraphs로 시스템 콜 오버헤드 시각화

**3) 커널 소스 코드**
- Bootlin (크로스 레퍼런스): https://elixir.bootlin.com/linux/latest/source
- 주요 파일:
 - `arch/x86/entry/entry_64.S`: Entry 코드
 - `arch/x86/entry/syscall_64.c`: 시스템 콜 테이블
 - `kernel/sys.c`: 일반 시스템 콜 구현
 - `fs/read_write.c`: 파일 I/O 시스템 콜

**4) YouTube 강의**
- "Understanding the Linux Kernel" by Richard Guy Briggs
- "Linux Kernel Development" by The Linux Foundation

### 관찰 환경

**1) 커널 컴파일 환경 구축**
```bash
# Ubuntu/Debian
sudo apt install build-essential libncurses-dev bison flex \
                 libssl-dev libelf-dev

# 커널 소스 다운로드
wget https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.15.tar.xz
tar xf linux-5.15.tar.xz
cd linux-5.15

# 설정 및 빌드
make menuconfig
make -j$(nproc)
```

**2) QEMU로 커널 테스트**
```bash
# QEMU 설치
sudo apt install qemu-system-x86

# 간단한 initramfs 생성
mkdir initramfs
cd initramfs
mkdir -p bin sbin etc proc sys usr/bin usr/sbin

# busybox 추가
cp /bin/busybox bin/
ln -s /bin/busybox bin/sh

# init 스크립트
cat > init <<EOF
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
exec /bin/sh
EOF
chmod +x init

# initramfs 패키징
find . | cpio -o -H newc | gzip > ../initramfs.cpio.gz

# QEMU로 부팅
qemu-system-x86_64 -kernel ../arch/x86/boot/bzImage \
                   -initrd ../initramfs.cpio.gz \
                   -append "console=ttyS0" -nographic
```

**3) strace로 시스템 콜 추적**
```bash
# 기본 사용
strace ls

# 통계 보기
strace -c ls

# 특정 시스템 콜만 추적
strace -e trace=open,read,write ls

# 시간 측정
strace -T ls

# 하위 프로세스 추적
strace -f bash -c "ls | wc"
```

**4) perf로 성능 분석**
```bash
# 시스템 콜 카운트
perf stat -e 'syscalls:sys_enter_*' ls

# 시스템 콜 추적
sudo perf trace ls

# 프로파일링
sudo perf record -a -g -- sleep 10
sudo perf report
```

### 인접 주제

- eBPF: 시스템 콜 후킹과 필터링, 성능 모니터링, 보안 정책 적용
- Seccomp: 시스템 콜 샌드박스, 컨테이너 보안
- ptrace: 디버거 구현, 시스템 콜 가로채기
- Kernel Live Patching: 재부팅 없이 커널 패치
- Time Namespaces: 컨테이너별 시간 격리, vDSO와의 상호작용

이 글은 Linux 5.15 커널을 기준으로 작성됐다. 최신 코드는 https://kernel.org 에서 확인할 수 있다.
