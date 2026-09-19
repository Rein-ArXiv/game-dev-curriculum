#include "measure/frame_timer.h"

namespace gllab {

FrameTimer::FrameTimer() {
    history_.fill(0.0f);
}

void FrameTimer::BeginFrame() {
    auto now = Clock::now();
    if (hasPrevFrame_) {
        // dt 는 *이전 BeginFrame 과 이번 BeginFrame 의 차이* — 한 프레임의
        // 전체 wall-clock 시간. EndFrame - BeginFrame 도 swap 을 포함한 같은
        // 구간이지만, BeginFrame 시점에는 *이번 프레임의 EndFrame 이 아직 없다*.
        std::chrono::duration<float> deltaSec = now - prevBegin_;
        lastDeltaSeconds_ = deltaSec.count();
    } else {
        lastDeltaSeconds_ = 0.0f;
    }
    frameBegin_ = now;
}

void FrameTimer::EndFrame() {
    auto now = Clock::now();
    std::chrono::duration<float, std::milli> ms = now - frameBegin_;
    lastFrameMs_ = ms.count();

    if (emaFrameMs_ <= 0.0f) {
        emaFrameMs_ = lastFrameMs_;
    } else {
        emaFrameMs_ = emaFrameMs_ * (1.0f - kEmaAlpha) + lastFrameMs_ * kEmaAlpha;
    }

    history_[(size_t)historyHead_] = lastFrameMs_;
    historyHead_ = (historyHead_ + 1) % kHistorySize;

    prevBegin_    = frameBegin_;
    hasPrevFrame_ = true;
}

}  // namespace gllab
