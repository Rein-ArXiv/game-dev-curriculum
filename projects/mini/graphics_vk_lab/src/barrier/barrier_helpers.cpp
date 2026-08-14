#include "barrier/barrier_helpers.h"

namespace vklab {

const char* BarrierPolicyName(BarrierPolicy p) {
    switch (p) {
        case BarrierPolicy::OverSync: return "over-sync (full)";
        case BarrierPolicy::Minimal:  return "minimal (precise)";
    }
    return "?";
}

bool MinimalPolicyKnowsTransition(VkImageLayout oldLayout, VkImageLayout newLayout) {
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) return true;
    if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
        newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) return true;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) return true;
    return false;
}

VkImageMemoryBarrier2 BuildImageBarrier(
        VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
        VkImageAspectFlags aspect, BarrierPolicy policy) {
    VkImageMemoryBarrier2 b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.image = image;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    b.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    if (policy == BarrierPolicy::OverSync) {
        b.srcStageMask = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.dstStageMask = newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            ? VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.srcAccessMask = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
            ? 0
            : VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        b.dstAccessMask = newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            ? 0
            : VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    } else {
        // Minimal — old/new layout 조합에 맞는 stage/access.
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
            newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            b.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            b.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b.srcAccessMask = 0;
            b.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
                    newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
            b.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b.dstStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            b.dstAccessMask = 0;
        } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                    newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) {
            b.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            b.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            b.srcAccessMask = 0;
            b.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        } else {
            b.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            b.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            b.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        }
    }

    return b;
}

bool ShouldRecordBarrier(const BarrierInjection& injection) {
    return !injection.skipBarriers;
}

VkImageMemoryBarrier2 ApplyInjection(VkImageMemoryBarrier2 barrier,
                                     const BarrierInjection& injection) {
    if (injection.wrongSrcAccess) barrier.srcAccessMask = 0;
    return barrier;
}

int ExpectedBarrierCalls(const BarrierInjection& injection, int transitionsPerFrame) {
    if (transitionsPerFrame <= 0) return 0;
    return ShouldRecordBarrier(injection) ? transitionsPerFrame : 0;
}

namespace {

void DefaultPipelineBarrier2(VkCommandBuffer cmd, const VkDependencyInfo* dep) {
    vkCmdPipelineBarrier2(cmd, dep);
}

PipelineBarrier2Fn g_pipelineBarrier2 = &DefaultPipelineBarrier2;

}  // namespace

void SetPipelineBarrier2Hook(PipelineBarrier2Fn fn) {
    g_pipelineBarrier2 = fn ? fn : &DefaultPipelineBarrier2;
}

PipelineBarrier2Fn PipelineBarrier2Hook() { return g_pipelineBarrier2; }

void ImageLayoutTransitions(VkCommandBuffer cmd,
                            const VkImageMemoryBarrier2* barriers,
                            uint32_t count) {
    if (count == 0 || barriers == nullptr) return;
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = count;
    dep.pImageMemoryBarriers    = barriers;
    g_pipelineBarrier2(cmd, &dep);
}

void ImageLayoutTransitions(VkCommandBuffer cmd,
                            const std::vector<VkImageMemoryBarrier2>& barriers) {
    ImageLayoutTransitions(cmd, barriers.data(), (uint32_t)barriers.size());
}

void ImageLayoutTransition(VkCommandBuffer cmd,
                            VkImage image,
                            VkImageLayout oldLayout,
                            VkImageLayout newLayout,
                            VkImageAspectFlags aspect,
                            BarrierPolicy policy) {
    const VkImageMemoryBarrier2 barrier = BuildImageBarrier(
        image, oldLayout, newLayout, aspect, policy);
    ImageLayoutTransitions(cmd, &barrier, 1);
}

}  // namespace vklab
