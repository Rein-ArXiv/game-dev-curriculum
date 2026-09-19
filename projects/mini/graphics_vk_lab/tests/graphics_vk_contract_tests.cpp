// graphics_vk_lab contract tests — 디바이스 없이 돌아가는 계약 검사.
//
// 이 바이너리는 제품 TU 를 실제로 링크한다 (CMakeLists 의 graphics_vk_contract_tests
// 소스 목록). 배리어 구조체 빌더뿐 아니라 실제 기록 경로(ImageLayoutTransition),
// Scene 의 draw 기록 규칙, timestamp ring 인덱싱, swapchain 선택 규칙, queue family
// 선택, descriptor set 모양, frame/image 완료 추적까지가 검사 대상이다.
//
// 실행 단위는 step 이다. 인자 없이 부르면 전부 돌고, `step04` 처럼 step 이름이나
// `queue_family_selection` 처럼 개별 이름을 주면 그것만 돈다. 어느 CHECK 가 어느
// step 소속인지는 파일 끝의 kStepTests 표에 있다.
//
// 실행: ctest --test-dir build -C Release --output-on-failure
//       ctest --test-dir build -C Release -R vk_step04_multi_queue
//       ./build/Release/graphics_vk_contract_tests step04

#include "barrier/barrier_helpers.h"
#include "device/descriptor_layout.h"
#include "device/queue_select.h"
#include "device/swapchain_select.h"
#include "measure/query_ring.h"
#include "scene/scene.h"
#include "sync/frame_completion.h"
#include "sync/timeline_sync.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

#define CHECK(expr)                                                          \
    do {                                                                     \
        ++checks;                                                            \
        if (!(expr)) {                                                       \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':' \
                      << __LINE__ << ")\n";                                  \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

// 비교 가능한 sentinel 핸들. VkImage 는 64bit 에서 포인터 핸들이고, 32bit
// 빌드에서는 VK_DEFINE_NON_DISPATCHABLE_HANDLE 이 uint64_t 이므로 캐스트가 다르다.
template <typename Handle>
Handle FakeHandle(uint64_t bits) {
    if constexpr (sizeof(Handle) == sizeof(uint64_t) &&
                  !std::is_pointer_v<Handle>) {
        return (Handle)bits;                       // 32bit 빌드: uint64_t 별칭.
    } else {
        return (Handle)(uintptr_t)bits;            // 64bit 빌드: 포인터 핸들.
    }
}

// ── 0. 토글 이름 ─────────────────────────────────────────────────────────
// 패널이 읽어 주는 이름은 그 토글을 도입한 step 소속이다. 셋을 한 함수에 두면
// 어느 step 을 돌려도 같은 CHECK 가 섞여 들어와 실행 단위가 흐려진다.

void TestDrawModeNames() {
    CHECK(vklab::ExpectedDrawSubmissions(vklab::DrawMode::PerDraw, 10000) == 10000);
    CHECK(vklab::ExpectedDrawSubmissions(vklab::DrawMode::Instanced, 10000) == 1);
    CHECK(vklab::ExpectedDrawSubmissions(vklab::DrawMode::Instanced, 0) == 0);

    CHECK(std::string(vklab::DrawModeName(vklab::DrawMode::PerDraw)) ==
          "Per-Draw (push constants x N)");
    CHECK(std::string(vklab::DrawModeName(vklab::DrawMode::Instanced)) ==
          "Instanced (1 vkCmdDrawIndexed)");
}

void TestBarrierPolicyNames() {
    CHECK(std::string(vklab::BarrierPolicyName(vklab::BarrierPolicy::Minimal)) ==
          "minimal (precise)");
    CHECK(std::string(vklab::BarrierPolicyName(vklab::BarrierPolicy::OverSync)) ==
          "over-sync (full)");
}

void TestSyncModeNames() {
    CHECK(std::string(vklab::SyncModeName(vklab::SyncMode::Binary)) ==
          "Fence completion tracking");
    CHECK(std::string(vklab::SyncModeName(vklab::SyncMode::Timeline)) ==
          "Timeline completion tracking");
}

// ── 1. 배리어 구조체 ─────────────────────────────────────────────────────

// image / oldLayout / newLayout / subresourceRange 는 이 lab 의 학습 주제 자체다.
// 셋 중 하나라도 상수로 고정되면 여기서 잡혀야 한다.
void CheckBarrierIdentity(const VkImageMemoryBarrier2& b,
                          VkImage expectImage,
                          VkImageLayout expectOld,
                          VkImageLayout expectNew,
                          VkImageAspectFlags expectAspect) {
    CHECK(b.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    CHECK(b.image == expectImage);
    CHECK(b.oldLayout == expectOld);
    CHECK(b.newLayout == expectNew);
    CHECK(b.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
    CHECK(b.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
    CHECK(b.subresourceRange.aspectMask == expectAspect);
    CHECK(b.subresourceRange.baseMipLevel == 0);
    CHECK(b.subresourceRange.baseArrayLayer == 0);
    CHECK(b.subresourceRange.levelCount == VK_REMAINING_MIP_LEVELS);
    CHECK(b.subresourceRange.layerCount == VK_REMAINING_ARRAY_LAYERS);
}

void TestMinimalBarriers() {
    const VkImage fake = FakeHandle<VkImage>(0xF00Du);

    // 렌더러가 실제로 만드는 전이 1 — acquire 직후.
    auto toColor = vklab::BuildImageBarrier(
        fake, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, vklab::BarrierPolicy::Minimal);
    CheckBarrierIdentity(toColor, fake, VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_ASPECT_COLOR_BIT);
    CHECK(toColor.srcStageMask == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
    CHECK(toColor.dstStageMask ==
          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    CHECK(toColor.srcAccessMask == 0);
    CHECK(toColor.dstAccessMask ==
          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    // 렌더러가 실제로 만드는 전이 2 — present 직전.
    auto toPresent = vklab::BuildImageBarrier(
        fake, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT, vklab::BarrierPolicy::Minimal);
    CheckBarrierIdentity(toPresent, fake,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                         VK_IMAGE_ASPECT_COLOR_BIT);
    CHECK(toPresent.srcStageMask ==
          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    CHECK(toPresent.dstStageMask == VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
    CHECK(toPresent.srcAccessMask ==
          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    CHECK(toPresent.dstAccessMask == 0);

    // 렌더러가 실제로 만드는 전이 3 — depth image 초기화 (App::CreateDepth).
    const VkImage fakeDepth = FakeHandle<VkImage>(0xBEEFu);
    auto toDepth = vklab::BuildImageBarrier(
        fakeDepth, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT, vklab::BarrierPolicy::Minimal);
    CheckBarrierIdentity(toDepth, fakeDepth, VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_ASPECT_DEPTH_BIT);
    CHECK(toDepth.srcStageMask == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
    CHECK(toDepth.dstStageMask ==
          (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
           VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT));
    CHECK(toDepth.srcAccessMask == 0);
    CHECK(toDepth.dstAccessMask ==
          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    // 두 색 전이는 서로 달라야 한다 — 하나로 고정하는 변이를 잡는다.
    CHECK(toColor.oldLayout != toPresent.oldLayout);
    CHECK(toColor.newLayout != toPresent.newLayout);
    CHECK(toColor.dstAccessMask != toPresent.dstAccessMask);
    CHECK(toDepth.newLayout != toColor.newLayout);
}

void TestOverSyncBarriers() {
    const VkImage fake = FakeHandle<VkImage>(0xF00Du);

    // Over-sync 도 앱이 실제로 만드는 두 전이로 검사한다.
    auto toColor = vklab::BuildImageBarrier(
        fake, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, vklab::BarrierPolicy::OverSync);
    CheckBarrierIdentity(toColor, fake, VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_ASPECT_COLOR_BIT);
    CHECK(toColor.srcStageMask == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
    CHECK(toColor.dstStageMask == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    CHECK(toColor.srcAccessMask == 0);
    CHECK(toColor.dstAccessMask ==
          (VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT));

    auto toPresent = vklab::BuildImageBarrier(
        fake, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT, vklab::BarrierPolicy::OverSync);
    CheckBarrierIdentity(toPresent, fake,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                         VK_IMAGE_ASPECT_COLOR_BIT);
    CHECK(toPresent.srcStageMask == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    CHECK(toPresent.dstStageMask == VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
    CHECK(toPresent.srcAccessMask ==
          (VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT));
    CHECK(toPresent.dstAccessMask == 0);

    // Over-sync 는 Minimal 보다 넓어야 의미가 있다.
    auto minimalPresent = vklab::BuildImageBarrier(
        fake, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT, vklab::BarrierPolicy::Minimal);
    CHECK(toPresent.srcStageMask != minimalPresent.srcStageMask);
    CHECK(toPresent.srcAccessMask != minimalPresent.srcAccessMask);

    // 보고서 항목 12 의 계약: Minimal 이 모르는 layout 쌍 가운데 UNDEFINED 소스·
    // PRESENT_SRC 대상이 아닌 쌍에서는 두 정책이 비트 단위로 같은 값을 낸다
    // (조용히 over-sync 로 떨어진다). 그 두 예외에서는 fallback 이 Over-sync 보다 넓다.
    CHECK(!vklab::MinimalPolicyKnowsTransition(
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    auto broadUnknown = vklab::BuildImageBarrier(
        fake, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, vklab::BarrierPolicy::OverSync);
    auto minimalUnknown = vklab::BuildImageBarrier(
        fake, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, vklab::BarrierPolicy::Minimal);
    CHECK(broadUnknown.srcStageMask  == minimalUnknown.srcStageMask);
    CHECK(broadUnknown.dstStageMask  == minimalUnknown.dstStageMask);
    CHECK(broadUnknown.srcAccessMask == minimalUnknown.srcAccessMask);
    CHECK(broadUnknown.dstAccessMask == minimalUnknown.dstAccessMask);
    CHECK(broadUnknown.srcStageMask == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    CHECK(broadUnknown.dstStageMask == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);

    // 반대로 아는 세 쌍에서는 반드시 달라야 한다.
    CHECK(vklab::MinimalPolicyKnowsTransition(
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
    CHECK(vklab::MinimalPolicyKnowsTransition(
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR));
    CHECK(vklab::MinimalPolicyKnowsTransition(
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL));
}

// ── 2. 실제 기록 경로 ────────────────────────────────────────────────────
// vkCmdPipelineBarrier2 seam 을 가로채 ImageLayoutTransition 이 무엇을
// 제출하는지 확인한다. 이 훅이 없으면 배리어 개수를 0 으로 낮춰도 아무 테스트도
// 실패하지 않는다.

struct CapturedBarrier {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    uint32_t count = 0;
    bool hadPointer = false;
    VkImageMemoryBarrier2 first{};
};

CapturedBarrier g_captured;
int g_captureCalls = 0;

void CaptureBarrier2(VkCommandBuffer cmd, const VkDependencyInfo* dep) {
    ++g_captureCalls;
    g_captured = CapturedBarrier{};
    g_captured.cmd = cmd;
    if (!dep) return;
    g_captured.count = dep->imageMemoryBarrierCount;
    g_captured.hadPointer = dep->pImageMemoryBarriers != nullptr;
    if (dep->imageMemoryBarrierCount > 0 && dep->pImageMemoryBarriers)
        g_captured.first = dep->pImageMemoryBarriers[0];
}

bool SameBarrier(const VkImageMemoryBarrier2& a, const VkImageMemoryBarrier2& b) {
    return a.image == b.image && a.oldLayout == b.oldLayout &&
           a.newLayout == b.newLayout && a.srcStageMask == b.srcStageMask &&
           a.dstStageMask == b.dstStageMask &&
           a.srcAccessMask == b.srcAccessMask &&
           a.dstAccessMask == b.dstAccessMask &&
           a.subresourceRange.aspectMask == b.subresourceRange.aspectMask &&
           a.subresourceRange.levelCount == b.subresourceRange.levelCount &&
           a.subresourceRange.layerCount == b.subresourceRange.layerCount;
}

void TestImageLayoutTransitionRecords() {
    const VkImage fake = FakeHandle<VkImage>(0xF00Du);
    const VkCommandBuffer fakeCmd = FakeHandle<VkCommandBuffer>(0xC0DEu);

    vklab::SetPipelineBarrier2Hook(&CaptureBarrier2);
    g_captureCalls = 0;

    struct Case {
        VkImageLayout oldLayout;
        VkImageLayout newLayout;
        VkImageAspectFlags aspect;
        vklab::BarrierPolicy policy;
    };
    const Case cases[] = {
        {VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         VK_IMAGE_ASPECT_COLOR_BIT, vklab::BarrierPolicy::Minimal},
        {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
         VK_IMAGE_ASPECT_COLOR_BIT, vklab::BarrierPolicy::Minimal},
        {VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         VK_IMAGE_ASPECT_COLOR_BIT, vklab::BarrierPolicy::OverSync},
        {VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
         VK_IMAGE_ASPECT_DEPTH_BIT, vklab::BarrierPolicy::Minimal},
    };

    int expectedCalls = 0;
    for (const Case& c : cases) {
        vklab::ImageLayoutTransition(fakeCmd, fake, c.oldLayout, c.newLayout,
                                     c.aspect, c.policy);
        ++expectedCalls;
        CHECK(g_captureCalls == expectedCalls);
        CHECK(g_captured.cmd == fakeCmd);
        CHECK(g_captured.count == 1);
        CHECK(g_captured.hadPointer);
        const auto expected = vklab::BuildImageBarrier(
            fake, c.oldLayout, c.newLayout, c.aspect, c.policy);
        CHECK(SameBarrier(g_captured.first, expected));
    }

    // 여러 image 를 한 dependency 로 — depth 배열 경로.
    std::vector<VkImageMemoryBarrier2> many;
    for (uint64_t i = 0; i < 3; ++i) {
        many.push_back(vklab::BuildImageBarrier(
            FakeHandle<VkImage>(0x1000u + i), VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_DEPTH_BIT, vklab::BarrierPolicy::Minimal));
    }
    vklab::ImageLayoutTransitions(fakeCmd, many);
    CHECK(g_captureCalls == expectedCalls + 1);
    CHECK(g_captured.count == 3);
    CHECK(g_captured.first.image == FakeHandle<VkImage>(0x1000u));

    vklab::SetPipelineBarrier2Hook(nullptr);
    CHECK(vklab::PipelineBarrier2Hook() != &CaptureBarrier2);
}

// ── 4. 실패 주입 계약 ────────────────────────────────────────────────────

void TestBarrierInjection() {
    const VkImage fake = FakeHandle<VkImage>(0xF00Du);
    const auto clean = vklab::BuildImageBarrier(
        fake, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT, vklab::BarrierPolicy::Minimal);

    vklab::BarrierInjection none{};
    CHECK(vklab::ShouldRecordBarrier(none));
    CHECK(vklab::ExpectedBarrierCalls(none, 2) == 2);
    CHECK(SameBarrier(vklab::ApplyInjection(clean, none), clean));

    vklab::BarrierInjection skip{};
    skip.skipBarriers = true;
    CHECK(!vklab::ShouldRecordBarrier(skip));
    CHECK(vklab::ExpectedBarrierCalls(skip, 2) == 0);

    vklab::BarrierInjection wrong{};
    wrong.wrongSrcAccess = true;
    CHECK(vklab::ShouldRecordBarrier(wrong));                 // 기록은 된다.
    CHECK(vklab::ExpectedBarrierCalls(wrong, 2) == 2);
    const auto broken = vklab::ApplyInjection(clean, wrong);
    CHECK(clean.srcAccessMask != 0);                          // 원본은 비어 있지 않다.
    CHECK(broken.srcAccessMask == 0);                         // 주입은 srcAccess 만 망친다.
    CHECK(broken.dstAccessMask == clean.dstAccessMask);
    CHECK(broken.oldLayout == clean.oldLayout);
    CHECK(broken.newLayout == clean.newLayout);
    CHECK(broken.image == clean.image);

    // 실제 기록 경로에서도 skip 이 배리어를 0 회로 만드는지 — App::RecordFrame 과
    // 같은 순서(ShouldRecordBarrier → ApplyInjection → ImageLayoutTransitions).
    const VkCommandBuffer fakeCmd = FakeHandle<VkCommandBuffer>(0xC0DEu);
    vklab::SetPipelineBarrier2Hook(&CaptureBarrier2);
    // snippet-begin: barrier-injection-record
    for (const vklab::BarrierInjection& inj : {none, skip, wrong}) {
        g_captureCalls = 0;
        int recorded = 0;
        for (int t = 0; t < 2; ++t) {   // 프레임당 color 전이 2회.
            if (!vklab::ShouldRecordBarrier(inj)) continue;
            const auto b = vklab::ApplyInjection(clean, inj);
            vklab::ImageLayoutTransitions(fakeCmd, &b, 1);
            ++recorded;
        }
        CHECK(recorded == (inj.skipBarriers ? 0 : 2));
        CHECK(g_captureCalls == vklab::ExpectedBarrierCalls(inj, 2));
    }
    // snippet-end: barrier-injection-record
    CHECK(g_captured.first.srcAccessMask == 0);   // 마지막이 wrong 주입.
    vklab::SetPipelineBarrier2Hook(nullptr);
}

// ── 5. Scene::RecordDraw ─────────────────────────────────────────────────

class FakeDrawRecorder final : public vklab::IDrawRecorder {
public:
    void PushConstants(const vklab::PushConstants& pc) override {
        ++pushes;
        lastMode = pc.mode;
    }
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount) override {
        ++draws;
        totalInstances += instanceCount;
        lastIndexCount = indexCount;
        maxInstances = instanceCount > maxInstances ? instanceCount : maxInstances;
    }

    int pushes = 0;
    int draws = 0;
    uint64_t totalInstances = 0;
    uint32_t maxInstances = 0;
    uint32_t lastIndexCount = 0xFFFFFFFFu;
    uint32_t lastMode = 0xFFFFFFFFu;
};

void TestRecordDrawMatchesExpectedSubmissions() {
    for (int n : {1, 1000, 10000}) {
        vklab::Scene scene;
        scene.Build(n);
        CHECK(scene.ObjectCount() == n);

        for (auto mode : {vklab::DrawMode::PerDraw, vklab::DrawMode::Instanced}) {
            FakeDrawRecorder rec;
            vklab::GlCounters counters{};
            scene.RecordDraw(rec, mode, counters);

            // snippet-begin: draw-submission-match
            const int expected = vklab::ExpectedDrawSubmissions(mode, n);
            CHECK(counters.drawCalls + counters.instancedDraws == expected);
            CHECK(rec.draws == expected);
            CHECK(counters.pushConstants ==
                  (mode == vklab::DrawMode::PerDraw ? n : 1));
            CHECK(rec.pushes == counters.pushConstants);
            // Instanced 는 1회 draw 에 N 인스턴스, PerDraw 는 N회 draw 에 1씩.
            CHECK(rec.totalInstances == (uint64_t)n);
            CHECK(rec.maxInstances ==
                  (mode == vklab::DrawMode::PerDraw ? 1u : (uint32_t)n));
            // snippet-end: draw-submission-match
            CHECK(rec.lastMode == (mode == vklab::DrawMode::PerDraw ? 0u : 1u));
            // mesh 업로드가 없어 indexCount_ 가 0 이므로, 이 대조는 RecordDraw 가
            // 리터럴 0 을 넘기는 변이를 구별하지 못한다 (알려진 검증 공백).
            CHECK(rec.lastIndexCount == scene.IndexCount());
        }
    }

    // 알려진 계약 어긋남(보고서용): Instanced + objectCount 0 에서
    // ExpectedDrawSubmissions 는 0 을 말하지만 RecordDraw 는 instanceCount=0 인
    // draw 를 1회 기록한다. 현재 동작을 고정해 둔다 — 바뀌면 여기서 잡힌다.
    {
        vklab::Scene empty;
        empty.Build(0);
        CHECK(empty.ObjectCount() == 0);
        FakeDrawRecorder rec;
        vklab::GlCounters counters{};
        empty.RecordDraw(rec, vklab::DrawMode::Instanced, counters);
        CHECK(vklab::ExpectedDrawSubmissions(vklab::DrawMode::Instanced, 0) == 0);
        CHECK(counters.instancedDraws == 1);
        CHECK(rec.maxInstances == 0);
    }
}

// ── 6c. Scene 규모 계약 (step07 의 3·4) ─────────────────────────────────

void TestSceneScaleContract() {
    vklab::Scene scene;
    for (int n : {1000, 10000, 100000}) {
        scene.Build(n);
        CHECK(scene.ObjectCount() == n);
    }
    CHECK(vklab::kMaxObjects == 100000);

    // WriteInstances 는 capacity 를 절대 넘지 않는다.
    scene.Build(100);
    std::vector<vklab::InstanceData> dst(200);
    std::memset(dst.data(), 0xAB, dst.size() * sizeof(vklab::InstanceData));
    vklab::InstanceData sentinel;
    std::memcpy(&sentinel, &dst[10], sizeof(sentinel));
    CHECK(scene.WriteInstances(dst.data(), 10) == 10);
    CHECK(std::memcmp(&dst[10], &sentinel, sizeof(sentinel)) == 0);  // 11번째는 안 건드림.
    CHECK(scene.WriteInstances(dst.data(), 0) == 0);
    CHECK(scene.WriteInstances(dst.data(), 150) == 100);   // 오브젝트 수가 상한.

    // 쓰인 내용이 ModelMatrix 와 일치해야 한다 (0 으로 채우는 변이를 잡는다).
    scene.Build(4);
    std::vector<vklab::InstanceData> four(4);
    CHECK(scene.WriteInstances(four.data(), 4) == 4);
    bool anyNonZeroTranslation = false;
    bool allW1 = true;
    for (const auto& inst : four) {
        if (inst.model[3][0] != 0.0f || inst.model[3][1] != 0.0f ||
            inst.model[3][2] != 0.0f) anyNonZeroTranslation = true;
        if (inst.model[3][3] != 1.0f) allW1 = false;
        if (inst.color.a != 1.0f) allW1 = false;
    }
    CHECK(anyNonZeroTranslation);
    CHECK(allW1);

    // Update 는 각 오브젝트의 회전을 실제로 진행시킨다.
    scene.Build(64);
    std::vector<vklab::InstanceData> before(64), after(64);
    scene.WriteInstances(before.data(), 64);
    scene.Update(0.5f);
    scene.WriteInstances(after.data(), 64);
    int changed = 0;
    for (int i = 0; i < 64; ++i)
        if (std::memcmp(&before[i].model, &after[i].model, sizeof(glm::mat4)) != 0)
            ++changed;
    CHECK(changed == 64);

    // 같은 count 로 다시 만들면 배치가 결정적이어야 한다 (고정 seed).
    scene.Build(64);
    std::vector<vklab::InstanceData> rebuilt(64);
    scene.WriteInstances(rebuilt.data(), 64);
    CHECK(std::memcmp(before.data(), rebuilt.data(),
                      sizeof(vklab::InstanceData) * 64) == 0);
}

// ── 6a. timestamp ring ───────────────────────────────────────────────────

void TestQueryRing() {
    vklab::QueryRing ring;
    CHECK(vklab::QueryRing::kRingSize == 3);
    CHECK(ring.InFlight() == 0);
    CHECK(!ring.Recording());
    CHECK(ring.ReadIndex() == -1);      // 수확할 것이 없다.
    CHECK(ring.End() == -1);            // Begin 없이 End 는 무효.

    // 슬롯을 0,1,2 순서로 채운다 — 수확 없이 3개까지.
    for (int i = 0; i < vklab::QueryRing::kRingSize; ++i) {
        CHECK(ring.Begin() == i);
        CHECK(ring.Recording());
        CHECK(ring.End() == i);
        CHECK(!ring.Recording());
        CHECK(ring.InFlight() == i + 1);
        CHECK(ring.ReadIndex() == 0);   // 가장 오래된 것은 계속 0번.
    }

    // snippet-begin: ring-full-guard
    // ring full — 새 기록을 열지 않는다.
    CHECK(ring.Begin() == -1);
    CHECK(!ring.Recording());
    CHECK(ring.End() == -1);
    CHECK(ring.InFlight() == 3);

    // 수확하면 다시 열린다. readIdx = (writeIdx - inFlight + kRingSize) % kRingSize.
    ring.Retire();
    CHECK(ring.InFlight() == 2);
    CHECK(ring.ReadIndex() == 1);
    // snippet-end: ring-full-guard
    CHECK(ring.Begin() == 0);           // writeIdx 는 한 바퀴 돌아 0.
    CHECK(ring.End() == 0);
    CHECK(ring.InFlight() == 3);
    CHECK(ring.ReadIndex() == 1);

    // 전부 수확하면 빈 상태로 돌아간다.
    ring.Retire(); ring.Retire(); ring.Retire();
    CHECK(ring.InFlight() == 0);
    CHECK(ring.ReadIndex() == -1);
    ring.Retire();                      // 언더플로 방지.
    CHECK(ring.InFlight() == 0);

    // 긴 루프에서 write/read 가 항상 kRingSize 안에 머문다.
    vklab::QueryRing loop;
    for (int frame = 0; frame < 100; ++frame) {
        const int slot = loop.Begin();
        if (slot >= 0) {
            CHECK(slot >= 0 && slot < vklab::QueryRing::kRingSize);
            CHECK(loop.End() == slot);
        }
        const int readIdx = loop.ReadIndex();
        if (readIdx >= 0) {
            CHECK(readIdx < vklab::QueryRing::kRingSize);
            CHECK(readIdx != loop.WriteIndex() || loop.InFlight() == vklab::QueryRing::kRingSize);
            loop.Retire();
        }
        CHECK(loop.InFlight() <= vklab::QueryRing::kRingSize);
    }
}

// ── 6b. swapchain 선택 ───────────────────────────────────────────────────

void TestSwapchainSelection() {
    const std::vector<VkPresentModeKHR> all = {
        VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_IMMEDIATE_KHR};
    const std::vector<VkPresentModeKHR> noMailbox = {
        VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR};
    const std::vector<VkPresentModeKHR> fifoOnly = {VK_PRESENT_MODE_FIFO_KHR};

    // vsync 켜짐 → 무조건 FIFO.
    CHECK(vklab::ChoosePresentMode(all, true) == VK_PRESENT_MODE_FIFO_KHR);
    CHECK(vklab::ChoosePresentMode(noMailbox, true) == VK_PRESENT_MODE_FIFO_KHR);
    // vsync 꺼짐 → MAILBOX > IMMEDIATE > FIFO.
    CHECK(vklab::ChoosePresentMode(all, false) == VK_PRESENT_MODE_MAILBOX_KHR);
    CHECK(vklab::ChoosePresentMode(noMailbox, false) == VK_PRESENT_MODE_IMMEDIATE_KHR);
    CHECK(vklab::ChoosePresentMode(fifoOnly, false) == VK_PRESENT_MODE_FIFO_KHR);
    CHECK(vklab::ChoosePresentMode({}, false) == VK_PRESENT_MODE_FIFO_KHR);

    // 선호 format 이 있으면 그것을, 없으면 첫 후보를.
    const VkSurfaceFormatKHR preferred{VK_FORMAT_B8G8R8A8_UNORM,
                                       VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    const VkSurfaceFormatKHR other{VK_FORMAT_R8G8B8A8_UNORM,
                                   VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    const VkSurfaceFormatKHR wrongSpace{VK_FORMAT_B8G8R8A8_UNORM,
                                        VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT};

    auto chosen = vklab::ChooseSurfaceFormat({other, preferred});
    CHECK(chosen.format == VK_FORMAT_B8G8R8A8_UNORM);
    CHECK(chosen.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);

    chosen = vklab::ChooseSurfaceFormat({other, wrongSpace});
    CHECK(chosen.format == other.format);              // colorSpace 가 다르면 선호 아님.
    CHECK(chosen.colorSpace == other.colorSpace);

    chosen = vklab::ChooseSurfaceFormat(
        {{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}});
    CHECK(chosen.format == VK_FORMAT_B8G8R8A8_UNORM);  // "아무거나" 표기.

    bool threw = false;
    try {
        (void)vklab::ChooseSurfaceFormat({});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

// ── 6d. push constant 예산 ───────────────────────────────────────────────
// PC 블록은 shader 의 push_constant 블록과 같은 레이아웃이어야 하고, 사양이
// 보장하는 최소 maxPushConstantsSize(128 bytes) 안에 들어와야 한다. 프레임
// 데이터까지 여기 넣으면 128 bytes 한도 장치에서 깨진다.

void TestPushConstantBudget() {
    CHECK(sizeof(vklab::PushConstants) == 84);      // mat4(64) + vec4(16) + uint(4).
    CHECK(sizeof(vklab::PushConstants) <= 128);     // 사양이 보장하는 최소 한도.
    CHECK(offsetof(vklab::PushConstants, model) == 0);
    CHECK(offsetof(vklab::PushConstants, color) == 64);
    CHECK(offsetof(vklab::PushConstants, mode) == 80);

    // 인스턴스 항목은 std430 의 mat4 + vec4 그대로 — 80 bytes/instance.
    CHECK(sizeof(vklab::InstanceData) == 80);
    CHECK(offsetof(vklab::InstanceData, color) == 64);

    // 프레임 데이터를 push constant 에 합치면 한도를 넘는다. UBO 로 나눈 이유다.
    CHECK(sizeof(vklab::FrameUBO) == 96);
    CHECK(sizeof(vklab::PushConstants) + sizeof(vklab::FrameUBO) > 128);
}

// ── 6e. queue family 선택 ────────────────────────────────────────────────

std::vector<VkQueueFamilyProperties> MakeFamilies(
    std::initializer_list<VkQueueFlags> flags) {
    std::vector<VkQueueFamilyProperties> families;
    for (VkQueueFlags f : flags) {
        VkQueueFamilyProperties p{};
        p.queueFlags = f;
        p.queueCount = 1;
        families.push_back(p);
    }
    return families;
}

void TestQueueFamilySelection() {
    // universal(graphics+compute+transfer) / dedicated compute / dedicated transfer.
    constexpr VkQueueFlags kG = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT |
                                VK_QUEUE_TRANSFER_BIT;
    constexpr VkQueueFlags kC = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    constexpr VkQueueFlags kT = VK_QUEUE_TRANSFER_BIT;

    // snippet-begin: queue-family-fallback
    // (a) dedicated 가 다 있는 장치 — 셋이 서로 다른 family 로 갈린다.
    vklab::QueueFamilies q;
    CHECK(vklab::SelectQueueFamilies(MakeFamilies({kG, kC, kT}), {}, q));
    CHECK(q.graphics == 0);
    CHECK(q.compute == 1);
    CHECK(q.transfer == 2);

    // (b) dedicated 가 하나도 없으면 셋 다 graphics family 로 접힌다.
    vklab::QueueFamilies only;
    CHECK(vklab::SelectQueueFamilies(MakeFamilies({kG}), {}, only));
    CHECK(only.graphics == 0);
    CHECK(only.compute == 0);          // c == kNoQueueFamily → c = g.
    CHECK(only.transfer == 0);         // t == kNoQueueFamily → t = g.
    // snippet-end: queue-family-fallback

    // (c) graphics 비트를 함께 가진 family 는 dedicated compute 가 아니다.
    vklab::QueueFamilies twoUniversal;
    CHECK(vklab::SelectQueueFamilies(MakeFamilies({kG, kG}), {}, twoUniversal));
    CHECK(twoUniversal.graphics == 0);
    CHECK(twoUniversal.compute == 0);
    CHECK(twoUniversal.transfer == 0);

    // (d) transfer 는 graphics·compute 비트가 모두 없어야 dedicated 이다.
    vklab::QueueFamilies noDedicatedTransfer;
    CHECK(vklab::SelectQueueFamilies(MakeFamilies({kG, kC}), {},
                                     noDedicatedTransfer));
    CHECK(noDedicatedTransfer.compute == 1);
    CHECK(noDedicatedTransfer.transfer == 0);   // family 1 은 compute 비트를 가진다.

    // (e) present 를 지원하지 않는 graphics family 는 건너뛴다.
    vklab::QueueFamilies present;
    CHECK(vklab::SelectQueueFamilies(MakeFamilies({kG, kG}),
                                     {VK_FALSE, VK_TRUE}, present));
    CHECK(present.graphics == 1);

    // (f) present 가능한 graphics family 가 없으면 그 장치는 후보에서 빠진다.
    vklab::QueueFamilies none;
    CHECK(!vklab::SelectQueueFamilies(MakeFamilies({kG, kC}),
                                      {VK_FALSE, VK_FALSE}, none));
    CHECK(!vklab::SelectQueueFamilies(MakeFamilies({kC, kT}), {}, none));
    CHECK(!vklab::SelectQueueFamilies({}, {}, none));
    CHECK(none.graphics == vklab::kNoQueueFamily);   // 실패 시 out 을 건드리지 않는다.
    CHECK(none.compute == vklab::kNoQueueFamily);
    CHECK(none.transfer == vklab::kNoQueueFamily);

    // (g) presentSupport 가 family 수보다 짧으면 모자란 쪽은 지원으로 본다.
    //     surface 없이 장치만 고르는 경로가 이 형태다.
    vklab::QueueFamilies partial;
    CHECK(vklab::SelectQueueFamilies(MakeFamilies({kG, kC}), {VK_TRUE}, partial));
    CHECK(partial.graphics == 0);
    CHECK(partial.compute == 1);
}

// ── 6f. descriptor set 모양 ──────────────────────────────────────────────
// 이 lab 의 set 0 은 UBO 1 + SSBO 1 이고 descriptor array 가 없다. 그래서
// descriptorIndexing / runtimeDescriptorArray 를 켜지 않아도 device 생성이
// 성공한다. 그 판단의 근거가 되는 모양을 여기서 고정한다.

void TestDescriptorSetShape() {
    const auto binds = vklab::SceneSetBindings();
    CHECK(binds.size() == 2);
    CHECK(binds[0].binding == 0);
    CHECK(binds[0].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    CHECK(binds[0].stageFlags ==
          (VkShaderStageFlags)(VK_SHADER_STAGE_VERTEX_BIT |
                               VK_SHADER_STAGE_FRAGMENT_BIT));
    CHECK(binds[1].binding == 1);
    CHECK(binds[1].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    CHECK(binds[1].stageFlags == (VkShaderStageFlags)VK_SHADER_STAGE_VERTEX_BIT);

    // snippet-begin: descriptor-not-bindless
    // "bindless 가 아니다"의 실체 — 모든 binding 이 원소 하나짜리이고, layout 에
    // update-after-bind 플래그가 없다. 배열 descriptor 를 넣는 순간 깨진다.
    for (const VkDescriptorSetLayoutBinding& b : binds) {
        CHECK(b.descriptorCount == 1);
        CHECK(b.pImmutableSamplers == nullptr);
    }
    CHECK(vklab::SceneSetLayoutFlags() == 0);
    CHECK((vklab::SceneSetLayoutFlags() &
           VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT) == 0);
    // snippet-end: descriptor-not-bindless

    // pool 은 layout 에서 파생된다 — binding 을 늘리고 pool 을 그대로 두면 여기서
    // 잡힌다. 실제 호출부는 setCount = kFramesInFlight 로 부른다.
    for (uint32_t sets : {1u, 2u, 4u}) {
        const auto sizes = vklab::SceneDescriptorPoolSizes(sets);
        CHECK(sizes.size() == binds.size());   // 두 타입이 달라 접히지 않는다.
        for (const VkDescriptorSetLayoutBinding& b : binds) {
            uint32_t available = 0;
            for (const VkDescriptorPoolSize& s : sizes)
                if (s.type == b.descriptorType) available += s.descriptorCount;
            CHECK(available >= b.descriptorCount * sets);
        }
    }
}

// ── 15. frame/image 완료 추적 ────────────────────────────────────────────

void TestFrameCompletionTracker() {
    using vklab::FrameCompletionTracker;
    using vklab::SyncMode;

    constexpr int kSlots = 2;
    constexpr uint32_t kImages = 3;

    // (a) Timeline: slot/image 별 마지막 값이 submit 의 signal 값과 일치하고
    //     단조 증가한다.
    FrameCompletionTracker t;
    t.Init(kImages, kSlots);
    CHECK(t.Mode() == SyncMode::Binary);
    CHECK(t.ImageCount() == kImages);
    CHECK(t.SlotCount() == kSlots);
    CHECK(t.OnSyncModeChanged(SyncMode::Timeline));
    CHECK(!t.OnSyncModeChanged(SyncMode::Timeline));   // 같은 모드는 no-op.

    uint64_t value = 0;
    uint64_t lastSlotValue[kSlots] = {0, 0};
    uint64_t lastImageValue[kImages] = {0, 0, 0};
    for (int frame = 0; frame < 12; ++frame) {
        const int slot = frame % kSlots;
        const uint32_t image = (uint32_t)(frame % kImages);

        uint64_t wait = 0;
        CHECK(t.NeedsSlotWait(slot, wait) == (lastSlotValue[slot] != 0));
        if (lastSlotValue[slot] != 0) CHECK(wait == lastSlotValue[slot]);
        wait = 0;
        CHECK(t.NeedsTimelineWait(image, wait) == (lastImageValue[image] != 0));
        if (lastImageValue[image] != 0) CHECK(wait == lastImageValue[image]);

        ++value;
        t.OnSubmit(slot, image, value);
        CHECK(t.SlotValue(slot) == value);
        CHECK(t.ImageValue(image) == value);
        CHECK(t.SlotValue(slot) > lastSlotValue[slot]);      // 단조 증가.
        CHECK(t.ImageValue(image) > lastImageValue[image]);
        lastSlotValue[slot] = t.SlotValue(slot);
        lastImageValue[image] = t.ImageValue(image);
    }

    // (b) 모드 전환 — fence 추적값과 timeline 값이 서로 오염되지 않는다.
    // snippet-begin: sync-mode-isolation
    CHECK(t.OnSyncModeChanged(SyncMode::Binary));
    for (uint32_t i = 0; i < kImages; ++i) {
        uint64_t w = 0;
        CHECK(!t.NeedsTimelineWait(i, w));             // Binary 에서는 값 대기 없음.
        CHECK(t.ImageValue(i) == 0);
        CHECK(t.LastSlotForImage(i) == FrameCompletionTracker::kNoSlot);
    }
    for (int s = 0; s < kSlots; ++s) {
        uint64_t w = 0;
        CHECK(!t.NeedsSlotWait(s, w));
        CHECK(t.SlotValue(s) == 0);
    }
    // snippet-end: sync-mode-isolation

    // Binary 모드: image 를 마지막으로 쓴 slot 을 기억한다.
    t.OnSubmit(1, 2, 7);                              // 0 이 아닌 값도 Binary 에서는 무시된다.
    CHECK(t.LastSlotForImage(2) == 1);
    CHECK(t.ImageValue(2) == 0);                       // 값은 쓰지 않는다.
    t.OnSubmit(0, 2, 0);
    CHECK(t.LastSlotForImage(2) == 0);

    // 다시 Timeline 으로 — 이전 Binary 의 slot 기록이 남아 잘못된 대기를
    // 만들면 안 된다.
    CHECK(t.OnSyncModeChanged(SyncMode::Timeline));
    CHECK(t.LastSlotForImage(2) == FrameCompletionTracker::kNoSlot);
    uint64_t w = 0;
    CHECK(!t.NeedsTimelineWait(2, w));

    // (c) swapchain 재생성 — image 별 추적값이 0 으로 초기화된다.
    FrameCompletionTracker r;
    r.Init(kImages, kSlots);
    CHECK(r.OnSyncModeChanged(SyncMode::Timeline));
    r.OnSubmit(0, 0, 41);
    r.OnSubmit(1, 1, 42);
    CHECK(r.ImageValue(1) == 42);
    CHECK(r.SlotValue(1) == 42);
    r.OnSwapchainRecreated(4);
    CHECK(r.ImageCount() == 4);
    for (uint32_t i = 0; i < 4; ++i) {
        uint64_t v = 0;
        CHECK(r.ImageValue(i) == 0);
        CHECK(!r.NeedsTimelineWait(i, v));
        CHECK(r.LastSlotForImage(i) == FrameCompletionTracker::kNoSlot);
    }
    // slot 쪽은 유효한 채로 남는다 (command buffer / fence 는 재생성되지 않는다).
    uint64_t slotWait = 0;
    CHECK(r.NeedsSlotWait(1, slotWait));
    CHECK(slotWait == 42);

    // 범위 밖 인덱스는 조용히 무시한다.
    r.OnSubmit(99, 99, 7);
    CHECK(r.SlotValue(99) == 0);
    CHECK(r.ImageValue(99) == 0);
    CHECK(r.LastSlotForImage(99) == FrameCompletionTracker::kNoSlot);

    // (d) Binary 과대 대기 — slot fence 는 slot 당 하나뿐이므로, image 를 쓴
    //     slot 이 그 뒤 다시 제출됐다면 그 fence 는 더 새 작업의 것이다. 그
    //     재제출이 이미 이전 제출을 기다렸으므로 여기서 또 기다리면 순수한
    //     과대 대기다. 3 image / 2 slot 라운드로빈에서 F3 부터는 acquire 한
    //     image 의 마지막 사용이 항상 두 프레임 전이고, 그 slot 은 그 사이
    //     한 번 더 제출됐다 — 그래서 F3 이후 fence 대기는 한 번도 필요 없다.
    // snippet-begin: binary-over-wait
    FrameCompletionTracker b;
    b.Init(kImages, kSlots);
    CHECK(b.Mode() == SyncMode::Binary);
    int staleWaits = 0;                 // 옛 정책(LastSlotForImage)이 걸었을 대기.
    int neededWaits = 0;                // 새 정책(NeedsFenceWait)이 거는 대기.
    for (int frame = 0; frame < 12; ++frame) {
        const int slot = frame % kSlots;
        const uint32_t image = (uint32_t)(frame % kImages);

        int waitSlot = FrameCompletionTracker::kNoSlot;
        const bool needs = b.NeedsFenceWait(image, waitSlot);
        if (b.LastSlotForImage(image) != FrameCompletionTracker::kNoSlot) ++staleWaits;
        if (needs) {
            ++neededWaits;
            CHECK(waitSlot == b.LastSlotForImage(image));
        } else {
            CHECK(waitSlot == FrameCompletionTracker::kNoSlot);
        }
        // F0..F2 는 image 첫 사용이라 대기가 없고, F3 부터는 위 이유로 없다.
        CHECK(!needs);
        b.OnSubmit(slot, image, 0);
    }
    CHECK(neededWaits == 0);            // 12 프레임 내내 fence 대기 0.
    CHECK(staleWaits == 9);             // 옛 정책은 F3..F11 에서 9번 걸었다.
    // snippet-end: binary-over-wait

    // 같은 image 를 같은 slot 이 연달아 쓰면(예: image 하나짜리 swapchain)
    // fence 는 여전히 그 제출의 것이므로 대기가 실제로 필요하다.
    FrameCompletionTracker s;
    s.Init(1, kSlots);
    int one = FrameCompletionTracker::kNoSlot;
    CHECK(!s.NeedsFenceWait(0, one));              // 첫 사용 전에는 대기 없음.
    s.OnSubmit(0, 0, 0);
    CHECK(s.NeedsFenceWait(0, one));               // 직전 제출이 그대로 살아 있다.
    CHECK(one == 0);
    s.OnSubmit(1, 0, 0);                           // 다른 slot 이 같은 image 를 씀.
    one = FrameCompletionTracker::kNoSlot;
    CHECK(s.NeedsFenceWait(0, one));
    CHECK(one == 1);
    s.OnSubmit(0, 0, 0);                           // slot 0 재제출 — image 도 갱신.
    one = FrameCompletionTracker::kNoSlot;
    CHECK(s.NeedsFenceWait(0, one));
    CHECK(one == 0);

    // Timeline 모드에서는 fence 대기 판정이 아예 나오지 않는다.
    CHECK(s.OnSyncModeChanged(SyncMode::Timeline));
    int none = FrameCompletionTracker::kNoSlot;
    CHECK(!s.NeedsFenceWait(0, none));
    // 범위 밖 image 도 조용히 false.
    CHECK(s.OnSyncModeChanged(SyncMode::Binary));
    s.OnSubmit(0, 0, 0);
    CHECK(!s.NeedsFenceWait(99, none));
    // swapchain 재생성 뒤에는 image 기록이 비므로 대기가 없다.
    s.OnSwapchainRecreated(2);
    CHECK(!s.NeedsFenceWait(0, none));
    CHECK(!s.NeedsFenceWait(1, none));
}

void TestTimelineCounter() {
    vklab::TimelineSemaphore timeline;
    CHECK(timeline.Current() == 0);
    CHECK(timeline.Next() == 1);
    CHECK(timeline.Next() == 2);
    CHECK(timeline.Current() == 2);
}

// ── 실행 단위 ────────────────────────────────────────────────────────────
// 소속은 "그 step 이 무엇을 추가했는가"로 정한다. swapchain 협상과 push
// constant 예산은 골격 단계가 세운 계약이고, ring 은 측정 인프라, 배리어 네
// 항목은 barrier 정책, queue family 는 queue 모델, 완료 추적은 semaphore 토글,
// descriptor 모양은 bindless 판단, draw 제출 수와 규모는 스케일 실험이 세운다.
// snippet-begin: step-test-table
struct StepTest {
    const char* step;
    const char* name;
    void (*fn)();
};

const StepTest kStepTests[] = {
    {"step01", "swapchain_negotiation",     &TestSwapchainSelection},
    {"step01", "push_constant_budget",      &TestPushConstantBudget},
    {"step02", "timestamp_ring",            &TestQueryRing},
    {"step03", "barrier_policy_names",      &TestBarrierPolicyNames},
    {"step03", "minimal_barriers",          &TestMinimalBarriers},
    {"step03", "oversync_barriers",         &TestOverSyncBarriers},
    {"step03", "barrier_record_path",       &TestImageLayoutTransitionRecords},
    {"step03", "barrier_injection",         &TestBarrierInjection},
    {"step04", "queue_family_selection",    &TestQueueFamilySelection},
    {"step05", "sync_mode_names",           &TestSyncModeNames},
    {"step05", "timeline_counter",          &TestTimelineCounter},
    {"step05", "frame_completion_tracking", &TestFrameCompletionTracker},
    {"step06", "descriptor_set_shape",      &TestDescriptorSetShape},
    {"step07", "draw_mode_names",           &TestDrawModeNames},
    {"step07", "draw_submission_counts",    &TestRecordDrawMatchesExpectedSubmissions},
    {"step07", "scene_scale_contract",      &TestSceneScaleContract},
};
// snippet-end: step-test-table

}  // namespace

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;

    int ran = 0;
    for (const StepTest& t : kStepTests) {
        if (filter && std::strcmp(filter, t.step) != 0 &&
            std::strcmp(filter, t.name) != 0) {
            continue;
        }
        const int checksBefore = checks;
        const int failuresBefore = failures;
        t.fn();
        ++ran;
        std::cout << '[' << t.step << "] " << t.name << " ... "
                  << (failures == failuresBefore ? "ok" : "FAILED")
                  << " (" << checks - checksBefore << " checks)\n";
    }

    if (ran == 0) {
        std::cerr << "no test matches `" << (filter ? filter : "") << "` — "
                     "step01..step07 또는 개별 이름을 주십시오.\n";
        return 2;
    }

    std::cout << (filter ? filter : "all") << ": " << ran << " test(s), "
              << checks << " checks, " << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
