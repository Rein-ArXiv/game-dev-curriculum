// 동기화 프리미티브 — 전부 futex 위에 직접 쌓는다.
//
// glibc pthread_mutex 를 그대로 쓰지 않는 이유: 엔진은 경합 없는 fast-path 를
// 원자 연산만으로 끝내고, 경합이 있을 때만 커널 대기로 떨어지는 경계를 명시적으로
// 통제해야 한다. 아래 Mutex 는 Ulrich Drepper 의 3-상태 futex mutex 다.
//
//   state: 0 = unlocked, 1 = locked(대기자 없음), 2 = locked(대기자 있음)
//
// 핵심은 unlock 이 *대기자가 있을 때만* 커널을 깨운다는 점이다(상태 2). 경합이
// 없으면 lock/unlock 모두 원자 연산 한 번으로 끝나 시스템 콜이 0회다.

#pragma once

#include "sync/futex.h"

#include <atomic>
#include <cstdint>
#include <utility>

namespace mega {
namespace sync {

class Mutex {
public:
    Mutex() = default;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void Lock() {
        uint32_t c = 0;
        // fast-path: 0 → 1 (경합 없음).
        if (state_.compare_exchange_strong(c, 1, std::memory_order_acquire, std::memory_order_relaxed))
            return;
        // 경합: 상태를 2(대기자 있음)로 올리고 0이 될 때까지 잔다. (커널로 빨리
        // 내려가 잠드는 편이, 짧은 임계 구역에 N-way 경합이 걸릴 때 캐시 라인
        // 핑퐁을 줄여 오히려 낫다 — sync_bench 에서 적응형 스핀이 더 느렸다.)
        if (c != 2) c = state_.exchange(2, std::memory_order_acquire);
        while (c != 0) {
            FutexWait(&state_, 2);
            c = state_.exchange(2, std::memory_order_acquire);
        }
    }

    bool TryLock() {
        uint32_t expected = 0;
        return state_.compare_exchange_strong(expected, 1, std::memory_order_acquire, std::memory_order_relaxed);
    }

    void Unlock() {
        // fetch_sub 가 1을 반환 → 대기자 없음(1→0), 깨울 필요 없음.
        if (state_.fetch_sub(1, std::memory_order_release) != 1) {
            state_.store(0, std::memory_order_release);
            FutexWakeOne(&state_);
        }
    }

private:
    friend class CondVar;
    std::atomic<uint32_t> state_{0};
};

// RAII 가드.
class LockGuard {
public:
    explicit LockGuard(Mutex& m) : m_(m) { m_.Lock(); }
    ~LockGuard() { m_.Unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
private:
    Mutex& m_;
};

// 스핀락 — 임계 구역이 수십 나노초로 짧고 컨텍스트 스위치가 더 비쌀 때.
class SpinLock {
public:
    void Lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            while (flag_.test(std::memory_order_relaxed)) CpuPause();   // 캐시 라인 read-only 스핀
        }
    }
    bool TryLock() { return !flag_.test_and_set(std::memory_order_acquire); }
    void Unlock() { flag_.clear(std::memory_order_release); }
private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

// 조건 변수 — 시퀀스 카운터 기반 futex condvar.
//
// Wait 는 mutex 를 풀고 자는데, 푸는 시점과 자는 시점 사이에 Notify 가 끼면
// lost-wakeup 이 된다. seq 를 먼저 읽어두고 FutexWait(seq) 로 자면, 그 사이
// Notify 가 seq 를 올린 경우 FutexWait 가 "값이 다르다"며 즉시 반환해 경쟁을
// 닫는다. 깨어난 뒤 술어를 while 로 재검사하는 호출 규약은 동일하다.
class CondVar {
public:
    void Wait(Mutex& m) {
        uint32_t s = seq_.load(std::memory_order_acquire);
        m.Unlock();
        FutexWait(&seq_, s);
        m.Lock();
    }
    void NotifyOne() {
        seq_.fetch_add(1, std::memory_order_release);
        FutexWakeOne(&seq_);
    }
    void NotifyAll() {
        seq_.fetch_add(1, std::memory_order_release);
        FutexWakeAll(&seq_);
    }
private:
    std::atomic<uint32_t> seq_{0};
};

// 읽기-쓰기 락 — writer 우선(writer 기아 방지). Mutex + CondVar 위에 구성.
class RWLock {
public:
    void LockShared() {
        LockGuard g(m_);
        while (writer_ || waitingWriters_ > 0) readerOk_.Wait(m_);
        ++readers_;
    }
    void UnlockShared() {
        LockGuard g(m_);
        if (--readers_ == 0) writerOk_.NotifyOne();
    }
    void Lock() {
        LockGuard g(m_);
        ++waitingWriters_;
        while (writer_ || readers_ > 0) writerOk_.Wait(m_);
        --waitingWriters_;
        writer_ = true;
    }
    void Unlock() {
        LockGuard g(m_);
        writer_ = false;
        if (waitingWriters_ > 0) writerOk_.NotifyOne();   // writer 우선
        else                     readerOk_.NotifyAll();
    }
private:
    Mutex   m_;
    CondVar readerOk_, writerOk_;
    int     readers_        = 0;
    bool    writer_         = false;
    int     waitingWriters_ = 0;
};

// 한 번만 실행 — 초기화 경쟁을 닫는다.
class OnceFlag {
public:
    template <class F>
    void CallOnce(F&& fn) {
        if (state_.load(std::memory_order_acquire) == kDone) return;
        uint32_t expected = kIdle;
        if (state_.compare_exchange_strong(expected, kRunning, std::memory_order_acquire)) {
            std::forward<F>(fn)();
            state_.store(kDone, std::memory_order_release);
            FutexWakeAll(&state_);
        } else {
            while (state_.load(std::memory_order_acquire) != kDone)
                FutexWait(&state_, kRunning);
        }
    }
private:
    static constexpr uint32_t kIdle = 0, kRunning = 1, kDone = 2;
    std::atomic<uint32_t> state_{kIdle};
};

}  // namespace sync
}  // namespace mega
