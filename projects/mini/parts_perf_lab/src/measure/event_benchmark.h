#pragma once

#include "alloc/iallocator.h"
#include "measure/bench_status.h"

#include <cstddef>
#include <cstdint>

namespace ppl {

// EventBenchmark — 매 프레임 *작은 노드 다발 alloc/free* 워크로드.
//
// 시뮬 핫 루프와 별개로 매 프레임 N개의 작은 노드(Node)를 linked list 형태로
// alloc하고 모두 해제한 뒤 allocator->Reset()까지 호출한다.
//
// 이 프로토콜은 "alloc + 개별 반환 + 일괄 Reset"을 한 구간에서 재는 특정 워크로드다.
// 따라서 수치 자체는 그 프로토콜의 실제 비용이지만, allocator 일반 성능이나
// "가장 적합한 수명 전략"의 비교로 해석하면 안 된다. Pool/Slab은 이미 반환된 슬롯을
// Reset에서 다시 연결하고, Arena는 일괄 회수 전에 노드를 순회한다.
//
// 개별 수명만(Reset 없이) 또는 일괄 수명만 재는 분리 워크로드는 보충 구현 목표다.
//
//   - malloc:   일반 heap allocator를 거친다. 보통 가장 비싼 경로.
//   - Arena:    bump pointer 증가만. Deallocate는 통계 기록 only, Reset이 일괄 회수.
//   - Pool:     free list head 갱신 두 줄. 매우 빠름.
//   - Slab:     크기 클래스로 라우팅 후 Pool과 동일.
class EventBenchmark {
public:
    // 노드 한 개의 크기. main이 PoolAllocator 블록 크기를 잡을 때 사용.
    static constexpr std::size_t kNodeSize  = 24;     // 64-bit 기준 sizeof(Node)
    static constexpr std::size_t kNodeAlign = 8;

    // Init은 이전 결과도 함께 비운다. 직전 프레임의 alloc이 새 결과로 보이면 안 된다.
    void Init(std::size_t targetEventCount);

    // alloc이 nullptr이거나 target이 0이면 워크로드를 건너뛴다(Skipped).
    // 요청량을 다 채우지 못하면 InvalidCapacity — 그 표본은 시간 비교에서 제외한다.
    void RunFrame(IAllocator* alloc);

    std::size_t   Target()              const { return target_; }
    std::uint64_t LastFrameAllocCount() const { return lastAllocCount_; }
    std::uint64_t LastFrameAllocBytes() const { return lastAllocBytes_; }
    float         LastFrameMs()         const { return lastFrameMs_; }
    // Allocate를 실제로 호출한 수(실패 포함)와 실패 수.
    std::uint64_t AttemptedCount()      const { return attempted_; }
    std::uint64_t FailedCount()         const { return failed_; }
    BenchStatus   Status()              const { return status_; }

private:
    struct Node {
        std::uint32_t id;
        float         dist;
        Node*         next;
        std::uint64_t payload;
    };
    static_assert(sizeof(Node) <= kNodeSize, "Node size constant out of date");

    void ResetLast();

    std::size_t   target_         = 0;
    std::uint64_t lastAllocCount_ = 0;
    std::uint64_t lastAllocBytes_ = 0;
    std::uint64_t attempted_      = 0;
    std::uint64_t failed_         = 0;
    float         lastFrameMs_    = 0.0f;
    BenchStatus   status_         = BenchStatus::Skipped;
};

}  // namespace ppl
