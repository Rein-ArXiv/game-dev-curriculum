// OpenAddressingHashMap — 개방 주소법(선형 탐사) 해시맵.
//
// 체이닝(노드 할당 + 포인터 추적)과 달리 개방 주소법은 한 연속 배열에 키-값을
// 놓아 캐시 친화적이다. 충돌은 다음 슬롯으로 선형 탐사한다. 삭제는 톰스톤
// 대신 *backward-shift* 로 처리해, 탐사 사슬을 끊지 않으면서 빈 슬롯을 진짜로
// 비운다(톰스톤 누적에 의한 성능 저하 회피). 용량은 2의 거듭제곱이라 모듈로가
// 비트 마스크가 된다. 게임에서 EntityId→인덱스 매핑 등에 쓴다.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace mega {

template <class K, class V, class Hash = std::hash<K>>
class OpenAddressingHashMap {
public:
    OpenAddressingHashMap() { Rehash(16); }

    size_t Size() const { return size_; }
    bool   Empty() const { return size_ == 0; }

    // 삽입 또는 갱신. 새로 삽입되면 true.
    bool Insert(const K& key, const V& value) {
        if ((size_ + 1) * 10 >= cap_ * 7) Rehash(cap_ * 2);   // load factor 0.7
        size_t i = Ideal(key);
        while (slots_[i].used) {
            if (slots_[i].key == key) { slots_[i].value = value; return false; }
            i = (i + 1) & mask_;
        }
        slots_[i] = {true, key, value};
        ++size_;
        return true;
    }

    V* Find(const K& key) {
        size_t i = Ideal(key);
        while (slots_[i].used) {
            if (slots_[i].key == key) return &slots_[i].value;
            i = (i + 1) & mask_;
        }
        return nullptr;
    }
    const V* Find(const K& key) const {
        return const_cast<OpenAddressingHashMap*>(this)->Find(key);
    }
    bool Contains(const K& key) const { return Find(key) != nullptr; }

    // backward-shift 삭제. 제거 후 사슬을 메운다.
    bool Erase(const K& key) {
        size_t i = Ideal(key);
        while (slots_[i].used && !(slots_[i].key == key)) i = (i + 1) & mask_;
        if (!slots_[i].used) return false;

        slots_[i].used = false;
        --size_;
        size_t j = i;
        while (true) {
            j = (j + 1) & mask_;
            if (!slots_[j].used) break;
            size_t k = Ideal(slots_[j].key);   // 이상적 슬롯
            // i 가 (k, j] 순환 구간 안이면 slots_[j] 를 i 로 당겨도 됨.
            bool inRange = (i <= j) ? (k <= i || k > j) : (k <= i && k > j);
            if (inRange) {
                slots_[i] = slots_[j];
                slots_[j].used = false;
                i = j;
            }
        }
        return true;
    }

    void Clear() {
        for (auto& s : slots_) s.used = false;
        size_ = 0;
    }

    // 순회 콜백: fn(const K&, V&)
    template <class Fn>
    void ForEach(Fn&& fn) {
        for (auto& s : slots_) if (s.used) fn(s.key, s.value);
    }

private:
    struct Slot { bool used = false; K key{}; V value{}; };

    size_t Ideal(const K& key) const {
        size_t h = Hash{}(key);
        h ^= h >> 16; h *= 0x9E3779B97F4A7C15ull; h ^= h >> 16;   // 믹스(클러스터 회피)
        return h & mask_;
    }

    void Rehash(size_t newCap) {
        std::vector<Slot> old = std::move(slots_);
        cap_  = newCap < 16 ? 16 : newCap;
        mask_ = cap_ - 1;
        slots_.assign(cap_, Slot{});
        size_ = 0;
        for (auto& s : old) if (s.used) Insert(s.key, s.value);
    }

    std::vector<Slot> slots_;
    size_t cap_  = 0;
    size_t mask_ = 0;
    size_t size_ = 0;
};

}  // namespace mega
