#pragma once

#include "sync/i_sync.h"

#include <atomic>
#include <thread>

namespace skl {

// std::atomic_flag (test-and-set) 기반 spinlock.
//
// uncontended fast path: 한 번의 atomic test_and_set (x86-64에서는 lock xchg).
// contended path: 재시도 + yield. yield는 구현에 따라 스케줄러 경계로
// 들어갈 수 있으므로, 순수 pause-spin과는 다른 비용을 낸다.
//
// 짧은 critical section + 워커 수 <= 코어 수에서는 빠를 수 있다.
// 길어지거나 워커 수가 많으면 *진짜 일하는 워커가 CPU를 못 받는* 문제가 생긴다.
class SpinSync final : public ISync {
public:
    void Lock() override {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // 단순성을 위해 yield로 대체한다. 순수 spinlock 측정이 아니라
            // "짧게 재시도하다가 스케줄러에 양보" 하는 모델이다.
            std::this_thread::yield();
        }
    }

    void Unlock() override {
        flag_.clear(std::memory_order_release);
    }

    const char* Name() const override { return "spinlock"; }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

}  // namespace skl
