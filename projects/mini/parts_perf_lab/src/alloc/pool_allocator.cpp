#include "alloc/pool_allocator.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace ppl {

namespace {

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
    // 계약: 잘못된 입력을 조용히 보정하지 않고 명시적으로 거부한다.
    //   - size/count는 양수여야 한다.
    //   - alignment는 2의 거듭제곱이어야 한다.
    if (blockSize == 0 || blockCount == 0 || !IsValidAlignment(blockAlign)) {
        throw std::invalid_argument(
            "Pool: positive size/count and power-of-two alignment required");
    }

    // free list 노드 크기보다 작은 블록은 free list를 만들 수 없다.
    blockSize_ = std::max(blockSize, sizeof(FreeNode));
    // 블록 시작이 free list 노드로 reinterpret_cast 되므로, 정렬은 노드의
    // 자연 정렬 이상이어야 한다. 그렇지 않으면 미정렬 접근 UB가 된다.
    blockAlign_ = std::max(blockAlign, alignof(FreeNode));

    // 정렬 올림과 곱셈은 overflow를 먼저 검사한다. 검사 없이 계산하면 unsigned
    // wrap으로 stride가 작아지고, 원래 blockCount로 Reset을 돌릴 때 heap을 넘어
    // 쓴다(실측 ASan heap-buffer-overflow).
    const std::size_t limit   = std::numeric_limits<std::size_t>::max();
    const std::size_t padding = blockAlign_ - 1;
    if (blockSize_ > limit - padding) {
        throw std::length_error("Pool: rounded block size overflow");
    }
    stride_ = (blockSize_ + padding) & ~padding;

    if (blockCount_ > limit / stride_) {
        throw std::length_error("Pool: capacity overflow");
    }
    const std::size_t total = stride_ * blockCount_;

    // total은 이미 blockAlign_의 배수다. 다시 올림하면 aligned_alloc 전제가 깨진다.
#if defined(_MSC_VER)
    buffer_ = static_cast<std::uint8_t*>(_aligned_malloc(total, blockAlign_));
#else
    buffer_ = static_cast<std::uint8_t*>(std::aligned_alloc(blockAlign_, total));
#endif
    if (!buffer_) {
        throw std::bad_alloc();
    }

    slotUsed_.assign(blockCount_, 0);

    // 모든 블록을 free list에 연결한다.
    Reset();
}

PoolAllocator::~PoolAllocator() {
    AlignedFreeBytes(buffer_);
}

void* PoolAllocator::Allocate(std::size_t size, std::size_t alignment) {
    // 공통 계약: 0바이트 요청은 no-op(nullptr)이고 어떤 카운터도 바꾸지 않는다.
    if (size == 0) return nullptr;
    // 호출자의 요구사항이 풀의 사양 안에 있는지 확인.
    if (!IsValidAlignment(alignment) ||
        size > blockSize_ || alignment > blockAlign_) {
        RecordFailure();
        return nullptr;
    }
    if (!freeHead_) {
        RecordFailure();  // 풀 고갈
        return nullptr;
    }

    // free list head를 pop. 분기·동기화 없이 두 줄로 끝나는 alloc 핵심.
    FreeNode* node = freeHead_;
    freeHead_ = node->next;
    --freeBlocks_;

    const std::size_t idx = SlotIndex(node);
    if (idx < slotUsed_.size()) slotUsed_[idx] = 1;

    RecordAlloc(size);
    return node;
}

void PoolAllocator::Deallocate(void* ptr, std::size_t size) {
    if (!ptr) return;

    // 이 Pool의 슬롯이고 아직 반환되지 않았을 때만 free list에 push한다.
    // 이미 반환된 포인터를 다시 넣으면 free list가 자기 자신을 가리켜
    // 같은 슬롯이 두 번 나오는 고전적 이중 반환 손상이 된다.
    const std::size_t idx = SlotIndex(ptr);
    if (idx < slotUsed_.size()) {
        if (!slotUsed_[idx]) return;
        slotUsed_[idx] = 0;
    }

    // 반환된 블록을 free list head에 push. 노드가 곧 블록 자신이다.
    FreeNode* node = static_cast<FreeNode*>(ptr);
    node->next = freeHead_;
    freeHead_ = node;
    ++freeBlocks_;

    RecordFree(size);
}

bool PoolAllocator::Owns(const void* ptr) const {
    if (!buffer_ || !ptr) return false;
    const std::size_t idx = SlotIndex(ptr);
    return idx < slotUsed_.size() && slotUsed_[idx] != 0;
}

std::size_t PoolAllocator::SlotIndex(const void* ptr) const {
    if (!buffer_ || !ptr) return blockCount_;
    const std::uintptr_t p     = reinterpret_cast<std::uintptr_t>(ptr);
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(buffer_);
    const std::uintptr_t end   = begin + stride_ * blockCount_;
    // 서로 다른 allocation의 포인터일 수 있으므로 포인터 관계 비교 대신
    // 정수 주소로 범위를 판정한다(flat-address 전제).
    if (p < begin || p >= end) return blockCount_;
    const std::uintptr_t offset = p - begin;
    if (offset % stride_ != 0) return blockCount_;
    return static_cast<std::size_t>(offset / stride_);
}

void PoolAllocator::Reset() {
    // 모든 블록을 free list로 다시 연결.
    if (!buffer_ || blockCount_ == 0) {
        freeHead_ = nullptr;
        freeBlocks_ = 0;
        slotUsed_.clear();
        ResetLiveBytes();
        return;
    }

    for (auto& bit : slotUsed_) bit = 0;
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
