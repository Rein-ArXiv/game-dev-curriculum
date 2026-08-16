#include "device/queue_select.h"

namespace vklab {

bool SelectQueueFamilies(const std::vector<VkQueueFamilyProperties>& families,
                         const std::vector<VkBool32>& presentSupport,
                         QueueFamilies& out) {
    uint32_t g = kNoQueueFamily, c = kNoQueueFamily, t = kNoQueueFamily;

    for (uint32_t i = 0; i < (uint32_t)families.size(); ++i) {
        const VkBool32 presentSupported =
            i < presentSupport.size() ? presentSupport[i] : VK_TRUE;
        const VkQueueFlags flags = families[i].queueFlags;

        if ((flags & VK_QUEUE_GRAPHICS_BIT) && presentSupported &&
            g == kNoQueueFamily) {
            g = i;
        }
        if ((flags & VK_QUEUE_COMPUTE_BIT) &&
            !(flags & VK_QUEUE_GRAPHICS_BIT) && c == kNoQueueFamily) {
            c = i;
        }
        if ((flags & VK_QUEUE_TRANSFER_BIT) &&
            !(flags & VK_QUEUE_GRAPHICS_BIT) &&
            !(flags & VK_QUEUE_COMPUTE_BIT) && t == kNoQueueFamily) {
            t = i;
        }
    }

    if (g == kNoQueueFamily) return false;
    if (c == kNoQueueFamily) c = g;
    if (t == kNoQueueFamily) t = g;

    out.graphics = g;
    out.compute  = c;
    out.transfer = t;
    return true;
}

}  // namespace vklab
