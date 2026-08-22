// sprite — 소프트웨어 스프라이트(RGBA8) 블릿.
//
// raster2d 의 도형(사각형·원)만으로는 캐릭터·아이콘을 표현하기 빈약해, 작은
// RGBA 비트맵을 프레임버퍼에 알파 블렌딩으로 찍는 최소 스프라이트 경로를 둔다.
// 외부 의존 없이 PPM(P3) 파일을 읽거나 ASCII 아트 + 팔레트로 스프라이트를
// 만든다. 파일이 없어도 폴백 그림으로 실행되며, 에셋 파일만 교체해 외형을
// 바꿀 수 있다. 블릿은 fb.Blend 를 통하므로 경계 클리핑·알파가 framebuffer 와
// 일관된다. GPU 백엔드에선 이 호출이 텍스처 쿼드 인스턴스로 바뀐다.

#pragma once

#include "io/read_file.hpp"
#include "render/framebuffer.hpp"

#include <initializer_list>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace mega {
namespace render {

// 행 우선 RGBA8 픽셀. a==0 은 완전 투명(블릿 시 건너뜀, fb.Blend 가 처리).
struct Sprite {
    int                   w = 0, h = 0;
    std::vector<uint32_t> px;   // 0xAABBGGRR(Pack 형식), 크기 w*h

    Color At(int x, int y) const { return Unpack(px[(size_t)y * w + x]); }
    bool Empty() const { return w <= 0 || h <= 0 || px.size() != static_cast<size_t>(w * h); }
};

inline bool ReadPpmToken(std::istream& input, std::string& token) {
    while (input >> token) {
        if (!token.empty() && token[0] == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        return true;
    }
    return false;
}

// ASCII PPM(P3) loader. Pure magenta is a transparency key. Dimensions are
// capped because assets are untrusted input at this boundary.
//
// 디스크 접근은 io 계층을 통한다(<fstream> 직접 사용 금지) — 파일을 여는 코드가
// 렌더·게임·툴로 흩어지면 그 지점을 교체할 수도, 계층 규칙을 강제할 수도 없다.
// 256x256 상한이 있으므로 전체를 메모리에 올려 파싱해도 안전하다.
//
// 디코드와 읽기를 나눠 둔 이유: 비동기 로드는 바이트를 워커 스레드에서 받아
// 오고, 그 바이트를 스프라이트로 바꾸는 일은 프레임 스레드에서 한다. 디코더가
// 파일 경로만 받으면 그 분리를 표현할 방법이 없다.
inline bool DecodePpmP3(const std::string& text, Sprite& out) {
    std::istringstream input(text);
    std::string token;
    if (!ReadPpmToken(input, token) || token != "P3") return false;
    if (!ReadPpmToken(input, token)) return false;
    int width = 0, height = 0, maxValue = 0;
    try {
        width = std::stoi(token);
        if (!ReadPpmToken(input, token)) return false;
        height = std::stoi(token);
        if (!ReadPpmToken(input, token)) return false;
        maxValue = std::stoi(token);
    } catch (...) {
        return false;
    }
    if (width <= 0 || height <= 0 || width > 256 || height > 256 || maxValue <= 0 || maxValue > 65535)
        return false;

    Sprite decoded;
    decoded.w = width;
    decoded.h = height;
    decoded.px.reserve(static_cast<size_t>(width * height));
    for (int i = 0; i < width * height; ++i) {
        int rgb[3]{};
        for (int channel = 0; channel < 3; ++channel) {
            if (!ReadPpmToken(input, token)) return false;
            try {
                rgb[channel] = std::stoi(token);
            } catch (...) {
                return false;
            }
            if (rgb[channel] < 0 || rgb[channel] > maxValue) return false;
            rgb[channel] = (rgb[channel] * 255 + maxValue / 2) / maxValue;
        }
        Color color{static_cast<uint8_t>(rgb[0]), static_cast<uint8_t>(rgb[1]),
                    static_cast<uint8_t>(rgb[2]), 255};
        if (color.r == 255 && color.g == 0 && color.b == 255) color.a = 0;
        decoded.px.push_back(Pack(color));
    }
    if (ReadPpmToken(input, token)) return false;
    out = std::move(decoded);
    return true;
}

inline bool LoadPpmP3(const std::string& path, Sprite& out) {
    std::string text;
    if (!io::ReadTextFile(path, text, 4 * 1024 * 1024)) return false;
    return DecodePpmP3(text, out);
}

// 스프라이트를 (dx,dy) 좌상단 기준으로 정수배 확대해 블릿. scale<1 은 1 로 클램프.
// 경계 밖 픽셀은 fb.Blend 가 무시하므로 자동 클리핑된다. a==0 픽셀은 스킵.
inline void BlitSprite(Framebuffer& fb, const Sprite& s, int dx, int dy, int scale = 1) {
    if (scale < 1) scale = 1;
    for (int y = 0; y < s.h; ++y) {
        for (int x = 0; x < s.w; ++x) {
            Color c = s.At(x, y);
            if (c.a == 0) continue;
            for (int sy = 0; sy < scale; ++sy)
                for (int sx = 0; sx < scale; ++sx)
                    fb.Blend(dx + x * scale + sx, dy + y * scale + sy, c);
        }
    }
}

// 좌우 반전 블릿(캐릭터가 바라보는 방향 전환용).
inline void BlitSpriteFlipX(Framebuffer& fb, const Sprite& s, int dx, int dy, int scale = 1) {
    if (scale < 1) scale = 1;
    for (int y = 0; y < s.h; ++y) {
        for (int x = 0; x < s.w; ++x) {
            Color c = s.At(s.w - 1 - x, y);
            if (c.a == 0) continue;
            for (int sy = 0; sy < scale; ++sy)
                for (int sx = 0; sx < scale; ++sx)
                    fb.Blend(dx + x * scale + sx, dy + y * scale + sy, c);
        }
    }
}

// ASCII 아트 + 팔레트로 스프라이트 생성. 각 문자열 = 한 행(모두 같은 길이여야).
// '.'(또는 ' ')는 투명. 팔레트에 없는 문자는 투명 처리한다.
// 예: MakeSprite({"...", ".R.", "..."}, {{'R', {255,0,0,255}}})
inline Sprite MakeSprite(std::initializer_list<const char*> rows,
                         const std::unordered_map<char, Color>& palette) {
    Sprite s;
    s.h = (int)rows.size();
    s.w = 0;
    for (const char* r : rows) {
        int len = (int)std::string(r).size();
        if (len > s.w) s.w = len;
    }
    s.px.assign((size_t)s.w * s.h, Pack(Color{0, 0, 0, 0}));  // 기본 투명
    int y = 0;
    for (const char* r : rows) {
        std::string row(r);
        for (int x = 0; x < (int)row.size(); ++x) {
            char ch = row[x];
            if (ch == '.' || ch == ' ') continue;  // 투명
            auto it = palette.find(ch);
            if (it == palette.end()) continue;      // 미정의 문자 = 투명
            s.px[(size_t)y * s.w + x] = Pack(it->second);
        }
        ++y;
    }
    return s;
}

// 불투명(a!=0) 픽셀을 모두 색 c 로 칠한 사본을 반환(알파는 보존). 마스크 모양을
// 런타임 색으로 재사용할 때(예: 클래스색 영웅, 체력 음영 몬스터) 쓴다.
inline Sprite Tint(const Sprite& mask, Color c) {
    Sprite s = mask;
    for (auto& p : s.px) {
        Color o = Unpack(p);
        if (o.a) p = Pack({c.r, c.g, c.b, o.a});
    }
    return s;
}

// --- 내장 절차 스프라이트(파일 없이) ---

// 7x7 영웅 실루엣 마스크(흰색). Tint 로 클래스 색을 입혀 쓴다. 중앙(4,4) 채워짐.
inline Sprite HeroMask() {
    return MakeSprite({
        "..BBB..",
        "..BBB..",
        ".BBBBB.",
        "BBBBBBB",
        ".BBBBB.",
        ".BB.BB.",
        ".B...B.",
    }, {{'B', {255, 255, 255, 255}}});
}

// 7x7 몬스터(블롭) 마스크. Tint 로 체력 음영 색을 입혀 쓴다. 중앙(3,3) 채워짐.
inline Sprite MonsterMask() {
    return MakeSprite({
        "..MMM..",
        ".MMMMM.",
        "MMMMMMM",
        "MMMMMMM",
        "MMMMMMM",
        ".MMMMM.",
        "..M.M..",
    }, {{'M', {255, 255, 255, 255}}});
}

// 7x6 하트(체력 UI). 빨강.
inline Sprite HeartSprite() {
    return MakeSprite({
        ".RR.RR.",
        "RRRRRRR",
        "RRRRRRR",
        ".RRRRR.",
        "..RRR..",
        "...R...",
    }, {{'R', {220, 40, 40, 255}}});
}

// 6x6 코인(골드 UI). 노랑 테두리 + 밝은 속.
inline Sprite CoinSprite() {
    return MakeSprite({
        ".YYYY.",
        "YWWWWY",
        "YWYYWY",
        "YWYYWY",
        "YWWWWY",
        ".YYYY.",
    }, {{'Y', {200, 160, 30, 255}}, {'W', {250, 220, 90, 255}}});
}

}  // namespace render
}  // namespace mega
