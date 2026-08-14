#pragma once

#include <array>
#include <chrono>
#include <cstddef>

namespace ppl {

// 최근 N 프레임의 update/draw/alloc/container 시간을 슬라이딩 윈도우로 보관한다.
// HUD가 이 데이터를 그래프로 표시한다.
class FrameTimer {
public:
    static constexpr std::size_t kCapacity = 600;

    void BeginUpdate();
    void EndUpdate();

    void BeginDraw();
    void EndDraw();

    // 호출 직전에 BeginUpdate/EndUpdate, BeginDraw/EndDraw가 모두 호출되었다고 가정한다.
    // 한 쌍의 측정값과 별도 채널 ms를 슬라이딩 윈도우에 push한다.
    // allocMs/containerMs는 외부 측정값(EventBenchmark·ContainerBenchmark)이라
    // 이 클래스가 직접 측정하지 않고 인자로 받는다.
    void CommitFrame(float allocMs = 0.0f, float containerMs = 0.0f);

    // 최근 N 프레임 ms 배열. ring buffer라 head 위치도 함께 반환한다.
    const std::array<float, kCapacity>& UpdateMs()    const { return updateMs_; }
    const std::array<float, kCapacity>& DrawMs()      const { return drawMs_; }
    const std::array<float, kCapacity>& AllocMs()     const { return allocMs_; }
    const std::array<float, kCapacity>& ContainerMs() const { return containerMs_; }
    std::size_t Head()   const { return head_; }
    std::size_t Filled() const { return filled_; }

    float LastUpdateMs()    const { return lastUpdateMs_; }
    float LastDrawMs()      const { return lastDrawMs_; }
    float LastTotalMs()     const { return lastUpdateMs_ + lastDrawMs_; }
    float LastAllocMs()     const { return lastAllocMs_; }
    float LastContainerMs() const { return lastContainerMs_; }

private:
    using Clock = std::chrono::steady_clock;
    using Time  = Clock::time_point;

    Time updateStart_{};
    Time drawStart_{};

    std::array<float, kCapacity> updateMs_{};
    std::array<float, kCapacity> drawMs_{};
    std::array<float, kCapacity> allocMs_{};
    std::array<float, kCapacity> containerMs_{};
    std::size_t head_ = 0;
    std::size_t filled_ = 0;

    float lastUpdateMs_    = 0.0f;
    float lastDrawMs_      = 0.0f;
    float lastAllocMs_     = 0.0f;
    float lastContainerMs_ = 0.0f;
};

}  // namespace ppl
