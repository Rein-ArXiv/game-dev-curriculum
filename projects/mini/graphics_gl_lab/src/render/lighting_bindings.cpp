#include "render/lighting_bindings.h"

#include "render/shader_program.h"
#include "measure/gl_counters.h"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <algorithm>
#include <cstring>
#include <string>

namespace gllab {

const char* BindingModeName(BindingMode m) {
    switch (m) {
        case BindingMode::PerCallUniform: return "per-call Uniform";
        case BindingMode::UBO:            return "UBO (std140)";
        case BindingMode::SSBO:           return "SSBO (std430)";
    }
    return "?";
}

std::vector<std::string> BuildPerCallUniformNames(int activeCount) {
    std::vector<std::string> names;
    if (activeCount <= 0) return names;
    names.reserve((size_t)activeCount * 2);
    for (int i = 0; i < activeCount; ++i) {
        names.push_back("uLightPos[" + std::to_string(i) + "]");
        names.push_back("uLightColor[" + std::to_string(i) + "]");
    }
    return names;
}

glm::vec3 LightRadiance(const Light& light) {
    return glm::vec3(light.colorIntensity) * light.colorIntensity.w;
}

BindingCost CountBindingCost(BindingMode mode, int activeCount) {
    const int count = activeCount > 0 ? activeCount : 0;
    if (mode == BindingMode::PerCallUniform) {
        // uLightCount 1 회 + 광원별 uniform 이름 수.
        return {1 + (int)BuildPerCallUniformNames(count).size(), 0};
    }
    // UBO / SSBO — uLightCount 1 회 + glBufferSubData 1 회.
    return {1, 1};
}

void LightingBindings::Init(int maxLights) {
    maxLights_   = maxLights;
    activeCount_ = maxLights;
    lights_.resize((size_t)maxLights_);

    // 광원 초기 위치 — 구 표면에 균등 분포.
    for (int i = 0; i < maxLights_; ++i) {
        float phi   = (float)i / (float)maxLights_ * glm::two_pi<float>();
        float theta = (float)((i * 13) % 31) / 31.0f * glm::pi<float>();
        float r     = 40.0f;
        glm::vec3 p(
            r * std::sin(theta) * std::cos(phi),
            r * std::cos(theta),
            r * std::sin(theta) * std::sin(phi));
        lights_[(size_t)i].posRadius      = glm::vec4(p, 80.0f);
        lights_[(size_t)i].colorIntensity = glm::vec4(1, 1, 1, 0.6f);
    }

    // UBO — std140 layout. Light 구조체가 vec4 2개 = 32바이트 정렬.
    glGenBuffers(1, &ubo_);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
    glBufferData(GL_UNIFORM_BUFFER,
                 (GLsizeiptr)(sizeof(Light) * (size_t)maxLights_),
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_);

    // SSBO — std430. 같은 layout 이지만 *동적 길이 가능 + 더 큼*.
    glGenBuffers(1, &ssbo_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 (GLsizeiptr)(sizeof(Light) * (size_t)maxLights_),
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo_);
}

void LightingBindings::Shutdown() {
    if (ubo_)  glDeleteBuffers(1, &ubo_);
    if (ssbo_) glDeleteBuffers(1, &ssbo_);
    ubo_ = ssbo_ = 0;
}

void LightingBindings::SetCount(int n) {
    activeCount_ = std::clamp(n, 0, maxLights_);
}

void LightingBindings::Update(float time) {
    for (int i = 0; i < maxLights_; ++i) {
        float phase = time * 0.5f + (float)i * 0.37f;
        lights_[(size_t)i].colorIntensity = glm::vec4(
            0.5f + 0.5f * std::sin(phase),
            0.5f + 0.5f * std::sin(phase + 2.094f),   // +120°
            0.5f + 0.5f * std::sin(phase + 4.189f),   // +240°
            0.6f);
    }
}

void LightingBindings::UploadUBO() {
    glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
    glBufferSubData(GL_UNIFORM_BUFFER, 0,
                    (GLsizeiptr)(sizeof(Light) * (size_t)activeCount_),
                    lights_.data());
}

void LightingBindings::UploadSSBO() {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    (GLsizeiptr)(sizeof(Light) * (size_t)activeCount_),
                    lights_.data());
}

void LightingBindings::Apply(BindingMode mode, ShaderProgram& shader,
                              GlCounters& counters) {
    if (shader.SetInt("uLightCount", activeCount_)) counters.uniformUpdates++;

    switch (mode) {
    case BindingMode::PerCallUniform: {
        // 광원당 위치 / 컬러 uniform 호출 — N × 2 회.
        const std::vector<std::string> names = BuildPerCallUniformNames(activeCount_);
        for (size_t k = 0; k + 1 < names.size(); k += 2) {
            const size_t i = k / 2;
            if (shader.SetVec3(names[k].c_str(),
                               glm::vec3(lights_[i].posRadius)))
                counters.uniformUpdates++;
            if (shader.SetVec3(names[k + 1].c_str(), LightRadiance(lights_[i])))
                counters.uniformUpdates++;
        }
    } break;

    case BindingMode::UBO:
        UploadUBO();
        counters.bufferUpdates++;
        break;

    case BindingMode::SSBO:
        UploadSSBO();
        counters.bufferUpdates++;
        break;
    }
}

}  // namespace gllab
