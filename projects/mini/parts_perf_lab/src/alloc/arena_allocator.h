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
// 장점: alloc 호출이 정렬 보정 + 포인터 증가 두 줄로 끝난다. free-list나 크기
//       클래스 라우팅이 없어 보통 가장 싼 경로다. 다만 "malloc 대비 항상 수십~수백 배"
//       같은 고정 배수는 환경·크기·단일/멀티스레드에 따라 달라지므로 가정하지 않는다.
//       이 lab은 같은 측정 구간에서의 상대 시간만 기록한다.
// 단점: 개별 free가 없다. 다만 "수명이 다르면 절대 못 쓴다"가 아니라, reset 전에
//       모든 사용이 끝나는 워크로드면 수명이 서로 달라도 쓸 수 있다. 문제는 개별
//       객체의 소멸 시점을 저장소 회수와 분리해 관리해야 한다는 점이다.
class ArenaAllocator final : public IAllocator {
public:
    explicit ArenaAllocator(std::size_t capacity);
    ~ArenaAllocator() override;

    // 복사 금지. 소멸자를 직접 선언하므로 이동 생성/대입도 암시적으로 생기지 않는다.
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    void* Allocate(std::size_t size, std::size_t alignment) override;
    void  Deallocate(void* ptr, std::size_t size) override;
    void  Reset() override;

    std::size_t Capacity() const { return capacity_; }
    std::size_t Used()     const { return used_; }

    // 확보한 단일 버퍼 전체가 backing이다. 점유 byte는 bump pointer가 지나간 양.
    std::size_t ReservedBacking() const override { return capacity_; }
    std::size_t SlotBytesLive()   const override { return used_; }

private:
    std::uint8_t* buffer_ = nullptr;
    std::size_t   capacity_ = 0;
    std::size_t   used_     = 0;
};

}  // namespace ppl
