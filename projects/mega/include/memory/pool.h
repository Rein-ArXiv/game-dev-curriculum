// Pool — 고정 크기 블록 할당자. 같은 크기 객체(엔티티·발사체)의 풀링.
//
// 정책: 같은 크기 블록 N 개를 한 번에 확보하고, 자유 블록을 intrusive
// freelist 로 잇는다. Acquire 는 freelist head pop, Release 는 push — 둘 다
// O(1) 이고 단편화가 없다. 빈 블록의 *내부 공간을 next 포인터로 재활용* 하므로
// 메타데이터 오버헤드도 0이다.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace mega {

class Pool {
public:
    Pool() = default;
    Pool(size_t blockSize, size_t blockCount) { Init(blockSize, blockCount); }
    ~Pool() { std::free(base_); }

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    void Init(size_t blockSize, size_t blockCount) {
        std::free(base_);
        // 블록은 최소 포인터 크기여야 freelist next 를 담는다.
        blockSize_  = blockSize < sizeof(void*) ? sizeof(void*) : blockSize;
        blockCount_ = blockCount;
        base_       = static_cast<uint8_t*>(std::malloc(blockSize_ * blockCount_));
        live_       = 0;
        peak_       = 0;
        BuildFreeList();
    }

    void* Acquire() {
        if (!free_) return nullptr;             // 풀 소진 — 호출자가 처리
        void* p = free_;
        free_ = *reinterpret_cast<void**>(free_);  // next 로 이동
        ++live_;
        if (live_ > peak_) peak_ = live_;
        return p;
    }

    void Release(void* p) {
        if (!p) return;
        *reinterpret_cast<void**>(p) = free_;   // 블록 내부에 next 저장
        free_ = p;
        --live_;
    }

    template <class T, class... Args>
    T* New(Args&&... args) {
        void* p = Acquire();
        return p ? new (p) T(static_cast<Args&&>(args)...) : nullptr;
    }
    template <class T>
    void Delete(T* obj) {
        if (!obj) return;
        obj->~T();
        Release(obj);
    }

    void Reset() { BuildFreeList(); live_ = 0; }   // 전체 회수(소멸자 호출 안 함)

    size_t BlockSize()  const { return blockSize_; }
    size_t BlockCount() const { return blockCount_; }
    size_t Live()       const { return live_; }
    size_t Peak()       const { return peak_; }
    bool   Owns(const void* p) const {
        return p >= base_ && p < base_ + blockSize_ * blockCount_;
    }

private:
    void BuildFreeList() {
        free_ = nullptr;
        if (!base_) return;
        // 역순으로 이어 0번 블록이 head 가 되도록(지역성).
        for (size_t i = blockCount_; i-- > 0;) {
            void* block = base_ + i * blockSize_;
            *reinterpret_cast<void**>(block) = free_;
            free_ = block;
        }
    }

    uint8_t* base_       = nullptr;
    void*    free_       = nullptr;
    size_t   blockSize_  = 0;
    size_t   blockCount_ = 0;
    size_t   live_       = 0;
    size_t   peak_       = 0;
};

}  // namespace mega
