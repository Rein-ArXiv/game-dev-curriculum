#pragma once

#include "device/vk_alloc.h"

#include <glm/glm.hpp>

#include <vector>
#include <cstdint>

namespace vklab {

class VkContext;

// 최대 오브젝트 수 — instance SSBO 용량 결정 (100k 토글까지 커버).
constexpr int kMaxObjects = 100000;

struct GlCounters {
    int drawCalls = 0;
    int instancedDraws = 0;
    int pipelineBinds  = 0;
    int barrierCalls   = 0;
    int pushConstants  = 0;
    void Reset() { drawCalls = instancedDraws = pipelineBinds = barrierCalls = pushConstants = 0; }
};

enum class DrawMode { PerDraw, Instanced };
const char* DrawModeName(DrawMode m);

inline int ExpectedDrawSubmissions(DrawMode mode, int objectCount) {
    if (objectCount <= 0) return 0;
    return mode == DrawMode::PerDraw ? objectCount : 1;
}

// Per-draw push constant — shader 의 push_constant 블록과 동일 레이아웃.
// mat4(64) + vec4(16) + uint(4) = 84 bytes (128 byte 보장 한도 내).
struct PushConstants {
    glm::mat4 model;
    glm::vec4 color;
    uint32_t  mode;     // 0 = Per-Draw, 1 = Instanced
};

// Per-instance SSBO 항목 — std430, shader 의 Instance struct 와 동일.
struct InstanceData {
    glm::mat4 model;
    glm::vec4 color;
};

// Per-frame UBO — std140, shader 의 Frame 블록과 동일.
struct FrameUBO {
    glm::mat4 viewProj;
    glm::vec4 lightDir;
    glm::vec4 cameraPos;
};

struct ObjectXfm {
    glm::vec3 position;
    glm::vec3 axis;
    float     angle;
    float     angularSpeed;
    glm::vec3 color;
};

// Draw 기록의 seam. Vulkan 구현은 vkCmdPushConstants / vkCmdDrawIndexed 로
// 내려보내고, 테스트는 같은 인터페이스를 구현한 fake 로 제출 수를 센다.
// RecordDraw 는 VkBuffer/VMA 를 전혀 만지지 않으므로 디바이스 없이 돌아간다.
struct IDrawRecorder {
    virtual ~IDrawRecorder() = default;
    virtual void PushConstants(const PushConstants& pc) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount) = 0;
};

class Scene {
public:
    void Init(VkContext& ctx, VkAlloc& alloc, VkCommandBuffer uploadCmd);
    void FreeStaging(VkAlloc& alloc);
    void Shutdown(VkContext& ctx, VkAlloc& alloc);

    void Build(int objectCount);
    void Update(float dt);
    int  ObjectCount() const { return (int)objects_.size(); }

    // Instanced 모드용 — dst 에 오브젝트별 model/color 를 채우고 개수를 반환.
    int WriteInstances(void* dst, int capacity) const;

    // 그리기. PerDraw 는 오브젝트마다 push constant, Instanced 는 1회 draw.
    void Draw(VkCommandBuffer cmd, VkPipelineLayout layout, DrawMode mode, GlCounters& counters);

    // Draw 의 기록 규칙만 떼어낸 부분 — src/scene/scene_core.cpp (디바이스 무관).
    void RecordDraw(IDrawRecorder& recorder, DrawMode mode, GlCounters& counters) const;

    uint32_t IndexCount() const { return indexCount_; }

    static glm::mat4 ModelMatrix(const ObjectXfm& o);

private:
    void BuildMesh(VkContext& ctx, VkAlloc& alloc, VkCommandBuffer uploadCmd);
    void BuildObjects(int count);

    std::vector<ObjectXfm> objects_;

    VkBuffer vb_     = VK_NULL_HANDLE;
    VkBuffer ib_     = VK_NULL_HANDLE;
    VmaAllocation vbAlloc_ = nullptr;
    VmaAllocation ibAlloc_ = nullptr;
    uint32_t indexCount_ = 0;

    // Staging keep-alive (upload 끝나면 해제).
    VkBuffer vbStaging_ = VK_NULL_HANDLE;
    VkBuffer ibStaging_ = VK_NULL_HANDLE;
    VmaAllocation vbStagingAlloc_ = nullptr;
    VmaAllocation ibStagingAlloc_ = nullptr;
};

}  // namespace vklab
