#define VMA_IMPLEMENTATION
#include "device/vk_alloc.h"
#include "device/vk_context.h"

#include <cstring>
#include <stdexcept>

namespace vklab {

void VkAlloc::Init(VkContext& ctx) {
    VmaAllocatorCreateInfo aci{};
    aci.instance       = ctx.Instance();
    aci.physicalDevice = ctx.Physical();
    aci.device         = ctx.Device();
    aci.vulkanApiVersion = VK_API_VERSION_1_3;
    if (vmaCreateAllocator(&aci, &allocator_) != VK_SUCCESS) {
        throw std::runtime_error("vmaCreateAllocator failed");
    }
}

void VkAlloc::Shutdown() {
    if (allocator_) { vmaDestroyAllocator(allocator_); allocator_ = nullptr; }
}

void VkAlloc::CreateBufferStatic(VkCommandBuffer cmd,
                                  const void* data, size_t bytes,
                                  VkBufferUsageFlags usage,
                                  VkBuffer& outBuffer,
                                  VmaAllocation& outAlloc,
                                  VkBuffer& outStaging,
                                  VmaAllocation& outStagingAlloc) {
    // Device Local target.
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = bytes;
    bci.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    vmaCreateBuffer(allocator_, &bci, &aci, &outBuffer, &outAlloc, nullptr);

    // Host Visible staging.
    VkBufferCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    sci.size  = bytes;
    sci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    sci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo saci{};
    saci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    saci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
               | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo sinfo{};
    vmaCreateBuffer(allocator_, &sci, &saci, &outStaging, &outStagingAlloc, &sinfo);

    std::memcpy(sinfo.pMappedData, data, bytes);
    vmaFlushAllocation(allocator_, outStagingAlloc, 0, bytes);

    VkBufferCopy copy{0, 0, bytes};
    vkCmdCopyBuffer(cmd, outStaging, outBuffer, 1, &copy);

    VkPipelineStageFlags2 dstStage = 0;
    VkAccessFlags2 dstAccess = 0;
    if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
        dstStage |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        dstAccess |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    }
    if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
        dstStage |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        dstAccess |= VK_ACCESS_2_INDEX_READ_BIT;
    }
    if (dstStage != 0) {
        VkBufferMemoryBarrier2 b{};
        b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        b.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b.dstStageMask = dstStage;
        b.dstAccessMask = dstAccess;
        b.buffer = outBuffer;
        b.offset = 0;
        b.size = bytes;

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.bufferMemoryBarrierCount = 1;
        dep.pBufferMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    }
}

void VkAlloc::CreateBufferDynamic(size_t bytes,
                                   VkBufferUsageFlags usage,
                                   VkBuffer& outBuffer,
                                   VmaAllocation& outAlloc,
                                   void** outMapped) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = bytes;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
              | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info{};
    vmaCreateBuffer(allocator_, &bci, &aci, &outBuffer, &outAlloc, &info);
    *outMapped = info.pMappedData;
}

}  // namespace vklab
