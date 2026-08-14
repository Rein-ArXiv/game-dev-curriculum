#pragma once

#include "sync/i_sync.h"

#include <atomic>
#include <cstdint>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace skl {

// Linux futex(2) 시스템 콜을 직접 호출하는 mutex.
//
// Drepper의 3-state futex mutex 패턴을 따른다.
//   state 0: unlocked
//   state 1: locked, no waiters
//   state 2: locked, has waiters (다음 unlock에서 깨워야 함)
//
// uncontended에서는 atomic CAS 한 번으로 끝나고 시스템 콜이 안 일어난다.
// contended에서만 SYS_futex(WAIT/WAKE) 시스템 콜이 일어난다. 이 lab의
// 워크로드(짧은 critical section + 작업 큐 push/pop)에서 contention은 드물어
// fast path 측정이 주가 된다.
//
// std::mutex와 거의 같은 fast path 비용이 *측정으로 확인되는* 게 핵심 포인트다.
// "futex가 mutex보다 빠르다"는 통념을 측정으로 검증한다.
class FutexSync final : public ISync {
public:
    void Lock() override {
        std::uint32_t expected = 0;
        if (state_.compare_exchange_strong(expected, 1,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
            return;  // uncontended fast path.
        }
        LockSlow();
    }

    void Unlock() override {
        // fetch_sub: 1 → 0 이면 waiters 없음, 2 → 1 이면 waiters 있음.
        if (state_.fetch_sub(1, std::memory_order_release) != 1) {
            // 2였으면 우리가 1을 만들었고, waiters가 있으니 0으로 정리하고 깨운다.
            state_.store(0, std::memory_order_release);
            FutexWake(1);
        }
    }

    const char* Name() const override { return "futex"; }

private:
    void LockSlow() {
        std::uint32_t c = state_.load(std::memory_order_relaxed);
        for (;;) {
            // c == 1 이면 contention 표시(2)로 올린 뒤 wait.
            if (c == 2 || (c == 1
                && state_.compare_exchange_strong(c, 2,
                                                   std::memory_order_relaxed))) {
                FutexWait(2);
            }
            std::uint32_t expected = 0;
            if (state_.compare_exchange_strong(expected, 2,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed)) {
                return;
            }
            c = state_.load(std::memory_order_relaxed);
        }
    }

    void FutexWait(std::uint32_t expected) {
        ::syscall(SYS_futex,
                  reinterpret_cast<std::uint32_t*>(&state_),
                  FUTEX_WAIT_PRIVATE,
                  static_cast<int>(expected),
                  nullptr, nullptr, 0);
    }

    void FutexWake(int count) {
        ::syscall(SYS_futex,
                  reinterpret_cast<std::uint32_t*>(&state_),
                  FUTEX_WAKE_PRIVATE,
                  count, nullptr, nullptr, 0);
    }

    std::atomic<std::uint32_t> state_{0};
};

}  // namespace skl
