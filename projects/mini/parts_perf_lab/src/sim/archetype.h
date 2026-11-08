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
// 이 lab은 *단일 component 조합* 만 다룬다. Entity generation, component 추가·삭제,
// query matching, archetype migration은 구현하지 않았으므로, "ECS 전체 구현"이나
// 다른 엔진(Unity DOTS, Bevy 등)의 구체 저장 방식과 같다고 단정하지 않는다.
//
// 장점:
//   - 같은 컴포넌트 조합을 한 chunk에 모아 chunk 내부를 SoA로 처리.
//   - chunk 단위 부분 처리(예: 마지막 chunk의 부분 채움)가 자연스럽다.
//
// 단점:
//   - chunk 경계 처리와 (전체 ECS로 확장할 때) archetype migration의 부담.
//
// Chunk의 네 배열 payload 합은 256*4float*4 = 4096 바이트지만, size 멤버와 정렬을
// 포함한 sizeof(Chunk)는 그보다 크다(이번 x86-64 빌드에서 4104, alignof 8). 즉
// "payload가 캐시 라인 배수"와 "구조체 전체가 캐시 라인 배수"는 다르다. 64바이트
// 정렬(alignas)을 추가하면 padding 비용이 더 늘어난다.
//
// 이 시뮬은 모든 엔티티가 같은 component 조합이라, 단순 SoA와 큰 차이가 안 날
// 수 있다. 다만 "SoA≈Archetype이어야 정상"이라는 결론을 미리 정하지 않는다.
// 같은 결과를 내는 정확성과 측정 조건이 맞으면, 예상 순서가 나오지 않아도 실험은
// 유효하다.
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
