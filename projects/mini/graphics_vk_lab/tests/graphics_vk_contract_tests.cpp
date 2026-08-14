// graphics_vk_lab contract tests — 디바이스 없이 돌아가는 계약 검사.
//
// 이 바이너리는 제품 TU 를 실제로 링크한다 (CMakeLists 의 graphics_vk_contract_tests
// 소스 목록). 배리어 구조체 빌더뿐 아니라 실제 기록 경로(ImageLayoutTransition),
// Scene 의 draw 기록 규칙, timestamp ring 인덱싱, swapchain 선택 규칙,
// frame/image 완료 추적까지가 검사 대상이다.
//
// 실행: ctest --test-dir build -C Release --output-on-failure

#include "barrier/barrier_helpers.h"
#include "device/swapchain_select.h"
#include "measure/query_ring.h"
#include "scene/scene.h"
#include "sync/frame_completion.h"
#include "sync/timeline_sync.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr)                                                          \
    do {                                                                     \
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

// ── 1. 배리어 구조체 ─────────────────────────────────────────────────────

void TestDrawSubmissions() {
    CHECK(vklab::ExpectedDrawSubmissions(vklab::DrawMode::PerDraw, 10000) == 10000);
    CHECK(vklab::ExpectedDrawSubmissions(vklab::DrawMode::Instanced, 10000) == 1);
    CHECK(vklab::ExpectedDrawSubmissions(vklab::DrawMode::Instanced, 0) == 0);

    CHECK(std::string(vklab::DrawModeName(vklab::DrawMode::PerDraw)) ==
          "Per-Draw (push constants x N)");
    CHECK(std::string(vklab::DrawModeName(vklab::DrawMode::Instanced)) ==
          "Instanced (1 vkCmdDrawIndexed)");
    CHECK(std::string(vklab::BarrierPolicyName(vklab::BarrierPolicy::Minimal)) ==
          "minimal (precise)");
    CHECK(std::string(vklab::BarrierPolicyName(vklab::BarrierPolicy::OverSync)) ==
          "over-sync (full)");
    CHECK(std::string(vklab::SyncModeName(vklab::SyncMode::Binary)) ==
          "Fence completion tracking");
    CHECK(std::string(vklab::SyncModeName(vklab::SyncMode::Timeline)) ==
          "Timeline completion tracking");
}

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

    // 보고서 항목 12 의 계약: Minimal 이 모르는 layout 쌍에서는 두 정책이
    // 비트 단위로 같은 값을 낸다 (조용히 over-sync 로 떨어진다).
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
    for (const vklab::BarrierInjection& inj : {none, skip, wrong}) {
        g_captureCalls = 0;
        int recorded = 0;
        for (int t = 0; t < 2; ++t) {   // 프레임당 color 전이 2회.
            if (!vklab::ShouldRecordBarrier(inj)) continue;
            const auto b = vklab::ApplyInjection(clean, inj);
            vklab::ImageLayoutTransitions(fakeCmd, &b, 1);
            ++recorded;
        }
        CHECK(recorded == vklab::ExpectedBarrierCalls(inj, 2));
        CHECK(g_captureCalls == vklab::ExpectedBarrierCalls(inj, 2));
    }
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
            CHECK(rec.lastMode == (mode == vklab::DrawMode::PerDraw ? 0u : 1u));
            // mesh 업로드가 없으므로 indexCount 는 0 이지만, RecordDraw 가
            // Scene 의 indexCount_ 를 그대로 넘기는지는 확인된다.
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

    // ring full — 새 기록을 열지 않는다.
    CHECK(ring.Begin() == -1);
    CHECK(!ring.Recording());
    CHECK(ring.End() == -1);
    CHECK(ring.InFlight() == 3);

    // 수확하면 다시 열린다. readIdx = (writeIdx - inFlight + kRingSize) % kRingSize.
    ring.Retire();
    CHECK(ring.InFlight() == 2);
    CHECK(ring.ReadIndex() == 1);
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
        CHECK(value > lastSlotValue[slot]);            // 단조 증가.
        CHECK(value > lastImageValue[image]);
        lastSlotValue[slot] = value;
        lastImageValue[image] = value;
    }

    // (b) 모드 전환 — fence 추적값과 timeline 값이 서로 오염되지 않는다.
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

    // Binary 모드: image 를 마지막으로 쓴 slot 을 기억한다.
    t.OnSubmit(1, 2, 0);
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
}

void TestTimelineCounter() {
    vklab::TimelineSemaphore timeline;
    CHECK(timeline.Current() == 0);
    CHECK(timeline.Next() == 1);
    CHECK(timeline.Next() == 2);
    CHECK(timeline.Current() == 2);
}

}  // namespace

int main() {
    TestDrawSubmissions();
    TestMinimalBarriers();
    TestOverSyncBarriers();
    TestImageLayoutTransitionRecords();
    TestBarrierInjection();
    TestRecordDrawMatchesExpectedSubmissions();
    TestSceneScaleContract();
    TestQueryRing();
    TestSwapchainSelection();
    TestFrameCompletionTracker();
    TestTimelineCounter();
    if (failures != 0) return 1;
    std::cout << "Vulkan contract tests passed\n";
    return 0;
}
