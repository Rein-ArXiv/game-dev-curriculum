// Scheduler — 워커별 큐 + 일감 훔치기.
//
// 각 워커는 자기 큐에서 LIFO 로 꺼내고(방금 쪼갠 하위 작업이 캐시에 더움),
// 비면 다른 워커의 큐에서 FIFO 로 훔친다(가장 오래된, 큰 작업을 가져와 다시
// 쪼갤 여지). 이 구조가 부하를 자동 분산해, 작업 크기가 불균일해도 워커가
// 놀지 않는다.
//
// 설계 노트: parts 시리즈는 Fiber 기반(컨텍스트 스위치 50~100ns)을 다루지만,
// Fiber 는 플랫폼 어셈블리/ucontext 의존이라 이식성·정확성 위험이 크다. 이
// stage 는 *정확성·빌드 가능성 우선* 으로 std::thread 워커 + 뮤텍스 보호 deque
// 로 구현한다. lock-free Chase-Lev deque 와 Fiber 스위치는 이 위에 얹는 설계
// 토픽으로 남긴다. ParallelFor 의 의미론(범위를 청크로 쪼개 병렬 실행 후 동기)
// 은 두 구현에서 동일하다.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace mega {

class Scheduler {
public:
    using Task = std::function<void()>;

    // numWorkers=0 이면 하드웨어 동시성-1. 메인 스레드도 드레인에 참여한다.
    void Start(int numWorkers = 0) {
        Stop();
        int hw = (int)std::thread::hardware_concurrency();
        int n  = numWorkers > 0 ? numWorkers : (hw > 1 ? hw - 1 : 1);
        // 큐는 워커 수 + 1(메인=인덱스 0). 워커는 1..n.
        queues_ = std::vector<Queue>(n + 1);
        stop_   = false;
        workers_.reserve(n);
        for (int i = 1; i <= n; ++i)
            workers_.emplace_back([this, i] { WorkerLoop(i); });
        workerCount_ = n;
    }

    void Stop() {
        if (workers_.empty() && queues_.empty()) return;
        { std::lock_guard<std::mutex> lk(cvMutex_); stop_ = true; }
        cv_.notify_all();
        for (auto& t : workers_) if (t.joinable()) t.join();
        workers_.clear();
        queues_.clear();
        workerCount_ = 0;
    }

    ~Scheduler() { Stop(); }

    int WorkerCount() const { return workerCount_; }

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
        cv_.notify_all();

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
        cv_.notify_one();
    }

private:
    struct Queue {
        std::mutex        m;
        std::deque<Task>  q;
        Queue() = default;
        Queue(const Queue&) = delete;
        Queue(Queue&&) noexcept {}                 // 벡터 재배치용(빈 큐만 이동)
    };

    void Push(size_t idx, Task&& t) {
        std::lock_guard<std::mutex> lk(queues_[idx].m);
        queues_[idx].q.push_back(std::move(t));
    }
    bool PopOwn(size_t idx, Task& out) {           // LIFO
        std::lock_guard<std::mutex> lk(queues_[idx].m);
        if (queues_[idx].q.empty()) return false;
        out = std::move(queues_[idx].q.back());
        queues_[idx].q.pop_back();
        return true;
    }
    bool StealAny(size_t self, Task& out) {        // 다른 큐에서 FIFO
        for (size_t k = 0; k < queues_.size(); ++k) {
            if (k == self) continue;
            std::lock_guard<std::mutex> lk(queues_[k].m);
            if (queues_[k].q.empty()) continue;
            out = std::move(queues_[k].q.front());
            queues_[k].q.pop_front();
            return true;
        }
        return false;
    }

    void WorkerLoop(int idx) {
        Task t;
        while (true) {
            if (PopOwn(idx, t) || StealAny(idx, t)) { t(); continue; }
            // 할 일 없음 — 깨어날 때까지 대기(주기적 재확인).
            std::unique_lock<std::mutex> lk(cvMutex_);
            if (stop_) return;
            cv_.wait_for(lk, std::chrono::milliseconds(1));
            if (stop_) return;
        }
    }

    std::vector<std::thread> workers_;
    std::vector<Queue>       queues_;
    std::mutex               cvMutex_;
    std::condition_variable  cv_;
    bool                     stop_ = false;
    int                      workerCount_ = 0;
};

}  // namespace mega
