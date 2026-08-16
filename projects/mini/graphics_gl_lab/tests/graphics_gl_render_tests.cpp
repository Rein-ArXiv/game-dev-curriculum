// 실제 GL 4.5 컨텍스트가 필요한 회귀 테스트.
//
//  (a) 렌더 캡처 회귀 — Individual / Instanced / Multi-Draw Indirect 세 모드가
//      같은 픽셀을 만드는지 오프스크린 FBO + glReadPixels 해시로 대조한다.
//  (b) 자원 수명 — Scene::Rebuild 반복 후 핸들이 유효하고 이름이 누적되지 않는지,
//      GL 에러와 KHR_debug HIGH 메시지가 없는지 확인한다.
//  (c) 동기화/폴백 상태 — MsaaTarget 의 FBO 불완전 폴백이 한 번만 통지되는지.
//  (d) 바인딩 비용 — LightingBindings::Apply 의 실제 카운터가 GL 없는 예측
//      모델 (CountBindingCost) 과 일치하는지.
//  (e) 텍스처 VRAM — 드라이버가 보고한 mip chain 크기가 예측 모델과 일치하고,
//      어긋나면 Init 이 통지를 남기는지.
//  (f) 셰이더 perm — uber 한 벌과 static 세 벌의 프로그램 동일성/분리, 그리고
//      프로그램 전환 감지.
//
// 컨텍스트를 만들 수 없는 환경(헤드리스 CI)에서는 77 로 종료해 CTest 가 skip 한다.
//
// 실행 단위는 *step 입도* 다. 인자를 주면 그 step 이 추가한 검증만 돈다.
//
//     graphics_gl_render_tests            — 전체
//     graphics_gl_render_tests step07     — step07 이 추가한 것만
//     graphics_gl_render_tests msaa_fallback
//
// CMakeLists 의 add_test 가 이 인자로 step 별 ctest 항목을 만든다. 전체를 도는
// 항목(`graphics_gl_render_tests`)은 그대로 남아 있다.

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
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

int failures = 0;

// 컨텍스트가 만들어진 뒤 한 번만 초기화되는 것들. 각 검증은 이것만 받는다.
struct Context {
    gllab::ShaderRegistry& shaders;
    gllab::KHRDebug&       debug;
};

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
void TestDrawModeCaptureRegression(Context& ctx) {
    gllab::ShaderRegistry& shaders = ctx.shaders;
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
void TestSceneResourceLifetime(Context& ctx) {
    gllab::KHRDebug& debug = ctx.debug;
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
void TestMsaaFallback(Context&) {
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
void TestBindingCountersMatchModel(Context& ctx) {
    gllab::ShaderRegistry& shaders = ctx.shaders;
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

// (f) step04 의 셰이더 perm — uber 는 한 프로그램, static 은 모델마다 다른
// 프로그램. 분기의 소재지(uShadingModel uniform)와 프로그램 전환 감지까지 본다.
void TestShaderPermutationPrograms(Context& ctx) {
    gllab::ShaderRegistry& shaders = ctx.shaders;
    while (glGetError() != GL_NO_ERROR) {}

    // uber 는 모델을 바꿔도 같은 프로그램이다 — 전환 비용이 0 이라는 주장의 근거.
    gllab::ShaderProgram& uberLambert =
        shaders.Pick(gllab::ShaderStyle::Uber, gllab::ShadingModel::Lambert);
    gllab::ShaderProgram& uberBlinn =
        shaders.Pick(gllab::ShaderStyle::Uber, gllab::ShadingModel::BlinnPhong);
    gllab::ShaderProgram& uberCel =
        shaders.Pick(gllab::ShaderStyle::Uber, gllab::ShadingModel::Cel);
    CHECK(uberLambert.Program() != 0u);
    CHECK(uberLambert.Program() == uberBlinn.Program());
    CHECK(uberLambert.Program() == uberCel.Program());

    // static permutation 은 세 프로그램이 전부 다르다.
    gllab::ShaderProgram& permLambert =
        shaders.Pick(gllab::ShaderStyle::StaticPermutation, gllab::ShadingModel::Lambert);
    gllab::ShaderProgram& permBlinn =
        shaders.Pick(gllab::ShaderStyle::StaticPermutation, gllab::ShadingModel::BlinnPhong);
    gllab::ShaderProgram& permCel =
        shaders.Pick(gllab::ShaderStyle::StaticPermutation, gllab::ShadingModel::Cel);
    CHECK(permLambert.Program() != 0u);
    CHECK(permLambert.Program() != permBlinn.Program());
    CHECK(permBlinn.Program()   != permCel.Program());
    CHECK(permLambert.Program() != permCel.Program());
    CHECK(permLambert.Program() != uberLambert.Program());

    // 분기가 어디 있는지 — uShadingModel 은 uber 에만 있다. SetInt 는 실제로
    // glUniform 을 불렀을 때만 true 이므로, 이 두 줄이 '런타임 분기 vs 컴파일
    // 시점 분리' 를 그대로 가른다.
    uberLambert.Use();
    CHECK(uberLambert.SetInt("uShadingModel", 1));
    permLambert.Use();
    CHECK(!permLambert.SetInt("uShadingModel", 1));

    // 프로그램 전환 감지 — state changes 카운터가 이 결과로만 늘어난다.
    shaders.ProgramChangedSinceLast(uberLambert);          // 최초 1 회로 기준을 잡는다.
    CHECK(!shaders.ProgramChangedSinceLast(uberLambert));
    CHECK(!shaders.ProgramChangedSinceLast(uberCel));      // uber 는 모델을 바꿔도 전환 없음.
    CHECK(shaders.ProgramChangedSinceLast(permLambert));   // 스타일을 바꾸면 전환.
    CHECK(shaders.ProgramChangedSinceLast(permBlinn));     // static 은 모델마다 전환.
    CHECK(!shaders.ProgramChangedSinceLast(permBlinn));

    std::printf("[shader] uber=%u perm={%u,%u,%u}\n",
                uberLambert.Program(), permLambert.Program(),
                permBlinn.Program(), permCel.Program());
    CHECK(glGetError() == GL_NO_ERROR);
}

// (e) 드라이버가 보고한 mip chain 크기와 예측 모델의 관계.
void TestTextureVram(Context& ctx) {
    gllab::KHRDebug& debug = ctx.debug;
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

// 어느 검증이 어느 step 소속인지는 *그 step 이 무엇을 추가했는지* 로 정한다.
//   step03 — 세 드로우 모드의 픽셀 동일성 + 제출 카운터.
//   step04 — uber / static permutation 프로그램 구성.
//   step05 — LightingBindings::Apply 의 실제 카운터.
//   step06 — 텍스처 mip chain 의 드라이버 실측치.
//   step07 — Scene::Rebuild 자원 수명 + MSAA 폴백.
struct TestCase {
    const char* step;
    const char* name;
    void (*fn)(Context&);
};

const TestCase kTests[] = {
    {"step03", "draw_mode_capture",     TestDrawModeCaptureRegression},
    {"step04", "shader_permutations",   TestShaderPermutationPrograms},
    {"step05", "binding_counters",      TestBindingCountersMatchModel},
    {"step06", "texture_vram",          TestTextureVram},
    {"step07", "scene_rebuild_handles", TestSceneResourceLifetime},
    {"step07", "msaa_fallback",         TestMsaaFallback},
};

bool Selected(const TestCase& t, const char* filter) {
    if (!filter || !*filter) return true;
    return std::strcmp(t.step, filter) == 0 || std::strcmp(t.name, filter) == 0 ||
           std::strstr(t.name, filter) != nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    const char* filter = (argc > 1) ? argv[1] : nullptr;
    if (filter && !*filter) filter = nullptr;

    // 필터가 아무것도 고르지 않으면 컨텍스트를 만들기 전에 실패시킨다 — 오타를
    // '헤드리스 skip' 으로 오해하지 않도록.
    bool anySelected = false;
    for (const TestCase& t : kTests) anySelected = anySelected || Selected(t, filter);
    if (!anySelected) {
        std::cerr << "no test matched filter: " << filter << "\n";
        return 2;
    }

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

    int selected = 0;
    int passed   = 0;
    try {
        gllab::ShaderRegistry shaders;
        shaders.Init();
        Context ctx{shaders, debug};

        for (const TestCase& t : kTests) {
            if (!Selected(t, filter)) continue;
            ++selected;
            const int before = failures;
            t.fn(ctx);
            const bool ok = (failures == before);
            if (ok) ++passed;
            std::cout << '[' << t.step << "] " << t.name
                      << (ok ? " ... ok\n" : " ... FAILED\n");
        }
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
    std::cout << "graphics_gl_render_tests: " << passed << '/' << selected
              << " passed (filter=" << (filter ? filter : "all") << ")\n";
    return 0;
}
