#pragma once

// 플랫폼·아키텍처 가드.
//
// PERF_LAB_X86_SIMD: SSE2 이상 x86/x64 SIMD 인트린식 사용 가능 여부.
//   - x86_64, AMD64에서는 ABI baseline이므로 1로 정의
//   - 32-bit x86에서는 컴파일러가 SSE2 타깃임을 알려 줄 때만 1로 정의
//   - ARM (Apple Silicon, 모바일) 등에서는 정의되지 않음
//   - 비-x86 환경에서 SIMD 코드는 컴파일 자체가 안 되므로 매크로로 가드한다.
//
// PERF_LAB_AVX2: AVX2/FMA 인트린식 사용 가능 여부.
//   - CMake의 PERF_LAB_ENABLE_AVX2 옵션이 ON일 때 정의된다.
//   - PERF_LAB_X86_SIMD가 정의된 경우에만 의미가 있다.

#if defined(__x86_64__) || defined(_M_X64) \
    || defined(__SSE2__) \
    || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#  define PERF_LAB_X86_SIMD 1
#endif

#if defined(PERF_LAB_X86_SIMD)
#  include <immintrin.h>
#endif
