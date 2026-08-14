#pragma once

// FrameTimer — CPU 측 프레임 ms + EMA + 240 프레임 히스토리. graphics_gl_lab 와 동형.

#include <array>
#include <chrono>

namespace dxlab {

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
    Clock::time_point frameBegin_;
    Clock::time_point prevBegin_;
    bool   hasPrevFrame_ = false;
    float  lastFrameMs_  = 0.0f;
    float  lastDeltaSeconds_ = 0.0f;
    float  emaFrameMs_   = 0.0f;
    static constexpr float kEmaAlpha = 0.1f;
    std::array<float, kHistorySize> history_{};
    int historyHead_ = 0;
};

}  // namespace dxlab
