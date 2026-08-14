// 실제 GL 4.5 컨텍스트가 필요한 회귀 테스트.
//
//  (a) 렌더 캡처 회귀 — Individual / Instanced / Multi-Draw Indirect 세 모드가
//      같은 픽셀을 만드는지 오프스크린 FBO + glReadPixels 해시로 대조한다.
//  (b) 자원 수명 — Scene::Rebuild 반복 후 핸들이 유효하고 이름이 누적되지 않는지,
//      GL 에러와 KHR_debug HIGH 메시지가 없는지 확인한다.
//  (c) 동기화/폴백 상태 — MsaaTarget 의 FBO 불완전 폴백이 한 번만 통지되는지.
//
// 컨텍스트를 만들 수 없는 환경(헤드리스 CI)에서는 77 로 종료해 CTest 가 skip 한다.

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "measure/gl_counters.h"
#include "measure/khr_debug.h"
#include "render/lighting_bindings.h"
#include "render/msaa_target.h"
#include "render/shader_program.h"
#include "render/shader_registry.h"
#include "render/texture_modes.h"
#include "scene/scene.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr)                                                          \
    do {                                                                     \
        if (!(expr)) {                                                       \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':' \
                      << __LINE__ << ")\n";                                  \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

constexpr int kCaptureSize  = 256;
constexpr int kObjectCount  = 64;

uint64_t Fnv1a(const std::vector<uint8_t>& bytes) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : bytes) {
        h ^= (uint64_t)b;
        h *= 1099511628211ull;
    }
    return h;
}

struct Offscreen {
    GLuint fbo   = 0;
    GLuint color = 0;
    GLuint depth = 0;

    bool Create(int w, int h) {
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glGenRenderbuffers(1, &depth);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, color, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                   GL_RENDERBUFFER, depth);
        const bool ok =
            glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return ok;
    }

    void Destroy() {
        if (fbo)   glDeleteFramebuffers(1, &fbo);
        if (color) glDeleteTextures(1, &color);
        if (depth) glDeleteRenderbuffers(1, &depth);
        fbo = color = depth = 0;
    }
};

// 한 프레임을 오프스크린에 그리고 픽셀 해시를 돌려준다.
uint64_t CapturePixels(Offscreen& target, gllab::Scene& scene,
                        gllab::ShaderProgram& prog, gllab::DrawMode mode,
                        gllab::GlCounters& counters, bool drawScene) {
    scene.SetDrawMode(mode);

    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, kCaptureSize, kCaptureSize);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    counters.Reset();
    if (drawScene) {
        prog.Use();
        if (prog.SetInt("uShadingModel", 0)) counters.uniformUpdates++;
        scene.Draw(prog, 1.0f, counters);
    }
    glFinish();

    std::vector<uint8_t> pixels((size_t)kCaptureSize * kCaptureSize * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, kCaptureSize, kCaptureSize, GL_RGBA, GL_UNSIGNED_BYTE,
                  pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return Fnv1a(pixels);
}

// (a) 세 드로우 모드의 픽셀이 같은지 + 카운터가 계약대로 움직이는지.
void TestDrawModeCaptureRegression(gllab::ShaderRegistry& shaders) {
    Offscreen target;
    if (!target.Create(kCaptureSize, kCaptureSize)) {
        std::cerr << "CHECK failed: offscreen FBO incomplete\n";
        ++failures;
        return;
    }

    gllab::Scene scene;
    scene.Build(kObjectCount);
    scene.Update(0.25f);   // 회전각을 0 이 아닌 결정적 값으로.

    gllab::ShaderProgram& prog =
        shaders.Pick(gllab::ShaderStyle::Uber, gllab::ShadingModel::Lambert);

    gllab::GlCounters blankCounters;
    const uint64_t blank = CapturePixels(target, scene, prog,
                                          gllab::DrawMode::Individual,
                                          blankCounters, false);

    gllab::GlCounters individual, instanced, indirect;
    const uint64_t hIndividual = CapturePixels(target, scene, prog,
                                                gllab::DrawMode::Individual,
                                                individual, true);
    const uint64_t hInstanced  = CapturePixels(target, scene, prog,
                                                gllab::DrawMode::Instanced,
                                                instanced, true);
    const uint64_t hIndirect   = CapturePixels(target, scene, prog,
                                                gllab::DrawMode::MultiDrawIndirect,
                                                indirect, true);

    std::printf("[capture] blank=%016llx individual=%016llx instanced=%016llx mdi=%016llx\n",
                (unsigned long long)blank, (unsigned long long)hIndividual,
                (unsigned long long)hInstanced, (unsigned long long)hIndirect);

    // 씬이 실제로 그려졌는가 — 이 확인이 없으면 '아무것도 안 그림' 도 3-way 일치다.
    CHECK(hIndividual != blank);

    // 세 모드의 픽셀이 비트 동일한가.
    CHECK(hIndividual == hInstanced);
    CHECK(hIndividual == hIndirect);

    // 제출 수 계약 — 헤더의 예측 함수와 실제 카운터를 맞댄다.
    CHECK(individual.drawCalls ==
          gllab::ExpectedDrawSubmissions(gllab::DrawMode::Individual, kObjectCount));
    CHECK(individual.instancedDraws == 0);
    CHECK(individual.indirectDraws == 0);
    CHECK(scene.LastDrawCalls() == 1);   // 마지막 실행은 MDI.

    CHECK(instanced.instancedDraws ==
          gllab::ExpectedDrawSubmissions(gllab::DrawMode::Instanced, kObjectCount));
    CHECK(instanced.drawCalls == 0);

    CHECK(indirect.indirectDraws ==
          gllab::ExpectedDrawSubmissions(gllab::DrawMode::MultiDrawIndirect,
                                          kObjectCount));
    CHECK(indirect.drawCalls == 0);

    // 이 lab 의 핵심 주장 — 같은 그림에 uniform 갱신은 수십 배 차이가 난다.
    CHECK(individual.uniformUpdates > instanced.uniformUpdates * 10);
    CHECK(instanced.uniformUpdates == indirect.uniformUpdates);

    target.Destroy();
}

// (b) Scene::Rebuild 를 반복해도 GL 이름이 누적되지 않는가 (핸들 누수 검출).
void TestSceneResourceLifetime(gllab::KHRDebug& debug) {
    while (glGetError() != GL_NO_ERROR) {}
    debug.Clear();

    GLuint probeBefore = 0;
    glGenBuffers(1, &probeBefore);
    glDeleteBuffers(1, &probeBefore);

    gllab::Scene scene;
    scene.Build(kObjectCount);
    const gllab::SceneHandles first = scene.Handles();
    CHECK(glIsVertexArray(first.vao) == GL_TRUE);
    CHECK(glIsBuffer(first.vboVerts) == GL_TRUE);
    CHECK(glIsBuffer(first.vboIndices) == GL_TRUE);
    CHECK(glIsBuffer(first.vboInstanceModel) == GL_TRUE);
    CHECK(glIsBuffer(first.vboInstanceColor) == GL_TRUE);
    CHECK(glIsBuffer(first.indirectBuffer) == GL_TRUE);
    CHECK(scene.IndexCount() == 36);

    constexpr int kRebuilds = 1000;
    for (int i = 0; i < kRebuilds; ++i) {
        scene.Rebuild(kObjectCount);
    }

    const gllab::SceneHandles after = scene.Handles();
    CHECK(glIsVertexArray(after.vao) == GL_TRUE);
    CHECK(glIsBuffer(after.vboVerts) == GL_TRUE);
    CHECK(glIsBuffer(after.vboIndices) == GL_TRUE);
    CHECK(glIsBuffer(after.vboInstanceModel) == GL_TRUE);
    CHECK(glIsBuffer(after.vboInstanceColor) == GL_TRUE);
    CHECK(glIsBuffer(after.indirectBuffer) == GL_TRUE);
    CHECK(scene.ObjectCount() == kObjectCount);

    // 해제가 빠지면 드라이버가 새 이름을 계속 발급해 값이 누적된다.
    // (1000 회 × 버퍼 5 개면 누수 시 이름이 최소 수천 단위로 올라간다.)
    GLuint probeAfter = 0;
    glGenBuffers(1, &probeAfter);
    glDeleteBuffers(1, &probeAfter);
    const GLuint maxHandle = std::max({after.vboVerts, after.vboIndices,
                                       after.vboInstanceModel,
                                       after.vboInstanceColor,
                                       after.indirectBuffer, probeAfter});
    std::printf("[lifetime] probeBefore=%u probeAfter=%u maxHandle=%u after %d rebuilds\n",
                probeBefore, probeAfter, maxHandle, kRebuilds);
    CHECK(maxHandle < 128u);
    CHECK(probeAfter <= probeBefore + 16u);

    CHECK(glGetError() == GL_NO_ERROR);

    int high = 0;
    for (int i = 0; i < debug.Count(); ++i) {
        if (debug.Get(i).severity == gllab::DebugMessage::Severity::High) ++high;
    }
    CHECK(high == 0);
}

// (c) MSAA FBO 폴백 — 성공 경로와 강제 실패 경로.
void TestMsaaFallback() {
    while (glGetError() != GL_NO_ERROR) {}

    {
        gllab::MsaaTarget msaa;
        msaa.Init();
        msaa.EnsureSize(kCaptureSize, kCaptureSize, gllab::MsaaSamples::X4);
        CHECK(msaa.Current() == gllab::MsaaSamples::X4);
        CHECK(msaa.Requested() == gllab::MsaaSamples::X4);
        CHECK(msaa.Enabled());
        CHECK(!msaa.TakeFallbackNotice());
        msaa.Bind();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        msaa.Resolve(kCaptureSize, kCaptureSize);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        CHECK(glGetError() == GL_NO_ERROR);
        msaa.Shutdown();
    }

    // 강제 실패 — GL_MAX_RENDERBUFFER_SIZE 를 넘기면 storage 가 잡히지 않아
    // 첨부가 0×0 으로 남고 FBO 가 불완전해진다.
    GLint maxRb = 0;
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRb);
    CHECK(maxRb > 0);
    {
        gllab::MsaaTarget msaa;
        msaa.Init();
        msaa.EnsureSize(maxRb + 1, maxRb + 1, gllab::MsaaSamples::X8);
        CHECK(msaa.Current() == gllab::MsaaSamples::Off);
        CHECK(msaa.Requested() == gllab::MsaaSamples::Off);
        CHECK(!msaa.Enabled());
        // 통지는 정확히 한 번만 소비된다.
        CHECK(msaa.TakeFallbackNotice());
        CHECK(!msaa.TakeFallbackNotice());
        msaa.Shutdown();
    }
    while (glGetError() != GL_NO_ERROR) {}   // 강제 실패로 생긴 에러 비우기.
}

// (d) LightingBindings::Apply 의 실제 카운터가 GL 없는 모델과 일치하는가.
void TestBindingCountersMatchModel(gllab::ShaderRegistry& shaders) {
    while (glGetError() != GL_NO_ERROR) {}

    gllab::LightingBindings lighting;
    lighting.Init(64);
    lighting.Update(1.0f);

    const gllab::BindingMode modes[] = {
        gllab::BindingMode::PerCallUniform,
        gllab::BindingMode::UBO,
        gllab::BindingMode::SSBO,
    };
    for (gllab::BindingMode mode : modes) {
        for (int n : {8, 32, 64}) {
            lighting.SetCount(n);
            CHECK(lighting.Count() == n);
            gllab::ShaderProgram& prog = shaders.PickLit(mode);
            prog.Use();
            gllab::GlCounters counters;
            counters.Reset();
            lighting.Apply(mode, prog, counters);

            const auto model = gllab::CountBindingCost(mode, n);
            if (counters.uniformUpdates != model.uniformUpdates ||
                counters.bufferUpdates != model.bufferUpdates) {
                std::printf("[binding] mode=%s n=%d runtime(u=%d,b=%d) model(u=%d,b=%d)\n",
                            gllab::BindingModeName(mode), n,
                            counters.uniformUpdates, counters.bufferUpdates,
                            model.uniformUpdates, model.bufferUpdates);
            }
            CHECK(counters.uniformUpdates == model.uniformUpdates);
            CHECK(counters.bufferUpdates == model.bufferUpdates);
        }
    }
    CHECK(glGetError() == GL_NO_ERROR);
    lighting.Shutdown();
}

// (e) 드라이버가 보고한 mip chain 크기와 예측 모델의 관계.
void TestTextureVram(gllab::KHRDebug& debug) {
    while (glGetError() != GL_NO_ERROR) {}
    debug.Clear();

    gllab::TextureRegistry textures;
    textures.Init(256, &debug);

    const int raw = textures.BytesFor(gllab::TextureMode::Uncompressed);
    CHECK(raw > 0);
    CHECK(raw == gllab::ExpectedMipChainBytes(gllab::TextureMode::Uncompressed,
                                               256, 256));

    int mismatches = 0;
    for (gllab::TextureMode m : {gllab::TextureMode::Uncompressed,
                                 gllab::TextureMode::DXT1,
                                 gllab::TextureMode::DXT5,
                                 gllab::TextureMode::BC7}) {
        if (!textures.IsSupported(m)) {
            std::printf("[vram] %s unsupported on this driver — skipped\n",
                        gllab::TextureModeName(m));
            continue;
        }
        const int measured = textures.BytesFor(m);
        const int model    = gllab::ExpectedMipChainBytes(m, 256, 256);
        std::printf("[vram] %s measured=%d model=%d\n", gllab::TextureModeName(m),
                    measured, model);
        CHECK(measured > 0);
        if (m != gllab::TextureMode::Uncompressed) {
            CHECK(measured < raw);   // 압축은 반드시 더 작다.
        }
        CHECK(measured == model);
        if (measured != model) ++mismatches;
    }

    // 드라이버 측정값과 모델이 어긋나면 Init 이 로그를 남겨야 한다 — 어긋난 수와
    // 남은 통지 수가 정확히 같아야 조용한 폴백이 없다.
    int notices = 0;
    for (int i = 0; i < debug.Count(); ++i) {
        if (debug.Get(i).text.rfind("VRAM model mismatch", 0) == 0) ++notices;
    }
    std::printf("[vram] mismatches=%d notices=%d\n", mismatches, notices);
    CHECK(notices == mismatches);

    textures.Shutdown();
    CHECK(glGetError() == GL_NO_ERROR);
}

int Skip(const char* why) {
    std::cout << "SKIP: " << why << "\n";
    return 77;
}

}  // namespace

int main() {
    glfwSetErrorCallback([](int code, const char* desc) {
        std::fprintf(stderr, "[glfw %d] %s\n", code, desc);
    });
    if (!glfwInit()) return Skip("glfwInit failed (no display?)");

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(64, 64, "graphics_gl_render_tests",
                                           nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return Skip("no OpenGL 4.5 core context available");
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return Skip("gladLoadGL failed");
    }

    std::printf("[gl] vendor=%s renderer=%s version=%s\n",
                (const char*)glGetString(GL_VENDOR),
                (const char*)glGetString(GL_RENDERER),
                (const char*)glGetString(GL_VERSION));

    gllab::KHRDebug debug;
    debug.Install();

    try {
        gllab::ShaderRegistry shaders;
        shaders.Init();

        TestDrawModeCaptureRegression(shaders);
        TestSceneResourceLifetime(debug);
        TestMsaaFallback();
        TestBindingCountersMatchModel(shaders);
        TestTextureVram(debug);
    } catch (const std::exception& e) {
        std::cerr << "exception: " << e.what() << "\n";
        ++failures;
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "OpenGL render tests passed\n";
    return 0;
}
