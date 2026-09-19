#include "render/texture_modes.h"

#include "measure/khr_debug.h"

#include <cmath>
#include <string>

namespace gllab {

int BlockBytesFor(TextureMode mode, int width, int height) {
    if (mode == TextureMode::None || width <= 0 || height <= 0) return 0;
    if (mode == TextureMode::Uncompressed) return width * height * 4;
    const int blockBytes = mode == TextureMode::DXT1 ? 8 : 16;
    return ((width + 3) / 4) * ((height + 3) / 4) * blockBytes;
}

const char* TextureModeName(TextureMode m) {
    switch (m) {
        case TextureMode::None:         return "None (lit shader)";
        case TextureMode::Uncompressed: return "RGBA8 (uncompressed)";
        case TextureMode::DXT1:         return "DXT1 / BC1 (RGB, 4 bpp)";
        case TextureMode::DXT5:         return "DXT5 / BC3 (RGBA, 8 bpp)";
        case TextureMode::BC7:          return "BC7 (RGBA, 8 bpp + 고품질)";
    }
    return "?";
}

void TextureRegistry::GenProcedural(int size, std::vector<uint8_t>& rgba) const {
    rgba.resize((size_t)size * (size_t)size * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            // 체크보드 + 색 그라데이션 (sin 파동).
            int cell = ((x / 32) + (y / 32)) & 1;
            float u = (float)x / (float)size;
            float v = (float)y / (float)size;
            float r = u;
            float g = v;
            float b = 0.5f + 0.5f * std::sin((u + v) * 6.28318f);
            if (cell == 0) { r *= 0.7f; g *= 0.7f; b *= 0.7f; }

            size_t off = (size_t)(y * size + x) * 4;
            rgba[off + 0] = (uint8_t)(r * 255.0f);
            rgba[off + 1] = (uint8_t)(g * 255.0f);
            rgba[off + 2] = (uint8_t)(b * 255.0f);
            rgba[off + 3] = 255;
        }
    }
}

void TextureRegistry::Upload(GLuint tex, GLenum internalFormat, int size,
                              const std::vector<uint8_t>& rgba) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internalFormat,
                  size, size, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TextureRegistry::Init(int size, KHRDebug* debug) {
    size_ = size;
    supportsS3tc_ = GLAD_GL_EXT_texture_compression_s3tc != 0;
    supportsBptc_ = GLAD_GL_ARB_texture_compression_bptc != 0;

    std::vector<uint8_t> rgba;
    GenProcedural(size, rgba);

    glGenTextures(1, &texUncompressed_);
    Upload(texUncompressed_, GL_RGBA8, size, rgba);

    if (supportsS3tc_) {
        glGenTextures(1, &texDxt1_);
        Upload(texDxt1_, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, size, rgba);

        glGenTextures(1, &texDxt5_);
        Upload(texDxt5_, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, size, rgba);
    }

    if (supportsBptc_) {
        glGenTextures(1, &texBc7_);
        Upload(texBc7_, GL_COMPRESSED_RGBA_BPTC_UNORM, size, rgba);
    }

    // VRAM 측정 — generic compression 의 실제 결과 크기. mip chain 전체 합산.
    auto MeasureSize = [](GLuint t) -> int {
        // snippet-begin: vram-measure
        glBindTexture(GL_TEXTURE_2D, t);
        int total = 0;
        for (int level = 0;; ++level) {
            GLint w = 0, h = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &w);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &h);
            if (w <= 0 || h <= 0) break;

            GLint comp = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_COMPRESSED, &comp);
            if (!comp) {
                total += w * h * 4;
                continue;
            }

            GLint sz = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D, level,
                                      GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &sz);
            total += sz;
        }
        return total;
        // snippet-end: vram-measure
    };

    bytesUncompressed_ = MeasureSize(texUncompressed_);
    bytesDxt1_         = texDxt1_ ? MeasureSize(texDxt1_) : 0;
    bytesDxt5_         = texDxt5_ ? MeasureSize(texDxt5_) : 0;
    bytesBc7_          = texBc7_  ? MeasureSize(texBc7_)  : 0;
    supportsS3tc_ = supportsS3tc_ &&
                    bytesDxt1_ > 0 && bytesDxt5_ > 0;
    supportsBptc_ = supportsBptc_ && bytesBc7_ > 0;

    // 드라이버가 실제로 잡은 크기와 이 lab 이 문서에 적은 예측 모델이 어긋나면
    // 조용히 넘기지 않고 로그 창에 남긴다 — 측정값과 모델이 갈라지는 순간이
    // 관측 가능해야 패널 수치를 믿을 수 있다.
    if (debug) {
        const TextureMode kModes[] = {
            TextureMode::Uncompressed, TextureMode::DXT1,
            TextureMode::DXT5,         TextureMode::BC7,
        };
        for (TextureMode m : kModes) {
            if (!IsSupported(m)) continue;
            const int measured = BytesFor(m);
            const int expected = ExpectedMipChainBytes(m, size_, size_);
            if (measured != expected) {
                debug->PushAppMessage(
                    std::string("VRAM model mismatch: ") + TextureModeName(m) +
                        " measured " + std::to_string(measured) +
                        " B, model expects " + std::to_string(expected) + " B",
                    DebugMessage::Severity::Medium);
            }
        }
    }
}

void TextureRegistry::Shutdown() {
    if (texUncompressed_) glDeleteTextures(1, &texUncompressed_);
    if (texDxt1_)         glDeleteTextures(1, &texDxt1_);
    if (texDxt5_)         glDeleteTextures(1, &texDxt5_);
    if (texBc7_)          glDeleteTextures(1, &texBc7_);
    texUncompressed_ = texDxt1_ = texDxt5_ = texBc7_ = 0;
}

GLuint TextureRegistry::TextureFor(TextureMode m) const {
    switch (m) {
        case TextureMode::None:         return 0;
        case TextureMode::Uncompressed: return texUncompressed_;
        case TextureMode::DXT1:         return texDxt1_;
        case TextureMode::DXT5:         return texDxt5_;
        case TextureMode::BC7:          return texBc7_;
    }
    return 0;
}

int TextureRegistry::BytesFor(TextureMode m) const {
    switch (m) {
        case TextureMode::None:         return 0;
        case TextureMode::Uncompressed: return bytesUncompressed_;
        case TextureMode::DXT1:         return bytesDxt1_;
        case TextureMode::DXT5:         return bytesDxt5_;
        case TextureMode::BC7:          return bytesBc7_;
    }
    return 0;
}

bool TextureRegistry::IsSupported(TextureMode m) const {
    switch (m) {
        case TextureMode::None:
        case TextureMode::Uncompressed:
            return true;
        case TextureMode::DXT1:
        case TextureMode::DXT5:
            return supportsS3tc_;
        case TextureMode::BC7:
            return supportsBptc_;
    }
    return false;
}

}  // namespace gllab
