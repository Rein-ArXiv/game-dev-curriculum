#pragma once

#include "sim/isimulation.h"

#include <vector>

namespace ppl {

// AoS (Array of Structures) 변종.
// 한 엔티티의 모든 컴포넌트가 메모리에 *연속* 으로 놓인다.
//
// 한 엔티티 전체를 자주 다루는 케이스(임의 인덱스로 한 엔티티의 모든 컴포넌트
// 동시 접근)에 유리하다. 핫 루프가 한 컴포넌트만 순차 접근하면 stride가 커져
// 캐시·벡터화에 불리할 수 있다.
//
// 다만 "AoS라서 자동 벡터화가 불가능하다"는 일반화는 틀리다. 컴파일러는 버전과
// 최적화 수준에 따라 AoS 루프도 벡터화(SLP·shuffle/transpose 등)할 수 있다.
// 이 lab의 AoS에 *수동 SIMD 경로를 넣지 않았다*는 구현 범위만 뜻한다. SoA와의
// 비교 baseline이다.
class AoSSimulation final : public ISimulation {
public:
    struct Entity {
        float posX, posY;
        float velX, velY;
    };

    void Init(std::size_t entityCount, int viewportW, int viewportH) override;
    void Update(float dt, SimdWidth simd, bool branchless) override;
    void Draw() const override;
    void Shutdown() override;
    std::size_t Count() const override { return entities_.size(); }
    void CopyState(std::vector<float>& out) const override;

private:
    void UpdateScalar(float dt, bool branchless);

    std::vector<Entity> entities_;
    int viewportW_ = 0;
    int viewportH_ = 0;
};

}  // namespace ppl
