#pragma once

// PipelineCache — 그래픽스 파이프라인 묶음. Dynamic Rendering 사용.

#include <vulkan/vulkan.h>

namespace vklab {

class VkContext;

class PipelineCache {
public:
    void Init(VkContext& ctx, VkFormat colorFormat, VkFormat depthFormat,
              VkDescriptorSetLayout setLayout);
    void Shutdown(VkContext& ctx);

    VkPipelineLayout Layout()      const { return layout_; }
    VkPipeline       Lambert()     const { return lambert_; }
    VkPipeline       BlinnPhong()  const { return blinn_; }

private:
    VkShaderModule Load(VkContext& ctx, const char* path);
    VkPipeline     Build(VkContext& ctx, VkFormat colorFormat, VkFormat depthFormat,
                         VkShaderModule vs, VkShaderModule fs);

    VkPipelineLayout layout_   = VK_NULL_HANDLE;
    VkPipeline       lambert_  = VK_NULL_HANDLE;
    VkPipeline       blinn_    = VK_NULL_HANDLE;
};

}  // namespace vklab
