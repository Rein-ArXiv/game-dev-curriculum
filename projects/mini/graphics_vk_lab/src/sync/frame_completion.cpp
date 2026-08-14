#include "sync/frame_completion.h"

#include <algorithm>

namespace vklab {

void FrameCompletionTracker::Init(size_t imageCount, size_t slotCount) {
    slotValues_.assign(slotCount, 0);
    imageValues_.assign(imageCount, 0);
    imageSlots_.assign(imageCount, kNoSlot);
}

void FrameCompletionTracker::OnSwapchainRecreated(size_t imageCount) {
    imageValues_.assign(imageCount, 0);
    imageSlots_.assign(imageCount, kNoSlot);
}

bool FrameCompletionTracker::OnSyncModeChanged(SyncMode mode) {
    if (mode == mode_) return false;
    mode_ = mode;
    std::fill(slotValues_.begin(), slotValues_.end(), (uint64_t)0);
    std::fill(imageValues_.begin(), imageValues_.end(), (uint64_t)0);
    std::fill(imageSlots_.begin(), imageSlots_.end(), kNoSlot);
    return true;
}

void FrameCompletionTracker::OnSubmit(int slot, uint32_t image, uint64_t value) {
    if (image < imageSlots_.size()) imageSlots_[image] = slot;
    if (mode_ != SyncMode::Timeline) return;
    if (slot >= 0 && (size_t)slot < slotValues_.size()) slotValues_[(size_t)slot] = value;
    if (image < imageValues_.size()) imageValues_[image] = value;
}

bool FrameCompletionTracker::NeedsTimelineWait(uint32_t image, uint64_t& outValue) const {
    if (mode_ != SyncMode::Timeline) return false;
    if (image >= imageValues_.size()) return false;
    if (imageValues_[image] == 0) return false;
    outValue = imageValues_[image];
    return true;
}

bool FrameCompletionTracker::NeedsSlotWait(int slot, uint64_t& outValue) const {
    if (mode_ != SyncMode::Timeline) return false;
    if (slot < 0 || (size_t)slot >= slotValues_.size()) return false;
    if (slotValues_[(size_t)slot] == 0) return false;
    outValue = slotValues_[(size_t)slot];
    return true;
}

int FrameCompletionTracker::LastSlotForImage(uint32_t image) const {
    if (image >= imageSlots_.size()) return kNoSlot;
    return imageSlots_[image];
}

uint64_t FrameCompletionTracker::SlotValue(int slot) const {
    if (slot < 0 || (size_t)slot >= slotValues_.size()) return 0;
    return slotValues_[(size_t)slot];
}

uint64_t FrameCompletionTracker::ImageValue(uint32_t image) const {
    if (image >= imageValues_.size()) return 0;
    return imageValues_[image];
}

}  // namespace vklab
