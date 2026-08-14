#include "app/app.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include "render/shader_program.h"
#include "render/shader_registry.h"
#include "render/lighting_bindings.h"
#include "render/texture_modes.h"
#include "render/msaa_target.h"
#include "scene/scene.h"
#include "measure/frame_timer.h"
#include "measure/gpu_timer.h"
#include "measure/khr_debug.h"
#include "ui/panel.h"

#include <stdexcept>
#include <cstdio>
#include <algorithm>

namespace gllab {

namespace {

void GLFWErrorCallback(int code, const char* desc) {
    std::fprintf(stderr, "[glfw %d] %s\n", code, desc);
}

}  // namespace

App::App()
    : shaders_(std::make_unique<ShaderRegistry>()),
      lighting_(std::make_unique<LightingBindings>()),
      textures_(std::make_unique<TextureRegistry>()),
      msaa_(std::make_unique<MsaaTarget>()),
      timer_(std::make_unique<FrameTimer>()),
      gpuTimer_(std::make_unique<GpuTimer>()),
      khrDebug_(std::make_unique<KHRDebug>()),
      panel_(std::make_unique<Panel>()) {
    InitWindow();
    InitGL();
    InitImGui();
    InitScene();
}

App::~App() {
    Shutdown();
}

void App::InitWindow() {
    glfwSetErrorCallback(&GLFWErrorCallback);
    if (!glfwInit()) {
        throw std::runtime_error("glfwInit failed");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif

    window_ = glfwCreateWindow(windowW_, windowH_, "graphics_gl_lab", nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed");
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(vsyncOn_ ? 1 : 0);
}

void App::InitGL() {
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        throw std::runtime_error("gladLoadGL failed");
    }
    glViewport(0, 0, windowW_, windowH_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.05f, 0.05f, 0.07f, 1.0f);

    khrDebug_->Install();
    gpuTimer_->Init();
    msaa_->Init();
}

void App::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 450 core");
}

void App::InitScene() {
    shaders_->Init();
    lighting_->Init(64);
    textures_->Init(512, khrDebug_.get());
    scene_ = std::make_unique<Scene>();
    scene_->Build(objectCount_);
}

void App::Shutdown() {
    if (gpuTimer_) gpuTimer_->Shutdown();
    if (lighting_) lighting_->Shutdown();
    if (textures_) textures_->Shutdown();
    if (msaa_)     msaa_->Shutdown();
    scene_.reset();
    shaders_.reset();
    lighting_.reset();
    textures_.reset();
    msaa_.reset();

    if (ImPlot::GetCurrentContext()) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

int App::Run() {
    while (!glfwWindowShouldClose(window_)) {
        timer_->BeginFrame();
        glfwPollEvents();
        ProcessFrame();
        Render();
        RenderUI();
        glfwSwapBuffers(window_);
        timer_->EndFrame();
    }
    return 0;
}

void App::ProcessFrame() {
    int w, h;
    glfwGetFramebufferSize(window_, &w, &h);
    const int safeW = std::max(w, 1);
    const int safeH = std::max(h, 1);
    if (safeW != windowW_ || safeH != windowH_) {
        windowW_ = safeW; windowH_ = safeH;
        glViewport(0, 0, safeW, safeH);
    }

    // Step 7: pendingCount_ 가 적용 대기 중이면 Scene 재빌드.
    if (pendingCount_ != objectCount_) {
        objectCount_ = pendingCount_;
        scene_->Rebuild(objectCount_);
    }

    // Step 7: MSAA FBO 크기 / 샘플 수 동기.
    msaa_->EnsureSize(windowW_, windowH_, msaa_samples_);
    if (msaa_->TakeFallbackNotice()) {
        // FBO 불완전 → 요청값도 Off 로 되돌려 패널 표시와 실제 렌더 상태를 맞춘다.
        msaa_samples_ = MsaaSamples::Off;
        khrDebug_->PushAppMessage(
            "MSAA framebuffer incomplete — fell back to Off",
            DebugMessage::Severity::Medium);
    }

    scene_->Update(timer_->LastDeltaSeconds());
}

void App::Render() {
    msaa_->Bind();              // MSAA on 이면 multisample FBO, off 면 default.
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPolygonMode(GL_FRONT_AND_BACK, wireframe_ ? GL_LINE : GL_FILL);

    counters_.Reset();
    gpuTimer_->Begin();

    // 셰이더 선택 — 우선순위:
    //   Step 6 textureMode_ != None  → Textured shader (디퓨즈 + Lambert)
    //   Step 5 useLighting_         → Lit shader (광원 배열)
    //   Step 4 기본                  → Uber 또는 Permutation
    ShaderProgram* prog = nullptr;
    if (textureMode_ != TextureMode::None) {
        prog = &shaders_->Textured();
    } else if (useLighting_) {
        prog = &shaders_->PickLit(bindingMode_);
        lighting_->SetCount(lightCount_);
        lighting_->Update((float)glfwGetTime());
    } else {
        prog = &shaders_->Pick(shaderStyle_, shadingModel_);
    }
    prog->Use();
    if (shaders_->ProgramChangedSinceLast(*prog)) {
        counters_.stateChanges++;
    }
    if (prog->SetInt("uShadingModel", (int)shadingModel_)) counters_.uniformUpdates++;

    if (textureMode_ != TextureMode::None) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textures_->TextureFor(textureMode_));
        counters_.stateChanges++;
        if (prog->SetInt("uDiffuse", 0)) counters_.uniformUpdates++;
    } else if (useLighting_) {
        lighting_->Apply(bindingMode_, *prog, counters_);
    }

    scene_->Draw(*prog, (float)windowW_ / (float)std::max(windowH_, 1), counters_);

    gpuTimer_->End();
    gpuTimer_->Poll();

    msaa_->Resolve(windowW_, windowH_);  // MSAA on 이면 default FB 로 blit.
}

void App::RenderUI() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    panel_->Draw(*timer_, *gpuTimer_, *khrDebug_,
                  *scene_, counters_,
                  shaderStyle_, shadingModel_,
                  useLighting_, bindingMode_, lightCount_,
                  textureMode_, *textures_,
                  pendingCount_, msaa_samples_,
                  wireframe_, vsyncOn_);

    if (vsyncOn_ != prevVsyncOn_) {
        glfwSwapInterval(vsyncOn_ ? 1 : 0);
        prevVsyncOn_ = vsyncOn_;
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

}  // namespace gllab
