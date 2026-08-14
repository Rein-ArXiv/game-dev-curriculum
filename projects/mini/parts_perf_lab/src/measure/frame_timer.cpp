#include "measure/frame_timer.h"

namespace ppl {

void FrameTimer::BeginUpdate() {
    updateStart_ = Clock::now();
}

void FrameTimer::EndUpdate() {
    const auto end = Clock::now();
    const auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end - updateStart_).count();
    lastUpdateMs_ = static_cast<float>(dur) / 1.0e6f;
}

void FrameTimer::BeginDraw() {
    drawStart_ = Clock::now();
}

void FrameTimer::EndDraw() {
    const auto end = Clock::now();
    const auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end - drawStart_).count();
    lastDrawMs_ = static_cast<float>(dur) / 1.0e6f;
}

void FrameTimer::CommitFrame(float allocMs, float containerMs) {
    lastAllocMs_     = allocMs;
    lastContainerMs_ = containerMs;

    updateMs_[head_]    = lastUpdateMs_;
    drawMs_[head_]      = lastDrawMs_;
    allocMs_[head_]     = lastAllocMs_;
    containerMs_[head_] = lastContainerMs_;
    head_ = (head_ + 1) % kCapacity;
    if (filled_ < kCapacity) {
        ++filled_;
    }
}

}  // namespace ppl
