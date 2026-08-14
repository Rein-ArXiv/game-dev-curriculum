#pragma once

// MsaaTarget — Step 7 의 MSAA off / 2x / 4x / 8x 토글용.
//
// MSAA off 면 기본 framebuffer 사용. MSAA on 이면 *별도 multisample FBO* 에
// 렌더 후 *resolve* (blit) 로 default framebuffer 에 복사.

#include <glad/gl.h>

namespace gllab {

enum class MsaaSamples {
    Off = 0,
    X2  = 2,
    X4  = 4,
    X8  = 8,
};

const char* MsaaName(MsaaSamples s);
int  MsaaCount(MsaaSamples s);

class MsaaTarget {
public:
    void Init();
    void Shutdown();

    // 윈도우 크기 / MSAA 옵션 변경 시 호출. 변경 사항 없으면 no-op.
    void EnsureSize(int width, int height, MsaaSamples samples);

    void Bind();          // 이 FBO 에 렌더.
    void Resolve(int dstW, int dstH);   // default framebuffer 로 blit.

    MsaaSamples Current() const { return current_; }
    MsaaSamples Requested() const { return requested_; }
    bool Enabled() const { return current_ != MsaaSamples::Off; }

    // 직전 EnsureSize 가 FBO 불완전으로 Off 폴백했으면 true 를 *한 번만* 반환.
    // 호출자가 요청값을 Off 로 되돌려 UI 표시와 실제 렌더 상태를 맞춘다.
    bool TakeFallbackNotice();

private:
    void Recreate();

    GLuint fbo_     = 0;
    GLuint colorRb_ = 0;
    GLuint depthRb_ = 0;

    int         w_ = 0;
    int         h_ = 0;
    MsaaSamples current_   = MsaaSamples::Off;   // 실제 렌더에 쓰이는 값.
    MsaaSamples requested_ = MsaaSamples::Off;   // 마지막으로 요청받은 값.
    bool        fallbackNotice_ = false;
};

}  // namespace gllab
