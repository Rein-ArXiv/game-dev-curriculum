// process.hpp 의 구현. 신호 처리기 안에서는 atomic 플래그 하나만 만진다.

#include "platform/process.hpp"

#include <atomic>
#include <csignal>

namespace mega {
namespace platform {
namespace {

// sig_atomic_t 가 아니라 std::atomic 을 쓰는 이유: 폴링하는 쪽이 다른 스레드일
// 수 있고, lock-free 정수 저장은 신호 처리기에서도 안전하다.
std::atomic_bool g_shutdown{false};

extern "C" void HandleShutdownSignal(int) {
    g_shutdown.store(true, std::memory_order_relaxed);
}

}  // namespace

void InstallShutdownSignals() {
    std::signal(SIGINT, HandleShutdownSignal);
    std::signal(SIGTERM, HandleShutdownSignal);
}

bool ShutdownRequested() { return g_shutdown.load(std::memory_order_relaxed); }

void ClearShutdownRequest() { g_shutdown.store(false, std::memory_order_relaxed); }

void RequestShutdown() { g_shutdown.store(true, std::memory_order_relaxed); }

}  // namespace platform
}  // namespace mega
