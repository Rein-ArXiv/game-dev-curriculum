#pragma once

#include "ui/toggles.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace ppl {

// 입력 계약: 유효한 dt는 음이 아닌 유한값이다. NaN/inf/음수는 시각적 성능 실험을
// 깨뜨리므로 0으로 취급한다(0으로 만들면 유효 상태가 변하지 않는다). 물리적으로
// 정확한 다중 반사가 아니라 "한 step 후 clamp + 부호 반전"이라는 이산 경계 규칙이다.
inline float SanitizeDt(float dt) {
    return (dt >= 0.0f && std::isfinite(dt)) ? dt : 0.0f;
}

// 입력 계약: viewport는 W,H >= 2다. 더 작은 값은 2로 올린다.
inline int SanitizeViewport(int v) {
    return v < 2 ? 2 : v;
}

// 모든 레이아웃 변종이 구현하는 추상 인터페이스.
//
// 같은 워크로드(1만 점이 화면 안에서 떠다닌다)를 다음 세 레이아웃으로 구현한다.
//   - SoA: 컴포넌트별 분리 배열. 캐시 친화·자동 벡터화 친화.
//   - AoS: struct 배열. 객체 지향 친화·임의 접근에 유리.
//   - Archetype: chunk 단위 SoA. 부분 컴포넌트 접근에 유리.
//
// Update는 simd/branchless 같은 *런타임 옵션* 을 인자로 받아 같은 클래스 안에서
// 여러 경로로 분기한다. 측정 시에는 dispatch 비용을 무시할 수 있을 정도로 작다.
class ISimulation {
public:
    virtual ~ISimulation() = default;

    virtual void Init(std::size_t entityCount, int viewportW, int viewportH) = 0;
    virtual void Update(float dt, SimdWidth simd, bool branchless) = 0;
    virtual void Draw() const = 0;
    virtual void Shutdown() = 0;

    virtual std::size_t Count() const = 0;

    // GUI와 독립적인 정확성 검증용 스냅숏. 엔티티 순서대로
    // [posX, posY, velX, velY]를 기록한다.
    virtual void CopyState(std::vector<float>& out) const = 0;
};

// 레이아웃 enum에서 해당 구현 인스턴스를 생성한다.
std::unique_ptr<ISimulation> CreateSimulation(Layout layout);

}  // namespace ppl
