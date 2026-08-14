#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>

namespace netlab {

// 재정렬된 명령을 받아 cumulative ACK가 나타내는 연속 prefix만 적용한다.
class OrderedInputBuffer {
public:
    void Reset(int lastApplied = -1) {
        buffered_.clear();
        lastApplied_ = lastApplied;
    }

    void Receive(int sequence, int input) {
        if (sequence > lastApplied_)
            buffered_.insert_or_assign(sequence, input);
    }

    template <class Apply>
    void Drain(Apply&& apply) {
        for (;;) {
            auto next = buffered_.find(lastApplied_ + 1);
            if (next == buffered_.end()) break;
            apply(next->second);
            ++lastApplied_;
            buffered_.erase(next);
        }
    }

    int LastApplied() const { return lastApplied_; }
    size_t BufferedCount() const { return buffered_.size(); }

private:
    std::map<int, int> buffered_;
    int lastApplied_ = -1;
};

inline bool AcceptCumulativeAck(int received, int& newestAccepted) {
    if (received < newestAccepted) return false;
    newestAccepted = received;
    return true;
}

template <class Pending, class SequenceOf, class InputOf, class Apply>
float ReconcileScalar(float authoritative, int acknowledged,
                      Pending& pending, SequenceOf&& sequenceOf,
                      InputOf&& inputOf, Apply&& apply) {
    while (!pending.empty() &&
           sequenceOf(pending.front()) <= acknowledged) {
        pending.pop_front();
    }

    float predicted = authoritative;
    for (const auto& command : pending)
        predicted = apply(predicted, inputOf(command));
    return predicted;
}

inline int SampleDelayFrames(float latency, float jitter,
                             uint32_t random, int maximum = 200) {
    const int base = std::max(0, (int)std::lround(latency));
    const int spread = std::max(0, (int)std::lround(jitter));
    const int offset = spread == 0
        ? 0
        : (int)(random % (uint32_t)(spread * 2 + 1)) - spread;
    return std::clamp(base + offset, 0, maximum);
}

inline bool MutuallyCompatibleMatch(int ratingDifference,
                                    float waitA, float waitB,
                                    float baseWindow, float wideningPerSecond) {
    const float allowedA =
        std::max(0.0f, baseWindow + std::max(0.0f, waitA) * wideningPerSecond);
    const float allowedB =
        std::max(0.0f, baseWindow + std::max(0.0f, waitB) * wideningPerSecond);
    return ratingDifference >= 0 &&
           (float)ratingDifference <= std::min(allowedA, allowedB);
}

}  // namespace netlab
