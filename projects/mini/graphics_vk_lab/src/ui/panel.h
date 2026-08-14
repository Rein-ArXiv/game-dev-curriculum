#pragma once

// Panel — ImGui 측정/토글 패널.

#include "scene/scene.h"             // DrawMode, GlCounters
#include "barrier/barrier_helpers.h" // BarrierPolicy
#include "sync/timeline_sync.h"      // SyncMode

#include <cstdint>

namespace vklab {

class FrameTimer;
class TimestampQuery;

struct PanelIO {
    // 입력 — 지표.
    const FrameTimer*     cpuTimer = nullptr;
    const TimestampQuery* gpuTimer = nullptr;
    const GlCounters*     counters = nullptr;
    int                   objectCount = 0;
    uint64_t              timelineValue = 0;

    // 입출력 — 토글 (포인터로 직접 갱신).
    DrawMode*      drawMode = nullptr;
    BarrierPolicy* barrierPolicy = nullptr;
    SyncMode*      syncMode = nullptr;
    bool*          useBlinnPhong = nullptr;
    bool*          vsync = nullptr;
    int*           pendingCount = nullptr;

    // 의도적 실패 주입 토글 — Debug 빌드에서 validation 메시지를 관측하기 위한 것.
    BarrierInjection* injection = nullptr;
};

class Panel {
public:
    void Draw(const PanelIO& io);
};

}  // namespace vklab
