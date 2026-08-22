// affinity.h 의 플랫폼별 구현. <windows.h> / pthread 헤더를 여기에 가둔다.

#include "sched/affinity.h"

#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

namespace mega {
namespace sched {

unsigned HardwareThreads() {
    unsigned n = std::thread::hardware_concurrency();
    return n ? n : 1u;
}

bool SetAffinityMask(uint64_t mask) {
#if defined(_WIN32)
    // SetThreadAffinityMask 는 이전 마스크를 반환하고, 실패 시 0.
    return SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(mask)) != 0;
#elif defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int i = 0; i < 64; ++i)
        if (mask & (uint64_t(1) << i)) CPU_SET(i, &set);
    return sched_setaffinity(0, sizeof(set), &set) == 0;
#else
    (void)mask;
    return false;
#endif
}

bool PinThreadToCore(int core) {
    if (core < 0 || core >= 64) return false;
    return SetAffinityMask(uint64_t(1) << core);
}

bool SetThreadPriorityHint(Priority p) {
#if defined(_WIN32)
    int prio;
    switch (p) {
        case Priority::Idle:     prio = THREAD_PRIORITY_IDLE;          break;
        case Priority::Low:      prio = THREAD_PRIORITY_BELOW_NORMAL;  break;
        case Priority::Normal:   prio = THREAD_PRIORITY_NORMAL;        break;
        case Priority::High:     prio = THREAD_PRIORITY_ABOVE_NORMAL;  break;
        case Priority::Realtime: prio = THREAD_PRIORITY_TIME_CRITICAL; break;
        default:                 prio = THREAD_PRIORITY_NORMAL;        break;
    }
    return SetThreadPriority(GetCurrentThread(), prio) != 0;
#elif defined(__linux__)
    // 비실시간은 nice 로, 실시간 급은 SCHED_FIFO 로(권한 필요).
    if (p == Priority::Realtime || p == Priority::High) {
        sched_param sp{};
        sp.sched_priority = (p == Priority::Realtime) ? 80 : 50;
        return pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0;
    }
    int nice_val = (p == Priority::Idle) ? 19 : (p == Priority::Low ? 10 : 0);
    return nice(nice_val) != -1;
#else
    (void)p;
    return false;
#endif
}

int CurrentCore() {
#if defined(_WIN32)
    return static_cast<int>(GetCurrentProcessorNumber());
#elif defined(__linux__)
    int c = sched_getcpu();
    return c;
#else
    return -1;
#endif
}

int NumaNodeOfCore(int core) {
#if defined(_WIN32)
    UCHAR node = 0;
    if (GetNumaProcessorNode(static_cast<UCHAR>(core), &node)) return static_cast<int>(node);
    return -1;
#elif defined(__linux__)
    // libnuma 의존을 피해 단일 노드로 가정(노드 토폴로지는 lab 에서 측정).
    (void)core;
    return 0;
#else
    (void)core;
    return -1;
#endif
}

// snippet-begin: scoped-affinity
ScopedAffinity::ScopedAffinity(int core) {
#if defined(_WIN32)
    if (core >= 0 && core < 64) {
        DWORD_PTR prev = SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(uint64_t(1) << core));
        if (prev != 0) { prevMask_ = static_cast<uint64_t>(prev); ok_ = true; }
    }
#elif defined(__linux__)
    cpu_set_t prev;
    CPU_ZERO(&prev);
    if (sched_getaffinity(0, sizeof(prev), &prev) == 0) {
        uint64_t m = 0;
        for (int i = 0; i < 64; ++i) if (CPU_ISSET(i, &prev)) m |= (uint64_t(1) << i);
        prevMask_ = m;
        ok_ = PinThreadToCore(core);
    }
#else
    (void)core;
#endif
}

ScopedAffinity::~ScopedAffinity() {
    if (ok_) SetAffinityMask(prevMask_);   // 이전 마스크 복원
}
// snippet-end: scoped-affinity

}  // namespace sched
}  // namespace mega
