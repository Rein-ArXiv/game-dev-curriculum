// uniform_grid — 균일 격자 공간 해시. 객체가 고르게 퍼지고 밀도가 일정할 때.
//
// 공간을 고정 크기 셀로 나눠 각 셀에 항목 목록을 둔다. 삽입·쿼리가 셀 인덱스
// 계산 한 번으로 끝나 O(1) 에 가깝다. 셀 크기를 평균 쿼리 반경에 맞추면 쿼리가
// 닿는 셀이 상수 개다. 트리와 달리 분포가 치우치면(한 셀에 몰리면) 퇴화하므로,
// 균일 분포·정적 지형에 맞는다(math 시리즈 공간 분할 비교의 cross-over).

#pragma once

#include "math/spatial/aabb.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

namespace mega {
namespace math {

template <class T>
class UniformGrid {
public:
    void Init(const Aabb2& bounds, float cellSize) {
        bounds_ = bounds;
        cell_   = cellSize > 1e-6f ? cellSize : 1.0f;
        cols_   = (int)std::ceil((bounds.max.x - bounds.min.x) / cell_);
        rows_   = (int)std::ceil((bounds.max.y - bounds.min.y) / cell_);
        if (cols_ < 1) cols_ = 1;
        if (rows_ < 1) rows_ = 1;
        cells_.assign((size_t)cols_ * rows_, {});
        size_ = 0;
    }

    void Clear() {
        for (auto& c : cells_) c.clear();
        size_ = 0;
    }

    void Insert(const Vec2& p, const T& data) {
        int ci = CellIndex(p);
        cells_[ci].push_back(Item{p, data});
        ++size_;
    }

    // 영역 쿼리: region 과 겹치는 셀만 순회.
    template <class Fn>
    void Query(const Aabb2& region, Fn&& fn) const {
        int x0, y0, x1, y1;
        CellRange(region, x0, y0, x1, y1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                for (const Item& it : cells_[(size_t)y * cols_ + x])
                    if (region.Contains(it.pos)) fn(it.data);
    }

    template <class Fn>
    void QueryRadius(const Vec2& center, float radius, Fn&& fn) const {
        Aabb2 box = Aabb2::FromCenterHalf(center, {radius, radius});
        float r2 = radius * radius;
        int x0, y0, x1, y1;
        CellRange(box, x0, y0, x1, y1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                for (const Item& it : cells_[(size_t)y * cols_ + x]) {
                    float dx = it.pos.x - center.x, dy = it.pos.y - center.y;
                    if (dx * dx + dy * dy <= r2) fn(it.data);
                }
    }

    size_t Size() const { return size_; }
    int    Cols() const { return cols_; }
    int    Rows() const { return rows_; }

private:
    struct Item { Vec2 pos; T data; };

    int ClampI(int v, int lo, int hi) const { return v < lo ? lo : (v > hi ? hi : v); }

    int CellIndex(const Vec2& p) const {
        int cx = ClampI((int)((p.x - bounds_.min.x) / cell_), 0, cols_ - 1);
        int cy = ClampI((int)((p.y - bounds_.min.y) / cell_), 0, rows_ - 1);
        return cy * cols_ + cx;
    }
    void CellRange(const Aabb2& r, int& x0, int& y0, int& x1, int& y1) const {
        x0 = ClampI((int)((r.min.x - bounds_.min.x) / cell_), 0, cols_ - 1);
        y0 = ClampI((int)((r.min.y - bounds_.min.y) / cell_), 0, rows_ - 1);
        x1 = ClampI((int)((r.max.x - bounds_.min.x) / cell_), 0, cols_ - 1);
        y1 = ClampI((int)((r.max.y - bounds_.min.y) / cell_), 0, rows_ - 1);
    }

    Aabb2                          bounds_;
    float                          cell_ = 1.0f;
    int                            cols_ = 1, rows_ = 1;
    std::vector<std::vector<Item>> cells_;
    size_t                         size_ = 0;
};

}  // namespace math
}  // namespace mega
