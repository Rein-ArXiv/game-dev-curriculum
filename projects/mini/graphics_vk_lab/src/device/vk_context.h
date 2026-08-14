#pragma once

// VkContext — Instance + PhysicalDevice + LogicalDevice + Queue 묶음.
//
// Step 1: Graphics queue 한 개 + Validation Layer 활성 (디버그 빌드).
// Step 4 에서 Compute / Transfer queue 추가.

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <vector>
#include <cstdint>

namespace vklab {

constexpr uint32_t kFramesInFlight = 2;

class VkContext {
public:
    void Init(GLFWwindow* window);
    void Shutdown();

    VkInstance       Instance()       const { return instance_; }
    VkPhysicalDevice Physical()        const { return physical_; }
    VkDevice         Device()          const { return device_; }
    VkQueue          GraphicsQueue()   const { return graphicsQueue_; }
    VkQueue          ComputeQueue()    const { return computeQueue_; }
    VkQueue          TransferQueue()   const { return transferQueue_; }
    uint32_t         GraphicsFamily()  const { return graphicsFamily_; }
    uint32_t         ComputeFamily()   const { return computeFamily_; }
    uint32_t         TransferFamily()  const { return transferFamily_; }
    VkDebugUtilsMessengerEXT DebugMessenger() const { return debugMsg_; }
    double           TimestampPeriodNs() const { return (double)timestampPeriod_; }

    void SetDebugCallback(PFN_vkDebugUtilsMessengerCallbackEXT cb,
                           void* user);

private:
    void CreateInstance();
    void SetupDebug();
    void PickPhysicalDevice(VkSurfaceKHR surface);
    void CreateLogicalDevice();

    VkInstance       instance_  = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMsg_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_  = VK_NULL_HANDLE;
    VkDevice         device_    = VK_NULL_HANDLE;
    VkQueue          graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue          computeQueue_  = VK_NULL_HANDLE;
    VkQueue          transferQueue_ = VK_NULL_HANDLE;
    uint32_t         graphicsFamily_ = ~0u;
    uint32_t         computeFamily_  = ~0u;
    uint32_t         transferFamily_ = ~0u;
    float            timestampPeriod_ = 1.0f;
};

}  // namespace vklab
