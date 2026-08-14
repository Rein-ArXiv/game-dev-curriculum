// Slab — 크기 클래스별 freelist 할당자. 가변 크기 소·중 객체용.
//
// 정책: 요청 크기를 가장 가까운 *크기 클래스* 로 올림해, 그 클래스의 freelist
// 에서 청크를 꺼낸다. freelist 가 비면 슬랩 페이지(예: 64KB)를 malloc 해 그
// 클래스 청크들로 잘게 나눠 채운다. 클래스 최대치를 넘는 큰 요청만 시스템
// 할당자로 떨어뜨린다(large fallback). 각 청크 앞에 16바이트 헤더를 두어
// 클래스 인덱스를 기록 → Free 시 size 없이도 올바른 freelist 로 돌려보낸다.
//
// 단편화 특성: 같은 클래스 객체가 같은 페이지에 모이고, 해제된 청크가 같은
// 클래스에서 재사용되므로 외부 단편화가 작다. 내부 단편화는 클래스 간격만큼.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace mega {

class Slab {
public:
    Slab() { freeLists_.assign(kNumClasses, nullptr); }
    ~Slab() { for (void* p : pages_) std::free(p); }

    Slab(const Slab&) = delete;
    Slab& operator=(const Slab&) = delete;

    void* Alloc(size_t size) {
        int c = ClassFor(size);
        if (c < 0) return AllocLarge(size);          // 클래스 초과 → malloc

        if (!freeLists_[c]) CarvePage(c);            // 클래스 소진 → 페이지 한 장
        void* chunk = freeLists_[c];
        if (!chunk) return nullptr;                  // OOM
        freeLists_[c] = *reinterpret_cast<void**>(chunk);

        Header* h = reinterpret_cast<Header*>(chunk);
        h->classIdx = c;
        ++liveCount_[c];
        return static_cast<uint8_t*>(chunk) + kHeader;
    }

    void Free(void* userPtr) {
        if (!userPtr) return;
        void*   chunk = static_cast<uint8_t*>(userPtr) - kHeader;
        Header* h = reinterpret_cast<Header*>(chunk);
        if (h->classIdx == kLarge) { std::free(chunk); return; }
        int c = h->classIdx;
        *reinterpret_cast<void**>(chunk) = freeLists_[c];   // freelist push
        freeLists_[c] = chunk;
        --liveCount_[c];
    }

    template <class T, class... Args>
    T* New(Args&&... args) {
        void* p = Alloc(sizeof(T));
        return p ? new (p) T(static_cast<Args&&>(args)...) : nullptr;
    }
    template <class T>
    void Delete(T* obj) { if (obj) { obj->~T(); Free(obj); } }

    // 통계: 클래스별 활성 청크 수.
    size_t LiveInClass(int c) const { return (c >= 0 && c < kNumClasses) ? liveCount_[c] : 0; }
    size_t PageCount()        const { return pages_.size(); }
    static int   NumClasses()      { return kNumClasses; }
    static size_t ClassSize(int c) { return kClasses[c]; }

private:
    static constexpr size_t kHeader   = 16;          // max_align 정렬 보장
    static constexpr size_t kPageSize = 64 * 1024;
    static constexpr int    kLarge    = -1;

    // parts 시리즈 freelist 분포에서 가져온 클래스(과한 내부 단편화 회피).
    static constexpr size_t kClasses[] = {
        16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048
    };
    static constexpr int kNumClasses = (int)(sizeof(kClasses) / sizeof(kClasses[0]));

    struct Header { int32_t classIdx; uint32_t pad[3]; };

    static int ClassFor(size_t size) {
        for (int c = 0; c < kNumClasses; ++c)
            if (size <= kClasses[c]) return c;
        return kLarge;
    }

    void CarvePage(int c) {
        size_t chunkSize = kHeader + kClasses[c];
        uint8_t* page = static_cast<uint8_t*>(std::malloc(kPageSize));
        if (!page) return;
        pages_.push_back(page);
        size_t n = kPageSize / chunkSize;
        for (size_t i = n; i-- > 0;) {               // 역순 → 0번이 head(지역성)
            void* chunk = page + i * chunkSize;
            *reinterpret_cast<void**>(chunk) = freeLists_[c];
            freeLists_[c] = chunk;
        }
    }

    void* AllocLarge(size_t size) {
        void* block = std::malloc(kHeader + size);
        if (!block) return nullptr;
        reinterpret_cast<Header*>(block)->classIdx = kLarge;
        return static_cast<uint8_t*>(block) + kHeader;
    }

    std::vector<void*> freeLists_;       // 클래스별 freelist head
    std::vector<void*> pages_;           // 회수용 페이지 목록
    size_t liveCount_[kNumClasses] = {};
};

}  // namespace mega
