#pragma once

#include "platform.h"
#include "ui/toggles.h"

namespace ppl {

// 요청한 SIMD와 실제로 실행되는 커널이 다를 수 있다. HUD·CSV·dispatch가 같은
// 판정을 쓰도록 한 곳에서 계산한다.
//
//   - AoS: 이 lab의 AoS 구현에는 수동 SIMD 경로가 없다. 요청과 무관하게 C++ Auto.
//   - AVX2 요청 + AVX2 미빌드: SSE로 폴백(비-x86이면 C++ Auto).
//   - SSE 요청 + 비-x86: C++ Auto.
//   - branchless는 scalar(C++ Auto) 경로에만 의미가 있다. SIMD는 이미 마스크 select.
enum class KernelFallback : int {
    None = 0,
    LayoutHasNoSimd,  // 선택한 layout에 그 SIMD 구현이 없음 (AoS)
    Avx2NotCompiled,  // AVX2 요청, 이 빌드에 없음
    NoIntrinsics,     // 비-x86 등 인트린식 자체가 없음
};

inline const char* KernelFallbackName(KernelFallback r) {
    switch (r) {
        case KernelFallback::None:            return "none";
        case KernelFallback::LayoutHasNoSimd: return "layout has no manual SIMD";
        case KernelFallback::Avx2NotCompiled: return "AVX2 not compiled";
        case KernelFallback::NoIntrinsics:    return "no SIMD intrinsics";
    }
    return "?";
}

// HUD 한 줄에 들어가도록 줄인 이름.
inline const char* KernelFallbackShortName(KernelFallback r) {
    switch (r) {
        case KernelFallback::None:            return "";
        case KernelFallback::LayoutHasNoSimd: return "no-simd";
        case KernelFallback::Avx2NotCompiled: return "no-avx2";
        case KernelFallback::NoIntrinsics:    return "no-x86";
    }
    return "?";
}

struct ResolvedKernel {
    SimdWidth      effective;          // 실제로 도는 커널
    bool           branchlessApplied;  // 실제로 predication이 걸리는가
    KernelFallback reason;             // 요청과 다를 때의 이유
};

inline ResolvedKernel ResolveKernel(Layout layout, SimdWidth requested,
                                    bool branchless) {
    if (layout == Layout::AoS) {
        return ResolvedKernel{
            SimdWidth::Scalar, branchless,
            requested == SimdWidth::Scalar ? KernelFallback::None
                                           : KernelFallback::LayoutHasNoSimd};
    }

    SimdWidth effective = requested;
    KernelFallback reason = KernelFallback::None;

    if (requested == SimdWidth::AVX2) {
#if defined(PERF_LAB_X86_SIMD) && defined(PERF_LAB_AVX2)
        effective = SimdWidth::AVX2;
#elif defined(PERF_LAB_X86_SIMD)
        effective = SimdWidth::SSE;
        reason = KernelFallback::Avx2NotCompiled;
#else
        effective = SimdWidth::Scalar;
        reason = KernelFallback::NoIntrinsics;
#endif
    } else if (requested == SimdWidth::SSE) {
#if defined(PERF_LAB_X86_SIMD)
        effective = SimdWidth::SSE;
#else
        effective = SimdWidth::Scalar;
        reason = KernelFallback::NoIntrinsics;
#endif
    }

    const bool branchlessApplied =
        (effective == SimdWidth::Scalar) ? branchless : false;
    return ResolvedKernel{effective, branchlessApplied, reason};
}

}  // namespace ppl
