// 스레드 배치 검증: affinity 가 실제로 코어를 제한하는지, 우선순위·NUMA 질의.
#include "jobs/scheduler.h"
#include "sched/affinity.h"
#include "test_util.h"

#include <atomic>
#include <chrono>
#include <set>
#include <thread>
#include <vector>

using namespace mega::sched;

// 코어에 고정한 뒤 yield 를 반복해 그 코어로 옮겨갔는지 확인(스케줄러 반영 대기).
static bool LandsOnCore(int core) {
    for (int spin = 0; spin < 2000; ++spin) {
        if (CurrentCore() == core) return true;
        std::this_thread::yield();
    }
    return CurrentCore() == core;
}

static void RunTests() {
    const unsigned hw = HardwareThreads();
    CHECK(hw >= 1);

    // --- 현재 코어 질의가 유효 범위 ---
    {
        int c = CurrentCore();
        CHECK(c >= 0 && c < (int)hw);
    }

    // --- affinity 가 실제로 코어를 제한 ---
    // 별도 스레드를 코어 0에 고정하고, 그 스레드가 코어 0에서 도는지 확인.
    {
        bool pinned = false, landed = false;
        std::thread th([&] {
            pinned = PinThreadToCore(0);
            landed = LandsOnCore(0);
        });
        th.join();
        CHECK(pinned);
        CHECK(landed);     // 고정 후 코어 0에서 실행됨
    }

    // --- 여러 워커를 서로 다른 코어에 고정 → 각자 제 코어에서 실행 ---
    {
        int workers = (int)hw < 4 ? (int)hw : 4;
        std::vector<int> observed(workers, -1);
        std::vector<std::thread> ts;
        for (int w = 0; w < workers; ++w)
            ts.emplace_back([&, w] {
                if (PinThreadToCore(w) && LandsOnCore(w)) observed[w] = CurrentCore();
            });
        for (auto& t : ts) t.join();
        for (int w = 0; w < workers; ++w) CHECK_EQ(observed[w], w);   // 워커 w → 코어 w
    }

    // --- ScopedAffinity 가 고정·복원 ---
    {
        std::thread th([&] {
            int target = (int)hw > 1 ? 1 : 0;
            {
                ScopedAffinity sa(target);
                CHECK(sa.Ok());
                CHECK(LandsOnCore(target));     // 스코프 안: 고정됨
            }
            // 스코프 종료 후 마스크 복원됨(여러 코어 허용) — 크래시 없음만 확인.
            CHECK(CurrentCore() >= 0);
        });
        th.join();
    }


    // --- 중첩 ScopedAffinity: 안쪽이 끝나면 바깥의 마스크로 돌아온다 ---
    // 소멸자가 이전 마스크를 복원하지 않으면 안쪽 스코프를 나온 뒤에도 스레드가
    // 안쪽 코어에 남는다. 워커 루프가 RAII 로 코어를 잡는 이상, 이 복원이 없으면
    // 스케줄러가 끝난 뒤에도 그 스레드는 한 코어에 묶인 채로 남는다.
    if (hw >= 2) {
        std::thread th([&] {
            ScopedAffinity outer(0);
            CHECK(outer.Ok());
            CHECK(LandsOnCore(0));
            {
                ScopedAffinity inner(1);
                CHECK(inner.Ok());
                CHECK(LandsOnCore(1));
            }
            CHECK(LandsOnCore(0));      // 바깥 마스크로 복원되었다
        });
        th.join();
    }

    // --- 잡 워커의 코어 고정(affinity=1) ---
    // 스케줄러가 이 헤더를 부르는 유일한 제품 경로다. 고정이 걸리면 워커마다
    // 서로 다른 코어에서 돌고, 걸리지 않는 환경에서도 결과는 같아야 한다.
    {
        for (bool pin : {false, true}) {
            mega::Scheduler scheduler;
            mega::Scheduler::Config config;
            config.workers = (int)hw < 4 ? (int)hw : 4;
            config.pinToCores = pin;
            scheduler.Start(config);
            // 워커가 실제로 스케줄되기를 기다린다. ParallelFor 는 메인 스레드도
            // 드레인에 참여하므로, 일이 가벼우면 워커가 한 번도 깨지 않은 채로
            // 끝날 수 있다 — 그 상태에서 배치를 물으면 "아직 아무 데도" 가 답이다.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            for (;;) {
                bool allSeen = true;
                for (int w = 1; w <= config.workers; ++w)
                    if (scheduler.WorkerCore((size_t)w) < 0) allSeen = false;
                if (allSeen || std::chrono::steady_clock::now() >= deadline) break;
                std::this_thread::yield();
            }
            std::atomic<int> sum{0};
            scheduler.ParallelFor(4096, 64, [&sum](size_t begin, size_t end) {
                sum.fetch_add((int)(end - begin), std::memory_order_relaxed);
            });
            CHECK_EQ(sum.load(), 4096);          // 결과는 고정 여부와 무관하다
            if (pin) {
                CHECK_EQ(scheduler.PinnedWorkers(), config.workers);
                std::set<int> cores;
                for (int w = 1; w <= config.workers; ++w) {
                    const int core = scheduler.WorkerCore((size_t)w);
                    CHECK(core >= 0);
                    cores.insert(core);
                }
                CHECK_EQ((int)cores.size(), config.workers);   // 서로 다른 코어
            } else {
                CHECK_EQ(scheduler.PinnedWorkers(), 0);
            }
            scheduler.Stop();
        }
    }

    // --- 우선순위 힌트 ---
    {
        std::thread th([&] {
            // High: Windows 는 ABOVE_NORMAL(권한 불필요)이지만 Linux 구현은
            // SCHED_FIFO 라 CAP_SYS_NICE 없으면 false 가 정상(헤더 명세:
            // "권한 부족 시 false 일 수 있다"). 성공 여부 대신 크래시 없음만 확인.
            bool high = SetThreadPriorityHint(Priority::High);
#if defined(_WIN32)
            CHECK(high);                                     // ABOVE_NORMAL: 권한 불필요
#else
            (void)high;                                      // 권한 의존 — 결과 비단정
            CHECK(true);
#endif
            CHECK(SetThreadPriorityHint(Priority::Normal));
        });
        th.join();
    }

    // --- NUMA 노드 질의 ---
    {
        int node = NumaNodeOfCore(0);
        CHECK(node >= 0);    // 단일 노드 데스크톱이면 0
    }
}

TEST_MAIN("sched")
