#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <string>

namespace vklab {

struct ValMsg {
    enum class Sev { Info, Warning, Error };
    Sev sev;
    int32_t id;
    std::string text;
};

class ValidationLog {
public:
    static constexpr int kRingSize = 256;

    static ValidationLog& Instance();

    void Push(ValMsg m);
    int  Count() const { return count_; }
    int  RingHead() const { return head_; }
    const ValMsg& Get(int i) const { return ring_[(size_t)i]; }

    // VkContext::SetDebugCallback 에 등록할 thunk.
    static VKAPI_ATTR VkBool32 VKAPI_CALL Thunk(
        VkDebugUtilsMessageSeverityFlagBitsEXT sev,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void*);

private:
    std::array<ValMsg, kRingSize> ring_{};
    int head_ = 0;
    int count_ = 0;
};

}  // namespace vklab
