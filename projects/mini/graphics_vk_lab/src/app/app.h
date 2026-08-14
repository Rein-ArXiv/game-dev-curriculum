#pragma once

// App — graphics_vk_lab 의 메인 애플리케이션.
//
// Vulkan 1.3 기반 동작 렌더러다. 같은 큐브 격자 장면을 그리며 명시적 동기화·
// 리소스 비용을 측정한다. acquire -> record -> submit -> present 프레임 루프와
// ImGui Vulkan backend (dynamic rendering) 를 통합한다.

#include "device/vk_context.h"
#include "device/vk_swapchain.h"
#include "device/vk_alloc.h"
#include "pipeline/pipeline_cache.h"
#include "barrier/barrier_helpers.h"
#include "sync/timeline_sync.h"
#include "sync/frame_completion.h"
#include "scene/scene.h"
#include "measure/frame_timer.h"
#include "measure/timestamp_query.h"

#include <vulkan/vulkan.h>

#include <array>
#include <memory>
#include <vector>

struct GLFWwindow;

namespace vklab {

class Panel;

class App {
public:
    App();
    ~App();
    int Run();

private:
    void InitWindow();
    void InitVulkan();
    void InitImGui();
    void Shutdown();

    void OnFrame();
    void RecordFrame(VkCommandBuffer cmd, uint32_t imageIndex);
    void UpdateFrameData(uint32_t frame);

    // 자원 생성/파괴 헬퍼.
    void CreateDepth();
    void DestroyDepth();
    void CreateDescriptors();
    void DestroyDescriptors();
    void CreateCommandResources();
    void CreateFrameSync();
    void CreatePresentSync();
    void DestroyPresentSync();
    void RecreateSwapchainAndDepth();

    VkCommandBuffer BeginSingleTime();
    void            EndSingleTime(VkCommandBuffer cmd);

    static void FramebufferResizeCb(GLFWwindow* w, int width, int height);

    // 코어 객체.
    GLFWwindow*       window_ = nullptr;
    VkContext         ctx_;
    VkSwapchain       swap_;
    VkAlloc           alloc_;
    PipelineCache     pipelines_;
    Scene             scene_;
    FrameTimer        cpuTimer_;
    TimestampQuery    gpuTimer_;
    TimelineSemaphore timeline_;
    std::unique_ptr<Panel> panel_;

    GlCounters counters_{};

    // 명령/동기화.
    VkCommandPool cmdPool_ = VK_NULL_HANDLE;

    struct FrameRes {
        VkCommandBuffer cmd            = VK_NULL_HANDLE;
        VkSemaphore     imageAvailable = VK_NULL_HANDLE;
        VkFence         inFlight        = VK_NULL_HANDLE;
        VkBuffer        ubo             = VK_NULL_HANDLE;
        VmaAllocation   uboAlloc        = nullptr;
        void*           uboMapped       = nullptr;
        VkBuffer        ssbo            = VK_NULL_HANDLE;
        VmaAllocation   ssboAlloc       = nullptr;
        void*           ssboMapped      = nullptr;
        VkDescriptorSet descSet         = VK_NULL_HANDLE;
    };
    std::array<FrameRes, kFramesInFlight> frames_{};
    uint32_t frameIndex_ = 0;

    // present 동기화 — swapchain image 단위.
    std::vector<VkSemaphore> renderFinished_;

    // slot/image 별 완료 추적 — 디바이스 무관 상태만 담는다 (src/sync/frame_completion.h).
    FrameCompletionTracker completion_;

    // depth — swapchain image 마다 1개 (동시 in-flight 프레임의 WAW 해저드 방지).
    std::vector<VkImage>       depthImages_;
    std::vector<VmaAllocation> depthAllocs_;
    std::vector<VkImageView>   depthViews_;
    VkFormat                   depthFormat_ = VK_FORMAT_D32_SFLOAT;

    // descriptor.
    VkDescriptorSetLayout setLayout_  = VK_NULL_HANDLE;
    VkDescriptorPool      descPool_   = VK_NULL_HANDLE;
    VkDescriptorPool      imguiPool_  = VK_NULL_HANDLE;
    VkFormat              imguiColorFormat_ = VK_FORMAT_B8G8R8A8_UNORM;

    // 상태.
    bool   framebufferResized_ = false;
    float  camTime_ = 0.0f;

    // 토글.
    bool          vsync_         = false;
    bool          prevVsync_     = false;
    DrawMode      drawMode_      = DrawMode::PerDraw;
    BarrierPolicy barrierPolicy_ = BarrierPolicy::Minimal;
    SyncMode      syncMode_      = SyncMode::Binary;
    SyncMode      prevSyncMode_  = SyncMode::Binary;
    bool          useBlinnPhong_ = false;
    int           objectCount_   = 10000;
    int           pendingCount_  = 10000;

    // 의도적 실패 주입 — 기본값은 둘 다 꺼짐(정상 경로).
    BarrierInjection inject_{};
};

}  // namespace vklab
