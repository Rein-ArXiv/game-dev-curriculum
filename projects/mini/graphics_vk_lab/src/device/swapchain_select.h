#pragma once

// Swapchain 협상 중 디바이스 호출이 필요 없는 선택 규칙.
//
// vkGetPhysicalDeviceSurface*KHR 로 받은 후보 목록을 넣으면 무엇을 고를지만
// 결정한다. VkSwapchain::CreateSwapchain 이 이 함수들을 쓰고, 테스트는 같은
// 함수에 손으로 만든 후보 목록을 넣어 fallback 규칙을 고정한다.

#include <vulkan/vulkan.h>

#include <vector>

namespace vklab {

// vsync 켜짐 → FIFO (사양상 항상 지원). 꺼짐 → MAILBOX 우선, 없으면 IMMEDIATE,
// 둘 다 없으면 FIFO.
VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& available,
                                   bool vsync);

// B8G8R8A8_UNORM + SRGB_NONLINEAR 를 선호. 없으면 첫 번째 후보.
// 목록이 {UNDEFINED} 하나뿐이면 (구형 드라이버의 "아무거나" 표기) B8G8R8A8_UNORM.
// 빈 목록은 std::runtime_error.
VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available);

}  // namespace vklab
