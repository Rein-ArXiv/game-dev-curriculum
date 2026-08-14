// rtt — 왕복 시간(RTT)과 지터 추정. 지수가중이동평균(EWMA).
//
// ping/pong 왕복 표본을 평활해 보간 지연·재전송 타임아웃을 적응적으로 잡는다.
// 한 표본이 튀어도(스파이크) 평균이 천천히 따라가, 일시적 지연에 과민 반응하지
// 않는다. 지터(표본 편차)도 함께 추정해 버퍼 여유를 정한다(netcode 시리즈 지연 보정).
//
//   rtt   ← rtt + g·(sample - rtt)
//   jitter← jitter + g·(|sample - rtt| - jitter)
//   timeout ≈ rtt + 4·jitter   (TCP RTO 와 같은 발상)

#pragma once

#include <cmath>

namespace mega {
namespace net {

class RttEstimator {
public:
    explicit RttEstimator(float gain = 0.1f) : gain_(gain) {}

    void Sample(float rttSample) {
        if (!init_) { rtt_ = rttSample; jitter_ = 0; init_ = true; return; }
        float err = rttSample - rtt_;
        rtt_    += gain_ * err;
        jitter_ += gain_ * (std::fabs(err) - jitter_);
    }

    float Rtt()     const { return rtt_; }
    float Jitter()  const { return jitter_; }
    float Timeout() const { return rtt_ + 4.0f * jitter_; }   // 재전송 타임아웃 권장치
    bool  Ready()   const { return init_; }

private:
    float gain_;
    float rtt_    = 0;
    float jitter_ = 0;
    bool  init_   = false;
};

}  // namespace net
}  // namespace mega
