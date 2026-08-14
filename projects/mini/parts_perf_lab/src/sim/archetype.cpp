// 계산 코어 전용 번역 단위. raylib에 의존하지 않는다.
// 화면 출력(Draw)은 archetype_draw.cpp에 있고, 테스트 타깃은 그 TU를 컴파일하지 않는다.
#include "sim/archetype.h"

#include <algorithm>
#include <random>

namespace ppl {

void ArchetypeSimulation::Init(std::size_t entityCount, int viewportW, int viewportH) {
    viewportW_  = viewportW;
    viewportH_  = viewportH;
    totalCount_ = entityCount;

    // 엔티티를 256개 단위 chunk로 나눈다. 마지막 chunk는 부분 채움.
    const std::size_t chunkCount =
        (entityCount + kChunkCapacity - 1) / kChunkCapacity;
    chunks_.clear();
    chunks_.reserve(chunkCount);

    std::mt19937 rng(0xC0FFEEu);
    std::uniform_real_distribution<float> distX(0.0f, static_cast<float>(viewportW_));
    std::uniform_real_distribution<float> distY(0.0f, static_cast<float>(viewportH_));
    std::uniform_real_distribution<float> distV(-120.0f, 120.0f);

    std::size_t remaining = entityCount;
    while (remaining > 0) {
        auto chunk = std::make_unique<Chunk>();
        const std::size_t take = std::min(remaining, kChunkCapacity);
        chunk->size = take;
        for (std::size_t i = 0; i < take; ++i) {
            chunk->posX[i] = distX(rng);
            chunk->posY[i] = distY(rng);
            chunk->velX[i] = distV(rng);
            chunk->velY[i] = distV(rng);
        }
        chunks_.push_back(std::move(chunk));
        remaining -= take;
    }
}

void ArchetypeSimulation::Update(float dt, SimdWidth simd, bool branchless) {
    switch (simd) {
        case SimdWidth::Scalar:
            UpdateScalar(dt, branchless);
            break;
        case SimdWidth::SSE:
#if defined(PERF_LAB_X86_SIMD)
            UpdateSSE(dt);
#else
            UpdateScalar(dt, branchless);
#endif
            break;
        case SimdWidth::AVX2:
#if defined(PERF_LAB_X86_SIMD) && defined(PERF_LAB_AVX2)
            UpdateAVX2(dt);
#elif defined(PERF_LAB_X86_SIMD)
            UpdateSSE(dt);
#else
            UpdateScalar(dt, branchless);
#endif
            break;
    }
}

void ArchetypeSimulation::UpdateScalar(float dt, bool branchless) {
    const float w = static_cast<float>(viewportW_);
    const float h = static_cast<float>(viewportH_);

    // clamp 상한은 w-1, h-1: DrawPixel(width, y)는 화면 밖이라 clip된다.
    const float maxX = w - 1.0f;
    const float maxY = h - 1.0f;

    if (branchless) {
        for (auto& chunkPtr : chunks_) {
            Chunk& c = *chunkPtr;
            const std::size_t n = c.size;
            for (std::size_t i = 0; i < n; ++i) {
                c.posX[i] += c.velX[i] * dt;
                c.posY[i] += c.velY[i] * dt;

                const bool outX = (c.posX[i] < 0.0f) || (c.posX[i] > maxX);
                const bool outY = (c.posY[i] < 0.0f) || (c.posY[i] > maxY);

                c.velX[i] = outX ? -c.velX[i] : c.velX[i];
                c.velY[i] = outY ? -c.velY[i] : c.velY[i];
                c.posX[i] = std::clamp(c.posX[i], 0.0f, maxX);
                c.posY[i] = std::clamp(c.posY[i], 0.0f, maxY);
            }
        }
        return;
    }

    // 명시적 분기.
    // chunk 외부 루프 + chunk 내부 SoA 루프.
    // chunk 내부 루프는 SoA와 동일한 핫 루프 형태라 자동 벡터화가 가능하다.
    for (auto& chunkPtr : chunks_) {
        Chunk& c = *chunkPtr;
        const std::size_t n = c.size;

        for (std::size_t i = 0; i < n; ++i) {
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

#if defined(PERF_LAB_X86_SIMD)

namespace {
inline __m128 SseSelect(__m128 a, __m128 b, __m128 mask) {
    return _mm_or_ps(_mm_andnot_ps(mask, a), _mm_and_ps(mask, b));
}
}  // namespace

void ArchetypeSimulation::UpdateSSE(float dt) {
    const float w = static_cast<float>(viewportW_);
    const float h = static_cast<float>(viewportH_);
    const __m128 dtv  = _mm_set1_ps(dt);
    const __m128 zero = _mm_setzero_ps();
    const __m128 wMax = _mm_set1_ps(w - 1.0f);
    const __m128 hMax = _mm_set1_ps(h - 1.0f);
    const float maxX = w - 1.0f;
    const float maxY = h - 1.0f;

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

#  if defined(PERF_LAB_AVX2)
void ArchetypeSimulation::UpdateAVX2(float dt) {
    const float w = static_cast<float>(viewportW_);
    const float h = static_cast<float>(viewportH_);
    const __m256 dtv  = _mm256_set1_ps(dt);
    const __m256 zero = _mm256_setzero_ps();
    const __m256 wMax = _mm256_set1_ps(w - 1.0f);
    const __m256 hMax = _mm256_set1_ps(h - 1.0f);
    const float maxX = w - 1.0f;
    const float maxY = h - 1.0f;

    for (auto& chunkPtr : chunks_) {
        Chunk& c = *chunkPtr;
        const std::size_t n = c.size;

        std::size_t i = 0;
        for (; i + 8 <= n; i += 8) {
            __m256 px = _mm256_loadu_ps(&c.posX[i]);
            __m256 py = _mm256_loadu_ps(&c.posY[i]);
            __m256 vx = _mm256_loadu_ps(&c.velX[i]);
            __m256 vy = _mm256_loadu_ps(&c.velY[i]);

            px = _mm256_fmadd_ps(vx, dtv, px);
            py = _mm256_fmadd_ps(vy, dtv, py);

            const __m256 mX = _mm256_or_ps(
                _mm256_cmp_ps(px, zero, _CMP_LT_OQ),
                _mm256_cmp_ps(px, wMax, _CMP_GT_OQ));
            const __m256 mY = _mm256_or_ps(
                _mm256_cmp_ps(py, zero, _CMP_LT_OQ),
                _mm256_cmp_ps(py, hMax, _CMP_GT_OQ));

            vx = _mm256_blendv_ps(vx, _mm256_sub_ps(zero, vx), mX);
            vy = _mm256_blendv_ps(vy, _mm256_sub_ps(zero, vy), mY);

            px = _mm256_max_ps(zero, _mm256_min_ps(wMax, px));
            py = _mm256_max_ps(zero, _mm256_min_ps(hMax, py));

            _mm256_storeu_ps(&c.posX[i], px);
            _mm256_storeu_ps(&c.posY[i], py);
            _mm256_storeu_ps(&c.velX[i], vx);
            _mm256_storeu_ps(&c.velY[i], vy);
        }
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
#  endif  // PERF_LAB_AVX2
#endif    // PERF_LAB_X86_SIMD

void ArchetypeSimulation::CopyState(std::vector<float>& out) const {
    out.resize(totalCount_ * 4);
    std::size_t entity = 0;
    for (const auto& chunkPtr : chunks_) {
        const Chunk& c = *chunkPtr;
        for (std::size_t i = 0; i < c.size; ++i, ++entity) {
            out[entity * 4 + 0] = c.posX[i];
            out[entity * 4 + 1] = c.posY[i];
            out[entity * 4 + 2] = c.velX[i];
            out[entity * 4 + 3] = c.velY[i];
        }
    }
}

void ArchetypeSimulation::Shutdown() {
    chunks_.clear();
    totalCount_ = 0;
}

}  // namespace ppl
