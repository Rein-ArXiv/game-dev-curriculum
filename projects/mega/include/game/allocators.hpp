// allocators — 이 게임의 할당을 **수명**으로 갈라 놓은 자리.
//
// 표준 컨테이너는 수명을 묻지 않는다. 프레임 하나만 사는 후보 배열도, 매치 내내
// 사는 맵 버퍼도 전부 같은 범용 할당자로 간다. 그 결과 "이 프레임에 몇 번
// 할당했는가"라는 질문에 답할 방법이 없고, 답할 수 없으면 줄일 수도 없다.
//
// 여기서는 둘로 가른다.
//
//   FrameScratch  프레임 스크래치. 프레임 시작에 Mark, 끝에 Rewind 한 번.
//                 개별 해제가 없다(memory/arena.h).
//   BlobStore     매치 수명이지만 크기가 제각각인 버퍼. 크기 클래스 freelist 로
//                 같은 클래스의 해제분을 재사용한다(memory/slab.h).
//
// 두 쪽 모두 기존 경로(operator new)를 A/B 의 한쪽 팔로 남긴다. 남겨 두는 이유는
// 정직성이다 — 새 할당자가 빠르다는 주장은 옛 할당자를 같은 워크로드에서 돌려
// 봐야만 성립한다.

#pragma once

#include "game/switches.hpp"
#include "memory/arena.h"
#include "memory/slab.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace mega {
namespace game {

// ── 프레임 수명 ────────────────────────────────────────────────────
// 한 프레임 안에서만 사는 배열(조준 후보·스플래시 후보·소멸한 발사체 id)을 담는다.
// arena 팔은 프레임당 전역 할당이 0이고, malloc 팔은 요청 하나당 한 번이다. 그
// 차이가 곧 이 클래스의 측정 대상이라, malloc 팔은 일부러 `::operator new` 를
// 직접 부른다 — std::malloc 을 쓰면 테스트의 전역 new 카운터에 잡히지 않아 두 팔의
// 차이가 관측되지 않는다.
class FrameScratch {
public:
    ~FrameScratch() { ReleaseBlocks(); }

    void Init(FrameAllocKind kind, size_t bytes) {
        ReleaseBlocks();
        kind_ = kind;
        overflows_ = 0;
        if (kind_ == FrameAllocKind::Arena) arena_.Init(bytes);
    }

    void Begin() {
        if (kind_ == FrameAllocKind::Arena) mark_ = arena_.Mark();
    }

    void End() {
        if (kind_ == FrameAllocKind::Arena) arena_.Rewind(mark_);
        ReleaseBlocks();
    }

    // snippet-begin: scratch-alloc
    template <class T>
    T* Alloc(size_t count) {
        static_assert(alignof(T) <= alignof(std::max_align_t), "과정렬 타입은 담지 않는다");
        if (count == 0) return nullptr;
        if (kind_ == FrameAllocKind::Arena) {
            if (T* p = arena_.AllocArray<T>(count)) return p;
            // 아레나가 모자라면 죽지 않고 힙으로 떨어지되 그 사실을 센다. 조용히
            // 넘어가면 "프레임당 할당 0" 이라는 주장이 거짓인 채로 통과한다.
            ++overflows_;
        }
        void* raw = ::operator new(sizeof(T) * count);
        blocks_.push_back(raw);
        return static_cast<T*>(raw);
    }
    // snippet-end: scratch-alloc

    size_t Overflows() const { return overflows_; }
    size_t ArenaUsed() const { return arena_.Used(); }
    size_t ArenaPeak() const { return arena_.Peak(); }
    size_t ArenaRemaining() const { return arena_.Remaining(); }
    FrameAllocKind Kind() const { return kind_; }

private:
    void ReleaseBlocks() {
        for (void* p : blocks_) ::operator delete(p);
        blocks_.clear();
    }

    Arena              arena_;
    Arena::Marker      mark_{0};
    std::vector<void*> blocks_;
    size_t             overflows_ = 0;
    FrameAllocKind     kind_ = FrameAllocKind::Malloc;
};

// ── 매치 수명 · 가변 크기 ──────────────────────────────────────────
// 맵 타일 배열 · 경로 배열처럼 크기가 맵에 따라 달라지고 매치 동안 사는 버퍼다.
// 맵을 바꾸거나 매치를 다시 시작할 때마다 같은 크기대의 버퍼가 해제·재생성되므로
// 크기 클래스 재사용이 실제로 돈다.
//
// 인스턴스가 프로세스에 하나인 이유: 이 버퍼를 들고 있는 DefenseMap 은 값으로
// 복사되고(월드 초기화가 맵 사본을 받는다) 그 사본의 수명이 원본과 다르다.
// 할당자를 맵 안에 넣으면 사본마다 다른 슬랩이 생겨 재사용이 사라진다.
class BlobStore {
public:
    static BlobStore& Instance() {
        static BlobStore store;
        return store;
    }

    void Configure(BlobAllocKind kind) { kind_ = kind; }
    BlobAllocKind Kind() const { return kind_; }

    void* Alloc(size_t bytes) {
        if (bytes == 0) return nullptr;
        ++live_;
        if (kind_ == BlobAllocKind::Slab) {
            if (void* p = slab_.Alloc(bytes)) return p;
            --live_;
            return nullptr;
        }
        return ::operator new(bytes);
    }

    // 어느 팔에서 잡았는지를 **호출자가 기억해 두고 돌려준다**. 스위치는 매치
    // 시작에 바뀔 수 있고, 그 순간 이미 살아 있는 버퍼는 옛 팔의 것이다. 지금
    // 모드로 반납하면 malloc 포인터를 슬랩에 밀어 넣는 즉시 힙이 깨진다.
    // snippet-begin: blob-free
    void Free(void* p, BlobAllocKind from) {
        if (!p) return;
        --live_;
        if (from == BlobAllocKind::Slab) slab_.Free(p);
        else ::operator delete(p);
    }
    // snippet-end: blob-free

    size_t LiveBlocks() const { return live_; }
    size_t PageCount() const { return slab_.PageCount(); }
    size_t LiveInClass(int c) const { return slab_.LiveInClass(c); }

private:
    BlobStore() = default;

    Slab          slab_;
    size_t        live_ = 0;
    BlobAllocKind kind_ = BlobAllocKind::Malloc;
};

// BlobStore 위의 고정 길이 배열. 값 의미론(복사 가능)이라 DefenseMap 이 그대로
// 값으로 오갈 수 있다. T 는 trivially copyable 만 담는다 — 슬랩은 소멸자를
// 부르지 않고 바이트만 돌려받기 때문이다.
template <class T>
class BlobArray {
    static_assert(std::is_trivially_copyable<T>::value,
                  "BlobArray 는 바이트 단위로만 오간다");

public:
    BlobArray() = default;
    explicit BlobArray(size_t count, const T& fill = T{}) { Assign(count, fill); }
    ~BlobArray() { Release(); }

    BlobArray(const BlobArray& o) { CopyFrom(o); }
    BlobArray& operator=(const BlobArray& o) {
        if (this != &o) { Release(); CopyFrom(o); }
        return *this;
    }
    BlobArray(BlobArray&& o) noexcept : data_(o.data_), size_(o.size_), from_(o.from_) {
        o.data_ = nullptr;
        o.size_ = 0;
    }
    BlobArray& operator=(BlobArray&& o) noexcept {
        if (this != &o) {
            Release();
            data_ = o.data_; size_ = o.size_; from_ = o.from_;
            o.data_ = nullptr; o.size_ = 0;
        }
        return *this;
    }

    // snippet-begin: blob-assign
    void Assign(size_t count, const T& fill) {
        Release();
        if (count == 0) return;
        from_ = BlobStore::Instance().Kind();
        data_ = static_cast<T*>(BlobStore::Instance().Alloc(sizeof(T) * count));
        if (!data_) return;
        size_ = count;
        for (size_t i = 0; i < size_; ++i) data_[i] = fill;
    }
    // snippet-end: blob-assign

    void AssignFrom(const T* source, size_t count) {
        Release();
        if (count == 0 || !source) return;
        from_ = BlobStore::Instance().Kind();
        data_ = static_cast<T*>(BlobStore::Instance().Alloc(sizeof(T) * count));
        if (!data_) return;
        size_ = count;
        for (size_t i = 0; i < size_; ++i) data_[i] = source[i];
    }

    void Clear() { Release(); }

    size_t Size() const { return size_; }
    bool   Empty() const { return size_ == 0; }
    T*       Data()       { return data_; }
    const T* Data() const { return data_; }
    T&       operator[](size_t i)       { return data_[i]; }
    const T& operator[](size_t i) const { return data_[i]; }
    const T* begin() const { return data_; }
    const T* end()   const { return data_ + size_; }

private:
    void Release() {
        if (data_) BlobStore::Instance().Free(data_, from_);
        data_ = nullptr;
        size_ = 0;
    }
    void CopyFrom(const BlobArray& o) { AssignFrom(o.data_, o.size_); }

    T*            data_ = nullptr;
    size_t        size_ = 0;
    BlobAllocKind from_ = BlobAllocKind::Malloc;   // 잡은 팔. 반납은 이쪽으로 간다
};

}  // namespace game
}  // namespace mega
