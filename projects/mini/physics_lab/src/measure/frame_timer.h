// 프레임 ms / fps 측정. EMA (지수 이동 평균) 으로 표시 안정화.
//
// 사용 흐름:
//   FrameTimer t;
//   while (running) {
//     t.BeginFrame();
//     ... update / draw ...
//     t.EndFrame();
//     printf("%.2f ms\n", t.SmoothedMs());
//   }

#pragma once

namespace physlab {

class FrameTimer {
public:
    void BeginFrame();
    void EndFrame();

    // 가장 최근 프레임 시간 (ms).
    float LastMs() const { return lastMs_; }

    // 지수 이동 평균 ms (표시용).
    float SmoothedMs() const { return smoothedMs_; }

    // 1000 / smoothedMs.
    float Fps() const;

private:
    double startSec_    = 0.0;   // raylib GetTime() — 초 단위
    float  lastMs_      = 0.0f;
    float  smoothedMs_  = 0.0f;
    float  emaAlpha_    = 0.1f;  // 약 10 프레임 평활
};

}  // namespace physlab
