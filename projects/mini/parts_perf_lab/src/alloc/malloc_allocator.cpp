#include "alloc/malloc_allocator.h"

#include <cstdlib>
#include <limits>

#if defined(_MSC_VER)
#include <malloc.h>  // _aligned_malloc, _aligned_free
#endif

namespace ppl {

void* MallocAllocator::Allocate(std::size_t size, std::size_t alignment) {
    // 공통 계약: 0바이트 요청은 no-op(nullptr)이고 어떤 카운터도 바꾸지 않는다.
    if (size == 0) return nullptr;
    // alignment는 0이 아니고 2의 거듭제곱이어야 한다.
    // 잘못된 값이면 nullptr을 돌려준다 (assert 대신 nullptr이 측정 흐름에 안전).
    if (!IsValidAlignment(alignment) ||
        size > std::numeric_limits<std::size_t>::max() - (alignment - 1)) {
        RecordFailure();
        return nullptr;
    }

    void* ptr = nullptr;

#if defined(_MSC_VER)
    // MSVC는 _aligned_malloc / _aligned_free 쌍을 사용해야 한다.
    // 일반 free에 _aligned_malloc 결과를 넘기면 UB.
    ptr = _aligned_malloc(size, alignment);
#else
    // C11 aligned_alloc은 size가 alignment의 배수여야 한다.
    // 호출 측 부담을 줄이려고 size를 올림한다.
    const std::size_t aligned = (size + alignment - 1) & ~(alignment - 1);
    ptr = std::aligned_alloc(alignment, aligned);
#endif

    if (ptr) {
        RecordAlloc(size);
    } else {
        RecordFailure();  // 실제 확보 실패
    }
    return ptr;
}

void MallocAllocator::Deallocate(void* ptr, std::size_t size) {
    if (!ptr) return;

#if defined(_MSC_VER)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif

    RecordFree(size);
}

}  // namespace ppl
