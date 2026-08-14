#pragma once

// LightingBindings — Step 5 의 광원 데이터 바인딩 토글.
//
// 같은 광원 N 개를 셰이더에 전달하는 세 방식:
//   - PerCallUniform : glUniform3fv 를 광원당 호출 (N × 2 = 위치/색).
//   - UBO            : glBufferSubData 한 번 + 셰이더가 std140 UBO 읽기.
//   - SSBO           : 같은 패턴 + 더 큰 / 가변 길이 지원.
//
// 광원 수 토글 — 8 / 32 / 64.

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace gllab {

class ShaderProgram;
struct GlCounters;

enum class BindingMode {
    PerCallUniform,
    UBO,
    SSBO,
};

const char* BindingModeName(BindingMode m);

struct BindingCost {
    int uniformUpdates = 0;
    int bufferUpdates = 0;
};

inline BindingCost ExpectedBindingCost(BindingMode mode, int lightCount) {
    const int count = lightCount > 0 ? lightCount : 0;
    if (mode == BindingMode::PerCallUniform) return {1 + count * 2, 0};
    return {1, 1};
}

struct Light {
    glm::vec4 posRadius;       // xyz = position, w = radius (감쇠).
    glm::vec4 colorIntensity;  // rgb = color,    w = intensity.
};

// per-call 모드가 실제로 건드리는 uniform 이름 목록 — 광원 i 당 위치/색 2 개.
// GL 호출이 없는 순수 함수라 컨텍스트 없이 테스트할 수 있고, Apply 의 루프가
// 이 목록을 그대로 순회하므로 '모델'과 '실행 경로'가 하나로 유지된다.
std::vector<std::string> BuildPerCallUniformNames(int activeCount);

// 셰이더로 넘기는 광원 색 — intensity 를 미리 곱한 값.
glm::vec3 LightRadiance(const Light& light);

// Apply 가 발생시키는 갱신 횟수를 GL 없이 세는 함수. per-call 경로는 위
// 이름 목록의 길이에서 나오므로 루프를 무력화하면 이 값도 함께 무너진다.
BindingCost CountBindingCost(BindingMode mode, int activeCount);

class LightingBindings {
public:
    void Init(int maxLights = 64);
    void Shutdown();

    void SetCount(int n);
    int  Count() const { return activeCount_; }

    // 매 프레임 광원 색을 시간에 따라 회전 → 셰이더에 갱신.
    void Update(float time);

    // 셰이더에 광원 데이터 적용. 모드별로 다른 GL 호출.
    void Apply(BindingMode mode, ShaderProgram& shader, GlCounters& counters);

private:
    void UploadUBO();
    void UploadSSBO();

    int                  maxLights_   = 64;
    int                  activeCount_ = 64;
    std::vector<Light>   lights_;

    GLuint ubo_  = 0;
    GLuint ssbo_ = 0;
};

}  // namespace gllab
