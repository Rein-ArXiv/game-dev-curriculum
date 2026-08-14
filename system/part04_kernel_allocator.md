## 들어가며

> Slab, NUMA, Huge Pages의 내부


리눅스 메모리 관리는 단순히 malloc/free를 넘어서는 복잡한 계층 구조다. 페이지 할당자부터 객체 캐싱, NUMA 최적화, 메모리 압축까지 각 계층은 특정 문제를 해결하기 위해 설계되었다.

가상 메모리는 사용자 공간 주소가 어떻게 변환되는지의 이야기였다면, 커널 메모리 할당자는 그 아래 단계, 즉 커널 자체가 자신과 사용자 프로세스를 위해 물리 페이지를 어떻게 손에 넣고 분배하는지의 이야기다. `kmalloc` 한 줄 뒤에는 버디 시스템, 슬랩 캐시, per-CPU 캐시, NUMA 노드, 메모리 압축이 모두 얽혀 있다.

이 글은 그 계층을 한 단계씩 해부한다. 버디에서 시작해서 슬랩(SLAB/SLUB), `kmalloc`/`vmalloc`/`kmem_cache`, NUMA 정책, Huge Page와 THP, zswap/zram, 그리고 메모리 cgroup으로 이어진다.


---

## 목차
1. [버디 시스템](#1-버디-시스템)
2. [Slab 할당자: 객체 캐싱의 원리](#2-slab-할당자-객체-캐싱의-원리)
3. [SLUB: 현대 리눅스의 기본 할당자](#3-slub-현대-리눅스의-기본-할당자)
4. [kmalloc vs vmalloc vs kmem_cache](#4-kmalloc-vs-vmalloc-vs-kmem_cache)
5. [NUMA: Non-Uniform Memory Access](#5-numa-non-uniform-memory-access)
6. [Huge Pages와 THP](#6-huge-pages와-thp)
7. [메모리 압축 (zswap, zram)](#7-메모리-압축-zswap-zram)
8. [메모리 cgroup](#8-메모리-cgroup)
9. [실전 디버깅](#9-실전-디버깅)


---

## 1. 버디 시스템

### 문제: 외부 단편화

연속된 물리 메모리를 할당하고 해제하다 보면 필연적으로 **외부 단편화**(external fragmentation)가 발생한다. 충분한 여유 메모리가 있음에도 연속된 공간이 없어 할당에 실패하는 상황이다.

```text
할당 전: [    16KB 여유    ]
할당 후: [4KB][  ][4KB][  ][4KB]  // 8KB는 여유지만 연속된 8KB는 없다
```

### 버디 알고리즘

리눅스는 **버디 시스템**(Buddy System)으로 이 문제를 완화한다.

**핵심 아이디어:**
- 메모리를 2의 거듭제곱 크기로만 할당한다 (1, 2, 4, 8,... pages)
- 각 크기별로 free list를 유지한다
- 할당/해제 시 블록을 분할/병합한다

**데이터 구조:**
```c
// include/linux/mmzone.h
struct free_area {
    struct list_head    free_list[MIGRATE_TYPES];
    unsigned long       nr_free;
};

struct zone {
    struct free_area    free_area[MAX_ORDER];  // 전통적으로 MAX_ORDER = 11 (order 0~10)
    // 커널 6.4부터 MAX_ORDER의 의미가 "포함되는 최대 order"(10)로 바뀌었고,
    // 배열 크기는 NR_PAGE_ORDERS로 표현된다.
    // ...
};
```

**할당 과정 (3 페이지 요청 시):**
```text
1. order-2 (4 pages) 리스트 확인
2. 없으면 order-3 (8 pages) 블록을 가져와 분할
   - 4 pages: 할당에 사용
   - 4 pages: order-2 리스트로 반환 (buddy)
3. 이 중 3 pages 할당, 1 page는 낭비 (내부 단편화)
```

**해제 과정:**
```c
// mm/page_alloc.c 간소화 버전
static inline void __free_one_page(struct page *page, unsigned int order) {
    unsigned long buddy_pfn = page_pfn ^ (1 << order);
    struct page *buddy = page + (buddy_pfn - page_pfn);

    if (page_is_buddy(page, buddy, order)) {
        // buddy도 free 상태면 병합
        list_del(&buddy->lru);
        __free_one_page(page, order + 1);  // 재귀적 병합
    } else {
        list_add(&page->lru, &zone->free_area[order].free_list);
    }
}
```

### Migration Types

버디 시스템은 페이지를 **이동 가능성**에 따라 분류한다:

```c
enum migratetype {
    MIGRATE_UNMOVABLE,      // 커널 자료구조 (이동 불가)
    MIGRATE_MOVABLE,        // 사용자 페이지 (이동 가능)
    MIGRATE_RECLAIMABLE,    // slab 캐시 (회수 가능)
    MIGRATE_CMA,            // Contiguous Memory Allocator
    // ...
};
```

**왜 분류하는가?**
- 이동 불가능한 페이지가 여기저기 흩어지면 대용량 연속 할당이 불가능해진다
- 같은 타입끼리 모아두면 메모리 압축(compaction)이 효율적이다

## 2. Slab 할당자: 객체 캐싱의 원리

### 문제: 작은 객체의 빈번한 할당

커널은 `task_struct`, `inode`, `dentry` 같은 작은 객체를 끊임없이 할당/해제한다. 매번 버디 시스템을 호출하면:
- 내부 단편화 심화 (64B 객체에 4KB 페이지 할당)
- 할당/해제 오버헤드 증가
- 캐시 지역성 저하

### Slab의 핵심 아이디어

**Jeff Bonwick (1994)의 설계 원칙:**
1. **객체별 전용 캐시**: `task_struct` 전용, `inode` 전용 캐시를 만든다
2. **사전 할당**: 페이지를 미리 가져와 객체 크기로 분할해둔다
3. **생성자 활용**: 객체 초기화를 재사용한다
4. **하드웨어 캐시 정렬**: 객체를 캐시 라인 경계에 배치한다

**3층 구조:**
```text
kmem_cache (캐시 디스크립터)
    └─ slab (페이지 묶음)
        └─ object (실제 할당 단위)
```

### 구조체 예시

```c
// mm/slab.h
struct kmem_cache {
    struct kmem_cache_cpu __percpu *cpu_slab;  // per-CPU 캐시
    unsigned int size;          // 객체 크기
    unsigned int object_size;   // 실제 객체 크기 (정렬 전)
    unsigned int offset;        // freelist 포인터 위치
    struct kmem_cache_node *node[MAX_NUMNODES];
    // ...
};

struct slab {
    struct page *page;
    void *freelist;     // 다음 free 객체
    unsigned int inuse; // 사용 중인 객체 수
};
```

**할당 과정 (간소화):**
```c
void *kmem_cache_alloc(struct kmem_cache *s, gfp_t flags) {
    void *object = get_cpu_ptr(s->cpu_slab)->freelist;
    if (likely(object)) {
        s->cpu_slab->freelist = get_freepointer(s, object);
        return object;
    }
    return slow_path(s, flags);  // 새 slab 할당
}
```

### Slab Coloring

**문제**: 같은 캐시의 객체들이 동일한 캐시 라인 오프셋을 가지면 하드웨어 캐시 충돌 발생.

**해결책**: 각 slab의 시작 위치를 약간씩 어긋나게 배치.

```text
Slab 0: [offset: 0  ] [obj][obj][obj]...
Slab 1: [offset: 64 ] [obj][obj][obj]...
Slab 2: [offset: 128] [obj][obj][obj]...
```

## 3. SLUB: 현대 리눅스의 기본 할당자

### SLAB의 한계

- 복잡한 큐 관리 (per-node, per-CPU, shared 캐시)
- 메타데이터 오버헤드
- NUMA 시스템에서 비효율

### SLUB의 개선점

**Christoph Lameter (2007)의 설계:**

1. **간소화된 구조**:
 - SLAB의 다단계 큐(per-CPU array cache, shared cache, per-node 큐)를 제거하고, per-CPU 활성 slab + freelist 포인터 하나로 단순화
 - 메타데이터를 `struct page`(최신 커널은 `struct slab`)에 직접 저장

2. **빠른 경로 최적화**:
```c
// mm/slub.c
void *kmem_cache_alloc(struct kmem_cache *s, gfp_t gfpflags) {
    void *ret;
    struct kmem_cache_cpu *c;

    c = this_cpu_ptr(s->cpu_slab);
    object = c->freelist;

    if (unlikely(!object || !node_match(c, node)))
        return slow_path();

    c->freelist = get_freepointer(s, object);
    return object;
}
```

3. **Freelist 임베딩**:
 - 별도 메타데이터 없이 free 객체 내부에 다음 포인터 저장
 ```
 [free object]: [next pointer | unused space]
 ```

4. **선택적 디버깅 기능**:
 - 설정에 따라 Redzone, Poison, 사용자 추적 등을 활성화
 - `/sys/kernel/slab/` 통해 실시간 모니터링

SLUB의 fast path는 현재 CPU의 freelist에서 객체를 꺼낼 수 있을 때 짧다. 디버그 옵션, freelist hardening, NUMA 정책, 캐시 refill 여부에 따라 경로가 달라지므로 SLAB과 SLUB의 비용을 고정 사이클이나 보편적 2배 차이로 표현할 수 없다. 비교할 때는 같은 커널 설정에서 `perf`, ftrace와 slab 통계를 이용해 fast/slow path 비중을 분리한다.

## 4. kmalloc vs vmalloc vs kmem_cache

### kmalloc

**용도**: 작은 크기의 물리적으로 연속된 메모리

```c
void *ptr = kmalloc(1024, GFP_KERNEL);
```

**내부 구현**:
- 크기별 `kmalloc-<size>` 캐시(8, 16, 32, …)를 SLUB 위에 구축한 범용 인터페이스. 캐시 크기 사다리는 SLUB 설정과 페이지 크기에 따라 달라진다. 일반 x86_64 + 4KB 페이지에서는 8B~8KB 사이의 2의 거듭제곱 + 일부 보조 슬롯으로 구성된다.

**제약**:
- 단일 호출 최대 크기는 buddy allocator의 최대 연속 페이지(`MAX_ORDER`)에 묶인다. 일반적으로 4MB 안팎이지만, 아키텍처/페이지 크기/`MAX_ORDER` 설정에 따라 달라진다.
- 결과 메모리는 물리적으로 연속이다. 큰 연속 메모리가 필요 없으면 `kvmalloc`(연속 시도 후 실패하면 vmalloc로 fallback)을 사용하는 편이 안전하다.

### vmalloc

**용도**: 큰 크기의 가상으로만 연속된 메모리

```c
void *ptr = vmalloc(1024 * 1024 * 16);  // 16MB
```

**내부 구현**:
- `VMALLOC_START` ~ `VMALLOC_END` 영역 사용
- 페이지 단위로 버디 시스템에서 할당 후 페이지 테이블로 매핑

**특징**:
- 큰 할당 가능 (수 GB까지)
- 페이지가 물리적으로 흩어지므로 huge page를 받지 못해 TLB 효율이 낮다
- 일반적인 streaming DMA API에 곧바로 넘길 물리 연속 버퍼가 아니다. scatter-gather DMA, IOMMU, `dma_map_sg`, 별도 매핑 같은 추가 작업이 들어가면 vmalloc 영역도 디바이스에 줄 수는 있다.

### kmem_cache_create

**용도**: 특정 자료구조 전용 캐시

```c
struct kmem_cache *my_cache = kmem_cache_create(
    "my_struct",
    sizeof(struct my_struct),
    0,  // alignment
    SLAB_HWCACHE_ALIGN,
    NULL  // constructor
);

struct my_struct *obj = kmem_cache_alloc(my_cache, GFP_KERNEL);
```

**장점**:
- 내부 단편화 최소화
- 캐시 지역성 향상
- 객체별 추적 가능

**실제 사례:**
```bash
$ cat /proc/slabinfo | head
slabinfo - version: 2.1
# name            <active_objs> <num_objs> <objsize> <objperslab> <pagesperslab>
task_struct          256       256      4096          1            1
inode_cache         4532      4680       608         13            2
dentry             12584     12584       192         21            1
```

## 5. NUMA: Non-Uniform Memory Access

### 아키텍처

**UMA (Uniform Memory Access):**
```text
CPU 0 ──┐
        ├── Bus ── Memory
CPU 1 ──┘
```
모든 CPU가 메모리에 동일한 속도로 접근.

**NUMA:**
```text
CPU 0 ── Local Memory (Node 0)
  │
  └── Interconnect ── CPU 1 ── Local Memory (Node 1)
```
- Local 접근: CPU와 같은 NUMA 노드의 memory controller 경로
- Remote 접근: interconnect를 거치는 다른 노드 경로

remote penalty는 CPU 세대, socket topology, memory load, cache 상태와 access pattern에 따라 달라진다. 고정 비율을 외우기보다 `numactl`, hardware counter와 local/remote bandwidth·latency benchmark로 대상 시스템을 측정한다.

### NUMA 정책

```c
// include/uapi/linux/mempolicy.h
#define MPOL_DEFAULT     0  // 커널 기본 정책
#define MPOL_PREFERRED   1  // 선호 노드 (fallback 허용)
#define MPOL_BIND        2  // 특정 노드에만 할당
#define MPOL_INTERLEAVE  3  // 여러 노드에 분산
```

**프로그램에서 설정:**
```c
#include <numaif.h>

unsigned long nodemask = 1 << 0;  // Node 0
set_mempolicy(MPOL_BIND, &nodemask, 2);

void *ptr = malloc(1024 * 1024);  // Node 0에만 할당
```

### 커널의 NUMA 최적화

**1. Per-Node 페이지 할당:**
```c
// mm/page_alloc.c
struct page *alloc_pages_node(int nid, gfp_t gfp, unsigned int order) {
    if (nid == NUMA_NO_NODE)
        nid = numa_node_id();  // 현재 CPU의 노드
    return __alloc_pages_node(nid, gfp, order);
}
```

**2. Slab의 per-node 캐시:**
```c
struct kmem_cache_node {
    spinlock_t list_lock;
    struct list_head partial;  // 부분적으로 사용 중인 slab
    unsigned long nr_partial;
    atomic_long_t nr_slabs;
};
```

**3. AutoNUMA (자동 마이그레이션):**
- 페이지 폴트로 접근 패턴 추적
- Remote 접근이 많으면 페이지를 로컬 노드로 이동

**확인 방법:**
```bash
$ numactl --hardware
available: 2 nodes (0-1)
node 0 cpus: 0 1 2 3
node 0 size: 16384 MB
node 0 free: 8192 MB
node distances:
node   0   1
  0:  10  20   # 로컬=10, 원격=20 (상대적 비용)
  1:  20  10
```

## 6. Huge Pages와 THP

### 문제: TLB 미스

**TLB (Translation Lookaside Buffer)**:
- 가상→물리 주소 변환을 캐싱하는 하드웨어
- 엔트리 수·계층·associativity와 페이지 크기별 분할은 CPU 모델에 따라 다름

**4KB 페이지의 한계:**
```text
메모리 사용: 8GB
필요한 페이지: 2,097,152개
```

필요 page 수가 TLB 엔트리보다 많다는 사실만으로 miss율을 계산할 수는 없다. 순차 접근은 page 안의 여러 cache line이 같은 번역을 재사용하고, 반복 접근은 working set과 associativity에 따라 hit할 수 있다. 반대로 stride가 page 크기이고 set 충돌이 심하면 miss가 늘어난다. 대상 CPU의 `dTLB-load-misses`, page-walk counter와 접근 횟수를 함께 측정한다.

### Huge Pages (2MB, 1GB)

**설정:**
```bash
# 2MB huge page 100개 예약
echo 100 > /proc/sys/vm/nr_hugepages

# hugetlbfs 마운트
mount -t hugetlbfs none /mnt/huge
```

**프로그램 사용:**
```c
#include <sys/mman.h>

void *ptr = mmap(NULL, 2 * 1024 * 1024,
                 PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                 -1, 0);

// 2MB 페이지 1개로 매핑 → TLB 효율 대폭 향상
```

2MiB page 하나는 4KiB page 512개와 같은 주소 범위를 덮으므로 같은 TLB 엔트리 수에서 이론적 reach가 512배다. 실제 성능 이득은 TLB miss가 병목인지, THP fault·compaction·split 비용과 내부 단편화가 얼마인지에 달려 있다. 같은 NUMA 배치와 데이터에서 page 크기만 바꾸고 TLB walk cycles, p95 latency, RSS와 allocation stall을 비교한다.

### THP (Transparent Huge Pages)

**문제**: Huge Page는 사전 예약 필요 + 프로그램 수정 필요.

**THP 해결책**: 커널이 자동으로 2MB 페이지 생성.

**작동 방식:**
```c
// mm/huge_memory.c
static vm_fault_t do_huge_pmd_anonymous_page(struct vm_fault *vmf) {
    // 1. 2MB 연속 메모리 할당 시도
    page = alloc_pages_vma(GFP_TRANSHUGE, HPAGE_PMD_ORDER, ...);
    if (page) {
        // 2. PMD 레벨에 직접 매핑
        set_pmd_at(vma->vm_mm, haddr, vmf->pmd, entry);
    } else {
        // 3. 실패 시 4KB 페이지로 fallback
        return VM_FAULT_FALLBACK;
    }
}
```

**백그라운드 압축 (khugepaged):**
```bash
# 설정 확인
cat /sys/kernel/mm/transparent_hugepage/enabled
# [always] madvise never

# khugepaged가 4KB 페이지들을 2MB로 병합
ps aux | grep khugepaged
```

**주의사항:**
- 메모리 단편화 증가 (2MB 연속 할당 실패 시)
- 지연 시간 증가 (병합 작업)
- 데이터베이스는 종종 명시적 huge page 선호

## 7. 메모리 압축 (zswap, zram)

### 문제: Swap의 비용

전통적 스왑:
```text
메모리 부족 → 디스크로 페이지 쓰기 (수 ms)
→ 느리고, SSD 수명 단축
```

### zswap: 압축 캐시

**아키텍처:**
```text
메모리 부족
  ↓
압축하여 메모리에 저장 (50% 압축률)
  ↓
압축 풀에도 공간 없을 때만 디스크로
```

**설정:**
```bash
# 부팅 파라미터
zswap.enabled=1
zswap.compressor=lz4      # 빠른 압축
zswap.max_pool_percent=20 # RAM의 20%까지 사용

# 확인
cat /sys/module/zswap/parameters/enabled
```

**내부 구조:** zswap은 swap-out 대상 page를 압축해 zpool에 보관하고 swap entry로 찾는다. 압축 결과가 이득이 없거나 pool 정책상 받아들일 수 없으면 backing swap device로 writeback한다. Linux 6.5까지는 이 연결에 `frontswap` 추상화를 사용했지만, Linux 6.6에서 frontswap 계층이 제거되면서 zswap이 swap 경로에 직접 통합되었다. 따라서 오래된 `zswap_frontswap_store()` 이름을 현재 커널 API로 간주하면 안 된다.

zswap hit는 backing swap device를 읽는 대신 RAM의 압축 데이터를 해제하므로 storage I/O를 피할 수 있다. 그러나 zswap miss는 결국 backing device를 읽고, 압축·해제에는 CPU와 메모리가 든다. 결과는 압축률, compressor, pool 크기, page의 재사용 거리, swap device와 메모리 압박에 따라 달라진다. 비교할 때는 zswap hit/miss, writeback, major fault latency, CPU time, PSI memory stall과 application tail latency를 함께 기록한다.

### zram: RAM을 블록 디바이스로

**개념**: 압축된 블록 디바이스를 스왑으로 사용.

**설정:**
```bash
# zram 디바이스 생성
modprobe zram num_devices=1
echo lz4 > /sys/block/zram0/comp_algorithm
echo 2G > /sys/block/zram0/disksize

# 스왑으로 활성화
mkswap /dev/zram0
swapon /dev/zram0 -p 10  # 우선순위 10

# 확인
cat /proc/swaps
# Filename      Type      Size      Used    Priority
# /dev/zram0    partition 2097152   0       10
```

**zswap vs zram:**
| 특징 | zswap | zram |
|------|-------|------|
| 위치 | swap 경로의 압축 캐시 | block device |
| fallback | 디스크 스왑 필요 | 디스크 불필요 |
| 투명성 | 완전 투명 | 스왑 설정 필요 |
| 용도 | 서버 (디스크 보조) | 임베디드 (디스크 대체) |

## 8. 메모리 cgroup

### 목적

다중 테넌트 환경에서 메모리 고립:
```text
컨테이너 A: 최대 2GB
컨테이너 B: 최대 4GB
→ A가 B의 메모리를 침범 불가
```

### 설정

```bash
# cgroup v2 마운트 확인
mount | grep cgroup2

# cgroup 생성
mkdir /sys/fs/cgroup/my_container
echo "2G" > /sys/fs/cgroup/my_container/memory.max
echo "1G" > /sys/fs/cgroup/my_container/memory.high  # soft limit

# 프로세스 할당
echo $$ > /sys/fs/cgroup/my_container/cgroup.procs
```

### 내부 구조

```c
// include/linux/memcontrol.h
struct mem_cgroup {
    struct page_counter memory;    // 메모리 사용량
    struct page_counter swap;      // 스왑 사용량

    unsigned long soft_limit;
    unsigned long high;
    unsigned long max;

    struct list_head lru_list;     // per-memcg LRU
};
```

**할당 과정:**
```c
// mm/memcontrol.c
int mem_cgroup_charge(struct page *page, struct mm_struct *mm, gfp_t gfp) {
    struct mem_cgroup *memcg = get_mem_cgroup_from_mm(mm);

    // 1. 사용량 증가 시도
    if (!page_counter_try_charge(&memcg->memory, 1, &counter)) {
        // 2. limit 초과 → 회수 시도
        try_to_free_mem_cgroup_pages(memcg, 1, gfp);

        // 3. 여전히 실패 → OOM
        if (!page_counter_try_charge(&memcg->memory, 1, &counter))
            return -ENOMEM;
    }

    /* 현대 커널은 page/folio의 memcg_data에 소유 정보를 인코딩한다. */
    page->memcg_data = encode_memcg_data(memcg);  // 개념 코드
    return 0;
}
```

### 메모리 회수

**Per-memcg LRU:**
- 각 cgroup이 독립적인 LRU 리스트 유지
- limit 도달 시 해당 cgroup 내에서만 회수

```c
// mm/vmscan.c
static unsigned long shrink_memcg(struct mem_cgroup *memcg) {
    struct lruvec *lruvec = mem_cgroup_lruvec(memcg, pgdat);

    // 이 memcg의 LRU만 스캔
    scan_adjusted = lruvec_size(lruvec, LRU_INACTIVE_ANON);
    shrink_list(lruvec, scan_adjusted, LRU_INACTIVE_ANON);
}
```

### OOM Killer 제어

```bash
# OOM 시 동작 설정
echo "1" > /sys/fs/cgroup/my_container/memory.oom.group
# → cgroup 전체 종료 (단일 프로세스만 죽이지 않음)

# 통계 확인
cat /sys/fs/cgroup/my_container/memory.stat
# anon 1048576           # 익명 메모리
# file 524288            # 파일 캐시
# kernel_stack 16384     # 커널 스택
# slab 65536             # slab 캐시
# oom_kill 3             # OOM 발생 횟수
```

## 9. 실전 디버깅

### 사례 1: SLUB 메모리 누수

**증상:**
```bash
$ cat /proc/slabinfo | grep my_cache
my_cache   125000  125000  256  16  1
# 125,000개 객체 - 계속 증가 중
```

**디버깅:**
```bash
# SLUB 디버깅 활성화 (재부팅 필요)
# Kernel config: CONFIG_SLUB_DEBUG=y

# 런타임 활성화
echo 1 > /sys/kernel/slab/my_cache/trace

# 할당 추적
cat /sys/kernel/debug/slab/my_cache/alloc_traces
# 5000 <not-available> age=4295 pid=0 cpus=1,3,5
#  __kmem_cache_alloc_node+0x1a3
#  my_driver_init+0x42
#  do_one_initcall+0x46
```

### 사례 2: NUMA 불균형

**증상:**
```bash
$ numastat
                           node0           node1
numa_hit              10000000          100000  # node1 거의 안 씀
numa_foreign            100000        10000000  # 대부분 원격 접근
```

**해결:**
```bash
# 프로세스 바인딩
numactl --cpunodebind=1 --membind=1 ./my_app

# 또는 코드 수정
set_mempolicy(MPOL_BIND, &nodemask, maxnode);
```

### 사례 3: THP 단편화

**증상:**
```bash
$ cat /proc/vmstat | grep thp
thp_fault_alloc 100
thp_fault_fallback 10000  # fallback이 너무 많음
```

**해결:**
```bash
# 메모리 압축 강제 실행
echo 1 > /proc/sys/vm/compact_memory

# 또는 khugepaged 조정
echo 10000 > /sys/kernel/mm/transparent_hugepage/khugepaged/pages_to_scan
echo 100 > /sys/kernel/mm/transparent_hugepage/khugepaged/scan_sleep_millisecs
```


---

## 결론

리눅스 메모리 관리는 다음 질문에 답하기 위해 진화해왔다.

1. **단편화**: 버디 시스템과 Migration Types
2. **작은 객체**: SLUB 할당자
3. **NUMA**: Per-node 최적화와 AutoNUMA
4. **TLB 압박**: Huge Pages와 THP
5. **메모리 부족**: zswap/zram 압축
6. **고립**: cgroup 계층

각 메커니즘은 특정 워크로드에 최적화되어 있으며, 시스템 특성에 맞는 튜닝이 필요하다. `/proc`, `/sys` 인터페이스로 런타임 동작을 관찰하고, 처리량뿐 아니라 할당 지연, reclaim stall, PSI와 tail latency를 함께 측정해야 한다.

## 더 읽을거리

- Daniel P. Bovet, Marco Cesati, *Understanding the Linux Kernel* (3rd ed., O'Reilly, 2005)
- Jeff Bonwick, "The Slab Allocator: An Object-Caching Kernel Memory Allocator" (USENIX Summer, 1994)
- Jonathan Corbet, Alessandro Rubini, Greg Kroah-Hartman, *Linux Device Drivers* (3rd ed., O'Reilly, 2005)
- Mel Gorman, *Understanding the Linux Virtual Memory Manager* (Prentice Hall, 2004)
- Linux 커널 소스: `mm/page_alloc.c`(버디), `mm/slub.c`(SLUB), `mm/mempolicy.c`(NUMA), `mm/huge_memory.c`(THP), `mm/zswap.c`
- 커널 문서: `Documentation/admin-guide/mm/`
