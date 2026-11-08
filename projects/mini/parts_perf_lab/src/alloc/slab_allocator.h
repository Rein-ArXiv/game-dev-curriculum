#pragma once

#include "alloc/iallocator.h"
#include "alloc/pool_allocator.h"

#include <array>
#include <memory>

namespace ppl {

// Slab 할당자 — 여러 크기 클래스를 가진 PoolAllocator의 묶음.
//
// 각 크기 클래스는 2의 제곱수로 잡고, 요청 크기를 가장 작은 적합 클래스로 라운드 업한다.
// Linux 커널의 SLUB 할당자와 같은 아이디어를 단순화한 형태다.
//
// 장점: 다양한 크기를 다룰 수 있으면서 각 클래스 안에서는 Pool과 같은 빠른 alloc/free.
// 단점: 클래스 사이의 크기 차이가 크면 내부 단편화. (예: 17바이트 요청이 32바이트 슬롯 차지)
class SlabAllocator final : public IAllocator {
public:
    // 크기 클래스: 16, 32, 64, 128, 256, 512, 1024, 2048 byte.
    static constexpr std::size_t kClassCount = 8;
    static constexpr std::size_t kSmallestClass = 16;

    // 각 클래스가 보유할 블록 수.
    explicit SlabAllocator(std::size_t blocksPerClass);
    ~SlabAllocator() override = default;

    SlabAllocator(const SlabAllocator&) = delete;
    SlabAllocator& operator=(const SlabAllocator&) = delete;

    void* Allocate(std::size_t size, std::size_t alignment) override;
    void  Deallocate(void* ptr, std::size_t size) override;
    void  Reset() override;

    // 각 클래스의 사용 현황. HUD는 IAllocator 공통 통계만 표시하므로 호출하지 않고,
    // 코어 테스트가 크기 클래스 라우팅을 검증할 때 쓴다.
    std::size_t ClassBlockSize(std::size_t classIdx) const;
    std::size_t ClassInUse(std::size_t classIdx) const;
    std::size_t ClassCapacity(std::size_t classIdx) const;

    // 모든 class pool의 backing/점유 byte 합. RSS가 아니고, class별 여유 불균형은
    // 이 합만으로 보이지 않는다.
    std::size_t ReservedBacking() const override;
    std::size_t SlotBytesLive()   const override;

private:
    // 요청 크기에 맞는 클래스 인덱스. size > 가장 큰 클래스면 kClassCount 반환.
    static std::size_t SizeToClass(std::size_t size);

    std::array<std::unique_ptr<PoolAllocator>, kClassCount> pools_;
};

}  // namespace ppl
