#include "measure/event_benchmark.h"

#include <chrono>
#include <cstdint>

namespace ppl {

void EventBenchmark::ResetLast() {
    lastAllocCount_ = 0;
    lastAllocBytes_ = 0;
    lastFrameMs_    = 0.0f;
    attempted_      = 0;
    failed_         = 0;
    status_         = BenchStatus::Skipped;
}

void EventBenchmark::Init(std::size_t targetEventCount) {
    target_ = targetEventCount;
    // target=0으로 다시 Init한 뒤 RunFrame을 돌려도 직전 프레임 결과가 남지 않는다.
    ResetLast();
}

void EventBenchmark::RunFrame(IAllocator* alloc) {
    // 매 프레임 먼저 비운다. null/0 경로에서도 옛 allocator의 frame 카운터가
    // 새 결과로 보이지 않게 한다.
    ResetLast();

    if (!alloc || target_ == 0) {
        return;  // Skipped
    }

    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();

    // 프레임 단위 통계 카운터 초기화.
    alloc->TickFrame();

    // N개 노드 alloc, linked list로 연결.
    // 매 호출이 작은 블록 alloc이라 할당자 차이가 가장 잘 보인다.
    Node* head = nullptr;
    std::size_t count = 0;
    for (std::size_t i = 0; i < target_; ++i) {
        ++attempted_;
        void* p = alloc->Allocate(sizeof(Node), alignof(Node));
        if (!p) {
            ++failed_;   // Pool/Slab 고갈 등
            break;
        }
        Node* node = static_cast<Node*>(p);
        node->id   = static_cast<std::uint32_t>(i);
        node->dist = 0.0f;
        node->payload = (static_cast<std::uint64_t>(i) << 32) ^ 0xC0FFEEu;
        node->next = head;
        head = node;
        ++count;
    }

    lastAllocCount_ = count;
    lastAllocBytes_ = count * sizeof(Node);
    status_ = (count == target_) ? BenchStatus::Valid
                                 : BenchStatus::InvalidCapacity;

    // 노드별 Deallocate. Arena는 통계만 기록(메모리는 Reset이 회수).
    // Pool은 free list에 push, Reset이 list를 다시 초기화.
    Node* cur = head;
    while (cur) {
        Node* next = cur->next;
        alloc->Deallocate(cur, sizeof(Node));
        cur = next;
    }
    // Reset: Arena/Pool/Slab은 일괄 회수, Malloc은 no-op.
    alloc->Reset();

    const auto t1 = Clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    lastFrameMs_ = static_cast<float>(ns) / 1.0e6f;
}

}  // namespace ppl
