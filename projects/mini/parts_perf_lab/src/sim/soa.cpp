// 계산 코어 전용 번역 단위. raylib에 의존하지 않는다.
// 화면 출력(Draw)은 soa_draw.cpp에 있고, 테스트 타깃은 그 TU를 컴파일하지 않는다.
#include "sim/soa.h"

#include "sim/kernel_dispatch.h"

#include <algorithm>
#include <random>

namespace ppl {

void SoASimulation::Init(std::size_t entityCount, int viewportW, int viewportH) {
    count_ = entityCount;
    // 좌표 계약: 0 <= x <= W-1, 0 <= y <= H-1. Init과 Update가 같은 상한을 쓴다.
    viewportW_ = SanitizeViewport(viewportW);
    viewportH_ = SanitizeViewport(viewportH);

    posX_.resize(count_);
    posY_.resize(count_);
    velX_.resize(count_);
    velY_.resize(count_);

    std::mt19937 rng(0xC0FFEEu);
    std::uniform_real_distribution<float> distX(0.0f, static_cast<float>(viewportW_ - 1));
    std::uniform_real_distribution<float> distY(0.0f, static_cast<float>(viewportH_ - 1));
    std::uniform_real_distribution<float> distV(-120.0f, 120.0f);

    for (std::size_t i = 0; i < count_; ++i) {
        posX_[i] = distX(rng);
        posY_[i] = distY(rng);
        velX_[i] = distV(rng);
        velY_[i] = distV(rng);
    }
}

void SoASimulation::Update(float dt, SimdWidth simd, bool branchless) {
    // 요청과 실제 실행 커널을 한 곳(ResolveKernel)에서 판정해 HUD·dispatch가 같은
    // 값을 쓴다. 비-x86·AVX2 미빌드는 여기서 SSE/Scalar로 폴백한다.
    const ResolvedKernel k = ResolveKernel(Layout::SoA, simd, branchless);
    const float safeDt = SanitizeDt(dt);
    switch (k.effective) {
        case SimdWidth::Scalar:
            UpdateScalar(safeDt, k.branchlessApplied);
            break;
        case SimdWidth::SSE:
#if defined(PERF_LAB_X86_SIMD)
            UpdateSSE(safeDt);   // SIMD는 이미 마스크 처리, branchless 무관
#else
            UpdateScalar(safeDt, k.branchlessApplied);
#endif
            break;
        case SimdWidth::AVX2:
#if defined(PERF_LAB_X86_SIMD) && defined(PERF_LAB_AVX2)
            UpdateAVX2(safeDt);
#elif defined(PERF_LAB_X86_SIMD)
            UpdateSSE(safeDt);
#else
            UpdateScalar(safeDt, k.branchlessApplied);
#endif
            break;
    }
}

void SoASimulation::UpdateScalar(float dt, bool branchless) {
    const float w = static_cast<float>(viewportW_);
    const float h = static_cast<float>(viewportH_);

    // clamp 상한은 w-1, h-1: DrawPixel(width, y)는 화면 밖이라 clip된다.
    const float maxX = w - 1.0f;
    const float maxY = h - 1.0f;

    if (branchless) {
        // Predication 형태: 컴파일러가 cmov/마스크로 바꿀 수 있도록 작성한다.
        for (std::size_t i = 0; i < count_; ++i) {
            posX_[i] += velX_[i] * dt;
            posY_[i] += velY_[i] * dt;

            const bool outX = (posX_[i] < 0.0f) || (posX_[i] > maxX);
            const bool outY = (posY_[i] < 0.0f) || (posY_[i] > maxY);

            velX_[i] = outX ? -velX_[i] : velX_[i];
            velY_[i] = outY ? -velY_[i] : velY_[i];
            posX_[i] = std::clamp(posX_[i], 0.0f, maxX);
            posY_[i] = std::clamp(posY_[i], 0.0f, maxY);
        }
    } else {
        // 명시적 if 분기. 분기 예측 결과에 따라 비용이 달라진다.
        for (std::size_t i = 0; i < count_; ++i) {
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
}

#if defined(PERF_LAB_X86_SIMD)

namespace {
// SSE2 비트마스크 select: mask 비트가 1인 lane은 b, 0인 lane은 a.
// SSE4.1의 _mm_blendv_ps와 동등 동작이지만 SSE2 명령만 사용한다.
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

#  if defined(PERF_LAB_AVX2)
void SoASimulation::UpdateAVX2(float dt) {
    const float w = static_cast<float>(viewportW_);
    const float h = static_cast<float>(viewportH_);

    const __m256 dtv  = _mm256_set1_ps(dt);
    const __m256 zero = _mm256_setzero_ps();
    const __m256 wMax = _mm256_set1_ps(w - 1.0f);
    const __m256 hMax = _mm256_set1_ps(h - 1.0f);

    // 8개 float씩. 처리 폭은 SSE의 두 배지만, FMA 사용 여부는 빌드 옵션
    // (PERF_LAB_FMA)에 따라 달라지므로 폭과 FMA를 같은 축으로 묶지 않는다.
    std::size_t i = 0;
    for (; i + 8 <= count_; i += 8) {
        __m256 px = _mm256_loadu_ps(&posX_[i]);
        __m256 py = _mm256_loadu_ps(&posY_[i]);
        __m256 vx = _mm256_loadu_ps(&velX_[i]);
        __m256 vy = _mm256_loadu_ps(&velY_[i]);

#if defined(PERF_LAB_FMA)
        px = _mm256_fmadd_ps(vx, dtv, px);
        py = _mm256_fmadd_ps(vy, dtv, py);
#else
        // FMA 없이 분리 곱셈·덧셈. 반올림 지점이 달라 FMA 경로와 비트가 다를 수 있다.
        px = _mm256_add_ps(px, _mm256_mul_ps(vx, dtv));
        py = _mm256_add_ps(py, _mm256_mul_ps(vy, dtv));
#endif

        const __m256 mX = _mm256_or_ps(_mm256_cmp_ps(px, zero, _CMP_LT_OQ),
                                        _mm256_cmp_ps(px, wMax, _CMP_GT_OQ));
        const __m256 mY = _mm256_or_ps(_mm256_cmp_ps(py, zero, _CMP_LT_OQ),
                                        _mm256_cmp_ps(py, hMax, _CMP_GT_OQ));

        // _mm256_blendv_ps는 AVX1 코어라 AVX2 컴파일 컨텍스트에서 항상 사용 가능.
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
#  endif  // PERF_LAB_AVX2
#endif    // PERF_LAB_X86_SIMD

void SoASimulation::CopyState(std::vector<float>& out) const {
    out.resize(count_ * 4);
    for (std::size_t i = 0; i < count_; ++i) {
        out[i * 4 + 0] = posX_[i];
        out[i * 4 + 1] = posY_[i];
        out[i * 4 + 2] = velX_[i];
        out[i * 4 + 3] = velY_[i];
    }
}

void SoASimulation::Shutdown() {
    posX_.clear(); posY_.clear();
    velX_.clear(); velY_.clear();
    count_ = 0;
}

}  // namespace ppl
