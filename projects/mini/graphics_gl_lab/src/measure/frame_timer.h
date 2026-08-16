#pragma once

// FrameTimer — CPU 측 프레임 시간 측정.
//
// 이 클래스가 재는 것은 *CPU 만*. GPU 측 GL_TIME_ELAPSED 쿼리는 같은 패턴의
// 별도 클래스 (`GpuTimer`) 가 맡는다.
//
// EMA (지수 이동 평균) 으로 *순간 ms* 가 아닌 *평활화된 ms* 도 함께 제공 — UI 가
// 깜빡이지 않도록.
//
// 최근 N 프레임 히스토리는 ImPlot 의 시계열 그래프용.

#include <array>
#include <chrono>

namespace gllab {

class FrameTimer {
public:
    static constexpr int kHistorySize = 240;     // 약 4초 @ 60fps.

    FrameTimer();

    void BeginFrame();
    void EndFrame();

    float LastFrameMs()    const { return lastFrameMs_; }
    float LastDeltaSeconds() const { return lastDeltaSeconds_; }
    float EmaFrameMs()     const { return emaFrameMs_; }
    float Fps()            const { return (emaFrameMs_ > 0.0f) ? 1000.0f / emaFrameMs_ : 0.0f; }

    const std::array<float, kHistorySize>& History() const { return history_; }
    int  HistoryHead() const { return historyHead_; }

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point frameBegin_;
    Clock::time_point prevBegin_;
    bool hasPrevFrame_ = false;

    float lastFrameMs_     = 0.0f;
    float lastDeltaSeconds_ = 0.0f;
    float emaFrameMs_      = 0.0f;
    static constexpr float kEmaAlpha = 0.1f;

    std::array<float, kHistorySize> history_{};
    int historyHead_ = 0;
};

}  // namespace gllab
