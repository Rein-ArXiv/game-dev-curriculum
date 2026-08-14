#include "grid/grid.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace ailab {

void Grid::Init(int width, int height) {
    width_  = width;
    height_ = height;
    walls_.assign((size_t)width_ * height_, 0);
    start_ = {2, height_ / 2};
    goal_  = {width_ - 3, height_ / 2};
}

void Grid::Resize(int width, int height) {
    if (width == width_ && height == height_) return;
    std::vector<uint8_t> next((size_t)width * height, 0);
    for (int y = 0; y < std::min(height, height_); ++y)
        for (int x = 0; x < std::min(width, width_); ++x)
            next[(size_t)(y * width + x)] = walls_[(size_t)Index(x, y)];
    width_  = width;
    height_ = height;
    walls_  = std::move(next);
    // start / goal 을 새 범위 안으로 clamp.
    start_.x = std::clamp(start_.x, 0, width_ - 1);
    start_.y = std::clamp(start_.y, 0, height_ - 1);
    goal_.x  = std::clamp(goal_.x, 0, width_ - 1);
    goal_.y  = std::clamp(goal_.y, 0, height_ - 1);
}

void Grid::ClearWalls() {
    std::fill(walls_.begin(), walls_.end(), (uint8_t)0);
}

void Grid::RandomWalls(unsigned seed, float density) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int y = 0; y < height_; ++y)
        for (int x = 0; x < width_; ++x)
            walls_[(size_t)Index(x, y)] = (dist(rng) < density) ? 1 : 0;
    // start / goal 은 항상 비운다.
    SetWall(start_.x, start_.y, false);
    SetWall(goal_.x, goal_.y, false);
}

void Grid::Layout(int canvasX, int canvasY, int canvasW, int canvasH) {
    if (width_ <= 0 || height_ <= 0) return;
    const float pad = 12.0f;
    float availW = canvasW - 2 * pad;
    float availH = canvasH - 2 * pad;
    cellSize_ = std::floor(std::min(availW / width_, availH / height_));
    if (cellSize_ < 2.0f) cellSize_ = 2.0f;
    float gridW = cellSize_ * width_;
    float gridH = cellSize_ * height_;
    originX_ = canvasX + (canvasW - gridW) * 0.5f;
    originY_ = canvasY + (canvasH - gridH) * 0.5f;
}

Vector2 Grid::CellCenter(int x, int y) const {
    return {originX_ + (x + 0.5f) * cellSize_,
            originY_ + (y + 0.5f) * cellSize_};
}

bool Grid::ScreenToCell(Vector2 s, Cell& out) const {
    int cx = (int)std::floor((s.x - originX_) / cellSize_);
    int cy = (int)std::floor((s.y - originY_) / cellSize_);
    if (!InBounds(cx, cy)) return false;
    out = {cx, cy};
    return true;
}

bool Grid::HandleMouseEdit(EditMode mode) {
    Cell c;
    if (!ScreenToCell(GetMousePosition(), c)) return false;

    bool changed = false;
    if (mode == EditMode::Walls) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            if (c != start_ && c != goal_ && !walls_[(size_t)Index(c.x, c.y)]) {
                SetWall(c.x, c.y, true);
                changed = true;
            }
        } else if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            if (walls_[(size_t)Index(c.x, c.y)]) {
                SetWall(c.x, c.y, false);
                changed = true;
            }
        }
    } else if (mode == EditMode::MoveStart) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && Walkable(c.x, c.y) && c != goal_) {
            if (c != start_) { start_ = c; changed = true; }
        }
    } else if (mode == EditMode::MoveGoal) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && Walkable(c.x, c.y) && c != start_) {
            if (c != goal_) { goal_ = c; changed = true; }
        }
    }
    return changed;
}

void Grid::DrawCellFill(int x, int y, Color col) const {
    if (!InBounds(x, y)) return;
    DrawRectangle((int)(originX_ + x * cellSize_),
                  (int)(originY_ + y * cellSize_),
                  (int)std::ceil(cellSize_), (int)std::ceil(cellSize_), col);
}

void Grid::Draw() const {
    // 벽.
    for (int y = 0; y < height_; ++y)
        for (int x = 0; x < width_; ++x)
            if (walls_[(size_t)Index(x, y)])
                DrawCellFill(x, y, Color{70, 74, 84, 255});

    // 격자선 (셀이 충분히 클 때만).
    if (cellSize_ >= 6.0f) {
        Color line{38, 40, 48, 255};
        for (int x = 0; x <= width_; ++x) {
            float sx = originX_ + x * cellSize_;
            DrawLine((int)sx, (int)originY_,
                     (int)sx, (int)(originY_ + height_ * cellSize_), line);
        }
        for (int y = 0; y <= height_; ++y) {
            float sy = originY_ + y * cellSize_;
            DrawLine((int)originX_, (int)sy,
                     (int)(originX_ + width_ * cellSize_), (int)sy, line);
        }
    }

    // start (초록) / goal (빨강).
    DrawCellFill(start_, Color{80, 200, 120, 255});
    DrawCellFill(goal_, Color{220, 90, 90, 255});
}

}  // namespace ailab
