// text — HUD용 5x7 비트맵 폰트.
//
// 한글 비트맵은 글자 수가 방대해, 화면 내 텍스트는 영문 대문자·숫자·기본 기호만 지원한다
// (한글은 창 타이틀바=시스템 폰트). 메뉴·HUD·오버레이용. 각 글자는 7행×5열 비트맵.
//
// games/defense/text.hpp 에서 옮겨 왔다. 게임별 자산이 아니라 raster2d 위에
// 얹힌 렌더 유틸이고, 게임이 아니라 렌더 계층이 소유해야 재사용된다.

#pragma once

#include "render/raster2d.hpp"

#include <cstdint>
#include <cstring>

namespace mega {
namespace render {

// 7행, 각 행의 하위 5비트(bit4=왼쪽). 미정의 문자는 빈 칸.
inline const uint8_t* Glyph5x7(char c) {
    static const uint8_t SP[7] = {0, 0, 0, 0, 0, 0, 0};
    switch (c) {
        case 'A': { static const uint8_t g[7] = {0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001}; return g; }
        case 'B': { static const uint8_t g[7] = {0b11110,0b10001,0b11110,0b10001,0b10001,0b10001,0b11110}; return g; }
        case 'C': { static const uint8_t g[7] = {0b01110,0b10001,0b10000,0b10000,0b10000,0b10001,0b01110}; return g; }
        case 'D': { static const uint8_t g[7] = {0b11110,0b10001,0b10001,0b10001,0b10001,0b10001,0b11110}; return g; }
        case 'E': { static const uint8_t g[7] = {0b11111,0b10000,0b11110,0b10000,0b10000,0b10000,0b11111}; return g; }
        case 'F': { static const uint8_t g[7] = {0b11111,0b10000,0b11110,0b10000,0b10000,0b10000,0b10000}; return g; }
        case 'G': { static const uint8_t g[7] = {0b01110,0b10001,0b10000,0b10111,0b10001,0b10001,0b01110}; return g; }
        case 'H': { static const uint8_t g[7] = {0b10001,0b10001,0b11111,0b10001,0b10001,0b10001,0b10001}; return g; }
        case 'I': { static const uint8_t g[7] = {0b01110,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110}; return g; }
        case 'J': { static const uint8_t g[7] = {0b00111,0b00010,0b00010,0b00010,0b10010,0b10010,0b01100}; return g; }
        case 'K': { static const uint8_t g[7] = {0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001}; return g; }
        case 'L': { static const uint8_t g[7] = {0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111}; return g; }
        case 'M': { static const uint8_t g[7] = {0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001}; return g; }
        case 'N': { static const uint8_t g[7] = {0b10001,0b11001,0b10101,0b10011,0b10001,0b10001,0b10001}; return g; }
        case 'O': { static const uint8_t g[7] = {0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110}; return g; }
        case 'P': { static const uint8_t g[7] = {0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000}; return g; }
        case 'Q': { static const uint8_t g[7] = {0b01110,0b10001,0b10001,0b10001,0b10101,0b10010,0b01101}; return g; }
        case 'R': { static const uint8_t g[7] = {0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001}; return g; }
        case 'S': { static const uint8_t g[7] = {0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110}; return g; }
        case 'T': { static const uint8_t g[7] = {0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100}; return g; }
        case 'U': { static const uint8_t g[7] = {0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110}; return g; }
        case 'V': { static const uint8_t g[7] = {0b10001,0b10001,0b10001,0b10001,0b10001,0b01010,0b00100}; return g; }
        case 'W': { static const uint8_t g[7] = {0b10001,0b10001,0b10001,0b10101,0b10101,0b11011,0b10001}; return g; }
        case 'X': { static const uint8_t g[7] = {0b10001,0b10001,0b01010,0b00100,0b01010,0b10001,0b10001}; return g; }
        case 'Y': { static const uint8_t g[7] = {0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100}; return g; }
        case 'Z': { static const uint8_t g[7] = {0b11111,0b00001,0b00010,0b00100,0b01000,0b10000,0b11111}; return g; }
        case '0': { static const uint8_t g[7] = {0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110}; return g; }
        case '1': { static const uint8_t g[7] = {0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110}; return g; }
        case '2': { static const uint8_t g[7] = {0b01110,0b10001,0b00001,0b00110,0b01000,0b10000,0b11111}; return g; }
        case '3': { static const uint8_t g[7] = {0b11111,0b00010,0b00100,0b00010,0b00001,0b10001,0b01110}; return g; }
        case '4': { static const uint8_t g[7] = {0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010}; return g; }
        case '5': { static const uint8_t g[7] = {0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110}; return g; }
        case '6': { static const uint8_t g[7] = {0b00110,0b01000,0b10000,0b11110,0b10001,0b10001,0b01110}; return g; }
        case '7': { static const uint8_t g[7] = {0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000}; return g; }
        case '8': { static const uint8_t g[7] = {0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110}; return g; }
        case '9': { static const uint8_t g[7] = {0b01110,0b10001,0b10001,0b01111,0b00001,0b00010,0b01100}; return g; }
        case '/': { static const uint8_t g[7] = {0b00001,0b00010,0b00010,0b00100,0b01000,0b01000,0b10000}; return g; }
        case ':': { static const uint8_t g[7] = {0b00000,0b00100,0b00100,0b00000,0b00100,0b00100,0b00000}; return g; }
        case '.': { static const uint8_t g[7] = {0b00000,0b00000,0b00000,0b00000,0b00000,0b01100,0b01100}; return g; }
        case '-': { static const uint8_t g[7] = {0b00000,0b00000,0b00000,0b11111,0b00000,0b00000,0b00000}; return g; }
        case '!': { static const uint8_t g[7] = {0b00100,0b00100,0b00100,0b00100,0b00100,0b00000,0b00100}; return g; }
        case '*': { static const uint8_t g[7] = {0b00000,0b01010,0b00100,0b11111,0b00100,0b01010,0b00000}; return g; }
        case '%': { static const uint8_t g[7] = {0b11001,0b11010,0b00010,0b00100,0b01000,0b01011,0b10011}; return g; }
        default: return SP;
    }
}

// (x,y) 에 글자 한 자(scale 배). 대문자로 변환해 그린다.
inline void DrawChar(Framebuffer& fb, int x, int y, char c, Color col, int scale = 2) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    const uint8_t* g = Glyph5x7(c);
    for (int row = 0; row < 7; ++row)
        for (int cx = 0; cx < 5; ++cx)
            if (g[row] & (1 << (4 - cx)))
                FillRect(fb, x + cx * scale, y + row * scale, scale, scale, col);
}

// 문자열을 (x,y) 부터. 글자 간격 = (5+1)*scale.
inline void DrawString(Framebuffer& fb, int x, int y, const char* s, Color col, int scale = 2) {
    int cx = x;
    for (const char* p = s; *p; ++p) {
        if (*p == '\n') { y += 8 * scale; cx = x; continue; }
        DrawChar(fb, cx, y, *p, col, scale);
        cx += 6 * scale;
    }
}

inline int TextWidth(const char* s, int scale = 2) { return (int)std::strlen(s) * 6 * scale; }

}  // namespace render
}  // namespace mega
