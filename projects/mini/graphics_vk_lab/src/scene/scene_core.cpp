// Scene 의 디바이스 무관 부분 — 오브젝트 배치 생성, 회전 적분, model 행렬,
// instance SSBO 쓰기, draw 기록 규칙. Vulkan/VMA 함수를 하나도 부르지 않으므로
// contract 테스트 타깃에 그대로 링크된다 (CMakeLists 의 graphics_vk_contract_tests).
//
// 디바이스가 필요한 나머지(mesh 업로드, 버퍼 파괴, vkCmd* 기록)는 scene.cpp 에
// 남아 있다.

#include "scene/scene.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <random>

namespace vklab {

const char* DrawModeName(DrawMode m) {
    switch (m) {
        case DrawMode::PerDraw:   return "Per-Draw (push constants x N)";
        case DrawMode::Instanced: return "Instanced (1 vkCmdDrawIndexed)";
    }
    return "?";
}

glm::mat4 Scene::ModelMatrix(const ObjectXfm& o) {
    return glm::translate(glm::mat4(1.0f), o.position)
         * glm::rotate(glm::mat4(1.0f), o.angle, o.axis);
}

void Scene::BuildObjects(int count) {
    objects_.clear();
    objects_.reserve((size_t)count);

    int side = (int)std::ceil(std::cbrt((double)count));
    float spacing = 2.5f;
    float origin = -0.5f * spacing * (float)(side - 1);

    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> uColor(0.3f, 1.0f);
    std::uniform_real_distribution<float> uAxis(-1.0f, 1.0f);
    std::uniform_real_distribution<float> uSpeed(0.3f, 1.5f);

    int placed = 0;
    for (int z = 0; z < side && placed < count; ++z)
    for (int y = 0; y < side && placed < count; ++y)
    for (int x = 0; x < side && placed < count; ++x) {
        ObjectXfm o{};
        o.position = {origin + x * spacing, origin + y * spacing, origin + z * spacing};
        glm::vec3 a(uAxis(rng), uAxis(rng), uAxis(rng));
        float L = glm::length(a);
        o.axis = (L > 1e-4f) ? (a / L) : glm::vec3(0, 1, 0);
        o.angle = 0;
        o.angularSpeed = uSpeed(rng);
        o.color = {uColor(rng), uColor(rng), uColor(rng)};
        objects_.push_back(o);
        ++placed;
    }
}

void Scene::Build(int objectCount) { BuildObjects(objectCount); }

void Scene::Update(float dt) {
    for (auto& o : objects_) o.angle += o.angularSpeed * dt;
}

int Scene::WriteInstances(void* dst, int capacity) const {
    int n = std::min((int)objects_.size(), capacity);
    auto* out = static_cast<InstanceData*>(dst);
    for (int i = 0; i < n; ++i) {
        out[i].model = ModelMatrix(objects_[(size_t)i]);
        out[i].color = glm::vec4(objects_[(size_t)i].color, 1.0f);
    }
    return n;
}

void Scene::RecordDraw(IDrawRecorder& recorder, DrawMode mode, GlCounters& counters) const {
    if (mode == DrawMode::Instanced) {
        // 단일 push constant 로 Instanced 모드 플래그만 전달. 변환은 SSBO 에서 읽는다.
        PushConstants pc{};
        pc.model = glm::mat4(1.0f);
        pc.color = glm::vec4(1.0f);
        pc.mode  = 1u;
        recorder.PushConstants(pc);
        counters.pushConstants++;

        recorder.DrawIndexed(indexCount_, (uint32_t)objects_.size());
        counters.instancedDraws++;
    } else {
        for (size_t i = 0; i < objects_.size(); ++i) {
            const auto& o = objects_[i];
            PushConstants pc{};
            pc.model = ModelMatrix(o);
            pc.color = glm::vec4(o.color, 1.0f);
            pc.mode  = 0u;
            recorder.PushConstants(pc);
            counters.pushConstants++;

            recorder.DrawIndexed(indexCount_, 1);
            counters.drawCalls++;
        }
    }
}

}  // namespace vklab
