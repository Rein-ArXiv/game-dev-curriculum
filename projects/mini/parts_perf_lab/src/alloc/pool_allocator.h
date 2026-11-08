#pragma once

#include "alloc/iallocator.h"

#include <cstdint>
#include <vector>

namespace ppl {

// Pool 할당자 — 고정 크기 블록을 미리 잡아 두고 free list로 재사용.
//
// 모든 블록의 크기와 정렬이 동일하다고 가정한다. 그래서 alloc/free가 free list의
// head 포인터 한 번 갱신으로 끝난다. 고정 슬롯이라 병합(compaction)이 필요 없다.
// 다만 "공간 낭비가 없다"는 뜻은 아니다: 24B 요청이 32B 슬롯을 차지하면 8B 내부
// 낭비이고, 예약한 backing과 실제 점유도 다르다.
//
// 사용 케이스: ECS 엔티티, 파티클, 발사체 같이 *수명이 짧고 같은 크기인* 객체 풀.
class PoolAllocator final : public IAllocator {
public:
    // 정책: blockSize/blockCount는 양수, blockAlign은 2의 거듭제곱이어야 한다.
    //   잘못된 입력은 invalid_argument, 정렬 올림·용량 곱셈 overflow는 length_error,
    //   실제 확보 실패는 bad_alloc으로 구분한다.
    PoolAllocator(std::size_t blockSize, std::size_t blockAlign,
                  std::size_t blockCount);
    ~PoolAllocator() override;

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    // size/alignment는 생성 시 지정한 값보다 작거나 같아야 한다. 크면 FailureCount 증가.
    // Allocate(0, ...)은 nullptr이고 카운터를 바꾸지 않는다.
    void* Allocate(std::size_t size, std::size_t alignment) override;
    void  Deallocate(void* ptr, std::size_t size) override;
    void  Reset() override;

    std::size_t BlockSize()  const { return blockSize_; }
    std::size_t BlockCount() const { return blockCount_; }
    std::size_t Free()       const { return freeBlocks_; }
    std::size_t InUse()      const { return blockCount_ - freeBlocks_; }

    // stride×capacity(예약 backing)과 stride×InUse(점유 byte). 요청 byte와 다르다.
    std::size_t ReservedBacking() const override { return stride_ * blockCount_; }
    std::size_t SlotBytesLive()   const override { return stride_ * InUse(); }

    // ptr이 *현재 할당된* 이 Pool의 슬롯 시작 주소인지 확인한다. SlabAllocator가
    // 반환 포인터의 소유 class를 찾을 때 사용한다. 주소 범위 판정은 서로 다른
    // allocation 포인터의 관계 비교를 피하려고 정수 주소로 한다(flat-address 전제).
    // 활성 상태는 슬롯 사용 비트로 확인하므로, 이미 반환된 포인터는 false다.
    bool Owns(const void* ptr) const;

private:
    // 각 블록은 사용 중이 아닐 때 다음 free 블록의 포인터를 저장한다.
    // 즉 블록 자신이 free list 노드 역할을 한다 (intrusive linked list).
    struct FreeNode {
        FreeNode* next;
    };

    // ptr이 이 Pool의 슬롯 시작 주소면 slot index, 아니면 blockCount_를 돌려준다.
    std::size_t SlotIndex(const void* ptr) const;

    std::uint8_t* buffer_     = nullptr;
    FreeNode*     freeHead_   = nullptr;
    std::size_t   blockSize_  = 0;
    std::size_t   blockAlign_ = 0;
    std::size_t   blockCount_ = 0;
    std::size_t   freeBlocks_ = 0;
    std::size_t   stride_     = 0;  // 정렬 패딩 포함된 블록 간격
    // 슬롯이 현재 할당되어 있는지. Owns의 활성 상태 판정과 이중 반환 방어에 쓴다.
    // free list만으로는 O(n) 순회가 필요하므로 별도 비트를 둔다.
    std::vector<std::uint8_t> slotUsed_;
};

}  // namespace ppl
