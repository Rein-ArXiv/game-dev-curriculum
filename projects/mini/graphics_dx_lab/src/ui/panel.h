#pragma once

#include "scene/scene.h"
#include "rootsig/root_signature.h"
#include "pso/pso_cache.h"
#include "descriptor/heap_manager.h"
#include "barrier/barrier_helpers.h"

namespace dxlab {

class FrameTimer;
class TimestampQuery;
class D3D12Debug;

class Panel {
public:
    void Draw(const FrameTimer& cpuTimer,
               const TimestampQuery& gpuTimer,
               const D3D12Debug& debug,
               const Scene& scene,
               const GlCounters& counters,
               DrawMode& drawMode,
               PsoSwitchPolicy& psoPolicy,
               RootSigPolicy& rsPolicy,
               HeapPolicy& heapPolicy,
               BarrierPolicy& barrierPolicy,
               bool& useBlinnPhong,
               int& pendingObjectCount,
               bool& vsync);
};

}  // namespace dxlab
