#pragma once

// TextureRegistry — Step 6 의 텍스처 압축 토글용.
//
// 같은 512×512 프로시저 텍스처를 *네 internalFormat* 으로 GPU 에 업로드.
// 토글 시 셰이더가 다른 텍스처 sampler 사용.
//
// 압축은 *드라이버 측 generic compression* — 학습 단계의 단순함.
// 실제 게임은 오프라인 인코더 (texconv / NVTT / ISPC) 출력을 .dds 로 로드.
//
// VRAM 측정 — mip level 별 합산. 압축 level 은 `GL_TEXTURE_COMPRESSED_IMAGE_SIZE`,
// 비압축 level 은 width × height × 4.

#include <glad/gl.h>

#include <vector>
#include <cstdint>

namespace gllab {

enum class TextureMode {
    None = 0,         // 텍스처 사용 안 함 (Step 5 의 lit 셰이더).
    Uncompressed,     // GL_RGBA8 — 4 B / pixel (+ mip chain)
    DXT1,             // BC1 (RGB) — 4 bpp (+ mip chain)
    DXT5,             // BC3 (RGBA) — 8 bpp (+ mip chain)
    BC7,              // BC7 (RGBA) — 8 bpp + 고품질 (+ mip chain)
};

class KHRDebug;

const char* TextureModeName(TextureMode m);

// mip level 하나가 차지하는 바이트. 압축 포맷은 4×4 블록 단위로 올림한다.
// GL 호출이 없는 순수 함수 — mip chain 합산이 이 함수만 반복 호출하므로 블록
// 계산을 바꾸면 예측값 전체가 함께 무너진다.
int BlockBytesFor(TextureMode mode, int width, int height);

inline int ExpectedMipChainBytes(TextureMode mode, int width, int height) {
    if (mode == TextureMode::None || width <= 0 || height <= 0) return 0;
    int total = 0;
    for (;;) {
        total += BlockBytesFor(mode, width, height);
        if (width == 1 && height == 1) break;
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    return total;
}

class TextureRegistry {
public:
    // debug 가 주어지면 드라이버가 보고한 mip chain 크기와 ExpectedMipChainBytes
    // 예측값이 어긋날 때 로그 창에 한 줄씩 남긴다.
    void Init(int size = 512, KHRDebug* debug = nullptr);
    void Shutdown();

    GLuint TextureFor(TextureMode m) const;
    int    BytesFor(TextureMode m)   const;     // VRAM 측정 바이트 수.
    bool   IsSupported(TextureMode m) const;

private:
    void GenProcedural(int size, std::vector<uint8_t>& rgba) const;
    void Upload(GLuint tex, GLenum internalFormat, int size,
                 const std::vector<uint8_t>& rgba);

    int    size_ = 512;
    GLuint texUncompressed_ = 0;
    GLuint texDxt1_         = 0;
    GLuint texDxt5_         = 0;
    GLuint texBc7_          = 0;

    int bytesUncompressed_ = 0;
    int bytesDxt1_         = 0;
    int bytesDxt5_         = 0;
    int bytesBc7_          = 0;
    bool supportsS3tc_      = false;
    bool supportsBptc_      = false;
};

}  // namespace gllab
