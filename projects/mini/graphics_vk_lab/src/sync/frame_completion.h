#pragma once

// FrameCompletionTracker — "이 자원을 다시 써도 되는가" 를 판정하는 상태만
// 모은 클래스. VkFence / VkSemaphore 를 만지지 않으므로 디바이스 없이 테스트한다.
//
// 추적 대상은 두 축이다.
//   - frame slot : command buffer 와 per-frame UBO/SSBO 의 재사용 시점.
//   - swapchain image : acquire 된 image 의 직전 사용 완료 시점.
//
// SyncMode 에 따라 완료의 근거가 다르다.
//   - Binary   : slot 마다 VkFence 하나. image 는 "마지막으로 이 image 를 쓴
//                slot" 을 기억했다가 그 slot 의 fence 를 기다린다.
//   - Timeline : 단조 증가하는 값 하나. slot/image 각각의 마지막 signal 값을
//                기억했다가 그 값을 기다린다.
//
// 두 체계는 섞이면 안 된다. 모드가 바뀌면 이전 모드의 기록은 전부 무효다.

#include "sync/timeline_sync.h"

#include <cstdint>
#include <cstddef>
#include <vector>

namespace vklab {

class FrameCompletionTracker {
public:
    static constexpr int kNoSlot = -1;

    void Init(size_t imageCount, size_t slotCount);

    // swapchain 재생성 — image 별 기록은 전부 무효. slot 별 기록은 유지된다
    // (command buffer 와 fence 는 재생성되지 않기 때문).
    void OnSwapchainRecreated(size_t imageCount);

    // 모드 전환 — 두 완료 체계가 섞이지 않도록 양쪽 기록을 모두 비운다.
    // 실제로 모드가 바뀌었으면 true (호출자는 이때 vkDeviceWaitIdle 한다).
    bool OnSyncModeChanged(SyncMode mode);

    SyncMode Mode() const { return mode_; }

    // 제출 기록. Binary 모드에서는 value 가 무시된다.
    void OnSubmit(int slot, uint32_t image, uint64_t value);

    // Timeline 모드에서 이 image 를 다시 쓰기 전에 기다려야 하는 값.
    bool NeedsTimelineWait(uint32_t image, uint64_t& outValue) const;

    // Timeline 모드에서 이 slot 을 다시 쓰기 전에 기다려야 하는 값.
    bool NeedsSlotWait(int slot, uint64_t& outValue) const;

    // Binary 모드에서 이 image 를 마지막으로 쓴 frame slot (없으면 kNoSlot).
    int LastSlotForImage(uint32_t image) const;

    uint64_t SlotValue(int slot) const;
    uint64_t ImageValue(uint32_t image) const;

    size_t ImageCount() const { return imageValues_.size(); }
    size_t SlotCount()  const { return slotValues_.size(); }

private:
    SyncMode              mode_ = SyncMode::Binary;
    std::vector<uint64_t> slotValues_;
    std::vector<uint64_t> imageValues_;
    std::vector<int>      imageSlots_;
};

}  // namespace vklab
