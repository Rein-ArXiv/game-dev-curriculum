// level_editor — 타일 레벨 편집과 검증. 가장 중요한 건 연결성 검사다.
//
// 레벨을 만들면 즉시 "스폰(S)에서 목표(G)까지 도달 가능한가" 를 flood fill 로
// 검사한다. 플레이 불가능한 레벨(벽으로 막힌 목표)이 빌드에 들어가지 못하게 막는
// 안전망이다. 타일은 '.'(바닥) '#'(벽) 'S'(스폰) 'G'(목표)(tools 레벨 검증 챕터).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mega {
namespace tools {

class Level {
public:
    void Init(int w, int h, char fill = '.') {
        w_ = w; h_ = h;
        tiles_.assign((size_t)w * h, fill);
    }
    void FromAscii(const std::vector<std::string>& rows) {
        h_ = (int)rows.size();
        w_ = h_ > 0 ? (int)rows[0].size() : 0;
        tiles_.assign((size_t)w_ * h_, '.');
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_ && x < (int)rows[y].size(); ++x)
                tiles_[(size_t)y * w_ + x] = rows[y][x];
    }
    std::vector<std::string> ToAscii() const {
        std::vector<std::string> rows;
        for (int y = 0; y < h_; ++y) {
            std::string r;
            for (int x = 0; x < w_; ++x) r += tiles_[(size_t)y * w_ + x];
            rows.push_back(r);
        }
        return rows;
    }

    int  Width()  const { return w_; }
    int  Height() const { return h_; }
    bool InBounds(int x, int y) const { return x >= 0 && x < w_ && y >= 0 && y < h_; }
    char At(int x, int y) const { return InBounds(x, y) ? tiles_[(size_t)y * w_ + x] : '#'; }
    void Set(int x, int y, char c) { if (InBounds(x, y)) tiles_[(size_t)y * w_ + x] = c; }
    bool Walkable(int x, int y) const { return InBounds(x, y) && At(x, y) != '#'; }

    struct Report {
        bool hasSpawn = false, hasGoal = false, reachable = false;
        int  walls = 0, floors = 0;
        int  sx = -1, sy = -1, gx = -1, gy = -1;
        bool Valid() const { return hasSpawn && hasGoal && reachable; }
    };

    // 검증: 스폰·목표 존재 + 스폰에서 목표 도달 가능(4방향 flood fill).
    Report Validate() const {
        Report r;
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x) {
                char c = At(x, y);
                if (c == '#') ++r.walls;
                else          ++r.floors;
                if (c == 'S') { r.hasSpawn = true; r.sx = x; r.sy = y; }
                if (c == 'G') { r.hasGoal = true;  r.gx = x; r.gy = y; }
            }
        if (!r.hasSpawn || !r.hasGoal) return r;

        std::vector<uint8_t> seen((size_t)w_ * h_, 0);
        std::vector<int> stack;
        stack.push_back(r.sy * w_ + r.sx);
        seen[r.sy * w_ + r.sx] = 1;
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        while (!stack.empty()) {
            int cur = stack.back(); stack.pop_back();
            int cx = cur % w_, cy = cur / w_;
            for (int d = 0; d < 4; ++d) {
                int nx = cx + dx[d], ny = cy + dy[d];
                if (!Walkable(nx, ny)) continue;
                int ni = ny * w_ + nx;
                if (seen[ni]) continue;
                seen[ni] = 1;
                stack.push_back(ni);
            }
        }
        r.reachable = seen[r.gy * w_ + r.gx] != 0;
        return r;
    }

private:
    int               w_ = 0, h_ = 0;
    std::vector<char> tiles_;
};

}  // namespace tools
}  // namespace mega
