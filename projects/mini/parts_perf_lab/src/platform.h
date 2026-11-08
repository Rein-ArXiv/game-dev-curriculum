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
//
// 주의: 이것들은 "이 코드가 빌드에 포함됐는가"를 정할 뿐, 실행 CPU/OS가 그 ISA를
// 지원하는지 검사하지 않는다. 전체 타깃을 -mavx2/-mfma로 빌드한 바이너리는 그
// 기능을 지원하는 CPU/OS 대상 전용이다. 모든 CPU에서 안전한 실행 파일이 필요하면
// 높은 ISA 전용 함수를 별도 TU로 분리하고, 안전한 baseline에서 실행 지원을 검사한
// 뒤 호출해야 한다(이 lab은 아직 그 범용 dispatch를 구현하지 않았다).

#if defined(__x86_64__) || defined(_M_X64) \
    || defined(__SSE2__) \
    || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#  define PERF_LAB_X86_SIMD 1
#endif

#if defined(PERF_LAB_X86_SIMD)
#  include <immintrin.h>
#endif

#if defined(PERF_LAB_X86_SIMD) && defined(_MSC_VER)
#  include <intrin.h>  // __cpuid / __cpuidex
#endif

namespace ppl {

// HUD 표시용 CPU 기능 조회. dispatch 결정에 쓰지 않는다(위 주의 참고).
// MSVC 경로는 CPUID 기능 비트만 읽고 OS의 XSAVE 상태(XGETBV)를 확인하지 않으므로
// "CPU가 보고한 기능"이라는 뜻이다. AVX2/FMA 전용 바이너리의 실행 가능성 보증이 아니다.
inline bool CpuReportsAvx2() {
#if defined(PERF_LAB_X86_SIMD) && defined(_MSC_VER)
    int info[4] = {0, 0, 0, 0};
    __cpuid(info, 0);
    if (info[0] < 7) return false;
    __cpuidex(info, 7, 0);
    return (info[1] & (1 << 5)) != 0;  // EBX bit5 = AVX2
#elif defined(PERF_LAB_X86_SIMD) && (defined(__GNUC__) || defined(__clang__))
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") != 0;
#else
    return false;
#endif
}

inline bool CpuReportsFma() {
#if defined(PERF_LAB_X86_SIMD) && defined(_MSC_VER)
    int info[4] = {0, 0, 0, 0};
    __cpuid(info, 1);
    return (info[2] & (1 << 12)) != 0;  // ECX bit12 = FMA
#elif defined(PERF_LAB_X86_SIMD) && (defined(__GNUC__) || defined(__clang__))
    __builtin_cpu_init();
    return __builtin_cpu_supports("fma") != 0;
#else
    return false;
#endif
}

}  // namespace ppl
