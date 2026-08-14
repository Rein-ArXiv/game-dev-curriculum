#pragma once

// Step 5 의 Semaphore 모드 토글.
//
//   - Binary   : per-frame VkFence로 frame/image 자원 재사용을 추적.
//   - Timeline : 증가하는 VkSemaphore 값으로 frame/image 재사용을 추적.
//
// swapchain acquire/present 연결에는 두 모드 모두 binary semaphore가 필요하다.

#include <vulkan/vulkan.h>

namespace vklab {

enum class SyncMode {
    Binary,
    Timeline,
};

const char* SyncModeName(SyncMode m);

// Timeline 의 사용 패턴.
class TimelineSemaphore {
public:
    void Init(VkDevice device);
    void Shutdown(VkDevice device);

    VkSemaphore Handle() const { return sem_; }
    uint64_t    Next() { return ++value_; }
    uint64_t    Current() const { return value_; }

    void Wait(VkDevice device, uint64_t value);

private:
    VkSemaphore sem_ = VK_NULL_HANDLE;
    uint64_t    value_ = 0;
};

}  // namespace vklab
