// GL 컨텍스트 없이 도는 계약 테스트.
//
// 이 타깃은 tests/ 파일 하나만이 아니라 scene.cpp / lighting_bindings.cpp /
// texture_modes.cpp / shader_program.cpp / msaa_target.cpp / khr_debug.cpp 를
// 함께 컴파일한다. 여기서 부르는 진입점은 전부 GL 호출이 없는 순수 함수이므로
// 컨텍스트 없이 실행되지만, 링크 대상은 앱이 실제로 쓰는 번역 단위와 같다.

#include "render/lighting_bindings.h"
#include "render/texture_modes.h"
#include "scene/scene.h"

#include <glm/gtc/matrix_access.hpp>

#include <cmath>
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

}  // namespace

int main() {
    TestSubmissionCounts();
    TestIndirectCommands();
    TestCameraMatrices();
    TestBindingCosts();
    TestCountedBindingCostMatchesModel();
    TestPerCallUniformNames();
    TestMipStorage();
    TestBlockBytes();
    if (failures != 0) return 1;
    std::cout << "OpenGL contract tests passed\n";
    return 0;
}
