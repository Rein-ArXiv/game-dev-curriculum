#pragma once

// Scene descriptor set(set 0) 의 모양 — `VkDevice` 없이 성립하는 순수 기술.
//
// binding 0 = per-frame UBO, binding 1 = per-instance SSBO. 둘 다
// `descriptorCount` 가 1 이고 layout flag 는 0 이다 — descriptor array 도,
// update-after-bind 도 쓰지 않는다는 뜻이다. pool 크기는 binding 표에서
// 파생되므로 binding 을 늘리면 pool 도 함께 커진다.

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace vklab {

std::vector<VkDescriptorSetLayoutBinding> SceneSetBindings();

VkDescriptorSetLayoutCreateFlags SceneSetLayoutFlags();

// `setCount` 개의 set 을 할당할 수 있는 pool 크기. binding 표를 타입별로 접는다.
std::vector<VkDescriptorPoolSize> SceneDescriptorPoolSizes(uint32_t setCount);

}  // namespace vklab
