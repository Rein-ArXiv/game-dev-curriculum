// Scheduler — 워커별 큐 + 일감 훔치기.
//
// 각 워커는 자기 큐에서 LIFO 로 꺼내고(방금 쪼갠 하위 작업이 캐시에 더움),
// 비면 다른 워커의 큐에서 FIFO 로 훔친다(가장 오래된, 큰 작업을 가져와 다시
// 쪼갤 여지). 이 구조가 부하를 자동 분산해, 작업 크기가 불균일해도 워커가
// 놀지 않는다.
//
// 내부 잠금은 두 벌이다. std::mutex/condition_variable 과, sync/lock.h 의
// futex 위에 직접 쌓은 Mutex/CondVar. 둘은 같은 일을 하고 같은 결과를 내지만
// 경합 구간의 비용이 다르다 — 어느 쪽이 나은지는 워크로드가 정하므로 하나를
// 지우지 않고 런타임에 고른다(Config::lock).
//
// 설계 노트: parts 시리즈는 Fiber 기반(컨텍스트 스위치 50~100ns)을 다루지만,
// Fiber 는 플랫폼 어셈블리/ucontext 의존이라 이식성·정확성 위험이 크다. 이
// stage 는 *정확성·빌드 가능성 우선* 으로 std::thread 워커 + 보호된 deque
// 로 구현한다. lock-free Chase-Lev deque 와 Fiber 스위치는 이 위에 얹는 설계
// 토픽으로 남긴다. ParallelFor 의 의미론(범위를 청크로 쪼개 병렬 실행 후 동기)
// 은 두 구현에서 동일하다.

#pragma once

#include "sched/affinity.h"
#include "sync/lock.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace mega {

// 어느 잠금 구현으로 워커를 돌릴 것인가.
enum class WorkerLock : uint8_t { Std, Futex };

namespace detail {

// 두 잠금 구현을 같은 표면 뒤에 둔다. 분기는 코어당 한 번이 아니라 잠금당 한 번
// 일어나지만, 그 분기(예측 가능한 멤버 비교)의 비용은 잠금 자체보다 두 자릿수
// 작다 — 그래서 이 표면이 측정을 오염시키지 않는다.
class HybridMutex {
public:
    void SetKind(WorkerLock kind) { kind_ = kind; }
    WorkerLock Kind() const { return kind_; }

    void Lock() {
        if (kind_ == WorkerLock::Futex) futex_.Lock();
        else standard_.lock();
    }
    void Unlock() {
        if (kind_ == WorkerLock::Futex) futex_.Unlock();
        else standard_.unlock();
    }

private:
    friend class HybridCondVar;
    std::mutex   standard_;
    sync::Mutex  futex_;
    WorkerLock   kind_ = WorkerLock::Std;
};

class HybridGuard {
public:
    explicit HybridGuard(HybridMutex& m) : m_(m) { m_.Lock(); }
    ~HybridGuard() { m_.Unlock(); }
    HybridGuard(const HybridGuard&) = delete;
    HybridGuard& operator=(const HybridGuard&) = delete;
private:
    HybridMutex& m_;
};

class HybridCondVar {
public:
    void SetKind(WorkerLock kind) { kind_ = kind; }

    // 잠긴 m 을 들고 들어와 잠긴 m 을 들고 나간다(양쪽 구현 모두).
    void WaitFor(HybridMutex& m, uint32_t milliseconds) {
        if (kind_ == WorkerLock::Futex) {
            futex_.WaitFor(m.futex_, milliseconds);
            return;
        }
        std::unique_lock<std::mutex> lock(m.standard_, std::adopt_lock);
        standard_.wait_for(lock, std::chrono::milliseconds(milliseconds));
        lock.release();   // 잠금은 유지, 소유권만 놓는다
    }
    void NotifyAll() {
        if (kind_ == WorkerLock::Futex) futex_.NotifyAll();
        else standard_.notify_all();
    }
    void NotifyOne() {
        if (kind_ == WorkerLock::Futex) futex_.NotifyOne();
        else standard_.notify_one();
    }

private:
    std::condition_variable standard_;
    sync::CondVar           futex_;
    WorkerLock              kind_ = WorkerLock::Std;
};

}  // namespace detail

class Scheduler {
public:
    using Task = std::function<void()>;

    struct Config {
        int        workers = 0;                    // 0 = 하드웨어 동시성 - 1
        WorkerLock lock = WorkerLock::Std;
        bool       pinToCores = false;             // 워커를 코어에 고정하는가
    };

    // numWorkers=0 이면 하드웨어 동시성-1. 메인 스레드도 드레인에 참여한다.
    void Start(int numWorkers = 0) { Start(Config{numWorkers, WorkerLock::Std, false}); }

    // snippet-begin: scheduler-start
    void Start(const Config& config) {
        Stop();
        int hw = (int)std::thread::hardware_concurrency();
        int n  = config.workers > 0 ? config.workers : (hw > 1 ? hw - 1 : 1);
        // 큐는 워커 수 + 1(메인=인덱스 0). 워커는 1..n.
        queues_ = std::vector<Queue>(n + 1);
        for (Queue& queue : queues_) queue.m.SetKind(config.lock);
        cvMutex_.SetKind(config.lock);
        cv_.SetKind(config.lock);
        stop_   = false;
        lock_   = config.lock;
        pinToCores_ = config.pinToCores;
        pinnedWorkers_.store(0, std::memory_order_relaxed);
        workerCores_.assign(static_cast<size_t>(n + 1), -1);
        workers_.reserve(n);
        for (int i = 1; i <= n; ++i)
            workers_.emplace_back([this, i] { WorkerLoop(i); });
        workerCount_ = n;
    }
    // snippet-end: scheduler-start

    void Stop() {
        if (workers_.empty() && queues_.empty()) return;
        { detail::HybridGuard guard(cvMutex_); stop_ = true; }
        cv_.NotifyAll();
        for (auto& t : workers_) if (t.joinable()) t.join();
        workers_.clear();
        queues_.clear();
        workerCount_ = 0;
    }

    ~Scheduler() { Stop(); }

    int WorkerCount() const { return workerCount_; }
    WorkerLock LockKind() const { return lock_; }
    // 실제로 코어에 고정된 워커 수(권한이 없으면 0일 수 있다).
    int PinnedWorkers() const { return pinnedWorkers_.load(std::memory_order_relaxed); }
    // 워커 i 가 마지막으로 관측된 논리 CPU 번호(-1 = 모름).
    int WorkerCore(size_t i) const { return i < workerCores_.size() ? workerCores_[i] : -1; }

    // [0,count) 를 grain 크기 청크로 쪼개 병렬 실행 후 동기. fn(begin,end).
    void ParallelFor(size_t count, size_t grain,
                     const std::function<void(size_t begin, size_t end)>& fn) {
        if (count == 0) return;
        if (grain == 0) grain = 1;

        // 워커가 없으면(단일 스레드) 직렬 실행.
        if (workerCount_ == 0) { fn(0, count); return; }

        std::atomic<int> remaining{0};
        size_t pushTo = 1;   // 라운드로빈 분배 시작(워커 큐부터)
        for (size_t begin = 0; begin < count; begin += grain) {
            size_t end = begin < count - grain ? begin + grain : count;
            remaining.fetch_add(1, std::memory_order_relaxed);
            Task t = [&fn, begin, end, &remaining] {
                fn(begin, end);
                remaining.fetch_sub(1, std::memory_order_acq_rel);
            };
            Push(pushTo % queues_.size(), std::move(t));
            ++pushTo;
        }
        cv_.NotifyAll();

        // 메인(인덱스 0)도 직접 일을 처리하며 완료를 기다린다(블로킹 회피·기여).
        Task t;
        while (remaining.load(std::memory_order_acquire) > 0) {
            if (PopOwn(0, t) || StealAny(0, t)) t();
            else std::this_thread::yield();
        }
    }

    // 단발 작업 제출(fire-and-forget). 동기화는 호출자 책임.
    void Submit(Task task) {
        Push(1 % queues_.size(), std::move(task));
        cv_.NotifyOne();
    }

private:
    struct Queue {
        detail::HybridMutex m;
        std::deque<Task>    q;
        Queue() = default;
        Queue(const Queue&) = delete;
        Queue(Queue&&) noexcept {}                 // 벡터 재배치용(빈 큐만 이동)
    };

    void Push(size_t idx, Task&& t) {
        detail::HybridGuard guard(queues_[idx].m);
        queues_[idx].q.push_back(std::move(t));
    }
    bool PopOwn(size_t idx, Task& out) {           // LIFO
        detail::HybridGuard guard(queues_[idx].m);
        if (queues_[idx].q.empty()) return false;
        out = std::move(queues_[idx].q.back());
        queues_[idx].q.pop_back();
        return true;
    }
    bool StealAny(size_t self, Task& out) {        // 다른 큐에서 FIFO
        for (size_t k = 0; k < queues_.size(); ++k) {
            if (k == self) continue;
            detail::HybridGuard guard(queues_[k].m);
            if (queues_[k].q.empty()) continue;
            out = std::move(queues_[k].q.front());
            queues_[k].q.pop_front();
            return true;
        }
        return false;
    }

    // snippet-begin: worker-loop
    void WorkerLoop(int idx) {
        // 코어 고정은 워커 스레드의 수명 전체에 걸린다. 소멸자가 이전 마스크를
        // 되돌리므로, 실패(권한 없음)해도 이 스레드는 그냥 OS 배치로 돈다.
        sched::ScopedAffinity pin(pinToCores_
            ? static_cast<int>(static_cast<unsigned>(idx) % sched::HardwareThreads())
            : -1);
        if (pin.Ok()) {
            pinnedWorkers_.fetch_add(1, std::memory_order_relaxed);
            sched::SetThreadPriorityHint(sched::Priority::High);
        }
        if (idx >= 0 && static_cast<size_t>(idx) < workerCores_.size())
            workerCores_[static_cast<size_t>(idx)] = sched::CurrentCore();

        Task t;
        while (true) {
            if (PopOwn(idx, t) || StealAny(idx, t)) { t(); continue; }
            // 할 일 없음 — 깨어날 때까지 대기(주기적 재확인).
            detail::HybridGuard guard(cvMutex_);
            if (stop_) return;
            cv_.WaitFor(cvMutex_, 1);
            if (stop_) return;
        }
    }
    // snippet-end: worker-loop

    std::vector<std::thread> workers_;
    std::vector<Queue>       queues_;
    std::vector<int>         workerCores_;
    detail::HybridMutex      cvMutex_;
    detail::HybridCondVar    cv_;
    std::atomic<int>         pinnedWorkers_{0};
    bool                     stop_ = false;
    bool                     pinToCores_ = false;
    WorkerLock               lock_ = WorkerLock::Std;
    int                      workerCount_ = 0;
};

}  // namespace mega
