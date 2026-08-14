// 스레드 배치 검증: affinity 가 실제로 코어를 제한하는지, 우선순위·NUMA 질의.
#include "sched/affinity.h"
#include "test_util.h"

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
