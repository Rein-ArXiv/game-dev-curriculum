#include "measure/event_benchmark.h"

#include <chrono>
#include <cstdint>

namespace ppl {

void EventBenchmark::Init(std::size_t targetEventCount) {
    target_ = targetEventCount;
}

void EventBenchmark::RunFrame(IAllocator* alloc) {
    if (!alloc || target_ == 0) {
        lastAllocCount_ = 0;
        lastAllocBytes_ = 0;
        lastFrameMs_    = 0.0f;
        return;
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
        void* p = alloc->Allocate(sizeof(Node), alignof(Node));
        if (!p) break;  // Pool/Slab 풀 고갈 시 nullptr
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
