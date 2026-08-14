// resource — 핸들 + 레퍼런스 카운트 리소스 매니저.
//
// 포인터 대신 핸들(인덱스+세대)을 넘긴다. 리소스가 해제·재사용되면 세대가 올라가
// 옛 핸들의 Get 이 안전하게 null 을 반환한다(dangling 방지). 레퍼런스 카운트가 0이
// 되면 슬롯을 해제하고 세대를 올린다(세대 핸들 패턴의 도메인 적용).

#pragma once

#include <cstdint>
#include <vector>

namespace mega {
namespace engine {

struct Handle {
    uint32_t index      = 0xFFFFFFFFu;
    uint32_t generation = 0;
    bool Valid() const { return index != 0xFFFFFFFFu; }
};

template <class T>
class ResourcePool {
public:
    // 리소스 적재. 레퍼런스 카운트 1 로 시작.
    Handle Load(const T& value) {
        uint32_t idx;
        if (!freeList_.empty()) { idx = freeList_.back(); freeList_.pop_back(); }
        else { idx = (uint32_t)slots_.size(); slots_.push_back({}); }
        Slot& s = slots_[idx];
        s.value = value;
        s.refcount = 1;
        s.alive = true;
        return {idx, s.generation};
    }

    bool IsValid(Handle h) const {
        return h.Valid() && h.index < slots_.size() && slots_[h.index].alive && slots_[h.index].generation == h.generation;
    }

    T* Get(Handle h) {
        if (!IsValid(h)) return nullptr;
        return &slots_[h.index].value;
    }

    void AddRef(Handle h) { if (IsValid(h)) ++slots_[h.index].refcount; }

    // 레퍼런스 1 감소. 0 이면 해제(세대++).
    void Release(Handle h) {
        if (!IsValid(h)) return;
        Slot& s = slots_[h.index];
        if (--s.refcount == 0) {
            s.alive = false;
            ++s.generation;          // 옛 핸들 무효화
            s.value = T{};
            freeList_.push_back(h.index);
        }
    }

    int RefCount(Handle h) const { return IsValid(h) ? (int)slots_[h.index].refcount : 0; }
    size_t AliveCount() const {
        size_t n = 0; for (const auto& s : slots_) if (s.alive) ++n; return n;
    }

private:
    struct Slot { T value{}; uint32_t generation = 0; int refcount = 0; bool alive = false; };
    std::vector<Slot>     slots_;
    std::vector<uint32_t> freeList_;
};

}  // namespace engine
}  // namespace mega
