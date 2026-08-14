#pragma once

#include <atomic>
#include <cstddef>

namespace skl {

// ============================================================================
// 테스트 전용 타이밍 훅
//
// fault seam(src/fault)이 "이 syscall 이 실패하면?" 을 결정적으로 만든다면,
// 이 훅은 "두 스레드가 하필 이 순서로 만나면?" 을 결정적으로 만든다.
//
// 필요한 이유. FileIoWorkload::RunCycleMulti 의
//
//     queue_.push(...) x N;
//     remaining_.store(N, release);   // ← 이 줄이 Unlock *앞* 에 있어야 한다
//     queueSync_->Unlock();
//
// 에서 store 를 Unlock 뒤로 내리면 다음 인터리빙이 생긴다.
//
//     직전 사이클의 마지막 작업을 막 끝낸 워커는 아직 내부 pop 루프 안에 있다.
//     그 워커가 Unlock 직후 ~ store 직전에 깨어나면 이번 사이클의 항목을 pop 해
//     remaining_ 이 0 인 상태에서 fetch_sub(1) 을 한다 → SIZE_MAX 언더플로.
//     그 뒤 main 의 store(N) 이 카운터를 덮어써서, 실제로는 N-1 개만 남았는데
//     N 을 기다리게 되고 완료 대기가 영원히 끝나지 않는다.
//
// 이 창은 보통 수백 나노초라 반복 실행만으로는 재현이 확률적이다. 플래키한
// 회귀 테스트는 없느니만 못하므로, 테스트가 그 창을 직접 벌린다:
//   - WorkerAfterDecrement 훅이 remaining_ 을 0 으로 만든 워커를 붙잡아 두고,
//   - AfterQueueUnlock 훅이 push 를 끝낸 직후 그 워커를 풀어 준 다음
//     그 워커가 감산을 마칠 때까지 기다린다.
// 두 훅이 랑데부를 이루어 문제의 인터리빙을 100% 재현한다.
//
// 비용. SKL_TEST_HOOKS 가 정의되지 않은 빌드(= 앱 타깃)에서는 훅 호출이
// 빈 인라인 함수로 완전히 사라진다. 제품 바이너리에는 분기도 load 도 남지
// 않는다. 무장하는 API(SetTestHook)도 그 빌드에는 존재하지 않는다.
// ============================================================================

enum class TestHookId : int {
    // RunCycleMulti 가 작업 큐 락을 푼 직후. 인자는 0.
    AfterQueueUnlock = 0,
    // WorkerLoop 이 remaining_.fetch_sub(1) 을 한 직후.
    // 인자는 fetch_sub 의 반환값(감산 *직전* 값). 1 이면 이 워커가 사이클의
    // 마지막 작업을 끝낸 것이고, 0 이면 언더플로가 일어난 것이다.
    WorkerAfterDecrement = 1,

    Count = 2,
};

#if defined(SKL_TEST_HOOKS)

using TestHookFn = void (*)(std::size_t);

namespace detail {
inline std::atomic<TestHookFn>
    g_testHooks[static_cast<int>(TestHookId::Count)]{};
}  // namespace detail

inline void RunTestHook(TestHookId id, std::size_t arg = 0) {
    const auto fn =
        detail::g_testHooks[static_cast<int>(id)].load(std::memory_order_acquire);
    if (fn) fn(arg);
}

// nullptr 로 부르면 해제. 훅 안에서 자기 자신을 해제해도 안전하다.
inline void SetTestHook(TestHookId id, TestHookFn fn) {
    detail::g_testHooks[static_cast<int>(id)].store(fn,
                                                    std::memory_order_release);
}

#else

inline void RunTestHook(TestHookId, std::size_t = 0) {}

#endif  // SKL_TEST_HOOKS

}  // namespace skl
