#pragma once

// GlCounters — 프레임당 GL 호출 카운터.
//
// 드로우콜 / 상태 변경 / 바인딩 호출 수를 *Scene 측에서 직접 증가* 시키는 단순
// 모델. glDraw* 호출 전후 후킹 대신 *Scene 이 자기가 호출한 횟수를 알린다*.
//
// 호출 후킹 (GL function pointer 교체) 은 더 정교하지만 *glad2 와 충돌* + *플랫폼
// 의존* — 학습 단계에선 *Scene 이 정직하게 카운트* 가 충분.

#include <cstdint>

namespace gllab {

struct GlCounters {
    int drawCalls         = 0;
    int instancedDraws    = 0;
    int indirectDraws     = 0;
    int stateChanges      = 0;     // glBind* / glUseProgram 등
    int uniformUpdates    = 0;
    int bufferUpdates     = 0;     // glBufferSubData (UBO / SSBO 갱신)
    int barrierCalls      = 0;

    void Reset() {
        drawCalls = 0;
        instancedDraws = 0;
        indirectDraws  = 0;
        stateChanges   = 0;
        uniformUpdates = 0;
        bufferUpdates  = 0;
        barrierCalls   = 0;
    }
};

}  // namespace gllab
