#include "render/msaa_target.h"

#include <algorithm>

namespace gllab {

const char* MsaaName(MsaaSamples s) {
    switch (s) {
        case MsaaSamples::Off: return "Off";
        case MsaaSamples::X2:  return "2x";
        case MsaaSamples::X4:  return "4x";
        case MsaaSamples::X8:  return "8x";
    }
    return "?";
}

int MsaaCount(MsaaSamples s) {
    return (int)s;
}

void MsaaTarget::Init() {
    glGenFramebuffers(1, &fbo_);
    glGenRenderbuffers(1, &colorRb_);
    glGenRenderbuffers(1, &depthRb_);
}

void MsaaTarget::Shutdown() {
    if (fbo_)     glDeleteFramebuffers(1, &fbo_);
    if (colorRb_) glDeleteRenderbuffers(1, &colorRb_);
    if (depthRb_) glDeleteRenderbuffers(1, &depthRb_);
    fbo_ = colorRb_ = depthRb_ = 0;
}

void MsaaTarget::EnsureSize(int width, int height, MsaaSamples samples) {
    width  = std::max(width, 1);
    height = std::max(height, 1);
    // 게이트는 *요청값* 으로 비교한다. current_ 로 비교하면 폴백 후 요청값과
    // 어긋난 상태가 계속돼 매 프레임 Recreate 를 재시도한다.
    if (width == w_ && height == h_ && samples == requested_) return;
    w_ = width; h_ = height; requested_ = samples; current_ = samples;
    if (samples != MsaaSamples::Off) Recreate();
}

bool MsaaTarget::TakeFallbackNotice() {
    bool notice = fallbackNotice_;
    fallbackNotice_ = false;
    return notice;
}

void MsaaTarget::Recreate() {
    int n = MsaaCount(current_);

    glBindRenderbuffer(GL_RENDERBUFFER, colorRb_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, n, GL_RGBA8, w_, h_);

    glBindRenderbuffer(GL_RENDERBUFFER, depthRb_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, n, GL_DEPTH24_STENCIL8,
                                      w_, h_);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_RENDERBUFFER, colorRb_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                               GL_RENDERBUFFER, depthRb_);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        // FBO 가 깨지면 MSAA 비활성으로 폴백 + 호출자에게 한 번 알린다.
        current_        = MsaaSamples::Off;
        requested_      = MsaaSamples::Off;
        fallbackNotice_ = true;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void MsaaTarget::Bind() {
    if (current_ == MsaaSamples::Off) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    }
}

void MsaaTarget::Resolve(int dstW, int dstH) {
    if (current_ == MsaaSamples::Off) return;
    dstW = std::max(dstW, 1);
    dstH = std::max(dstH, 1);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, w_, h_,
                       0, 0, dstW, dstH,
                       GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}  // namespace gllab
