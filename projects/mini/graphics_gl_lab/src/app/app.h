#pragma once

// App — graphics_gl_lab 의 메인 애플리케이션.
//
// Step 1: 윈도우 + Scene + 기본 셰이더.
// Step 2: + GpuTimer + KHRDebug + GlCounters.
// Step 3: + Scene 의 DrawMode 토글.
// Step 4: + ShaderRegistry (uber vs static permutation) + ShadingModel.
// Step 5~7: + binding mode + texture compression + scale/MSAA.

#include "measure/gl_counters.h"
#include "render/shader_registry.h"
#include "render/lighting_bindings.h"
#include "render/texture_modes.h"
#include "render/msaa_target.h"

#include <memory>

struct GLFWwindow;

namespace gllab {

class Scene;
class FrameTimer;
class GpuTimer;
class KHRDebug;
class Panel;

class App {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run();

private:
    void InitWindow();
    void InitGL();
    void InitImGui();
    void InitScene();

    void Shutdown();

    void ProcessFrame();
    void Render();
    void RenderUI();

    GLFWwindow*                       window_ = nullptr;
    int                               windowW_ = 1280;
    int                               windowH_ = 720;
    std::unique_ptr<ShaderRegistry>   shaders_;
    std::unique_ptr<LightingBindings> lighting_;
    std::unique_ptr<TextureRegistry>  textures_;
    std::unique_ptr<MsaaTarget>       msaa_;
    std::unique_ptr<Scene>            scene_;
    std::unique_ptr<FrameTimer>       timer_;
    std::unique_ptr<GpuTimer>         gpuTimer_;
    std::unique_ptr<KHRDebug>         khrDebug_;
    std::unique_ptr<Panel>            panel_;
    GlCounters                        counters_{};

    bool wireframe_   = false;
    bool vsyncOn_     = false;
    bool prevVsyncOn_ = false;

    // Step 4 토글.
    ShaderStyle  shaderStyle_  = ShaderStyle::Uber;
    ShadingModel shadingModel_ = ShadingModel::Lambert;

    // Step 5 토글.
    bool        useLighting_   = false;
    BindingMode bindingMode_   = BindingMode::UBO;
    int         lightCount_    = 32;

    // Step 6 토글.
    TextureMode textureMode_   = TextureMode::None;

    // Step 7 토글.
    int         objectCount_   = 10000;     // 100 / 1000 / 10000 / 100000
    int         pendingCount_  = 10000;     // 사용자가 슬라이더 변경 — Apply 시 적용.
    MsaaSamples msaa_samples_  = MsaaSamples::Off;
};

}  // namespace gllab
