#pragma once

// Step 3 의 Pipeline Barrier 모드 토글.
//
//   - OverSync : 필요한 layout은 유지하되 dependency mask를 넓힌 비교 모드.
//   - Minimal  : 정확한 stage/access mask 만 명시 (권장).
//
// 같은 transition을 보수적인 mask와 필요한 mask로 기록해 validation과 시간을
// 비교한다. 넓은 mask가 잘못된 layout/queue ownership을 고쳐 주지는 않으며,
// 작은 장면에서는 유의미한 성능 차이가 없을 수 있다.
//
// Minimal 이 정확한 mask 를 아는 layout 쌍은 아래 세 가지뿐이고, 그 밖의 조합은
// ALL_COMMANDS + MEMORY_READ|WRITE 보수적 fallback 으로 떨어진다. OverSync 가
// UNDEFINED 소스·PRESENT_SRC 대상에 두는 예외를 fallback 은 두지 않으므로, 그 두
// 경우를 뺀 미지 쌍에서만 OverSync 와 비트 단위로 같다. 테스트는 그중 한 쌍
// (TRANSFER_DST -> SHADER_READ_ONLY)을 고정한다.
//
//   UNDEFINED               -> COLOR_ATTACHMENT_OPTIMAL
//   COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
//   UNDEFINED               -> DEPTH_ATTACHMENT_OPTIMAL

#include <vulkan/vulkan.h>

#include <vector>

namespace vklab {

enum class BarrierPolicy {
    OverSync,
    Minimal,
};

const char* BarrierPolicyName(BarrierPolicy p);

VkImageMemoryBarrier2 BuildImageBarrier(
    VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
    VkImageAspectFlags aspect, BarrierPolicy policy);

// Minimal 정책이 정확한 stage/access 를 아는 layout 쌍인지 — 아니면 OverSync 와
// 같은 보수적 fallback 이 나온다.
bool MinimalPolicyKnowsTransition(VkImageLayout oldLayout, VkImageLayout newLayout);

// ── 의도적 실패 주입 ────────────────────────────────────────────────────
// PROJECTS_PLAN 의 "실패 주입" 축. 배리어를 통째로 빼거나 srcAccessMask 를
// 틀리게 넣었을 때 validation layer 가 무엇을 잡는지 관측하기 위한 토글이다.
// 기본값은 둘 다 꺼짐이므로 정상 경로의 동작은 변하지 않는다.
struct BarrierInjection {
    bool skipBarriers   = false;  // ImageLayoutTransition 자체를 기록하지 않는다.
    bool wrongSrcAccess = false;  // srcAccessMask 를 0 으로 덮어쓴다.
};

// 배리어를 실제로 기록해야 하는가.
bool ShouldRecordBarrier(const BarrierInjection& injection);

// 주입 설정을 배리어 구조체에 반영한다 (skip 은 여기서 다루지 않는다).
VkImageMemoryBarrier2 ApplyInjection(VkImageMemoryBarrier2 barrier,
                                     const BarrierInjection& injection);

// 프레임당 transition 수가 주어졌을 때 counters_.barrierCalls 의 기대값.
int ExpectedBarrierCalls(const BarrierInjection& injection, int transitionsPerFrame);

// ── vkCmdPipelineBarrier2 seam ──────────────────────────────────────────
// 실제 기록 경로를 headless 테스트에서 가로채기 위한 함수 포인터. 기본값은
// vkCmdPipelineBarrier2 를 그대로 호출하므로 제품 동작은 변하지 않는다.
using PipelineBarrier2Fn = void (*)(VkCommandBuffer, const VkDependencyInfo*);

// nullptr 을 넘기면 기본 구현으로 되돌린다.
void SetPipelineBarrier2Hook(PipelineBarrier2Fn fn);
PipelineBarrier2Fn PipelineBarrier2Hook();

void ImageLayoutTransition(VkCommandBuffer cmd,
                            VkImage image,
                            VkImageLayout oldLayout,
                            VkImageLayout newLayout,
                            VkImageAspectFlags aspect,
                            BarrierPolicy policy);

// 여러 image 를 한 번의 dependency 로 전이한다 (depth image 배열 등).
void ImageLayoutTransitions(VkCommandBuffer cmd,
                            const VkImageMemoryBarrier2* barriers,
                            uint32_t count);
void ImageLayoutTransitions(VkCommandBuffer cmd,
                            const std::vector<VkImageMemoryBarrier2>& barriers);

}  // namespace vklab
