#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace skl {

// 한 측정 사이클 = 한 번의 워크로드 패스 (예: 16개 파일 read + 가공 한 번).
// 사이클의 ms와 처리량(MB/s)을 슬라이딩 윈도우로 보관해 그래프에 그린다.
class FrameTimer {
public:
    static constexpr std::size_t kCapacity = 600;

    void BeginCycle();
    // bytesProcessed: 이번 사이클에서 처리한 바이트 수 (MB/s 계산용).
    // ctxSwitchesDelta / pageFaultsDelta: 이번 사이클 중 누적된 카운터 차이.
    //   ctxSwitchesDelta = voluntary + involuntary 합산.
    //   pageFaultsDelta  = minor + major 합산.
    // 외부에서 ProcCounters로 측정해 인자로 넘긴다.
    void EndCycle(std::uint64_t bytesProcessed,
                  std::uint64_t ctxSwitchesDelta = 0,
                  std::uint64_t pageFaultsDelta  = 0);

    const std::array<float, kCapacity>& CycleMs()       const { return cycleMs_; }
    const std::array<float, kCapacity>& ThroughputMBs() const { return throughputMBs_; }
    const std::array<float, kCapacity>& CtxSwitches()   const { return ctxSwitches_; }
    const std::array<float, kCapacity>& PageFaults()    const { return pageFaults_; }

    std::size_t Head()   const { return head_; }
    std::size_t Filled() const { return filled_; }

    float         LastCycleMs()       const { return lastCycleMs_; }
    float         LastThroughputMBs() const { return lastThroughputMBs_; }
    float         LastCtxSwitches()   const { return lastCtxSwitches_; }
    float         LastPageFaults()    const { return lastPageFaults_; }
    std::uint64_t LastBytes()         const { return lastBytes_; }
    std::uint64_t TotalCycles()       const { return totalCycles_; }
    std::uint64_t TotalBytes()        const { return totalBytes_; }

private:
    using Clock = std::chrono::steady_clock;
    using Time  = Clock::time_point;

    Time cycleStart_{};

    std::array<float, kCapacity> cycleMs_{};
    std::array<float, kCapacity> throughputMBs_{};
    std::array<float, kCapacity> ctxSwitches_{};
    std::array<float, kCapacity> pageFaults_{};
    std::size_t head_   = 0;
    std::size_t filled_ = 0;

    float         lastCycleMs_       = 0.0f;
    float         lastThroughputMBs_ = 0.0f;
    float         lastCtxSwitches_   = 0.0f;
    float         lastPageFaults_    = 0.0f;
    std::uint64_t lastBytes_         = 0;
    std::uint64_t totalCycles_       = 0;
    std::uint64_t totalBytes_        = 0;
};

}  // namespace skl
