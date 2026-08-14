#include "alloc/pool_allocator.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace ppl {

namespace {

void* AlignedAllocBytes(std::size_t bytes, std::size_t alignment) {
#if defined(_MSC_VER)
    return _aligned_malloc(bytes, alignment);
#else
    const std::size_t rounded = (bytes + alignment - 1) & ~(alignment - 1);
    return std::aligned_alloc(alignment, rounded);
#endif
}

void AlignedFreeBytes(void* p) {
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

}  // namespace

PoolAllocator::PoolAllocator(std::size_t blockSize, std::size_t blockAlign,
                              std::size_t blockCount)
    : blockSize_(blockSize),
      blockAlign_(blockAlign),
      blockCount_(blockCount),
      freeBlocks_(blockCount) {
    // free list 노드 크기보다 작은 블록은 free list를 만들 수 없다.
    blockSize_ = std::max(blockSize_, sizeof(FreeNode));
    // 블록 시작이 free list 노드로 reinterpret_cast 되므로, 정렬은 노드의
    // 자연 정렬 이상이어야 한다. 그렇지 않으면 미정렬 접근 UB가 된다.
    blockAlign_ = std::max(blockAlign_, alignof(FreeNode));
    // 정렬은 2의 거듭제곱이어야 한다.
    if (!IsValidAlignment(blockAlign_)) {
        blockAlign_ = alignof(FreeNode);
    }
    // stride는 정렬 단위로 올림한 블록 크기.
    stride_ = (blockSize_ + blockAlign_ - 1) & ~(blockAlign_ - 1);

    const std::size_t total = stride_ * blockCount_;
    buffer_ = static_cast<std::uint8_t*>(AlignedAllocBytes(total, blockAlign_));

    // 모든 블록을 free list에 연결한다. 시작 시 한 번만.
    Reset();
}

PoolAllocator::~PoolAllocator() {
    AlignedFreeBytes(buffer_);
}

void* PoolAllocator::Allocate(std::size_t size, std::size_t alignment) {
    // 호출자의 요구사항이 풀의 사양 안에 있는지 확인.
    if (!IsValidAlignment(alignment) ||
        size > blockSize_ || alignment > blockAlign_) {
        return nullptr;
    }
    if (!freeHead_) {
        return nullptr;  // 풀 고갈
    }

    // free list head를 pop. 분기·동기화 없이 두 줄로 끝나는 alloc 핵심.
    FreeNode* node = freeHead_;
    freeHead_ = node->next;
    --freeBlocks_;

    RecordAlloc(size);
    return node;
}

void PoolAllocator::Deallocate(void* ptr, std::size_t size) {
    if (!ptr) return;

    // 반환된 블록을 free list head에 push. 노드가 곧 블록 자신이다.
    FreeNode* node = static_cast<FreeNode*>(ptr);
    node->next = freeHead_;
    freeHead_ = node;
    ++freeBlocks_;

    RecordFree(size);
}

bool PoolAllocator::Owns(const void* ptr) const {
    if (!buffer_ || !ptr) return false;

    const auto* p = static_cast<const std::uint8_t*>(ptr);
    const std::uint8_t* begin = buffer_;
    const std::uint8_t* end   = buffer_ + stride_ * blockCount_;
    if (p < begin || p >= end) {
        return false;
    }

    const std::uintptr_t offset = static_cast<std::uintptr_t>(p - begin);
    return (offset % stride_) == 0;
}

void PoolAllocator::Reset() {
    // 모든 블록을 free list로 다시 연결.
    if (!buffer_ || blockCount_ == 0) {
        freeHead_ = nullptr;
        freeBlocks_ = 0;
        ResetLiveBytes();
        return;
    }

    freeHead_ = nullptr;
    for (std::size_t i = blockCount_; i > 0; --i) {
        std::uint8_t* slot = buffer_ + (i - 1) * stride_;
        FreeNode* node = reinterpret_cast<FreeNode*>(slot);
        node->next = freeHead_;
        freeHead_ = node;
    }
    freeBlocks_ = blockCount_;
    ResetLiveBytes();
}

}  // namespace ppl
