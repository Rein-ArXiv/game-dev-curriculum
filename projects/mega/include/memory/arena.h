// Arena — 선형 bump 할당자. 프레임 단위 임시 할당의 기본.
//
// 정책: 프레임 시작에 Reset() 으로 통째로 되감고, 개별 free 는 없다. 임시
// 객체 비중이 큰 게임 워크로드에서 malloc 대비 일관되게 빠른 이유는, 할당이
// "포인터를 size 만큼 전진"이 전부이고 해제가 "포인터를 처음으로" 한 번이기
// 때문이다. 단편화가 없고 메타데이터도 없다.
//
// Scope 로 중첩된 임시 영역의 부분 되감기를 지원한다(스택처럼).

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace mega {

class Arena {
public:
    Arena() = default;
    explicit Arena(size_t capacity) { Init(capacity); }
    ~Arena() { std::free(base_); }

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&& o) noexcept { MoveFrom(o); }
    Arena& operator=(Arena&& o) noexcept {
        if (this != &o) { std::free(base_); MoveFrom(o); }
        return *this;
    }

    void Init(size_t capacity) {
        std::free(base_);
        base_     = static_cast<uint8_t*>(std::malloc(capacity));
        capacity_ = base_ ? capacity : 0;
        offset_   = 0;
        peak_     = 0;
    }

    // size 바이트를 align 정렬로 할당. 용량 초과면 nullptr.
    void* Alloc(size_t size, size_t align = alignof(std::max_align_t)) {
        size_t aligned = AlignUp(offset_, align);
        if (aligned + size > capacity_) return nullptr;   // 용량 초과 — 호출자가 처리
        void* p = base_ + aligned;
        offset_ = aligned + size;
        if (offset_ > peak_) peak_ = offset_;
        return p;
    }

    template <class T, class... Args>
    T* New(Args&&... args) {
        void* p = Alloc(sizeof(T), alignof(T));
        return p ? new (p) T(static_cast<Args&&>(args)...) : nullptr;
        // 주의: Arena 객체는 소멸자가 호출되지 않는다(통째 Reset). trivial
        // destructor 타입에만 쓴다.
    }

    template <class T>
    T* AllocArray(size_t count) {
        return static_cast<T*>(Alloc(sizeof(T) * count, alignof(T)));
    }

    // 프레임 끝: 전체 되감기. 메모리는 유지(재사용).
    void Reset() { offset_ = 0; }

    // 중첩 스코프: 현재 위치를 저장했다가 되감는다.
    struct Marker { size_t offset; };
    Marker Mark() const { return {offset_}; }
    void   Rewind(Marker m) { offset_ = m.offset; }

    struct Scope {
        Arena&  arena;
        Marker  marker;
        explicit Scope(Arena& a) : arena(a), marker(a.Mark()) {}
        ~Scope() { arena.Rewind(marker); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };

    size_t Used()     const { return offset_; }
    size_t Capacity() const { return capacity_; }
    size_t Peak()     const { return peak_; }
    size_t Remaining() const { return capacity_ - offset_; }

private:
    static size_t AlignUp(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }
    void MoveFrom(Arena& o) {
        base_ = o.base_; capacity_ = o.capacity_; offset_ = o.offset_; peak_ = o.peak_;
        o.base_ = nullptr; o.capacity_ = o.offset_ = o.peak_ = 0;
    }

    uint8_t* base_     = nullptr;
    size_t   capacity_ = 0;
    size_t   offset_   = 0;
    size_t   peak_     = 0;
};

}  // namespace mega
