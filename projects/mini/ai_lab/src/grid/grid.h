// 공유 그리드 — 여러 Scene 이 같은 장애물 배치 위에서 알고리즘을 비교한다.
//
// 책임:
//   - 셀 walkable 비트맵 + 마우스 페인트 (좌클릭 드래그=벽, 우클릭=지움).
//   - start / goal 셀 (MoveStart/MoveGoal 모드에서 좌클릭으로 이동).
//   - 셀 <-> 화면 좌표 변환 (캔버스 영역 안에 맞춤).
//   - 셀·격자선·start·goal 기본 그리기.
//
// 좌표계: cell (cx, cy) 는 0 <= cx < width, 0 <= cy < height. 화면 변환은
// 매 프레임 캔버스 Rectangle 로 갱신된다 (창 크기 변화 대응).

#pragma once

#include "raylib.h"

#include <cstdint>
#include <vector>

namespace ailab {

struct Cell {
    int x = 0, y = 0;
    bool operator==(const Cell& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Cell& o) const { return !(*this == o); }
};

// 마우스 페인트 모드 — 좌/우클릭이 무엇을 하는지.
enum class EditMode {
    Walls,     // 좌클릭=벽 추가, 우클릭=벽 제거
    MoveStart, // 좌클릭=start 이동
    MoveGoal,  // 좌클릭=goal 이동
};

class Grid {
public:
    void Init(int width, int height);
    void Resize(int width, int height);   // 내용 유지하며 크기 변경 (clamp)
    void ClearWalls();
    void RandomWalls(unsigned seed, float density);

    int  Width()  const { return width_; }
    int  Height() const { return height_; }
    int  Index(int x, int y) const { return y * width_ + x; }
    bool InBounds(int x, int y) const {
        return x >= 0 && y >= 0 && x < width_ && y < height_;
    }
    bool Walkable(int x, int y) const {
        return InBounds(x, y) && !walls_[(size_t)Index(x, y)];
    }
    void SetWall(int x, int y, bool wall) {
        if (InBounds(x, y)) walls_[(size_t)Index(x, y)] = wall;
    }

    Cell Start() const { return start_; }
    Cell Goal()  const { return goal_; }
    void SetStart(Cell c) { start_ = c; }
    void SetGoal(Cell c)  { goal_  = c; }

    // 캔버스 영역에 맞춰 셀 크기·오프셋 계산. 매 프레임 호출.
    void Layout(int canvasX, int canvasY, int canvasW, int canvasH);
    float CellSize() const { return cellSize_; }

    Vector2 CellCenter(int x, int y) const;
    Vector2 CellCenter(Cell c) const { return CellCenter(c.x, c.y); }
    bool    ScreenToCell(Vector2 screen, Cell& out) const;

    // 마우스 입력 처리 (페인트 / start·goal 이동). 변경이 있으면 true.
    bool HandleMouseEdit(EditMode mode);

    // 격자선 + 벽 + start + goal 그리기.
    void Draw() const;
    void DrawCellFill(int x, int y, Color c) const;
    void DrawCellFill(Cell c, Color col) const { DrawCellFill(c.x, c.y, col); }

private:
    int   width_  = 0;
    int   height_ = 0;
    std::vector<uint8_t> walls_;   // 1=벽

    Cell  start_{2, 2};
    Cell  goal_{0, 0};

    // 화면 레이아웃 (Layout 이 갱신).
    float originX_  = 0.0f;
    float originY_  = 0.0f;
    float cellSize_ = 16.0f;
};

}  // namespace ailab
