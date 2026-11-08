// 계산 코어 전용 번역 단위. raylib에 의존하지 않는다.
// 화면 출력(Draw)은 aos_draw.cpp에 있고, 테스트 타깃은 그 TU를 컴파일하지 않는다.
#include "sim/aos.h"

#include "sim/kernel_dispatch.h"

#include <algorithm>
#include <random>

namespace ppl {

void AoSSimulation::Init(std::size_t entityCount, int viewportW, int viewportH) {
    // 좌표 계약: 0 <= x <= W-1, 0 <= y <= H-1. Init과 Update가 같은 상한을 쓴다.
    viewportW_ = SanitizeViewport(viewportW);
    viewportH_ = SanitizeViewport(viewportH);
    entities_.resize(entityCount);

    std::mt19937 rng(0xC0FFEEu);
    std::uniform_real_distribution<float> distX(0.0f, static_cast<float>(viewportW_ - 1));
    std::uniform_real_distribution<float> distY(0.0f, static_cast<float>(viewportH_ - 1));
    std::uniform_real_distribution<float> distV(-120.0f, 120.0f);

    for (auto& e : entities_) {
        e.posX = distX(rng);
        e.posY = distY(rng);
        e.velX = distV(rng);
        e.velY = distV(rng);
    }
}

void AoSSimulation::Update(float dt, SimdWidth simd, bool branchless) {
    // AoS는 이 lab에 수동 SIMD 경로가 없다. 요청과 실제 실행 커널을 분리해 기록한다.
    const ResolvedKernel k = ResolveKernel(Layout::AoS, simd, branchless);
    UpdateScalar(SanitizeDt(dt), k.branchlessApplied);
}

void AoSSimulation::UpdateScalar(float dt, bool branchless) {
    const float w = static_cast<float>(viewportW_);
    const float h = static_cast<float>(viewportH_);

    // clamp 상한은 w-1, h-1: DrawPixel(width, y)는 화면 밖이라 clip된다.
    const float maxX = w - 1.0f;
    const float maxY = h - 1.0f;

    if (branchless) {
        // Predication 형태.
        for (auto& e : entities_) {
            e.posX += e.velX * dt;
            e.posY += e.velY * dt;

            const bool outX = (e.posX < 0.0f) || (e.posX > maxX);
            const bool outY = (e.posY < 0.0f) || (e.posY > maxY);

            e.velX = outX ? -e.velX : e.velX;
            e.velY = outY ? -e.velY : e.velY;
            e.posX = std::clamp(e.posX, 0.0f, maxX);
            e.posY = std::clamp(e.posY, 0.0f, maxY);
        }
        return;
    }

    // 명시적 분기.
    // 핫 루프는 한 엔티티의 모든 필드를 차례로 만진다. 같은 캐시 라인 안이라
    // 임의 접근에는 좋지만, 한 컴포넌트만 순차 처리하는 SIMD에는 stride가 커서 손해다.
    for (auto& e : entities_) {
        e.posX += e.velX * dt;
        e.posY += e.velY * dt;

        if (e.posX < 0.0f || e.posX > maxX) {
            e.velX = -e.velX;
            e.posX = std::clamp(e.posX, 0.0f, maxX);
        }
        if (e.posY < 0.0f || e.posY > maxY) {
            e.velY = -e.velY;
            e.posY = std::clamp(e.posY, 0.0f, maxY);
        }
    }
}

void AoSSimulation::CopyState(std::vector<float>& out) const {
    out.resize(entities_.size() * 4);
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        out[i * 4 + 0] = entities_[i].posX;
        out[i * 4 + 1] = entities_[i].posY;
        out[i * 4 + 2] = entities_[i].velX;
        out[i * 4 + 3] = entities_[i].velY;
    }
}

void AoSSimulation::Shutdown() {
    entities_.clear();
}

}  // namespace ppl
