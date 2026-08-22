// futex 기반 동기화 프리미티브 검증: 상호배제·RW 일관성·조건변수 핸드오프·OnceFlag.
#include "sync/lock.h"
#include "test_util.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <thread>
#include <vector>

using namespace mega::sync;

namespace {

// Mutex 상호배제: T 스레드가 각자 M 번 증가 → 정확히 T*M.
void TestMutex() {
    Mutex m;
    int64_t counter = 0;
    const int T = 8, M = 100000;
    std::vector<std::thread> ts;
    for (int t = 0; t < T; ++t)
        ts.emplace_back([&] {
            for (int i = 0; i < M; ++i) { LockGuard g(m); ++counter; }
        });
    for (auto& th : ts) th.join();
    CHECK_EQ(counter, (int64_t)T * M);
}

// SpinLock 상호배제.
void TestSpinLock() {
    SpinLock s;
    int64_t counter = 0;
    const int T = 8, M = 50000;
    std::vector<std::thread> ts;
    for (int t = 0; t < T; ++t)
        ts.emplace_back([&] {
            for (int i = 0; i < M; ++i) { s.Lock(); ++counter; s.Unlock(); }
        });
    for (auto& th : ts) th.join();
    CHECK_EQ(counter, (int64_t)T * M);
}

// RWLock: writer 가 (a,b) 쌍을 항상 a==b 로 유지. reader 는 찢긴 쌍을 절대 못 본다.
void TestRWLock() {
    RWLock rw;
    struct Pair { std::atomic<uint32_t> a{0}, b{0}; } p;
    std::atomic<int> torn{0};
    std::atomic<bool> stop{false};
    const int writers = 3, readers = 5, M = 40000;

    std::vector<std::thread> ts;
    for (int w = 0; w < writers; ++w)
        ts.emplace_back([&, w] {
            for (int i = 0; i < M; ++i) {
                rw.Lock();
                uint32_t v = (uint32_t)(w * M + i);
                p.a.store(v, std::memory_order_relaxed);
                p.b.store(v, std::memory_order_relaxed);   // 쓰기 락 안에서 둘을 같게
                rw.Unlock();
            }
        });
    for (int r = 0; r < readers; ++r)
        ts.emplace_back([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                rw.LockShared();
                uint32_t a = p.a.load(std::memory_order_relaxed);
                uint32_t b = p.b.load(std::memory_order_relaxed);
                if (a != b) torn.fetch_add(1, std::memory_order_relaxed);
                rw.UnlockShared();
            }
        });
    for (int w = 0; w < writers; ++w) ts[w].join();   // writer 끝나면
    stop.store(true);                                  // reader 정지
    for (size_t i = writers; i < ts.size(); ++i) ts[i].join();
    CHECK_EQ(torn.load(), 0);                          // 찢긴 관측 0회
}

// CondVar: 용량 제한 블로킹 큐로 생산자/소비자 핸드오프.
struct BlockingQueue {
    Mutex m;
    CondVar notEmpty, notFull;
    std::deque<int> q;
    size_t cap;
    explicit BlockingQueue(size_t c) : cap(c) {}

    void Push(int v) {
        LockGuard g(m);
        while (q.size() >= cap) notFull.Wait(m);
        q.push_back(v);
        notEmpty.NotifyOne();
    }
    int Pop() {
        LockGuard g(m);
        while (q.empty()) notEmpty.Wait(m);
        int v = q.front();
        q.pop_front();
        notFull.NotifyOne();
        return v;
    }
};

void TestCondVar() {
    BlockingQueue bq(16);            // 작은 용량 → 양방향 블로킹 강제
    const int N = 200000;
    int64_t sum = 0;
    std::thread consumer([&] {
        for (int i = 0; i < N; ++i) sum += bq.Pop();
    });
    for (int i = 0; i < N; ++i) bq.Push(i);
    consumer.join();
    CHECK_EQ(sum, (int64_t)(N - 1) * N / 2);   // 0..N-1 합, 누락·중복 없음
}

// OnceFlag: T 스레드가 동시에 CallOnce → 초기화는 정확히 1회, 결과는 모두 관측.
void TestOnceFlag() {
    OnceFlag once;
    std::atomic<int> runs{0};
    int value = 0;
    const int T = 16;
    std::vector<std::thread> ts;
    std::vector<int> seen(T, -1);
    for (int t = 0; t < T; ++t)
        ts.emplace_back([&, t] {
            once.CallOnce([&] { ++runs; value = 42; });
            seen[t] = value;             // CallOnce 반환 시 초기화 완료 보장
        });
    for (auto& th : ts) th.join();
    CHECK_EQ(runs.load(), 1);            // 정확히 한 번만 실행
    for (int t = 0; t < T; ++t) CHECK_EQ(seen[t], 42);
}


// ── 스핀 → 대기 전이와 깨우기 대상 ─────────────────────────────────
// 잡 스케줄러가 futex 팔로 돌 때 여기 있는 세 성질에 목숨이 걸린다.
//   1. FutexWait 는 값이 이미 다르면 **자지 않는다**(lost-wakeup 을 닫는 비교).
//   2. WakeOne 은 정확히 하나만, WakeAll 은 전부 깨운다.
//   3. FutexWaitFor 는 아무도 깨우지 않아도 시한이 지나면 돌아온다 — 큐가 비었다고
//      판정한 순간과 잠드는 순간 사이에 들어온 작업이 워커를 영영 재우지 못하게
//      막는 마지막 방어선이다.
void TestFutexWaitCompare() {
    std::atomic<uint32_t> word{7};
    // 기대값이 다르면 즉시 반환한다. 여기서 매달리면 이 테스트는 타임아웃으로 죽는다.
    FutexWait(&word, 1);
    CHECK_EQ(word.load(), 7u);
}

void TestFutexTimedWaitReturns() {
    std::atomic<uint32_t> word{0};
    const auto begin = std::chrono::steady_clock::now();
    FutexWaitFor(&word, 0, 30);            // 아무도 깨우지 않는다
    const double elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    CHECK(elapsed >= 1.0);                 // 정말 잤다(즉시 반환이 아니다)
    CHECK(elapsed < 5000.0);               // 그리고 돌아왔다
}

void TestWakeOneWakesExactlyOne() {
    std::atomic<uint32_t> word{0};
    std::atomic<int> awake{0};
    const int T = 4;
    std::vector<std::thread> ts;
    for (int t = 0; t < T; ++t)
        ts.emplace_back([&] {
            while (word.load(std::memory_order_acquire) == 0)
                FutexWaitFor(&word, 0, 5);
            awake.fetch_add(1, std::memory_order_relaxed);
        });

    // 값을 바꾸지 않은 채 하나만 깨운다. 값이 그대로라 깨어난 스레드는 다시 잔다.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK_EQ(awake.load(), 0);
    FutexWakeOne(&word);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK_EQ(awake.load(), 0);             // 술어가 거짓이라 아무도 빠져나가지 못한다

    word.store(1, std::memory_order_release);
    FutexWakeAll(&word);
    for (auto& th : ts) th.join();
    CHECK_EQ(awake.load(), T);             // 전부 깨어났다
}

// CondVar::WaitFor: 통지를 놓쳐도 시한이 스레드를 되살린다.
void TestCondVarTimedWait() {
    Mutex m;
    CondVar cv;
    std::atomic<bool> ready{false};
    std::atomic<int> wakeups{0};
    std::thread waiter([&] {
        m.Lock();
        while (!ready.load(std::memory_order_acquire)) {
            cv.WaitFor(m, 2);
            wakeups.fetch_add(1, std::memory_order_relaxed);
        }
        m.Unlock();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    // 통지를 한 번도 보내지 않았는데도 대기자가 여러 번 깨어나 술어를 다시 봤다.
    CHECK(wakeups.load() > 0);
    ready.store(true, std::memory_order_release);
    cv.NotifyAll();
    waiter.join();
}

}  // namespace

static void RunTests() {
    TestMutex();
    TestSpinLock();
    TestRWLock();
    TestCondVar();
    TestOnceFlag();
    TestFutexWaitCompare();
    TestFutexTimedWaitReturns();
    TestWakeOneWakesExactlyOne();
    TestCondVarTimedWait();
}

TEST_MAIN("sync")
