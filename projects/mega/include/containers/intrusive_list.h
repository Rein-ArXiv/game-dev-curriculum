// IntrusiveList — 링크가 노드 안에 박힌 이중 연결 리스트.
//
// 일반 std::list 는 노드를 따로 할당하지만, intrusive 리스트는 *객체 자신이
// prev/next 를 가진다*. 그래서 삽입/제거에 할당이 없고(O(1)), 한 객체가 여러
// 리스트에 동시에 들 수 있다(훅을 여러 개 두면). 게임에서 "활성 엔티티 리스트",
// "이번 프레임 dirty 리스트" 처럼 같은 객체를 여러 목록에 넣는 패턴에 쓴다.
//
// 센티넬(circular) 구조라 빈 리스트·양 끝 처리에 분기가 없다.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mega {

struct ListHook {
    ListHook* prev = nullptr;
    ListHook* next = nullptr;
    bool Linked() const { return prev != nullptr; }
};

template <class T, ListHook T::*Hook>
class IntrusiveList {
public:
    IntrusiveList() { sentinel_.prev = sentinel_.next = &sentinel_; }
    IntrusiveList(const IntrusiveList&) = delete;
    IntrusiveList& operator=(const IntrusiveList&) = delete;

    bool Empty() const { return sentinel_.next == &sentinel_; }
    size_t Size() const { return size_; }

    void PushBack(T* obj)  { InsertBefore(&sentinel_, Hk(obj)); }
    void PushFront(T* obj) { InsertBefore(sentinel_.next, Hk(obj)); }

    void Remove(T* obj) {
        ListHook* h = Hk(obj);
        if (!h->Linked()) return;
        h->prev->next = h->next;
        h->next->prev = h->prev;
        h->prev = h->next = nullptr;
        --size_;
    }

    T* Front() { return Empty() ? nullptr : Owner(sentinel_.next); }
    T* Back()  { return Empty() ? nullptr : Owner(sentinel_.prev); }

    // 순회: for (T* e = list.First(); e; e = list.Next(e))
    T* First() { return Empty() ? nullptr : Owner(sentinel_.next); }
    T* Next(T* obj) {
        ListHook* n = Hk(obj)->next;
        return n == &sentinel_ ? nullptr : Owner(n);
    }

    void Clear() {
        ListHook* cur = sentinel_.next;
        while (cur != &sentinel_) {
            ListHook* nxt = cur->next;
            cur->prev = cur->next = nullptr;
            cur = nxt;
        }
        sentinel_.prev = sentinel_.next = &sentinel_;
        size_ = 0;
    }

private:
    static ListHook* Hk(T* obj) { return &(obj->*Hook); }

    // ListHook* → 소유 객체 T*. 멤버 포인터로 오프셋을 역산.
    static T* Owner(ListHook* h) {
        // &((T*)0->*Hook) 의 주소가 곧 훅의 오프셋. 모든 실 컴파일러에서 동작.
        const size_t off = reinterpret_cast<size_t>(
            &(reinterpret_cast<T*>(0)->*Hook));
        return reinterpret_cast<T*>(reinterpret_cast<char*>(h) - off);
    }

    void InsertBefore(ListHook* pos, ListHook* node) {
        node->prev = pos->prev;
        node->next = pos;
        pos->prev->next = node;
        pos->prev = node;
        ++size_;
    }

    ListHook sentinel_;   // 자기 자신을 가리키는 센티넬
    size_t   size_ = 0;
};

}  // namespace mega
