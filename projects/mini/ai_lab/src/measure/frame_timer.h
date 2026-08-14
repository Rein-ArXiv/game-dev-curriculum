// 프레임 ms / fps 측정. EMA (지수 이동 평균) 으로 표시 안정화.
//
// 사용 흐름:
//   FrameTimer t;
//   while (running) {
//     t.BeginFrame();
//     ... update / draw ...
//     t.EndFrame();
//   }

#pragma once

namespace ailab {

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
    float  emaAlpha_   = 0.1f;  // 약 10 프레임 평활
};

}  // namespace ailab
