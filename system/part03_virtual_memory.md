## 들어가며

8GB RAM이 설치된 컴퓨터에서 Task Manager를 열어 실행 중인 프로세스들의 메모리 사용량을 모두 합치면 20GB가 넘을 수 있다. 어떻게 이것이 가능한가. 더 흥미로운 것은, 각 프로세스가 자신만의 독립적인 메모리 공간을 가진 것처럼 동작한다는 점이다. 정상적인 사용자 모드 접근은 다른 프로세스의 사적 매핑으로 바로 변환되지 않는다. 공유 메모리, 디버깅·프로세스 제어 API, 커널 중개와 취약점은 별도의 명시적 경계다.

이것이 가능한 이유는 **가상 메모리**(Virtual Memory) 때문이다. 가상 메모리는 컴퓨터 과학 역사상 가장 우아한 추상화 중 하나로, 1960년대 초반 Manchester Atlas 컴퓨터에서 처음 구현된 이후 현대 모든 운영체제의 근간이 되었다.

이 글은 가상 메모리가 어떻게 작동하는지를 Linux와 x86-64 아키텍처를 중심으로 분해한다. MMU의 주소 변환, 4단계 페이지 테이블, TLB 캐싱, 페이지 폴트와 요구 페이징, mm_struct/VMA, mmap, 스왑, 그리고 커널 주소 공간까지 한 번에 따라간다.


---

## 목차
1. [왜 가상 메모리인가](#1-왜-가상-메모리인가)
2. [주소 변환: 가상에서 물리로](#2-주소-변환-가상에서-물리로)
3. [페이지 테이블: x86-64의 4단계 구조](#3-페이지-테이블-x86-64의-4단계-구조)
4. [TLB: 주소 변환의 캐시](#4-tlb-주소-변환의-캐시)
5. [페이지 폴트: 세 가지 종류](#5-페이지-폴트-세-가지-종류)
6. [요구 페이징: 필요할 때만 할당](#6-요구-페이징-필요할-때만-할당)
7. [mm_struct와 vm_area_struct](#7-mm_struct와-vm_area_struct)
8. [mmap: 메모리 매핑의 모든 것](#8-mmap-메모리-매핑의-모든-것)
9. [스왑: 물리 메모리의 확장](#9-스왑-물리-메모리의-확장)
10. [커널 주소 공간](#10-커널-주소-공간)
11. [Working Set과 접근 관측](#11-working-set과-접근-관측)


---

## 1. 왜 가상 메모리인가

### 초기 컴퓨터의 메모리 관리

1950년대와 1960년대 초반, 컴퓨터는 **물리 메모리**를 직접 다뤘다. 프로그램이 메모리 주소 `0x1000`을 참조하면, 실제로 물리 RAM의 `0x1000` 위치에 접근했다. 이 방식은 간단했지만 심각한 문제들이 있었다:

1. **보호 문제**: 한 프로그램이 다른 프로그램의 메모리를 덮어쓸 수 있었다
2. **단편화 문제**: 메모리가 여러 조각으로 나뉘어 충분한 공간이 있어도 큰 프로그램을 로드할 수 없었다
3. **제한된 주소 공간**: 프로그램이 사용할 수 있는 메모리가 물리 RAM 크기로 제한되었다

### 가상 메모리의 탄생

1961년, 영국 맨체스터 대학의 Tom Kilburn과 그의 팀은 Atlas 컴퓨터를 개발하면서 **one-level store**라는 개념을 도입했다. 이것이 바로 가상 메모리의 시초였다. Atlas는 프로그램에게 실제 물리 메모리보다 훨씬 큰 가상의 주소 공간을 제공했고, 자동으로 디스크와 RAM 사이에서 데이터를 옮겼다.

1970년, Peter Denning은 그의 유명한 논문 "Virtual Memory"에서 이 개념을 이론적으로 정립했다:

> "Virtual memory is the simulation of a storage space so large that programmers do not need to concern themselves with the details of managing the hierarchy of storage devices."

### 가상 메모리가 제공하는 것

현대 가상 메모리 시스템은 세 가지 핵심 목표를 달성한다:

**1) 격리(Isolation)**
- 각 프로세스는 자신만의 독립적인 주소 공간을 가진다
- 일반 사용자 모드 load/store로 다른 프로세스의 사적 매핑에 접근할 수 없다
- 커널 메모리는 사용자 프로세스로부터 보호된다

**2) 추상화(Abstraction)**
- 프로그램은 연속된 것처럼 사용할 수 있는 가상 주소 구간을 매핑받는다
- 물리 메모리의 실제 위치를 알 필요가 없다
- 메모리 할당/해제의 복잡성이 숨겨진다

**3) 확장(Extension)**
- 물리 RAM보다 큰 주소 공간을 사용할 수 있다
- 디스크를 "확장 메모리"로 사용한다 (스왑)
- 여러 프로세스가 물리 메모리를 효율적으로 공유한다

### 현대 시스템의 주소 공간

x86-64 명령의 포인터 폭은 64비트지만 현재 구현은 전체 64비트를 주소 변환에 사용하지 않는다. 4단계 paging은 보통 48비트 canonical virtual address를, LA57 5단계 paging을 지원하고 활성화한 시스템은 57비트를 사용한다. canonical하지 않은 상위 비트 조합은 fault를 일으킨다.

4단계 paging Linux의 전형적인 배치는 다음과 같다. 실제 경계는 커널 설정, KASLR과 아키텍처 문서를 따라야 한다.

```text
0x0000000000000000 - 0x00007FFFFFFFFFFF : User Space (128 TB)
0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF : Kernel Space (128 TB)
```

각 프로세스는 독립적인 사용자 매핑을 가지며 커널은 상위 주소 영역의 커널 매핑을 관리한다. KPTI가 활성화된 시스템은 사용자 실행 중 노출하는 커널 page table 범위를 더 제한할 수 있다.

## 2. 주소 변환: 가상에서 물리로

### 주소 변환의 기본 개념

가상 메모리의 핵심은 **주소 변환(Address Translation)**이다. 프로그램이 사용하는 모든 메모리 주소는 가상 주소(Virtual Address)이고, 이것이 실제 물리 메모리에 접근하려면 물리 주소(Physical Address)로 변환되어야 한다.

```text
Virtual Address (Program)
    ↓ [MMU]
Physical Address (RAM)
```

이 변환은 **MMU(Memory Management Unit)**라는 하드웨어가 담당한다. MMU는 CPU 내부에 있으며, 모든 메모리 접근마다 자동으로 주소를 변환한다.

### 페이징: 메모리를 페이지로 나누기

세그멘테이션(Segmentation)은 주소 공간을 코드, 데이터, 스택 같은 가변 크기 논리 단위로 나누는 방식이다. paging과 segmentation은 역사적으로 서로 다른 시스템에서 사용되거나 결합되었으므로 “segmentation 뒤에 paging이 등장했다”는 한 줄의 보편적 순서로 볼 수 없다. 가변 크기 segment는 물리 배치에서 외부 단편화를 만들 수 있다.

현대 시스템은 **페이징(Paging)**을 사용한다. 페이징은 메모리를 고정 크기의 **페이지(Page)**로 나눈다:

- **가상 메모리**: 페이지(Page)로 나뉨
- **물리 메모리**: 프레임(Frame)으로 나뉨
- 이 장의 기본 가정: x86-64의 **4KiB base page**. 다른 아키텍처와 커널 구성은 base page가 다를 수 있음

가상 페이지는 어떤 물리 프레임에도 매핑될 수 있다. 이것이 페이징의 핵심 장점이다.

```text
Virtual Memory:          Physical Memory:
[Page 0] ----\          [Frame 3]
[Page 1] -----\------>  [Frame 0]
[Page 2] ------\        [Frame 5]
[Page 3] -------\       [Frame 2]
                 \----> [Frame 1]
                        [Frame 4]
```

### 주소의 구조

4KB 페이지를 사용할 때, 가상 주소는 두 부분으로 나뉜다:

```text
Virtual Address (예: 48비트)
┌────────────────────┬─────────────┐
│  VPN (36 bits)     │  Offset     │
│ Virtual Page Number│  (12 bits)  │
└────────────────────┴─────────────┘
```

- **VPN (Virtual Page Number)**: 어느 페이지인지 식별 (상위 36비트)
- **Offset**: 페이지 내에서의 위치 (하위 12비트 = 4KB)

물리 주소도 유사한 구조:

```text
Physical Address
┌────────────────────┬─────────────┐
│  PFN (Physical     │  Offset     │
│  Frame Number)     │  (12 bits)  │
└────────────────────┴─────────────┘
```

**주소 변환 과정**:
1. 가상 주소에서 VPN 추출
2. VPN을 사용하여 페이지 테이블에서 PFN 찾기
3. PFN과 오프셋을 결합하여 물리 주소 생성

### 간단한 예제

가상 주소 `0x12345678`을 물리 주소로 변환해보자 (4KB 페이지 가정):

```text
0x12345678 = 0001 0010 0011 0100 0101 0110 0111 1000 (binary)

분해:
VPN    = 0x12345 (상위 20비트, 32비트 시스템 가정)
Offset = 0x678   (하위 12비트)

페이지 테이블 조회:
VPN 0x12345 → PFN 0xABCDE

물리 주소 생성:
Physical Address = (0xABCDE << 12) | 0x678
                 = 0xABCDE678
```

### 왜 4KB인가?

페이지 크기는 트레이드오프다:

**작은 페이지 (예: 4KB)**
- 장점: 내부 단편화 감소, 세밀한 메모리 관리
- 단점: 페이지 테이블 크기 증가, TLB 미스 증가

**큰 페이지 (예: 2MB, 1GB - Huge Pages)**
- 장점: 페이지 테이블 크기 감소, TLB 적중률 향상
- 단점: 내부 단편화 증가, 메모리 낭비

x86-64 paging은 CPU feature와 page-table leaf level에 따라 다음 크기를 사용할 수 있다:
- **4KiB**: 일반 PTE leaf
- **2MiB**: PMD-level large page
- **1GiB**: PUD-level large page를 지원하는 CPU·OS 구성

어느 크기가 실제 mapping에 쓰이는지는 HugeTLB/THP 정책, alignment, 권한, 단편화와 kernel decision에 달려 있다. 애플리케이션 종류만으로 결정되지 않는다.

Linux에서 Huge Pages 사용:

```c
// 2MB huge page 매핑
void *addr = mmap(NULL, 2 * 1024 * 1024,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                  -1, 0);
```

## 3. 페이지 테이블: x86-64의 4단계 구조

### 페이지 테이블의 문제: 크기

페이지 테이블은 VPN을 PFN으로 변환하는 데이터 구조다. 가장 단순한 형태는 배열이다:

```c
struct page_table_entry {
    unsigned long pfn;
    // ... flags
};

struct page_table_entry page_table[NUM_PAGES];
```

하지만 이 방식은 현실적이지 않다. 48비트 주소 공간에서 4KB 페이지를 사용하면:

```text
페이지 수 = 2^48 / 2^12 = 2^36 = 68,719,476,736 페이지

페이지 테이블 엔트리가 8바이트라면:
테이블 크기 = 2^36 × 8 = 512 GB
```

각 프로세스마다 512GB의 페이지 테이블을 유지하는 것은 불가능하다. 더구나 대부분의 프로세스는 주소 공간의 극히 일부만 사용한다.

### 다단계 페이지 테이블

해결책은 **다단계 페이지 테이블(Multi-level Page Table)**이다. 페이지 테이블 자체를 페이징하는 것이다.

x86-64는 일반적으로 **4단계 페이징(4-Level Paging)**을 사용한다. Ice Lake 이후 일부 서버 CPU는 LA57(5-Level Paging)을 지원해 PML5(또는 PML4 위에 한 단계)가 추가된 5단계 페이징을 켤 수 있고, 이 경우 가상 주소 폭이 48비트에서 57비트로 확장된다. Linux는 부팅 옵션과 CPU 지원 여부에 따라 4단계와 5단계 사이를 동적으로 선택한다(`CONFIG_X86_5LEVEL` + `no5lvl` 부팅 파라미터 등). 본문 설명은 4단계를 기준으로 하지만, 5단계 시스템에서는 위에 한 레벨이 더 끼어든다고 보면 된다.

1. **PML4 (Page Map Level 4)**: 최상위 레벨 (5단계 모드에서는 그 위에 PML5)
2. **PDPT (Page Directory Pointer Table)**
3. **PD (Page Directory)**
4. **PT (Page Table)**: 최하위 레벨

48비트 가상 주소는 다음과 같이 분해된다:

```text
Virtual Address (48 bits used out of 64)
┌──────┬─────┬─────┬─────┬─────┬────────┐
│ Sign │ PML4│ PDPT│ PD  │ PT  │ Offset │
│ Ext  │Index│Index│Index│Index│        │
│16bits│9bits│9bits│9bits│9bits│12 bits │
└──────┴─────┴─────┴─────┴─────┴────────┘
  63-48  47-39 38-30 29-21 20-12  11-0
```

- **Sign Extension (63-48)**: 47번 비트의 부호 확장 (0 또는 1로 채워짐)
- **PML4 Index (47-39)**: PML4 테이블의 인덱스 (512개 엔트리)
- **PDPT Index (38-30)**: PDPT의 인덱스 (512개 엔트리)
- **PD Index (29-21)**: PD의 인덱스 (512개 엔트리)
- **PT Index (20-12)**: PT의 인덱스 (512개 엔트리)
- **Offset (11-0)**: 페이지 내 오프셋 (4KB)

### 주소 변환 과정

주소 `0x00007F8A12345678`을 변환하는 과정:

```text
1. CR3 레지스터에서 PML4 테이블의 물리 주소를 얻는다
   CR3 = 0x1A2B3C4000

2. PML4 Index (비트 47-39) 추출
   PML4_idx = (0x00007F8A12345678 >> 39) & 0x1FF = 0xFF

3. PML4[0xFF] 엔트리를 읽어 PDPT 주소를 얻는다
   PML4[0xFF] = 0x1234567000 (PDPT의 물리 주소)

4. PDPT Index (비트 38-30) 추출
   PDPT_idx = (0x00007F8A12345678 >> 30) & 0x1FF = 0x28

5. PDPT[0x28] 엔트리를 읽어 PD 주소를 얻는다
   PDPT[0x28] = 0x9ABCDEF000

6. PD Index (비트 29-21) 추출
   PD_idx = (0x00007F8A12345678 >> 21) & 0x1FF = 0x91

7. PD[0x91] 엔트리를 읽어 PT 주소를 얻는다
   PD[0x91] = 0x5555666000

8. PT Index (비트 20-12) 추출
   PT_idx = (0x00007F8A12345678 >> 12) & 0x1FF = 0x145

9. PT[0x145] 엔트리를 읽어 PFN을 얻는다
   PT[0x145] = PFN 0x77777 + flags

10. 물리 주소 생성
    Physical = (0x77777 << 12) | (0x00007F8A12345678 & 0xFFF)
             = 0x77777678
```

총 **5번의 메모리 접근**이 필요하다 (PML4, PDPT, PD, PT, 실제 데이터). 이것이 TLB가 중요한 이유다.

### 페이지 테이블 엔트리 구조

각 페이지 테이블 엔트리는 64비트이며, 다음과 같은 구조를 가진다:

```text
Page Table Entry (64 bits)
┌──┬─────┬───────────────────────────┬────┬─┬───┬─┬─┬───┬───┬───┬───┬─┐
│NX│ AVL │  Physical Frame Number    │AVL │G│PAT│D│A│PCD│PWT│U/S│R/W│P│
│  │     │ (PFN) or Next Level Table │    │ │   │ │ │   │   │   │   │ │
└──┴─────┴───────────────────────────┴────┴─┴───┴─┴─┴───┴───┴───┴───┴─┘
 63 62:52            51:12            11:9 8  7  6 5  4   3   2   1  0
```

주요 비트들:

**P (Present, bit 0)**
- 1: 페이지가 물리 메모리에 존재
- 0: 페이지 폴트 발생 (페이지가 스왑되었거나 할당되지 않음)

**R/W (Read/Write, bit 1)**
- 1: 읽기/쓰기 가능
- 0: 읽기 전용

**U/S (User/Supervisor, bit 2)**
- 1: 사용자 모드에서 접근 가능
- 0: 커널 모드에서만 접근 가능

**PWT (Page-level Write-Through, bit 3)**
- 캐시 정책 제어

**PCD (Page-level Cache Disable, bit 4)**
- 1: 캐싱 비활성화

**A (Accessed, bit 5)**
- 1: 페이지가 읽기 또는 쓰기됨 (하드웨어가 자동 설정)
- LRU 페이지 교체 알고리즘에 사용

**D (Dirty, bit 6)**
- 1: 페이지가 쓰여짐 (하드웨어가 자동 설정)
- 페이지를 스왑 아웃할 때 디스크에 쓸지 결정

**PAT (Page Attribute Table, bit 7)**
- 4KiB leaf PTE에서: PAT. PCD(bit 4)와 함께 메모리 타입/캐시 정책을 선택한다
- 상위 PDE/PDPTE(leaf가 아닌 entry)에서 같은 bit 7은 **PS (Page Size)**로, 1이면 2MB/1GB large page를 선택한다. 같은 bit 위치라도 page-table level과 leaf 여부에 따라 의미가 다르다

**G (Global, bit 8)**
- 1: 전역 페이지 (TLB에서 CR3 변경 시에도 유지)
- 커널 페이지에 사용

**NX (No Execute, bit 63)**
- 1: 실행 불가 (XD, Execute Disable이라고도 함)
- 보안: 데이터 영역에서 코드 실행 방지 (스택 오버플로우 공격 차단)

### 페이지 테이블 크기 계산

다단계 구조의 장점은 사용하는 만큼만 메모리를 차지한다는 것이다.

예를 들어, 프로세스가 다음 영역만 사용한다고 가정하자:
- 코드: 4MB (0x00400000 - 0x007FFFFF)
- 힙: 4MB (0x01000000 - 0x013FFFFF)
- 스택: 8MB (0x00007FFFFF800000 - 0x00007FFFFFFFFFFF)

필요한 페이지 테이블 (PT 하나와 이를 가리키는 PD 엔트리 하나가 2MB를 커버하고, PD 테이블 전체 또는 huge-page PDPT 엔트리 하나가 1GB를 커버):
```text
1 PML4 테이블:          4KB
2 PDPT 테이블:          8KB (저주소 영역 1개, 스택 영역 1개)
2 PD 테이블:            8KB (코드+힙은 같은 1GB 안이라 PD 공유)
8 PT 테이블:            32KB (코드 4MB→2개, 힙 4MB→2개, 스택 8MB→4개)
────────────────────────────
총:                     ~52KB
```

반면 단일 레벨 페이지 테이블이라면 512GB가 필요했을 것이다.

### CR3 레지스터와 컨텍스트 스위치

**CR3 (Control Register 3)** 레지스터는 현재 프로세스의 PML4 테이블의 물리 주소를 저장한다.

```text
CR3 Register
┌────────────────────────────────┬──────┐
│ PML4 Physical Base Address     │ Flags│
│ (Page Aligned)                 │      │
└────────────────────────────────┴──────┘
  63                             12    0
```

프로세스 전환 시:
1. 커널이 새 프로세스의 PML4 주소를 CR3에 로드
2. 모든 가상 주소 변환이 새 페이지 테이블을 사용
3. TLB가 플러시됨 (Global 페이지 제외)

```c
// Linux 커널: arch/x86/mm/tlb.c
static inline void __native_load_cr3(unsigned long cr3)
{
    asm volatile("mov %0, %%cr3" : : "r" (cr3) : "memory");
}
```

## 4. TLB: 주소 변환의 캐시

### 페이지 테이블 워크의 비용

x86-64의 4단계 페이지 테이블에서 TLB가 미스하면 하드웨어 page walker는 최대 네 개의 페이지 테이블 엔트리를 읽은 뒤 실제 데이터에 접근한다. 5단계 페이징(LA57)이 활성화되면 한 단계가 더 늘어난다. 다만 이것을 곧바로 "DRAM 접근 5회"로 계산하면 안 된다.

- 상위 단계 엔트리는 page-walk cache나 일반 캐시에 남아 있을 수 있다.
- 여러 독립 miss는 out-of-order 실행과 메모리 수준 병렬성으로 일부 겹칠 수 있다.
- 하위 단계 엔트리까지 DRAM에서 읽어야 하면 직렬 의존 사슬 때문에 지연이 크게 늘어난다.
- 실제 데이터가 어느 캐시 계층에 있는지는 주소 변환 비용과 별개의 변수다.

따라서 page walk 비용은 고정 나노초가 아니라 **필요한 테이블 단계 수와 각 엔트리의 캐시 적중 상태**의 함수다.

### TLB (Translation Lookaside Buffer)

**TLB**는 최근 사용된 가상-물리 주소 변환을 캐싱하는 특수한 하드웨어 캐시다.

```text
Virtual Address Input
    ↓
┌───────────────────┐
│       TLB         │  → Hit: 변환을 즉시 공급
│ (구조와 용량은 CPU별)│
└───────────────────┘
    ↓ Miss
┌───────────────────┐
│  Page Table Walk  │  → 캐시 또는 메모리에서 PTE 탐색
└───────────────────┘
    ↓
Update TLB
```

TLB는 보통 명령어와 데이터용 1차 구조, 더 크고 통합된 2차 구조로 구성된다.

```text
┌─────────────────────────────────────┐
│  Instruction TLB (ITLB)             │
│  - 명령어 fetch의 페이지 변환       │
│  - 페이지 크기별 별도 경로 가능     │
└─────────────────────────────────────┘

┌─────────────────────────────────────┐
│  Data TLB (DTLB)                    │
│  - load/store의 페이지 변환         │
│  - 권한과 접근 비트도 함께 캐시     │
└─────────────────────────────────────┘

┌─────────────────────────────────────┐
│  Second-level TLB (종종 Unified)    │
│  - 더 큰 reach, 더 긴 hit latency   │
└─────────────────────────────────────┘
```

엔트리 수, associativity, 지원 페이지 크기, 공유 범위는 마이크로아키텍처마다 다르다. 정확한 TLB reach를 계산하려면 대상 CPU의 최적화 매뉴얼이나 성능 모니터링 문서를 확인해야 한다. 예를 들어 4KB 변환을 `N`개 보관할 수 있다면 단순 상한은 `N × 4KB`지만, 충돌 미스와 코드·데이터 작업 집합 때문에 실제 유효 reach는 더 작을 수 있다.

### TLB 엔트리 구조

TLB 엔트리는 다음 정보를 저장한다:

```text
TLB Entry
┌──────────────┬──────────────┬──────┬──────┐
│ VPN (Tag)    │ PFN          │Flags │ ASID │
└──────────────┴──────────────┴──────┴──────┘
```

- **VPN**: 가상 페이지 번호 (태그)
- **PFN**: 물리 프레임 번호
- **Flags**: P, R/W, U/S, NX 등
- **ASID (Address Space ID)**: 프로세스 식별자 (일부 아키텍처)

### TLB 적중률과 성능

TLB 적중률의 영향은 기대 비용 식으로 표현할 수 있다.

```text
E[translation] =
    P(L1 TLB hit) × C_L1_TLB
  + P(L2 TLB hit) × C_L2_TLB
  + P(TLB miss)   × C_page_walk

E[load]는 여기에 데이터 캐시 계층의 비용을 더한 단순 합이라고 단정할 수 없다.
주소 변환과 캐시 태그 조회가 구현에 따라 일부 겹치고, 여러 load도 병렬 실행될 수 있기 때문이다.
```

실측에서는 `dTLB-load-misses`, page-walk 관련 이벤트, cycles per instruction, 접근 패턴별 처리량을 함께 본다. TLB 적중률을 높이는 방법은 다음과 같다.
1. **지역성 활용**: 메모리 접근을 집중시킨다
2. **Huge Pages 사용**: 하나의 TLB 엔트리가 더 큰 영역을 커버
3. **Working Set 최소화**: 자주 사용하는 메모리를 줄인다

### TLB 플러시 (TLB Flush)

TLB 엔트리는 언제 무효화되어야 할까?

**1) 컨텍스트 스위치**
CR3가 변경되면 TLB가 플러시된다 (Global 페이지 제외).

```c
// Linux: arch/x86/include/asm/tlbflush.h
static inline void __native_flush_tlb(void)
{
    unsigned long cr3 = __read_cr3();
    __write_cr3(cr3);  // CR3에 같은 값을 쓰면 TLB 플러시
}
```

**2) 페이지 테이블 변경**
페이지 매핑을 변경하거나 권한을 바꾸면 해당 TLB 엔트리를 무효화해야 한다.

```c
// 특정 주소의 TLB 엔트리만 무효화
static inline void __native_flush_tlb_one(unsigned long addr)
{
    asm volatile("invlpg (%0)" : : "r" (addr) : "memory");
}
```

**3) 메모리 맵 변경**
`mmap`, `munmap`, `mprotect` 등의 시스템 콜은 TLB 플러시를 트리거한다.

### TLB Shootdown: 멀티코어의 문제

멀티코어 시스템에서 한 CPU가 페이지 테이블을 변경하면, 다른 모든 CPU의 TLB도 무효화해야 한다. 이를 **TLB Shootdown**이라 한다.

```text
CPU 0                   CPU 1, 2, 3
  │                         │
  ├─ 페이지 언맵             │
  ├─ 페이지 테이블 변경      │
  ├─ IPI 전송 ─────────────>├─ IPI 수신
  │                         ├─ TLB 플러시
  │                         ├─ ACK ───────>│
  ├─ 모든 ACK 대기          │
  ├─ 완료                   │
```

Linux의 TLB Shootdown:

```c
// mm/tlb.c
void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
                        unsigned long end, unsigned int stride_shift)
{
    int cpu = get_cpu();

    // 현재 CPU의 TLB 플러시
    if (cpumask_test_cpu(cpu, mm_cpumask(mm))) {
        local_flush_tlb();
    }

    // 다른 CPU들에게 IPI (Inter-Processor Interrupt) 전송
    if (cpumask_any_but(mm_cpumask(mm), cpu) < nr_cpu_ids) {
        smp_call_function_many(mm_cpumask(mm),
                               flush_tlb_func_remote, &info, 1);
    }

    put_cpu();
}
```

TLB Shootdown은 비용이 크다. 대규모 멀티코어 시스템에서 `munmap`이 느린 이유다.

### PCID: TLB 플러시 최적화

**PCID(Process-Context IDentifier)** 자체는 Intel x86이 훨씬 이전 세대(Westmere, 2010 무렵)부터 지원해 온 기능이다. CR4.PCIDE를 켜면 TLB 엔트리에 12-bit PCID가 태그된다. Haswell(2013)에서 새로 들어온 것은 PCID가 아니라 그것을 효율적으로 다루는 명령어인 `INVPCID`다. Linux는 INVPCID와 함께 사용할 수 있게 된 후에야(Linux 4.14, 2017) PCID를 본격적으로 활용해 컨텍스트 스위치 시 TLB 플러시를 피하도록 동작을 바꿨다.

PCID는 TLB 엔트리에 프로세스 ID를 태그한다.

```text
TLB Entry with PCID
┌──────┬──────────────┬──────────────┬──────┐
│ PCID │ VPN          │ PFN          │Flags │
└──────┴──────────────┴──────────────┴──────┘
  12bits
```

PCID를 사용하면:
- 컨텍스트 스위치 시 TLB를 플러시하지 않아도 된다
- 여러 프로세스의 TLB 엔트리가 공존할 수 있다
- 컨텍스트 스위치 성능이 크게 향상된다

Linux는 x86에서 KPTI 완화와 함께 PCID를 적극 활용하도록 바뀌었다. 실제 사용 여부는 CPU의 PCID/INVPCID 지원, 커널 설정, 부팅 옵션에 의존한다.

## 5. 페이지 폴트: 세 가지 종류

### 페이지 폴트란?

프로그램이 가상 주소에 접근할 때, 해당 페이지가 물리 메모리에 없으면 **페이지 폴트(Page Fault)**가 발생한다. 페이지 폴트는 CPU 예외(Exception)이며, 커널의 페이지 폴트 핸들러가 처리한다.

페이지 폴트는 세 가지 종류로 분류된다:

1. **Minor Page Fault**: 페이지가 메모리에 있지만 페이지 테이블에 매핑되지 않음
2. **Major Page Fault**: 페이지가 디스크에 있어 I/O가 필요함
3. **Invalid Page Fault**: 잘못된 메모리 접근 (Segmentation Fault)

### 페이지 폴트 처리 과정

```text
1. 메모리 접근
    ↓
2. TLB 미스
    ↓
3. 페이지 테이블 워크
    ↓
4. PTE의 Present 비트가 0
    ↓
5. CPU가 페이지 폴트 예외 발생
    ↓
6. 커널 모드로 전환
    ↓
7. do_page_fault() 핸들러 실행
    ↓
8. 페이지 폴트 유형 판별
    ↓
9. 적절한 처리 (페이지 할당, 디스크 읽기, 또는 프로세스 종료)
    ↓
10. 사용자 모드로 복귀, 명령어 재실행
```

### 페이지 폴트 핸들러

Linux의 페이지 폴트 핸들러: `do_page_fault` (arch/x86/mm/fault.c)

```c
dotraplinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code)
{
    unsigned long address = read_cr2();  // CR2: 폴트를 일으킨 가상 주소
    struct mm_struct *mm = current->mm;
    struct vm_area_struct *vma;
    unsigned int flags = FAULT_FLAG_DEFAULT;

    // 1. present/protection 원인과 user/kernel 주체는 서로 다른 비트다.
    if (likely(!(error_code & X86_PF_PROT))) {
        // Not-present (P=0). 사용자 공간의 not-present fault도 여기 해당한다.
    } else {
        // Protection violation (P=1): 권한 위반.
    }
    if (error_code & X86_PF_USER)
        flags |= FAULT_FLAG_USER;   // 주체가 사용자 모드인지는 이 비트로 판단한다
    if (error_code & X86_PF_WRITE)
        flags |= FAULT_FLAG_WRITE;

    // 2. find_vma는 vm_end > address인 첫 VMA를 반환한다. address를 포함한다는
    //    보장이 없으므로 caller가 vm_start를 검사한다.
    vma = find_vma(mm, address);
    if (!vma)
        goto bad_area;

    // 3. 포함 여부와 stack 확장 대상 여부 확인
    if (vma->vm_start <= address)
        goto good_area;
    if (!(vma->vm_flags & VM_GROWSDOWN))
        goto bad_area;
    if (expand_stack(vma, address))
        goto bad_area;

good_area:
    // 4. 접근 종류가 VMA 권한과 맞는지 확인
    if ((error_code & X86_PF_WRITE) && !(vma->vm_flags & VM_WRITE))
        goto bad_area;

    // 5. 페이지 할당 및 매핑 (do_page_fault는 void trap handler다)
    handle_mm_fault(vma, address, flags, regs);
    return;

bad_area:
    // Segmentation fault: 사용자에게 SIGSEGV 전달
    force_sig_fault(SIGSEGV, si_code, (void __user *)address);
}
```

**Error Code 비트**:
- Bit 0 (P): 0 = not-present, 1 = protection violation
- Bit 1 (W/R): 0 = read, 1 = write
- Bit 2 (U/S): 0 = kernel, 1 = user
- Bit 3 (RSVD): 1 = 페이지 테이블 엔트리의 reserved bit 위반
- Bit 4 (I/D): 1 = instruction fetch
- Bit 5 (PK): 1 = protection key violation

### Minor Page Fault

**Minor Page Fault**는 페이지가 이미 물리 메모리에 있지만, 현재 프로세스의 페이지 테이블에 매핑되지 않은 경우 발생한다.

**시나리오 1: Copy-on-Write (COW)**

`fork` 후 부모와 자식 프로세스는 페이지를 공유한다. 페이지는 읽기 전용으로 표시된다.

```c
pid_t pid = fork();
if (pid == 0) {
    // 자식 프로세스
    int x = 10;  // 읽기: OK, 공유 페이지 사용
    x = 20;      // 쓰기: Minor Page Fault!
}
```

쓰기 시도 시:
1. 페이지 폴트 발생 (R/W 비트 위반)
2. 커널이 COW 페이지임을 확인
3. 새 물리 페이지 할당
4. 원본 페이지 내용을 새 페이지로 복사
5. 새 페이지를 읽기/쓰기로 매핑
6. 명령어 재실행

```c
// mm/memory.c
static vm_fault_t do_wp_page(struct vm_fault *vmf)
{
    struct page *old_page = vmf->page;
    struct page *new_page;

    // 익명 독점 페이지일 때만 복사 없이 재사용한다. page_count 하나로
    // 판단하지 않고 매핑 수, KSM 여부, pin/추가 참조와 해당 커널 버전의
    // 독점성 조건을 함께 본다.
    if (PageAnon(old_page) && !PageKsm(old_page) &&
        total_mapcount(old_page) == 1 && page_count(old_page) == 1) {
        // 독점 소유: 복사 없이 쓰기 가능으로 전환
        set_pte_at(mm, address, pte,
                   maybe_mkwrite(pte_mkdirty(vmf->orig_pte), vma));
        return VM_FAULT_WRITE;
    }

    // 그 밖의 private file 매핑, 공유 페이지, pin된 페이지는 copy 경로로 보낸다.
    new_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, address);

    // 데이터 복사
    copy_user_highpage(new_page, old_page, address, vma);

    // 새 페이지로 매핑
    set_pte_at(mm, address, pte, mk_pte(new_page, vma->vm_page_prot));
    return VM_FAULT_WRITE;
}
```

**시나리오 2: Demand Zeroing**

`malloc`으로 메모리를 할당하면, 커널은 즉시 물리 메모리를 할당하지 않는다. 대신 VMA만 생성한다.

```c
char *buf = malloc(4096);  // 페이지 테이블 엔트리 없음
buf[0] = 'A';              // Minor Page Fault!
```

첫 접근 시:
1. 페이지 폴트 발생
2. 커널이 제로 페이지(Zero Page) 또는 새 페이지 할당
3. 페이지를 0으로 초기화
4. 페이지 테이블에 매핑

**시나리오 3: File Memory Mapping**

파일을 `mmap`으로 매핑하면, 페이지는 처음에 메모리에 로드되지 않는다.

```c
int fd = open("data.txt", O_RDONLY);
char *map = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
char c = map[0];  // Minor 또는 Major Page Fault
```

페이지가 캐시에 있으면 Minor, 없으면 Major.

### Major Page Fault

**Major Page Fault**는 페이지를 디스크에서 읽어야 하는 경우 발생한다. I/O가 필요하므로 훨씬 느리다 (수 밀리초).

**시나리오 1: 스왑 인 (Swap In)**

페이지가 스왑 영역에 있는 경우:

```c
// mm/memory.c
vm_fault_t do_swap_page(struct vm_fault *vmf)
{
    swp_entry_t entry = pte_to_swp_entry(vmf->orig_pte);
    struct page *page;

    // 스왑에서 페이지 읽기 (I/O 대기!)
    page = lookup_swap_cache(entry);
    if (!page) {
        page = swapin_readahead(entry, GFP_HIGHUSER_MOVABLE, vmf);
        // 디스크 I/O 발생
    }

    // 페이지 테이블에 매핑
    set_pte_at(vma->vm_mm, vmf->address, vmf->pte, pte);

    // 스왑 캐시에서 제거
    swap_free(entry);
}
```

**시나리오 2: 파일 매핑 (File-Backed Page)**

파일이 페이지 캐시에 없는 경우:

```c
vm_fault_t filemap_fault(struct vm_fault *vmf)
{
    struct file *file = vmf->vma->vm_file;
    struct address_space *mapping = file->f_mapping;
    struct page *page;

    // 페이지 캐시에서 찾기
    page = find_get_page(mapping, offset);
    if (!page) {
        // 캐시 미스: 디스크에서 읽기
        page = page_cache_alloc(mapping);
        error = add_to_page_cache_lru(page, mapping, offset, gfp_mask);

        // 파일 시스템의 readpage 호출 (I/O 대기!)
        error = mapping->a_ops->readpage(file, page);
    }

    vmf->page = page;
    return 0;
}
```

### Invalid Page Fault (Segmentation Fault)

**Invalid Page Fault**는 프로그램이 잘못된 메모리 주소에 접근한 경우 발생한다.

**원인**:
1. NULL 포인터 역참조
2. 매핑되지 않은 주소 접근
3. 권한 위반 (읽기 전용 메모리에 쓰기)
4. 스택 오버플로우
5. NX 비트 위반 (데이터 영역에서 코드 실행)

```c
// 예제
char *p = NULL;
*p = 'A';  // NULL 역참조: Segmentation Fault

char *buf = malloc(100);
free(buf);
buf[0] = 'A';  // Use-after-free: 미정의 동작(UB). 페이지가 이미 회수되었다면
               // Segmentation Fault가 나지만, 할당자가 페이지를 보유 중이면
               // 조용히 데이터만 오염될 수 있다

void (*func)() = (void(*)())malloc(100);
func();  // NX 위반: Segmentation Fault
```

커널의 처리:

```c
// arch/x86/mm/fault.c
static noinline void no_context(struct pt_regs *regs, unsigned long error_code,
                                unsigned long address)
{
    // 커널 모드 폴트: 심각한 오류
    if (is_kernel_mode(regs)) {
        oops_end(...);  // 커널 패닉
    }

    // 사용자 모드 폴트: SIGSEGV 전송
    force_sig_fault(SIGSEGV, si_code, (void __user *)address);
}
```

프로세스는 SIGSEGV 시그널을 받고 종료된다 (시그널 핸들러가 없다면).

### 페이지 폴트 통계 확인

```bash
# 프로세스의 페이지 폴트 통계
$ ps -o min_flt,maj_flt,cmd 1234
MINFLT MAJFLT CMD
  1234     56 /usr/bin/program

# 시스템 전체 페이지 폴트
$ sar -B 1
03:30:01 PM  pgpgin/s pgpgout/s   fault/s  majflt/s
03:30:02 PM      0.00      0.00   1234.00      12.00

# 프로세스의 누적 minor/major fault: /proc/[pid]/stat의 10·12번째 필드
$ awk '{ print "minflt=" $10, "majflt=" $12 }' /proc/1234/stat
minflt=1234 majflt=56
```

`/proc/[pid]/status`에는 page-fault 누적 카운터가 없다. 공백을 포함할 수 있는 `comm` 필드 때문에 범용 파서는 `/proc/[pid]/stat`을 단순 공백 분리하지 말고 닫는 괄호 뒤부터 명세된 필드를 해석해야 한다. 위 `awk`는 일반적인 짧은 프로세스 이름을 가정한 셸 관찰 예시다.

## 6. 요구 페이징: 필요할 때만 할당

### 요구 페이징의 원리

**요구 페이징(Demand Paging)**은 페이지를 실제로 사용할 때까지 물리 메모리 할당을 지연시키는 기법이다. 이것은 가상 메모리의 핵심 최적화다.

전통적인 방식:
```text
프로그램 로드 → 전체 코드/데이터를 메모리에 복사 → 실행
```

요구 페이징:
```text
프로그램 로드 → 페이지 테이블만 생성 → 실행
           → 페이지 접근 시 폴트 → 그때 메모리 할당/로드
```

### 실행 파일 로딩 과정

`execve` 시스템 콜로 프로그램을 실행할 때:

```c
// fs/exec.c
static int load_elf_binary(struct linux_binprm *bprm)
{
    // 1. ELF 헤더 파싱
    struct elfhdr *elf_ex = (struct elfhdr *)bprm->buf;

    // 2. 이전 주소 공간 해제
    flush_old_exec(bprm);

    // 3. 새 주소 공간 설정
    setup_new_exec(bprm);

    // 4. 각 세그먼트를 VMA로 매핑 (페이지는 아직 로드 안 함!)
    for (i = 0; i < elf_ex->e_phnum; i++) {
        if (elf_ppnt->p_type == PT_LOAD) {
            // 코드/데이터 세그먼트를 파일로 매핑
            elf_map(bprm->file,
                    load_bias + vaddr,
                    elf_ppnt,
                    prot,
                    type);
            // 실제 페이지 로딩은 페이지 폴트 시 발생!
        }
    }

    // 5. 진입점으로 점프
    start_thread(regs, elf_entry, bprm->p);
}
```

프로그램이 시작되면:
1. 첫 명령어 실행 → 코드 페이지 폴트 → 첫 코드 페이지 로드
2. 전역 변수 접근 → 데이터 페이지 폴트 → 데이터 페이지 로드
3. `malloc` → 힙 확장 (VMA만 생성)
4. 힙 메모리 접근 → 페이지 폴트 → 제로 페이지 할당

### 장점: 성능과 메모리 효율

**1) 시작 시 불필요한 I/O 회피**

loader는 ELF segment의 mapping을 만들고 실제 file-backed page는 접근 시 page cache에서 찾거나 storage에서 읽을 수 있다. 따라서 실행되지 않는 code page와 아직 접근하지 않은 data page를 시작 전에 모두 읽지 않아도 된다. 시작 시간은 binary 크기만이 아니라 page-cache 상태, relocation, dynamic linking, constructor, storage와 실제 초기 working set에 의해 결정되므로 `10MB → 몇 ms` 같은 고정 환산은 성립하지 않는다.

**2) 메모리 효율**
```text
프로그램 크기: 10MB
실제 사용: 2MB (20%)

전통적 방식: 10MB 메모리 사용
요구 페이징: 2MB 메모리 사용 (80% 절감!)
```

대부분의 프로그램은 자신의 코드 중 일부만 실행한다:
- 에러 처리 코드: 거의 실행 안 됨
- 특수 기능: 가끔 사용
- 초기화 코드: 한 번만 실행

**3) 오버커밋(Overcommit)**

upstream Linux의 mode 0은 휴리스틱 **overcommit**을 허용한다. 배포 설정, resource limit와 cgroup limit에 따라 정책은 달라질 수 있으며, 가상 주소 예약량이 물리 RAM보다 클 수 있다는 뜻이지 모든 allocation이 성공한다는 뜻은 아니다.

```c
// 8GB 물리 RAM 시스템에서
char *p1 = malloc(4 * GB);  // 정책과 주소 공간에 따라 성공할 수 있음
char *p2 = malloc(4 * GB);
char *p3 = malloc(4 * GB);  // 반환값 검사가 반드시 필요
```

접근하지 않은 익명 페이지는 resident RAM을 거의 쓰지 않을 수 있지만 commit accounting, VMA·page table 메타데이터와 주소 공간은 소비한다. 이후 모두 접근하면 OOM killer, allocation failure 또는 심한 reclaim이 발생할 수 있으므로 성공한 `malloc`이 미래 touch의 성공까지 보장하지 않는다.

오버커밋 정책 설정:
```bash
# /proc/sys/vm/overcommit_memory
# 0: 휴리스틱 overcommit (kernel 문서의 기본 mode)
# 1: always-overcommit accounting policy
# 2: CommitLimit을 계산해 엄격히 account

$ echo 2 > /proc/sys/vm/overcommit_memory
```

### 단점: 페이지 폴트 오버헤드

요구 페이징의 단점은 페이지 폴트 오버헤드다.

```text
첫 실행 시:
- 명령어마다 페이지 폴트 가능
- 수천 번의 페이지 폴트 발생
- 각 폴트: 수십 마이크로초

총 오버헤드: 수십 밀리초
```

최적화 기법:

**1) 프리페칭(Prefetching)**
커널은 페이지 폴트 시 주변 페이지도 미리 읽는다.

```c
// mm/filemap.c
vm_fault_t filemap_fault(struct vm_fault *vmf)
{
    // 요청된 페이지
    page = find_or_create_page(mapping, offset);

    // 미리 읽기
    page_cache_sync_readahead(mapping,
                              ra,        // readahead state
                              file,
                              offset,
                              req_size); // 여러 페이지 읽기
}
```

**2) Readahead 튜닝**
```bash
# block device readahead 크기 설정 (512-byte sector 단위, 256 sectors = 128KiB)
$ blockdev --setra 256 /dev/sda
```

```c
// 프로세스별 readahead 힌트 (C 라이브러리 호출)
posix_fadvise(fd, offset, len, POSIX_FADV_WILLNEED);
```

### 익명 메모리 vs 파일 백드 메모리

**익명 메모리(Anonymous Memory)**
- `malloc`, `mmap(MAP_ANONYMOUS)`로 할당
- 디스크의 파일과 연결 안 됨
- 내용: 프로그램이 생성한 데이터
- 스왑 아웃 시: 스왑 영역에 저장
- 예: 힙, 스택, 동적 배열

**파일 백드 메모리(File-Backed Memory)**
- `mmap`으로 파일 매핑
- 디스크의 파일과 연결됨
- 내용: 파일 데이터
- 스왑 아웃 시: 수정되지 않았다면 버림 (파일에서 재로드 가능)
- 예: 코드 세그먼트, 공유 라이브러리, 메모리 매핑 파일

```c
// 익명 메모리
void *anon = mmap(NULL, size,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS,
                  -1, 0);

// 파일 백드 메모리
int fd = open("data.bin", O_RDWR);
void *file_backed = mmap(NULL, size,
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED,
                         fd, 0);
```

메모리 압박 시 커널의 선택:
1. 깨끗한 파일 페이지 (clean file-backed): 즉시 버림
2. 더티 파일 페이지 (dirty file-backed): 파일에 쓰기 후 버림
3. 익명 페이지: 스왑 영역에 쓰기 후 버림

## 7. mm_struct와 vm_area_struct

### 프로세스 메모리 관리 구조

Linux 커널은 각 프로세스의 메모리를 **mm_struct**로 관리한다. mm_struct는 프로세스의 전체 주소 공간을 표현한다.

```c
// include/linux/mm_types.h (Linux 5.15 기준)
//
// Linux 6.1 이후로는 VMA를 maple tree(struct maple_tree mm_mt)로 관리하며,
// mmap/mm_rb는 제거되었다. 아래 정의는 이해를 돕는 5.15 시점의 모습이다.
struct mm_struct {
    struct vm_area_struct *mmap;      // VMA 리스트 (5.15)
    struct rb_root mm_rb;             // VMA 레드-블랙 트리 (5.15)

    pgd_t *pgd;                       // 페이지 테이블 (PML4)

    atomic_t mm_users;                // 사용자 수
    atomic_t mm_count;                // 참조 카운트

    unsigned long start_code;         // 코드 세그먼트 시작
    unsigned long end_code;           // 코드 세그먼트 끝
    unsigned long start_data;         // 데이터 세그먼트 시작
    unsigned long end_data;           // 데이터 세그먼트 끝
    unsigned long start_brk;          // 힙 시작
    unsigned long brk;                // 힙 끝 (현재)
    unsigned long start_stack;        // 스택 시작

    unsigned long arg_start;          // argv 시작
    unsigned long arg_end;            // argv 끝
    unsigned long env_start;          // 환경 변수 시작
    unsigned long env_end;            // 환경 변수 끝

    unsigned long total_vm;           // 전체 페이지 수
    unsigned long locked_vm;          // 잠긴 페이지 수
    unsigned long pinned_vm;          // 핀된 페이지 수
    unsigned long data_vm;            // 데이터 페이지 수
    unsigned long exec_vm;            // 실행 가능 페이지 수
    unsigned long stack_vm;           // 스택 페이지 수

    // ...
};
```

각 프로세스의 `task_struct`는 `mm_struct`를 가리킨다:

```c
struct task_struct {
    // ...
    struct mm_struct *mm;             // 프로세스 주소 공간
    struct mm_struct *active_mm;      // 현재 활성 주소 공간
    // ...
};
```

### VMA: 가상 메모리 영역

**vm_area_struct (VMA)**는 연속된 가상 주소 공간의 한 영역을 나타낸다.

```c
// include/linux/mm_types.h
struct vm_area_struct {
    unsigned long vm_start;           // 시작 주소 (포함)
    unsigned long vm_end;             // 끝 주소 (제외)

    struct vm_area_struct *vm_next;   // 다음 VMA
    struct vm_area_struct *vm_prev;   // 이전 VMA

    struct rb_node vm_rb;             // 레드-블랙 트리 노드

    struct mm_struct *vm_mm;          // 속한 mm_struct

    pgprot_t vm_page_prot;            // 페이지 보호 플래그
    unsigned long vm_flags;           // 플래그

    struct file *vm_file;             // 매핑된 파일 (NULL이면 익명)
    unsigned long vm_pgoff;           // 파일 내 오프셋

    const struct vm_operations_struct *vm_ops;  // 연산
    void *vm_private_data;            // 프라이빗 데이터
};
```

### VMA 플래그

`vm_flags`는 VMA의 속성을 정의한다:

```c
// include/linux/mm.h

// 접근 권한
#define VM_READ     0x00000001  // 읽기 가능
#define VM_WRITE    0x00000002  // 쓰기 가능
#define VM_EXEC     0x00000004  // 실행 가능
#define VM_SHARED   0x00000008  // 공유 매핑

// 특수 플래그
#define VM_MAYREAD  0x00000010  // 읽기로 변경 가능
#define VM_MAYWRITE 0x00000020  // 쓰기로 변경 가능
#define VM_MAYEXEC  0x00000040  // 실행으로 변경 가능
#define VM_MAYSHARE 0x00000080  // 공유로 변경 가능

#define VM_GROWSDOWN    0x00000100  // 스택처럼 아래로 성장
#define VM_UFFD_MISSING 0x00000200  // userfaultfd 미매핑 추적
// VM_GROWSUP은 독립 비트가 아니라 VM_ARCH_1이며 x86-64에서는 VM_NONE(0)이다

#define VM_DENYWRITE    0x00000800  // 매핑된 파일에 대한 쓰기 거부 (5.15에서 제거됨)
#define VM_LOCKED       0x00002000  // 페이지 잠금 (스왑 방지)
#define VM_IO           0x00004000  // I/O 영역
#define VM_DONTCOPY     0x00020000  // fork 시 복사 안 함
#define VM_DONTEXPAND   0x00040000  // mremap으로 확장 불가
```

### VMA 예제: 프로세스 메모리 맵

실제 프로세스의 VMA 구조를 확인해보자:

```bash
$ cat /proc/self/maps
```

출력:
```text
00400000-00401000 r-xp 00000000 08:01 12345  /usr/bin/cat
00600000-00601000 r--p 00000000 08:01 12345  /usr/bin/cat
00601000-00602000 rw-p 00001000 08:01 12345  /usr/bin/cat
01234000-01255000 rw-p 00000000 00:00 0      [heap]
7f1234000000-7f1234021000 rw-p 00000000 00:00 0
7f1234567000-7f1234700000 r-xp 00000000 08:01 67890  /lib/libc.so.6
7f1234700000-7f1234900000 ---p 00199000 08:01 67890  /lib/libc.so.6
7f1234900000-7f1234904000 r--p 00199000 08:01 67890  /lib/libc.so.6
7f1234904000-7f1234906000 rw-p 0019d000 08:01 67890  /lib/libc.so.6
7ffc12345000-7ffc12366000 rw-p 00000000 00:00 0      [stack]
7ffc123ab000-7ffc123ad000 r-xp 00000000 00:00 0      [vdso]
```

각 줄이 하나의 VMA다:

```text
주소 범위        권한  오프셋   디바이스  inode  파일
00400000-00401000 r-xp 00000000 08:01    12345  /usr/bin/cat
```

- **주소 범위**: vm_start - vm_end
- **권한**: r (READ), w (WRITE), x (EXEC), p (PRIVATE) / s (SHARED)
- **오프셋**: 파일 내 오프셋
- **파일**: vm_file (없으면 익명 메모리)

### VMA 탐색: 레드-블랙 트리

프로세스가 수백 개의 VMA를 가질 수 있다. 빠른 탐색을 위해 Linux는 5.15까지 VMA를 레드-블랙 트리로 관리했고, 6.1부터는 maple tree로 관리한다.

```c
// mm/mmap.c
struct vm_area_struct *find_vma(struct mm_struct *mm, unsigned long addr)
{
    struct rb_node *rb_node;
    struct vm_area_struct *vma = NULL;

    // 캐시 확인 (마지막으로 찾은 VMA)
    vma = vmacache_find(mm, addr);
    if (likely(vma))
        return vma;

    // 레드-블랙 트리 탐색: vm_end > addr인 첫 VMA를 찾는다
    rb_node = mm->mm_rb.rb_node;
    while (rb_node) {
        struct vm_area_struct *tmp;

        tmp = rb_entry(rb_node, struct vm_area_struct, vm_rb);

        if (tmp->vm_end > addr) {
            vma = tmp;
            if (tmp->vm_start <= addr)
                break;  // addr를 포함하면 확정
            rb_node = rb_node->rb_left;
        } else {
            rb_node = rb_node->rb_right;
        }
    }

    return vma;
}
```

시간 복잡도: O(log N)

`find_vma`의 반환 계약은 `addr`를 **포함하는** VMA가 아니라 `vm_end > addr`인 첫 VMA를 반환한다는 것이다. 예를 들어 `[0x2000, 0x3000)` VMA 하나만 있을 때 `addr=0x1000`이면 이 VMA가 반환된다. 호출자는 `addr < vm_start`인지(스택 확장 대상인지)를 별도로 검사해야 한다.

### VMA 연산

VMA는 다양한 연산을 지원한다:

```c
// include/linux/mm.h
struct vm_operations_struct {
    void (*open)(struct vm_area_struct *area);
    void (*close)(struct vm_area_struct *area);

    vm_fault_t (*fault)(struct vm_fault *vmf);      // 페이지 폴트
    vm_fault_t (*map_pages)(struct vm_fault *vmf,   // 배치 매핑
                            pgoff_t start_pgoff,
                            pgoff_t end_pgoff);

    int (*split)(struct vm_area_struct *area,       // VMA 분할
                 unsigned long addr);
    int (*mremap)(struct vm_area_struct *area);     // VMA 리매핑
};
```

예: 파일 매핑 VMA의 연산

```c
static const struct vm_operations_struct generic_file_vm_ops = {
    .fault = filemap_fault,        // 페이지 폴트 시 파일에서 읽기
    .map_pages = filemap_map_pages,
};
```

## 8. mmap: 메모리 매핑의 모든 것

### mmap 시스템 콜

**mmap**은 파일이나 디바이스를 메모리에 매핑하는 시스템 콜이다. 이것은 가상 메모리의 가장 강력한 기능 중 하나다.

```c
#include <sys/mman.h>

void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset);
```

**매개변수**:
- **addr**: 원하는 시작 주소 (NULL이면 커널이 선택)
- **length**: 매핑 크기 (바이트)
- **prot**: 보호 플래그
 - `PROT_READ`: 읽기 가능
 - `PROT_WRITE`: 쓰기 가능
 - `PROT_EXEC`: 실행 가능
 - `PROT_NONE`: 접근 불가
- **flags**: 매핑 유형
 - `MAP_SHARED`: 공유 매핑 (다른 프로세스와 공유)
 - `MAP_PRIVATE`: 프라이빗 매핑 (COW)
 - `MAP_ANONYMOUS`: 익명 매핑 (파일 없음)
 - `MAP_FIXED`: 정확히 addr에 매핑
 - `MAP_HUGETLB`: Huge pages 사용
- **fd**: 파일 디스크립터 (익명 매핑이면 -1)
- **offset**: 파일 내 오프셋 (페이지 정렬되어야 함)

**반환값**: 매핑된 주소 (실패 시 MAP_FAILED)

### 익명 매핑: malloc의 내부

glibc malloc은 큰 요청을 `mmap`으로 직접 처리할 수 있다. `M_MMAP_THRESHOLD`의 초기·허용 범위에 128KiB가 등장하는 구현이 있지만, 임계값은 아키텍처, glibc 버전, 튜닝 옵션과 런타임의 동적 조정에 따라 달라진다. 따라서 128KiB를 `malloc` ABI의 고정 경계로 간주하면 안 된다.

```c
// glibc 정책을 설명하기 위한 의사 코드. 실제 malloc source가 아님
void *malloc(size_t size)
{
    if (size > MMAP_THRESHOLD) {
        // mmap으로 할당
        void *p = mmap(NULL, size,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS,
                       -1, 0);
        return p;
    } else {
        // brk로 할당 (힙 확장)
        return sbrk(size);
    }
}
```

예제:

```c
// 10MB 익명 메모리 할당
void *mem = mmap(NULL, 10 * 1024 * 1024,
                 PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS,
                 -1, 0);

if (mem == MAP_FAILED) {
    perror("mmap");
    return;
}

// 사용
memset(mem, 0, 10 * 1024 * 1024);

// 해제
munmap(mem, 10 * 1024 * 1024);
```

### 파일 매핑: 명시적 사용자 버퍼 복사 없는 I/O

파일을 `mmap`으로 매핑하면, `read`/`write` 없이 파일을 메모리처럼 다룰 수 있다.

```c
// 파일 읽기: 전통적 방법
int fd = open("data.bin", O_RDONLY);
char *buf = malloc(1024);
read(fd, buf, 1024);
process(buf);
free(buf);
close(fd);

// 파일 읽기: mmap 방법
int fd = open("data.bin", O_RDONLY);
char *map = mmap(NULL, 1024, PROT_READ, MAP_PRIVATE, fd, 0);
process(map);  // read()가 만드는 별도 사용자 버퍼 복사는 없음
munmap(map, 1024);
close(fd);
```

**장점**:
1. **별도 사용자 버퍼 복사 회피**: page cache의 파일 페이지를 주소 공간에 매핑
2. **페이지 캐시 공유**: 여러 프로세스가 같은 파일을 매핑하면 물리 메모리 공유
3. **지연 로딩**: 접근한 페이지에서 fault를 통해 page cache를 채울 수 있음
4. **간단한 코드**: 포인터 연산으로 파일 접근

### MAP_PRIVATE vs MAP_SHARED

**MAP_PRIVATE**:
- 변경 사항이 다른 프로세스나 파일에 보이지 않음
- Copy-on-Write 사용
- 실행 파일 로딩에 사용

```c
int fd = open("data.bin", O_RDWR);
char *map = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE, fd, 0);

map[0] = 'X';  // COW: 새 페이지 할당, 원본 파일 변경 안 됨
```

**MAP_SHARED**:
- 변경 사항이 다른 프로세스와 파일에 보임
- 프로세스 간 통신(IPC)에 사용
- 변경 사항은 `msync`로 디스크에 동기화

```c
int fd = open("data.bin", O_RDWR);
char *map = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                 MAP_SHARED, fd, 0);

map[0] = 'X';  // 파일 변경됨 (페이지 캐시에)
msync(map, 4096, MS_SYNC);  // 디스크에 동기화
```

### 공유 메모리: MAP_SHARED + MAP_ANONYMOUS

`MAP_SHARED | MAP_ANONYMOUS`는 부모-자식 프로세스 간 메모리 공유에 사용된다.

```c
// 공유 메모리 생성
int *shared = mmap(NULL, sizeof(int),
                   PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS,
                   -1, 0);

*shared = 0;

pid_t pid = fork();
if (pid == 0) {
    // 자식 프로세스
    (*shared)++;
    printf("Child: %d\n", *shared);  // 1
    exit(0);
} else {
    // 부모 프로세스
    wait(NULL);
    printf("Parent: %d\n", *shared);  // 1 (자식의 변경 보임!)
}

munmap(shared, sizeof(int));
```

이것은 POSIX 공유 메모리(`shm_open`)의 간단한 대안이다.

### 메모리 매핑 동기화

`msync`는 메모리 매핑을 디스크와 동기화한다:

```c
int msync(void *addr, size_t length, int flags);
```

**flags**:
- `MS_ASYNC`: 비동기 쓰기 (빠름)
- `MS_SYNC`: 동기 쓰기 (완료될 때까지 대기)
- `MS_INVALIDATE`: 다른 매핑 무효화

```c
char *map = mmap(NULL, size, PROT_READ | PROT_WRITE,
                 MAP_SHARED, fd, 0);

strcpy(map, "Hello, mmap!");

// 비동기 동기화
msync(map, size, MS_ASYNC);

// 동기 동기화 (디스크 쓰기 완료 보장)
msync(map, size, MS_SYNC);
```

### 메모리 조언: madvise

`madvise`는 커널에게 메모리 사용 패턴을 알려준다:

```c
int madvise(void *addr, size_t length, int advice);
```

**advice**:
- `MADV_NORMAL`: 기본 동작
- `MADV_RANDOM`: 랜덤 접근 (프리페칭 비활성화)
- `MADV_SEQUENTIAL`: 순차 접근 (적극적 프리페칭)
- `MADV_WILLNEED`: 곧 사용할 예정 (미리 로드)
- `MADV_DONTNEED`: 더 이상 필요 없음 (페이지 해제)
- `MADV_FREE`: 내용이 중요하지 않음 (재사용 가능)

```c
// 큰 파일 순차 처리
char *map = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);

// 순차 접근 힌트
madvise(map, file_size, MADV_SEQUENTIAL);

// 다음 1MB 미리 로드
madvise(map + offset, 1024 * 1024, MADV_WILLNEED);

// 처리 완료된 부분 해제
madvise(map, processed_size, MADV_DONTNEED);
```

### 메모리 잠금: mlock

`mlock`은 페이지를 물리 메모리에 고정시켜 스왑 아웃을 방지한다:

```c
int mlock(const void *addr, size_t len);
int munlock(const void *addr, size_t len);
```

사용 사례:
- **보안**: 암호화 키를 스왑에 쓰지 않음
- **실시간**: 페이지 폴트 지연 제거

```c
// 암호화 키 저장
char key[32];
generate_key(key);

// 메모리 잠금 (스왑 방지)
if (mlock(key, sizeof(key)) != 0) {
    perror("mlock");
}

// 사용...
encrypt_data(data, key);

// 메모리 제로화 후 잠금 해제
memset(key, 0, sizeof(key));
munlock(key, sizeof(key));
```

주의: `mlock`은 특권이 필요할 수 있다 (`CAP_IPC_LOCK` 또는 RLIMIT_MEMLOCK).

### 커널의 mmap 구현

```c
// mm/mmap.c
SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
                unsigned long, prot, unsigned long, flags,
                unsigned long, fd, unsigned long, off)
{
    struct file *file = NULL;
    unsigned long retval;

    // 파일 디스크립터 검증
    if (!(flags & MAP_ANONYMOUS)) {
        file = fget(fd);
        if (!file)
            return -EBADF;
    }

    // 주소 공간에 VMA 할당
    retval = vm_mmap_pgoff(file, addr, len, prot, flags, off >> PAGE_SHIFT);

    if (file)
        fput(file);

    return retval;
}

unsigned long vm_mmap_pgoff(struct file *file, unsigned long addr,
                            unsigned long len, unsigned long prot,
                            unsigned long flag, unsigned long pgoff)
{
    struct mm_struct *mm = current->mm;
    unsigned long ret;

    mmap_write_lock(mm);          // 락 획득 (Linux 5.8+)
    ret = do_mmap(file, addr, len, prot, flag, pgoff);
    mmap_write_unlock(mm);        // 락 해제

    // 이전 커널(5.7 이하)은 down_write(&mm->mmap_sem) /
    // up_write(&mm->mmap_sem)으로 같은 작업을 했다.
    // mmap_sem 필드가 mmap_lock으로 이름이 바뀌면서 래퍼 함수도 함께 정리됐다.

    return ret;
}
```

`do_mmap`은:
1. 주소 범위 검증
2. 기존 VMA와 겹치는지 확인
3. 새 VMA 생성 (또는 기존 VMA 확장)
4. VMA를 mm_struct에 추가
5. 페이지 테이블은 아직 생성 안 함 (요구 페이징!)

## 9. 스왑: 물리 메모리의 확장

### 스왑의 필요성

물리 메모리는 유한하다. 모든 프로세스의 working set이 RAM에 들어가지 않으면 어떻게 해야 할까?

**스왑(Swap)**은 디스크를 "느린 RAM"처럼 사용하는 기법이다. 사용되지 않는 페이지를 디스크로 옮기고(swap out), 나중에 필요하면 다시 읽어온다(swap in).

```text
Physical RAM (8GB)
┌────────────────┐
│ Active Pages   │ ← 자주 사용됨
├────────────────┤
│ Inactive Pages │ ← 가끔 사용됨
└────────────────┘
        ↓ Swap out
┌────────────────┐
│ Swap Space     │ ← 디스크
│ (16GB)         │
└────────────────┘
```

### 스왑 공간 설정

Linux에서 스왑은 스왑 파티션 또는 스왑 파일로 구성된다.

**스왑 파티션**:
```bash
# 스왑 파티션 생성
$ mkswap /dev/sda2

# 스왑 활성화
$ swapon /dev/sda2

# /etc/fstab에 추가
/dev/sda2 none swap sw 0 0
```

**스왑 파일**:
```bash
# 2GB 스왑 파일 생성
$ dd if=/dev/zero of=/swapfile bs=1M count=2048
$ chmod 600 /swapfile
$ mkswap /swapfile
$ swapon /swapfile

# 확인
$ swapon --show
NAME      TYPE SIZE USED
/swapfile file   2G   0B
```

### 스왑 아웃: 어떤 페이지를 내보낼까?

메모리가 부족하면 커널은 **페이지 교체 알고리즘**으로 스왑 아웃할 페이지를 선택한다.

**LRU (Least Recently Used)**
Linux는 근사 LRU 알고리즘을 사용한다:

1. 페이지를 두 리스트로 관리:
 - **Active List**: 최근 사용된 페이지
 - **Inactive List**: 오랫동안 사용 안 된 페이지

2. 페이지 접근 시:
 - PTE의 Accessed 비트가 하드웨어에 의해 설정됨
 - 주기적으로 커널이 Accessed 비트 확인
 - Accessed이면 Active로 이동, 아니면 Inactive로 이동

3. 스왑 아웃 시:
 - Inactive 리스트의 끝에서 페이지 선택
 - Clean 페이지 우선 (Dirty 페이지는 디스크 쓰기 필요)

```c
// mm/vmscan.c
static unsigned long shrink_inactive_list(unsigned long nr_to_scan,
                                          struct lruvec *lruvec,
                                          struct scan_control *sc)
{
    LIST_HEAD(page_list);
    unsigned long nr_reclaimed = 0;

    // Inactive 리스트에서 페이지 가져오기
    nr_taken = isolate_lru_pages(nr_to_scan, lruvec, &page_list);

    // 페이지 회수 (스왑 아웃 또는 버림)
    nr_reclaimed = shrink_page_list(&page_list, ...);

    return nr_reclaimed;
}
```

### 스왑 아웃 과정

익명 페이지를 스왑 아웃하는 과정:

```text
1. 페이지 선택 (Inactive 리스트에서)
    ↓
2. Dirty 비트 확인
    ↓
3. PTE lock 획득 및 swap slot/page 참조 확보
    ↓
4. 스왑 공간 할당 (swap slot)
    ↓
5. 페이지 내용을 스왑으로 쓰기 (I/O) — 성공을 확인해야 한다
    ↓
6. I/O 성공 후 페이지 테이블 엔트리를 swap entry로 변경:
   - Present 비트: 0
   - PFN 대신 swap entry 저장
    ↓
7. 해당 주소 범위의 stale writable TLB 번역 무효화 (TLB shootdown 완료)
    ↓
8. 참조 회수와 실제 회수 조건을 확인한 뒤 물리 페이지 해제
```

PTE를 swap entry로 바꾼 것만으로 원래 페이지를 즉시 재사용할 수 있는 것은 아니다. 스왑 I/O 성공, 해당 주소의 TLB shootdown 완료, 그리고 페이지에 대한 남은 참조 회수가 모두 확인된 뒤에야 물리 페이지를 해제할 수 있다. 그 전에 다른 CPU가 stale writable TLB로 접근하거나 진행 중인 writeback이 버퍼를 붙들고 있으면 데이터 손실이나 다른 객체 덮어쓰기가 가능하다.

페이지 테이블 엔트리의 스왑 엔트리:

```text
Page Table Entry (when swapped out)
┌─┬──────────────────────────────────────────┐
│0│ Swap Offset                              │
│ │ (Type + Offset in swap space)            │
└─┴──────────────────────────────────────────┘
 P=0 (not present)
```

### 스왑 인 과정

스왑된 페이지에 접근하면 Major Page Fault가 발생한다:

```text
1. 페이지 접근
    ↓
2. PTE의 Present 비트가 0
    ↓
3. 페이지 폴트 발생
    ↓
4. PTE에서 swap entry 읽기
    ↓
5. 새 물리 페이지 할당
    ↓
6. 스왑에서 페이지 내용 읽기 (I/O)
    ↓
7. PTE 업데이트:
   - Present 비트: 1
   - PFN: 새 페이지의 프레임 번호
    ↓
8. 스왑 슬롯 해제
    ↓
9. TLB 업데이트
    ↓
10. 명령어 재실행
```

```c
// mm/memory.c
vm_fault_t do_swap_page(struct vm_fault *vmf)
{
    swp_entry_t entry = pte_to_swp_entry(vmf->orig_pte);
    struct page *page;

    // 스왑 캐시 확인 (이미 읽었나?)
    page = lookup_swap_cache(entry);
    if (!page) {
        // 스왑에서 읽기
        page = swapin_readahead(entry, GFP_HIGHUSER_MOVABLE, vmf);
        if (!page)
            return VM_FAULT_OOM;
    }

    // 페이지 테이블 업데이트
    set_pte_at(vma->vm_mm, vmf->address, vmf->pte,
               pte_mkold(mk_pte(page, vma->vm_page_prot)));

    // 스왑 캐시에서 제거
    swap_free(entry);

    return 0;
}
```

### 스왑 캐시 (Swap Cache)

**스왑 캐시**는 스왑된 페이지를 임시로 메모리에 유지하는 캐시다.

왜 필요한가?
- 스왑 인 후 페이지를 즉시 스왑에서 삭제하지 않음
- 같은 페이지를 다시 스왑 아웃할 때 디스크 쓰기 생략 (이미 스왑에 있으므로)
- 여러 프로세스가 같은 스왑 페이지를 공유할 수 있음

```text
Page Lifecycle with Swap Cache

Active ← → Inactive → Swap Out → Swap Cache
                                      ↓
                                  Swap Space
                                      ↓
Swap In ← Swap Cache (페이지 여전히 스왑에 있음)
    ↓
Active (Dirty 시 스왑 삭제, Clean이면 유지)
```

### Swappiness: 스왑 적극성 조정

`vm.swappiness`는 anonymous page의 swap I/O와 filesystem paging의 상대 비용을 표현하는 값이다. 현재 Linux 문서의 범위는 0~200이며, 단순한 “swap 사용 비율”이 아니다.

```bash
# 현재 설정 확인
$ cat /proc/sys/vm/swappiness
60

# 설정 변경
$ echo 10 > /proc/sys/vm/swappiness
```

- **0**: free·file-backed page가 zone high watermark 아래로 내려갈 때까지 kernel이 swap을 먼저 시작하지 않음
- **60**: upstream Linux의 기본값
- **100**: swap I/O와 filesystem paging의 비용을 같게 보는 기준
- **100 초과**: zram처럼 swap I/O가 filesystem I/O보다 싸다고 모델링할 때 고려 가능

최적값은 storage, zswap/zram, anonymous working set과 file cache 재사용에 따라 달라진다. 데이터베이스라는 이름만으로 1을 권장하거나, 낮을수록 항상 응답성이 좋다고 판단하지 않는다. memory pressure 실험에서 major fault, swap-in/out, workingset refault와 tail latency를 함께 측정한다.

### OOM Killer: 최후의 수단

메모리와 스왑이 모두 고갈되면 **OOM (Out Of Memory) Killer**가 작동한다.

OOM Killer는:
1. 프로세스들을 점수로 평가 (메모리 사용량, 중요도 등)
2. 가장 점수가 높은 프로세스 선택
3. SIGKILL로 프로세스 종료
4. 메모리 확보

```c
// mm/oom_kill.c
void out_of_memory(struct oom_control *oc)
{
    struct task_struct *p;
    unsigned long points;

    // 프로세스 선택
    p = select_bad_process(oc);

    // 로그
    pr_err("Out of memory: Killed process %d (%s) total-vm:%lukB\n",
           task_pid_nr(p), p->comm, K(p->mm->total_vm));

    // 종료
    do_send_sig_info(SIGKILL, SEND_SIG_FORCED, p, PIDTYPE_TGID);
}
```

OOM 점수 확인:

```bash
# 프로세스의 OOM 점수
$ cat /proc/1234/oom_score
150

# OOM 점수 조정 (-1000 ~ 1000)
$ echo -500 > /proc/1234/oom_score_adj  # 더 안전하게
$ echo 500 > /proc/1234/oom_score_adj   # 더 위험하게
```

특정 프로세스를 OOM Killer로부터 보호:

```bash
# -1000: 절대 죽이지 않음
$ echo -1000 > /proc/1234/oom_score_adj
```

### 스왑 없이 운영하기

일부 시스템은 스왑을 비활성화한다:

```bash
$ swapoff -a
```

**장점**:
- anonymous page의 swap-out·swap-in I/O 경로 제거
- swap write traffic 제거

**단점**:
- OOM 위험 증가
- 메모리 압박 시 프로세스 종료

swap 정책은 workload와 failure policy에 따라 다르다. swap을 끄면 stall 원인 하나는 줄지만 reclaim이 실패했을 때 OOM이 더 빨리 발생할 수 있다. 반대로 작은 cold anonymous working set을 swap으로 내보내 page cache와 hot memory를 보존하는 편이 유리한 서비스도 있다. 서버라는 이유만으로 최소값 하나를 일반화하지 않고 PSI, major fault, swap-in/out, OOM과 tail latency를 함께 관찰한다.

`swappiness`는 reclaim에서 anonymous memory와 file-backed page cache 사이의 상대 비용을 표현하는 입력이지 “RAM이 몇 % 찼을 때 swap을 시작한다”는 임계치가 아니다.

```bash
# 예시 값. 적용 전후 reclaim과 latency를 측정한다.
$ echo 1 > /proc/sys/vm/swappiness
```

## 10. 커널 주소 공간

### 주소 공간 분할

x86-64에서 48비트 가상 주소 공간 (256TB)은 반으로 나뉜다.

```text
0x0000000000000000 - 0x00007FFFFFFFFFFF : User Space (128 TB)
0x0000800000000000 - 0xFFFF7FFFFFFFFFFF : Canonical Hole (invalid)
0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF : Kernel Space (128 TB)
```

**Canonical Hole**: 비트 47의 부호 확장 때문에 중간 주소들은 사용 불가.

개념적으로 커널 영역의 mapping은 프로세스 사이에서 공통된 kernel object와 physical page를 가리킨다. 그러나 page-table isolation(KPTI)이 활성화된 x86 시스템은 사용자 모드용 page table에서 대부분의 kernel mapping을 분리하고 kernel 진입 시 별도 page-table root로 전환할 수 있다. 따라서 “CR3가 바뀌어도 하나의 커널 page table이 항상 그대로 공유된다”는 설명은 현대 보안 구성에서 성립하지 않는다.

```text
Process A                 Process B
┌─────────────────┐       ┌─────────────────┐
│ User Pages      │       │ User Pages      │
│ (different)     │       │ (different)     │
├─────────────────┤       ├─────────────────┤
│ Kernel Pages    │ ←───→ │ Kernel Pages    │
│ (shared!)       │       │ (shared!)       │
└─────────────────┘       └─────────────────┘
```

### 커널 주소 공간 레이아웃

4단계 페이징 기준의 공식 레이아웃(`Documentation/x86/x86_64/mm.rst`)은 다음과 같다.

```text
0xFFFF800000000000 - 0xFFFF87FFFFFFFFFF : 가드 홀 (하이퍼바이저 예약)
0xFFFF888000000000 - 0xFFFFC87FFFFFFFFF : 4-level paging 구성의 direct-map 예
0xFFFFC90000000000 - 0xFFFFE8FFFFFFFFFF : 32TB vmalloc/ioremap 공간 (vmalloc_base)
0xFFFFEA0000000000 - 0xFFFFEAFFFFFFFFFF : 1TB 가상 메모리 맵 (vmemmap_base, struct page 배열)
0xFFFFFE0000000000 - 0xFFFFFE7FFFFFFFFF : cpu_entry_area 매핑
0xFFFFFFFF80000000 - 0xFFFFFFFF9FFFFFFF : 512MB 커널 텍스트 매핑
0xFFFFFFFFA0000000 - 0xFFFFFFFFFEFFFFFF : 모듈 매핑 공간
0xFFFFFFFFFF600000 - ...                : vsyscall, fixmap 등
```

KASLR이 켜져 있으면 직접 매핑, vmalloc, vmemmap의 기준 주소(`page_offset_base` 등)가 부팅마다 랜덤화된다. 4.20 이전 커널 문서에는 직접 매핑 기점이 `0xFFFF880000000000`으로 나오는데, KAISER/KPTI 대응 과정에서 LDT 리맵 영역이 끼어들며 `0xFFFF888000000000`으로 이동했다. 위 표는 KASLR이 꺼진 기준 레이아웃이다. KASLR이 켜지면 커널 이미지 매핑 창(`KERNEL_IMAGE_SIZE`)이 1GB로 넓어지면서 모듈 매핑 시작 주소(`MODULES_VADDR`)가 `0xFFFFFFFFC0000000`으로 밀리므로, 커널 텍스트 심볼이 `0xFFFFFFFFA0000000` 이상에 나타날 수 있다.

### 직접 매핑 (Direct Mapping)

**직접 매핑 영역**은 kernel이 직접 주소화하는 physical memory range를 선형 offset 관계로 매핑한다. firmware·device hole과 지원되는 physical-address 범위 때문에 모든 숫자 주소가 유효 RAM이라는 뜻은 아니다.

```text
Virtual Address                Physical Address
0xFFFF888000000000  ←──────→  0x0000000000000000
0xFFFF888000001000  ←──────→  0x0000000000001000
0xFFFF888000002000  ←──────→  0x0000000000002000
...
0xFFFF8880FFFFFFFF  ←──────→  0x00000000FFFFFFFF
```

한 x86-64 kernel build에서의 개념식:
```text
virt_addr = phys_addr + PAGE_OFFSET
phys_addr = virt_addr - PAGE_OFFSET

PAGE_OFFSET = runtime/build configuration의 page_offset_base
```

커널 코드:

```c
// include/asm/page.h
#define __va(x)    ((void *)((unsigned long)(x) + PAGE_OFFSET))
#define __pa(x)    ((unsigned long)(x) - PAGE_OFFSET)

// 물리 주소 → 가상 주소
void *virt = __va(0x12345000);

// 가상 주소 → 물리 주소
phys_addr_t phys = __pa(virt);  // 0x12345000
```

위 주소 범위는 4-level paging의 한 layout을 설명하기 위한 예다. 5-level paging, KASLR과 kernel version에 따라 direct map의 base와 크기가 달라질 수 있으므로 숫자를 module ABI로 사용하지 않는다.

**장점**:
- 가상↔물리 변환이 단순 덧셈/뺄셈으로 끝나 소프트웨어 비용이 작다
- 가능한 범위는 large page로 매핑해 TLB reach를 늘릴 수 있다
- 커널이 모든 물리 메모리에 일관된 가상 주소로 접근할 수 있다

직접 매핑이라 해도 가상 주소 접근이므로 CPU 관점에서는 TLB 조회를 거치고 miss 시 page walk가 필요하다. large-page leaf를 쓰면 같은 TLB entry 수로 더 넓은 범위를 덮고 walk 단계가 줄 수 있지만, 실제 TLB miss 빈도는 working set과 접근 패턴에 달려 있다. “직접 매핑은 page walk가 없거나 miss가 거의 없다”는 보장은 아니다.

**제한**:
- 직접 매핑 영역의 크기와 위치는 아키텍처, page-table level과 Linux 커널 설정에 따라 달라진다.
- 모든 물리 주소가 항상 하나의 고정된 direct-map 범위에 들어간다고 사용자 프로그램이 가정할 수 없다.

### 커널 스택

각 프로세스는 커널 모드 실행을 위한 **커널 스택**을 가진다.

x86-64의 한 일반적인 Linux 구성에서는 `THREAD_SIZE`가 16KiB다. 그러나 크기는 아키텍처와 커널 구성에 따른 compile-time 값이고, `CONFIG_VMAP_STACK`이 활성화된 경우 스택은 guard page를 둔 vmalloc 영역에 배치될 수 있다. x86은 IST와 IRQ 처리용 stack 같은 별도 entry stack도 사용하지만 정확한 종류와 전환 경로는 예외·커널 버전에 따라 다르다.

```c
// include/linux/sched.h
struct task_struct {
    // ...
    void *stack;  // 커널 스택 포인터
    // ...
};
```

커널 스택 오버플로우는 심각한 문제다:
- 커널 패닉 발생
- 데이터 손상 가능

방어책:
- 스택 가드 페이지 (접근 시 폴트) — `CONFIG_VMAP_STACK`이 켜지면 vmalloc 영역에 스택을 두어 가드 페이지가 자동으로 생긴다
- 스택 프로텍터 (`-fstack-protector`)

커널 스택 크기(`THREAD_SIZE`)는 compile-time 값이다. 특정 x86-64 빌드에서 16KiB인지 확인하려면 그 커널의 generated headers와 config를 기준으로 해야 하며, 이를 바꾸는 공통 runtime sysctl은 없다. `/proc/<pid>/stack`은 call stack 관측 인터페이스이지 크기 계약을 조회하는 API가 아니다.

### Per-CPU 변수

멀티코어 시스템에서 각 CPU는 자신만의 데이터를 가질 수 있다. 이를 **Per-CPU 변수**라고 한다.

```c
// include/linux/percpu.h

// Per-CPU 변수 선언
DEFINE_PER_CPU(int, my_counter);

// 현재 CPU의 변수 읽기
int val = this_cpu_read(my_counter);

// read와 write를 두 번의 this_cpu_* 호출로 나눈 갱신은 하나의 원자 연산이 아니다.
// 그 사이 CPU migration이나 같은 CPU의 interrupt가 값을 바꿀 수 있다.
// 단일 CPU 대상 증가/가산은 this_cpu_inc / this_cpu_add로 한 번에 수행한다.
this_cpu_add(my_counter, 1);

// 여러 필드를 함께 갱신하는 복합 연산이면 preemption/interrupt 비활성화처럼
// 같은 CPU의 끼어들기를 막는 보호 계약을 코드에 명시해야 한다.

// 특정 CPU의 변수 접근
int val_cpu2 = per_cpu(my_counter, 2);
```

**장점**:
- 락 불필요 (각 CPU가 자신의 데이터만 접근)
- 캐시 친화적 (false sharing 없음)

**구현**:
각 CPU마다 별도의 메모리 영역이 있고, per-CPU 변수는 CPU ID를 기준으로 오프셋 계산:

```text
CPU 0 영역:  [my_counter] [other_var] ...
CPU 1 영역:  [my_counter] [other_var] ...
CPU 2 영역:  [my_counter] [other_var] ...
...
```

### KASLR: 커널 주소 공간 랜덤화

**KASLR (Kernel Address Space Layout Randomization)**은 커널 코드의 로드 주소를 부팅마다 랜덤화한다.

목적: **보안**
- 공격자가 커널 코드의 주소를 예측할 수 없음
- ROP (Return-Oriented Programming) 공격 방어

```bash
# KASLR 상태 확인
$ dmesg | grep KASLR
[    0.000000] KASLR enabled

# /proc/kallsyms에서 커널 심볼 주소 확인
$ sudo cat /proc/kallsyms | grep sys_call_table
ffffffffa1c00000 R sys_call_table

# 재부팅 후 주소가 변경됨
ffffffffa2400000 R sys_call_table
```

KASLR은 기본적으로 활성화되어 있다. 비활성화하려면:

```bash
# 부트 파라미터
nokaslr
```

### 주소 공간과 물리 할당의 경계

가상 주소 공간의 VMA와 페이지 테이블은 "어느 주소를 어떤 권한으로 해석하는가"를 소유한다. 실제 물리 페이지를 어느 NUMA 노드에서 가져오고 작은 커널 객체를 어느 slab에 배치하는지는 별도의 할당 계층이다. 두 계층은 페이지 폴트에서 만나지만 같은 자료구조가 아니다.

## 11. Working Set과 접근 관측

### Working set은 RSS가 아니다

Denning의 working set $W(t,\tau)$는 시각 $t$ 직전의 시간창 $\tau$ 동안 참조된 페이지 집합이다.

$$
W(t,\tau)
=
\{p \mid p\text{가 }(t-\tau,t]\text{에서 참조됨}\}
$$

RSS는 현재 물리 메모리에 상주한 페이지 수다. 최근에 접근하지 않은 페이지도 RSS에 남아 있을 수 있고, 방금 필요한 페이지가 아직 fault 나지 않았다면 working set에 필요하지만 RSS에는 없을 수 있다. 메모리 압박에서 중요한 값은 "얼마나 매핑했는가"보다 가까운 시간에 재사용할 페이지가 RAM에 들어가는가다.

모든 프로세스의 active working set 합이 물리 메모리를 오래 넘으면 페이지 회수와 fault가 서로를 증폭하는 thrashing이 발생한다. CPU는 유용한 계산보다 페이지를 내리고 다시 올리는 데 시간을 쓴다.

### Accessed bit

MMU는 페이지 테이블 엔트리의 accessed/young bit를 통해 페이지가 참조되었음을 표시할 수 있다. 운영체제는 이 bit를 읽고 지운 뒤 일정 시간이 지난 후 다시 확인하여 최근 접근을 추정한다.

```text
관측 시작: accessed bit clear
     |
     | CPU/GPU DMA가 아닌 CPU load/store
     v
하드웨어가 accessed bit set
     |
     | 커널 scanner가 읽고 clear
     v
다음 관측 구간
```

이 신호에는 한계가 있다.

- boolean이므로 구간 안에서 1회 접근과 100만 회 접근을 구분하지 못한다.
- bit를 너무 자주 지우면 page-table walk와 TLB shootdown 비용이 커질 수 있다.
- 너무 늦게 읽으면 짧은 phase 변화를 놓친다.
- 같은 물리 페이지의 여러 alias와 shared mapping은 주체별 원인을 흐릴 수 있다.
- 장치 DMA 접근은 CPU page-table accessed bit에 같은 방식으로 반영되지 않을 수 있다.

### 샘플링 비용 모델

$P$개 페이지를 매 관측마다 전수 검사하고 관측 주기를 $\Delta_s$라 하면 초당 검사량은 대략

$$
C_{\text{scan}} \propto \frac{P}{\Delta_s}
$$

이다. 정밀도를 높이려고 $\Delta_s$를 줄이면 오버헤드가 선형으로 증가한다. 반대로 페이지를 $R$개 region으로 묶고 region당 대표 주소만 샘플링하면

$$
C_{\text{sample}} \propto \frac{R}{\Delta_s},
\qquad R \ll P
$$

로 줄일 수 있다. 대신 한 sample이 region 전체를 대표한다는 편향이 생긴다.

### 적응적 region

접근 빈도가 비슷한 인접 구간은 합치고, 내부 접근 패턴이 달라지는 구간은 나누면 region 수를 제한하면서 phase 경계를 추적할 수 있다.

```text
초기:
[                    region                    ]

서로 다른 접근 패턴 관측:
[ cold ][          hot          ][    cold    ]

패턴이 다시 같아짐:
[             merged cold region              ]
```

Region 기반 관측의 불변식은 주소 공간을 겹치지 않는 정렬 구간으로 덮는 것이다. Split은 전체 범위를 보존해야 하고 merge는 인접 구간만 합쳐야 한다. Region 수에 상·하한을 두지 않으면 hot/cold 경계가 많은 workload에서 페이지 단위 전수 추적으로 퇴화하거나, 반대로 하나의 거대한 region이 되어 신호를 잃는다.

### DAMON은 이 설계의 한 구현이다

Linux DAMON(Data Access MONitor)은 sample interval, aggregation interval, region update interval을 분리한다.

- sample interval: representative address의 접근 여부를 확인하는 주기
- aggregation interval: 여러 sample을 접근 빈도로 합치는 창
- region update interval: VMA 변화와 region 경계를 다시 맞추는 주기

세 주기를 분리해야 짧은 접근을 잡으면서도 사용자에게 지나치게 noisy한 결과를 내지 않는다. DAMOS는 관측된 age·빈도·크기 조건에 reclaim, pageout, huge-page 같은 action을 연결한다. 이때 관측과 정책은 분리해야 한다. "cold로 관측됨"은 사실이고 "즉시 회수해야 함"은 workload 비용을 포함한 정책이다.

### 오차와 검증

접근 관측기는 다음 workload로 검증한다.

1. 한 region만 지속적으로 순회하는 hot set
2. 긴 cold 구간 안에 작은 hot island가 있는 입력
3. working set이 시간에 따라 이동하는 phase change
4. 여러 mapping이 같은 file page를 공유하는 경우
5. process가 매핑을 빠르게 만들고 지우는 churn
6. 관측기 자체가 CPU·page fault·lock contention을 만드는지 확인하는 대조군

정확도는 "hot page의 몇 %를 찾았는가" 하나로 끝나지 않는다. false hot은 불필요한 상주를 만들고 false cold는 재fault를 만든다. 관측을 켠 상태와 끈 상태의 처리량, major/minor fault, reclaim, PSI memory stall, p99 latency를 함께 본다.


---

## 결론

가상 메모리는 현대 운영체제의 가장 정교한 메커니즘 중 하나다. 핵심을 정리하면 다음과 같다.

1. **가상 메모리의 역사**: 1960년대 Atlas 컴퓨터에서 시작하여 현대 모든 시스템의 기반이 됨
2. **주소 변환**: MMU가 가상 주소를 물리 주소로 변환, 4단계 페이징 사용
3. **페이지 테이블**: PML4, PDPT, PD, PT의 4단계 구조, 다양한 플래그로 보호 및 관리
4. **TLB**: 주소 변환 캐싱으로 성능 향상, 적중률이 매우 중요
5. **페이지 폴트**: Minor/Major/Invalid 세 종류, 요구 페이징의 핵심
6. **요구 페이징**: 필요할 때만 메모리 할당, 빠른 시작과 메모리 효율
7. **mm_struct와 VMA**: 프로세스 주소 공간 관리, 5.15까지는 레드-블랙 트리·6.1부터는 maple tree로 효율적 탐색
8. **mmap**: 파일과 메모리 매핑, 공유 메모리, 명시적 사용자 버퍼 복사를 피하는 I/O
9. **스왑**: 디스크를 확장 메모리로 사용, LRU 기반 페이지 교체
10. **커널 주소 공간**: 사용자/커널 분할, 직접 매핑, KASLR
11. **Working set 관측**: accessed bit, sampling, 적응적 region, 관측과 회수 정책의 분리

가상 메모리를 이해하면 프로그램의 메모리 사용 패턴을 최적화하고, 세그폴트나 OOM 같은 메모리 버그를 디버깅하며, 시스템 성능을 튜닝하고, NX/KASLR/ASLR 같은 보안 메커니즘을 정확히 해석할 수 있다.

가상 메모리는 단순한 트릭이 아니라, 프로그래머를 물리 메모리 관리의 복잡성으로부터 해방시키는 근본적인 추상화다.

## 더 읽을거리

- Daniel P. Bovet, Marco Cesati, *Understanding the Linux Kernel* (3rd ed., O'Reilly, 2005) - Ch. 8 Memory Management, Ch. 9 Process Address Space
- Remzi H. Arpaci-Dusseau, Andrea C. Arpaci-Dusseau, [*Operating Systems: Three Easy Pieces*](https://pages.cs.wisc.edu/~remzi/OSTEP/) - Ch. 13-23 Virtual Memory
- Randal E. Bryant, David R. O'Hallaron, *Computer Systems: A Programmer's Perspective* (3rd ed.) - Ch. 9 Virtual Memory
- Mel Gorman, *Understanding the Linux Virtual Memory Manager* (Prentice Hall, 2004)
- Robert Love, *Linux Kernel Development* (3rd ed., Addison-Wesley, 2010)
- Peter J. Denning, "Virtual Memory" (ACM Computing Surveys, 1970)
- Peter J. Denning, "The Working Set Model for Program Behavior" (CACM, 1968)
- Moritz Lipp et al., "Meltdown: Reading Kernel Memory from User Space" (USENIX Security, 2018)
- [Intel 64 and IA-32 Architectures Software Developer's Manual, Vol. 3](https://software.intel.com/sdm) - Ch. 4 Paging
- [Linux Kernel Documentation](https://www.kernel.org/doc/html/latest/) - `Documentation/admin-guide/mm/`, `Documentation/arch/x86/x86_64/mm.rst`
- [LWN.net Memory Management](https://lwn.net/Kernel/Index/#Memory_management)
- [Linux Kernel DAMON Documentation](https://docs.kernel.org/mm/damon/index.html)
- SeongJae Park, Yunjae Lee, Heon Y. Yeom, "Profiling Dynamic Data Access Patterns with Controlled Overhead and Quality" (Middleware '19 Industry Track, 2019)
- man pages: `mmap(2)`, `mprotect(2)`, `madvise(2)`, `brk(2)`, `proc(5)`
- 소스: `mm/`, `arch/x86/mm/` (https://elixir.bootlin.com/linux/latest/source)
