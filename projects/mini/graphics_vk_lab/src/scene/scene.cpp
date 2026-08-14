// Scene 의 디바이스 의존 부분 — mesh 업로드/해제와 vkCmd* 기록.
// 배치 생성·model 행렬·instance 쓰기·draw 기록 규칙은 scene_core.cpp 에 있다.

#include "scene/scene.h"

#include "device/vk_context.h"

namespace vklab {

namespace {

struct Vertex { glm::vec3 pos; glm::vec3 normal; };

const Vertex kCube[] = {
    {{-0.5f,-0.5f,-0.5f},{-1,0,0}}, {{-0.5f, 0.5f,-0.5f},{-1,0,0}},
    {{-0.5f, 0.5f, 0.5f},{-1,0,0}}, {{-0.5f,-0.5f, 0.5f},{-1,0,0}},
    {{ 0.5f,-0.5f,-0.5f},{ 1,0,0}}, {{ 0.5f, 0.5f,-0.5f},{ 1,0,0}},
    {{ 0.5f, 0.5f, 0.5f},{ 1,0,0}}, {{ 0.5f,-0.5f, 0.5f},{ 1,0,0}},
    {{-0.5f,-0.5f,-0.5f},{ 0,-1,0}}, {{ 0.5f,-0.5f,-0.5f},{ 0,-1,0}},
    {{ 0.5f,-0.5f, 0.5f},{ 0,-1,0}}, {{-0.5f,-0.5f, 0.5f},{ 0,-1,0}},
    {{-0.5f, 0.5f,-0.5f},{ 0, 1,0}}, {{ 0.5f, 0.5f,-0.5f},{ 0, 1,0}},
    {{ 0.5f, 0.5f, 0.5f},{ 0, 1,0}}, {{-0.5f, 0.5f, 0.5f},{ 0, 1,0}},
    {{-0.5f,-0.5f,-0.5f},{ 0,0,-1}}, {{ 0.5f,-0.5f,-0.5f},{ 0,0,-1}},
    {{ 0.5f, 0.5f,-0.5f},{ 0,0,-1}}, {{-0.5f, 0.5f,-0.5f},{ 0,0,-1}},
    {{-0.5f,-0.5f, 0.5f},{ 0,0, 1}}, {{ 0.5f,-0.5f, 0.5f},{ 0,0, 1}},
    {{ 0.5f, 0.5f, 0.5f},{ 0,0, 1}}, {{-0.5f, 0.5f, 0.5f},{ 0,0, 1}},
};
const uint16_t kCubeIdx[] = {
     0, 2, 1,  0, 3, 2,
     4, 5, 6,  4, 6, 7,
     8, 9,10,  8,10,11,
    12,14,13, 12,15,14,
    16,18,17, 16,19,18,
    20,21,22, 20,22,23,
};

// IDrawRecorder 의 Vulkan 구현 — RecordDraw 가 내린 결정을 명령 버퍼에 옮긴다.
class VulkanDrawRecorder final : public IDrawRecorder {
public:
    VulkanDrawRecorder(VkCommandBuffer cmd, VkPipelineLayout layout)
        : cmd_(cmd), layout_(layout) {}

    void PushConstants(const vklab::PushConstants& pc) override {
        vkCmdPushConstants(cmd_, layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(vklab::PushConstants), &pc);
    }

    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount) override {
        vkCmdDrawIndexed(cmd_, indexCount, instanceCount, 0, 0, 0);
    }

private:
    VkCommandBuffer  cmd_;
    VkPipelineLayout layout_;
};

}  // namespace

void Scene::BuildMesh(VkContext& /*ctx*/, VkAlloc& alloc, VkCommandBuffer uploadCmd) {
    alloc.CreateBufferStatic(uploadCmd, kCube, sizeof(kCube),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              vb_, vbAlloc_, vbStaging_, vbStagingAlloc_);
    alloc.CreateBufferStatic(uploadCmd, kCubeIdx, sizeof(kCubeIdx),
                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                              ib_, ibAlloc_, ibStaging_, ibStagingAlloc_);
    indexCount_ = sizeof(kCubeIdx) / sizeof(kCubeIdx[0]);
}

void Scene::Init(VkContext& ctx, VkAlloc& alloc, VkCommandBuffer uploadCmd) {
    BuildMesh(ctx, alloc, uploadCmd);
    BuildObjects(10000);
}

void Scene::FreeStaging(VkAlloc& alloc) {
    if (vbStaging_) { vmaDestroyBuffer(alloc.Allocator(), vbStaging_, vbStagingAlloc_); vbStaging_ = VK_NULL_HANDLE; }
    if (ibStaging_) { vmaDestroyBuffer(alloc.Allocator(), ibStaging_, ibStagingAlloc_); ibStaging_ = VK_NULL_HANDLE; }
}

void Scene::Shutdown(VkContext& /*ctx*/, VkAlloc& alloc) {
    if (vb_) vmaDestroyBuffer(alloc.Allocator(), vb_, vbAlloc_);
    if (ib_) vmaDestroyBuffer(alloc.Allocator(), ib_, ibAlloc_);
    FreeStaging(alloc);
    vb_ = ib_ = VK_NULL_HANDLE;
}

void Scene::Draw(VkCommandBuffer cmd, VkPipelineLayout layout, DrawMode mode, GlCounters& counters) {
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb_, offsets);
    vkCmdBindIndexBuffer(cmd, ib_, 0, VK_INDEX_TYPE_UINT16);

    VulkanDrawRecorder recorder(cmd, layout);
    RecordDraw(recorder, mode, counters);
}

}  // namespace vklab
