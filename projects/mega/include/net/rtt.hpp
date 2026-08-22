// rtt — 왕복 시간(RTT)과 지터 추정. 지수가중이동평균(EWMA).
//
// 왕복 표본을 평활해 보간 지연·재전송 타임아웃을 적응적으로 잡는다. 한 표본이
// 튀어도(스파이크) 평균이 천천히 따라가, 일시적 지연에 과민 반응하지 않는다.
// 지터(표본 편차)도 함께 추정해 버퍼 여유를 정한다(netcode 시리즈 지연 보정).
//
//   rtt   ← rtt + g·(sample - rtt)
//   jitter← jitter + g·(|sample - rtt| - jitter)
//   timeout ≈ rtt + 4·jitter   (TCP RTO 와 같은 발상)
//
// ── 표본은 어디서 오는가 ───────────────────────────────────────────
// 별도 ping/pong 패킷을 두지 않는다. 이미 나가고 있는 패킷마다 송신 시각을
// 적어 두고, 상대 헤더의 ack/ackBits 가 그 seq 를 덮는 순간 `now - sendTime`
// 을 표본으로 쓴다. 이유는 정확성이다 — 전용 ping 은 게임 트래픽이 없는 순간에도
// 오가므로 **유휴 경로**를 재게 되고, 정작 스냅샷 20Hz + 명령 재전송이 함께
// 밀려 나가는 혼잡 구간의 왕복 시간과 다른 수를 낸다. 게임이 실제로 겪는 지연을
// 재려면 표본이 게임 트래픽에서 나와야 한다.
//
// 대가도 분명하다. 트래픽이 없으면 표본도 없다. 그래서 추정기는 `Ready()` 로
// "아직 아무것도 모른다"를 말할 수 있어야 하고, 모르는 동안 쓰는 값이
// AdaptiveTiming 의 fallback 이다.

#pragma once

#include "net/packet.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mega {
namespace net {

class RttEstimator {
public:
    explicit RttEstimator(float gain = 0.1f) : gain_(gain) {}

    void Sample(float rttSample) {
        ++samples_;
        if (!init_) { rtt_ = rttSample; jitter_ = 0; init_ = true; return; }
        float err = rttSample - rtt_;
        rtt_    += gain_ * err;
        jitter_ += gain_ * (std::fabs(err) - jitter_);
    }

    float Rtt()     const { return rtt_; }
    float Jitter()  const { return jitter_; }
    float Timeout() const { return rtt_ + 4.0f * jitter_; }   // 재전송 타임아웃 권장치
    bool  Ready()   const { return init_; }
    uint64_t Samples() const { return samples_; }

private:
    float    gain_;
    float    rtt_    = 0;
    float    jitter_ = 0;
    uint64_t samples_ = 0;
    bool     init_   = false;
};

// ── 표본 추출기 ────────────────────────────────────────────────────
// seq → 송신 시각(ms) 표. 보관은 SequenceBuffer 라 용량이 고정이고, 영영 ack
// 되지 않는 항목은 새 seq 에 밀려 조용히 사라진다 — 손실된 패킷의 송신 시각을
// 언제 지울지 따로 정할 필요가 없다.
//
// **재전송된 seq 는 표본을 내지 않는다**(Karn's algorithm). 같은 seq 가 두 번
// 나갔으면 도착한 ack 가 어느 쪽 것인지 알 수 없고, 첫 번째 송신 시각으로 재면
// 실제보다 큰 값이, 두 번째로 재면 실제보다 작은 값이 나온다. 둘 다 타임아웃을
// 잘못된 방향으로 밀기 때문에 아예 버린다.
class RttSampler {
public:
    explicit RttSampler(size_t capacity = 64, float gain = 0.1f)
        : sent_(capacity), estimator_(gain) {}

    void OnSent(uint16_t seq, double sendMs) {
        Pending* existing = sent_.Find(seq);
        if (existing) { existing->resent = true; return; }   // 재전송: 표본 자격 상실
        Pending pending;
        pending.sendMs = sendMs;
        pending.resent = false;
        sent_.Insert(seq, pending);
    }

    // 상대 헤더가 확인해 준 미확인 항목을 전부 표본으로 바꾼다. 새 표본 수 반환.
    int OnRemoteHeader(const PacketHeader& remote, double nowMs) {
        uint16_t hit[kAckBitCount + 1];
        int back[kAckBitCount + 1];
        int count = 0;
        sent_.ForEach([&](uint16_t seq, const Pending&) {
            if (count > kAckBitCount) return;
            if (!HeaderAcknowledges(remote, seq)) return;
            hit[count] = seq;
            back[count] = SeqDiff(remote.ack, seq);
            ++count;
        });
        // 오래된 것부터(back 이 큰 것부터) 먹인다. 슬롯 순서 그대로 먹이면 같은
        // 표본 집합이 EWMA 에 다른 순서로 들어가 결과가 실행마다 갈릴 수 있다.
        for (int i = 1; i < count; ++i)
            for (int j = i; j > 0 && back[j] > back[j - 1]; --j) {
                std::swap(back[j], back[j - 1]);
                std::swap(hit[j], hit[j - 1]);
            }

        int fresh = 0;
        for (int i = 0; i < count; ++i) {
            const Pending* pending = sent_.Find(hit[i]);
            if (!pending) continue;
            if (!pending->resent) {
                const double sample = nowMs - pending->sendMs;
                if (sample >= 0.0) { estimator_.Sample(static_cast<float>(sample)); ++fresh; }
            }
            sent_.Remove(hit[i]);
        }
        return fresh;
    }

    const RttEstimator& Estimator() const { return estimator_; }
    RttEstimator&       Estimator() { return estimator_; }
    float  Rtt() const { return estimator_.Rtt(); }
    float  Jitter() const { return estimator_.Jitter(); }
    float  Timeout() const { return estimator_.Timeout(); }
    bool   Ready() const { return estimator_.Ready(); }
    size_t OutstandingCount() const { return sent_.Size(); }

private:
    struct Pending { double sendMs = 0.0; bool resent = false; };

    SequenceBuffer<Pending> sent_;
    RttEstimator            estimator_;
};

// ── 측정값이 상수를 대체하는 자리 ──────────────────────────────────
// 두 상수가 있었다. 보간 지연 3.0틱과 재전송 주기 100ms 다. 둘 다 링크를 보고
// 고른 값이 아니라 "적당히 고른 값"이었고, 링크가 달라져도 그대로였다.
//
// 대체 규칙이 둘에서 서로 다르다는 것이 이 구조체가 가르쳐 준 것이다.
//
//   재전송 주기 ← rtt + 4·jitter. 왕복이 끝나기 전에 다시 보내면 그것은
//     복구가 아니라 중복이므로, 왕복 시간 그 자체가 하한이다.
//   보간 지연  ← 스냅샷 간격 + 편도 지터의 배수. **rtt 는 들어가지 않는다.**
//     스냅샷은 재전송되지 않는 연속 스트림이라, 링크가 아무리 멀어도 도착
//     *간격*은 호스트가 보낸 간격 그대로다. 늘려야 하는 것은 그 간격이
//     흔들리는 폭뿐이다.
struct AdaptiveTiming {
    double tickHz                = 60.0;   // 시뮬 주파수
    double snapshotIntervalTicks = 3.0;    // 호스트가 스냅샷을 내는 간격
    double jitterSafety          = 2.0;    // 편도 지터의 몇 배를 여유로 둘 것인가
    double fallbackDelayTicks    = 3.0;    // 표본이 없을 때 — 측정 이전의 값
    double maxDelayTicks         = 12.0;   // 이 위로는 지연이 반응성보다 나쁘다
    double fallbackResendMs      = 100.0;  // 표본이 없을 때 — 측정 이전의 값
    double minResendMs           = 20.0;
    double maxResendMs           = 1000.0;

    double DelayTicks(const RttEstimator& estimator) const {
        if (!estimator.Ready()) return fallbackDelayTicks;
        const double oneWayJitterMs = static_cast<double>(estimator.Jitter()) * 0.5;
        const double ticks =
            snapshotIntervalTicks + jitterSafety * oneWayJitterMs * tickHz / 1000.0;
        return std::clamp(ticks, fallbackDelayTicks, maxDelayTicks);
    }

    // 렌더 시계가 최신에 다가갈 수 있는 한계. 지연보다 한 틱 앞이되 최소 1틱은
    // 남긴다 — 0 이면 시계가 최신 스냅샷에 붙어 보간할 구간이 사라진다.
    double LeadTicks(const RttEstimator& estimator) const {
        return std::max(1.0, DelayTicks(estimator) - 1.0);
    }

    double ResendMs(const RttEstimator& estimator) const {
        if (!estimator.Ready()) return fallbackResendMs;
        return std::clamp(static_cast<double>(estimator.Timeout()), minResendMs, maxResendMs);
    }
};

}  // namespace net
}  // namespace mega
