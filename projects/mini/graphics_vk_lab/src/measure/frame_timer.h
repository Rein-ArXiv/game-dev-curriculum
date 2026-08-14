#pragma once

#include <array>
#include <chrono>

namespace vklab {

class FrameTimer {
public:
    static constexpr int kHistorySize = 240;

    FrameTimer();
    void BeginFrame();
    void EndFrame();

    float LastFrameMs()      const { return lastFrameMs_; }
    float LastDeltaSeconds() const { return lastDeltaSeconds_; }
    float EmaFrameMs()       const { return emaFrameMs_; }
    float Fps()              const { return (emaFrameMs_ > 0.0f) ? 1000.0f / emaFrameMs_ : 0.0f; }
    const std::array<float, kHistorySize>& History() const { return history_; }
    int HistoryHead() const { return historyHead_; }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point frameBegin_, prevBegin_;
    bool   hasPrev_ = false;
    float  lastFrameMs_ = 0.0f;
    float  lastDeltaSeconds_ = 0.0f;
    float  emaFrameMs_ = 0.0f;
    static constexpr float kEmaAlpha = 0.1f;
    std::array<float, kHistorySize> history_{};
    int historyHead_ = 0;
};

}  // namespace vklab
