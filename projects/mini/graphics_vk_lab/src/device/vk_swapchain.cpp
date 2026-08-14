#include "device/vk_swapchain.h"

#include "device/vk_context.h"
#include "device/swapchain_select.h"

#include <GLFW/glfw3.h>
#include <stdexcept>
#include <algorithm>
#include <vector>

namespace vklab {

namespace {

void RequireSuccess(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(operation);
}

VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(
    VkCompositeAlphaFlagsKHR supported) {
    constexpr VkCompositeAlphaFlagBitsKHR preferred[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (VkCompositeAlphaFlagBitsKHR candidate : preferred) {
        if ((supported & candidate) != 0) return candidate;
    }
    throw std::runtime_error("surface reports no supported composite alpha mode");
}

}  // namespace

void VkSwapchain::CreateSurface(VkContext& ctx, GLFWwindow* window) {
    if (glfwCreateWindowSurface(ctx.Instance(), window, nullptr, &surface_) != VK_SUCCESS) {
        throw std::runtime_error("glfwCreateWindowSurface failed");
    }
}

void VkSwapchain::CreateSwapchain(VkContext& ctx, GLFWwindow* window, bool vsync) {
    VkSurfaceCapabilitiesKHR caps;
    RequireSuccess(
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.Physical(), surface_, &caps),
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");

    extent_ = caps.currentExtent;
    if (extent_.width == 0xFFFFFFFF) {
        int w, h; glfwGetFramebufferSize(window, &w, &h);
        extent_.width  = std::clamp((uint32_t)w, caps.minImageExtent.width,  caps.maxImageExtent.width);
        extent_.height = std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    uint32_t imageCount = std::max(caps.minImageCount + 1, kFramesInFlight);
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    uint32_t formatCount = 0;
    RequireSuccess(
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            ctx.Physical(), surface_, &formatCount, nullptr),
        "vkGetPhysicalDeviceSurfaceFormatsKHR(count) failed");
    if (formatCount == 0)
        throw std::runtime_error("surface reports no supported format");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    RequireSuccess(
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            ctx.Physical(), surface_, &formatCount, formats.data()),
        "vkGetPhysicalDeviceSurfaceFormatsKHR(data) failed");
    VkSurfaceFormatKHR chosenFormat = ChooseSurfaceFormat(formats);
    format_ = chosenFormat.format;

    uint32_t presentCount = 0;
    RequireSuccess(
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            ctx.Physical(), surface_, &presentCount, nullptr),
        "vkGetPhysicalDeviceSurfacePresentModesKHR(count) failed");
    if (presentCount == 0)
        throw std::runtime_error("surface reports no present mode");
    std::vector<VkPresentModeKHR> presentModes(presentCount);
    RequireSuccess(
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            ctx.Physical(), surface_, &presentCount, presentModes.data()),
        "vkGetPhysicalDeviceSurfacePresentModesKHR(data) failed");

    // vsync 토글: 켜짐 → FIFO (tearing 없음), 꺼짐 → MAILBOX 우선, 없으면 IMMEDIATE.
    // FIFO 는 사양상 항상 지원되므로 안전한 fallback.
    const VkPresentModeKHR presentMode = ChoosePresentMode(presentModes, vsync);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = surface_;
    ci.minImageCount    = imageCount;
    ci.imageFormat      = chosenFormat.format;
    ci.imageColorSpace  = chosenFormat.colorSpace;
    ci.imageExtent      = extent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = ChooseCompositeAlpha(caps.supportedCompositeAlpha);
    ci.presentMode      = presentMode;
    ci.clipped          = VK_TRUE;

    if (vkCreateSwapchainKHR(ctx.Device(), &ci, nullptr, &swap_) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSwapchainKHR failed");
    }

    uint32_t n = 0;
    RequireSuccess(vkGetSwapchainImagesKHR(ctx.Device(), swap_, &n, nullptr),
                   "vkGetSwapchainImagesKHR(count) failed");
    if (n == 0) throw std::runtime_error("swapchain reports no images");
    images_.resize(n);
    RequireSuccess(vkGetSwapchainImagesKHR(ctx.Device(), swap_, &n, images_.data()),
                   "vkGetSwapchainImagesKHR(data) failed");
}

void VkSwapchain::CreateImageViews(VkContext& ctx) {
    views_.assign(images_.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = images_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format_;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(ctx.Device(), &vi, nullptr, &views_[i]) !=
            VK_SUCCESS) {
            for (VkImageView view : views_) {
                if (view) vkDestroyImageView(ctx.Device(), view, nullptr);
            }
            views_.clear();
            throw std::runtime_error("vkCreateImageView failed");
        }
    }
}

void VkSwapchain::Init(VkContext& ctx, GLFWwindow* window, bool vsync) {
    CreateSurface(ctx, window);
    CreateSwapchain(ctx, window, vsync);
    CreateImageViews(ctx);
}

void VkSwapchain::Shutdown(VkContext& ctx) {
    for (auto v : views_) if (v) vkDestroyImageView(ctx.Device(), v, nullptr);
    views_.clear(); images_.clear();
    if (swap_)    vkDestroySwapchainKHR(ctx.Device(), swap_, nullptr); swap_ = VK_NULL_HANDLE;
    if (surface_) vkDestroySurfaceKHR(ctx.Instance(), surface_, nullptr); surface_ = VK_NULL_HANDLE;
}

void VkSwapchain::Recreate(VkContext& ctx, GLFWwindow* window, bool vsync) {
    vkDeviceWaitIdle(ctx.Device());
    for (auto v : views_) if (v) vkDestroyImageView(ctx.Device(), v, nullptr);
    views_.clear(); images_.clear();
    if (swap_) { vkDestroySwapchainKHR(ctx.Device(), swap_, nullptr); swap_ = VK_NULL_HANDLE; }
    CreateSwapchain(ctx, window, vsync);
    CreateImageViews(ctx);
}

}  // namespace vklab
