#include "measure/frame_timer.h"

namespace vklab {

FrameTimer::FrameTimer() { history_.fill(0.0f); }

void FrameTimer::BeginFrame() {
    auto now = Clock::now();
    if (hasPrev_) {
        std::chrono::duration<float> d = now - prevBegin_;
        lastDeltaSeconds_ = d.count();
    } else lastDeltaSeconds_ = 0.0f;
    frameBegin_ = now;
}

void FrameTimer::EndFrame() {
    auto now = Clock::now();
    std::chrono::duration<float, std::milli> ms = now - frameBegin_;
    lastFrameMs_ = ms.count();
    if (emaFrameMs_ <= 0.0f) emaFrameMs_ = lastFrameMs_;
    else emaFrameMs_ = emaFrameMs_ * (1.0f - kEmaAlpha) + lastFrameMs_ * kEmaAlpha;
    history_[(size_t)historyHead_] = lastFrameMs_;
    historyHead_ = (historyHead_ + 1) % kHistorySize;
    prevBegin_ = frameBegin_;
    hasPrev_ = true;
}

}  // namespace vklab
