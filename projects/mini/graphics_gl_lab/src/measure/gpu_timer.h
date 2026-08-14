#pragma once

// GpuTimer — GL_TIME_ELAPSED 쿼리 래퍼.
//
// 사용 패턴:
//   gpuTimer.Begin();
//   <GL draw calls>
//   gpuTimer.End();
//   ... 다음 프레임 ...
//   double ms = gpuTimer.LastMs();    // 결과 회수 (지연 1~2 프레임)
//
// GL_TIME_ELAPSED 쿼리는 query 구간 *전체 GPU 시간* 을 반환한다. 셰이더 stage
// (vertex / geometry / fragment) 별 분리는 *불가능* — 그건 NVIDIA Nsight /
// AMD RGP 같은 벤더 프로파일러의 영역.
//
// CPU-GPU 동기화 비용을 피하기 위해 N 프레임 ring buffer 패턴을 쓴다 — 결과를
// *2~3 프레임 후에 회수*. 매 프레임 결과를 받지만 약간 지연됨.

#include <glad/gl.h>

#include <array>
#include <cstdint>

namespace gllab {

class GpuTimer {
public:
    static constexpr int kRingSize     = 3;
    static constexpr int kHistorySize  = 240;

    void Init();
    void Shutdown();

    void Begin();
    void End();

    // 결과 회수 — 가장 오래된 ring slot 의 결과를 받아 history 에 push.
    // CPU stall 없이 처리 가능 (이미 GPU 가 완료한 쿼리).
    void Poll();

    double LastMs()   const { return lastMs_; }
    double EmaMs()    const { return emaMs_; }

    const std::array<float, kHistorySize>& History() const { return history_; }
    int HistoryHead() const { return historyHead_; }

private:
    GLuint queries_[kRingSize] = {0, 0, 0};
    int    writeIdx_           = 0;
    int    readIdx_            = 0;
    int    inFlight_           = 0;     // 결과 대기 중인 쿼리 수.
    bool   active_             = false;

    double lastMs_ = 0.0;
    double emaMs_  = 0.0;
    static constexpr double kEmaAlpha = 0.1;

    std::array<float, kHistorySize> history_{};
    int historyHead_ = 0;
    bool initialized_ = false;
};

}  // namespace gllab
