#include "device/vk_context.h"

#include "device/queue_select.h"

#include <stdexcept>
#include <vector>
#include <cstring>
#include <cstdio>

namespace vklab {

namespace {

void Check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s (vkResult=%d)", what, (int)r);
        throw std::runtime_error(buf);
    }
}

#ifndef NDEBUG
constexpr bool kEnableValidation = true;
#else
constexpr bool kEnableValidation = false;
#endif

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCb(
    VkDebugUtilsMessageSeverityFlagBitsEXT /*sev*/,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*user*/) {
    std::fprintf(stderr, "[vk] %s\n", data->pMessage);
    return VK_FALSE;
}

}  // namespace

void VkContext::CreateInstance() {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "graphics_vk_lab";
    app.apiVersion = VK_API_VERSION_1_3;

    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> exts(glfwExts, glfwExts + glfwExtCount);
    if (kEnableValidation) {
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char*> layers;
    if (kEnableValidation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount   = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount   = (uint32_t)layers.size();
    ci.ppEnabledLayerNames = layers.data();

    Check(vkCreateInstance(&ci, nullptr, &instance_), "vkCreateInstance");
}

void VkContext::SetupDebug() {
    if (!kEnableValidation) return;
    VkDebugUtilsMessengerCreateInfoEXT ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = &DebugCb;

    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
    if (fn) fn(instance_, &ci, nullptr, &debugMsg_);
}

void VkContext::PickPhysicalDevice(VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) throw std::runtime_error("no Vulkan devices");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    uint32_t bestGraphics = ~0u;
    uint32_t bestCompute = ~0u;
    uint32_t bestTransfer = ~0u;

    for (auto d : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(d, &props);
        if (props.apiVersion < VK_API_VERSION_1_3) continue;

        VkPhysicalDeviceVulkan13Features req13{};
        req13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceVulkan12Features req12{};
        req12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        req12.pNext = &req13;
        VkPhysicalDeviceFeatures2 feats{};
        feats.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        feats.pNext = &req12;
        vkGetPhysicalDeviceFeatures2(d, &feats);
        if (!req13.synchronization2 ||
            !req13.dynamicRendering ||
            !req12.timelineSemaphore) {
            continue;
        }

        uint32_t qfc = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qfc, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qfc);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qfc, qfs.data());

        std::vector<VkBool32> presentSupport(qfc, VK_TRUE);
        for (uint32_t i = 0; i < qfc; ++i) {
            VkBool32 presentSupported = VK_TRUE;
            if (surface != VK_NULL_HANDLE) {
                vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &presentSupported);
            }
            presentSupport[i] = presentSupported;
        }

        // 어느 family 를 고르고 무엇으로 대체하는가는 Vulkan 핸들과 무관한
        // 판정이라 순수 함수로 나가 있다 — 헤드리스 테스트가 도는 지점이다.
        QueueFamilies picked;
        if (!SelectQueueFamilies(qfs, presentSupport, picked)) continue;
        const uint32_t g = picked.graphics;
        const uint32_t c = picked.compute;
        const uint32_t t = picked.transfer;

        if (best == VK_NULL_HANDLE ||
            props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            best = d;
            bestGraphics = g;
            bestCompute = c;
            bestTransfer = t;
            timestampPeriod_ = props.limits.timestampPeriod;
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) break;
        }
    }

    if (best == VK_NULL_HANDLE) {
        throw std::runtime_error("no Vulkan 1.3 present-capable device with required features");
    }

    physical_ = best;
    graphicsFamily_ = bestGraphics;
    computeFamily_ = bestCompute;
    transferFamily_ = bestTransfer;
}

void VkContext::CreateLogicalDevice() {
    float prio = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> qcis;

    auto add = [&](uint32_t family) {
        for (auto& q : qcis) if (q.queueFamilyIndex == family) return;
        VkDeviceQueueCreateInfo q{};
        q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        q.queueFamilyIndex = family;
        q.queueCount = 1;
        q.pQueuePriorities = &prio;
        qcis.push_back(q);
    };
    add(graphicsFamily_);
    add(computeFamily_);
    add(transferFamily_);

    const char* exts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    VkPhysicalDeviceVulkan13Features v13{};
    v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    v13.synchronization2 = VK_TRUE;
    v13.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceVulkan12Features v12{};
    v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    v12.timelineSemaphore = VK_TRUE;
    v12.pNext = &v13;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = (uint32_t)qcis.size();
    dci.pQueueCreateInfos = qcis.data();
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = exts;
    dci.pNext = &v12;

    Check(vkCreateDevice(physical_, &dci, nullptr, &device_), "vkCreateDevice");

    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, computeFamily_,  0, &computeQueue_);
    vkGetDeviceQueue(device_, transferFamily_, 0, &transferQueue_);
}

void VkContext::Init(GLFWwindow* window) {
    CreateInstance();
    SetupDebug();
    VkSurfaceKHR probeSurface = VK_NULL_HANDLE;
    if (window) {
        Check(glfwCreateWindowSurface(instance_, window, nullptr, &probeSurface),
              "glfwCreateWindowSurface(probe)");
    }
    PickPhysicalDevice(probeSurface);
    if (probeSurface) {
        vkDestroySurfaceKHR(instance_, probeSurface, nullptr);
    }
    CreateLogicalDevice();
}

void VkContext::SetDebugCallback(PFN_vkDebugUtilsMessengerCallbackEXT cb,
                                  void* user) {
    if (!kEnableValidation || !instance_) return;

    auto destroyFn = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
    if (debugMsg_ && destroyFn) {
        destroyFn(instance_, debugMsg_, nullptr);
        debugMsg_ = VK_NULL_HANDLE;
    }

    VkDebugUtilsMessengerCreateInfoEXT ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = cb ? cb : &DebugCb;
    ci.pUserData = user;

    auto createFn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
    if (createFn) createFn(instance_, &ci, nullptr, &debugMsg_);
}

void VkContext::Shutdown() {
    if (device_) { vkDeviceWaitIdle(device_); vkDestroyDevice(device_, nullptr); device_ = VK_NULL_HANDLE; }
    if (debugMsg_) {
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
        if (fn) fn(instance_, debugMsg_, nullptr);
        debugMsg_ = VK_NULL_HANDLE;
    }
    if (instance_) { vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE; }
}

}  // namespace vklab
