#pragma once

#include "platform.h"
#include "sim/isimulation.h"

#include <array>
#include <memory>
#include <vector>

namespace ppl {

// Archetype 변종 — chunk 기반 SoA.
//
// 엔티티들을 *고정 크기 chunk* 로 분할하고, 각 chunk 안에서는 SoA 레이아웃을 쓴다.
// Unity DOTS, Bevy ECS의 모델과 같은 형태다.
//
// 장점:
//   - 같은 컴포넌트 조합(=archetype)의 엔티티만 한 chunk에 모이므로 branch 없이 처리.
//   - chunk 단위 부분 처리(예: chunk 절반만 활성)가 자연스럽다.
//   - chunk 크기를 캐시 라인 배수로 잡아 prefetch 친화적.
//
// 단점:
//   - chunk 경계 처리, archetype migration의 부담.
//   - 같은 archetype에서는 단순 SoA 대비 이득이 거의 없다(이 시뮬이 그 케이스).
//
// 이 시뮬은 모든 엔티티가 같은 archetype(pos+vel)이라, *측정상* 으로는 SoA와
// 거의 같은 결과가 나오는 게 정상이다. 차이가 미미하다는 것을 보여 주는 것이
// 비교의 의도다.
class ArchetypeSimulation final : public ISimulation {
public:
    static constexpr std::size_t kChunkCapacity = 256;

    struct Chunk {
        std::array<float, kChunkCapacity> posX;
        std::array<float, kChunkCapacity> posY;
        std::array<float, kChunkCapacity> velX;
        std::array<float, kChunkCapacity> velY;
        std::size_t size = 0;
    };

    void Init(std::size_t entityCount, int viewportW, int viewportH) override;
    void Update(float dt, SimdWidth simd, bool branchless) override;
    void Draw() const override;
    void Shutdown() override;
    std::size_t Count() const override { return totalCount_; }
    void CopyState(std::vector<float>& out) const override;

private:
    void UpdateScalar(float dt, bool branchless);
#if defined(PERF_LAB_X86_SIMD)
    void UpdateSSE(float dt);
#  if defined(PERF_LAB_AVX2)
    void UpdateAVX2(float dt);
#  endif
#endif

    std::vector<std::unique_ptr<Chunk>> chunks_;
    std::size_t totalCount_ = 0;
    int viewportW_ = 0;
    int viewportH_ = 0;
};

}  // namespace ppl
