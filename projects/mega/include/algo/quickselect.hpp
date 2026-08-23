// quickselect — 상위 k 개를 **정렬하지 않고** 앞으로 모은다.
//
// 스냅샷 한 장에 실을 수 있는 적은 64자리다. 규모 팔에서 살아 있는 적은 2048까지
// 오른다. 그 2048 중 어느 64를 실을 것인가 — 이 파일이 답하는 것은 그 고르기이고,
// 전체를 정렬해서 앞을 자르는 것보다 싼 방법이 있다는 것이 알고리즘 쪽 사정이다.
//
// 게임 쪽 사정은 다르다. **무엇을 고르든 같은 월드는 같은 64를 내야 한다.** 그래서
// 이 파일이 요구하는 비교자는 임의의 약한 순서가 아니라 **전순서**다 — 동점이
// 하나도 없어야 한다. 동점이 있으면 분할이 그 둘을 어느 쪽에 두는지가 입력 순서에
// 좌우되고, 그러면 같은 월드가 두 번 다른 패킷을 낸다. 저장 배치를 바꾸는 A/B
// 팔에서 행 순서가 실제로 달라지므로 이것은 가정이 아니라 관측되는 성질이다.
//
// 피벗도 같은 이유로 결정적이다. 무작위 피벗은 기대 O(n) 을 주지만 이 제품에서는
// 쓸 수 없다 — 난수를 쓰는 순간 선택 자체는 여전히 같아도(전순서이므로) 그
// 난수원이 시뮬의 다른 소비자와 상태를 공유하면 게임이 갈린다. 세 자리 중앙값은
// 난수 없이 정렬된 입력의 최악을 피한다.

#pragma once

#include <cstddef>
#include <utility>

namespace mega {
namespace algo {

namespace detail {

// (first, mid, last) 세 자리의 중앙값 위치. 이미 정렬된 입력에서 첫 원소를 피벗으로
// 잡는 최악(O(n^2))을 난수 없이 피한다.
template <class T, class Less>
inline size_t MedianOfThree(T* data, size_t lo, size_t hi, Less less) {
    const size_t mid = lo + (hi - lo) / 2;
    if (less(data[mid], data[lo])) {
        if (less(data[hi], data[mid])) return mid;
        return less(data[hi], data[lo]) ? hi : lo;
    }
    if (less(data[hi], data[mid])) return less(data[hi], data[lo]) ? lo : hi;
    return mid;
}

}  // namespace detail

// data[0..count) 를 재배열해 `less` 기준 상위 k 개가 data[0..k) 에 오게 한다.
// 그 k 개 안의 순서는 정하지 않는다 — 필요한 것은 **집합**이지 순서가 아니다.
//
// `less` 는 전순서여야 한다. 동점을 남기면 결과 집합이 입력 순서에 좌우된다.
// snippet-begin: quickselect-topk
template <class T, class Less>
inline void SelectTopK(T* data, size_t count, size_t k, Less less) {
    if (k >= count || k == 0) return;
    size_t lo = 0, hi = count - 1;
    while (lo < hi) {
        const size_t pivotAt = detail::MedianOfThree(data, lo, hi, less);
        // 피벗을 끝으로 치워 두고 한 번 훑는다(Lomuto). 교환이 실제로 원소를
        // 옮기지 않으면 분할은 아무 일도 하지 않고, 그러면 앞 k 개는 그냥 저장
        // 순서 앞 k 개다 — 흡수 전 동작과 구분되지 않는다.
        std::swap(data[pivotAt], data[hi]);
        size_t store = lo;
        for (size_t i = lo; i < hi; ++i) {
            if (less(data[i], data[hi])) {
                std::swap(data[i], data[store]);
                ++store;
            }
        }
        std::swap(data[store], data[hi]);
        if (store == k) return;
        if (store < k) lo = store + 1;
        else hi = store - 1;   // store > k >= 1 이므로 store - 1 은 음수가 되지 않는다
    }
}
// snippet-end: quickselect-topk

}  // namespace algo
}  // namespace mega
