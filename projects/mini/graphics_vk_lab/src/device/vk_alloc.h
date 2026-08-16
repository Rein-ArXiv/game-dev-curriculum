#pragma once

// VkAlloc — VMA (Vulkan Memory Allocator) 래퍼. Buffer 생성 헬퍼 두 개와
// VmaAllocator 접근자를 제공한다. Image 는 호출부가 Allocator() 로 직접 만든다
// (App::CreateDepth 의 vmaCreateImage).

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#include <cstddef>

namespace vklab {

class VkContext;

class VkAlloc {
public:
    void Init(VkContext& ctx);
    void Shutdown();

    VmaAllocator Allocator() { return allocator_; }

    // 정적 버퍼 (Device Local) — 한 번 업로드 + 영구 사용.
    void CreateBufferStatic(VkCommandBuffer cmd,
                             const void* data, size_t bytes,
                             VkBufferUsageFlags usage,
                             VkBuffer& outBuffer,
                             VmaAllocation& outAlloc,
                             VkBuffer& outStaging,
                             VmaAllocation& outStagingAlloc);

    // 동적 버퍼 (Host Visible + 영구 Map). 매 프레임 갱신용.
    void CreateBufferDynamic(size_t bytes,
                              VkBufferUsageFlags usage,
                              VkBuffer& outBuffer,
                              VmaAllocation& outAlloc,
                              void** outMapped);

private:
    VmaAllocator allocator_ = nullptr;
};

}  // namespace vklab
