// Futex — 사용자 공간 fast-path + 커널 대기를 결합하는 동기화의 바닥.
//
// futex(fast userspace mutex)의 핵심 아이디어: 경합이 없으면 원자 연산만으로
// 끝내고(시스템 콜 0회), 경합이 있을 때만 커널에 "이 주소가 이 값인 동안 재워
// 달라"고 요청한다. mutex/condvar/rwlock 을 전부 이 위에 직접 쌓으면, 빠른
// 경로에서 glibc 호출 오버헤드와 불필요한 시스템 콜을 들어낼 수 있다.
//
// 플랫폼 등가물:
//   - Linux : SYS_futex (FUTEX_WAIT/WAKE_PRIVATE)
//   - Windows: WaitOnAddress / WakeByAddress* (Win8+) — futex 와 의미가 같다.
// 두 경로 모두 "주소의 현재 값이 expected 와 다르면 즉시 반환"하는 비교 의미를
// 가져, lost-wakeup 경쟁을 닫는다(값을 바꾸고 깨우는 쪽과의 race).
//
// 이 머신(Windows)에서는 WaitOnAddress 경로가 실제 빌드·검증된다. Linux 경로는
// 동봉하되 이 환경에서 컴파일되지 않는다.

#pragma once

#include <atomic>
#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>   // _mm_pause
#endif

#if defined(_WIN32)
// <windows.h> 오염을 피하려 필요한 심볼만 직접 선언(Synchronization.lib 링크).
extern "C" __declspec(dllimport) int  __stdcall
    WaitOnAddress(volatile void* Address, void* CompareAddress, size_t AddressSize, unsigned long dwMilliseconds);
extern "C" __declspec(dllimport) void __stdcall WakeByAddressSingle(void* Address);
extern "C" __declspec(dllimport) void __stdcall WakeByAddressAll(void* Address);
#elif defined(__linux__)
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <climits>
#endif

namespace mega {
namespace sync {

// addr 의 값이 expected 와 같은 동안 호출 스레드를 잰다(다르면 즉시 반환).
inline void FutexWait(std::atomic<uint32_t>* addr, uint32_t expected) {
#if defined(_WIN32)
    uint32_t cmp = expected;
    WaitOnAddress(reinterpret_cast<volatile void*>(addr), &cmp, sizeof(cmp), 0xFFFFFFFFul /*INFINITE*/);
#elif defined(__linux__)
    syscall(SYS_futex, reinterpret_cast<uint32_t*>(addr), FUTEX_WAIT_PRIVATE, expected, nullptr, nullptr, 0);
#else
    (void)addr; (void)expected;   // 폴백 없음: 단일 스레드 빌드 전용
#endif
}

// addr 에서 자고 있는 스레드 하나를 깨운다.
inline void FutexWakeOne(std::atomic<uint32_t>* addr) {
#if defined(_WIN32)
    WakeByAddressSingle(reinterpret_cast<void*>(addr));
#elif defined(__linux__)
    syscall(SYS_futex, reinterpret_cast<uint32_t*>(addr), FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
#else
    (void)addr;
#endif
}

// addr 에서 자고 있는 스레드 전부를 깨운다.
inline void FutexWakeAll(std::atomic<uint32_t>* addr) {
#if defined(_WIN32)
    WakeByAddressAll(reinterpret_cast<void*>(addr));
#elif defined(__linux__)
    syscall(SYS_futex, reinterpret_cast<uint32_t*>(addr), FUTEX_WAKE_PRIVATE, INT_MAX, nullptr, nullptr, 0);
#else
    (void)addr;
#endif
}

// CPU 힌트: 스핀 대기 시 파이프라인을 양보해 경합 비용·전력을 줄인다.
inline void CpuPause() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _mm_pause();
#elif defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    __asm__ __volatile__("yield");
#endif
}

}  // namespace sync
}  // namespace mega
