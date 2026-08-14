// raster2d — 소프트웨어 2D 래스터 프리미티브.
//
// 디펜스 게임을 그리는 데 필요한 최소 집합: 채운 사각형(타워·바닥), 원(적·발사체),
// 선(사거리·디버그), 삼각형(일반 폴리곤 채우기의 기본). GPU 인스턴싱 대신 CPU 로
// 직접 픽셀을 채우지만, 같은 호출 형태(DrawXxx)라 RHI 백엔드가 GPU 로 바뀌어도
// 호출부는 그대로다.

#pragma once

#include "render/framebuffer.hpp"

#include <algorithm>
#include <cmath>

namespace mega {
namespace render {

inline void FillRect(Framebuffer& fb, int x, int y, int w, int h, Color c) {
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i)
            fb.Blend(i, j, c);
}

inline void FillCircle(Framebuffer& fb, int cx, int cy, int r, Color c) {
    int r2 = r * r;
    for (int j = -r; j <= r; ++j)
        for (int i = -r; i <= r; ++i)
            if (i * i + j * j <= r2) fb.Blend(cx + i, cy + j, c);
}

// 입체(베벨) 사각형: 밑색을 채우고 위/왼쪽 1px 은 밝게, 아래/오른쪽 1px 은 어둡게.
// 평평한 FillRect 보다 블록이 떠 보인다(벽돌·타워·버튼). 크기 무관(가변 크기 OK).
inline void FillRectBevel(Framebuffer& fb, int x, int y, int w, int h, Color base, int d = 60) {
    if (w <= 0 || h <= 0) return;
    auto cl = [](int v) -> uint8_t { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); };
    Color lite{cl(base.r + d), cl(base.g + d), cl(base.b + d), base.a};
    Color dark{cl(base.r - d), cl(base.g - d), cl(base.b - d), base.a};
    FillRect(fb, x, y, w, h, base);
    for (int i = x; i < x + w; ++i) { fb.Blend(i, y, lite); fb.Blend(i, y + h - 1, dark); }
    for (int j = y; j < y + h; ++j) { fb.Blend(x, j, lite); fb.Blend(x + w - 1, j, dark); }
}

// Bresenham 직선.
inline void DrawLine(Framebuffer& fb, int x0, int y0, int x1, int y1, Color c) {
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        fb.Blend(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// 빈 사각형 테두리.
inline void DrawRectOutline(Framebuffer& fb, int x, int y, int w, int h, Color c) {
    DrawLine(fb, x, y, x + w - 1, y, c);
    DrawLine(fb, x, y + h - 1, x + w - 1, y + h - 1, c);
    DrawLine(fb, x, y, x, y + h - 1, c);
    DrawLine(fb, x + w - 1, y, x + w - 1, y + h - 1, c);
}

// 채운 삼각형(에지 함수 + 바운딩 박스).
inline void FillTriangle(Framebuffer& fb, int x0, int y0, int x1, int y1, int x2, int y2, Color c) {
    int minx = std::min({x0, x1, x2}), maxx = std::max({x0, x1, x2});
    int miny = std::min({y0, y1, y2}), maxy = std::max({y0, y1, y2});
    auto edge = [](int ax, int ay, int bx, int by, int px, int py) {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    };
    int area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    for (int py = miny; py <= maxy; ++py)
        for (int px = minx; px <= maxx; ++px) {
            int w0 = edge(x1, y1, x2, y2, px, py);
            int w1 = edge(x2, y2, x0, y0, px, py);
            int w2 = edge(x0, y0, x1, y1, px, py);
            // 면적 부호에 맞춰 모두 같은 부호면 내부.
            bool inside = area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
                                   : (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (inside) fb.Blend(px, py, c);
        }
}

}  // namespace render
}  // namespace mega
