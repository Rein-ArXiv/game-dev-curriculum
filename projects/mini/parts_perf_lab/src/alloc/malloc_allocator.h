#pragma once

#include "alloc/iallocator.h"

namespace ppl {

// 표준 aligned_alloc/free 래퍼. 비교의 baseline.
//
// 정렬은 C++17의 aligned_alloc / _aligned_malloc(Windows)을 사용한다. 즉 raw
// malloc이 아니라 정렬 wrapper다.
//
// 모든 할당이 매번 시스템 호출 + 전역 동기화를 지불한다고 단정하면 틀리다.
// glibc의 tcache/small-bin fast path 같은 사용자 공간 재사용 경로가 있고, 비용은
// 크기·스레드 수·구현 버전에 따라 달라진다. 이 lab은 "일반 heap allocator를 거치는
// 경로"라는 baseline만 재현하며, 특정 libc의 내부 단계별 시간은 측정하지 않는다.
class MallocAllocator final : public IAllocator {
public:
    void* Allocate(std::size_t size, std::size_t alignment) override;
    void  Deallocate(void* ptr, std::size_t size) override;

    // 내부 낭비를 알 수 없으므로 점유 byte는 요청 byte로 근사한다. backing 미상.
    std::size_t SlotBytesLive() const override {
        return static_cast<std::size_t>(LiveBytes());
    }
};

}  // namespace ppl
