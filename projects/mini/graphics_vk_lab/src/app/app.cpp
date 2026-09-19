// App — Vulkan lab 의 메인 진입점.
//
// 동작 렌더러다. 큐브 격자를 dynamic rendering 으로 그리며, 명시적 barrier /
// timeline / draw 모드 비용을 측정한다.

#include "app/app.h"
#include "ui/panel.h"
#include "device/descriptor_layout.h"
#include "measure/validation_log.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <implot.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace vklab {

App::App() : panel_(std::make_unique<Panel>()) {
    InitWindow();
    InitVulkan();
    InitImGui();
}

App::~App() { Shutdown(); }

void App::InitWindow() {
    if (!glfwInit()) throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(1280, 720, "graphics_vk_lab", nullptr, nullptr);
    if (!window_) { glfwTerminate(); throw std::runtime_error("glfwCreateWindow"); }
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, &App::FramebufferResizeCb);
}

void App::FramebufferResizeCb(GLFWwindow* w, int /*width*/, int /*height*/) {
    auto* self = static_cast<App*>(glfwGetWindowUserPointer(w));
    if (self) self->framebufferResized_ = true;
}

void App::InitVulkan() {
    ctx_.Init(window_);
    ctx_.SetDebugCallback(&ValidationLog::Thunk, nullptr);
    swap_.Init(ctx_, window_, vsync_);
    prevVsync_ = vsync_;
    alloc_.Init(ctx_);

    CreateCommandResources();   // command pool — CreateDepth/mesh upload 의 single-time cmd 에 필요.
    CreateDepth();              // per-image depth + UNDEFINED->DEPTH_ATTACHMENT_OPTIMAL.
    CreateDescriptors();        // set layout + per-frame UBO/SSBO + descriptor set.
    pipelines_.Init(ctx_, swap_.Format(), depthFormat_, setLayout_);
    gpuTimer_.Init(ctx_);
    timeline_.Init(ctx_.Device());
    CreateFrameSync();
    CreatePresentSync();
    completion_.Init(swap_.ImageCount(), kFramesInFlight);

    // 메시 staging upload — 1회 command buffer.
    VkCommandBuffer up = BeginSingleTime();
    gpuTimer_.ResetAll(up);     // query 는 첫 읽기 전에 GPU 에서 한 번 reset 되어야 한다.
    scene_.Init(ctx_, alloc_, up);
    EndSingleTime(up);
    scene_.FreeStaging(alloc_);
}

void App::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(window_, true);

    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8 };
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pci.maxSets = 8;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(ctx_.Device(), &pci, nullptr, &imguiPool_) != VK_SUCCESS)
        throw std::runtime_error("ImGui descriptor pool");

    imguiColorFormat_ = swap_.Format();
    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = ctx_.Instance();
    info.PhysicalDevice = ctx_.Physical();
    info.Device = ctx_.Device();
    info.QueueFamily = ctx_.GraphicsFamily();
    info.Queue = ctx_.GraphicsQueue();
    info.DescriptorPool = imguiPool_;
    info.MinImageCount = kFramesInFlight;
    info.ImageCount = swap_.ImageCount();
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.UseDynamicRendering = true;
    info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &imguiColorFormat_;
    info.PipelineRenderingCreateInfo.depthAttachmentFormat = depthFormat_;
    ImGui_ImplVulkan_Init(&info);
}

// ── 자원 ─────────────────────────────────────────────────────────────────

void App::CreateDepth() {
    VkExtent2D ext = swap_.Extent();
    uint32_t n = swap_.ImageCount();
    depthImages_.resize(n);
    depthAllocs_.resize(n);
    depthViews_.resize(n);

    for (uint32_t i = 0; i < n; ++i) {
        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = depthFormat_;
        ici.extent = { ext.width, ext.height, 1 };
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(alloc_.Allocator(), &ici, &aci,
                           &depthImages_[i], &depthAllocs_[i], nullptr) != VK_SUCCESS)
            throw std::runtime_error("vmaCreateImage depth");

        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = depthImages_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = depthFormat_;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        vkCreateImageView(ctx_.Device(), &vi, nullptr, &depthViews_[i]);
    }

    // UNDEFINED -> DEPTH_ATTACHMENT_OPTIMAL (한 번). 이후 매 프레임 loadOp CLEAR 로 재사용.
    // barrier 모듈을 통과시켜 이 전이도 Barrier policy 토글과 테스트 범위 안에 둔다.
    VkCommandBuffer cmd = BeginSingleTime();
    std::vector<VkImageMemoryBarrier2> bs(n);
    for (uint32_t i = 0; i < n; ++i) {
        bs[i] = BuildImageBarrier(depthImages_[i],
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                  VK_IMAGE_ASPECT_DEPTH_BIT, barrierPolicy_);
    }
    // counters_.barrierCalls 는 프레임마다 Reset 되는 per-frame 카운터라
    // 1회성 초기화 전이는 세지 않는다.
    ImageLayoutTransitions(cmd, bs);
    EndSingleTime(cmd);
}

void App::DestroyDepth() {
    VkDevice dev = ctx_.Device();
    for (auto v : depthViews_)  if (v) vkDestroyImageView(dev, v, nullptr);
    for (size_t i = 0; i < depthImages_.size(); ++i)
        if (depthImages_[i]) vmaDestroyImage(alloc_.Allocator(), depthImages_[i], depthAllocs_[i]);
    depthViews_.clear();
    depthImages_.clear();
    depthAllocs_.clear();
}

void App::CreateDescriptors() {
    VkDevice dev = ctx_.Device();

    // set 0 의 binding 표와 그로부터 파생되는 pool 크기는 디바이스와 무관해
    // 순수 함수로 나가 있다 — 헤드리스 테스트가 그 모양을 고정한다.
    const std::vector<VkDescriptorSetLayoutBinding> binds = SceneSetBindings();

    VkDescriptorSetLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lci.flags = SceneSetLayoutFlags();
    lci.bindingCount = (uint32_t)binds.size();
    lci.pBindings = binds.data();
    vkCreateDescriptorSetLayout(dev, &lci, nullptr, &setLayout_);

    const std::vector<VkDescriptorPoolSize> sizes =
        SceneDescriptorPoolSizes(kFramesInFlight);
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = kFramesInFlight;
    pci.poolSizeCount = (uint32_t)sizes.size();
    pci.pPoolSizes = sizes.data();
    vkCreateDescriptorPool(dev, &pci, nullptr, &descPool_);

    for (auto& fr : frames_) {
        alloc_.CreateBufferDynamic(sizeof(FrameUBO),
                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   fr.ubo, fr.uboAlloc, &fr.uboMapped);
        alloc_.CreateBufferDynamic((size_t)kMaxObjects * sizeof(InstanceData),
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   fr.ssbo, fr.ssboAlloc, &fr.ssboMapped);

        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = descPool_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &setLayout_;
        vkAllocateDescriptorSets(dev, &ai, &fr.descSet);

        VkDescriptorBufferInfo ubi{ fr.ubo, 0, sizeof(FrameUBO) };
        VkDescriptorBufferInfo sbi{ fr.ssbo, 0, VK_WHOLE_SIZE };
        VkWriteDescriptorSet w[2]{};
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = fr.descSet;
        w[0].dstBinding = 0;
        w[0].descriptorCount = 1;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[0].pBufferInfo = &ubi;
        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[1].dstSet = fr.descSet;
        w[1].dstBinding = 1;
        w[1].descriptorCount = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[1].pBufferInfo = &sbi;
        vkUpdateDescriptorSets(dev, 2, w, 0, nullptr);
    }
}

void App::DestroyDescriptors() {
    VkDevice dev = ctx_.Device();
    for (auto& fr : frames_) {
        if (fr.ubo)  vmaDestroyBuffer(alloc_.Allocator(), fr.ubo,  fr.uboAlloc);
        if (fr.ssbo) vmaDestroyBuffer(alloc_.Allocator(), fr.ssbo, fr.ssboAlloc);
        fr.ubo = fr.ssbo = VK_NULL_HANDLE;
    }
    if (descPool_)  { vkDestroyDescriptorPool(dev, descPool_, nullptr); descPool_ = VK_NULL_HANDLE; }
    if (setLayout_) { vkDestroyDescriptorSetLayout(dev, setLayout_, nullptr); setLayout_ = VK_NULL_HANDLE; }
}

void App::CreateCommandResources() {
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = ctx_.GraphicsFamily();
    vkCreateCommandPool(ctx_.Device(), &pci, nullptr, &cmdPool_);

    for (auto& fr : frames_) {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = cmdPool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        vkAllocateCommandBuffers(ctx_.Device(), &ai, &fr.cmd);
    }
}

void App::CreateFrameSync() {
    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (auto& fr : frames_) {
        vkCreateSemaphore(ctx_.Device(), &sci, nullptr, &fr.imageAvailable);
        vkCreateFence(ctx_.Device(), &fci, nullptr, &fr.inFlight);
    }
}

void App::CreatePresentSync() {
    uint32_t n = swap_.ImageCount();
    renderFinished_.resize(n);
    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < n; ++i)
        vkCreateSemaphore(ctx_.Device(), &sci, nullptr, &renderFinished_[i]);
}

void App::DestroyPresentSync() {
    for (auto s : renderFinished_) if (s) vkDestroySemaphore(ctx_.Device(), s, nullptr);
    renderFinished_.clear();
}

VkCommandBuffer App::BeginSingleTime() {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = cmdPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(ctx_.Device(), &ai, &cmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void App::EndSingleTime(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkCommandBufferSubmitInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    ci.commandBuffer = cmd;
    VkSubmitInfo2 s{};
    s.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    s.commandBufferInfoCount = 1;
    s.pCommandBufferInfos = &ci;
    vkQueueSubmit2(ctx_.GraphicsQueue(), 1, &s, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx_.GraphicsQueue());
    vkFreeCommandBuffers(ctx_.Device(), cmdPool_, 1, &cmd);
}

void App::RecreateSwapchainAndDepth() {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    while ((w == 0 || h == 0) && !glfwWindowShouldClose(window_)) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window_, &w, &h);
    }
    vkDeviceWaitIdle(ctx_.Device());
    DestroyPresentSync();
    DestroyDepth();
    swap_.Recreate(ctx_, window_, vsync_);
    CreateDepth();
    CreatePresentSync();
    // 새 image 집합은 이전 완료 값을 물려받으면 안 된다 — 재생성 직후 존재하지
    // 않는 제출을 기다리게 된다.
    completion_.OnSwapchainRecreated(swap_.ImageCount());
}

// ── 종료 ─────────────────────────────────────────────────────────────────

void App::Shutdown() {
    if (ctx_.Device()) {
        vkDeviceWaitIdle(ctx_.Device());

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        if (imguiPool_) { vkDestroyDescriptorPool(ctx_.Device(), imguiPool_, nullptr); imguiPool_ = VK_NULL_HANDLE; }

        DestroyPresentSync();
        for (auto& fr : frames_) {
            if (fr.imageAvailable) vkDestroySemaphore(ctx_.Device(), fr.imageAvailable, nullptr);
            if (fr.inFlight)       vkDestroyFence(ctx_.Device(), fr.inFlight, nullptr);
            fr.imageAvailable = VK_NULL_HANDLE;
            fr.inFlight = VK_NULL_HANDLE;
        }
        DestroyDescriptors();
        if (cmdPool_) { vkDestroyCommandPool(ctx_.Device(), cmdPool_, nullptr); cmdPool_ = VK_NULL_HANDLE; }
        DestroyDepth();

        gpuTimer_.Shutdown(ctx_);
        scene_.Shutdown(ctx_, alloc_);
        pipelines_.Shutdown(ctx_);
        timeline_.Shutdown(ctx_.Device());
        alloc_.Shutdown();
        swap_.Shutdown(ctx_);
    }
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    ctx_.Shutdown();
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

// ── 프레임 ───────────────────────────────────────────────────────────────

int App::Run() {
    while (window_ && !glfwWindowShouldClose(window_)) {
        cpuTimer_.BeginFrame();
        glfwPollEvents();
        OnFrame();
        cpuTimer_.EndFrame();
    }
    return 0;
}

void App::UpdateFrameData(uint32_t frame) {
    FrameRes& fr = frames_[frame];
    VkExtent2D ext = swap_.Extent();
    if (ext.height == 0) return;

    int side = (int)std::ceil(std::cbrt((double)std::max(objectCount_, 1)));
    float spacing = 2.5f;
    float span = spacing * (float)std::max(side - 1, 1);
    float dist = span * 0.9f + 8.0f;

    float a = camTime_ * 0.25f;
    glm::vec3 eye = glm::vec3(std::sin(a), 0.45f, std::cos(a)) * dist;
    glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    float aspect = (float)ext.width / (float)ext.height;
    glm::mat4 proj = glm::perspective(glm::radians(50.0f), aspect, 0.1f, dist * 4.0f + 50.0f);
    proj[1][1] *= -1.0f;   // Vulkan 클립 공간 Y 반전.

    FrameUBO ubo{};
    ubo.viewProj  = proj * view;
    ubo.lightDir  = glm::vec4(glm::normalize(glm::vec3(0.4f, 0.85f, 0.5f)), 0.0f);
    ubo.cameraPos = glm::vec4(eye, 1.0f);
    std::memcpy(fr.uboMapped, &ubo, sizeof(ubo));
    vmaFlushAllocation(alloc_.Allocator(), fr.uboAlloc, 0, VK_WHOLE_SIZE);

    if (drawMode_ == DrawMode::Instanced) {
        int n = scene_.WriteInstances(fr.ssboMapped, kMaxObjects);
        vmaFlushAllocation(alloc_.Allocator(), fr.ssboAlloc, 0,
                           (VkDeviceSize)n * sizeof(InstanceData));
    }
}

void App::RecordFrame(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkExtent2D ext = swap_.Extent();

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    gpuTimer_.Begin(cmd);   // reset + top-of-pipe timestamp (rendering 밖).

    // color: UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL.
    // 실패 주입 토글이 켜져 있으면 배리어를 빼거나 srcAccessMask 를 0 으로 덮어쓴다.
    if (ShouldRecordBarrier(inject_)) {
        const VkImageMemoryBarrier2 b = ApplyInjection(
            BuildImageBarrier(swap_.Image(imageIndex),
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_ASPECT_COLOR_BIT, barrierPolicy_),
            inject_);
        ImageLayoutTransitions(cmd, &b, 1);
        counters_.barrierCalls++;
    }

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = swap_.ImageView(imageIndex);
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.05f, 0.06f, 0.08f, 1.0f}};

    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = depthViews_[imageIndex];
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea.offset = {0, 0};
    ri.renderArea.extent = ext;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;
    ri.pDepthAttachment = &depth;

    vkCmdBeginRendering(cmd, &ri);

    VkViewport vpst{ 0.0f, 0.0f, (float)ext.width, (float)ext.height, 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, ext };
    vkCmdSetViewport(cmd, 0, 1, &vpst);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    VkPipeline pipe = useBlinnPhong_
        ? pipelines_.BlinnPhong()
        : pipelines_.Lambert();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    counters_.pipelineBinds++;

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_.Layout(),
                            0, 1, &frames_[frameIndex_].descSet, 0, nullptr);

    scene_.Draw(cmd, pipelines_.Layout(), drawMode_, counters_);

    // ImGui — 같은 dynamic rendering 영역 안에서.
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRendering(cmd);

    // color: COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC.
    if (ShouldRecordBarrier(inject_)) {
        const VkImageMemoryBarrier2 b = ApplyInjection(
            BuildImageBarrier(swap_.Image(imageIndex),
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                              VK_IMAGE_ASPECT_COLOR_BIT, barrierPolicy_),
            inject_);
        ImageLayoutTransitions(cmd, &b, 1);
        counters_.barrierCalls++;
    }

    gpuTimer_.End(cmd);

    vkEndCommandBuffer(cmd);
}

void App::OnFrame() {
    VkDevice dev = ctx_.Device();

    // 토글 반영.
    if (pendingCount_ != objectCount_) {
        vkDeviceWaitIdle(dev);
        objectCount_ = pendingCount_;
        scene_.Build(objectCount_);
    }
    if (vsync_ != prevVsync_) { framebufferResized_ = true; prevVsync_ = vsync_; }

    camTime_ += cpuTimer_.LastDeltaSeconds();
    scene_.Update(cpuTimer_.LastDeltaSeconds());

    // ImGui 새 프레임 + 패널.
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    PanelIO io{};
    io.cpuTimer = &cpuTimer_;
    io.gpuTimer = &gpuTimer_;
    io.counters = &counters_;
    io.objectCount = objectCount_;
    io.timelineValue = timeline_.Current();
    io.drawMode = &drawMode_;
    io.barrierPolicy = &barrierPolicy_;
    io.syncMode = &syncMode_;
    io.useBlinnPhong = &useBlinnPhong_;
    io.vsync = &vsync_;
    io.pendingCount = &pendingCount_;
    io.injection = &inject_;
    panel_->Draw(io);
    ImGui::Render();

    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window_, &fbw, &fbh);
    if (fbw == 0 || fbh == 0) return;

    // 모드 전환은 서로 다른 완료 추적 체계를 섞지 않는다. 대기 후 이전
    // 모드의 image/frame 연관 정보를 비우면 같은 자원을 새 계약으로 추적할
    // 수 있다. 이 대기는 토글 순간에만 발생하며 정상 프레임 비용에는 없다.
    if (syncMode_ != prevSyncMode_) {
        vkDeviceWaitIdle(dev);
        completion_.OnSyncModeChanged(syncMode_);
        prevSyncMode_ = syncMode_;
    }

    FrameRes& fr = frames_[frameIndex_];

    // 프레임 슬롯 throttle. Binary는 fence, Timeline은 값으로 command
    // buffer와 per-frame UBO/SSBO의 재사용 시점을 보호한다.
    if (syncMode_ == SyncMode::Binary) {
        vkWaitForFences(dev, 1, &fr.inFlight, VK_TRUE, UINT64_MAX);
    } else {
        uint64_t slotWait = 0;
        if (completion_.NeedsSlotWait((int)frameIndex_, slotWait))
            timeline_.Wait(dev, slotWait);
    }

    uint32_t imageIndex = 0;
    VkResult acq = vkAcquireNextImageKHR(dev, swap_.Handle(), UINT64_MAX,
                                         fr.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) { RecreateSwapchainAndDepth(); return; }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("vkAcquireNextImageKHR");

    // swapchain image 수와 frame slot 수는 같다고 가정할 수 없다. acquire된
    // image의 직전 사용이 다른 slot이었다면 해당 모드의 완료 객체를 기다린다.
    // Binary 에서 slot fence 는 slot 당 하나뿐이라 그 slot 이 그 뒤 다시
    // 제출됐으면 fence 는 더 새 작업의 것이다. 그 재제출이 이미 이전 제출을
    // 기다렸으므로, fence 가 아직 이 image 를 쓴 제출의 것일 때만 기다린다.
    if (syncMode_ == SyncMode::Binary) {
        int lastSlot = FrameCompletionTracker::kNoSlot;
        if (completion_.NeedsFenceWait(imageIndex, lastSlot))
            vkWaitForFences(dev, 1, &frames_[(size_t)lastSlot].inFlight, VK_TRUE, UINT64_MAX);
    } else {
        uint64_t imageWait = 0;
        if (completion_.NeedsTimelineWait(imageIndex, imageWait))
            timeline_.Wait(dev, imageWait);
    }

    UpdateFrameData(frameIndex_);
    counters_.Reset();

    if (syncMode_ == SyncMode::Binary)
        vkResetFences(dev, 1, &fr.inFlight);
    vkResetCommandBuffer(fr.cmd, 0);
    RecordFrame(fr.cmd, imageIndex);

    uint64_t signalValue = 0;
    if (syncMode_ == SyncMode::Timeline) signalValue = timeline_.Next();
    // Binary 모드에서는 signalValue 가 0 이고 slot 기록만 남는다.
    completion_.OnSubmit((int)frameIndex_, imageIndex, signalValue);

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = fr.cmd;

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = fr.imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalInfos[2]{};
    signalInfos[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfos[0].semaphore = renderFinished_[imageIndex];
    signalInfos[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    uint32_t signalCount = 1;
    if (syncMode_ == SyncMode::Timeline) {
        signalInfos[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfos[1].semaphore = timeline_.Handle();
        signalInfos[1].value = signalValue;
        signalInfos[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalCount = 2;
    }

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &waitInfo;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    submit.signalSemaphoreInfoCount = signalCount;
    submit.pSignalSemaphoreInfos = signalInfos;

    VkFence submitFence = (syncMode_ == SyncMode::Binary) ? fr.inFlight : VK_NULL_HANDLE;
    if (vkQueueSubmit2(ctx_.GraphicsQueue(), 1, &submit, submitFence) != VK_SUCCESS)
        throw std::runtime_error("vkQueueSubmit2");

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished_[imageIndex];
    VkSwapchainKHR sc = swap_.Handle();
    present.swapchainCount = 1;
    present.pSwapchains = &sc;
    present.pImageIndices = &imageIndex;

    VkResult pres = vkQueuePresentKHR(ctx_.GraphicsQueue(), &present);
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR || framebufferResized_) {
        framebufferResized_ = false;
        RecreateSwapchainAndDepth();
    } else if (pres != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR");
    }

    gpuTimer_.Poll(ctx_);
    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
}

}  // namespace vklab
