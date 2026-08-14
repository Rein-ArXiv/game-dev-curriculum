# parts/ 시리즈 기술 검수 보고서

- **검수 대상**: `parts/part01`~`part24` 전 24편, 26,417행
- **검수일**: 2026-08-02
- **검수 범위**: 사실오류, 수식·수치 오류, 컴파일 불가 코드, 서지 오류, 내부 모순
- **제외**: 문체, 구성, 어투

---

## 요약

| 구분 | 건수 |
|---|---:|
| 중대 오류 (실행하면 틀림 / 개념 반전) | 9 |
| 확정 사실오류 | 14 |
| 컴파일 불가 코드 | 13 |
| 서지·인용 오류 | 12 |
| 근거 없는 수치 (문서 자체 방법론과 모순) | 5 |
| 부정확·내부 모순 | 다수 (파일별 항목 참조) |

**확정 오류 없이 통과한 문서**: `part12`(동기화), `part14`(코루틴/파이버), `part19`(SIMD)

**전반 평가**: 문서 대부분이 "고정 배수를 제시하지 말고 측정하라"는 방법론을 일관되게 지키고 있으며, 흔히 틀리는 지점(비가상 소멸자 delete가 UB인 점, strict aliasing의 character type 예외, proto3의 기본값 규칙, van Emde Boas layout의 정의, Cap'n Proto zero-copy의 의미, TMap이 separate chaining인 점)을 정확히 짚고 있다. 아래 지적은 그 높은 기준 대비 남은 결함이다.

---

## 1. 중대 오류 — 실행하면 틀리거나 개념이 반전됨

### 1.1 Robin Hood Hashing 예제가 규칙을 반대로 설명
`part10_hashmap.md:512-523`

코드(L477)는 `if (current_psl > slots[index].psl)` — **가난한(PSL 큰) 신참이 부자를 밀어낸다**. 그런데 예제는 "D의 PSL=0 < B의 PSL=1 → D가 인덱스 1을 차지"라며 반대로 서술하고, L523에서 "D는 부자, B는 가난… D가 B 자리를 차지하는 것이 공평성을 높인다"로 못박는다.

알고리즘대로 D(ideal=1, psl=0)를 삽입하면 B(psl=1), C(psl=2)를 지나 index 3에 psl=2로 안착한다.

```
문서:   [A][D][B][C]   PSL 0,0,2,3   ← 나올 수 없음
실제:   [A][B][C][D]   PSL 0,1,2,2
```

절의 핵심 개념이 뒤집혀 있어 최우선 수정 대상.

### 1.2 UTF-8 검증 함수가 ASCII 입력을 전부 거부
`part07_encoding.md:891-962`

`is_valid_utf8()`에 2/3/4바이트 분기만 있고 `b1 <= 0x7F` 분기가 없어 ASCII 바이트를 만나면 곧장 `return false`. 즉 **`is_valid_utf8("hello") == false`**. overlong 공격의 해결책으로 제시된 함수라 영향이 크다. 바로 위 `decode_utf8`(L536)에는 해당 분기가 있어 누락이 명백하다.

```c
// 루프 첫머리에 추가
if (b1 <= 0x7F) { i += 1; continue; }
```

### 1.3 댕글링 `string_view`를 "안전한 패턴"으로 제시
`part06_string.md:830-834`

```cpp
std::string_view first_line = input.substr(0, input.find('\n'));
process(first_line);  // 주석: "OK: input은 함수 종료까지 유효"  ← 근거 자체가 틀림
```

`std::string::substr`는 새 `std::string`을 값으로 반환하므로 view가 full-expression 끝에 소멸하는 임시객체를 가리킨다. 같은 문서 L767이 "`log.substr(0,5)`는 std::string을 새로 만들어 복사가 발생한다"고 정확히 경고해놓고, **"### 안전한 사용 패턴"** 절에 정반대 예시를 넣었다.
수정: `std::string_view(input).substr(0, input.find('\n'))`

### 1.4 String pool의 dangling key
`part10_hashmap.md:1026,1046` / `1223,1254`

맵이 `flat_hash_map<std::string_view, const char*>`인데 `interned[str] = ptr;`로 **호출자가 넘긴 view를 그대로 키로 저장**한다. `pool.intern(std::string("Enemy"))` 한 번이면 키가 즉시 dangling → 이후 `find`가 UB.
수정: `interned[std::string_view(ptr, str.size())] = ptr;`

### 1.5 Hazard Pointer의 메모리 순서가 프로토콜을 만족하지 못함
`part13_lockfree.md:484-493`, 근거 설명 `L528`

`protect()`가 hazard 슬롯 store에 `release`, src 재검사 load에 `acquire`만 쓴다. HP 정확성은 **StoreLoad 순서**(Dekker 패턴)를 요구한다 — 독자는 "hazard 공개 → src 재읽기", 회수자는 "src에서 제거 → hazard 스캔" 순으로 서로를 반드시 관측해야 한다. release store 뒤 acquire load는 StoreLoad 재배치를 막지 못하므로 양쪽이 서로를 놓쳐 **use-after-free**가 가능하다.
수정: `memory_order_seq_cst` 또는 `std::atomic_thread_fence(seq_cst)`. L528의 "release로 공개한 사실이 retire의 acquire 스캔에서 관찰된다"는 설명도 함께 고쳐야 한다(서로 다른 변수 간에는 성립하지 않음).

### 1.6 `object::operator=`가 무한 재귀
`part17_type_erasure.md:702-709`

```cpp
object& operator=(const object& x) { object tmp(x); *this = std::move(tmp); return *this; }
```

`object`는 사용자 선언 복사 생성자(L702) + 이동 생성자(L703) + 복사 대입(L705)을 모두 가지므로 [class.copy.assign]/4에 의해 **이동 대입 연산자가 암묵 선언되지 않는다**. `std::move(tmp)`가 `const object&`에 바인딩되어 자기 자신을 다시 호출 → 스택 오버플로.
같은 문서의 `document`(L770)는 `operator=(document x)` pass-by-value로 올바르게 구현되어 있다.

### 1.7 비정렬 접근 예제의 산술 오류
`part01_memory_allocation.md:97-108`

8바이트 워드(0x1000–0x1007) 기준으로 "0x1001에 있는 4바이트 int"는 워드를 걸치지 않는다(0x1001~0x1004가 전부 첫 워드 안). 두 번 읽기 설명이 성립하려면 시작 주소가 **0x1005 이상**이어야 한다. 도식 L100도 동일.

### 1.8 순회 중 컨테이너 변경 + 미정의 메서드
`part11_ecs.md:822-834`

`enemies.forEach()` 안에서 `damageEnemy()`가 `erase()`를 호출한다. SlotMap의 erase는 **swap-and-pop**(L750-757)이라 마지막 원소가 현재 인덱스로 당겨져 원소를 건너뛴다. 게다가 `SlotMap`에는 **`forEach`가 정의되어 있지 않다**(L687-785은 insert/erase/get/isValid만).

### 1.9 branchless binary search가 오답을 반환
`part21_branch_prediction.md:653-662`

n이 2의 거듭제곱이 아니면 실패한다.
```
arr=[1,2,3,4,5], n=5, target=5
step=2 → pos=2, step=1 → pos=3, 종료 후 arr[3]=4 ≠ 5 → -1 반환 (5는 배열에 있음)
```
L665가 "경계 처리도 별도로 검증해야 한다"고 암시만 하므로, 반례를 명시하거나 `step`을 최대 2의 거듭제곱으로 잡는 정식 형태로 고쳐야 한다.

---

## 2. 확정 사실오류

| # | 위치 | 내용 |
|---|---|---|
| 1 | `part03:652,1546` | OSDI 2021 논문("Beyond malloc efficiency to fleet efficiency")은 **hugepage-aware allocator(TEMERAIRE)** 논문. TCMalloc **Per-CPU Cache는 이 논문의 기여가 아니다** |
| 2 | `part07:882-883,917` | Swiss Table H1/H2 상하위 비트 반전. Abseil은 **H1 = hash>>7(상위), H2 = 하위 7비트**. 같은 문서 L965가 정확히 반대로 적어 자기모순 |
| 3 | `part08:335-343,372-384` | jemalloc size class를 2의 거듭제곱만 나열 → "400B 요청→512B, 112B 낭비"가 틀림. 실제는 **448B, 48B**. `part03:533-538`의 정확한 사다리와 모순 |
| 4 | `part09:141-149` | `std::list<GameObject>`는 노드가 객체를 **인라인 포함**(단일 할당). 그림은 `std::list<GameObject*>` 기준인데 라벨이 틀림 |
| 5 | `part05:599` | "make_shared 안 쓰면 역참조 3번" — 역참조 경로는 제어 블록을 건드리지 않음. L654가 스스로 정확히 반박 |
| 6 | `part06:151,155` | `strcat` 10,000회에 "약 3초" — 총 스캔 5×10⁷바이트라 실제로는 수 ms. **약 100배 과대** |
| 7 | `part01:299-305` | `alignas(64)`를 C++17로 귀속. alignas는 **C++11** (`part02:275`는 정확) |
| 8 | `part02:30-36` | 계층도가 `DRAM → CPU 캐시` 순 — 캐시가 DRAM 아래에 배치됨 |
| 9 | `part16:715-718` | `__builtin_add_overflow`는 **오버플로 시 true** 반환. `safe_add_builtin`이 규약 반전(`!` 누락) |
| 10 | `part16:356-360` | UB 글의 "합법" 예제가 UB. `bytes[0] << 24`는 int 승격 후 signed shift overflow. `((uint32_t)bytes[0] << 24)` 필요 |
| 11 | `part20:650-657` | I/O 하한이 `Ω((n/B)·(n/B)·log)`로 적혀 **(n/B)²** 가 됨. `+` 누락. 같은 절 L648의 올바른 식과 모순 |
| 12 | `part20:600` | "L1 캐시(256 sets)" — 32KB/8-way/64B면 **64개** (`part02:151`이 정확히 계산) |
| 13 | `part23:154,352` | `20.3f` 비트가 `9A 99 A2 41`(=20.325). 정답은 **0x41A26666 → `66 66 A2 41`**. 같은 표의 10.5f·5.0f는 정확 |
| 14 | `part24:645-648` | `TWeakObjectPtr<FAnimInstanceProxy>` — TWeakObjectPtr은 **UObject 파생 타입 전용**. `F` 접두 타입에는 쓸 수 없다. `part05:756-791`이 이 구분을 정확히 설명 |

### 반복 오류 (2곳 이상)
- **ARM/MIPS 비정렬 접근** — `part01:110`, `part23:306`: "정렬되지 않은 접근 시 하드웨어 예외를 발생시킨다"는 과장. ARMv6/v7 이후와 AArch64는 Normal 메모리의 일반 load/store 비정렬 접근을 허용(예외는 Device 메모리, LDM/STM/LDRD, exclusive/atomic). `part19:145`도 "초기 SSE는 정렬 필수, 아니면 segfault"로 유사하게 부정확(SSE1부터 `movups` 존재).

---

## 3. 컴파일 불가 / 실행 불가 코드

| # | 위치 | 내용 |
|---|---|---|
| 1 | `part05:1042` | `prev_ = &other;` — `const LinkedPtr*` → `LinkedPtr*` const 제거 불가. `mutable`은 포인티 타입을 바꾸지 않음 |
| 2 | `part10:1488-1493` | `RobinHoodMap<K,V,Hash,uint8_t>` — 정의는 템플릿 인자 3개(L1316) |
| 3 | `part10:1274-1287` | `PointerHash`/`PointerEqual`을 선언 **전에** 멤버 타입으로 사용 (complete-class context는 멤버 타입에 적용 안 됨) |
| 4 | `part13:535-549` | 중첩 클래스 `EpochGuard`가 바깥 클래스의 **비정적** 멤버 `global_epoch` 접근 |
| 5 | `part15:834-839` | `virtual void f = 0;`, `virtual ~IBase = default;`, `IBase* CreateBase;` — 괄호 전부 누락 |
| 6 | `part08:1498` | `v.clear;`, `v.shrink_to_fit;`, `v = vector<int>;` — 호출 괄호 누락 |
| 7 | `part16:191-199,217-221` | `void process(...)` 함수에서 `return value;` |
| 8 | `part17:841-857` | `shape_tagged`의 union 멤버가 가상 소멸자를 가진 non-trivial 타입 → 기본 생성자·소멸자 암묵 삭제. `std::vector<shape_tagged>`도 불가 |
| 9 | `part17:1009-1011` | `std::align(..., BufferSize)` — 4번째 인자는 `size_t&`인데 비타입 템플릿 파라미터(prvalue) 전달 |
| 10 | `part17:279-295` | `any_cast`가 `any`의 private 멤버 접근 + `const any*`를 비-const 오버로드에 전달 |
| 11 | `part14:406-414` | `generator()`가 `helper`를 정의보다 먼저 호출, 전방 선언 없음 |
| 12 | `part11:963-971` | `create()`가 forwarding reference라 lvalue 인자에서 `Components = T&` → `std::vector<T&>` ill-formed |
| 13 | `part17:3,49,53` | `std::function<void>`, `void(*)` — 괄호 누락 |

### 비표준 확장 사용 (이식성)
- `part04:1834-1836` — `TRACKED_ALLOC` 매크로가 GCC/Clang statement expression `({...})` 사용. MSVC 불가
- `part21:563-570` — `[0 ... 59] = GRADE_F` range designated initializer는 **GCC 확장**(표준 C/C++ 모두에 없음)

---

## 4. 서지·인용 오류

| # | 위치 | 잘못된 표기 | 실제 |
|---|---|---|---|
| 1 | `part10:1641,1643` | "Designing a Fast, Efficient, Cache-friendly Hash Table" → Skarupke | **Matt Kulukundis, CppCon 2017**. 두 항목이 뒤바뀜 |
| 2 | `part09:1045` | Chandler Carruth "Intrusive Data Structures" CppCon 2014 | **"Efficiency with Algorithms, Performance with Data Structures"** (`part15:696`은 정확) |
| 3 | `part13:853` | Herb Sutter "atomic Weapons" **CppCon 2014** | **C++ and Beyond 2012** |
| 4 | `part15:401,945` | "GotW #18: Virtuality" + `gotw.ca/publications/xc++.htm` | **Sutter's Mill "Virtuality"**, `mill18.htm`, C/C++ Users Journal 2001 |
| 5 | `part21:832` | "Kim, H., et al. (2016). Branch Prediction for Free. MICRO" | **Ball & Larus, PLDI 1993** |
| 6 | `part21:829` | Yeh & Patt 1993, **ACM TOCS** | **ISCA 1993**, 제목은 "…that use Two Levels of Branch History" |
| 7 | `part16:964` | Pascal Cuoq, "Undefined behavior can result in time travel" | **Raymond Chen, The Old New Thing, 2014** |
| 8 | `part17:1181` | Kevlin Henney "Valued Conversions", Overload 2005 | **C++ Report, 2000** |
| 9 | `part07(algorithms 계열 아님)` `part08:1517` | Koenig "How Vectors Grow" Dr.Dobb's **1998** / Hinnant "On vector growth" **ACCU 2016** | 둘 다 원출처 확인 필요("C++ Made Easier" 칼럼은 *Accelerated C++*(2000) 이후 시작) |
| 10 | `part22:996` | Grant Ayers, "Understanding Branch Prediction", Cloudflare 블로그 | Cloudflare의 분기예측 글은 **Marek Majkowski**. Ayers는 AsmDB(ISCA 2019) 저자 |
| 11 | `part22:997` | Lemire, "Branch Misprediction Doesn't Slow Down Programs by 20%" | 해당 제목의 글 확인 불가 |
| 12 | `part22:1024` | "**Gernot** Goossens" | **Gert Goossens** (Marwedel & Goossens 편저) |

추가 확인 필요: `part23:1219` "Game Engine Architecture 3rd Ed., **Chapter 5.4: Serialization**" — GEA 3판 5.4는 Strings.

---

## 5. 근거 없는 수치 — 문서 자체 방법론과 모순

다섯 문서 모두 본문에서 "고정 배수를 만들지 말라"고 반복 경고하면서 표에서는 단정한다.

| 위치 | 문제 |
|---|---|
| `part15:245-259` | "Agner Fog에 따르면" 표가 vptr load(3-4)+vtable(3-4)+분기(1-2)를 **더해서 "총계 ~10 사이클"**. 파이프라인에서 겹치는 latency의 합산은 방법론적으로 틀림. 들어가며 "10배 느려진다"도 본문(L227,231,233,761)과 충돌 |
| `part17:860-868` | "가상 125ms / variant 45 / 직접배열 12, L1 miss 15%→0.5%"를 출처 없이 제시. L429-438의 자체 경고와 정면 충돌. 예제의 Circle/Rectangle이 vptr을 가지므로 variant 크기도 24B가 아니라 32B |
| `part20:576-586` | 1024² double 전치(원소 ~1.05M)에 "L1-dcache-load-misses 152,345,678 (45.2% of all loads)" — miss가 원소 수의 **150배**로 물리적으로 불가능 |
| `part18:5` | "L1 캐시 히트와 메인 메모리 사이에 **50배 이상**" — L779가 "고정된 한 표는 존재하지 않는다"고 스스로 경고 |
| `part06:143` | JSON 예제 "79바이트" — 2-space/LF로 세면 **71바이트** |

---

## 6. 파일별 상세

### part01 memory_allocation
- [오류] L97-108 비정렬 접근 산술 (§1.7)
- [오류] L299-305 `alignas(64)`를 C++17로 귀속
- [오류] L330 소제목 "NULL 포인터 역참조 감지" — 0xCC/0xCD는 NULL과 무관, "초기화되지 않은 포인터"가 맞음
- [부정확] L110 ARM/MIPS 비정렬 접근
- [부정확] L392-395 `#ifdef _DEBUG`로 malloc 재정의 → 실제 CRT는 `_CRTDBG_MAP_ALLOC` 필요
- [미미] L509 더 읽을거리에 BSTR 링크 — 본문에 BSTR 언급 전무(이전 버전 잔재)
- ✅ glibc 3플래그/하위3비트, SSE16/AVX32/AVX512-64 + #GP(0), long double x86-64 SysV 16B, `my_aligned_malloc` 크기 계산과 0x1004→0x1010, 0xCC=INT3, MSVC 패턴표 계층 구분

### part02 memory_architecture
- [오류] L30-36 계층도 방향
- [부정확] L218 "invalidate가 모든 코어에 전파" → snoop filter/directory로 sharer에게만
- [부정확] L283 128B 근거를 prefetcher로 설명 — ARM(Apple Silicon)은 실제 라인이 128B
- ✅ sets=32768/(64×8)=64, stride 4096=64set×64B, PML4/PDPT/PD/PT 비트범위, 1GB/4KB=262144, 1GB/2MB=512, FS/GS TLS, Lameter ACM Queue 2013, Sutter Dr.Dobb's 2009

### part03 memory_allocator
- [오류] L652-654,1546-1549 OSDI 2021 오귀속 (§2-1)
- [부정확] L32-42,108 현대 MSVC/UCRT malloc은 `HeapAlloc`에 위임, 서브페이지 관리는 NT Heap(LFH)
- [부정확] L475 "CTZ는 한 사이클" → TZCNT/BSF latency 3, throughput 1
- [부정확] L561-564 "Huge (4MB+)" — jemalloc 4.x 이후 large로 흡수(extent/muzzy/decay와 버전 혼재)
- [부정확] L1427 "Rust 등 이미 검증됨" — Rust는 1.32(2019)부터 시스템 할당자
- [미미] L334-336 large bin 경계 off-by-one, L884-905 `mi_page_s` 필드 의미, L1006 jemalloc은 radix tree(rtree), L1226 FlushViewOfFile 내구성
- ✅ tcache 64bin/최대 1032B(tidx2usize(63) 계산 일치), bin당 7개, fastbin index, small<1024·62개/large 63개, arena max=코어×8, jemalloc 내부단편화<20%·129→160=19.4%·100→112=10.7%, small 최대 14KiB, bitmap 32704×64=2093056 & 4088B, span 8pages/512객체, mimalloc segment 4MiB/page 64KiB, 1GiB÷4KiB=262144, allocation granularity 64KiB vs page 4KiB, rseq 설명

### part04 custom_allocator
- [오류] L11 TOC 앵커 깨짐(`#1-arena-allocator-가장-빠른-할당` ↔ 실제 제목 "bump allocation")
- [오류] L1767 `FMallocAnsi`는 표준 malloc 래퍼 — "mmap 직접"이 아님
- [이식성] L1834-1836 GNU statement expression
- [미미] L1203 `inode_init_early` → 실제는 `inode_init()`, `SLAB_MEM_SPREAD` 제거됨
- ✅ arena/stack 정렬 padding·overflow-safe 검사, stack header 정렬 논증, buddy XOR 16^8=24, Linux buddy order 0~10=4KB~4MB, buddy 내부낭비 ~50%와 Wilson 헤지, **PMR 절 전체**(allocator_traits 기본값 is_empty, POCS=false+unequal swap UB, polymorphic_allocator 복사대입 삭제, uses-allocator construction, std::string 별칭 고정, bad_array_new_length, null_memory_resource), Bonwick 1994·Wilson 1995·N3916·N2271·LWN SLUB

### part05 smart_pointer
- [오류] L599 역참조 3번 주장 (§2-5)
- [컴파일불가] L1042 `prev_ = &other;`
- [오류] L924-927 `CreateFileW` 실패 시 `INVALID_HANDLE_VALUE`(=-1)라 nullptr 검사에 안 걸림 → `CloseHandle(-1)`
- [불일치] L354-357 atomic 사이클 표가 L393·L654·L1239의 자체 경고와 어긋남
- [불일치] L824-833 `Pool::allocate()`가 선형 스캔(worst 1024) + mutex인데 문서는 O(1)로 소개
- [미미] L819 `std::aligned_storage_t`(C++23 deprecated), L848-857 생성자 예외 시 slot 누수, L1267 링크 rot
- ✅ C++17 인자 평가 인터리브 금지, `[[no_unique_address]]` C++20, Carruth CppCon 2019 + Itanium ABI non-trivial dtor, reference collapsing, intrusive release의 acq_rel, weak_ptr lock/expired 경합, enable_shared_from_this bad_weak_ptr, union 비활성 멤버 UB와 32bit 한정, **Unreal UObject=GC/UPROPERTY vs TSharedPtr 구분**

### part06 string
- [중대] L830-834 댕글링 string_view (§1.3)
- [오류] L151,155 "약 3초"
- [내부모순] L192 "C 표준 라이브러리에는 stpcpy" ↔ L213 "POSIX 확장"
- [오류] L1108 `CComBSTR::operator&()`는 `ATLASSERT(m_str==NULL)`만 하고 **해제하지 않음** → 주석 "기존 값 자동 해제"는 틀림
- [오류] L442-445 SSO 판별자: `buffer[15]`의 MSB는 long 모드에서 `length`의 최상위 바이트라 성립 불가 + 비활성 union 멤버 읽기 UB
- [미미] L920 `BSTR b = L"Hello"`는 C++11부터 컴파일 에러, L1004-1032 실제 BSTR은 CoTaskMemAlloc 아님, L1170 P0254R2 제목
- ✅ Shlemiel 합 n(n-1)/2, PDP-11 64KB, **SSO 구현체 표 전부 정확**(libstdc++ 15/32B, libc++ 22/24B, MSVC 15/32B), libc++ 비트 인코딩(size<<1 + LSB 플래그), MSVC data()==&s1, 황금비 이하 성장률 논증, N2668 2008·GCC 5 ABI, Qt `loadRelaxed`/Qt6, BSTR -4 오프셋과 바이트/문자 구분, "Test"→8

### part07 encoding
- [중대] L891-962 `is_valid_utf8` ASCII 분기 누락 (§1.2)
- [오류] L287 `[D8 3D][DE 00] (Little Endian)` — LE 바이트열은 `3D D8 00 DE`. L333이 정확해 자기모순
- [불일치] `decode_utf8` 시그니처 3인자 정의 ↔ L805,1251 2인자 호출
- [부정확] L1398-1403 noncharacter(U+FFFE 등)를 서로게이트와 함께 "금지"로 묶음 — noncharacter는 UTF-8 인코딩이 적법
- [부정확] L1420 Python 3 `str`은 PEP 393(Latin-1/UCS-2/UCS-4), UTF-8 아님
- [미미] L160-162 CP949 trail byte 구간 공백, L848 주석/동작 불일치
- ✅ 19×21×28=11172, 11172-2350=8822, U+4E00~9FFF=20,992/최초 20,902, U+AC00~D7A3=11,172, 0x10FFFF+1=1,114,112·17 plane, U+1F600 서로게이트 D83D/DE00 전 과정, "가"→EA B0 80·"😀"→F0 9F 98 80, **엄격 UTF-8 디코더(C2/E0-A0/ED-9F/F0-90/F4-8F) 전부 정확**, 가족 이모지 7코드포인트·25바이트, 타임라인 1988/1991/1992/1993/1996

### part08 vector
- [오류] L335-343,372-384 jemalloc size class와 400→512 예시 (§2-3)
- [컴파일불가] L1498 괄호 누락
- [확인필요] L1517 Koenig/Hinnant 서지
- [미미] L1264-1268 `move_if_noexcept` 조건 단순화, L1411 무조건 noexcept, L447 capacity 0 처리, L1037 FAM 표기
- ✅ **황금비 유도 전 과정 대수 검증 완료**, log_1.5 n ≈ 1.71 log₂n, 이동 상한 2.0배<n·1.5배<2n, invalidation 표 전체, end() 무효화 이유, erase-remove idiom, SmallVector 32/80/272B, strong exception guarantee, emplace_back 재할당 인덱스, P1144/P0843

### part09 intrusive_list
- [오류] L141-149 `std::list<GameObject>` 레이아웃 (§2-4)
- [서지] L1045 Carruth 발표 제목
- [부정확] L552 "kmalloc이 malloc보다 훨씬 비싸다" — SLUB per-CPU fast path는 빠름. 실제 이유는 실패 가능성/sleep/atomic context
- [불일치] L415,419 `container_of(current, NodePtr)`가 3인자 매크로 정의와 불일치
- [불일치] L615-620 skipfield 도식 ↔ L684-690 erase 코드
- [미미] L444 경로 `neo/` 누락, L479 "6,000회" 무출처, L981-987 C++17 guaranteed copy elision 필요
- ✅ container_of 오프셋 역산(0x1010-0x10=0x1000), **pointer-to-member offset·offsetof standard-layout 제약 경고**, 순환 리스트 insert/remove, 센티널, idLinkList 실제 필드, 커널 `__list_add`/`LIST_HEAD_INIT`, std::list<int> 24B→32B 청크, P0447/`std::hive`

### part10 hashmap
- [중대] L512-523 Robin Hood 규칙 반전 (§1.1)
- [중대] L1026,1046,1223,1254 string_view dangling key (§1.4)
- [오류] L882-883,917 H1/H2 비트 위치
- [서지] L1641,1643 Kulukundis/Skarupke 뒤바뀜
- [오류] L360-373 quadratic probing 코드가 c1=c2=1/2 공식과 불일치 — `(i*i)/2`는 i=0,1이 같은 슬롯. 삼각수 `(i*i+i)/2` 필요
- [컴파일불가] L1488-1493, L1274-1287
- [부정확] L531 "기대 분산 O(log log n)" → 알려진 결과는 **최대 PSL Θ(log log n)**
- [미미] L184-192 insert가 DELETED 너머 중복 키 미검출, L1078 SSO 이중 계산, L1089 경로 47자
- ✅ FNV-1a·murmur3 fmix32 상수, chaining 1+α/2, **linear probing 성공 ½(1+1/(1-α))·실패 ½(1+1/(1-α)²)와 α=0.9에서 50회가 상호 일치**, Celis 1986 Waterloo, "PSL 총합 불변", backward shift 추적, Swiss ctrl 바이트 값·64B=ctrl 64개, max load 7/8, 0x9e3779b9·2654435769=2^32/φ, 인터닝 73%/89%, **TMap/EASTL이 separate chaining이라는 정정**

### part11 ecs
- [오류] L822-834 순회 중 erase + `forEach` 미정의 (§1.8)
- [오류] L334 `pool.acquireHandle()` 미정의
- [불일치] L389-398 "역순 순회(제거 시 효율적)" 주석과 달리 중간 erase는 O(n²)
- [컴파일불가] L963-971 forwarding reference → `std::vector<T&>`
- [미미] L930-941 type-erased vector 누수, L1062-1073 AVX2 tail 없음, L844-847 들여쓰기, L131 Size==0 통과
- ✅ SPSC ring buffer memory order와 한정 조건, **`std::array<T,N>` 풀의 placement new 문제 자기지적**, SparseSet has/get/add/remove, "unordered_map range-for는 원소마다 hash lookup 안 함", **Slot Map 세대검증과 lock-free ABA 구분**, generation 래핑 주의, DOTS chunk 헤지

### part12 synchronization — ✅ 확정 오류 없음
- [미미] L650 C typedef에 C++ 멤버 초기화 혼재, L213 "표준이 제어 블록을 규정"(다음 문장에서 헤지)
- ✅ CRITICAL_SECTION 3경로 헤지, `_mm_pause` 두 역할, 진행 보장 표와 포함관계, lock-free 정의, shared_ptr implicit weak reference 모형, weak→shared CAS 루프, seq_cst 전역 총순서와 happens-before 사슬, **x86-64 seq_cst load/store 구현 차이와 "모든 seq_cst는 mfence" 반박**, x86 TSO/ARM `dmb ish`·`stlr`, SB litmus, **DCLP가 데이터 레이스라 "재배치 서사"로 설명하면 안 된다는 지적**, TTAS spinlock, Preshing 4편 URL 날짜 전부 일치, Michael&Scott PODC 1996, Herlihy&Shavit Ch3·Ch7, Intel SDM Vol.3A Ch.8, ARM genc007826

### part13 lockfree
- [중대] L484-493,528 Hazard Pointer StoreLoad (§1.5)
- [컴파일불가] L535-549 EpochGuard가 비정적 멤버 접근 + active 표시 없음
- [서지] L853 "atomic Weapons, CppCon 2014"
- [오류] L495-499 `~HPGuard()`가 레코드의 **모든** hazard 슬롯 해제
- [미미] L556 `epoch-2` 언더플로, L502-524 retired_list 재스캔 없음, L10-25 목차만 앵커 없음
- ✅ **"Blocking은 비차단 위계의 하위 항목이 아니다"**, Mars Pathfinder 1997, CAS/weak/strong·LL-SC, ABA 타임라인, 128bit CAS와 **`is_lock_free`는 비정적이라 `is_always_lock_free` 사용**, MS queue 구조와 CAS 전 data 읽기, **자기 벤치마크가 스레드별 지역 큐라 경합을 측정 못한다는 자기비판**, Herlihy TOPLAS 1991·Michael TPDS 2004·Fraser 2004·Kogan&Petrank 2011

### part14 coroutine_fiber — ✅ 확정 오류 없음
- [컴파일불가] L406-414 `helper` 전방 선언 없음
- [미미] L928-931 `(int)FlsGetValue` 축소 캐스트, L559-579 `current_value` 미선언
- ✅ `CreateFiberEx` 인자 순서와 **"dwStackSize는 전체 스택이 아니라 초기 commit"**, fiber proc 반환 시 스레드 종료, TLS vs FLS, glibc jmp_buf, **setjmp 함수 반환 후 longjmp는 UB**, `fcontext_t=void*`, 문맥 전환 push/pop 대칭, Boost.Context 출력 순서 검증, **C++20 Generator 전체(promise_type 5요소 + 0 1 4 9 16)**, HALO, **`await_suspend` 게시 시 happens-before·소유권 규약**, job counter release/acquire, work-stealing LIFO/FIFO 근거

### part15 vptr_polymorphism
- [수치] L245-259 Agner Fog 표 (§5), L3 "10배"
- [서지] L401,945 GotW #18 → Sutter's Mill "Virtuality"
- [컴파일불가] L834-839 괄호 누락 + 들여쓰기
- [부정확] L912 "ASan이 vptr 손상을 자동 감지" → vptr 검사는 UBSan `-fsanitize=vptr`
- [확인필요] L859-867 GCC vtable 덤프(Itanium은 가상 소멸자에 D1/D0 두 슬롯)
- [미미] L620-624 미사용 템플릿 파라미터, L512 "접근할 때마다 vbase offset", L940 MSVC ABI 링크가 호출규약 문서
- ✅ Simula 67, sizeof 1/8/4/16, Base 레이아웃 16B, 단일 상속 vtable 슬롯표, GCC -O2 asm, **비가상 소멸자 delete가 "~Base만 호출"이 아니라 UB라는 [expr.delete] 인용**, Meyers Item 9, 생성/소멸 vptr 단계 변경, **다중상속 C 레이아웃 32B 전 오프셋**, thunk `sub rdi,16`, Itanium -16/-8/0

### part16 compiler_ub
- [오류] L715-718 `__builtin_add_overflow` 반전 (§2-9)
- [오류] L356-360 예제 자체가 UB (§2-10)
- [개념오용] L253-269 "Happens-Before의 붕괴" — 단일 스레드는 sequenced-before + as-if 규칙. part12·part13이 정확히 사용
- [서지] L964 Raymond Chen
- [컴파일불가] L191-199,217-221 void 함수의 return
- [논증부실] L442-460 "wrap이면 unroll 불가"는 성립하지 않음. 실제 용도는 유도변수 64비트 확장·`i*2/2→i`·루프 종료 증명
- [오도] L630-641 "size_t를 쓰면 안전" — wrap이 정의된다고 올바른 건 아님. 규칙 7이 정답
- ✅ "no requirements" 인용, C99 Annex J.2 191개, **strict aliasing의 character type 예외와 uint8_t 바이트 접근 합법성**, C99 TC3 union punning 허용 vs **C++에서는 UB**, `std::bit_cast`, C11 6.5p5, `for(i=1;i>0;i++)` 무한루프, CVE-2009-1897과 `-fno-delete-null-pointer-checks`, **`unlikely`가 UB 추론을 되돌리지 못함**, ASan shadow 8:1과 1/8 주소공간, MSan/ASan 동시 불가, CMake 3.28 모듈

### part17 type_erasure
- [중대] L702-709 무한 재귀 (§1.6)
- [수치] L860-868 벤치마크 표 (§5)
- [컴파일불가] L841-857, L1009-1011, L279-295, L3/49/53
- [서지] L1181 Kevlin Henney
- [미미] L490-503 이동 후 원본 소멸자 미호출, L1070-1078 Cache Line Alignment 무설명
- ✅ SBO 판정에 nothrow-move 포함, **버퍼/힙 destroy 구분**, "호출 비용은 두 방식 모두 한 번의 간접 호출", `unique_ptr<void, void(*)(void*)>`, variant index_of/jump table, **"공유 소유권과 SBO는 양립 불가, 그래서 shared_ptr에 SBO가 없다"**, Rust fat pointer 16B와 `&dyn`/`Box<dyn>`

### part18 dod
- [오류] L254,272 `_mm256_store_ps`(**정렬 요구**)를 `std::vector<float>`에 사용. L248 주석이 스스로 "정렬을 보장하지 않는 배열"이라고 적음. 5절(L630-638)은 `storeu`로 정확
- [수치] L5 "50배 이상"
- [미미] L420 "50% 낭비" → 11/24 = 45.8%, L849-852 그래프의 "DOD 버전"으로 `vector<vector<>>` 제시(CSR이 표준 답이고 `part05:67`이 이미 CSR 설명), L666 sortedIndices 크기 미설정, L235 `_mm256_set_ps`를 "gather 연산"이라 표현, L1001 Nikolov 발표를 CPP Russia 2019로(part15:949는 CppCon 2018)
- ✅ **AoS/SoA 캐시라인 산술 전부**(24B/파티클, 3,750 vs 625 라인, 6배), Entity 256B 중 28B=89% 낭비와 280KB/2.5MB, EntityTransform 32B, ComponentGood 16B/18.75%, **`std::vector<vec3>`는 12B 간격이라 성분별 SIMD 로드 불가**, swap-and-pop 파티클 루프, 쿼터니언 적분 형태, C(1000,2)=499,500

### part19 simd — ✅ 확정 오류 없음
- [용어] L221-226 `a[i]=a[i-1]+b[i]`를 "recurrence 또는 **reduction**"으로 분류 — reduction은 다음 예제
- [부정확] L145 "초기 SSE는 정렬 필수, 아니면 segfault"(SSE1부터 `movups` 존재), L157 "Sandy Bridge 이후" ↔ part01:148 "Nehalem 이후"
- [미미] L490-522 `dot_avx2_unroll`에 return 없음, L71 SSE4 연도
- ✅ Flynn 1972, MMX/SSE/AVX 레지스터 확장, **FMA3가 AVX2와 별개이며 CPUID.01H:ECX.FMA[bit12]**, AVX-512 zmm0-31·k0-7·Alder Lake 제거, `_mm256_i32gather_ps`, **`std::aligned_alloc`의 MSVC 미제공 + size 배수 제약**, `_mm256_blendv_ps` 인자 순서, **수평 합 시퀀스 값 추적 검증**, `-ffast-math` 의미 변경 경고, SVE VLA/ACLE, WASM SIMD 2021

### part20 cache_oblivious
- [수식] L650-657 I/O 하한 (§2-11)
- [수치] L576-586 perf 예시 (§5)
- [불일치] L600 "256 sets" (§2-12)
- [확인필요] L462 "K = √(M/B)가 최적" — 외부 메모리 병합의 최적 fan-in은 Θ(M/B), funnel 제약은 k ≤ √M
- [미미] L258-260 "n ≤ 37" → 36, L739 GPU shared memory 48KB 고정
- ✅ n=2048에서 n²/8=524,288·n²=4,194,304, transpose_rec 사분면 매핑, **2의 거듭제곱 전제와 홀수 n 누락 경고**, base case가 cutoff라는 정정, **Morton 비트 트릭과 재귀판 일치**, **"van Emde Boas layout은 트리 *높이*를 재귀 분할하는 배치이며 2D bit interleaving과 구분"**, 정적 vEB O(log_B N), 동적 CO B-tree에 packed-memory array 필요, funnelsort n^(1/3)×n^(2/3), Strassen I/O bound, C block 누적 레이스

### part21 branch_prediction
- [코드결함] L653-662 branchless binary search (§1.9)
- [서지] L832 Ball&Larus, L829 ISCA 1993
- [이식성] L563-570 GCC range designated initializer
- [미미] L541-547 `x>>31`은 C++20 이전 구현정의 + INT_MIN UB, L192 Alder Lake perceptron 귀속(L686의 자체 경고와 상충), L831 L-TAGE 연도
- ✅ Pentium 4 20/31단계, BTFNT 60-70%, **2-bit counter의 노이즈 흡수**, BHT/PHT/BTB, GHR과 correlated branch, **"예측 정확도는 장르가 아니라 분기 지점별 속성"**, **"파이프라인 단계 수를 관측 cycle과 일대일 대응시키지 말라"**, 비트 트릭과 `(x+mask)^mask` 유도, **"부수효과가 있는 ternary는 CMOV로 변환되지 않는다"**, `[[likely]]` 지원 버전, Jiménez&Lin HPCA 2001·Rohou CGO 2015·SO 11227809

### part22 hot_cold_splitting
- [오류] L659,667,675 "Less common cases"라고 명시한 `t`/`f`/`n` 세 분기에 전부 `[[likely]]`. 상호배타적 분기를 모두 likely로 표시하는 것은 모순이며, `part21:357-366`이 "잘못된 힌트는 역효과"라고 경고한 사례
- [오류] L191-199 "좋은 배치" asm의 `.cold: jmp loop`가 `inc i`를 건너뛰어 무한루프
- [불일치] L923-927 ExtTSP를 **함수 배치**로 설명 — ExtTSP는 basic block 재배치(`-reorder-blocks=ext-tsp`), 함수는 hfsort(L516-517과 각주 [^8] "Improved Basic Block Reordering"이 정확)
- [미미] L770-771 `ASSERT` 매크로의 dangling-else(문서 L827-831은 `do{}while(0)` 사용), L325-343 `[[likely]]` 전후 asm이 사실상 동일해 차이를 못 보여줌, L939-957 hfsort 코드가 페이지 정렬을 실제로 하지 않음, L247 bullet과 L264 정정이 충돌
- [서지] L996,997,1024 (§4)
- ✅ **"파레토는 법칙이 아니라 측정값"**, I-cache 압력을 함수 크기로 환산할 수 없다는 지적, `-jt any,u` LBR 수집, BOLT 옵션(ext-tsp/hfsort/split-functions), **BOLT가 relocation 등 메타데이터를 요구한다는 단서**, `hot` 속성이 벡터화를 켜는 게 아니라 `.text.hot` 배치라는 정정, Ottoni CGO 2017·Chen CGO 2016·Panchenko CGO 2019·Shen ASPLOS 2023·Newell&Pupyrev 2020·Ayers ISCA 2019

### part23 serialization
- [오류] L154,352 `20.3f` 비트 (§2-13)
- [반복] L306 ARM/MIPS 비정렬
- [UB] L244-248 `*(uint32_t*)&p.x` strict aliasing 위반 — `part16:369`이 정확히 이 코드를 UB 예제로 사용
- [미미] L143 "79바이트"→71, L402-405 varint 중간 표기(최종 [AC 02]는 정확), L294/991 바이트 다이어그램에 ```json 펜스, L529 ↔ L584 표현 충돌, L1219 GEA 5.4
- ✅ **protobuf 태그 바이트 전수 검증**(08/15/1D/25/2A)과 총 24=2+5×3+7, varint 경계값 1/127/128/16383/16384, big-endian read가 `mov+bswap`로 접힘, packed 8바이트, FlatBuffers vtable 구조, **Cap'n Proto zero-copy가 "구조체 포인터 캐스트"가 아니라는 정정**, MovementPacket 18B·int16 고정소수점 ±327.67·델타 3B=16%, SaveFileHeader 20B/ChunkHeader 16B와 상수 정합, checksum 오프셋 전 필드 정합, **CRC-32/ISO-HDLC 구현과 단일비트+32bit burst 검출·2^-32**, **CRC는 변조 방지가 아니며 HMAC 필요**, **proto3는 커스텀 기본값 미지원**, Source BSP HEADER_LUMPS=64, Rob Pike byte order fallacy, RFC 8949

### part24 memory_profiling
- [오류] L645-648 `TWeakObjectPtr<FAnimInstanceProxy>` (§2-14)
- [용어] L232,239,282 shadow 값 1..7을 "부분 **초기화**"로 표현 — ASan은 addressability를 추적하고 초기화는 MSan 담당. 문서 자신이 L5·L114-115에서 이 구분을 정확히 함
- [부정확] L800 "함수 종료 시 use-after-return을 확인한다" — `detect_stack_use_after_return`은 기본 비활성
- [미미] L220-222 shadow 상한 주소값, L943 `HEAP_TAIL_CHECKING_ENABLED`는 GFlags(HeapAlloc 플래그 아님), L538 변수명 `used`가 heap_size를 담음
- ✅ ASan 논문 73% 인용과 헤지, shadow 1:8 매핑과 `(addr>>3)+offset`, **shadow byte 인코딩표(0xF1/F2/F3/F5/F8/FA/FB/FD)와 "음수면 접근 불가" 단일 비교**, `last_byte < k` fast path와 asm(`je`/`js`), redzone 최소 16B, 패딩이 shadow 값을 결정한다는 설명, MSan 1:1 비트 shadow와 오염 전파, **TSan ShadowCell 비트필드 16+42+1+3와 4:1 확장**, TSan 5~15배/5~10배(Clang 문서와 일치), LSan 플랫폼별 기본값, gperftools 512KB 샘플링, Heartbleed 64KB, **HWASan은 TBI 기반 software tagging이고 ARM MTE는 별도 hardware 경로**, Serebryany ATC 2012·Nethercote&Seward PLDI 2007·Stepanov&Serebryany CGO 2015·Serebryany&Iskhodzhanov WBIA 2009, MSVC `/fsanitize=address` VS2019 16.9

---

## 7. 시리즈 전반 권고

1. **반복 오류 일괄 수정**: ARM 비정렬 접근(part01/19/23), 함수 호출 괄호 누락(part08/15/17/23)
2. **수치 표 정책 통일**: part15/17/18/20의 하드 수치 표를 제거하거나 측정 조건과 함께 재작성. 이들 문서는 본문에서 스스로 "고정 배수를 만들지 말라"고 반복하므로 표만 예외가 되어 있다
3. **서지 일괄 재확인**: 12건 확인. 특히 Carruth·Sutter·Kulukundis 발표는 시리즈 내 다른 파일이 정확히 인용하고 있어 교차 대조가 쉽다
4. **비표준 확장 표시**: statement expression(part04), range designated initializer(part21)에 "GCC 확장" 명시
5. **자기 정정 문장의 위치**: part15(L247↔L264), part22(L247↔L264), part16(L192↔L213 유형) 등에서 bullet 목록 뒤 수십 줄 뒤에 정정이 오는 패턴이 있다. bullet 자체를 고치는 편이 낫다
