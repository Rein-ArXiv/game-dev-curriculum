#pragma once

// Queue family 선택 규칙 — Vulkan 핸들이 필요 없는 순수 판정.
//
// `VkContext::PickPhysicalDevice` 는 장치에서 queue family 목록과 family 별
// present 지원 여부만 뽑아 이 함수에 넘긴다. 규칙 자체는 `VkInstance` /
// `VkPhysicalDevice` 없이 성립하므로 헤드리스 테스트가 같은 함수를 그대로 돌린다.

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace vklab {

constexpr uint32_t kNoQueueFamily = ~0u;

struct QueueFamilies {
    uint32_t graphics = kNoQueueFamily;
    uint32_t compute  = kNoQueueFamily;
    uint32_t transfer = kNoQueueFamily;
};

// present 가능한 graphics family 가 없으면 false — 그 장치는 후보에서 빠진다.
// dedicated compute/transfer 가 없으면 graphics family 로 대체하고 true.
//
// `presentSupport` 가 family 수보다 짧으면 모자란 쪽은 지원하는 것으로 본다.
// surface 없이 장치만 고를 때(probe surface 가 `VK_NULL_HANDLE`) present 조건을
// 검사하지 않는 기존 동작이 그 형태다.
bool SelectQueueFamilies(const std::vector<VkQueueFamilyProperties>& families,
                         const std::vector<VkBool32>& presentSupport,
                         QueueFamilies& out);

}  // namespace vklab
