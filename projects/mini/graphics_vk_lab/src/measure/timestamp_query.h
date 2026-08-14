#pragma once

#include "measure/query_ring.h"

#include <vulkan/vulkan.h>
#include <array>

namespace vklab {

class VkContext;

class TimestampQuery {
public:
    static constexpr int kRingSize = QueryRing::kRingSize;
    static constexpr int kHistorySize = 240;

    void Init(VkContext& ctx);
    void Shutdown(VkContext& ctx);

    void Begin(VkCommandBuffer cmd);
    void End(VkCommandBuffer cmd);
    void Poll(VkContext& ctx);

    double LastMs() const { return lastMs_; }
    double EmaMs()  const { return emaMs_;  }
    const std::array<float, kHistorySize>& History() const { return history_; }
    int HistoryHead() const { return historyHead_; }

private:
    VkQueryPool pool_ = VK_NULL_HANDLE;
    double      periodNs_ = 1.0;
    QueryRing   ring_;
    double lastMs_ = 0.0;
    double emaMs_  = 0.0;
    static constexpr double kEmaAlpha = 0.1;
    std::array<float, kHistorySize> history_{};
    int historyHead_ = 0;
};

}  // namespace vklab
