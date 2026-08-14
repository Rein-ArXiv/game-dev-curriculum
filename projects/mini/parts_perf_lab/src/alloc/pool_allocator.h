#pragma once

#include "alloc/iallocator.h"

#include <cstdint>

namespace ppl {

// Pool 할당자 — 고정 크기 블록을 미리 잡아 두고 free list로 재사용.
//
// 모든 블록의 크기와 정렬이 동일하다고 가정한다. 그래서 alloc/free가 free list의
// head 포인터 한 번 갱신으로 끝난다. 캐시 친화적이고 단편화가 거의 없다.
//
// 사용 케이스: ECS 엔티티, 파티클, 발사체 같이 *수명이 짧고 같은 크기인* 객체 풀.
class PoolAllocator final : public IAllocator {
public:
    PoolAllocator(std::size_t blockSize, std::size_t blockAlign,
                  std::size_t blockCount);
    ~PoolAllocator() override;

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    // size/alignment는 생성 시 지정한 값보다 작거나 같아야 한다. 크면 nullptr.
    void* Allocate(std::size_t size, std::size_t alignment) override;
    void  Deallocate(void* ptr, std::size_t size) override;
    void  Reset() override;

    std::size_t BlockSize()  const { return blockSize_; }
    std::size_t BlockCount() const { return blockCount_; }
    std::size_t Free()       const { return freeBlocks_; }
    std::size_t InUse()      const { return blockCount_ - freeBlocks_; }

    // ptr이 이 Pool의 슬롯 시작 주소인지 확인한다. SlabAllocator가
    // 반환 포인터의 소유 class를 찾을 때 사용한다.
    bool Owns(const void* ptr) const;

private:
    // 각 블록은 사용 중이 아닐 때 다음 free 블록의 포인터를 저장한다.
    // 즉 블록 자신이 free list 노드 역할을 한다 (intrusive linked list).
    struct FreeNode {
        FreeNode* next;
    };

    std::uint8_t* buffer_     = nullptr;
    FreeNode*     freeHead_   = nullptr;
    std::size_t   blockSize_  = 0;
    std::size_t   blockAlign_ = 0;
    std::size_t   blockCount_ = 0;
    std::size_t   freeBlocks_ = 0;
    std::size_t   stride_     = 0;  // 정렬 패딩 포함된 블록 간격
};

}  // namespace ppl
