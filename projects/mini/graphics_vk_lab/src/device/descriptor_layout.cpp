#include "device/descriptor_layout.h"

namespace vklab {

// snippet-begin: scene-set-bindings
std::vector<VkDescriptorSetLayoutBinding> SceneSetBindings() {
    std::vector<VkDescriptorSetLayoutBinding> binds(2);
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    return binds;
}

VkDescriptorSetLayoutCreateFlags SceneSetLayoutFlags() {
    // UPDATE_AFTER_BIND_POOL 이 없다. bindless 경로를 만들지 않았으므로
    // descriptor 를 bind 이후에 갱신할 일도 없다.
    return 0;
}
// snippet-end: scene-set-bindings

std::vector<VkDescriptorPoolSize> SceneDescriptorPoolSizes(uint32_t setCount) {
    std::vector<VkDescriptorPoolSize> sizes;
    for (const VkDescriptorSetLayoutBinding& b : SceneSetBindings()) {
        bool merged = false;
        for (VkDescriptorPoolSize& s : sizes) {
            if (s.type != b.descriptorType) continue;
            s.descriptorCount += b.descriptorCount * setCount;
            merged = true;
            break;
        }
        if (!merged) {
            sizes.push_back({b.descriptorType, b.descriptorCount * setCount});
        }
    }
    return sizes;
}

}  // namespace vklab
