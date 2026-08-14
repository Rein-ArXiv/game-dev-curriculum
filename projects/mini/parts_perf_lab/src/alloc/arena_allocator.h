#pragma once

#include "alloc/iallocator.h"

#include <cstdint>

namespace ppl {

// Arena (또는 Linear/Bump) 할당자.
//
// 큰 메모리 블록을 한 번 잡아 두고, Allocate 호출은 *bump pointer* 만 증가시킨다.
// Deallocate는 no-op이다. 한 번에 모두 회수하려면 Reset.
//
// 매 프레임 시작에 Reset을 호출하면 *Frame Arena* 패턴이 된다. 매 프레임 발생하는
// 임시 데이터(충돌 후보 페어, draw call 큐 등)에 적합하다.
//
// 장점: alloc 호출이 정렬 보정 + 포인터 증가 두 줄로 끝난다. malloc 대비
//       수십~수백 배 빠르다.
// 단점: 개별 free 불가능. 객체 수명이 다르면 사용 불가.
class ArenaAllocator final : public IAllocator {
public:
    explicit ArenaAllocator(std::size_t capacity);
    ~ArenaAllocator() override;

    // 복사 금지, 이동만 허용.
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    void* Allocate(std::size_t size, std::size_t alignment) override;
    void  Deallocate(void* ptr, std::size_t size) override;
    void  Reset() override;

    std::size_t Capacity() const { return capacity_; }
    std::size_t Used()     const { return used_; }

private:
    std::uint8_t* buffer_ = nullptr;
    std::size_t   capacity_ = 0;
    std::size_t   used_     = 0;
};

}  // namespace ppl
