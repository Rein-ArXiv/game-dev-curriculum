#pragma once

#include <vulkan/vulkan.h>

#include <vector>

struct GLFWwindow;

namespace vklab {

class VkContext;

class VkSwapchain {
public:
    void Init(VkContext& ctx, GLFWwindow* window, bool vsync);
    void Shutdown(VkContext& ctx);
    void Recreate(VkContext& ctx, GLFWwindow* window, bool vsync);

    VkSurfaceKHR    Surface()    const { return surface_; }
    VkSwapchainKHR  Handle()     const { return swap_; }
    VkFormat        Format()     const { return format_; }
    VkExtent2D      Extent()     const { return extent_; }
    uint32_t        ImageCount() const { return (uint32_t)images_.size(); }
    VkImage         Image(uint32_t i)     const { return images_[i]; }
    VkImageView     ImageView(uint32_t i) const { return views_[i]; }

private:
    void CreateSurface(VkContext& ctx, GLFWwindow* window);
    void CreateSwapchain(VkContext& ctx, GLFWwindow* window, bool vsync);
    void CreateImageViews(VkContext& ctx);

    VkSurfaceKHR              surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR            swap_    = VK_NULL_HANDLE;
    VkFormat                  format_  = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D                extent_  = {0, 0};
    std::vector<VkImage>      images_;
    std::vector<VkImageView>  views_;
};

}  // namespace vklab
