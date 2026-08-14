#include "device/swapchain_select.h"

#include <stdexcept>

namespace vklab {

VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& available,
                                   bool vsync) {
    auto has = [&](VkPresentModeKHR mode) {
        for (VkPresentModeKHR m : available) if (m == mode) return true;
        return false;
    };

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync) {
        if      (has(VK_PRESENT_MODE_MAILBOX_KHR))   presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
        else if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    return presentMode;
}

VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available) {
    if (available.empty())
        throw std::runtime_error("surface reports no supported format");

    VkSurfaceFormatKHR chosen = available[0];
    if (available.size() == 1 && available[0].format == VK_FORMAT_UNDEFINED) {
        chosen.format = VK_FORMAT_B8G8R8A8_UNORM;
    }
    for (const auto& f : available) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    return chosen;
}

}  // namespace vklab
