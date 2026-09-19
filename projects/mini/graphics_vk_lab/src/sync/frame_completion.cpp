#include "sync/frame_completion.h"

#include <algorithm>

namespace vklab {

void FrameCompletionTracker::Init(size_t imageCount, size_t slotCount) {
    slotValues_.assign(slotCount, 0);
    imageValues_.assign(imageCount, 0);
    imageSlots_.assign(imageCount, kNoSlot);
    slotSerial_.assign(slotCount, 0);
    imageSerial_.assign(imageCount, 0);
    nextSerial_ = 0;
}

void FrameCompletionTracker::OnSwapchainRecreated(size_t imageCount) {
    imageValues_.assign(imageCount, 0);
    imageSlots_.assign(imageCount, kNoSlot);
    imageSerial_.assign(imageCount, 0);
}

bool FrameCompletionTracker::OnSyncModeChanged(SyncMode mode) {
    if (mode == mode_) return false;
    mode_ = mode;
    std::fill(slotValues_.begin(), slotValues_.end(), (uint64_t)0);
    std::fill(imageValues_.begin(), imageValues_.end(), (uint64_t)0);
    std::fill(imageSlots_.begin(), imageSlots_.end(), kNoSlot);
    std::fill(slotSerial_.begin(), slotSerial_.end(), (uint64_t)0);
    std::fill(imageSerial_.begin(), imageSerial_.end(), (uint64_t)0);
    return true;
}

void FrameCompletionTracker::OnSubmit(int slot, uint32_t image, uint64_t value) {
    // 제출 일련번호는 모드와 무관하게 매 제출마다 하나씩 늘어난다. Binary 의
    // fence 는 slot 마다 하나뿐이므로 "그 fence 가 지금 어느 제출의 것인가" 는
    // 이 번호로만 구분된다.
    const uint64_t serial = ++nextSerial_;
    if (slot >= 0 && (size_t)slot < slotSerial_.size()) slotSerial_[(size_t)slot] = serial;
    if (image < imageSerial_.size()) imageSerial_[image] = serial;
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

bool FrameCompletionTracker::NeedsFenceWait(uint32_t image, int& outSlot) const {
    if (mode_ != SyncMode::Binary) return false;
    if (image >= imageSerial_.size()) return false;
    const uint64_t serial = imageSerial_[image];
    if (serial == 0) return false;                    // 아직 쓴 적 없는 image.
    const int slot = LastSlotForImage(image);
    if (slot < 0 || (size_t)slot >= slotSerial_.size()) return false;
    // slot 이 그 뒤 다시 제출됐으면 fence 는 더 새 제출의 것이다. 그 재제출은
    // 자기 차례에 이 slot 의 fence 를 이미 기다렸으므로 이 image 의 마지막
    // 사용은 그때 끝나 있었다 — 지금 기다릴 이유가 없다.
    if (slotSerial_[(size_t)slot] != serial) return false;
    outSlot = slot;
    return true;
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
