// GL 컨텍스트 없이 도는 계약 테스트.
//
// 이 타깃은 tests/ 파일 하나만이 아니라 scene.cpp / lighting_bindings.cpp /
// texture_modes.cpp / shader_program.cpp / msaa_target.cpp / khr_debug.cpp /
// frame_timer.cpp 를 함께 컴파일한다. 여기서 부르는 진입점은 전부 GL 호출이
// 없는 순수 함수이므로 컨텍스트 없이 실행되지만, 링크 대상은 앱이 실제로 쓰는
// 번역 단위와 같다.
//
// 실행 단위는 *step 입도* 다. 인자를 주면 그 step 이 추가한 검증만 돈다.
//
//     graphics_gl_contract_tests                    — 전체
//     graphics_gl_contract_tests step03             — step03 이 추가한 것만
//     graphics_gl_contract_tests indirect_commands  — 이름 하나만
//
// CMakeLists 의 add_test 가 이 인자로 step 별 ctest 항목을 만든다. 전체를 도는
// 항목(`graphics_gl_contract_tests`)은 그대로 남아 있다.

#include "measure/frame_timer.h"
#include "measure/gl_counters.h"
#include "measure/khr_debug.h"
#include "render/lighting_bindings.h"
#include "render/texture_modes.h"
#include "scene/scene.h"

#include <glm/gtc/matrix_access.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
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

bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// 시계가 반드시 전진하도록 최소 duration 만큼 돈다. steady_clock 은 단조이므로
// 이 함수가 돌아온 시점의 경과는 *항상* d 이상이다 — sleep 과 달리 하한이 보장된다.
void SpinFor(std::chrono::microseconds d) {
    const auto deadline = std::chrono::steady_clock::now() + d;
    while (std::chrono::steady_clock::now() < deadline) {}
}

// step01 이 추가한 FrameTimer — EMA 씨앗 규칙 + 240 슬롯 히스토리 링.
// (시간 값 자체는 기계마다 다르므로, 고정된 것은 *계산 규칙* 이다.)
void TestFrameTimerHistory() {
    gllab::FrameTimer timer;
    CHECK(timer.HistoryHead() == 0);
    CHECK(timer.LastDeltaSeconds() == 0.0f);
    CHECK(timer.EmaFrameMs() == 0.0f);
    CHECK(timer.Fps() == 0.0f);
    for (int i = 0; i < gllab::FrameTimer::kHistorySize; ++i)
        CHECK(timer.History()[(size_t)i] == 0.0f);

    // 첫 프레임 — dt 는 0 (이전 BeginFrame 이 없다), EMA 는 측정값을 그대로 씨앗으로 받는다.
    timer.BeginFrame();
    SpinFor(std::chrono::microseconds(300));
    timer.EndFrame();
    CHECK(timer.LastDeltaSeconds() == 0.0f);
    CHECK(timer.LastFrameMs() >= 0.29f);          // 스핀 하한 0.3ms.
    CHECK(Near(timer.EmaFrameMs(), timer.LastFrameMs(), 1e-6f));
    CHECK(timer.HistoryHead() == 1);
    CHECK(Near(timer.History()[0], timer.LastFrameMs(), 1e-6f));
    CHECK(Near(timer.Fps(), 1000.0f / timer.EmaFrameMs(), 1e-2f));

    // 둘째 프레임 — 씨앗 분기를 벗어나 α = 0.1 의 혼합이 된다. 두 값을 읽어
    // 직접 계산하므로 이 단언은 실제 소요 시간과 무관하게 성립한다.
    const float ema1 = timer.EmaFrameMs();
    timer.BeginFrame();
    SpinFor(std::chrono::microseconds(2000));
    timer.EndFrame();
    const float last2 = timer.LastFrameMs();
    CHECK(ema1 > 0.0f);
    CHECK(last2 > ema1);
    CHECK(Near(timer.EmaFrameMs(), ema1 * 0.9f + last2 * 0.1f, 1e-3f));
    CHECK(timer.LastDeltaSeconds() > 0.0f);       // dt 는 BeginFrame 간격이라 이제 0 이 아니다.
    CHECK(timer.HistoryHead() == 2);
    CHECK(Near(timer.History()[1], last2, 1e-6f));

    // 히스토리는 링이다 — kHistorySize 프레임을 더 돌면 머리가 제자리로 돌아온다.
    for (int i = 0; i < gllab::FrameTimer::kHistorySize; ++i) {
        timer.BeginFrame();
        timer.EndFrame();
    }
    CHECK(timer.HistoryHead() == 2);
    CHECK(timer.History().size() == (size_t)gllab::FrameTimer::kHistorySize);
}

// step02 가 추가한 프레임 카운터 — Reset 이 일곱 칸을 모두 되돌리는가.
// 한 칸이라도 살아남으면 다음 프레임의 수치가 누적되어 패널이 거짓말을 한다.
void TestCounterReset() {
    gllab::GlCounters c;
    CHECK(c.drawCalls == 0 && c.instancedDraws == 0 && c.indirectDraws == 0);
    CHECK(c.stateChanges == 0 && c.uniformUpdates == 0 && c.bufferUpdates == 0);
    CHECK(c.barrierCalls == 0);

    c.drawCalls      = 10000;
    c.instancedDraws = 1;
    c.indirectDraws  = 1;
    c.stateChanges   = 2;
    c.uniformUpdates = 20006;
    c.bufferUpdates  = 3;
    c.barrierCalls   = 4;
    c.Reset();
    CHECK(c.drawCalls == 0);
    CHECK(c.instancedDraws == 0);
    CHECK(c.indirectDraws == 0);
    CHECK(c.stateChanges == 0);
    CHECK(c.uniformUpdates == 0);
    CHECK(c.bufferUpdates == 0);
    CHECK(c.barrierCalls == 0);
}

// step02 가 추가한 KHR_debug 원형 버퍼 — 256 슬롯을 넘겨도 메모리가 늘지 않고
// *가장 최근 256 개* 만 남는가. 드라이버가 초당 수백 메시지를 던지는 경로다.
void TestDebugMessageRing() {
    gllab::KHRDebug debug;
    CHECK(debug.Count() == 0);
    CHECK(debug.RingHead() == 0);

    debug.PushAppMessage("first", gllab::DebugMessage::Severity::High);
    CHECK(debug.Count() == 1);
    CHECK(debug.RingHead() == 1);
    CHECK(debug.Get(0).text == "first");
    CHECK(debug.Get(0).severity == gllab::DebugMessage::Severity::High);
    CHECK(debug.Get(0).source == GL_DEBUG_SOURCE_APPLICATION);
    CHECK(debug.Get(0).type == GL_DEBUG_TYPE_OTHER);

    debug.PushAppMessage("second");   // 앱 메시지의 기본 심각도는 Medium.
    CHECK(debug.Count() == 2);
    CHECK(debug.Get(1).severity == gllab::DebugMessage::Severity::Medium);

    constexpr int kOverflow = gllab::KHRDebug::kRingSize + 10;
    for (int i = 0; i < kOverflow; ++i)
        debug.PushAppMessage("msg" + std::to_string(i));

    // 슬롯 수는 링 크기에서 멈춘다 — push_back 누적이면 여기서 갈린다.
    CHECK(debug.Count() == gllab::KHRDebug::kRingSize);
    CHECK(debug.RingHead() == (2 + kOverflow) % gllab::KHRDebug::kRingSize);

    // 가장 최근 메시지는 머리 바로 앞 칸이고, 가장 오래된 것은 덮여 사라졌다.
    const int newest =
        (debug.RingHead() + gllab::KHRDebug::kRingSize - 1) % gllab::KHRDebug::kRingSize;
    CHECK(debug.Get(newest).text == "msg" + std::to_string(kOverflow - 1));
    bool survivedFirst = false;
    for (int i = 0; i < debug.Count(); ++i)
        if (debug.Get(i).text == "first") survivedFirst = true;
    CHECK(!survivedFirst);

    debug.Clear();
    CHECK(debug.Count() == 0);
    CHECK(debug.RingHead() == 0);
}

void TestSubmissionCounts() {
    CHECK(gllab::ExpectedDrawSubmissions(gllab::DrawMode::Individual, 10000) == 10000);
    CHECK(gllab::ExpectedDrawSubmissions(gllab::DrawMode::Instanced, 10000) == 1);
    CHECK(gllab::ExpectedDrawSubmissions(gllab::DrawMode::MultiDrawIndirect, 10000) == 1);
    CHECK(gllab::ExpectedDrawSubmissions(gllab::DrawMode::Instanced, 0) == 0);
}

// scene.cpp 가 실제로 GPU 에 올리는 MDI 커맨드 배열을 그대로 검사한다.
// 커맨드 수를 줄이거나 baseInstance 를 0 으로 고정하면 여기서 죽는다.
void TestIndirectCommands() {
    const auto cmds = gllab::BuildIndirectCommands(1000, 36);
    CHECK(cmds.size() == 1000);
    CHECK((int)cmds.size() ==
          1000 * gllab::ExpectedDrawSubmissions(gllab::DrawMode::MultiDrawIndirect, 1000));

    bool allCount = true, allInstance = true, allFirst = true, allBase = true;
    bool baseInstanceIsIndex = true;
    for (size_t i = 0; i < cmds.size(); ++i) {
        if (cmds[i].count != 36u)                    allCount = false;
        if (cmds[i].instanceCount != 1u)             allInstance = false;
        if (cmds[i].firstIndex != 0u)                allFirst = false;
        if (cmds[i].baseVertex != 0u)                allBase = false;
        if (cmds[i].baseInstance != (uint32_t)i)     baseInstanceIsIndex = false;
    }
    CHECK(allCount);
    CHECK(allInstance);
    CHECK(allFirst);
    CHECK(allBase);
    CHECK(baseInstanceIsIndex);

    // baseInstance 가 인스턴스 버퍼 인덱스라는 규약 — 상수로 고정되면 여기서 갈린다.
    CHECK(cmds.front().baseInstance == 0u);
    CHECK(cmds.back().baseInstance == 999u);

    // 인덱스 수는 큐브 6면 × 2삼각형 × 3정점.
    const auto cube = gllab::BuildIndirectCommands(3, 36);
    CHECK(cube.size() == 3);
    CHECK(cube[2].count == 36u);

    CHECK(gllab::BuildIndirectCommands(0, 36).empty());
    CHECK(gllab::BuildIndirectCommands(-5, 36).empty());
}

// scene.cpp 의 카메라/투영 행렬 — 변환 결과 검증.
void TestCameraMatrices() {
    const int   kCount  = 64;          // side = 4 격자.
    const float kAspect = 16.0f / 9.0f;
    const auto cam = gllab::ComputeCamera(kCount, kAspect);

    // 격자 파라미터는 Scene::Build 와 같다: spacing 2.5, side = ceil(cbrt(N)).
    const int   side    = (int)std::ceil(std::cbrt((double)kCount));
    const float spacing = 2.5f;
    const float half    = 0.5f * spacing * (float)(side - 1);
    const float camDist = spacing * (float)side * 1.2f + 5.0f;

    // 카메라 위치 규약 — 격자 크기에 비례해 물러난다.
    CHECK(Near(cam.eye.x, camDist * 0.7f, 1e-4f));
    CHECK(Near(cam.eye.y, camDist * 0.5f, 1e-4f));
    CHECK(Near(cam.eye.z, camDist * 0.7f, 1e-4f));

    // view 는 eye 를 원점으로, 원점을 -Z 축 위 거리 |eye| 로 보낸다.
    const glm::vec4 eyeInView = cam.view * glm::vec4(cam.eye, 1.0f);
    CHECK(Near(eyeInView.x, 0.0f, 1e-4f));
    CHECK(Near(eyeInView.y, 0.0f, 1e-4f));
    CHECK(Near(eyeInView.z, 0.0f, 1e-4f));

    const float dist = glm::length(cam.eye);
    const glm::vec4 originInView = cam.view * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(Near(originInView.x, 0.0f, 1e-4f));
    CHECK(Near(originInView.y, 0.0f, 1e-4f));
    CHECK(Near(originInView.z, -dist, 1e-3f));

    // 원점은 NDC 중심에 사상된다 — lookAt 의 center 를 옮기면 여기서 죽는다.
    const glm::mat4 viewProj = cam.proj * cam.view;
    const glm::vec4 originClip = viewProj * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(originClip.w > 0.0f);
    CHECK(Near(originClip.x / originClip.w, 0.0f, 1e-4f));
    CHECK(Near(originClip.y / originClip.w, 0.0f, 1e-4f));

    // near/far 를 뒤집으면 원점의 깊이가 음수로 뒤집힌다.
    const float originNdcZ = originClip.z / originClip.w;
    CHECK(originNdcZ > 0.0f);
    CHECK(originNdcZ < 1.0f);

    // 깊이는 카메라에서 멀어질수록 커진다 (GL 기본 규약).
    const glm::vec3 toward = glm::normalize(cam.eye);
    const glm::vec4 nearClip = viewProj * glm::vec4(toward * (dist * 0.5f), 1.0f);
    const glm::vec4 farClip  = viewProj * glm::vec4(toward * (-dist * 0.5f), 1.0f);
    CHECK(nearClip.w > 0.0f && farClip.w > 0.0f);
    CHECK(nearClip.z / nearClip.w < originNdcZ);
    CHECK(originNdcZ < farClip.z / farClip.w);

    // 격자 최외곽 큐브 8 개가 전부 클립 볼륨 안에 들어온다.
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            for (int sz = -1; sz <= 1; sz += 2) {
                const glm::vec4 corner((float)sx * half, (float)sy * half,
                                        (float)sz * half, 1.0f);
                const glm::vec4 clip = viewProj * corner;
                CHECK(clip.w > 0.0f);
                CHECK(std::fabs(clip.x / clip.w) <= 1.0f);
                CHECK(std::fabs(clip.y / clip.w) <= 1.0f);
                CHECK(std::fabs(clip.z / clip.w) <= 1.0f);
            }
        }
    }

    // aspect 는 proj[0][0] 만 바꾼다.
    const auto square = gllab::ComputeCamera(kCount, 1.0f);
    CHECK(!Near(square.proj[0][0], cam.proj[0][0], 1e-6f));
    CHECK(Near(square.proj[0][0], cam.proj[0][0] * kAspect, 1e-4f));
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (c == 0 && r == 0) continue;
            CHECK(Near(square.proj[c][r], cam.proj[c][r], 1e-6f));
        }
    }
    // view 는 aspect 와 무관하다.
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            CHECK(Near(square.view[c][r], cam.view[c][r], 1e-6f));

    // 오브젝트 수가 늘면 카메라가 더 물러난다.
    const auto wide = gllab::ComputeCamera(100000, kAspect);
    CHECK(glm::length(wide.eye) > glm::length(cam.eye));
}

void TestBindingCosts() {
    const auto uniforms =
        gllab::ExpectedBindingCost(gllab::BindingMode::PerCallUniform, 32);
    CHECK(uniforms.uniformUpdates == 65);
    CHECK(uniforms.bufferUpdates == 0);

    const auto ubo = gllab::ExpectedBindingCost(gllab::BindingMode::UBO, 32);
    const auto ssbo = gllab::ExpectedBindingCost(gllab::BindingMode::SSBO, 32);
    CHECK(ubo.uniformUpdates == 1 && ubo.bufferUpdates == 1);
    CHECK(ssbo.uniformUpdates == 1 && ssbo.bufferUpdates == 1);
}

// lighting_bindings.cpp 의 실제 갱신 계획과 헤더의 예측 모델을 맞댄다.
// Apply 의 per-call 루프는 BuildPerCallUniformNames 를 그대로 순회하므로,
// 그 루프를 무력화하면 CountBindingCost 도 함께 무너져 여기서 죽는다.
void TestCountedBindingCostMatchesModel() {
    const gllab::BindingMode modes[] = {
        gllab::BindingMode::PerCallUniform,
        gllab::BindingMode::UBO,
        gllab::BindingMode::SSBO,
    };
    const int counts[] = {0, 1, 8, 32, 64};
    for (gllab::BindingMode mode : modes) {
        for (int n : counts) {
            const auto counted  = gllab::CountBindingCost(mode, n);
            const auto expected = gllab::ExpectedBindingCost(mode, n);
            CHECK(counted.uniformUpdates == expected.uniformUpdates);
            CHECK(counted.bufferUpdates == expected.bufferUpdates);
        }
    }

    // 절대 기준 — per-call 은 광원당 2 회 + uLightCount 1 회.
    CHECK(gllab::CountBindingCost(gllab::BindingMode::PerCallUniform, 32)
              .uniformUpdates == 65);
    CHECK(gllab::CountBindingCost(gllab::BindingMode::PerCallUniform, 64)
              .uniformUpdates == 129);
    CHECK(gllab::CountBindingCost(gllab::BindingMode::UBO, 64).uniformUpdates == 1);
    CHECK(gllab::CountBindingCost(gllab::BindingMode::UBO, 64).bufferUpdates == 1);
    // 광원 수가 늘어도 UBO 경로는 상수 — 이 lab 의 핵심 주장.
    CHECK(gllab::CountBindingCost(gllab::BindingMode::UBO, 8).uniformUpdates ==
          gllab::CountBindingCost(gllab::BindingMode::UBO, 64).uniformUpdates);
    CHECK(gllab::CountBindingCost(gllab::BindingMode::PerCallUniform, 8).uniformUpdates <
          gllab::CountBindingCost(gllab::BindingMode::PerCallUniform, 64).uniformUpdates);
}

void TestPerCallUniformNames() {
    const auto names = gllab::BuildPerCallUniformNames(3);
    CHECK(names.size() == 6);
    CHECK(names[0] == "uLightPos[0]");
    CHECK(names[1] == "uLightColor[0]");
    CHECK(names[2] == "uLightPos[1]");
    CHECK(names[3] == "uLightColor[1]");
    CHECK(names[4] == "uLightPos[2]");
    CHECK(names[5] == "uLightColor[2]");
    CHECK(gllab::BuildPerCallUniformNames(0).empty());
    CHECK(gllab::BuildPerCallUniformNames(-1).empty());
    CHECK(gllab::BuildPerCallUniformNames(64).size() == 128);

    // 셰이더로 넘어가는 색은 intensity 를 미리 곱한 값이다.
    gllab::Light l;
    l.posRadius      = glm::vec4(1.0f, 2.0f, 3.0f, 80.0f);
    l.colorIntensity = glm::vec4(0.5f, 0.25f, 1.0f, 0.6f);
    const glm::vec3 radiance = gllab::LightRadiance(l);
    CHECK(Near(radiance.x, 0.30f, 1e-6f));
    CHECK(Near(radiance.y, 0.15f, 1e-6f));
    CHECK(Near(radiance.z, 0.60f, 1e-6f));
}

void TestMipStorage() {
    CHECK(gllab::ExpectedMipChainBytes(gllab::TextureMode::Uncompressed, 4, 4) ==
          64 + 16 + 4);
    CHECK(gllab::ExpectedMipChainBytes(gllab::TextureMode::DXT1, 4, 4) ==
          8 + 8 + 8);
    CHECK(gllab::ExpectedMipChainBytes(gllab::TextureMode::DXT5, 4, 4) ==
          16 + 16 + 16);
    CHECK(gllab::ExpectedMipChainBytes(gllab::TextureMode::BC7, 5, 3) ==
          32 + 16 + 16);
    CHECK(gllab::ExpectedMipChainBytes(gllab::TextureMode::None, 512, 512) == 0);
}

// texture_modes.cpp 의 블록 바이트 계산 — mip chain 합산의 유일한 재료.
void TestBlockBytes() {
    CHECK(gllab::BlockBytesFor(gllab::TextureMode::Uncompressed, 512, 512) ==
          512 * 512 * 4);
    CHECK(gllab::BlockBytesFor(gllab::TextureMode::DXT1, 512, 512) ==
          128 * 128 * 8);
    CHECK(gllab::BlockBytesFor(gllab::TextureMode::DXT5, 512, 512) ==
          128 * 128 * 16);
    CHECK(gllab::BlockBytesFor(gllab::TextureMode::BC7, 512, 512) ==
          128 * 128 * 16);
    CHECK(gllab::BlockBytesFor(gllab::TextureMode::None, 512, 512) == 0);

    // 4 의 배수가 아닌 크기는 블록 단위로 올림한다.
    CHECK(gllab::BlockBytesFor(gllab::TextureMode::DXT1, 5, 3) == 2 * 1 * 8);
    CHECK(gllab::BlockBytesFor(gllab::TextureMode::BC7, 1, 1) == 16);
    CHECK(gllab::BlockBytesFor(gllab::TextureMode::DXT1, 0, 4) == 0);
    CHECK(gllab::BlockBytesFor(gllab::TextureMode::DXT1, 4, -1) == 0);

    // 압축비 주장 — DXT1 은 RGBA8 의 1/8, DXT5/BC7 은 1/4.
    const int raw  = gllab::BlockBytesFor(gllab::TextureMode::Uncompressed, 512, 512);
    const int dxt1 = gllab::BlockBytesFor(gllab::TextureMode::DXT1, 512, 512);
    const int dxt5 = gllab::BlockBytesFor(gllab::TextureMode::DXT5, 512, 512);
    const int bc7  = gllab::BlockBytesFor(gllab::TextureMode::BC7, 512, 512);
    CHECK(raw == dxt1 * 8);
    CHECK(raw == dxt5 * 4);
    CHECK(dxt5 == bc7);

    // mip chain 합산은 레벨별 BlockBytesFor 의 합과 같아야 한다.
    for (gllab::TextureMode m : {gllab::TextureMode::Uncompressed,
                                 gllab::TextureMode::DXT1,
                                 gllab::TextureMode::DXT5,
                                 gllab::TextureMode::BC7}) {
        int total = 0;
        for (int w = 512, h = 512;; ) {
            total += gllab::BlockBytesFor(m, w, h);
            if (w == 1 && h == 1) break;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
        CHECK(gllab::ExpectedMipChainBytes(m, 512, 512) == total);
        // mip chain 은 base level 보다 크고 4/3 배를 넘지 않는다.
        const int base = gllab::BlockBytesFor(m, 512, 512);
        CHECK(total > base);
        CHECK(total <= base * 4 / 3 + 64);
    }
}

// 어느 검증이 어느 step 소속인지는 *그 step 이 무엇을 추가했는지* 로 정한다.
//   step01 — 격자 씬 + 카메라 행렬 + FrameTimer.
//   step02 — GlCounters + KHR_debug 링.
//   step03 — 드로우 모드 제출 수 + MDI 커맨드 배열.
//   step05 — 광원 binding 비용 모델.
//   step06 — 텍스처 블록 바이트 + mip chain.
// (step04 와 step07 은 GL 컨텍스트가 필요해 graphics_gl_render_tests 쪽에 있다.)
struct TestCase {
    const char* step;
    const char* name;
    void (*fn)();
};

const TestCase kTests[] = {
    {"step01", "camera_matrices",        TestCameraMatrices},
    {"step01", "frame_timer_history",    TestFrameTimerHistory},
    {"step02", "counter_reset",          TestCounterReset},
    {"step02", "debug_message_ring",     TestDebugMessageRing},
    {"step03", "draw_submission_counts", TestSubmissionCounts},
    {"step03", "indirect_commands",      TestIndirectCommands},
    {"step05", "binding_cost_model",     TestBindingCosts},
    {"step05", "counted_binding_cost",   TestCountedBindingCostMatchesModel},
    {"step05", "per_call_uniform_names", TestPerCallUniformNames},
    {"step06", "mip_chain_bytes",        TestMipStorage},
    {"step06", "block_bytes",            TestBlockBytes},
};

bool Selected(const TestCase& t, const char* filter) {
    if (!filter || !*filter) return true;
    return std::strcmp(t.step, filter) == 0 || std::strcmp(t.name, filter) == 0 ||
           std::strstr(t.name, filter) != nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    const char* filter = (argc > 1) ? argv[1] : nullptr;

    int selected = 0;
    int passed   = 0;
    for (const TestCase& t : kTests) {
        if (!Selected(t, filter)) continue;
        ++selected;
        const int before = failures;
        t.fn();
        const bool ok = (failures == before);
        if (ok) ++passed;
        std::cout << '[' << t.step << "] " << t.name
                  << (ok ? " ... ok\n" : " ... FAILED\n");
    }

    if (selected == 0) {
        std::cerr << "no test matched filter: " << filter << "\n";
        return 2;
    }
    std::cout << "graphics_gl_contract_tests: " << passed << '/' << selected
              << " passed (filter=" << (filter ? filter : "all") << ")\n";
    return failures == 0 ? 0 : 1;
}
