// SoAVector — Structure-of-Arrays 벡터. 필드별로 연속 배열을 둔다.
//
// AoS(`std::vector<Struct>`)는 한 객체의 모든 필드가 인접하지만, "위치만 갱신"
// 같은 핫 루프가 안 쓰는 필드(색·HP)까지 캐시 라인에 끌고 온다. SoA 는 각
// 필드를 *독립된 연속 배열* 로 둬, 한 필드만 순회할 때 캐시 라인을 그 필드로만
// 채운다. parts 시리즈의 캐시 측정에서 갱신 루프가 AoS 대비 2~5배 빠른 근거다.
// SIMD 적용도 SoA 배치에서 자연스럽다(같은 필드가 연속).
//
// 구현은 `std::tuple<std::vector<Ts>...>` — 각 컬럼이 독립 std::vector.

#pragma once

#include <cstddef>
#include <tuple>
#include <utility>
#include <vector>

namespace mega {

template <class... Ts>
class SoAVector {
    static_assert(sizeof...(Ts) >= 1, "SoAVector 는 최소 한 컬럼이 필요하다");
    using Indices = std::index_sequence_for<Ts...>;

public:
    size_t Size() const { return std::get<0>(columns_).size(); }
    bool   Empty() const { return Size() == 0; }

    void Reserve(size_t n) { ForEachCol([n](auto& v) { v.reserve(n); }); }
    void Clear()           { ForEachCol([](auto& v) { v.clear(); }); }

    // 한 행 추가(각 컬럼에 대응 값 push).
    void PushBack(const Ts&... vals) { PushImpl(Indices{}, vals...); }

    // I 번째 컬럼 배열에 직접 접근(연속 메모리). 핫 루프는 이걸로 순회.
    template <size_t I> auto&       Column()       { return std::get<I>(columns_); }
    template <size_t I> const auto& Column() const { return std::get<I>(columns_); }
    template <size_t I> auto*       Data()         { return std::get<I>(columns_).data(); }
    template <size_t I> const auto* Data()   const { return std::get<I>(columns_).data(); }

    // O(1) swap-remove: 마지막 행과 교체 후 pop. 순서 보존 안 함.
    void SwapRemove(size_t i) {
        size_t last = Size() - 1;
        SwapRemoveImpl(Indices{}, i, last);
    }

    // 모든 컬럼에 같은 연산을 적용한다: fn(std::vector<Ti>&).
    //
    // 순서를 보존하는 압축(살아남은 행만 앞으로 당기기)은 컬럼 수만큼 같은
    // 코드를 반복해야 한다. 컬럼 목록을 아는 것은 이 클래스뿐이므로 그 반복을
    // 여기서 한 번 접는다. SwapRemove 로 대신할 수 없는 이유는 순서다 — 두
    // 저장이 같은 결과를 내려면 남은 행의 상대 순서가 같아야 한다.
    template <class Fn>
    void ForEachColumn(Fn&& fn) { ForEachColImpl(static_cast<Fn&&>(fn), Indices{}); }

private:
    template <size_t... Is>
    void PushImpl(std::index_sequence<Is...>, const Ts&... vals) {
        (std::get<Is>(columns_).push_back(vals), ...);
    }

    template <size_t... Is>
    void SwapRemoveImpl(std::index_sequence<Is...>, size_t i, size_t last) {
        (SwapPop<Is>(i, last), ...);
    }
    template <size_t I>
    void SwapPop(size_t i, size_t last) {
        auto& col = std::get<I>(columns_);
        if (i != last) col[i] = std::move(col[last]);
        col.pop_back();
    }

    template <class Fn, size_t... Is>
    void ForEachColImpl(Fn&& fn, std::index_sequence<Is...>) {
        (fn(std::get<Is>(columns_)), ...);
    }
    template <class Fn>
    void ForEachCol(Fn&& fn) { ForEachColImpl(static_cast<Fn&&>(fn), Indices{}); }

    std::tuple<std::vector<Ts>...> columns_;
};

}  // namespace mega
