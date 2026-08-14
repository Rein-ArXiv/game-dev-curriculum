#pragma once

#include "render/shader_registry.h"
#include "render/lighting_bindings.h"
#include "render/texture_modes.h"
#include "render/msaa_target.h"

namespace gllab {

class FrameTimer;
class GpuTimer;
class KHRDebug;
class Scene;
class TextureRegistry;
struct GlCounters;

class Panel {
public:
    void Draw(const FrameTimer& cpuTimer,
               const GpuTimer&   gpuTimer,
               const KHRDebug&   khrDebug,
               Scene&            scene,
               const GlCounters& counters,
               ShaderStyle&      style,
               ShadingModel&     model,
               bool&             useLighting,
               BindingMode&      bindingMode,
               int&              lightCount,
               TextureMode&      textureMode,
               const TextureRegistry& textures,
               int&              pendingObjectCount,
               MsaaSamples&      msaa,
               bool& wireframe,
               bool& vsyncOn);
};

}  // namespace gllab
