// framebuffer — CPU 프레임버퍼(RGBA8). 소프트웨어 래스터라이저의 출력 대상.
//
// GPU 백엔드가 있어도 소프트웨어 래스터라이저를 RHI 의 1급 백엔드로 둔다. Vulkan 이
// 선택 의존이기 때문이다 — CMake 의 find_package(Vulkan QUIET) 가 실패하면
// MEGA_HAS_VULKAN 이 정의되지 않아 platform/vk_present.hpp 가 통째로 비활성이고, 그
// 환경에서도 제품은 빌드되고 돌아야 한다. 게다가 *검증 가능한 백엔드* 는 이쪽뿐이다
// — 픽셀 단위로 결과를 단언할 수 있고, 게임 루프 전체를 창도 장치도 없이 돌려
// (mega_defense --selftest) PPM 으로 떨군다.
//
// GPU 표시 경로는 이 저장소 안에 있다. platform/vk_present.hpp 가 아래 Data() 를 매
// 프레임 텍스처로 올려 스왑체인에 present 하고, ctest 의 mega_defense_gpu_selftest 가
// 그것을 검증한다(장치·창이 없는 환경에서는 실패가 아니라 SKIP). 어느 쪽이든 시뮬과
// 그리기는 같다 — 바뀌는 것은 "표시"뿐이다.

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
