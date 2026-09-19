## 들어가며

코드는 왜 4배 빨라지지 않았는가?

AVX2의 256비트 레지스터에는 8개의 `float`이 들어가지만, 이것은 한 벡터 명령의 lane 수이지 프로그램 전체의 가속 배수가 아니다. 실제 처리량은 연산 의존성, load/store 대역폭, gather·shuffle 비용, tail 처리, 컴파일러가 이미 만든 scalar/vector 코드, CPU의 실행 포트와 주파수 정책에 따라 달라지며 느려질 수도 있다. **컴파일러는 언제 자동 벡터화에 실패하는가?** 그리고 그것을 어떻게 돕거나 우회할 수 있는가?

SIMD(Single Instruction, Multiple Data)는 현대 프로세서의 핵심 기능이지만, 그 힘을 제대로 끌어내려면 하드웨어, 컴파일러, 그리고 프로그래머가 함께 춤을 춰야 한다. 이 글은 SIMD의 역사적 맥락에서 시작해, 실제로 벡터화를 가로막는 장벽들을 하나씩 살펴본다.


---

## 목차
1. [SIMD란 무엇인가: Flynn's Taxonomy](#1-simd란-무엇인가-flynns-taxonomy)
2. [x86 SIMD의 진화](#2-x86-simd의-진화)
3. [인트린직의 세계](#3-인트린직의-세계)
4. [자동 벡터화: 컴파일러는 무엇을 보는가](#4-자동-벡터화-컴파일러는-무엇을-보는가)
5. [벡터화를 막는 것들](#5-벡터화를-막는-것들)
6. [레이아웃과 lane 적재 비용](#6-레이아웃과-lane-적재-비용)
7. [실전 최적화: 벡터화된 내적](#7-실전-최적화-벡터화된-내적)
8. [벡터화 체크리스트](#8-벡터화-체크리스트)
9. [SIMD의 미래](#9-simd의-미래)


---

## 1. SIMD란 무엇인가: Flynn's Taxonomy

1972년, Michael Flynn은 컴퓨터 아키텍처를 명령어 스트림(Instruction)과 데이터 스트림(Data)의 개수에 따라 네 가지로 분류했다.

- **SISD** (Single Instruction, Single Data): 전통적인 폰 노이만 아키텍처
- **SIMD** (Single Instruction, Multiple Data): 하나의 명령어가 여러 데이터에 동시 적용
- **MISD** (Multiple Instruction, Single Data): 이론적으로만 존재
- **MIMD** (Multiple Instruction, Multiple Data): 멀티코어 프로세서

SIMD는 데이터 병렬성(Data Parallelism)의 핵심이다. 같은 연산을 여러 데이터에 동시에 적용할 때 진가를 발휘한다.

```c
// 스칼라 버전 (SISD)
for (int i = 0; i < n; i++) {
    c[i] = a[i] + b[i];
}

// 벡터화 버전 (SIMD) - 개념적 표현
for (int i = 0; i < n; i += 8) {
    vec8_c[i/8] = vec8_a[i/8] + vec8_b[i/8];  // 8개를 한 번에
}
```

이 단순한 개념이 왜 실제로는 복잡한가? 하드웨어의 진화와 함께 살펴보자.

## 2. x86 SIMD의 진화

x86 아키텍처의 SIMD는 끊임없이 진화해왔다.

### MMX (1996)

Intel Pentium MMX에서 처음 등장. 64비트 레지스터(mm0-mm7) 8개를 제공했지만, FPU 레지스터를 재사용했기 때문에 부동소수점과 혼용할 수 없었다. 정수 연산만 지원했고, 주로 멀티미디어 처리에 사용되었다.

### SSE (1999)

Pentium III에서 도입. 128비트 레지스터(xmm0–xmm7) 8개와 독립적인 실행 유닛을 제공(64비트 x86-64에서 xmm0–xmm15 16개로 확장). 부동소수점 SIMD가 가능해졌다.

```c
// SSE: 128비트 = 4개의 float
__m128 a = _mm_load_ps(&arr[i]);      // 4개 로드
__m128 b = _mm_load_ps(&arr[i+4]);
__m128 c = _mm_add_ps(a, b);          // 4개 동시 덧셈
_mm_store_ps(&result[i], c);          // 4개 저장
```

SSE2(2000)는 정수 연산을, SSE3(2004)와 SSSE3(2006)은 추가 명령어를, SSE4(2006-2008)는 더 많은 연산을 추가했다.

### AVX (2011)

Sandy Bridge에서 도입. 레지스터 크기가 256비트(ymm0-ymm15)로 확장되었다. 중요한 변화는 VEX 인코딩을 도입해 3-operand 형식을 지원한 것이다.

```c
// SSE: 2-operand (destination을 덮어씀)
addps xmm0, xmm1    // xmm0 = xmm0 + xmm1

// AVX: 3-operand (non-destructive)
vaddps ymm0, ymm1, ymm2    // ymm0 = ymm1 + ymm2
```

### AVX2 (2013)

Haswell에서 도입. 정수 연산이 256비트로 확장되고 gather 명령어가 추가되었다.

```c
// Gather: 불연속적인 메모리 접근
__m256i indices = _mm256_set_epi32(7, 5, 3, 1, 6, 4, 2, 0);
__m256 data = _mm256_i32gather_ps(base_ptr, indices, 4);
```

FMA(Fused Multiply-Add)는 AVX2와 별개의 확장 집합이다. Intel은 이를 FMA3로 분리해 별도 CPUID 비트(`CPUID.01H:ECX.FMA[bit 12]`)로 표시하고, 컴파일러는 `-mfma`(GCC/Clang) 또는 `/arch:AVX2`(MSVC, AVX2와 함께 활성화)로 별도 켜야 한다. Haswell 이후 인텔 CPU는 AVX2와 FMA3를 함께 제공해 둘이 동의어처럼 쓰이지만, 사양상 별개라는 점이 정확하다.

```c
// FMA: a = a * b + c (한 명령어, IEEE 754 정확도가 더 높음)
__m256 result = _mm256_fmadd_ps(a, b, c);
```

### AVX-512 (2016~)

Xeon Phi와 Skylake-X에서 도입. 512비트 레지스터(zmm0-zmm31) 32개, 그리고 마스크 레지스터(k0-k7) 8개를 제공한다. 조건부 실행이 가능해졌다.

```c
// 마스크를 사용한 조건부 연산
__mmask16 mask = _mm512_cmp_ps_mask(a, b, _CMP_GT_OQ);  // a > b인 위치
__m512 result = _mm512_mask_add_ps(c, mask, a, b);      // mask가 1인 곳만 덧셈
```

하지만 AVX-512는 논란이 많다. 전력 소비가 크고, CPU 클럭이 떨어지며, 칩 면적을 많이 차지한다. Intel은 최신 클라이언트 CPU(Alder Lake 이후)에서 AVX-512를 제거했다.

핵심 교훈은 더 넓은 SIMD가 항상 더 빠른 것은 아니라는 점이다. 전력, 클럭 다운, 메모리 대역폭, 그리고 실제 벡터화 가능성을 모두 고려해야 한다.

## 3. 인트린직의 세계

SIMD를 사용하는 방법은 세 가지다.

1. **어셈블리**: 완전한 제어, 하지만 이식성 제로
2. **인트린직(Intrinsics)**: C 함수처럼 보이지만 직접 명령어로 매핑
3. **자동 벡터화**: 컴파일러에게 맡기기

인트린직은 중간 지점이다. [Intel Intrinsics Guide](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html)는 수천 개의 함수를 문서화하고 있다.

### 네이밍 컨벤션

```c
__m256 _mm256_add_ps(__m256 a, __m256 b);
//  │      │     │  │
//  │      │     │  └─ packed single (float)
//  │      │     └──── 연산 (add)
//  │      └────────── 256비트
//  └───────────────── 데이터 타입 (__m256 = 8개의 float)
```

접미사 규칙:
- `ps`: packed single (float)
- `pd`: packed double
- `epi32`: packed 32-bit integers
- `si256`: 256-bit integer vector (타입 불특정)

### 정렬 요구사항

SSE1부터 정렬을 요구하는 `MOVAPS`와 비정렬 주소를 허용하는 `MOVUPS`가 함께 존재했다. `_mm_load_ps`처럼 정렬형 intrinsic에 계약을 어긴 주소를 넘기면 fault나 UB가 발생할 수 있지만, SSE 전체가 비정렬 접근을 지원하지 않았던 것은 아니다.

```c
// 정렬된 로드/저장 (16바이트 경계)
__m128 a = _mm_load_ps(ptr);       // ptr은 반드시 16바이트 정렬
_mm_store_ps(ptr, a);

// 정렬되지 않은 로드/저장
__m128 b = _mm_loadu_ps(ptr);      // 어떤 주소든 가능
_mm_storeu_ps(ptr, b);
```

여러 현대 x86 CPU에서 한 캐시 라인 안의 unaligned load/store는 aligned 형태와 같은 실행 처리량을 낼 수 있다. 그러나 캐시 라인이나 페이지 경계를 가로지르는 접근, store-forwarding 제약, 오래된 CPU에서는 추가 비용이 생길 수 있다. 정렬은 경계 횡단 빈도를 줄이고 aligned intrinsic의 전제 조건을 만족시키지만 정확한 비용은 대상 마이크로아키텍처에서 측정한다.

C++17의 `std::aligned_alloc`이나 `alignas`를 사용한다.

```cpp
alignas(32) float data[1024];  // 32바이트 정렬 (AVX)

void* ptr = std::aligned_alloc(32, 1024 * sizeof(float));
```

플랫폼 주의: `std::aligned_alloc`은 C11/C++17 표준이지만 **MSVC는 이를 제공하지 않는다**(Windows에서는 `_aligned_malloc(size, alignment)`를 쓰고, 해제는 `free`가 아니라 `_aligned_free`로 해야 한다). 또한 표준 `aligned_alloc`은 size가 alignment의 배수여야 한다는 제약이 있다. C++17 이상이라면 `operator new`의 정렬 오버로드나 `std::pmr` 할당자가 이식성 있는 대안이다.

## 4. 자동 벡터화: 컴파일러는 무엇을 보는가

컴파일러(GCC, Clang, MSVC)는 점점 똑똑해지고 있다. 하지만 벡터화는 여전히 "될 때도 있고 안 될 때도 있다."

### 벡터화 리포트

```bash
# GCC
gcc -O3 -march=native -fopt-info-vec-optimized -fopt-info-vec-missed

# Clang
clang -O3 -march=native -Rpass=loop-vectorize -Rpass-missed=loop-vectorize

# MSVC
cl /O2 /arch:AVX2 /Qvec-report:2
```

이 플래그들은 컴파일러가 무엇을 벡터화했고, 무엇을 실패했는지 알려준다.

### 벡터화 가능한 루프

```c
// 벡터화됨: 단순한 map 연산
void add_arrays(float* a, float* b, float* c, int n) {
    for (int i = 0; i < n; i++) {
        c[i] = a[i] + b[i];
    }
}
```

컴파일러는 이를 대략 이렇게 변환한다.

```c
int i = 0;
for (; i + 8 <= n; i += 8) {
    __m256 va = _mm256_loadu_ps(&a[i]);
    __m256 vb = _mm256_loadu_ps(&b[i]);
    __m256 vc = _mm256_add_ps(va, vb);
    _mm256_storeu_ps(&c[i], vc);
}
// 나머지 처리
for (; i < n; i++) {
    c[i] = a[i] + b[i];
}
```

## 5. 벡터화를 막는 것들

### 데이터 의존성 (Loop-Carried Dependency)

```c
// 벡터화 불가: 이전 반복의 결과를 사용
for (int i = 1; i < n; i++) {
    a[i] = a[i-1] + b[i];  // a[i]가 a[i-1]에 의존
}
```

각 반복이 바로 이전 반복이 쓴 `a[i-1]`을 기다리는 **loop-carried recurrence**이므로 단순한 독립 lane 벡터화가 불가능하다. Reduction은 여러 입력을 하나의 합·최댓값 등으로 모으는 별도 패턴이다.

일부 reduction은 벡터화 가능하다.

```c
// 벡터화 가능: associative & commutative
float sum = 0.0f;
for (int i = 0; i < n; i++) {
    sum += a[i];  // 순서를 바꿔도 됨 (부동소수점 정밀도 제외)
}
```

컴파일러는 부분 합을 만들어 나중에 합친다.

```c
__m256 vsum = _mm256_setzero_ps();  // 8개의 부분 합
for (int i = 0; i < n; i += 8) {
    vsum = _mm256_add_ps(vsum, _mm256_loadu_ps(&a[i]));
}
// vsum의 8개 원소를 모두 더함
float sum = horizontal_sum(vsum);
```

### 포인터 앨리어싱 (Pointer Aliasing)

```c
// 벡터화 불확실: a와 b가 겹칠 수 있음
void add(float* a, float* b, int n) {
    for (int i = 0; i < n; i++) {
        a[i] = a[i] + b[i];
    }
}
```

만약 `a`와 `b`가 같은 메모리를 가리키거나 겹친다면, 벡터화가 결과를 바꿀 수 있다. C99의 `restrict` 키워드는 이를 해결한다.

```c
// 벡터화됨: 포인터가 겹치지 않음을 보장
void add(float* restrict a, float* restrict b, int n) {
    for (int i = 0; i < n; i++) {
        a[i] = a[i] + b[i];
    }
}
```

`restrict`는 "이 포인터를 통해서만 해당 메모리에 접근한다"는 프로그래머의 약속이다. 거짓말하면 undefined behavior다.

C++에는 `restrict`가 없지만, GCC/Clang은 `__restrict__`를, MSVC는 `__restrict`를 제공한다.

### 함수 호출

```c
// 벡터화 불가: 함수 호출
for (int i = 0; i < n; i++) {
    c[i] = expensive_function(a[i], b[i]);
}
```

컴파일러는 `expensive_function`의 내부를 모를 수 있다. 해결책은 두 가지다.

1. **인라인**: `inline` 또는 `-flto` (Link Time Optimization)
2. **벡터 함수**: `#pragma omp declare simd` (OpenMP) 또는 SVML (Short Vector Math Library)

```c
// OpenMP SIMD 선언
#pragma omp declare simd
float expensive_function(float a, float b) {
    return sqrtf(a * a + b * b);
}

#pragma omp simd
for (int i = 0; i < n; i++) {
    c[i] = expensive_function(a[i], b[i]);
}
```

### 복잡한 제어 흐름

```c
// 벡터화 어려움: 조건문
for (int i = 0; i < n; i++) {
    if (a[i] > threshold) {
        c[i] = a[i] * 2.0f;
    } else {
        c[i] = a[i] * 0.5f;
    }
}
```

AVX-512의 마스크 연산이 없다면, 컴파일러는 두 경로를 모두 계산하고 선택(blending)한다.

```c
for (int i = 0; i < n; i += 8) {
    __m256 va = _mm256_loadu_ps(&a[i]);
    __m256 mask = _mm256_cmp_ps(va, vthreshold, _CMP_GT_OQ);
    __m256 v1 = _mm256_mul_ps(va, _mm256_set1_ps(2.0f));
    __m256 v2 = _mm256_mul_ps(va, _mm256_set1_ps(0.5f));
    __m256 vc = _mm256_blendv_ps(v2, v1, mask);
    _mm256_storeu_ps(&c[i], vc);
}
```

이는 분기가 거의 50/50일 때 유리하다. 분기 예측이 잘 되는 상황(99% 한쪽)이라면 스칼라 버전이 더 빠를 수 있다.

### 불규칙한 stride 접근

```c
// 벡터화 어려움: stride 접근
for (int i = 0; i < n; i++) {
    c[i] = a[i*2] + b[i*3];  // 불규칙한 간격
}
```

이런 패턴은 gather/scatter나 셔플 명령어가 필요하며, 오버헤드가 크다.

## 6. 레이아웃과 lane 적재 비용

앞 절의 장벽들은 대부분 컴파일러가 루프의 **의미**를 확신하지 못해 생겼다. 마지막에 본 불규칙한 stride 접근은 성격이 다르다. 의미가 명확해도 **레지스터를 채우는 데 드는 명령 수**가 이득을 먹어 버리는 경우다.

벡터 레지스터 하나를 채우는 방법은 세 가지뿐이다. 연속된 메모리에서 한 번에 읽거나, 흩어진 주소에서 lane별로 모으거나, 이미 읽어 둔 레지스터들을 섞어 재배치하거나. 어느 쪽이 되는지는 필드가 메모리에 어떤 간격으로 놓여 있는지가 정한다. 구조체 배열에서 한 필드만 뽑으면 그 필드는 `sizeof(struct)` 간격으로 떨어져 있고, 필드별로 배열을 나눠 두면 4바이트 간격으로 붙어 있다.

### 세 가지 적재 경로의 명령 수

`float x, y, z, vx, vy, vz`를 가진 구조체(24바이트)의 배열에서 `x` 8개를 모은다고 하자.

| 경로 | 필요한 명령 | 비고 |
|---|---|---|
| 연속 load | `vmovups` 1개 | 필드별 배열일 때만 가능 |
| gather | `vgatherdps` 1개 + 인덱스 준비 | 명령은 하나지만 내부적으로 lane별 접근 |
| scalar 조립 | scalar load 8개 + 삽입 연산 | `_mm256_set_ps`가 생성하는 형태 |
| 전치 | load 4개 + shuffle·unpack 8개 | 4×4 블록을 한 번에 뒤집는다 |

`_mm256_set_ps`는 gather 명령이 아니다. 8개의 스칼라 load와 그것을 레지스터에 끼워 넣는 연산으로 펼쳐진다.

```c
// 이 한 줄은 명령 하나가 아니다
__m256 xs = _mm256_set_ps(p[7].x, p[6].x, p[5].x, p[4].x,
                          p[3].x, p[2].x, p[1].x, p[0].x);
```

`_mm256_i32gather_ps`는 명령 하나로 표현되지만 처리량이 연속 load와 같지 않다. 마이크로아키텍처에 따라 lane 수에 비례하는 사이클을 소비하며, 세대에 따라 처리량 차이가 크다. gather가 쓸 만한지는 대상 CPU에서 직접 재야 한다. 확실한 것은 **연속 load 한 개보다 싸지는 경우는 없다**는 점이다.

전치는 이미 읽은 데이터를 재배치하므로 메모리 접근이 아니라 셔플 포트를 쓴다. 4개 벡터를 한 번에 뒤집는 SSE 전치가 표준 형태다.

```c
// 4개의 __m128을 열/행 전치. 셔플·언팩 8개로 구성된다.
__m128 r0 = _mm_loadu_ps(&p[0].x);   // x0 y0 z0 vx0
__m128 r1 = _mm_loadu_ps(&p[1].x);
__m128 r2 = _mm_loadu_ps(&p[2].x);
__m128 r3 = _mm_loadu_ps(&p[3].x);
_MM_TRANSPOSE4_PS(r0, r1, r2, r3);   // r0 = x0 x1 x2 x3
```

구조체 필드 대부분을 읽는 루프라면 이 방법이 유리하다. 어차피 모든 바이트를 읽어야 하므로 메모리 트래픽은 같고, 재배치 비용만 추가되기 때문이다. 반대로 24바이트 중 4바이트만 쓰는 루프에서는 나머지 20바이트를 읽어 버리는 것 자체가 손해다.

### 판단 기준: 읽는 필드의 비율

레이아웃 선택을 벡터화 관점에서 정리하면 기준은 하나다. **루프가 읽는 필드가 구조체 전체에서 차지하는 비율.**

| 읽는 비율 | 유리한 배치 | 이유 |
|---|---|---|
| 낮음 (한두 필드) | 필드별 배열 | 연속 load 하나로 lane이 채워진다 |
| 높음 (대부분) | 구조체 배열 + 전치 | 메모리 트래픽이 같으므로 셔플 비용만 낸다 |
| 중간 | 자주 읽는 필드만 분리 | 나머지는 묶어 둔다 |

전부를 필드별 배열로 바꿀 필요는 없다. 벡터화하려는 루프가 실제로 읽는 필드만 떼어내면 그 루프의 적재 경로가 연속 load로 바뀐다.

### AoSoA: 블록 폭을 lane 수에 맞춘다

두 배치의 절충으로 작은 고정 크기 블록 안에서만 필드를 나누는 형태가 있다.

```cpp
struct Particle8 {
    float x[8], y[8], z[8];
    float vx[8], vy[8], vz[8];
};

Particle8 blocks[125];   // 125 * 8 = 1000
```

벡터화 관점에서 이 형태의 핵심은 블록 크기가 임의의 값이 아니라는 데 있다. **블록 폭은 벡터 lane 수의 배수여야 한다.** 8이면 AVX의 `__m256`이 블록 하나의 필드 배열을 정확히 한 번에 소비하고, 남는 원소가 없다. 블록 폭을 6이나 10으로 잡으면 블록마다 tail이 생겨 스칼라 처리가 블록 수만큼 반복된다.

블록 시작 주소를 정렬하면 블록 안의 각 필드 배열도 정렬된 주소에서 시작한다. 32바이트 정렬된 블록에서 `x[0]`의 주소는 블록 시작과 같고, `y[0]`은 32바이트 뒤이므로 역시 정렬되어 있다. AVX-512로 폭을 넓히려면 블록 폭을 16으로, 정렬을 64바이트로 올린다. 즉 이 배치는 **대상 ISA의 lane 수를 자료구조에 박아 넣는** 선택이며, 그 값이 바뀌면 레이아웃도 함께 바꿔야 한다는 유지 비용을 동반한다.

## 7. 실전 최적화: 벡터화된 내적

이론을 실전에 적용해보자. 벡터 내적(dot product)을 최적화한다.

### 버전 1: 스칼라

```c
float dot_scalar(const float* a, const float* b, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        sum += a[i] * b[i];
    }
    return sum;
}
```

### 버전 2: 자동 벡터화

```c
float dot_auto(const float* restrict a, const float* restrict b, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        sum += a[i] * b[i];
    }
    return sum;
}
// 컴파일: gcc -O3 -march=native -ffast-math
```

`restrict`와 `-ffast-math`(부동소수점 재결합 허용)가 핵심이다.

### 버전 3: 명시적 인트린직 (AVX2)

```c
float dot_avx2(const float* a, const float* b, int n) {
    __m256 vsum = _mm256_setzero_ps();  // 8개의 부분 합

    int i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 va = _mm256_loadu_ps(&a[i]);
        __m256 vb = _mm256_loadu_ps(&b[i]);
        vsum = _mm256_fmadd_ps(va, vb, vsum);  // vsum += va * vb
    }

    // 수평 합
    __m128 lo = _mm256_castps256_ps128(vsum);
    __m128 hi = _mm256_extractf128_ps(vsum, 1);
    lo = _mm_add_ps(lo, hi);
    lo = _mm_hadd_ps(lo, lo);
    lo = _mm_hadd_ps(lo, lo);

    float sum = _mm_cvtss_f32(lo);

    // 나머지 처리
    for (; i < n; i++) {
        sum += a[i] * b[i];
    }

    return sum;
}
```

### 버전 4: 언롤링 + 인트린직

```c
float dot_avx2_unroll(const float* a, const float* b, int n) {
    __m256 vsum0 = _mm256_setzero_ps();
    __m256 vsum1 = _mm256_setzero_ps();
    __m256 vsum2 = _mm256_setzero_ps();
    __m256 vsum3 = _mm256_setzero_ps();

    int i = 0;
    for (; i + 32 <= n; i += 32) {
        __m256 va0 = _mm256_loadu_ps(&a[i]);
        __m256 vb0 = _mm256_loadu_ps(&b[i]);
        vsum0 = _mm256_fmadd_ps(va0, vb0, vsum0);

        __m256 va1 = _mm256_loadu_ps(&a[i+8]);
        __m256 vb1 = _mm256_loadu_ps(&b[i+8]);
        vsum1 = _mm256_fmadd_ps(va1, vb1, vsum1);

        __m256 va2 = _mm256_loadu_ps(&a[i+16]);
        __m256 vb2 = _mm256_loadu_ps(&b[i+16]);
        vsum2 = _mm256_fmadd_ps(va2, vb2, vsum2);

        __m256 va3 = _mm256_loadu_ps(&a[i+24]);
        __m256 vb3 = _mm256_loadu_ps(&b[i+24]);
        vsum3 = _mm256_fmadd_ps(va3, vb3, vsum3);
    }

    // 4개의 부분 합을 합침
    vsum0 = _mm256_add_ps(vsum0, vsum1);
    vsum2 = _mm256_add_ps(vsum2, vsum3);
    vsum0 = _mm256_add_ps(vsum0, vsum2);

    // 8개 lane 수평 합
    __m128 lo = _mm256_castps256_ps128(vsum0);
    __m128 hi = _mm256_extractf128_ps(vsum0, 1);
    lo = _mm_add_ps(lo, hi);
    lo = _mm_hadd_ps(lo, lo);
    lo = _mm_hadd_ps(lo, lo);
    float sum = _mm_cvtss_f32(lo);

    // 32개 단위로 처리하고 남은 scalar tail
    for (; i < n; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}
```

누적 합 하나에는 FMA 결과가 같은 누적기의 입력으로 돌아오는 dependency chain이 생긴다. 독립 누적기를 여러 개 두면 CPU가 여러 연산을 겹쳐 FMA의 latency보다 throughput에 가까워질 수 있다. 필요한 누적기 수와 FMA latency/throughput은 마이크로아키텍처마다 다르며, 과도한 언롤은 레지스터 압박과 코드 크기를 늘린다.

### 성능 비교 방법

`scalar`, 일반 최적화, 자동 벡터화, intrinsic, unroll 버전을 같은 데이터와 부동소수점 계약으로 비교한다. `-ffast-math`는 결합법칙 재배치, NaN/Inf·signed zero 처리 같은 의미를 바꾸므로 단지 “빠른 최적화 옵션”으로 baseline에 섞으면 안 된다. 정확한 합 순서가 필요한 버전과 재결합을 허용한 버전을 별도 결과로 둔다.

결과에는 CPU·컴파일러·플래그, vectorization report, 입력이 cache에 상주하는지, bytes/s와 FLOP/s를 기록한다. 메모리 대역폭이 포화되면 AVX 폭을 늘려도 속도가 비례하지 않고, 컴파일러가 같은 명령을 생성했다면 intrinsic의 이득도 없다.

Agner Fog의 *Optimizing software in C++*가 일관되게 권하는 순서도 같다. 먼저 측정하고, 자동 벡터화로 충분한지 확인한 뒤에야 인트린직의 유지보수 비용을 감수할 가치가 있는지 판단한다.

## 8. 벡터화 체크리스트

벡터화를 시도할 때 확인할 항목:

1. **프로파일링**: Hot loop를 찾는다. 전체 시간의 90%를 차지하는 10%의 코드에 집중.
2. **컴파일러 리포트**: 왜 벡터화되지 않는지 묻는다.
3. **데이터 레이아웃**: SoA가 가능한가?
4. **의존성 제거**: `restrict`, loop fission, 임시 변수.
5. **정렬**: 가능하면 맞춘다. 최소한 캐시 라인을 넘지 않게.
6. **컴파일러 힌트**:
 ```c
 #pragma GCC ivdep // "ignore vector dependencies"
 #pragma clang loop vectorize(enable)
 #pragma omp simd
 ```
7. **측정**: 실제 성능을 측정한다. 이론적 속도 향상은 실제 속도 향상과 다를 수 있다.

## 9. SIMD의 미래

### ARM NEON과 SVE

ARM도 SIMD를 가지고 있다. NEON은 128비트 고정 폭이지만, SVE(Scalable Vector Extension)는 가변 폭(128~2048비트)을 지원한다. 핵심은 벡터 길이가 컴파일 시점이 아니라 런타임 하드웨어에 의해 결정된다는 점이다. 코드는 길이를 모른 채 작성하고(VLA, vector length agnostic), 같은 바이너리가 128비트 단위로 다른 폭(128~2048비트)을 갖는 다양한 ARM 칩에서 그대로 작동한다.

```c
// SVE: 벡터 길이를 모른 채 코드 작성
svfloat32_t va = svld1_f32(pg, &a[i]);
svfloat32_t vb = svld1_f32(pg, &b[i]);
svfloat32_t vc = svadd_f32_z(pg, va, vb);
svst1_f32(pg, &c[i], vc);
```

### WebAssembly SIMD

WASM도 128비트 SIMD를 지원한다(2021년부터 표준). 웹 브라우저에서도 고성능 계산이 가능해졌다.

### GPU와의 관계

SIMD와 GPU는 모두 데이터 병렬성을 다루지만, 규모가 다르다.

- SIMD: 8-16개의 데이터 (CPU 코어당)
- GPU: 수천 개의 데이터 (수천 개의 코어)

작은 데이터나 복잡한 제어 흐름은 SIMD가, 거대한 데이터와 단순한 연산은 GPU가 유리하다.


---

## 결론

SIMD는 공짜 점심이 아니다. 하드웨어는 가능성을 제공하지만, 그 힘을 끌어내는 것은 프로그래머와 컴파일러의 협력에 달려 있다.

핵심 교훈은 다음과 같다. 컴파일러를 믿되 생성 코드를 확인한다. `-O3`와 대상 ISA 설정은 출발점이며, 벡터화 리포트를 읽어야 한다. 필요한 field만 밀집 순회할 때 SoA가 유리할 수 있지만 모든 field를 함께 쓰는 작업은 AoS가 나을 수 있다. alias와 loop-carried dependency를 제거하거나 알고리즘을 재설계한다. lane 수로 프로그램 가속 배수를 단정하지 않고 bandwidth, cache, 분기와 downclock을 함께 측정한다. 인트린직은 ISA 종속성과 유지 비용이 있으므로 자동 벡터화가 충분한 경로에서는 더 단순한 코드를 유지한다.

벡터화된 코드도 캐시 미스 앞에서는 무력하다. SIMD의 효과는 연산 폭만이 아니라 메모리 배치, 접근 패턴, prefetch, 캐시 라인 활용률이 함께 맞을 때 드러난다.

## 더 읽을거리

- Flynn, M. J. (1972). "Some Computer Organizations and Their Effectiveness". IEEE Transactions on Computers.
- [Intel Intrinsics Guide](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/) - 모든 인트린직의 바이블.
- Fog, A. (2023). "Optimizing software in C++". https://www.agner.org/optimize/
- Fog, A. "The microarchitecture of Intel, AMD and VIA CPUs".
- Stephens, N. et al. (2017). "The ARM Scalable Vector Extension". IEEE Micro 37(2), 26-39.
- Lemire, D. https://lemire.me/ - SIMD 최적화 블로그.
- [Godbolt Compiler Explorer](https://godbolt.org/) - 컴파일러가 실제로 무엇을 생성하는지 확인.
- Ulrich Drepper, "What Every Programmer Should Know About Memory" (2007).
