#include "alloc/slab_allocator.h"

#include <algorithm>

namespace ppl {

namespace {

// 크기 클래스의 블록 크기 표.
constexpr std::size_t kClassSize[SlabAllocator::kClassCount] = {
    16, 32, 64, 128, 256, 512, 1024, 2048,
};

}  // namespace

SlabAllocator::SlabAllocator(std::size_t blocksPerClass) {
    // 각 클래스마다 PoolAllocator를 하나씩.
    // 정렬은 클래스 크기 자체로 둔다(16바이트 클래스는 16바이트 정렬).
    for (std::size_t i = 0; i < kClassCount; ++i) {
        pools_[i] = std::make_unique<PoolAllocator>(
            kClassSize[i], kClassSize[i], blocksPerClass);
    }
}

std::size_t SlabAllocator::SizeToClass(std::size_t size) {
    for (std::size_t i = 0; i < kClassCount; ++i) {
        if (size <= kClassSize[i]) return i;
    }
    return kClassCount;  // 너무 큰 요청
}

void* SlabAllocator::Allocate(std::size_t size, std::size_t alignment) {
    if (!IsValidAlignment(alignment)) {
        return nullptr;
    }
    // size 또는 alignment 중 큰 쪽을 기준으로 클래스를 고른다.
    // 예: Allocate(8, 64)는 64바이트 정렬을 만족하는 64바이트 클래스로 라우팅.
    const std::size_t need = std::max(size, alignment);
    const std::size_t cls  = SizeToClass(need);
    if (cls == kClassCount) {
        return nullptr;  // 클래스 한도 초과
    }

    void* ptr = pools_[cls]->Allocate(size, alignment);
    if (ptr) {
        RecordAlloc(size);
    }
    return ptr;
}

void SlabAllocator::Deallocate(void* ptr, std::size_t size) {
    if (!ptr) return;
    // Allocate가 max(size, alignment)로 클래스를 고르므로 size만으로는
    // 반환 class를 복원할 수 없다. 포인터가 속한 Pool을 찾아 정확히 반환한다.
    for (auto& pool : pools_) {
        if (pool->Owns(ptr)) {
            pool->Deallocate(ptr, size);
            RecordFree(size);
            return;
        }
    }
}

void SlabAllocator::Reset() {
    for (auto& pool : pools_) {
        pool->Reset();
    }
    ResetLiveBytes();
}

std::size_t SlabAllocator::ClassBlockSize(std::size_t classIdx) const {
    return classIdx < kClassCount ? kClassSize[classIdx] : 0;
}

std::size_t SlabAllocator::ClassInUse(std::size_t classIdx) const {
    return classIdx < kClassCount ? pools_[classIdx]->InUse() : 0;
}

std::size_t SlabAllocator::ClassCapacity(std::size_t classIdx) const {
    return classIdx < kClassCount ? pools_[classIdx]->BlockCount() : 0;
}

}  // namespace ppl
