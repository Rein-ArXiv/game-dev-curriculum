// dsu — 서로소 집합. 경로 압축 + 크기 기준 합치기.
//
// 이 게임이 이 자료구조를 필요로 하는 자리는 하나다. 절차 맵의 두 번째 골격
// 생성기가 셀 사이 간선을 가중치 순으로 훑으며 "이 간선을 채택하면 사이클이
// 생기는가"를 묻는데, 그 질문에 상수 시간에 가깝게 답하는 것이 이것이다.
//
// 두 최적화의 성격이 다르다는 것을 코드가 아니라 **검사가** 드러낸다.
//
//   경로 압축     Find 가 지나온 정점을 전부 뿌리에 직결한다. 이것을 지우면
//                 여전히 옳지만 느려진다.
//   크기 합치기   작은 나무를 큰 나무 밑에 붙인다. 이것도 지우면 옳고 느리다.
//
// 즉 **둘 다 정확성에 기여하지 않는다.** 정확성을 지키는 것은 `Union` 이
// 돌려주는 불리언 하나다 — 이미 같은 집합이면 거짓이고, 그 거짓을 무시하고
// 벽을 뚫으면 신장 트리가 아니라 사이클 있는 그래프가 나온다. 무력화 변이가
// 어느 줄에서 검사를 죽이고 어느 줄에서 죽이지 못하는지가 그대로 갈린다.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mega {
namespace algo {

// snippet-begin: dsu-core
class DisjointSet {
public:
    DisjointSet() = default;
    explicit DisjointSet(size_t count) { Reset(count); }

    void Reset(size_t count) {
        parent_.resize(count);
        size_.assign(count, 1);
        for (size_t i = 0; i < count; ++i) parent_[i] = static_cast<uint32_t>(i);
        components_ = count;
    }

    size_t Count() const { return parent_.size(); }
    size_t Components() const { return components_; }

    // 경로 압축. 재귀가 아니라 두 번 훑는다 — 41x41 격자에서는 재귀도 안전하지만,
    // 깊이가 입력에 좌우되는 재귀를 자료구조 안에 두면 그 안전이 호출자의 크기
    // 선택에 딸리게 된다.
    size_t Find(size_t x) {
        size_t root = x;
        while (parent_[root] != root) root = parent_[root];
        while (parent_[x] != root) {
            const size_t next = parent_[x];
            parent_[x] = static_cast<uint32_t>(root);
            x = next;
        }
        return root;
    }

    bool Connected(size_t a, size_t b) { return Find(a) == Find(b); }

    // 합쳐졌으면 참, 이미 같은 집합이었으면 거짓. **이 반환값이 정확성의
    // 전부다.** 크기 비교를 지워도 아무 단언도 죽지 않지만, 이 불리언을
    // 무시하면 신장 트리라는 성질이 즉시 무너진다.
    bool Union(size_t a, size_t b) {
        size_t ra = Find(a), rb = Find(b);
        if (ra == rb) return false;
        if (size_[ra] < size_[rb]) { const size_t t = ra; ra = rb; rb = t; }
        parent_[rb] = static_cast<uint32_t>(ra);
        size_[ra] += size_[rb];
        --components_;
        return true;
    }

    size_t SizeOf(size_t x) { return size_[Find(x)]; }

private:
    std::vector<uint32_t> parent_;
    std::vector<uint32_t> size_;
    size_t                components_ = 0;
};
// snippet-end: dsu-core

}  // namespace algo
}  // namespace mega
