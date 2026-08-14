#include "sync/timeline_sync.h"

#include <stdexcept>

namespace vklab {

const char* SyncModeName(SyncMode m) {
    switch (m) {
        case SyncMode::Binary:   return "Fence completion tracking";
        case SyncMode::Timeline: return "Timeline completion tracking";
    }
    return "?";
}

void TimelineSemaphore::Init(VkDevice device) {
    value_ = 0;
    VkSemaphoreTypeCreateInfo type{};
    type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type.initialValue = 0;

    VkSemaphoreCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    ci.pNext = &type;
    if (vkCreateSemaphore(device, &ci, nullptr, &sem_) != VK_SUCCESS)
        throw std::runtime_error("timeline vkCreateSemaphore failed");
}

void TimelineSemaphore::Shutdown(VkDevice device) {
    if (sem_) vkDestroySemaphore(device, sem_, nullptr);
    sem_ = VK_NULL_HANDLE;
    value_ = 0;
}

void TimelineSemaphore::Wait(VkDevice device, uint64_t value) {
    VkSemaphoreWaitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    info.semaphoreCount = 1;
    info.pSemaphores = &sem_;
    info.pValues = &value;
    if (vkWaitSemaphores(device, &info, UINT64_MAX) != VK_SUCCESS)
        throw std::runtime_error("vkWaitSemaphores failed");
}

}  // namespace vklab
