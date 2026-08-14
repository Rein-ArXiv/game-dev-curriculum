// 프레임 ms / fps 측정. EMA 평활.
#pragma once

namespace netlab {

class FrameTimer {
public:
    void BeginFrame();
    void EndFrame();
    float LastMs() const { return lastMs_; }
    float SmoothedMs() const { return smoothedMs_; }
    float Fps() const;

private:
    double startNs_    = 0.0;
    float  lastMs_     = 0.0f;
    float  smoothedMs_ = 0.0f;
    float  emaAlpha_   = 0.1f;
};

}  // namespace netlab
