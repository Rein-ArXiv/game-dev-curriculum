// vk_present — 프레임버퍼를 Vulkan 스왑체인으로 표시하는 백엔드.
//
// 계층 규칙(A3): games/ 는 GPU·창 API 를 직접 부를 수 없다. 이 코드는
// games/defense/vk_present.hpp 에 있었고 <windows.h> + <vulkan/vulkan.h> 를
// 게임 진입점으로 끌어들이던 세 경로 중 하나였다. 이제 platform/ 이 소유한다.
// 창 핸들은 platform::Window::NativeHandle() 이 주는 void* 로 받는다 — 게임
// 코드가 HWND 라는 타입을 아예 모르게 하려는 것이다.
//
// CPU 프레임버퍼(RGBA8, Framebuffer::Data())를 GPU 텍스처에 매 프레임 업로드하고,
// 풀스크린 삼각형으로 그 텍스처를 샘플링해 스왑체인 이미지에 그린 뒤 present 한다.
// 텍스처(게임 해상도)→스왑체인(창 크기) 스케일은 샘플러가 자동 처리.
// 즉 게임 로직·렌더 호출은 그대로, "표시"만 GPU 로.
//
// Vulkan 1.0 코어 + 단일 in-flight 프레임(단순). 셰이더는 빌드 시 glslc 로 컴파일된
// fullscreen.vert.spv / blit.frag.spv 를 exe 옆에서 읽는다(io 계층 경유).

#pragma once

#if defined(_WIN32) && defined(MEGA_HAS_VULKAN)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>

#include "io/read_file.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace mega {
namespace platform {

#define VKP_CHECK(x, msg) do { VkResult _r = (x); if (_r != VK_SUCCESS) { std::printf("[vk] %s 실패 (VkResult %d)\n", msg, (int)_r); return false; } } while (0)

class VkPresenter {
public:
    VkPresenter() = default;
    VkPresenter(const VkPresenter&) = delete;
    VkPresenter& operator=(const VkPresenter&) = delete;

    // nativeWindow 는 platform::Window::NativeHandle() 의 반환값(HWND).
    bool Init(void* nativeWindow, int fbW, int fbH, const std::string& spvDir) {
        hwnd_ = static_cast<HWND>(nativeWindow);
        fbW_ = fbW; fbH_ = fbH;
        if (!hwnd_) { std::printf("[vk] 창 핸들이 없다\n"); return false; }
        return CreateInstance() && CreateSurface() && PickPhysical() && CreateDevice()
            && CreateSwapchain() && CreateRenderPass() && CreateFramebuffers()
            && CreatePipeline(spvDir) && CreateTexture() && CreateStaging()
            && CreateDescriptor() && CreateCommands() && CreateSync();
    }

    // CPU 프레임버퍼(fbW*fbH RGBA8)를 GPU로 올려 한 프레임 그린 뒤 present.
    void Present(const uint32_t* pixels) {
        if (device_ == VK_NULL_HANDLE) return;
        std::memcpy(stagingMapped_, pixels, (size_t)fbW_ * fbH_ * 4);

        vkWaitForFences(device_, 1, &inFlight_, VK_TRUE, UINT64_MAX);

        uint32_t idx = 0;
        VkResult acq = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imgAvail_, VK_NULL_HANDLE, &idx);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { Recreate(); return; }
        if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) return;

        vkResetFences(device_, 1, &inFlight_);
        vkResetCommandBuffer(cmd_, 0);
        RecordFrame(idx);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = 1; si.pWaitSemaphores = &imgAvail_; si.pWaitDstStageMask = &waitStage;
        si.commandBufferCount = 1; si.pCommandBuffers = &cmd_;
        si.signalSemaphoreCount = 1; si.pSignalSemaphores = &renderDone_;
        vkQueueSubmit(queue_, 1, &si, inFlight_);

        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &renderDone_;
        pi.swapchainCount = 1; pi.pSwapchains = &swapchain_; pi.pImageIndices = &idx;
        VkResult pr = vkQueuePresentKHR(queue_, &pi);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) Recreate();
    }

    void Destroy() {
        if (device_ == VK_NULL_HANDLE) return;
        vkDeviceWaitIdle(device_);
        if (inFlight_)   vkDestroyFence(device_, inFlight_, nullptr);
        if (imgAvail_)   vkDestroySemaphore(device_, imgAvail_, nullptr);
        if (renderDone_) vkDestroySemaphore(device_, renderDone_, nullptr);
        if (cmdPool_)    vkDestroyCommandPool(device_, cmdPool_, nullptr);
        if (dpool_)      vkDestroyDescriptorPool(device_, dpool_, nullptr);
        if (sampler_)    vkDestroySampler(device_, sampler_, nullptr);
        if (texView_)    vkDestroyImageView(device_, texView_, nullptr);
        if (texImage_)   vkDestroyImage(device_, texImage_, nullptr);
        if (texMem_)     vkFreeMemory(device_, texMem_, nullptr);
        if (staging_)    vkDestroyBuffer(device_, staging_, nullptr);
        if (stagingMem_) vkFreeMemory(device_, stagingMem_, nullptr);
        if (pipeline_)   vkDestroyPipeline(device_, pipeline_, nullptr);
        if (pipeLayout_) vkDestroyPipelineLayout(device_, pipeLayout_, nullptr);
        if (dsl_)        vkDestroyDescriptorSetLayout(device_, dsl_, nullptr);
        DestroySwapchainObjects();
        if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
        vkDestroyDevice(device_, nullptr);
        if (surface_)  vkDestroySurfaceKHR(instance_, surface_, nullptr);
        if (instance_) vkDestroyInstance(instance_, nullptr);
        device_ = VK_NULL_HANDLE; instance_ = VK_NULL_HANDLE;
    }
    ~VkPresenter() { Destroy(); }

private:
    bool CreateInstance() {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "mega-vk-present"; app.apiVersion = VK_API_VERSION_1_0;
        const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app; ci.enabledExtensionCount = 2; ci.ppEnabledExtensionNames = exts;
        VKP_CHECK(vkCreateInstance(&ci, nullptr, &instance_), "vkCreateInstance");
        return true;
    }
    bool CreateSurface() {
        VkWin32SurfaceCreateInfoKHR si{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        si.hinstance = GetModuleHandleW(nullptr); si.hwnd = hwnd_;
        VKP_CHECK(vkCreateWin32SurfaceKHR(instance_, &si, nullptr, &surface_), "win32 surface");
        return true;
    }
    bool PickPhysical() {
        uint32_t n = 0; vkEnumeratePhysicalDevices(instance_, &n, nullptr);
        if (!n) { std::printf("[vk] GPU 없음\n"); return false; }
        std::vector<VkPhysicalDevice> devs(n); vkEnumeratePhysicalDevices(instance_, &n, devs.data());
        for (auto d : devs) {
            uint32_t qn = 0; vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
            std::vector<VkQueueFamilyProperties> qs(qn); vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qs.data());
            for (uint32_t i = 0; i < qn; ++i) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface_, &present);
                if ((qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { phys_ = d; qfamily_ = i; return true; }
            }
        }
        std::printf("[vk] 그래픽스+present 큐 패밀리 없음\n"); return false;
    }
    bool CreateDevice() {
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = qfamily_; qci.queueCount = 1; qci.pQueuePriorities = &prio;
        const char* exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        ci.queueCreateInfoCount = 1; ci.pQueueCreateInfos = &qci;
        ci.enabledExtensionCount = 1; ci.ppEnabledExtensionNames = exts;
        VKP_CHECK(vkCreateDevice(phys_, &ci, nullptr, &device_), "vkCreateDevice");
        vkGetDeviceQueue(device_, qfamily_, 0, &queue_);
        return true;
    }
    bool CreateSwapchain() {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_, surface_, &caps);
        uint32_t fn = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &fn, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(fn); vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &fn, fmts.data());
        VkSurfaceFormatKHR fmt = fmts[0];
        for (auto& f : fmts) if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { fmt = f; break; }
        scFormat_ = fmt.format;

        scExtent_ = caps.currentExtent;
        if (scExtent_.width == 0xFFFFFFFFu) { scExtent_.width = (uint32_t)fbW_; scExtent_.height = (uint32_t)fbH_; }
        if (scExtent_.width == 0 || scExtent_.height == 0) { scExtent_.width = (uint32_t)fbW_; scExtent_.height = (uint32_t)fbH_; }

        uint32_t imgCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;

        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        ci.surface = surface_; ci.minImageCount = imgCount; ci.imageFormat = scFormat_;
        ci.imageColorSpace = fmt.colorSpace; ci.imageExtent = scExtent_; ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;     // vsync, 항상 지원
        ci.clipped = VK_TRUE;
        VKP_CHECK(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_), "swapchain");

        uint32_t n = 0; vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
        scImages_.resize(n); vkGetSwapchainImagesKHR(device_, swapchain_, &n, scImages_.data());
        scViews_.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = scImages_[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = scFormat_;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VKP_CHECK(vkCreateImageView(device_, &vi, nullptr, &scViews_[i]), "swapchain view");
        }
        return true;
    }
    bool CreateRenderPass() {
        VkAttachmentDescription a{};
        a.format = scFormat_; a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; a.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        ci.attachmentCount = 1; ci.pAttachments = &a; ci.subpassCount = 1; ci.pSubpasses = &sub;
        ci.dependencyCount = 1; ci.pDependencies = &dep;
        VKP_CHECK(vkCreateRenderPass(device_, &ci, nullptr, &renderPass_), "render pass");
        return true;
    }
    bool CreateFramebuffers() {
        scFbs_.resize(scViews_.size());
        for (size_t i = 0; i < scViews_.size(); ++i) {
            VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            ci.renderPass = renderPass_; ci.attachmentCount = 1; ci.pAttachments = &scViews_[i];
            ci.width = scExtent_.width; ci.height = scExtent_.height; ci.layers = 1;
            VKP_CHECK(vkCreateFramebuffer(device_, &ci, nullptr, &scFbs_[i]), "framebuffer");
        }
        return true;
    }
    // SPIR-V 는 io 계층으로 읽는다 — platform/ 이라고 해서 <fstream> 을 다시
    // 꺼낼 이유는 없다. 파일 읽기의 주인은 하나여야 한다.
    VkShaderModule LoadShader(const std::string& path) {
        std::vector<uint8_t> code;
        if (!io::ReadFileBytes(path, code, 4u * 1024u * 1024u) || code.empty() ||
            (code.size() % 4) != 0) {
            std::printf("[vk] 셰이더 못읽음: %s\n", path.c_str());
            return VK_NULL_HANDLE;
        }
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = code.size(); ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule m = VK_NULL_HANDLE; vkCreateShaderModule(device_, &ci, nullptr, &m); return m;
    }
    bool CreatePipeline(const std::string& spvDir) {
        // 디스크립터: binding0 = 프래그먼트 셰이더용 combined image sampler
        VkDescriptorSetLayoutBinding b{}; b.binding = 0; b.descriptorCount = 1;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dl.bindingCount = 1; dl.pBindings = &b;
        VKP_CHECK(vkCreateDescriptorSetLayout(device_, &dl, nullptr, &dsl_), "dsl");

        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1; pl.pSetLayouts = &dsl_;
        VKP_CHECK(vkCreatePipelineLayout(device_, &pl, nullptr, &pipeLayout_), "pipeline layout");

        VkShaderModule vs = LoadShader(spvDir + "/fullscreen.vert.spv");
        VkShaderModule fs = LoadShader(spvDir + "/blit.frag.spv");
        if (!vs || !fs) return false;
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vs; stages[0].pName = "main";
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1; vp.scissorCount = 1;     // 동적
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState cba{}; cba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1; cb.pAttachments = &cba;
        VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;

        VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        ci.stageCount = 2; ci.pStages = stages;
        ci.pVertexInputState = &vin; ci.pInputAssemblyState = &ia; ci.pViewportState = &vp;
        ci.pRasterizationState = &rs; ci.pMultisampleState = &ms; ci.pColorBlendState = &cb;
        ci.pDynamicState = &ds; ci.layout = pipeLayout_; ci.renderPass = renderPass_; ci.subpass = 0;
        VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline_);
        vkDestroyShaderModule(device_, vs, nullptr); vkDestroyShaderModule(device_, fs, nullptr);
        if (r != VK_SUCCESS) { std::printf("[vk] 파이프라인 실패 %d\n", (int)r); return false; }
        return true;
    }
    uint32_t FindMemType(uint32_t bits, VkMemoryPropertyFlags want) {
        VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(phys_, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
        return 0;
    }
    bool CreateTexture() {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D; ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        ci.extent = {(uint32_t)fbW_, (uint32_t)fbH_, 1}; ci.mipLevels = 1; ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VKP_CHECK(vkCreateImage(device_, &ci, nullptr, &texImage_), "tex image");
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(device_, texImage_, &mr);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = mr.size; ai.memoryTypeIndex = FindMemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VKP_CHECK(vkAllocateMemory(device_, &ai, nullptr, &texMem_), "tex mem");
        vkBindImageMemory(device_, texImage_, texMem_, 0);
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = texImage_; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VKP_CHECK(vkCreateImageView(device_, &vi, nullptr, &texView_), "tex view");
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = VK_FILTER_NEAREST; si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VKP_CHECK(vkCreateSampler(device_, &si, nullptr, &sampler_), "sampler");
        return true;
    }
    bool CreateStaging() {
        VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        ci.size = (VkDeviceSize)fbW_ * fbH_ * 4; ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VKP_CHECK(vkCreateBuffer(device_, &ci, nullptr, &staging_), "staging buf");
        VkMemoryRequirements mr; vkGetBufferMemoryRequirements(device_, staging_, &mr);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = FindMemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VKP_CHECK(vkAllocateMemory(device_, &ai, nullptr, &stagingMem_), "staging mem");
        vkBindBufferMemory(device_, staging_, stagingMem_, 0);
        vkMapMemory(device_, stagingMem_, 0, ci.size, 0, &stagingMapped_);
        return true;
    }
    bool CreateDescriptor() {
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = 1; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
        VKP_CHECK(vkCreateDescriptorPool(device_, &pi, nullptr, &dpool_), "desc pool");
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = dpool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &dsl_;
        VKP_CHECK(vkAllocateDescriptorSets(device_, &ai, &dset_), "desc set");
        VkDescriptorImageInfo ii{sampler_, texView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = dset_; w.dstBinding = 0; w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &ii;
        vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
        return true;
    }
    bool CreateCommands() {
        VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; ci.queueFamilyIndex = qfamily_;
        VKP_CHECK(vkCreateCommandPool(device_, &ci, nullptr, &cmdPool_), "cmd pool");
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = cmdPool_; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        VKP_CHECK(vkAllocateCommandBuffers(device_, &ai, &cmd_), "cmd buf");
        return true;
    }
    bool CreateSync() {
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VKP_CHECK(vkCreateSemaphore(device_, &si, nullptr, &imgAvail_), "sem1");
        VKP_CHECK(vkCreateSemaphore(device_, &si, nullptr, &renderDone_), "sem2");
        VKP_CHECK(vkCreateFence(device_, &fi, nullptr, &inFlight_), "fence");
        return true;
    }
    void Barrier(VkImage img, VkImageLayout oldL, VkImageLayout newL,
                 VkAccessFlags srcA, VkAccessFlags dstA, VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = oldL; b.newLayout = newL; b.image = img;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask = srcA; b.dstAccessMask = dstA;
        vkCmdPipelineBarrier(cmd_, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    void RecordFrame(uint32_t idx) {
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_, &bi);

        VkImageLayout oldL = texReady_ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        Barrier(texImage_, oldL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy cp{};
        cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        cp.imageExtent = {(uint32_t)fbW_, (uint32_t)fbH_, 1};
        vkCmdCopyBufferToImage(cmd_, staging_, texImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
        Barrier(texImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        texReady_ = true;

        VkClearValue clear{}; clear.color = {{0.f, 0.f, 0.f, 1.f}};
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = renderPass_; rp.framebuffer = scFbs_[idx];
        rp.renderArea.extent = scExtent_; rp.clearValueCount = 1; rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd_, &rp, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vpt{0, 0, (float)scExtent_.width, (float)scExtent_.height, 0, 1};
        VkRect2D sc{{0, 0}, scExtent_};
        vkCmdSetViewport(cmd_, 0, 1, &vpt); vkCmdSetScissor(cmd_, 0, 1, &sc);
        vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &dset_, 0, nullptr);
        vkCmdDraw(cmd_, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd_);
        vkEndCommandBuffer(cmd_);
    }
    void DestroySwapchainObjects() {
        for (auto fb : scFbs_) if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
        for (auto v : scViews_) if (v) vkDestroyImageView(device_, v, nullptr);
        scFbs_.clear(); scViews_.clear();
        if (swapchain_) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
    }
    void Recreate() {
        vkDeviceWaitIdle(device_);
        DestroySwapchainObjects();
        CreateSwapchain(); CreateFramebuffers();
    }

    HWND hwnd_ = nullptr;
    int  fbW_ = 0, fbH_ = 0;
    VkInstance instance_ = VK_NULL_HANDLE; VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_ = VK_NULL_HANDLE; uint32_t qfamily_ = 0;
    VkDevice device_ = VK_NULL_HANDLE; VkQueue queue_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE; VkFormat scFormat_ = VK_FORMAT_B8G8R8A8_UNORM; VkExtent2D scExtent_{};
    std::vector<VkImage> scImages_; std::vector<VkImageView> scViews_; std::vector<VkFramebuffer> scFbs_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl_ = VK_NULL_HANDLE; VkDescriptorPool dpool_ = VK_NULL_HANDLE; VkDescriptorSet dset_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout_ = VK_NULL_HANDLE; VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkImage texImage_ = VK_NULL_HANDLE; VkDeviceMemory texMem_ = VK_NULL_HANDLE; VkImageView texView_ = VK_NULL_HANDLE; VkSampler sampler_ = VK_NULL_HANDLE;
    bool texReady_ = false;
    VkBuffer staging_ = VK_NULL_HANDLE; VkDeviceMemory stagingMem_ = VK_NULL_HANDLE; void* stagingMapped_ = nullptr;
    VkCommandPool cmdPool_ = VK_NULL_HANDLE; VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkSemaphore imgAvail_ = VK_NULL_HANDLE, renderDone_ = VK_NULL_HANDLE; VkFence inFlight_ = VK_NULL_HANDLE;
};

#undef VKP_CHECK

}  // namespace platform
}  // namespace mega

#endif  // _WIN32 && MEGA_HAS_VULKAN
