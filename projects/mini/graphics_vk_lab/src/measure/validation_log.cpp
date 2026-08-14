#include "measure/validation_log.h"

namespace vklab {

ValidationLog& ValidationLog::Instance() {
    static ValidationLog s;
    return s;
}

void ValidationLog::Push(ValMsg m) {
    ring_[(size_t)head_] = std::move(m);
    head_ = (head_ + 1) % kRingSize;
    if (count_ < kRingSize) ++count_;
}

VkBool32 ValidationLog::Thunk(VkDebugUtilsMessageSeverityFlagBitsEXT sev,
                               VkDebugUtilsMessageTypeFlagsEXT,
                               const VkDebugUtilsMessengerCallbackDataEXT* data,
                               void*) {
    ValMsg m;
    m.sev = (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)   ? ValMsg::Sev::Error
          : (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ? ValMsg::Sev::Warning
          : ValMsg::Sev::Info;
    m.id   = data->messageIdNumber;
    m.text = data->pMessage ? data->pMessage : "";
    Instance().Push(std::move(m));
    return VK_FALSE;
}

}  // namespace vklab
