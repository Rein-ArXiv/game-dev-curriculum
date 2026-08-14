// framebuffer — CPU 프레임버퍼(RGBA8). 소프트웨어 래스터라이저의 출력 대상.
//
// 이 머신엔 Vulkan SDK 가 없어 GPU 백엔드를 빌드·검증할 수 없다. 그래서 RHI 의
// *검증 가능한 백엔드* 로 소프트웨어 래스터라이저를 1급으로 둔다 — 픽셀 단위로
// 결과를 단언할 수 있고, 게임 루프 전체를 GPU 없이 돌려 PPM 으로 떨군다. Vulkan/
// OpenGL 백엔드는 같은 인터페이스 뒤 교체 지점으로 남긴다(graphics_vk_lab 검증).

#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

namespace mega {
namespace render {

struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

inline uint32_t Pack(Color c) {
    return (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16) | ((uint32_t)c.a << 24);
}
inline Color Unpack(uint32_t v) {
    return {(uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF), (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 24) & 0xFF)};
}

class Framebuffer {
public:
    void Init(int w, int h) {
        w_ = w; h_ = h;
        px_.assign((size_t)w * h, 0xFF000000u);   // 불투명 검정
    }

    int  Width()  const { return w_; }
    int  Height() const { return h_; }

    void Clear(Color c) {
        uint32_t v = Pack(c);
        for (auto& p : px_) p = v;
    }

    void Set(int x, int y, Color c) {
        if (x < 0 || x >= w_ || y < 0 || y >= h_) return;
        px_[(size_t)y * w_ + x] = Pack(c);
    }

    // 알파 블렌딩 set(src over dst).
    void Blend(int x, int y, Color c) {
        if (x < 0 || x >= w_ || y < 0 || y >= h_) return;
        if (c.a == 255) { px_[(size_t)y * w_ + x] = Pack(c); return; }
        if (c.a == 0) return;
        Color d = Unpack(px_[(size_t)y * w_ + x]);
        float a = c.a / 255.0f, ia = 1.0f - a;
        Color o{(uint8_t)(c.r * a + d.r * ia), (uint8_t)(c.g * a + d.g * ia),
                (uint8_t)(c.b * a + d.b * ia), 255};
        px_[(size_t)y * w_ + x] = Pack(o);
    }

    Color Get(int x, int y) const {
        if (x < 0 || x >= w_ || y < 0 || y >= h_) return {};
        return Unpack(px_[(size_t)y * w_ + x]);
    }

    // 원시 픽셀 버퍼(0xAABBGGRR, 행 우선). 창 표시(blit) 등 외부 표현용.
    const uint32_t* Data() const { return px_.data(); }

    // P6 바이너리 PPM 으로 저장(RGB). 시각 확인용.
    bool SavePPM(const char* path) const {
        std::FILE* f = std::fopen(path, "wb");
        if (!f) return false;
        std::fprintf(f, "P6\n%d %d\n255\n", w_, h_);
        std::vector<uint8_t> row((size_t)w_ * 3);
        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                Color c = Unpack(px_[(size_t)y * w_ + x]);
                row[(size_t)x * 3 + 0] = c.r;
                row[(size_t)x * 3 + 1] = c.g;
                row[(size_t)x * 3 + 2] = c.b;
            }
            std::fwrite(row.data(), 1, row.size(), f);
        }
        std::fclose(f);
        return true;
    }

private:
    int                   w_ = 0, h_ = 0;
    std::vector<uint32_t> px_;
};

}  // namespace render
}  // namespace mega
