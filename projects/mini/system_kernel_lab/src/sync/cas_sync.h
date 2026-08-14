#pragma once

#include "sync/i_sync.h"

#include <atomic>
#include <cstdint>
#include <thread>

namespace skl {

// std::atomic<uint32_t> compare_exchange 기반 mutex.
//
// SpinSync(atomic_flag, test-and-set)와 의미는 같지만 *명령이 다르다*.
//   - atomic_flag::test_and_set: x86-64에서 보통 lock xchg b
//   - atomic<uint32>::cmpxchg: lock cmpxchg
//
// uncontended에서는 명령별 마이크로아키텍처 비용이 차이를 만든다.
// contended에서는 yield 비용도 함께 들어가므로 순수 CAS 비용만 보이지 않는다.
class CasSync final : public ISync {
public:
    void Lock() override {
        std::uint32_t expected = 0;
        while (!state_.compare_exchange_strong(expected, 1,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed)) {
            expected = 0;
            std::this_thread::yield();
        }
    }

    void Unlock() override {
        state_.store(0, std::memory_order_release);
    }

    const char* Name() const override { return "atomic CAS"; }

private:
    std::atomic<std::uint32_t> state_{0};
};

}  // namespace skl
