#include "measure/timestamp_query.h"

#include "device/vk_context.h"

namespace vklab {

void TimestampQuery::Init(VkContext& ctx) {
    VkQueryPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    ci.queryCount = kRingSize * 2;
    vkCreateQueryPool(ctx.Device(), &ci, nullptr, &pool_);
    periodNs_ = ctx.TimestampPeriodNs();
    history_.fill(0.0f);
}

void TimestampQuery::Shutdown(VkContext& ctx) {
    if (pool_) vkDestroyQueryPool(ctx.Device(), pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
}

void TimestampQuery::ResetAll(VkCommandBuffer cmd) {
    vkCmdResetQueryPool(cmd, pool_, 0, kRingSize * 2);
}

void TimestampQuery::Begin(VkCommandBuffer cmd) {
    const int slot = ring_.Begin();
    if (slot < 0) return;
    vkCmdResetQueryPool(cmd, pool_, slot * 2, 2);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        pool_, slot * 2);
}

void TimestampQuery::End(VkCommandBuffer cmd) {
    const int slot = ring_.End();
    if (slot < 0) return;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        pool_, slot * 2 + 1);
}

void TimestampQuery::Poll(VkContext& ctx) {
    const int readIdx = ring_.ReadIndex();
    if (readIdx < 0) return;
    uint64_t ts[2] = {0, 0};
    VkResult r = vkGetQueryPoolResults(ctx.Device(), pool_,
                                        readIdx * 2, 2,
                                        sizeof(ts), ts, sizeof(uint64_t),
                                        VK_QUERY_RESULT_64_BIT);
    if (r != VK_SUCCESS) return;
    ring_.Retire();
    if (ts[1] <= ts[0]) return;
    lastMs_ = (double)(ts[1] - ts[0]) * (double)periodNs_ / 1.0e6;
    if (emaMs_ <= 0.0) emaMs_ = lastMs_;
    else emaMs_ = emaMs_ * (1.0 - kEmaAlpha) + lastMs_ * kEmaAlpha;
    history_[(size_t)historyHead_] = (float)lastMs_;
    historyHead_ = (historyHead_ + 1) % kHistorySize;
}

}  // namespace vklab
