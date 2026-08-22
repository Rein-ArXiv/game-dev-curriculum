// worker_lock=std|futex — 두 잠금 구현이 같은 스케줄러를 돌리는가.
//
// 잠금을 바꾸는 것은 결과가 아니라 비용의 문제여야 한다. 그래서 여기서 묻는 것은
// 세 가지다.
//   1. 같은 작업 집합이 **정확히 한 번씩** 실행된다(두 번도, 0번도 아니다).
//   2. 스톱 신호가 걸린 워커가 전부 join 된다 — 못 깨어난 워커가 하나라도 있으면
//      이 테스트는 실패가 아니라 **타임아웃**으로 끝난다.
//   3. 경합 구간의 실측 시간(두 팔 모두 출력한다. 단언하지 않는다 — 머신 부하에
//      따라 흔들리는 수를 게이트로 쓰면 게이트가 먼저 무너진다).

#include "jobs/scheduler.h"
#include "test_util.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace mega;

namespace {

struct Run {
    double   millis = 0;
    uint64_t sum = 0;
};

// 짧은 임계 구역을 대량으로 두드린다 — 잠금 구현의 차이가 나타나는 모양이다.
Run Contend(WorkerLock lock, int workers, int tasks, int innerLoops) {
    Scheduler scheduler;
    Scheduler::Config config;
    config.workers = workers;
    config.lock = lock;
    scheduler.Start(config);
    CHECK_EQ(scheduler.WorkerCount(), workers);
    CHECK(scheduler.LockKind() == lock);

    std::vector<int> executed(static_cast<size_t>(tasks), 0);
    std::atomic<uint64_t> total{0};
    const auto begin = std::chrono::steady_clock::now();
    scheduler.ParallelFor(static_cast<size_t>(tasks), 1,
                          [&executed, &total, innerLoops](size_t from, size_t to) {
        for (size_t i = from; i < to; ++i) {
            ++executed[i];
            uint64_t local = 0;
            for (int k = 0; k < innerLoops; ++k) local += static_cast<uint64_t>(k) ^ i;
            total.fetch_add(local, std::memory_order_relaxed);
        }
    });
    const auto end = std::chrono::steady_clock::now();

    for (int i = 0; i < tasks; ++i) CHECK_EQ(executed[static_cast<size_t>(i)], 1);
    scheduler.Stop();                       // 여기서 매달리면 wake 가 죽은 것이다
    CHECK_EQ(scheduler.WorkerCount(), 0);

    Run run;
    run.millis = std::chrono::duration<double, std::milli>(end - begin).count();
    run.sum = total.load();
    return run;
}

void TestBothLocksRunEveryTaskExactlyOnce() {
    const int tasks = 20000;
    const Run standard = Contend(WorkerLock::Std, 4, tasks, 40);
    const Run futex = Contend(WorkerLock::Futex, 4, tasks, 40);
    CHECK_EQ(standard.sum, futex.sum);      // 같은 일을 했다
    std::printf("[worker_lock] tasks=%d workers=4  std=%.2fms  futex=%.2fms\n",
                tasks, standard.millis, futex.millis);
}

// 시작·정지를 반복해도 워커가 남지 않는다. 깨우기가 죽으면 여기서 매달린다.
void TestRepeatedStartStopJoins() {
    Scheduler scheduler;
    for (WorkerLock lock : {WorkerLock::Std, WorkerLock::Futex}) {
        for (int round = 0; round < 8; ++round) {
            Scheduler::Config config;
            config.workers = 2 + (round % 3);
            config.lock = lock;
            scheduler.Start(config);
            std::atomic<int> done{0};
            scheduler.ParallelFor(64, 4, [&done](size_t from, size_t to) {
                done.fetch_add(static_cast<int>(to - from), std::memory_order_relaxed);
            });
            CHECK_EQ(done.load(), 64);
            scheduler.Stop();
        }
    }
    CHECK_EQ(scheduler.WorkerCount(), 0);
}

// Submit(fire-and-forget)도 두 팔에서 깨어난다. 깨우기가 죽으면 1ms 재확인이
// 결국 집어 가지만, 그 경로도 여기서 함께 지킨다.
void TestSubmitWakesAWorker() {
    for (WorkerLock lock : {WorkerLock::Std, WorkerLock::Futex}) {
        Scheduler scheduler;
        Scheduler::Config config;
        config.workers = 2;
        config.lock = lock;
        scheduler.Start(config);
        std::atomic<int> ran{0};
        for (int i = 0; i < 500; ++i)
            scheduler.Submit([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (ran.load() < 500 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        CHECK_EQ(ran.load(), 500);
        scheduler.Stop();
    }
}

}  // namespace

static void RunTests() {
    TestBothLocksRunEveryTaskExactlyOnce();
    TestRepeatedStartStopJoins();
    TestSubmitWakesAWorker();
}

TEST_MAIN("scheduler_lock_ab")
