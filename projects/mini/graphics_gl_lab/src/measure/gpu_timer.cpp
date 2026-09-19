#include "measure/gpu_timer.h"

namespace gllab {

void GpuTimer::Init() {
    if (initialized_) return;
    glGenQueries(kRingSize, queries_);
    writeIdx_ = readIdx_ = inFlight_ = 0;     // Shutdown 후 재-Init 시 ring 상태 복원.
    active_   = false;
    history_.fill(0.0f);
    initialized_ = true;
}

void GpuTimer::Shutdown() {
    if (!initialized_) return;
    glDeleteQueries(kRingSize, queries_);
    initialized_ = false;
}

void GpuTimer::Begin() {
    if (!initialized_) return;
    if (inFlight_ == kRingSize) Poll();
    if (inFlight_ == kRingSize) return;     // GPU 가 밀리면 이 프레임 측정은 건너뜀.
    glBeginQuery(GL_TIME_ELAPSED, queries_[writeIdx_]);
    active_ = true;
}

void GpuTimer::End() {
    if (!initialized_ || !active_) return;
    glEndQuery(GL_TIME_ELAPSED);
    active_ = false;
    ++inFlight_;
    writeIdx_ = (writeIdx_ + 1) % kRingSize;
}

void GpuTimer::Poll() {
    if (!initialized_) return;
    if (inFlight_ == 0) return;

    GLuint available = GL_FALSE;
    glGetQueryObjectuiv(queries_[readIdx_], GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available) return;

    GLuint64 ns = 0;
    glGetQueryObjectui64v(queries_[readIdx_], GL_QUERY_RESULT, &ns);
    readIdx_ = (readIdx_ + 1) % kRingSize;
    --inFlight_;

    lastMs_ = (double)ns / 1.0e6;
    if (emaMs_ <= 0.0) emaMs_ = lastMs_;
    else                emaMs_ = emaMs_ * (1.0 - kEmaAlpha) + lastMs_ * kEmaAlpha;

    history_[(size_t)historyHead_] = (float)lastMs_;
    historyHead_ = (historyHead_ + 1) % kHistorySize;
}

}  // namespace gllab
