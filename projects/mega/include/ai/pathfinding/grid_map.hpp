// grid_map — 격자 맵. ASCII 로 정의하고 A*·Flow Field 가 공유한다.
//
//   '.' 통행 가능, '#' 벽, 'S' 스폰, 'G' 목표.
//
// 디펜스 시나리오의 지형이다. 텍스트 에디터로 곧장 그릴 수 있어 빠르게 실험한다.

#pragma once

#include "math/vec.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mega {
namespace ai {
namespace path {

class GridMap {
public:
    int Width()  const { return w_; }
    int Height() const { return h_; }
    int StartX() const { return sx_; }
    int StartY() const { return sy_; }
    int GoalX()  const { return gx_; }
    int GoalY()  const { return gy_; }

    void Init(int w, int h, bool walkable = true) {
        w_ = w; h_ = h;
        cells_.assign((size_t)w * h, walkable ? 1 : 0);
        sx_ = sy_ = gx_ = gy_ = 0;
    }

    // ASCII 행들로 맵 구성. 모든 행 길이가 같다고 가정.
    void ParseAscii(const std::vector<std::string>& rows) {
        h_ = (int)rows.size();
        w_ = h_ > 0 ? (int)rows[0].size() : 0;
        cells_.assign((size_t)w_ * h_, 1);
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_ && x < (int)rows[y].size(); ++x) {
                char c = rows[y][x];
                if (c == '#') cells_[Index(x, y)] = 0;
                else          cells_[Index(x, y)] = 1;
                if (c == 'S') { sx_ = x; sy_ = y; }
                if (c == 'G') { gx_ = x; gy_ = y; }
            }
    }

    int  Index(int x, int y) const { return y * w_ + x; }
    bool InBounds(int x, int y) const { return x >= 0 && x < w_ && y >= 0 && y < h_; }
    bool Walkable(int x, int y) const { return InBounds(x, y) && cells_[Index(x, y)] != 0; }
    void SetWalkable(int x, int y, bool v) { if (InBounds(x, y)) cells_[Index(x, y)] = v ? 1 : 0; }

private:
    int w_ = 0, h_ = 0;
    int sx_ = 0, sy_ = 0, gx_ = 0, gy_ = 0;
    std::vector<uint8_t> cells_;   // 1 = 통행 가능
};

}  // namespace path
}  // namespace ai
}  // namespace mega
