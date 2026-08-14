#include "alloc/arena_allocator.h"

#include <cstdlib>
#include <cstring>

namespace ppl {

ArenaAllocator::ArenaAllocator(std::size_t capacity)
    : capacity_(capacity) {
    // 백킹 버퍼는 시스템 alignof(max_align_t)에 정렬된 단일 블록.
    // 개별 alloc은 자기 정렬 요구사항을 bump pointer 위에서 직접 보정한다.
    buffer_ = static_cast<std::uint8_t*>(std::malloc(capacity_));
}

ArenaAllocator::~ArenaAllocator() {
    std::free(buffer_);
}

void* ArenaAllocator::Allocate(std::size_t size, std::size_t alignment) {
    if (!buffer_) return nullptr;
    if (!IsValidAlignment(alignment)) return nullptr;

    // 현재 head 주소를 정렬에 맞춰 올림한다.
    const std::uintptr_t base    = reinterpret_cast<std::uintptr_t>(buffer_);
    const std::uintptr_t current = base + used_;
    const std::uintptr_t aligned = AlignUp(current, alignment);
    const std::size_t    padding = static_cast<std::size_t>(aligned - current);

    // 버퍼 한도 검사. 한 번 넘기면 nullptr 반환 (assert는 측정 흐름을 깨므로 피한다).
    if (used_ + padding + size > capacity_) {
        return nullptr;
    }

    used_ += padding + size;
    RecordAlloc(size);
    return reinterpret_cast<void*>(aligned);
}

void ArenaAllocator::Deallocate(void* /*ptr*/, std::size_t size) {
    // Arena의 핵심: 개별 free는 무의미. 통계만 기록하고 메모리는 그대로 둔다.
    // Reset이 일괄 회수한다.
    RecordFree(size);
}

void ArenaAllocator::Reset() {
    used_ = 0;
    // 일괄 회수이므로 LiveBytes 통계도 0으로 되돌린다.
    ResetLiveBytes();
}

}  // namespace ppl
