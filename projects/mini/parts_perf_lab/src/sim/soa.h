#pragma once

#include "platform.h"
#include "sim/isimulation.h"

#include <vector>

namespace ppl {

// SoA (Structure of Arrays) 변종.
// 컴포넌트(pos, vel)별로 별도 배열을 둔다. 핫 루프가 한 컴포넌트를 순차 접근하면
// 캐시가 효율적이고 컴파일러가 자동 벡터화하기 가장 쉽다.
class SoASimulation final : public ISimulation {
public:
    void Init(std::size_t entityCount, int viewportW, int viewportH) override;
    void Update(float dt, SimdWidth simd, bool branchless) override;
    void Draw() const override;
    void Shutdown() override;
    std::size_t Count() const override { return count_; }
    void CopyState(std::vector<float>& out) const override;

private:
    // branchless == false: 명시적 if 분기로 처리.
    // branchless == true:  predication 형태로 작성해 컴파일러의 branchless 변환을 유도.
    // SIMD 경로는 이미 마스크 select라 branchless 인자에 영향받지 않는다.
    void UpdateScalar(float dt, bool branchless);
#if defined(PERF_LAB_X86_SIMD)
    void UpdateSSE(float dt);     // SSE2 인트린식. 모든 x86/x64에서 사용 가능.
#  if defined(PERF_LAB_AVX2)
    void UpdateAVX2(float dt);    // AVX2/FMA. CMake 옵션 ON일 때만 컴파일.
#  endif
#endif

    std::vector<float> posX_, posY_, velX_, velY_;
    std::size_t count_ = 0;
    int viewportW_ = 0;
    int viewportH_ = 0;
};

}  // namespace ppl
