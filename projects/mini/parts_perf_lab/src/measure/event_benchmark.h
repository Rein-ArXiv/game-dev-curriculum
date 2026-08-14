#pragma once

#include "alloc/iallocator.h"

#include <cstddef>
#include <cstdint>

namespace ppl {

// EventBenchmark — 매 프레임 *작은 노드 다발 alloc/free* 워크로드.
//
// 시뮬 핫 루프와 별개로 매 프레임 N개의 작은 노드(Node)를 linked list 형태로
// alloc하고 즉시 모두 해제한다. 토글된 allocator 인스턴스에 따라 alloc/free
// 비용이 갈라진다.
//
//   - malloc:   매 호출마다 일반 힙 allocator를 통과한다. 보통 가장 비싼 경로.
//   - Arena:    bump pointer 증가만. Deallocate는 통계 기록 only, Reset이 일괄 회수.
//   - Pool:     free list head 갱신 두 줄. 매우 빠름.
//   - Slab:     크기 클래스로 라우팅 후 Pool과 동일.
//
// 이 워크로드는 ECS 시뮬 자체와 독립적이다. 시뮬 워크로드(layout/SIMD/branchless)는
// 그 자체 ms로 측정되고, EventBenchmark는 *별도 ms* 로 측정된다. HUD에서 두
// 측정값을 분리해 표시한다.
class EventBenchmark {
public:
    // 노드 한 개의 크기. main이 PoolAllocator 블록 크기를 잡을 때 사용.
    static constexpr std::size_t kNodeSize  = 24;     // 64-bit 기준 sizeof(Node)
    static constexpr std::size_t kNodeAlign = 8;

    void Init(std::size_t targetEventCount);

    // alloc이 nullptr이면 워크로드를 건너뛴다.
    // 매 프레임 호출. 끝에 alloc->Reset()을 호출해 Arena/Pool/Slab의 일괄 회수를 활용.
    void RunFrame(IAllocator* alloc);

    std::size_t      Target()                const { return target_; }
    std::uint64_t    LastFrameAllocCount()   const { return lastAllocCount_; }
    std::uint64_t    LastFrameAllocBytes()   const { return lastAllocBytes_; }
    float            LastFrameMs()           const { return lastFrameMs_; }

private:
    struct Node {
        std::uint32_t id;
        float         dist;
        Node*         next;
        std::uint64_t payload;
    };
    static_assert(sizeof(Node) <= kNodeSize, "Node size constant out of date");

    std::size_t   target_         = 0;
    std::uint64_t lastAllocCount_ = 0;
    std::uint64_t lastAllocBytes_ = 0;
    float         lastFrameMs_    = 0.0f;
};

}  // namespace ppl
