#pragma once

#include "alloc/iallocator.h"

namespace ppl {

// 표준 malloc/free 래퍼. 비교의 baseline.
//
// 정렬은 C++17의 aligned_alloc / _aligned_malloc(Windows)을 사용한다.
// 각 할당이 시스템 호출 + 동기화 비용을 가진다 (특히 멀티스레드에서).
class MallocAllocator final : public IAllocator {
public:
    void* Allocate(std::size_t size, std::size_t alignment) override;
    void  Deallocate(void* ptr, std::size_t size) override;
};

}  // namespace ppl
