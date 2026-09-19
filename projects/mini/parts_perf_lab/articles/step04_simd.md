## 들어가며

현재까지의 결과물은 layout 토글로 SoA·AoS·Archetype 세 변종을 *런타임에 교체* 하는 측정 도구다. 핫 루프는 모두 scalar로 도는 상태고, 컴파일러가 자동 벡터화를 시도하지만 *경계 반사 분기* 때문에 부분 벡터화에 그친다. SoA의 핫 루프 구조는 이상적인데, 컴파일러 자동화의 손길이 닿지 않는 부분이 남아 있다는 뜻이다.

이 단계의 목표는 *수동 SIMD 인트린식* 으로 같은 핫 루프를 직접 작성해 SSE·AVX2 경로를 만드는 것이다. 자동 벡터화에 의존하지 않고 4개(SSE) 또는 8개(AVX2) float을 한 명령으로 처리하는 코드를 직접 박는다. 분기는 *마스크 + 비트 select* 로 분기 자체를 SIMD 안에서 처리한다. SIMD 토글이 활성화되어 콤보박스 한 번 클릭으로 Scalar → SSE → AVX2 경로가 즉시 교체되고, 그 차이가 그래프에 그려진다.

이 단계의 결과물은 다음 다섯 가지다.

1. 플랫폼 가드 헤더 `platform.h` (x86/x64에서만 SIMD 코드 컴파일)
2. CMake 옵션 `PERF_LAB_ENABLE_AVX2` (기본 OFF, 켜면 AVX2/FMA 활성화)
3. SoA에 SSE/AVX2 핫 루프 추가
4. Archetype에 SSE/AVX2 핫 루프 추가
5. AoS는 의도적으로 scalar 유지 (stride 문제로 SIMD 효과 없음)

---

## 목차
1. [자동 벡터화 vs 수동 SIMD](#1-자동-벡터화-vs-수동-simd)
2. [플랫폼·CPU 기능 가드](#2-플랫폼cpu-기능-가드)
3. [SSE2 안전 select 패턴](#3-sse2-안전-select-패턴)
4. [SoA의 SSE 경로](#4-soa의-sse-경로)
5. [SoA의 AVX2 경로와 FMA](#5-soa의-avx2-경로와-fma)
6. [Archetype의 chunk 단위 SIMD](#6-archetype의-chunk-단위-simd)
7. [AoS는 왜 scalar로 두는가](#7-aos는-왜-scalar로-두는가)
8. [Hud 활성화와 빌드·실행 결과](#8-hud-활성화와-빌드실행-결과)

---

## 1. 자동 벡터화 vs 수동 SIMD

기존 SoA 핫 루프는 다음 형태였다.

```cpp
for (std::size_t i = 0; i < count_; ++i) {
    posX_[i] += velX_[i] * dt;          // (a) 산술
    posY_[i] += velY_[i] * dt;

    if (posX_[i] < 0.0f || posX_[i] > maxX) {  // (b) 분기 + 부호 반전 + clamp
        velX_[i] = -velX_[i];
        posX_[i] = std::clamp(posX_[i], 0.0f, maxX);
    }
    // ...
}
```

`-O3` 또는 `/O2` 빌드에서 컴파일러는 이 루프를 다음과 같이 본다.

- (a) 산술 부분은 *SIMD로 자동 변환 가능*. 4개 float을 SSE 한 명령으로 처리.
- (b) 분기 부분은 *반환값이 데이터 의존* 이라 자동 벡터화가 막히거나 부분 처리에 그친다.

결과적으로 자동 벡터화는 *반쪽짜리* 다. 산술은 빠르지만 분기는 lane별 scalar 처리. 수동 SIMD는 분기까지 *마스크 연산* 으로 풀어 쓴다.

수동 SIMD의 트레이드오프는 다음과 같다.

| 항목 | 자동 벡터화 | 수동 SIMD |
|---|---|---|
| 코드 분량 | 동일 (그대로) | 별도 함수 추가 |
| 분기 처리 | 어렵거나 부분 | 마스크로 통합 |
| 컴파일러 의존 | 큼 (버전·옵션마다 결과 다름) | 작음 (인트린식이 곧 명령) |
| 가독성 | 일반 C++ | 인트린식 학습 필요 |
| 측정 일관성 | 낮음 | 높음 |

이 도구는 *측정 일관성* 이 중요하므로 수동 SIMD를 추가한다. 자동 벡터화 결과는 baseline (`Scalar` 토글) 으로 남겨 비교한다.

## 2. 플랫폼·CPU 기능 가드

수동 SIMD 코드를 모든 환경에서 그대로 컴파일하면 두 가지 문제가 생긴다.

1. **비-x86 플랫폼**: ARM (Apple Silicon, 모바일 등)에서 `<immintrin.h>` 가 없거나 `__m128` 타입이 정의되지 않는다. 컴파일 자체가 실패.
2. **x86 안에서도 명령 셋 차이**: SSE2는 모든 x86-64에서 보장되지만 AVX2는 추가 옵션 (`/arch:AVX2`, `-mavx2`) 이 필요. 옵션 없이 AVX2 인트린식을 쓰면 GCC/Clang은 *target specific option mismatch* 류 오류를 내고, MSVC는 진단 없이 컴파일해 AVX2 명령을 그대로 방출한다.

해결 방법: *컴파일 시점 매크로 가드*.

<!-- snippet: src/platform.h -->
```cpp
// src/platform.h

#pragma once

// PERF_LAB_X86_SIMD: x86/x64 SSE2 인트린식 사용 가능 여부.
//   - x86_64 / AMD64는 SSE2가 ABI baseline이라 항상 보장.
//   - 32-bit x86은 컴파일 옵션으로 SSE2가 명시적으로 켜진 경우만 정의.
//     (__SSE2__는 GCC/Clang, _M_IX86_FP >= 2는 MSVC의 /arch:SSE2 지표)
#if defined(__x86_64__) || defined(_M_X64) \
    || defined(__SSE2__) \
    || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#  define PERF_LAB_X86_SIMD 1
#endif

// PERF_LAB_AVX2: AVX2/FMA 인트린식 사용 가능 여부.
//   - CMake의 PERF_LAB_ENABLE_AVX2 옵션이 ON일 때 정의된다.
//   - PERF_LAB_X86_SIMD가 정의된 경우에만 의미가 있다.

#if defined(PERF_LAB_X86_SIMD)
#  include <immintrin.h>
#endif
```

매크로 두 단계로 구분한다.

- `PERF_LAB_X86_SIMD`: 플랫폼 + SSE2 보장 여부. x86_64는 ABI baseline이라 자동 통과, 32-bit x86은 SSE2 활성 빌드에서만 통과해 *SSE2 미지원 빌드에서 SIMD 코드가 컴파일되는* 사고를 막는다.
- `PERF_LAB_AVX2`: 빌드 옵션 기준. CMake가 `target_compile_definitions` 로 설정한다.

`<immintrin.h>` 한 헤더로 SSE2부터 AVX2까지 모두 들어온다. MSVC도 같은 헤더를 받아들이므로 컴파일러별 분기는 필요 없다.

### CMakeLists.txt

```cmake
# SIMD(AVX2) 활성화 옵션. 켜지 않은 채로 빌드해도 SSE 경로는 SSE2까지만
# 사용하므로 모든 x86-64에서 정상 동작한다.
option(PERF_LAB_ENABLE_AVX2
       "Enable AVX2/FMA compiler flags for SIMD modules" OFF)

# AVX2 플래그는 x86/x64 타깃에서만 의미가 있다. ARM 등에서 -mavx2 를 붙이면
# 컴파일 자체가 깨지므로 타깃 아키텍처를 먼저 판정한다.
set(PERF_LAB_TARGET_X86 OFF)
if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x86|x64|X64)$"
   OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86)$")
    set(PERF_LAB_TARGET_X86 ON)
endif()

if(PERF_LAB_ENABLE_AVX2)
    if(PERF_LAB_TARGET_X86)
        # 앱 타깃과 테스트 타깃에 동일하게 부여한다. 테스트에 주지 않으면
        # UpdateAVX2 가 매크로 가드 때문에 테스트 바이너리에서 아예 컴파일되지
        # 않아 AVX2 경로가 무검증으로 남는다.
        target_compile_definitions(${PROJECT_NAME} PRIVATE PERF_LAB_AVX2=1)
        target_compile_definitions(parts_core_tests PRIVATE PERF_LAB_AVX2=1)
        if(MSVC)
            target_compile_options(${PROJECT_NAME} PRIVATE /arch:AVX2)
            target_compile_options(parts_core_tests PRIVATE /arch:AVX2)
        else()
            target_compile_options(${PROJECT_NAME} PRIVATE -mavx2 -mfma)
            target_compile_options(parts_core_tests PRIVATE -mavx2 -mfma)
        endif()
    else()
        message(WARNING
            "PERF_LAB_ENABLE_AVX2 is ON, but the target is not x86/x64. "
            "AVX2 code is disabled and runtime SIMD selections fall back.")
    endif()
endif()
```

빌드 시 `cmake -DPERF_LAB_ENABLE_AVX2=ON` 으로 켠다. 옵션이 꺼진 빌드에서 AVX2 토글을 선택하면 SSE 경로로 폴백한다(런타임 동작). 비-x86/x64 타깃에서 이 옵션을 켜도 CMake는 AVX2 컴파일 플래그를 붙이지 않고, SIMD 경로는 scalar로 폴백한다.

### Update 분기 패턴

이 두 매크로를 받아서 SIMD 토글에 따라 적절한 경로를 호출한다.

```cpp
void SoASimulation::Update(float dt, SimdWidth simd, bool /*branchless*/) {
    switch (simd) {
        case SimdWidth::Scalar:
            UpdateScalar(dt);
            break;
        case SimdWidth::SSE:
#if defined(PERF_LAB_X86_SIMD)
            UpdateSSE(dt);
#else
            UpdateScalar(dt);  // 비-x86 플랫폼은 scalar로 폴백
#endif
            break;
        case SimdWidth::AVX2:
#if defined(PERF_LAB_X86_SIMD) && defined(PERF_LAB_AVX2)
            UpdateAVX2(dt);
#elif defined(PERF_LAB_X86_SIMD)
            UpdateSSE(dt);     // AVX2 빌드 옵션이 꺼져 있으면 SSE로 폴백
#else
            UpdateScalar(dt);  // 비-x86 플랫폼은 scalar로 폴백
#endif
            break;
    }
}
```

3중 가드를 통과해야 AVX2 코드가 실행된다. 이러면 다음 시나리오 모두 안전하다.

- Apple Silicon에서 `cmake -DPERF_LAB_ENABLE_AVX2=ON` → 컴파일 OK, 런타임에 모두 scalar
- Intel CPU에서 옵션 OFF → SSE/AVX2 토글 모두 SSE 또는 scalar
- AVX2/FMA 지원 x86/x64 CPU에서 옵션 ON → SSE/AVX2 토글 각각 해당 경로

주의할 점은 *런타임 CPU 기능 검출은 아직 없다는 것* 이다. `/arch:AVX2`(`-mavx2 -mfma`)는 타깃 전체에 붙으므로 scalar·SSE 경로까지 VEX 인코딩으로 컴파일된다. 그래서 이 옵션으로 빌드한 바이너리는 SIMD 토글과 무관하게 실행 CPU가 AVX2와 FMA를 지원해야 하고, 지원하지 않는 CPU에서는 시작 직후 illegal instruction이 날 수 있다. 이 실험 도구는 빌드 옵션을 명시적으로 켠 개발 환경에서 측정한다는 전제를 둔다.

## 3. SSE2 안전 select 패턴

SIMD에서 *분기* 를 처리하는 방법은 *마스크* 를 쓰는 것이다. 비교 결과(`__m128`) 가 *각 lane별로 모든 비트가 1 또는 0* 이라, 그 마스크로 두 값 중 하나를 lane별로 선택한다.

SSE4.1에는 이 동작을 한 명령으로 하는 `_mm_blendv_ps` 가 있다.

```cpp
// SSE4.1: 한 명령으로 lane별 select
__m128 result = _mm_blendv_ps(a, b, mask);  // mask가 1이면 b, 0이면 a
```

문제는 `_mm_blendv_ps` 가 *SSE4.1* 명령이라는 점이다. CMake에 `-msse4.1` 옵션이 없으면 GCC/Clang에서 *target specific option mismatch* 컴파일 오류가 난다. 기본 빌드에서 깨지지 않게 하려면 *SSE2까지만* 사용하는 패턴이 필요하다.

### SSE2 bitwise select

같은 동작을 SSE2 명령 셋으로 풀어 쓰면 다음과 같다.

```cpp
// SSE2 bitwise select: mask 비트가 1인 lane은 b, 0인 lane은 a
inline __m128 SseSelect(__m128 a, __m128 b, __m128 mask) {
    return _mm_or_ps(_mm_andnot_ps(mask, a), _mm_and_ps(mask, b));
}
```

세 비트 연산으로 같은 결과를 얻는다. 명령 수는 늘지만 SSE2가 모든 x86-64 ABI의 baseline이라 추가 옵션 없이 컴파일된다.

수식으로 보면 다음과 같다.

$$
\text{result}_i = (\overline{\text{mask}_i} \wedge a_i) \vee (\text{mask}_i \wedge b_i)
$$

`mask_i` 가 모든 비트 1일 때 `~mask_i` 는 모든 비트 0이므로 `a` 의 lane이 0이 되고 `b` 의 lane이 그대로 OR된다. 반대도 마찬가지. *각 lane이 독립적으로* 선택된다.

AVX2 경로는 `_mm256_blendv_ps` 가 AVX1부터 있어 그대로 쓸 수 있다(AVX2 컴파일 컨텍스트에서 보장됨).

## 4. SoA의 SSE 경로

수동 SIMD 핫 루프의 전체 흐름은 다음과 같다.

```mermaid
flowchart LR
    Load["4개 float 로드<br/>posX/posY/velX/velY"] --> Add["pos += vel * dt<br/>add + mul"]
    Add --> Cmp["경계 비교<br/>cmplt + cmpgt"]
    Cmp --> Mask["OR로 마스크 합성"]
    Mask --> Sel["select로 vel 부호 반전<br/>SseSelect(v, -v, mask)"]
    Sel --> Clamp["pos clamp<br/>min + max"]
    Clamp --> Store["4개 float 저장"]
```

코드 전체:

```cpp
#if defined(PERF_LAB_X86_SIMD)

namespace {
inline __m128 SseSelect(__m128 a, __m128 b, __m128 mask) {
    return _mm_or_ps(_mm_andnot_ps(mask, a), _mm_and_ps(mask, b));
}
}  // namespace

void SoASimulation::UpdateSSE(float dt) {
    const float w = static_cast<float>(viewportW_);
    const float h = static_cast<float>(viewportH_);

    const __m128 dtv  = _mm_set1_ps(dt);
    const __m128 zero = _mm_setzero_ps();
    const __m128 wMax = _mm_set1_ps(w - 1.0f);
    const __m128 hMax = _mm_set1_ps(h - 1.0f);

    // 4개 float씩 SoA 핫 루프.
    std::size_t i = 0;
    for (; i + 4 <= count_; i += 4) {
        __m128 px = _mm_loadu_ps(&posX_[i]);
        __m128 py = _mm_loadu_ps(&posY_[i]);
        __m128 vx = _mm_loadu_ps(&velX_[i]);
        __m128 vy = _mm_loadu_ps(&velY_[i]);

        // pos += vel * dt
        px = _mm_add_ps(px, _mm_mul_ps(vx, dtv));
        py = _mm_add_ps(py, _mm_mul_ps(vy, dtv));

        // 경계 마스크: pos < 0 OR pos > max
        const __m128 mX = _mm_or_ps(_mm_cmplt_ps(px, zero),
                                     _mm_cmpgt_ps(px, wMax));
        const __m128 mY = _mm_or_ps(_mm_cmplt_ps(py, zero),
                                     _mm_cmpgt_ps(py, hMax));

        // 마스크 영역만 vel 부호 반전 (SSE2 bitwise select)
        vx = SseSelect(vx, _mm_sub_ps(zero, vx), mX);
        vy = SseSelect(vy, _mm_sub_ps(zero, vy), mY);

        // pos clamp [0, max]
        px = _mm_max_ps(zero, _mm_min_ps(wMax, px));
        py = _mm_max_ps(zero, _mm_min_ps(hMax, py));

        _mm_storeu_ps(&posX_[i], px);
        _mm_storeu_ps(&posY_[i], py);
        _mm_storeu_ps(&velX_[i], vx);
        _mm_storeu_ps(&velY_[i], vy);
    }
```

벡터 폭으로 나누어떨어지지 않는 꼬리 구간은 scalar 루프로 처리한다.

<!-- snippet: src/sim/soa.cpp#SoASimulation::UpdateSSE -->
```cpp
    // 잔여(count % 4) 스칼라.
    const float maxX = w - 1.0f;
    const float maxY = h - 1.0f;
    for (; i < count_; ++i) {
        posX_[i] += velX_[i] * dt;
        posY_[i] += velY_[i] * dt;
        if (posX_[i] < 0.0f || posX_[i] > maxX) {
            velX_[i] = -velX_[i];
            posX_[i] = std::clamp(posX_[i], 0.0f, maxX);
        }
        if (posY_[i] < 0.0f || posY_[i] > maxY) {
            velY_[i] = -velY_[i];
            posY_[i] = std::clamp(posY_[i], 0.0f, maxY);
        }
    }
}
```

핵심 설계 선택 셋.

1. **`_mm_loadu_ps`/`_mm_storeu_ps`**: unaligned load/store. SoA의 `std::vector<float>` 는 16 byte 정렬이 보장되지 않으므로 aligned 변종(`_mm_load_ps`)을 쓰면 segfault 위험이 있다. unaligned 명령은 Nehalem 이후 CPU에서 정렬된 주소에 대해 aligned와 거의 같은 속도다.
2. **`SseSelect` 헬퍼**: SSE2만 쓰는 lane select. SSE4.1을 요구하지 않아 모든 x86-64에서 컴파일된다.
3. **잔여 처리**: `count_ % 4` 가 0이 아니면 마지막 0~3개는 scalar 루프로 처리. SoA에서는 흔한 패턴이다.

### 분기를 마스크로 푸는 형태

scalar 코드의 분기:

```cpp
if (posX_[i] < 0.0f || posX_[i] > maxX) {
    velX_[i] = -velX_[i];
}
```

SIMD 코드의 마스크:

```cpp
const __m128 mX = _mm_or_ps(_mm_cmplt_ps(px, zero), _mm_cmpgt_ps(px, wMax));
vx = SseSelect(vx, _mm_sub_ps(zero, vx), mX);
```

분기가 *없다*. 모든 lane이 *항상* 두 분기 양쪽을 계산하고 마스크로 결과를 선택한다. 분기 예측 실패 비용이 0이 되는 대신 *항상 최대* 비용을 낸다. 분기가 거의 안 일어나는 코드(*경계 반사는 드물다*)에서는 마스크 처리가 더 비쌀 수도 있지만, 분기 빈도가 높거나 패턴이 불규칙하면 마스크 처리가 압도적이다.

## 5. SoA의 AVX2 경로와 FMA

AVX2는 `__m256` (8 float)을 다룬다. SSE 대비 처리 폭이 두 배다. 또한 FMA(Fused Multiply-Add) 명령을 사용해 `pos += vel * dt` 를 *한 명령* 으로 처리한다.

```cpp
#if defined(PERF_LAB_AVX2)
void SoASimulation::UpdateAVX2(float dt) {
    const float w = static_cast<float>(viewportW_);
    const float h = static_cast<float>(viewportH_);

    const __m256 dtv  = _mm256_set1_ps(dt);
    const __m256 zero = _mm256_setzero_ps();
    const __m256 wMax = _mm256_set1_ps(w - 1.0f);
    const __m256 hMax = _mm256_set1_ps(h - 1.0f);

    std::size_t i = 0;
    for (; i + 8 <= count_; i += 8) {
        __m256 px = _mm256_loadu_ps(&posX_[i]);
        __m256 py = _mm256_loadu_ps(&posY_[i]);
        __m256 vx = _mm256_loadu_ps(&velX_[i]);
        __m256 vy = _mm256_loadu_ps(&velY_[i]);

        // FMA로 pos += vel * dt 한 명령.
        px = _mm256_fmadd_ps(vx, dtv, px);
        py = _mm256_fmadd_ps(vy, dtv, py);

        // 경계 마스크. _CMP_LT_OQ는 ordered + quiet (NaN-safe).
        const __m256 mX = _mm256_or_ps(_mm256_cmp_ps(px, zero, _CMP_LT_OQ),
                                        _mm256_cmp_ps(px, wMax, _CMP_GT_OQ));
        const __m256 mY = _mm256_or_ps(_mm256_cmp_ps(py, zero, _CMP_LT_OQ),
                                        _mm256_cmp_ps(py, hMax, _CMP_GT_OQ));

        // _mm256_blendv_ps는 AVX1 코어라 AVX2 컴파일에서 항상 사용 가능.
        vx = _mm256_blendv_ps(vx, _mm256_sub_ps(zero, vx), mX);
        vy = _mm256_blendv_ps(vy, _mm256_sub_ps(zero, vy), mY);

        px = _mm256_max_ps(zero, _mm256_min_ps(wMax, px));
        py = _mm256_max_ps(zero, _mm256_min_ps(hMax, py));

        _mm256_storeu_ps(&posX_[i], px);
        _mm256_storeu_ps(&posY_[i], py);
        _mm256_storeu_ps(&velX_[i], vx);
        _mm256_storeu_ps(&velY_[i], vy);
    }

    // 잔여(count % 8) 스칼라.
    const float maxX = w - 1.0f;
    const float maxY = h - 1.0f;
    for (; i < count_; ++i) {
        posX_[i] += velX_[i] * dt;
        posY_[i] += velY_[i] * dt;
        if (posX_[i] < 0.0f || posX_[i] > maxX) {
            velX_[i] = -velX_[i];
            posX_[i] = std::clamp(posX_[i], 0.0f, maxX);
        }
        if (posY_[i] < 0.0f || posY_[i] > maxY) {
            velY_[i] = -velY_[i];
            posY_[i] = std::clamp(posY_[i], 0.0f, maxY);
        }
    }
}
#endif  // PERF_LAB_AVX2
```

SSE 코드와 비교한 주요 차이는 다음 셋이다.

1. **`_mm_*` → `_mm256_*`**: 모든 인트린식이 256비트 변종. 처리 폭 4 → 8.
2. **FMA**: `_mm256_fmadd_ps(a, b, c)` 는 `c + a * b` 를 한 명령으로. 정밀도도 *중간 결과를 반올림하지 않아* 약간 더 좋다.
3. **`_mm256_cmp_ps` 와 predicate**: AVX의 비교 명령은 *predicate 인자* 가 있어 `_CMP_LT_OQ`(less than, ordered, quiet) 같은 식으로 NaN 처리 방식까지 명시한다.

이 세 차이로 AVX2가 SSE 대비 *이론상* 2배 처리량 + FMA의 추가 이득을 본다. 실측에서는 메모리 대역폭과 store 비용이 병목이라 1.5배 ~ 2배 사이가 흔하다.

## 6. Archetype의 chunk 단위 SIMD

Archetype은 *chunk 외부 루프 + chunk 내부 SoA 루프* 라 SIMD 적용 패턴이 SoA와 거의 같다. 차이는 *외부 루프* 에서 chunk마다 한 번 더 indirection이 일어난다는 점이다.

```cpp
#if defined(PERF_LAB_X86_SIMD)
void ArchetypeSimulation::UpdateSSE(float dt) {
    const float w = static_cast<float>(viewportW_);
    const float h = static_cast<float>(viewportH_);
    const __m128 dtv  = _mm_set1_ps(dt);
    const __m128 zero = _mm_setzero_ps();
    const __m128 wMax = _mm_set1_ps(w - 1.0f);
    const __m128 hMax = _mm_set1_ps(h - 1.0f);
    const float maxX = w - 1.0f;
    const float maxY = h - 1.0f;

    // chunk 외부 루프 + chunk 내부 SSE 루프.
    for (auto& chunkPtr : chunks_) {
        Chunk& c = *chunkPtr;
        const std::size_t n = c.size;

        std::size_t i = 0;
        for (; i + 4 <= n; i += 4) {
            __m128 px = _mm_loadu_ps(&c.posX[i]);
            __m128 py = _mm_loadu_ps(&c.posY[i]);
            __m128 vx = _mm_loadu_ps(&c.velX[i]);
            __m128 vy = _mm_loadu_ps(&c.velY[i]);

            px = _mm_add_ps(px, _mm_mul_ps(vx, dtv));
            py = _mm_add_ps(py, _mm_mul_ps(vy, dtv));

            const __m128 mX = _mm_or_ps(_mm_cmplt_ps(px, zero),
                                         _mm_cmpgt_ps(px, wMax));
            const __m128 mY = _mm_or_ps(_mm_cmplt_ps(py, zero),
                                         _mm_cmpgt_ps(py, hMax));

            vx = SseSelect(vx, _mm_sub_ps(zero, vx), mX);
            vy = SseSelect(vy, _mm_sub_ps(zero, vy), mY);

            px = _mm_max_ps(zero, _mm_min_ps(wMax, px));
            py = _mm_max_ps(zero, _mm_min_ps(hMax, py));

            _mm_storeu_ps(&c.posX[i], px);
            _mm_storeu_ps(&c.posY[i], py);
            _mm_storeu_ps(&c.velX[i], vx);
            _mm_storeu_ps(&c.velY[i], vy);
        }
        // 잔여(chunk size % 4) 스칼라.
        for (; i < n; ++i) {
            c.posX[i] += c.velX[i] * dt;
            c.posY[i] += c.velY[i] * dt;
            if (c.posX[i] < 0.0f || c.posX[i] > maxX) {
                c.velX[i] = -c.velX[i];
                c.posX[i] = std::clamp(c.posX[i], 0.0f, maxX);
            }
            if (c.posY[i] < 0.0f || c.posY[i] > maxY) {
                c.velY[i] = -c.velY[i];
                c.posY[i] = std::clamp(c.posY[i], 0.0f, maxY);
            }
        }
    }
}
#endif
```

chunk 크기 256은 4의 배수, 8의 배수라 SSE는 *마지막 chunk 외에는* 잔여 처리가 없다. 마지막 chunk만 0~3 또는 0~7개의 잔여를 처리할 수 있다. 100k 엔티티면 가득 찬 390개 chunk(각 256) + 마지막 chunk 160개로 총 391 chunk다(390×256+160=100,000). 160도 4와 8의 배수라 이 경우엔 마지막 chunk조차 SSE 40패스 / AVX2 20패스로 정확히 떨어지고 scalar 잔여가 없다.

Archetype의 SIMD 측정 결과는 SoA와 거의 같다. 단일 archetype 시뮬에서 chunk 외부 루프 오버헤드는 무시 수준이다.

## 7. AoS는 왜 scalar로 두는가

AoS의 데이터 배치를 다시 보자.

```text
메모리: [px0 py0 vx0 vy0] [px1 py1 vx1 vy1] [px2 py2 vx2 vy2] ...
        ^16B^             ^16B^             ^16B^
```

핫 루프가 *4개 entity의 posX* (`px0, px1, px2, px3`)를 한 SSE 명령에 모으려면 메모리에서 16 byte 간격으로 *4개의 4-byte 값* 을 읽어야 한다. SSE의 일반 load 명령(`_mm_loadu_ps`)은 *연속된 16 byte* 를 한 번에 읽는 거지, *16 byte 간격으로 4개 4-byte* 를 읽지 못한다.

이를 처리하려면 다음 중 하나를 한다.

1. **gather/scatter 인트린식**: AVX2의 `_mm_i32gather_ps`. 하지만 gather는 일반 load의 4~8배 비싸 SoA 대비 이득이 사라진다.
2. **shuffle/transpose**: 4개 entity를 한 번에 로드한 뒤 SIMD 안에서 transpose. 가능하지만 코드 복잡도 폭증 + transpose 비용.
3. **temporary SoA**: AoS 데이터를 매 프레임 시작에 임시 SoA로 복사. 복사 비용이 SIMD 이득을 상쇄한다.

세 옵션 모두 *AoS의 stride 한계* 를 *우회* 하는 형태고, 결국 *AoS이면 SIMD를 안 쓴다* 가 게임 엔진의 실용적 결론이다. SoA/Archetype 측에서 SIMD 이득을 가져가고, AoS는 임의 접근에 강한 모델로 두는 게 일반 패턴이다.

이 도구도 같은 방향으로, AoS는 SIMD 토글에 무관하게 scalar로 동작한다.

```cpp
void AoSSimulation::Update(float dt, SimdWidth /*simd*/, bool /*branchless*/) {
    UpdateScalar(dt);
}
```

`simd` 인자를 받지만 무시한다. 측정 시점에서 *SoA·Archetype은 SSE/AVX2로 갈수록 빨라지고, AoS는 변하지 않는* 모습이 그래프에 명확히 드러난다. 이게 *측정 도구가 알려 주는 정보* 다.

## 8. Hud 활성화와 빌드·실행 결과

기존 코드에서 SIMD 콤보박스는 grayed out이었다. 두 줄(`GuiSetState(STATE_DISABLED)` 와 그 짝)을 빼고 라벨 색을 회색에서 밝은색으로 바꾼다.

```cpp
// src/ui/hud.cpp 의 simd 토글 부분

DrawText("simd     :", x2, rowY2(row2), 14, LIGHTGRAY);
int simdIdx = (int)toggles.simd;
if (GuiDropdownBox(Rect(x2 + kLabelW, rowY2(row2), kFieldW, kRowH),
                   kSimdOptions, &simdIdx, simdEdit_)) {
    simdEdit_ = !simdEdit_;
}
if (!simdEdit_) {
    toggles.simd = (SimdWidth)std::clamp(simdIdx, 0, kSimdCount - 1);
}
```

이제 SIMD 토글이 활성화되어 콤보박스 클릭으로 Scalar/SSE/AVX2 경로가 즉시 전환된다.

### 빌드

기본(SSE만):
```bash
cmake -S . -B build
cmake --build build --config Release
```

AVX2 활성화:
```bash
cmake -S . -B build -DPERF_LAB_ENABLE_AVX2=ON
cmake --build build --config Release
```

### 실행 결과

Intel CPU + AVX2 옵션 ON 빌드, 100k 엔티티 기준 일반적인 경향:

- **SoA Scalar**: 자동 벡터화로 어느 정도 빠름. baseline.
- **SoA SSE**: scalar 대비 update ms가 작아짐. 정확한 비율은 컴파일러와 CPU에 따라 다르며, 자동 벡터화가 이미 잘 동작했다면 차이가 작을 수 있다.
- **SoA AVX2**: SSE 대비 추가 단축. FMA 효과까지 합쳐 SSE × 1.5~2배 정도가 흔하다.
- **AoS Scalar / SSE / AVX2**: *모두 같음*. SIMD 토글이 무효화되어 같은 scalar 경로가 도는 결과.

콤보를 SoA Scalar → SoA AVX2 → AoS AVX2 순서로 클릭하면 그래프에 *세로 절벽 두 번* 이 그려진다. 첫 절벽은 SIMD 효과(아래로), 두 번째 절벽은 layout이 AoS가 되면서 SIMD 토글이 무효화돼 다시 위로. *AoS의 SIMD 무효성* 이 시각적 사건으로 드러난다.

### 자동 검증: `parts_simd_tests`

"SIMD로 빨라졌다"는 주장은 세 폭이 *같은 값* 을 낼 때만 성립한다. 결과가 달라졌다면 빨라진 게 아니라 덜 한 것이다. `simd` 그룹이 그 등식을 붙든다.

두 종류의 단언이 들어 있다. SoA 쪽은 폭이 달라도 결과가 허용 오차 안이라는 것, AoS 쪽은 폭이 달라도 결과가 *비트까지* 같다는 것이다. 뒤엣것이 앞에서 말한 *AoS의 SIMD 무효성* 을 기계가 확인하는 형태다.

<!-- snippet: tests/core_tests.cpp#TestSimdEquivalence -->
```cpp
// tests/core_tests.cpp — simd 그룹

void TestSimdEquivalence() {
    constexpr int   kFrames = 240;
    constexpr float kDt     = 1.0f / 144.0f;

    auto scalar = MakeAndInit(ppl::Layout::SoA);
    auto simd   = MakeAndInit(ppl::Layout::SoA);
    // ...
    std::vector<float> a, c;
    scalar->CopyState(a);
    simd->CopyState(c);
    CHECK(NearStates(a, c, 1e-4f));
#if defined(PERF_LAB_AVX2)
    // AVX2는 FMA로 pos += vel*dt 의 중간 반올림이 사라져 비트가 다르다.
    std::vector<float> d;
    wide->CopyState(d);
    CHECK(NearStates(a, d, ToleranceFor(ppl::SimdWidth::AVX2, kFrames)));
    // SSE만큼 가깝지는 않다는 것까지 못 박는다. 깨지면 AVX2 토글이 폴백 중이다.
    CHECK(!NearStates(a, d, 1e-6f));
#endif

    // AoS는 SIMD 토글을 통째로 무시하고 언제나 scalar 경로를 돈다.
    std::vector<float> aosState[ppl::kSimdCount];
    const ppl::SimdWidth kWidths[] = {
        ppl::SimdWidth::Scalar, ppl::SimdWidth::SSE, ppl::SimdWidth::AVX2};
    for (int w = 0; w < ppl::kSimdCount; ++w) {
        auto aos = MakeAndInit(ppl::Layout::AoS);
        for (int i = 0; i < kFrames; ++i) aos->Update(kDt, kWidths[w], false);
        aos->CopyState(aosState[w]);
    }
    CHECK(NearStates(aosState[0], aosState[1], 0.0f));
    CHECK(NearStates(aosState[0], aosState[2], 0.0f));
}
```

`CHECK(!NearStates(a, d, 1e-6f))` 는 방향이 반대인 단언이다. AVX2 결과가 스칼라와 *너무 똑같으면* 오히려 실패한다 — 그건 AVX2 경로가 아니라 SSE나 scalar가 돌고 있다는 신호이기 때문이다. 옵션을 켜지 않은 빌드에서는 이 두 줄이 아예 컴파일되지 않는다.

```bash
cmake -S . -B build -DPERF_LAB_ENABLE_AVX2=ON
cmake --build build --config Release
ctest --test-dir build -C Release -R parts_simd_tests --output-on-failure

./build/parts_core_tests simd              # Linux/macOS
.\build\Release\parts_core_tests.exe simd  # Windows
```

```text
parts_core_tests: group `simd`
  simd/SimdEquivalence ... 5 check(s) ok
simd: 1 test(s), 5 check(s), 0 failure(s)
```

`5 check(s)` 는 AVX2를 켠 빌드의 수다. 옵션 없이 구성하면 위의 두 줄이 빠져 `3 check(s)` 가 나온다. 이 차이가 곧 "AVX2 경로가 실제로 컴파일됐는가" 의 지표라, 옵션을 켰는데 `3` 이 나오면 아키텍처가 x86이 아니거나 매크로 가드가 어긋난 것이다.

### 검증 체크리스트

- AVX2 토글 선택 시 *SSE와 같은 ms* → AVX2 빌드 옵션이 꺼져 있어 SSE로 폴백 중. `cmake -DPERF_LAB_ENABLE_AVX2=ON` 으로 재빌드.
- SSE/AVX2 토글이 *Scalar와 같은 ms* → 비-x86 플랫폼이라 scalar 폴백 중 (Apple Silicon 등). 이 경우 layout만 비교 가능.
- 컴파일 오류 *target specific option mismatch* → AVX2 코드가 옵션 OFF 빌드에서 컴파일 시도됨. `PERF_LAB_AVX2` 매크로 가드가 빠진 줄 확인.
- 100k에서도 SIMD 차이가 작음 → 컴파일러 자동 벡터화가 이미 SoA scalar 코드에 잘 동작한 케이스. 엔티티 수를 늘리거나 워크로드를 더 산술 집약적으로 바꿔 비교한다. `-O0` 빌드는 디버그 참고용이지 성능 판단 기준으로 쓰지 않는다.

### 측정의 정직성

수동 SIMD가 *항상 빠르다* 는 단정은 위험하다. 컴파일러가 이미 잘 자동 벡터화한 코드를 인트린식으로 다시 짜면 비슷하거나 살짝 느릴 수도 있다. SIMD 이득은 *분기 마스크 처리* 와 *FMA 같은 명시적 활용* 에서 주로 나온다. 워크로드가 *분기 거의 없는 단순 산술* 이면 자동 벡터화가 이미 한계 근처에 도달해 차이가 작다.

이 도구의 의의는 *비율* 이 아니라 *동일 워크로드에서 토글 전후 변화* 를 *그 자리에서* 본다는 점이다. 머신마다 다르고 워크로드마다 다르지만 *내 머신의 내 워크로드* 에 대한 측정은 도구가 즉각 보여 준다.

---

## 결론

이 단계의 결과물은 SIMD 토글 한 번에 SoA·Archetype의 핫 루프가 SSE 또는 AVX2 경로로 전환되고, AoS는 토글에 무관하게 scalar로 도는 모습이 그래프에 그려지는 도구다. 약 350줄의 SIMD 코드(SoA SSE/AVX2 + Archetype SSE/AVX2 + 플랫폼 가드 헤더)가 기존 코드 위에 얹혔다.

세 가지 결정이 이 단계의 토대를 만들었다.

1. **2단계 가드 (`PERF_LAB_X86_SIMD` + `PERF_LAB_AVX2`)** — 플랫폼 자동 감지 + 빌드 옵션을 분리해 ARM 환경, AVX2 미지원 환경, 옵션 끈 빌드까지 모두 안전하게 폴백한다.
2. **SSE2 bitwise select** — `_mm_blendv_ps`(SSE4.1) 의존을 빼고 `_mm_andnot_ps + _mm_and_ps + _mm_or_ps` 세 명령으로 같은 lane select를 구현. 모든 x86-64 baseline에서 추가 컴파일 옵션 없이 동작.
3. **AoS는 의도적 scalar 유지** — gather/scatter 또는 transpose로 우회 가능하지만 비용이 SIMD 이득을 상쇄. *AoS의 SIMD 무효성* 자체가 측정 결과의 핵심 정보다.

여기까지의 코드는 자동 벡터화와 수동 SIMD를 같은 시뮬에서 토글로 비교할 수 있다. 다만 *분기 비용* 자체는 아직 측정에 별도 변수로 안 들어와 있다. 분기 처리 방식이 scalar 경로에 고정되어 있어, 분기 예측이 그래프에 어떤 영향을 주는지는 이 상태에서 관측할 수 없다.

## 더 읽을거리

- [Intel Intrinsics Guide](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html) — 모든 x86 SIMD 인트린식의 검색 가능한 레퍼런스
- [Agner Fog, "Optimizing software in C++"](https://www.agner.org/optimize/optimizing_cpp.pdf) — SIMD와 분기 처리 패턴
- [Daniel Lemire, "SIMD-accelerated string and number parsing"](https://lemire.me/blog/) — 실제 SIMD 적용 예
- [Wojciech Muła — articles (optimization, SIMD, algorithms)](http://0x80.pl/notesen.html) — SSE/AVX 실전 패턴 글 색인
- [Compiler Explorer (godbolt.org)](https://godbolt.org/) — 자동 벡터화 결과를 어셈블리로 직접 확인
- [SIMD-everywhere (sse2neon)](https://github.com/DLTcollab/sse2neon) — x86 SSE 인트린식을 ARM NEON으로 매핑하는 어댑터 헤더
- Bryce Adelstein Lelbach, "Benchmarking C++ Code" (CppCon 2015) — SIMD 비교 측정의 함정
