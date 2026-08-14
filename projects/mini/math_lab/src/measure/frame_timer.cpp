#include "measure/frame_timer.h"

#include "raylib.h"

namespace mathlab {

void FrameTimer::BeginFrame() {
    startNs_ = GetTime();  // raylib: 초 단위 double
}

void FrameTimer::EndFrame() {
    double now = GetTime();
    lastMs_    = (float)((now - startNs_) * 1000.0);
    if (smoothedMs_ <= 0.0f) {
        smoothedMs_ = lastMs_;
    } else {
        smoothedMs_ = smoothedMs_ * (1.0f - emaAlpha_) + lastMs_ * emaAlpha_;
    }
}

float FrameTimer::Fps() const {
    return (smoothedMs_ > 0.0001f) ? (1000.0f / smoothedMs_) : 0.0f;
}

}  // namespace mathlab
