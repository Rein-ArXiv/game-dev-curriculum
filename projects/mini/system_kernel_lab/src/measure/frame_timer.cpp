#include "measure/frame_timer.h"

namespace skl {

void FrameTimer::BeginCycle() {
    cycleStart_ = Clock::now();
}

void FrameTimer::EndCycle(std::uint64_t bytesProcessed,
                            std::uint64_t ctxSwitchesDelta,
                            std::uint64_t pageFaultsDelta) {
    const auto end = Clock::now();
    const auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end - cycleStart_).count();
    const float ms = static_cast<float>(ns) / 1.0e6f;

    lastCycleMs_ = ms;
    lastBytes_   = bytesProcessed;

    lastThroughputMBs_ = (ms > 0.0f)
        ? static_cast<float>(bytesProcessed) / ms / 1000.0f
        : 0.0f;
    lastCtxSwitches_ = static_cast<float>(ctxSwitchesDelta);
    lastPageFaults_  = static_cast<float>(pageFaultsDelta);

    cycleMs_[head_]       = lastCycleMs_;
    throughputMBs_[head_] = lastThroughputMBs_;
    ctxSwitches_[head_]   = lastCtxSwitches_;
    pageFaults_[head_]    = lastPageFaults_;
    head_ = (head_ + 1) % kCapacity;
    if (filled_ < kCapacity) {
        ++filled_;
    }

    ++totalCycles_;
    totalBytes_ += bytesProcessed;
}

}  // namespace skl
