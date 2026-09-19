## 들어가며

> **핵심 질문**: "KVM은 어떻게 CPU를 속이는가?"


가상 머신(Virtual Machine)은 하나의 물리 서버 위에서 여러 개의 독립적인 운영체제를 실행하는 기술이다. 클라우드 컴퓨팅의 기반이 되었고, 개발 환경, 테스트, 보안 격리의 필수 요소가 되었다.

CPU는 어떻게 guest의 privileged operation과 주소 변환을 host가 통제하도록 실행할 수 있는가? guest가 가상 환경을 반드시 모르는 것은 아니다. hypervisor CPUID bit, paravirtual device와 timing을 통해 탐지할 수 있으며, 관리자가 이를 숨기는 것과 정확한 virtual machine boundary는 다른 문제다. Intel VT-x와 AMD-V는 guest kernel을 별도 실행 상태에서 구동하고 필요한 사건에 host로 제어를 돌려주는 하드웨어 경계를 제공한다.

이 글은 가상화의 역사, KVM의 내부 구조, VT-x의 VMCS와 VM Entry/Exit, EPT 메모리 가상화, virtio I/O 가속, 그리고 중첩 가상화까지 다룬다.


---

## 목차
1. [가상화의 역사: 왜 하드웨어 지원이 필요했는가?](#1-가상화의-역사-왜-하드웨어-지원이-필요했는가)
2. [VT-x: 하드웨어가 제공하는 가상화 지원](#2-vt-x-하드웨어가-제공하는-가상화-지원)
3. [KVM 아키텍처: 리눅스를 하이퍼바이저로](#3-kvm-아키텍처-리눅스를-하이퍼바이저로)
4. [메모리 가상화: EPT/NPT](#4-메모리-가상화-eptnpt)
5. [I/O 가상화: virtio](#5-io-가상화-virtio)
6. [중첩 가상화(Nested Virtualization)](#6-중첩-가상화nested-virtualization)
7. [실전 예제: KVM 벤치마크](#7-실전-예제-kvm-벤치마크)


---

## 1. 가상화의 역사: 왜 하드웨어 지원이 필요했는가?

### 전가상화 (Full Virtualization)

**목표**: 게스트 OS를 수정하지 않고 그대로 실행.

**문제점**: x86 아키텍처는 가상화하기 어렵게 설계되었다.

**예시 - POPF 명령어:**
```asm
POPF  ; 스택에서 FLAGS 레지스터 복원
```

- Ring 0 (커널): FLAGS 레지스터 전체 수정 가능
- Ring 3 (유저): IF (Interrupt Flag) 같은 특권 플래그는 무시됨 (예외 발생 없음!)

이 문제는 하드웨어 가상화 지원이 없던 시기의 trap-and-emulate 또는 binary-translation 방식 가상화의 맥락이다. 그 시절 VMM은 게스트 OS를 Ring 1이나 Ring 3로 강등시킨 채 실행하면서 특권 명령어를 가로채려 했고, `POPF` 같은 명령은 예외를 일으키지 않고 조용히 무시되는 탓에 binary translation이 필요했다.

현대 x86 하드웨어 가상화(VT-x/AMD-V)에서는 이 모델이 더 이상 유효하지 않다. VT-x는 VMX root와 VMX non-root라는 별도 동작 모드를 둔다. 게스트 OS는 VMX non-root 안에서 진짜 Ring 0(guest CPL=0)에서 실행되고, 특권 명령어/IO/CPUID 등 가상화에 영향을 주는 이벤트가 일어나면 CPU가 직접 VMM(VMX root)으로 VM Exit을 일으켜 제어를 넘긴다. 즉 게스트 OS가 "Ring 0인 줄 알면서 사실은 Ring 3"인 것이 아니라, 실제로 별도 모드의 Ring 0에서 동작하고 hypervisor는 다른 모드에서 그걸 감독하는 구조다.

**VMware의 해결책**: Binary Translation

```text
게스트 코드:
    POPF        ; 특권 명령어
    MOV EAX, 1

↓ VMM이 런타임에 변환

변환된 코드:
    CALL vmm_handle_popf  ; 에뮬레이션
    MOV EAX, 1
```

- 실행되는 basic block을 동적으로 번역하고 민감한 명령과 제어 흐름을 안전한 VMM 경로로 바꿈
- 동적 변환 + 코드 캐시 사용
- 오버헤드는 workload의 privileged operation·I/O·page-table 변경 빈도와 번역 cache hit율에 달림. 고정 10~30%로 일반화할 수 없음

### 반가상화 (Paravirtualization)

**Xen (2003)의 접근법**: 게스트 OS를 수정하여 하이퍼바이저와 협력하게 만든다.

**변경 사항:**
```c
// 전통적 리눅스
void enable_interrupts(void) {
    asm volatile("sti");  // 특권 명령어
}

// Xen 반가상화 리눅스
void enable_interrupts(void) {
    HYPERVISOR_enable_interrupts();  // 하이퍼콜
}
```

**하이퍼콜 (Hypercall)**:
- 게스트가 하이퍼바이저에게 요청하는 시스템 콜
- syscall이 커널을 호출하듯, hypercall은 하이퍼바이저 호출

```c
// arch/x86/include/asm/xen/hypercall.h (리눅스 커널)
static inline int HYPERVISOR_set_trap_table(struct trap_info *table) {
    return _hypercall1(int, set_trap_table, table);
}

#define _hypercall1(type, name, a1)                \
({                                                   \
    long __res;                                      \
    asm volatile(                                    \
        "call hypercall_page + (%c1 * 32)"         \
        : "=a" (__res)                              \
        : "i" (__HYPERVISOR_##name), "D" (a1)      \
        : "memory"                                   \
    );                                               \
    (type)__res;                                     \
})
```

**장점**: trap·emulation이 잦은 연산을 명시적 hypercall과 paravirtual driver로 바꿔 비용을 줄일 수 있음
**단점**: guest kernel이나 driver의 지원이 필요하고 hypervisor ABI에 결합됨. Windows도 Hyper-V enlightenment나 paravirtual I/O driver를 사용할 수 있으므로 운영체제 이름으로 가능·불가능을 나누면 안 됨

### 하드웨어 가상화 (Hardware-Assisted Virtualization)

**Intel VT-x (2005), AMD-V (2006)**: CPU가 가상화를 네이티브 지원

**핵심 아이디어**: 새로운 실행 모드 추가

```text
전통적 x86:
    Ring 0 (OS)
    Ring 3 (User)

VT-x 추가:
    VMX root mode (Hypervisor)
      └─ VMX non-root mode
           ├─ Ring 0 (Guest OS) ← 진짜 Ring 0처럼 동작!
           └─ Ring 3 (Guest User)
```

**VM Entry / VM Exit:**
- VM Entry: 하이퍼바이저 → 게스트로 전환
- VM Exit: 게스트가 민감한 동작 수행 시 하이퍼바이저로 trap

**이제 게스트 OS는 수정 없이 Ring 0에서 실행 가능!**

## 2. VT-x: 하드웨어가 제공하는 가상화 지원

### VMCS (Virtual Machine Control Structure)

**VMCS**는 Intel VMX가 한 virtual CPU 실행 context의 guest/host state, execution control과 exit 정보를 보관하는 hardware-defined region이다. VMCS region은 4KiB 정렬이 필요하고 `IA32_VMX_BASIC`이 보고한 크기는 한 4KiB region 안에 들어가지만, “정확히 일반 C 구조체 한 페이지”로 layout을 추측할 수 없다. 한 VM에 여러 vCPU가 있으면 각각의 실행 state가 필요하다. AMD-V는 대응되는 별도 구조인 VMCB를 사용한다.

**포함 정보:**
```c
// Intel SDM Volume 3C, Chapter 24
struct vmcs {
    // Guest State (게스트 CPU 상태)
    uint64_t guest_rip;
    uint64_t guest_rsp;
    uint64_t guest_cr0, guest_cr3;
    uint64_t guest_idtr_base, guest_idtr_limit;
    uint64_t guest_cs_selector, guest_cs_base;
    // ... 모든 레지스터

    // Host State (VM Exit 시 복귀할 호스트 상태)
    uint64_t host_rip;
    uint64_t host_rsp;
    uint64_t host_cr3;
    // ...

    // Execution Control (어떤 이벤트가 VM Exit 유발할지)
    uint32_t pin_based_vm_exec_control;
    uint32_t cpu_based_vm_exec_control;
    uint32_t exception_bitmap;  // 어떤 예외를 intercept할지

    // VM-Exit Control
    uint32_t vm_exit_controls;
    uint32_t vm_exit_reason;      // 왜 VM Exit 발생했는지
    uint64_t exit_qualification;  // 추가 정보

    // VM-Entry Control
    uint32_t vm_entry_controls;
};
```

**VMCS 관리 명령어:**
```asm
VMPTRLD [vmcs_address]  ; 현재 VMCS 설정
VMREAD  reg, field       ; VMCS 필드 읽기
VMWRITE field, reg       ; VMCS 필드 쓰기
VMCLEAR [vmcs_address]   ; VMCS 초기화
```

### VM Entry: 게스트로 진입

```c
// 간소화된 KVM 코드 (arch/x86/kvm/vmx/vmx.c)
static void vmx_vcpu_run(struct kvm_vcpu *vcpu) {
    // 1. 게스트 레지스터 복원
    vcpu->arch.regs[VCPU_REGS_RAX] = ...;

    // 2. VMCS에 게스트 상태 로드
    vmcs_writel(GUEST_RIP, vcpu->arch.regs[VCPU_REGS_RIP]);
    vmcs_writel(GUEST_RSP, vcpu->arch.regs[VCPU_REGS_RSP]);

    // 3. VM Entry
    asm volatile(
        "vmlaunch\n\t"  // 또는 vmresume
        : : : "memory"
    );

    // 이 코드는 VM Exit 후에야 실행됨!
}
```

**VMLAUNCH vs VMRESUME:**
- VMLAUNCH: 새 VM 첫 실행
- VMRESUME: 이미 실행됐던 VM 재개

### VM Exit: 하이퍼바이저로 복귀

**VM Exit가 발생하는 경우:**
```c
// Intel SDM Volume 3C, Appendix C
enum vm_exit_reason {
    EXIT_REASON_EXCEPTION_NMI = 0,
    EXIT_REASON_EXTERNAL_INTERRUPT = 1,
    EXIT_REASON_TRIPLE_FAULT = 2,
    EXIT_REASON_CPUID = 10,           // CPUID 명령어
    EXIT_REASON_HLT = 12,             // HLT 명령어
    EXIT_REASON_INVLPG = 14,          // TLB flush
    EXIT_REASON_VMCALL = 18,          // 게스트의 하이퍼콜
    EXIT_REASON_CR_ACCESS = 28,       // CR0/CR3/CR4 접근
    EXIT_REASON_IO_INSTRUCTION = 30,  // IN/OUT 명령어
    EXIT_REASON_MSR_READ = 31,
    EXIT_REASON_MSR_WRITE = 32,
    EXIT_REASON_EPT_VIOLATION = 48,   // 메모리 접근 위반
    // ...
};
```

**VM Exit 처리 루프:**
```c
// arch/x86/kvm/x86.c
int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu) {
    for (;;) {
        if (vcpu->arch.mp_state == KVM_MP_STATE_RUNNABLE)
            r = vcpu_enter_guest(vcpu);  // VM Entry

        // VM Exit 발생 - 이유 확인
        switch (vcpu->run->exit_reason) {
        case KVM_EXIT_IO:
            r = handle_io(vcpu);
            break;
        case KVM_EXIT_MMIO:
            r = handle_mmio(vcpu);
            break;
        case KVM_EXIT_HLT:
            vcpu->arch.mp_state = KVM_MP_STATE_HALTED;
            r = 0;  // 유저스페이스로 복귀
            break;
        // ...
        }

        if (r <= 0)
            break;
    }
    return r;
}
```

**성능 고려사항:**
- VM Exit/Entry 비용은 CPU 세대, exit reason, 보안 완화와 VMM 처리 경로에 따라 수백~수천 사이클 이상으로 크게 달라질 수 있다.
- MMIO emulation처럼 사용자 공간 VMM까지 왕복하면 단순한 커널 내 exit보다 훨씬 길어질 수 있다.
- 최적화 목표는 exit 횟수뿐 아니라 exit reason별 처리 비용과 batch 가능성을 줄이는 것이다.

## 3. KVM 아키텍처: 리눅스를 하이퍼바이저로

### KVM과 QEMU의 역할 분리

KVM과 QEMU는 같은 물건이 아니다. **KVM**은 리눅스 커널 자체에 내장된 가상화 모듈이며, `/dev/kvm` 인터페이스를 통해 사용자 공간 프로그램이 VT-x/AMD-V를 손쉽게 다룰 수 있게 해주는 API다. CPU 가상화와 EPT/NPT 같은 하드웨어 지원을 추상화하지만, 커널 안에서 에뮬레이션하는 인터럽트 컨트롤러(PIC/IOAPIC/LAPIC)와 PIT를 빼면 I/O 디바이스 모델은 가지고 있지 않다. **QEMU**는 그 위에서 실제 VM을 실행하는 대표적인 사용자 공간 VMM이다. 디스크/네트워크/그래픽 디바이스를 에뮬레이션하거나 virtio로 페어링하고, KVM이 노출한 vCPU에 명령을 내려 게스트를 돈다. QEMU 외에도 cloud-hypervisor, Firecracker, crosvm 같은 다른 VMM이 KVM API를 사용한다.

따라서 "KVM = Kernel + QEMU"라는 표현은 오해를 부른다. 정확히는 "KVM은 커널 가상화 API/모듈, QEMU는 그 위에서 도는 VMM 중 가장 흔한 구현체"다.

```text
┌───────────────────────────────────┐
│  Guest OS                         │
│  ┌─────────┬─────────┬─────────┐ │
│  │ Process │ Process │ Process │ │
│  └─────────┴─────────┴─────────┘ │
│          Guest Kernel             │
└───────────────────────────────────┘
         ↕ (VM Entry/Exit)
┌───────────────────────────────────┐
│  KVM (kernel module)              │
│  - /dev/kvm 디바이스              │
│  - VMCS 관리                      │
│  - VM Exit 핸들러                 │
└───────────────────────────────────┘
         ↕ (ioctl)
┌───────────────────────────────────┐
│  QEMU (userspace)                 │
│  - 디바이스 에뮬레이션            │
│  - I/O 처리                       │
│  - 메모리 할당                    │
└───────────────────────────────────┘
```

**KVM의 역할:**
- CPU 가상화 (VT-x/AMD-V 활용)
- 메모리 가상화 (EPT/NPT)
- 인터럽트 전달

**QEMU의 역할:**
- 디스크, 네트워크, USB 등 디바이스 에뮬레이션
- BIOS 제공
- VNC/SPICE 같은 디스플레이

### KVM API: /dev/kvm

```c
#include <linux/kvm.h>

// 1. KVM 디바이스 열기
int kvmfd = open("/dev/kvm", O_RDWR);

// 2. VM 생성
int vmfd = ioctl(kvmfd, KVM_CREATE_VM, 0);

// 3. 메모리 설정
struct kvm_userspace_memory_region region = {
    .slot = 0,
    .guest_phys_addr = 0x0,
    .memory_size = 128 * 1024 * 1024,  // 128MB
    .userspace_addr = (uint64_t)mmap(...),
};
ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &region);

// 4. vCPU 생성
int vcpufd = ioctl(vmfd, KVM_CREATE_VCPU, 0);

// 5. vCPU 레지스터 설정 (리셋 직후 CS.base=0xffff0000이므로 실모드 CS를 0으로 내린다)
struct kvm_sregs sregs;
ioctl(vcpufd, KVM_GET_SREGS, &sregs);
sregs.cs.base = 0;
sregs.cs.selector = 0;
ioctl(vcpufd, KVM_SET_SREGS, &sregs);

struct kvm_regs regs = {};
regs.rip = 0x0;     // 시작 주소 (guest_code를 guest_phys_addr 0x0에 복사)
regs.rflags = 0x2;  // IF=0, IOPL=0
ioctl(vcpufd, KVM_SET_REGS, &regs);

// 6. vCPU 실행
struct kvm_run *run = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
                            MAP_SHARED, vcpufd, 0);
for (;;) {
    ioctl(vcpufd, KVM_RUN, 0);

    // VM Exit 처리
    switch (run->exit_reason) {
    case KVM_EXIT_IO:
        if (run->io.direction == KVM_EXIT_IO_OUT) {
            // 포트 출력 처리
            handle_io_out(run->io.port, (char *)run + run->io.data_offset);
        }
        break;
    case KVM_EXIT_HLT:
        printf("CPU halted\n");
        return 0;
    }
}
```

### 실제 예시: 간단한 x86 코드 실행

```c
// 게스트 코드 (16비트 실모드)
unsigned char guest_code[] = {
    0xba, 0xf8, 0x03,  // mov dx, 0x3f8 (COM1 포트)
    0xb0, 0x48,        // mov al, 'H'
    0xee,              // out dx, al
    0xb0, 0x69,        // mov al, 'i'
    0xee,              // out dx, al
    0xf4,              // hlt
};

// 메모리에 복사
memcpy((void *)region.userspace_addr, guest_code, sizeof(guest_code));

// 실행 - "Hi" 출력
```

**실행 흐름:**
1. `KVM_RUN` → VM Entry
2. 게스트 코드 실행 (`mov`, `out`)
3. `out` 명령어 → VM Exit (EXIT_REASON_IO_INSTRUCTION)
4. QEMU가 UART 에뮬레이션 수행
5. VM Entry로 복귀
6. `hlt` → VM Exit (EXIT_REASON_HLT)
7. 종료

## 4. 메모리 가상화: EPT/NPT

### 문제: 이중 주소 변환

```text
게스트 가상 주소 (GVA)
    ↓ 게스트 페이지 테이블
게스트 물리 주소 (GPA)
    ↓ ???
호스트 물리 주소 (HPA)
```

**소프트웨어 방식 (Shadow Page Table)**:
- 하이퍼바이저가 GVA → HPA 직접 매핑 테이블 유지
- 게스트가 페이지 테이블 수정 시 shadow 동기화 필요
- CR3 쓰기마다 VM Exit 발생 → 오버헤드 심각

### EPT (Extended Page Tables)

**Intel VT-x의 해결책**: 하드웨어가 2단계 변환 수행

```text
1단계: 게스트 페이지 테이블 (CR3 사용)
   GVA → GPA

2단계: EPT (VMCS의 EPTP 사용)
   GPA → HPA
```

**MMU가 자동으로 두 단계를 순회:**
```text
GVA 0x400000 (게스트 가상 주소)
  ↓
Guest PML4[0] → Guest PDPT[0] → Guest PD[2] → Guest PT[0]
  ↓
GPA 0x12345000 (게스트 물리 주소)
  ↓
EPT PML4[0] → EPT PDPT[0] → EPT PD[145] → EPT PT[325]
  ↓
HPA 0x5ABCD000 (호스트 물리 주소)
```

**장점:**
- 게스트 CR3 변경 시 VM Exit 불필요
- 컨텍스트 스위치 오버헤드 대폭 감소
- 소프트웨어 복잡도 감소

**EPT Violation:**
- 게스트가 매핑되지 않은 GPA 접근 시 VM Exit
- 하이퍼바이저가 페이지 fault 처리 후 VM Entry

```c
// arch/x86/kvm/vmx/vmx.c
static int handle_ept_violation(struct kvm_vcpu *vcpu) {
    gpa_t gpa = vmcs_read64(GUEST_PHYSICAL_ADDRESS);

    // 페이지 fault 처리
    return kvm_mmu_page_fault(vcpu, gpa, error_code, NULL, 0);
}
```

### AMD-V의 NPT (Nested Page Tables)

AMD의 동등 기술. 개념은 EPT와 동일하지만 용어가 다름:
- nCR3: Nested CR3
- nPT: Nested Page Tables

## 5. I/O 가상화: virtio

### 문제: 전통적 디바이스 에뮬레이션

**예시 - 네트워크 패킷 전송:**
```text
1. 게스트: e1000 NIC 레지스터에 쓰기
2. VM Exit (MMIO 접근)
3. QEMU: e1000 에뮬레이션 코드 실행
4. QEMU: tap 디바이스에 패킷 쓰기
5. VM Entry로 복귀
```

**문제점:**
- register-level emulation과 MMIO access마다 VMM 처리 경로가 길어질 수 있음
- 패킷당 exit와 복사가 많으면 작은 패킷 처리량이 제한됨
- 목표 line rate는 packet size, batching, offload와 host CPU 예산에 따라 달라짐

### virtio: 반가상화 I/O

**핵심 아이디어**: 게스트와 호스트가 공유 메모리로 직접 통신

```text
┌─────────────────┐
│  Guest Driver   │
│  (virtio-net)   │
└────────┬────────┘
         │ vring (공유 메모리)
         │ ┌──────────────┐
         └─┤ Descriptor   │
           │ Available    │
           │ Used         │
           └──────────────┘
         │
┌────────┴────────┐
│  Host Backend   │
│  (vhost-net)    │
└─────────────────┘
```

**vring 구조:**
```c
// include/uapi/linux/virtio_ring.h
struct vring_desc {
    __le64 addr;   // 버퍼 물리 주소 (GPA)
    __le32 len;    // 버퍼 크기
    __le16 flags;  // VRING_DESC_F_NEXT, VRING_DESC_F_WRITE
    __le16 next;   // 다음 descriptor 인덱스
};

struct vring_avail {
    __le16 flags;
    __le16 idx;    // 다음 쓸 위치
    __le16 ring[]; // descriptor 인덱스 배열
};

struct vring_used {
    __le16 flags;
    __le16 idx;
    struct vring_used_elem {
        __le32 id;  // descriptor 인덱스
        __le32 len; // 실제 사용된 길이
    } ring[];
};
```

**패킷 전송 흐름:**
```c
// 게스트 (drivers/net/virtio_net.c)
static int virtnet_xmit(struct sk_buff *skb, struct net_device *dev) {
    // 1. descriptor 할당
    int desc_idx = alloc_desc();
    vring->desc[desc_idx].addr = virt_to_phys(skb->data);
    vring->desc[desc_idx].len = skb->len;

    // 2. available ring에 추가
    vring->avail.ring[vring->avail.idx % queue_size] = desc_idx;
    vring->avail.idx++;

    // 3. 호스트에게 알림 (kick)
    virtqueue_kick(vq);  // VM Exit 발생 (ioeventfd 사용)

    return NETDEV_TX_OK;
}
```

**호스트는 폴링 또는 인터럽트로 처리:**
```c
// vhost-net (drivers/vhost/net.c)
static void handle_tx(struct vhost_net *net) {
    // 1. available ring 확인
    while (avail_idx != last_avail_idx) {
        desc_idx = vring->avail.ring[last_avail_idx % queue_size];

        // 2. descriptor 읽기
        gpa = vring->desc[desc_idx].addr;
        hva = gpa_to_hva(gpa);  // GPA → HVA 변환

        // 3. tap 디바이스에 쓰기
        write(tapfd, hva, len);

        // 4. used ring 업데이트
        vring->used.ring[used_idx % queue_size].id = desc_idx;
        vring->used.idx++;
        last_avail_idx++;
    }

    // 5. 게스트에게 인터럽트 (irqfd 사용)
    eventfd_signal(irqfd, 1);
}
```

**성능 개선:**
- VM Exit 횟수: 패킷당 1회 → 배치 단위 1회
- 처리량은 packet size, batching, offload, queue 수와 host CPU 예산에 따라 달라지므로 특정 배수를 일반화하지 않는다. 같은 조건에서 packets/s, CPU time, p99 latency와 drop을 함께 비교한다.

### vhost: 커널 공간 가속

**문제**: QEMU는 유저스페이스 → 느림

**해결**: vhost-net 커널 모듈이 직접 처리
```text
Guest virtio-net
    ↓ vring (공유 메모리)
Host vhost-net (커널)
    ↓ tun/tap
Physical NIC
```

**추가 최적화 - vhost-user:**
- DPDK 같은 유저스페이스 네트워크 스택과 직접 통신
- SPDK로 스토리지도 가능

## 6. 중첩 가상화 (Nested Virtualization)

### 개념

VM 안에서 또 다른 VM 실행:
```text
물리 하드웨어
  └─ L0: bare-metal host의 KVM
       └─ L1: guest hypervisor
            └─ L2: nested guest OS
```

**사용 사례:**
- 클라우드 VM 안에서 다시 하이퍼바이저 실행 (GCP/Azure의 nested virtualization 옵션)
- 클라우드 VM에서 WSL2, Android 에뮬레이터, VirtualBox 같은 가상화 도구 사용
- 하이퍼바이저 개발/테스트 환경

### 구현 복잡도

**문제점:**
- L1이 VMCS를 수정하면 L0이 intercept해야 함
- L2의 VM Exit를 누가 처리할지 결정 필요
- EPT가 3단계로 (GVA → GPA_L2 → GPA_L1 → HPA)

**Intel의 VMCS Shadowing:**
- L1의 VMCS 읽기/쓰기를 하드웨어가 직접 처리
- VM Exit 횟수 감소

**성능 계약:**
- nested VM exit는 L2→L1에서 끝날 수도 있고 L0까지 전달돼야 할 수도 있어 exit 종류에 따라 비용이 크게 다르다.
- 주소 변환은 L2 guest page table과 nested translation을 결합하며, L0은 shadow/composed nested page table을 유지할 수 있다. 이를 항상 고정된 “3단 EPT walk”로 환산할 수 없다.
- CPU 세대만으로 오버헤드를 예측하지 않는다. L2 workload의 exit 이유 분포, nested page fault, TLB flush, I/O 가상화 경로와 overcommit을 bare metal·L1 VM과 같은 조건에서 비교한다.

## 7. 실전 예제: KVM 벤치마크

### VM Exit 오버헤드 측정

```c
// test_vmexit.c
#include <stdio.h>
#include <time.h>

#define ITERATIONS 1000000

int main() {
    struct timespec start, end;

    // VT-x에서 CPUID는 VMCS 설정과 무관하게 무조건 VM Exit이고, AMD-V에서만 VMCB intercept 설정에 따른다.
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < ITERATIONS; i++) {
        unsigned int eax = 0;
        asm volatile("cpuid" : "+a"(eax) : : "ebx", "ecx", "edx");
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    long ns = (end.tv_sec - start.tv_sec) * 1000000000L +
              (end.tv_nsec - start.tv_nsec);
    printf("CPUID per call: %ld ns\n", ns / ITERATIONS);

    return 0;
}
```

이 코드는 `CPUID` 처리 비용을 비교하는 예일 뿐 순수 VM Exit/Entry만 분리하지 못한다. 게스트에서는 VMM의 CPUID emulation과 정책이 포함되고, 호스트에서도 직렬화 명령 자체의 비용이 있다. CPU pinning, warm-up, clock source, hypervisor 설정을 기록하고 exit 통계와 함께 해석해야 한다.

### EPT 성능 측정

```c
// test_ept.c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SIZE (1UL << 30)  // 1GB

int main() {
    char *buf = malloc(SIZE);
    struct timespec start, end;

    // 첫 접근 - EPT 위반으로 페이지 할당
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t i = 0; i < SIZE; i += 4096)
        buf[i] = 1;
    clock_gettime(CLOCK_MONOTONIC, &end);
    long long first_ns =
        (end.tv_sec - start.tv_sec) * 1000000000LL +
        (end.tv_nsec - start.tv_nsec);
    printf("First access: %lld ms\n", first_ns / 1000000);

    // 두 번째 접근 - EPT 캐싱됨
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t i = 0; i < SIZE; i += 4096)
        buf[i] = 2;
    clock_gettime(CLOCK_MONOTONIC, &end);
    long long second_ns =
        (end.tv_sec - start.tv_sec) * 1000000000LL +
        (end.tv_nsec - start.tv_nsec);
    printf("Second access: %lld ms\n", second_ns / 1000000);

    free(buf);
}
```

첫 접근에는 게스트의 demand-zero page fault, 호스트 backing page 준비, nested page-table 채움, cache/TLB cold state가 함께 섞일 수 있다. 두 번째 접근과의 차이를 모두 EPT 하나의 비용으로 돌리면 안 된다. guest major/minor fault, KVM exit reason, host fault, EPT/NPT walk 관련 hardware counter를 함께 기록해야 원인을 분리할 수 있다.


---

## 결론

가상화는 소프트웨어와 하드웨어의 정교한 협업이다.

1. 전가상화, 반가상화와 하드웨어 지원 가상화가 서로 다른 경계와 비용을 선택한다는 점
2. Intel VT-x의 VMX mode·VMCS와 AMD-V의 host/guest state·VMCB
3. 게스트와 하이퍼바이저의 전환 메커니즘인 VM Exit/Entry
4. 리눅스 커널을 하이퍼바이저로 만든 KVM
5. 2단계 페이지 테이블로 메모리 가상화를 가속하는 EPT/NPT
6. 반가상화 I/O로 디바이스 성능을 끌어올리는 virtio

클라우드 VM 서비스는 하드웨어 가상화, host VMM과 전용 I/O 경로를 조합한다. 공급자마다 hypervisor와 offload 구조가 다르고 시간이 지나며 바뀔 수 있으므로, 특정 브랜드를 하나의 고정 구현과 동일시하지 않는다. 컨테이너 오케스트레이션도 VM 위에 배치될 수 있지만 bare-metal host에서도 동작하므로 가상 머신 계층이 개념적 필수조건은 아니다.

## 더 읽을거리

- [Intel 64 and IA-32 Architectures Software Developer's Manual, Volume 3C](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) — VMX
- [AMD64 Architecture Programmer's Manual Volume 2: System Programming](https://www.amd.com/en/support/tech-docs) — SVM
- [KVM 프로젝트](https://www.linux-kvm.org/page/Main_Page)
- [virtio 명세](https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.html)
- Linux 커널 소스: `arch/x86/kvm/`(`vmx/vmx.c`, `svm/svm.c`, `mmu/mmu.c`), QEMU `hw/virtio/`, `drivers/vhost/`
- Gil Neiger et al., "Intel Virtualization Technology: Hardware Support for Efficient Processor Virtualization", Intel Technology Journal, 2006
- Paul Barham et al., "Xen and the Art of Virtualization", SOSP 2003
- Rusty Russell, "virtio: Towards a De-Facto Standard For Virtual I/O Devices", ACM SIGOPS Operating Systems Review, 2008
- Jim Smith and Ravi Nair, *Virtual Machines: Versatile Platforms for Systems and Processes*, Morgan Kaufmann, 2005
- [Using the KVM API](https://lwn.net/Articles/658511/) — LWN.net
- [KVM host in a few lines of code](https://zserge.com/posts/kvm/)
- [vhost Architecture](https://www.redhat.com/en/blog/introduction-virtio-networking-and-vhost-net)
