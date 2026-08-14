// graphics_dx_lab 계약 테스트 — GPU·디바이스 없이 제품 TU 를 그대로 링크해
// 명령 기록·배리어·descriptor 계획·타임스탬프 상태 기계·행렬 합성을 검증한다.
//
// 링크되는 제품 TU: src/scene/scene_record.cpp, src/barrier/barrier_helpers.cpp,
// src/descriptor/heap_manager.cpp, src/measure/timestamp_query.cpp.

#include "app/frame_cb.h"
#include "barrier/barrier_helpers.h"
#include "descriptor/heap_manager.h"
#include "measure/timestamp_query.h"
#include "scene/scene.h"

#include "d3d12_stubs.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

using namespace DirectX;

namespace {

int failures = 0;
constexpr UINT ninetySix = 96;

#define CHECK(expr)                                                          \
    do {                                                                     \
        if (!(expr)) {                                                       \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':' \
                      << __LINE__ << ")\n";                                  \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

bool NearF(float a, float b, float tol = 1e-4f) {
    return std::fabs(a - b) <= tol;
}

// ---------------------------------------------------------------------------
// 명령 기록을 남기는 command list 대역.
// ---------------------------------------------------------------------------

struct DrawRec {
    UINT indexCount = 0;
    UINT instanceCount = 0;
};

struct RootConstRec {
    UINT rootIndex = 0;
    UINT num = 0;
    UINT destOffset = 0;
    std::vector<UINT> data;
};

struct BarrierRec {
    UINT count = 0;
    std::vector<D3D12_RESOURCE_BARRIER> list;
};

struct FakeCmdList : dxlab_test::StubGraphicsCommandList {
    std::vector<DrawRec> draws;
    std::vector<ID3D12PipelineState*> psoSets;
    std::vector<ID3D12RootSignature*> rootSigSets;
    std::vector<RootConstRec> rootConsts;
    std::vector<BarrierRec> barriers;
    int topologySets = 0;
    int vertexBufferSets = 0;
    int indexBufferSets = 0;
    int rootSrvSets = 0;
    int endQueries = 0;
    int resolves = 0;

    void STDMETHODCALLTYPE DrawIndexedInstanced(UINT indexCount, UINT instanceCount,
                                                UINT, INT, UINT) override {
        draws.push_back({indexCount, instanceCount});
    }
    void STDMETHODCALLTYPE SetPipelineState(ID3D12PipelineState* pso) override {
        psoSets.push_back(pso);
    }
    void STDMETHODCALLTYPE SetGraphicsRootSignature(
            ID3D12RootSignature* rs) override {
        rootSigSets.push_back(rs);
    }
    void STDMETHODCALLTYPE SetGraphicsRoot32BitConstants(
            UINT rootIndex, UINT num, const void* src, UINT destOffset) override {
        RootConstRec rec;
        rec.rootIndex = rootIndex;
        rec.num = num;
        rec.destOffset = destOffset;
        rec.data.resize(num);
        if (num != 0) std::memcpy(rec.data.data(), src, num * sizeof(UINT));
        rootConsts.push_back(std::move(rec));
    }
    void STDMETHODCALLTYPE SetGraphicsRootShaderResourceView(
            UINT, D3D12_GPU_VIRTUAL_ADDRESS) override {
        ++rootSrvSets;
    }
    void STDMETHODCALLTYPE ResourceBarrier(
            UINT count, const D3D12_RESOURCE_BARRIER* list) override {
        BarrierRec rec;
        rec.count = count;
        rec.list.assign(list, list + count);
        barriers.push_back(std::move(rec));
    }
    void STDMETHODCALLTYPE IASetPrimitiveTopology(
            D3D12_PRIMITIVE_TOPOLOGY) override {
        ++topologySets;
    }
    void STDMETHODCALLTYPE IASetVertexBuffers(
            UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*) override {
        ++vertexBufferSets;
    }
    void STDMETHODCALLTYPE IASetIndexBuffer(
            const D3D12_INDEX_BUFFER_VIEW*) override {
        ++indexBufferSets;
    }
    void STDMETHODCALLTYPE EndQuery(ID3D12QueryHeap*, D3D12_QUERY_TYPE,
                                    UINT) override {
        ++endQueries;
    }
    void STDMETHODCALLTYPE ResolveQueryData(ID3D12QueryHeap*, D3D12_QUERY_TYPE,
                                            UINT, UINT, ID3D12Resource*,
                                            UINT64) override {
        ++resolves;
    }
};

// RootSig / PSO / Resource 는 포인터 동일성만 검사하므로 sentinel 로 충분하다.
ID3D12RootSignature* FakeRootSig() {
    return reinterpret_cast<ID3D12RootSignature*>(static_cast<uintptr_t>(0x2000));
}
ID3D12PipelineState* FakePsoA() {
    return reinterpret_cast<ID3D12PipelineState*>(static_cast<uintptr_t>(0x3000));
}
ID3D12PipelineState* FakePsoB() {
    return reinterpret_cast<ID3D12PipelineState*>(static_cast<uintptr_t>(0x4000));
}
ID3D12Resource* FakeResource() {
    return reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0x1000));
}

// ---------------------------------------------------------------------------
// Step 3 — 드로우 제출 수 계약(헬퍼 자기 자신).
// ---------------------------------------------------------------------------

void TestDrawSubmissions() {
    CHECK(dxlab::ExpectedDrawSubmissions(dxlab::DrawMode::PerDraw, 10000) == 10000);
    CHECK(dxlab::ExpectedDrawSubmissions(dxlab::DrawMode::Instanced, 10000) == 1);
    CHECK(dxlab::ExpectedDrawSubmissions(dxlab::DrawMode::Instanced, 0) == 0);
}

// ---------------------------------------------------------------------------
// Step 3/4 — Scene::RecordDraw 가 실제로 기록하는 명령 검증.
// ---------------------------------------------------------------------------

struct RecordResult {
    FakeCmdList cmd;
    dxlab::GlCounters counters{};
};

void RunRecord(RecordResult& out, int objectCount,
               dxlab::DrawMode mode,
               dxlab::RootSigPolicy rsPolicy,
               dxlab::PsoSwitchPolicy psoPolicy,
               ID3D12PipelineState* secondary = nullptr) {
    dxlab::Scene scene;
    scene.BuildObjects(objectCount);
    scene.RecordDraw(&out.cmd, mode, rsPolicy, psoPolicy, FakeRootSig(),
                     FakePsoA(), secondary, out.counters);
}

void TestSceneRecordsPerDraw() {
    // (a) PerDraw + 오브젝트 100개 -> DrawIndexedInstanced 100회.
    RecordResult r;
    RunRecord(r, 100, dxlab::DrawMode::PerDraw, dxlab::RootSigPolicy::PerDraw,
              dxlab::PsoSwitchPolicy::PerDraw);
    CHECK(r.cmd.draws.size() == 100);
    CHECK(r.counters.drawCalls == 100);
    CHECK(r.counters.instancedDraws == 0);
    for (const auto& d : r.cmd.draws) CHECK(d.instanceCount == 1);

    // 기록 진입부는 프레임당 1회.
    CHECK(r.cmd.topologySets == 1);
    CHECK(r.cmd.vertexBufferSets == 1);
    CHECK(r.cmd.indexBufferSets == 1);

    // (b) RootSigPolicy::PerDraw -> Scene 내부 SetGraphicsRootSignature 100회.
    CHECK(r.cmd.rootSigSets.size() == 100);
    for (auto* rs : r.cmd.rootSigSets) CHECK(rs == FakeRootSig());

    // (c) PsoSwitchPolicy::PerDraw -> SetPipelineState 100회.
    CHECK(r.cmd.psoSets.size() == 100);

    // (d) 드로우마다 루트 상수 (0,1,0),(0,16,4),(0,4,20).
    CHECK(r.cmd.rootConsts.size() == 300);
    bool sequenceOk = (r.cmd.rootConsts.size() == 300);
    for (size_t i = 0; sequenceOk && i + 2 < r.cmd.rootConsts.size(); i += 3) {
        const auto& a = r.cmd.rootConsts[i];
        const auto& b = r.cmd.rootConsts[i + 1];
        const auto& c = r.cmd.rootConsts[i + 2];
        sequenceOk = a.rootIndex == 0 && a.num == 1  && a.destOffset == 0 &&
                     b.rootIndex == 0 && b.num == 16 && b.destOffset == 4 &&
                     c.rootIndex == 0 && c.num == 4  && c.destOffset == 20;
    }
    CHECK(sequenceOk);
    // useInstancing 은 Per-Draw 경로에서 항상 0.
    if (!r.cmd.rootConsts.empty()) CHECK(r.cmd.rootConsts[0].data[0] == 0u);
}

void TestSceneRecordsGlobalPolicies() {
    // (b) RootSigPolicy::Global -> Scene 은 RootSig 를 건드리지 않는다.
    // (c) PsoSwitchPolicy::PerPass -> Scene 은 PSO 를 건드리지 않는다.
    RecordResult r;
    RunRecord(r, 100, dxlab::DrawMode::PerDraw, dxlab::RootSigPolicy::Global,
              dxlab::PsoSwitchPolicy::PerPass);
    CHECK(r.cmd.rootSigSets.empty());
    CHECK(r.cmd.psoSets.empty());
    CHECK(r.cmd.draws.size() == 100);
    CHECK(r.counters.rootSigSets == 0);
    CHECK(r.counters.pipelineSets == 0);
}

void TestSceneRecordsInstanced() {
    RecordResult r;
    RunRecord(r, 100, dxlab::DrawMode::Instanced, dxlab::RootSigPolicy::Global,
              dxlab::PsoSwitchPolicy::PerPass);
    CHECK(r.cmd.draws.size() == 1);
    if (!r.cmd.draws.empty()) CHECK(r.cmd.draws[0].instanceCount == 100);
    CHECK(r.counters.instancedDraws == 1);
    CHECK(r.counters.drawCalls == 0);
    // 루트 상수는 useInstancing 1개만, 값은 1.
    CHECK(r.cmd.rootConsts.size() == 1);
    if (!r.cmd.rootConsts.empty()) {
        CHECK(r.cmd.rootConsts[0].num == 1);
        CHECK(r.cmd.rootConsts[0].destOffset == 0);
        CHECK(r.cmd.rootConsts[0].data[0] == 1u);
    }
}

void TestSceneRecordsPerMaterial() {
    // PerMaterial 은 머티리얼 2그룹으로 나눠 PSO 를 2회만 바꾼다.
    RecordResult r;
    RunRecord(r, 100, dxlab::DrawMode::PerDraw, dxlab::RootSigPolicy::Global,
              dxlab::PsoSwitchPolicy::PerMaterial, FakePsoB());
    CHECK(r.cmd.psoSets.size() == 2);
    CHECK(r.cmd.psoSets[0] == FakePsoA());
    CHECK(r.cmd.psoSets[1] == FakePsoB());
    CHECK(r.cmd.draws.size() == 100);
    CHECK(r.counters.drawCalls == 100);
}

// 항목 3 — 헬퍼 예측값과 실제 기록 결과를 맞물리게 한다.
void TestRecordedSubmissionsMatchHelper() {
    {
        RecordResult r;
        RunRecord(r, 100, dxlab::DrawMode::PerDraw, dxlab::RootSigPolicy::Global,
                  dxlab::PsoSwitchPolicy::PerPass);
        CHECK(r.counters.drawCalls + r.counters.instancedDraws ==
              dxlab::ExpectedDrawSubmissions(dxlab::DrawMode::PerDraw, 100));
        CHECK((int)r.cmd.draws.size() ==
              dxlab::ExpectedDrawSubmissions(dxlab::DrawMode::PerDraw, 100));
    }
    {
        RecordResult r;
        RunRecord(r, 100, dxlab::DrawMode::Instanced, dxlab::RootSigPolicy::Global,
                  dxlab::PsoSwitchPolicy::PerPass);
        CHECK(r.counters.drawCalls + r.counters.instancedDraws ==
              dxlab::ExpectedDrawSubmissions(dxlab::DrawMode::Instanced, 100));
        CHECK((int)r.cmd.draws.size() ==
              dxlab::ExpectedDrawSubmissions(dxlab::DrawMode::Instanced, 100));
    }
    {
        // 알려진 불일치: objectCount==0 일 때 헬퍼는 0 을 예측하지만 기록부는
        // instanceCount=0 인 드로우를 한 번 제출한다. 덮지 않고 그대로 못박아
        // 둔다 — 어느 쪽이든 바뀌면 이 테스트가 알려 준다.
        RecordResult r;
        RunRecord(r, 0, dxlab::DrawMode::Instanced, dxlab::RootSigPolicy::Global,
                  dxlab::PsoSwitchPolicy::PerPass);
        CHECK(dxlab::ExpectedDrawSubmissions(dxlab::DrawMode::Instanced, 0) == 0);
        CHECK(r.counters.instancedDraws == 1);
        CHECK(r.cmd.draws.size() == 1);
        if (!r.cmd.draws.empty()) CHECK(r.cmd.draws[0].instanceCount == 0);
    }
}

// (e) 루트 상수 슬롯 규약이 루트 시그니처 예산과 정확히 맞물리는지.
void TestRootConstantBudget() {
    static_assert(dxlab::kRootConstUseInstancingOffset +
                      dxlab::kRootConstUseInstancingCount <=
                  dxlab::kRootConstTotalDwords, "useInstancing 슬롯이 예산 밖");
    static_assert(dxlab::kRootConstModelOffset + dxlab::kRootConstModelCount <=
                  dxlab::kRootConstTotalDwords, "model 슬롯이 예산 밖");
    static_assert(dxlab::kRootConstColorOffset + dxlab::kRootConstColorCount ==
                  dxlab::kRootConstTotalDwords, "color 슬롯이 예산 끝과 불일치");
    CHECK(dxlab::kRootConstUseInstancingOffset +
              dxlab::kRootConstUseInstancingCount <= dxlab::kRootConstTotalDwords);
    CHECK(dxlab::kRootConstModelOffset + dxlab::kRootConstModelCount <=
          dxlab::kRootConstTotalDwords);
    CHECK(dxlab::kRootConstColorOffset + dxlab::kRootConstColorCount ==
          dxlab::kRootConstTotalDwords);
    // 슬롯이 겹치지 않는다.
    CHECK(dxlab::kRootConstUseInstancingOffset +
              dxlab::kRootConstUseInstancingCount <= dxlab::kRootConstModelOffset);
    CHECK(dxlab::kRootConstModelOffset + dxlab::kRootConstModelCount <=
          dxlab::kRootConstColorOffset);
}

// ---------------------------------------------------------------------------
// Step 5 — descriptor 수명 계획.
// ---------------------------------------------------------------------------

void TestDescriptorLifetimePlan() {
    // capacity 96 / framesInFlight 3 -> 슬라이스 24 (transient 3 + persistent 1).
    auto transient = dxlab::PlanNullSrvWrites(
        dxlab::HeapPolicy::RebuildEachFrame, 5, 3, 80,  ninetySix,
        0, dxlab::HeapPolicy::RebuildEachFrame);
    CHECK(transient.first == 48);
    CHECK(transient.count == 24);
    CHECK(transient.writes == 24);
    CHECK(transient.persistentCount == 0);

    auto firstPersistent = dxlab::PlanNullSrvWrites(
        dxlab::HeapPolicy::PersistentTable, 0, 3, 20, ninetySix,
        0, dxlab::HeapPolicy::RebuildEachFrame);
    CHECK(firstPersistent.first == 72);
    CHECK(firstPersistent.writes == 20);
    CHECK(firstPersistent.persistentCount == 20);

    auto reused = dxlab::PlanNullSrvWrites(
        dxlab::HeapPolicy::PersistentTable, 2, 3, 20, ninetySix,
        20, dxlab::HeapPolicy::PersistentTable);
    CHECK(reused.writes == 0);
    CHECK(reused.persistentCount == 20);
}

bool RangesOverlap(const dxlab::DescriptorWritePlan& a,
                   const dxlab::DescriptorWritePlan& b) {
    if (a.count == 0 || b.count == 0) return false;
    return a.first < b.first + b.count && b.first < a.first + a.count;
}

// 항목 11 — 구간 불변식. step05 가 규정한 실패 신호 'frame 구간 겹침' 을
// 자동 판정으로 바꾼다.
void TestDescriptorRangesDisjoint() {
    const UINT framesInFlight = 3;
    const UINT capacity = ninetySix;
    const UINT requested = 32;

    dxlab::DescriptorWritePlan frames[3];
    for (UINT f = 0; f < framesInFlight; ++f) {
        frames[f] = dxlab::PlanNullSrvWrites(
            dxlab::HeapPolicy::RebuildEachFrame, f, framesInFlight, requested,
            capacity, 0, dxlab::HeapPolicy::RebuildEachFrame);
        CHECK(frames[f].count > 0);
        CHECK(frames[f].first + frames[f].count <= capacity);
    }
    // (a) frame slot 구간끼리 겹치지 않는다.
    CHECK(!RangesOverlap(frames[0], frames[1]));
    CHECK(!RangesOverlap(frames[0], frames[2]));
    CHECK(!RangesOverlap(frames[1], frames[2]));

    // (b) persistent 구간은 어떤 frame slot 구간과도 겹치지 않는다.
    auto persistent = dxlab::PlanNullSrvWrites(
        dxlab::HeapPolicy::PersistentTable, 0, framesInFlight, requested,
        capacity, 0, dxlab::HeapPolicy::RebuildEachFrame);
    CHECK(persistent.count > 0);
    CHECK(persistent.first + persistent.count <= capacity);
    CHECK(!RangesOverlap(persistent, frames[0]));
    CHECK(!RangesOverlap(persistent, frames[1]));
    CHECK(!RangesOverlap(persistent, frames[2]));

    // 앱이 실제로 쓰는 크기(capacity 100000, framesInFlight 3, 요청 32768)도
    // transient 3슬라이스 + persistent 구간을 모두 담는다.
    const UINT appCapacity = 100000;
    const UINT appRequested = 32768;
    dxlab::DescriptorWritePlan appFrames[3];
    for (UINT f = 0; f < framesInFlight; ++f) {
        appFrames[f] = dxlab::PlanNullSrvWrites(
            dxlab::HeapPolicy::RebuildEachFrame, f, framesInFlight, appRequested,
            appCapacity, 0, dxlab::HeapPolicy::RebuildEachFrame);
        CHECK(appFrames[f].count > 0);
        CHECK(appFrames[f].first + appFrames[f].count <= appCapacity);
    }
    CHECK(!RangesOverlap(appFrames[0], appFrames[1]));
    CHECK(!RangesOverlap(appFrames[0], appFrames[2]));
    CHECK(!RangesOverlap(appFrames[1], appFrames[2]));
    auto appPersistent = dxlab::PlanNullSrvWrites(
        dxlab::HeapPolicy::PersistentTable, 0, framesInFlight, appRequested,
        appCapacity, 0, dxlab::HeapPolicy::RebuildEachFrame);
    CHECK(appPersistent.count > 0);
    CHECK(appPersistent.first + appPersistent.count <= appCapacity);
    CHECK(!RangesOverlap(appPersistent, appFrames[0]));
    CHECK(!RangesOverlap(appPersistent, appFrames[1]));
    CHECK(!RangesOverlap(appPersistent, appFrames[2]));
}

// (g) HeapManager 가 계획대로 WriteNullSrvs 를 호출하는지.
struct RecordingHeapManager : dxlab::HeapManager {
    struct WriteRec { UINT first; UINT count; };
    std::vector<WriteRec> writes;

    explicit RecordingHeapManager(UINT capacity) { InitHeadless(capacity, 32); }

    void WriteNullSrvs(ID3D12Device*, UINT first, UINT count) override {
        writes.push_back({first, count});
    }
};

void TestHeapManagerFollowsPlan() {
    auto* fakeDevice = reinterpret_cast<ID3D12Device*>(static_cast<uintptr_t>(0x5000));
    const UINT framesInFlight = 3;

    RecordingHeapManager mgr(ninetySix);
    for (UINT f = 0; f < framesInFlight; ++f) {
        const auto plan = dxlab::PlanNullSrvWrites(
            dxlab::HeapPolicy::RebuildEachFrame, f, framesInFlight, 20,
            ninetySix, 0, dxlab::HeapPolicy::RebuildEachFrame);
        const UINT written = mgr.PrepareNullSrvWorkload(
            fakeDevice, dxlab::HeapPolicy::RebuildEachFrame, f, framesInFlight, 20);
        CHECK(written == plan.writes);
        CHECK(mgr.writes.size() == (size_t)f + 1);
        CHECK(mgr.writes.back().first == plan.first);
        CHECK(mgr.writes.back().count == plan.writes);
    }

    // Persistent 는 최초 1회만 쓰고 이후에는 재사용한다.
    RecordingHeapManager persistent(ninetySix);
    const UINT firstWrite = persistent.PrepareNullSrvWorkload(
        fakeDevice, dxlab::HeapPolicy::PersistentTable, 0, framesInFlight, 20);
    CHECK(firstWrite == 20);
    CHECK(persistent.writes.size() == 1);
    CHECK(persistent.writes[0].first ==
          dxlab::NullSrvPersistentFirst(framesInFlight, ninetySix));
    const UINT secondWrite = persistent.PrepareNullSrvWorkload(
        fakeDevice, dxlab::HeapPolicy::PersistentTable, 1, framesInFlight, 20);
    CHECK(secondWrite == 0);
    CHECK(persistent.writes.size() == 1);
}

// ---------------------------------------------------------------------------
// Step 6 — 배리어.
// ---------------------------------------------------------------------------

void TestBarrierDescription() {
    auto* fake = FakeResource();
    const auto barrier = dxlab::MakeTransitionBarrier(
        fake, D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    CHECK(barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION);
    CHECK(barrier.Transition.pResource == fake);
    CHECK(barrier.Transition.StateBefore == D3D12_RESOURCE_STATE_COPY_DEST);
    CHECK(barrier.Transition.StateAfter ==
          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    CHECK(barrier.Transition.Subresource ==
          D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
}

// 항목 12 — Flush 반환값 == 실제 기록된 ResourceBarrier 호출 수.
void TestBarrierFlushCounts() {
    const UINT ks[] = {0, 1, 3};
    for (UINT k : ks) {
        {   // Immediate: K 회.
            dxlab::BarrierBatch batch;
            FakeCmdList cmd;
            for (UINT i = 0; i < k; ++i)
                batch.Transition(FakeResource(), D3D12_RESOURCE_STATE_PRESENT,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET);
            CHECK(batch.PendingCount() == k);
            const size_t issued = batch.FlushImmediate(&cmd);
            CHECK(issued == k);
            CHECK(cmd.barriers.size() == k);
            CHECK(batch.PendingCount() == 0);
            for (const auto& rec : cmd.barriers) CHECK(rec.count == 1);
        }
        {   // Batched: 계류가 있으면 1회, 없으면 0회.
            dxlab::BarrierBatch batch;
            FakeCmdList cmd;
            for (UINT i = 0; i < k; ++i)
                batch.Transition(FakeResource(), D3D12_RESOURCE_STATE_PRESENT,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET);
            const size_t issued = batch.FlushBatched(&cmd);
            CHECK(issued == (k != 0 ? 1u : 0u));
            CHECK(cmd.barriers.size() == (k != 0 ? 1u : 0u));
            if (k != 0) CHECK(cmd.barriers[0].count == k);
            CHECK(batch.PendingCount() == 0);
        }
        {   // Split: begin/end 두 번씩 -> 2K 회.
            dxlab::BarrierBatch batch;
            FakeCmdList cmd;
            for (UINT i = 0; i < k; ++i)
                batch.Transition(FakeResource(), D3D12_RESOURCE_STATE_PRESENT,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET);
            const size_t issued = batch.FlushSplit(&cmd);
            CHECK(issued == (size_t)k * 2);
            CHECK(cmd.barriers.size() == (size_t)k * 2);
            CHECK(batch.PendingCount() == 0);
            for (size_t i = 0; i + 1 < cmd.barriers.size(); i += 2) {
                CHECK(cmd.barriers[i].list[0].Flags ==
                      D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY);
                CHECK(cmd.barriers[i + 1].list[0].Flags ==
                      D3D12_RESOURCE_BARRIER_FLAG_END_ONLY);
            }
        }
    }
}

// 항목 13 (2) — 한 프레임의 백버퍼 상태 전이 시퀀스.
void TestBackBufferBarrierSequence() {
    dxlab::BarrierBatch batch;
    FakeCmdList cmd;
    const size_t entry = dxlab::RecordBackBufferBarriers(
        batch, &cmd, FakeResource(), dxlab::BarrierPolicy::Immediate, true);
    const size_t exit = dxlab::RecordBackBufferBarriers(
        batch, &cmd, FakeResource(), dxlab::BarrierPolicy::Immediate, false);
    CHECK(entry == 1);
    CHECK(exit == 1);
    CHECK(cmd.barriers.size() == 2);

    const auto& first = cmd.barriers[0].list[0].Transition;
    const auto& second = cmd.barriers[1].list[0].Transition;
    CHECK(first.StateBefore == D3D12_RESOURCE_STATE_PRESENT);
    CHECK(first.StateAfter == D3D12_RESOURCE_STATE_RENDER_TARGET);
    CHECK(second.StateBefore == D3D12_RESOURCE_STATE_RENDER_TARGET);
    CHECK(second.StateAfter == D3D12_RESOURCE_STATE_PRESENT);
    // 앞 전이의 도착 상태 == 뒤 전이의 출발 상태 (상태 사슬이 끊기지 않는다).
    CHECK(first.StateAfter == second.StateBefore);
    CHECK(first.pResource == second.pResource);

    // Split 정책이면 같은 시퀀스를 begin/end 로 나눠 4회 발급한다.
    FakeCmdList splitCmd;
    const size_t splitEntry = dxlab::RecordBackBufferBarriers(
        batch, &splitCmd, FakeResource(), dxlab::BarrierPolicy::Split, true);
    const size_t splitExit = dxlab::RecordBackBufferBarriers(
        batch, &splitCmd, FakeResource(), dxlab::BarrierPolicy::Split, false);
    CHECK(splitEntry == 2);
    CHECK(splitExit == 2);
    CHECK(splitCmd.barriers.size() == 4);
}

// ---------------------------------------------------------------------------
// Step 2 — 타임스탬프 slot/fence 상태 기계.
// ---------------------------------------------------------------------------

struct FakeFence : dxlab_test::StubFence {
    UINT64 completed = 0;
    UINT64 lastSignaled = 0;
    UINT64 STDMETHODCALLTYPE GetCompletedValue() override { return completed; }
    HRESULT STDMETHODCALLTYPE Signal(UINT64 value) override {
        lastSignaled = value;
        return S_OK;
    }
};

struct FakeReadback : dxlab_test::StubResource {
    int mapCalls = 0;
    int unmapCalls = 0;
    std::vector<UINT64> storage;
    HRESULT STDMETHODCALLTYPE Map(UINT, const D3D12_RANGE*,
                                  void** ppData) override {
        ++mapCalls;
        if (ppData) *ppData = storage.data();
        return S_OK;
    }
    void STDMETHODCALLTYPE Unmap(UINT, const D3D12_RANGE*) override {
        ++unmapCalls;
    }
};

struct FakeQueue : dxlab_test::StubCommandQueue {
    HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence* fence, UINT64 value) override {
        return fence ? fence->Signal(value) : E_FAIL;
    }
};

void TestSlotReadyPredicate() {
    // 미제출 slot(fence 값 0)은 완료값이 아무리 커도 준비되지 않았다.
    CHECK(!dxlab::IsSlotReady(0, 0));
    CHECK(!dxlab::IsSlotReady(9999, 0));
    // 완료값이 slot fence 값보다 작으면 준비되지 않았다.
    CHECK(!dxlab::IsSlotReady(4, 5));
    // 같거나 크면 준비됐다.
    CHECK(dxlab::IsSlotReady(5, 5));
    CHECK(dxlab::IsSlotReady(6, 5));
}

void TestTimestampPollWaitsForFence() {
    FakeFence fence;
    FakeReadback readback;
    FakeQueue queue;
    dxlab_test::StubQueryHeap heap;
    readback.storage.assign((size_t)dxlab::TimestampQuery::kRingSize * 2,
                            UINT64{0});

    dxlab::TimestampQuery tq;
    tq.InitHeadless(&heap, &readback, &queue, &fence, 1000000);   // 1 MHz.

    FakeCmdList cmd;
    tq.Begin(&cmd);
    CHECK(tq.ActiveSlot() == 0);
    CHECK(cmd.endQueries == 1);
    tq.End(&cmd);
    CHECK(cmd.endQueries == 2);
    CHECK(cmd.resolves == 1);
    tq.SignalSubmitted();
    CHECK(tq.SlotFence(0) == 1);
    CHECK(fence.lastSignaled == 1);

    // GPU 미완료 -> Map 하지 않고, slot 도 소비하지 않는다.
    fence.completed = 0;
    tq.Poll();
    CHECK(readback.mapCalls == 0);
    CHECK(tq.SlotFence(0) == 1);
    CHECK(tq.ReadIndex() == 0);

    // 완료 -> 정확히 1회 Map/Unmap 하고 ms 를 계산한다.
    fence.completed = 1;
    readback.storage[0] = 1000;
    readback.storage[1] = 3000;       // 2000 tick @1MHz = 2ms.
    tq.Poll();
    CHECK(readback.mapCalls == 1);
    CHECK(readback.unmapCalls == 1);
    CHECK(std::fabs(tq.LastMs() - 2.0) < 1e-9);
    CHECK(tq.SlotFence(0) == 0);
    CHECK(tq.ReadIndex() == 1);
}

void TestTimestampBeginWaitsBeforeOverwrite() {
    FakeFence fence;
    FakeReadback readback;
    FakeQueue queue;
    dxlab_test::StubQueryHeap heap;
    readback.storage.assign((size_t)dxlab::TimestampQuery::kRingSize * 2,
                            UINT64{0});

    dxlab::TimestampQuery tq;
    tq.InitHeadless(&heap, &readback, &queue, &fence, 1000000);

    FakeCmdList cmd;
    // 링을 가득 채우되 GPU 완료는 알리지 않는다.
    for (int i = 0; i < dxlab::TimestampQuery::kRingSize; ++i) {
        tq.Begin(&cmd);
        CHECK(tq.ActiveSlot() == i);
        tq.End(&cmd);
        tq.SignalSubmitted();
    }
    CHECK(tq.WriteIndex() == 0);
    for (int i = 0; i < dxlab::TimestampQuery::kRingSize; ++i)
        CHECK(tq.SlotFence(i) != 0);

    // slot 0 은 아직 미완료 -> Begin 은 덮어쓰지 않고 물러난다.
    const int queriesBefore = cmd.endQueries;
    tq.Begin(&cmd);
    CHECK(tq.ActiveSlot() == -1);
    CHECK(cmd.endQueries == queriesBefore);

    // 완료를 알리면 다시 slot 0 을 잡는다.
    fence.completed = (UINT64)dxlab::TimestampQuery::kRingSize;
    tq.Begin(&cmd);
    CHECK(tq.ActiveSlot() == 0);
    CHECK(cmd.endQueries == queriesBefore + 1);
}

// ---------------------------------------------------------------------------
// 항목 13 (3) — 변환 결과.
// ---------------------------------------------------------------------------

XMFLOAT3 ToNdc(const XMMATRIX& vp, float x, float y, float z, float& outW) {
    XMVECTOR clip = XMVector4Transform(XMVectorSet(x, y, z, 1.0f), vp);
    outW = XMVectorGetW(clip);
    XMFLOAT3 ndc{XMVectorGetX(clip) / outW, XMVectorGetY(clip) / outW,
                 XMVectorGetZ(clip) / outW};
    return ndc;
}

void TestFrameCamera() {
    dxlab::CameraSetup cam;
    cam.eye = {0.0f, 0.0f, 5.0f};
    cam.at  = {0.0f, 0.0f, 0.0f};
    cam.up  = {0.0f, 1.0f, 0.0f};
    cam.fovDegrees = 90.0f;
    cam.nearZ = 1.0f;
    cam.farZ  = 100.0f;

    const dxlab::FrameCB cb =
        dxlab::MakeFrameCB(256, 256, cam, dxlab::DefaultLightDir(), 0.25f);

    // cbuffer 규약대로 transpose 되어 저장되므로 되돌려서 검산한다.
    const XMMATRIX vp = XMMatrixTranspose(XMLoadFloat4x4(&cb.viewProj));

    float w = 0.0f;
    XMFLOAT3 origin = ToNdc(vp, 0.0f, 0.0f, 0.0f, w);
    CHECK(w > 0.0f);
    CHECK(NearF(origin.x, 0.0f));
    CHECK(NearF(origin.y, 0.0f));
    CHECK(origin.z > 0.0f && origin.z < 1.0f);

    // near/far 평면 위의 점은 각각 깊이 0 과 1 로 사상된다.
    XMFLOAT3 nearPt = ToNdc(vp, 0.0f, 0.0f, 4.0f, w);      // view z = -1.
    CHECK(NearF(nearPt.z, 0.0f));
    XMFLOAT3 farPt = ToNdc(vp, 0.0f, 0.0f, -95.0f, w);     // view z = -100.
    CHECK(NearF(farPt.z, 1.0f));

    // 오른손 좌표계 + up=+Y -> 월드 +X 는 화면 오른쪽, +Y 는 위쪽.
    XMFLOAT3 right = ToNdc(vp, 1.0f, 0.0f, 0.0f, w);
    CHECK(NearF(right.x, 0.2f));
    CHECK(NearF(right.y, 0.0f));
    XMFLOAT3 up = ToNdc(vp, 0.0f, 1.0f, 0.0f, w);
    CHECK(NearF(up.y, 0.2f));
    CHECK(NearF(up.x, 0.0f));

    // aspect 는 가로 방향에만 적용된다.
    const dxlab::FrameCB wide =
        dxlab::MakeFrameCB(512, 256, cam, dxlab::DefaultLightDir(), 0.25f);
    const XMMATRIX wideVp = XMMatrixTranspose(XMLoadFloat4x4(&wide.viewProj));
    XMFLOAT3 wideRight = ToNdc(wideVp, 1.0f, 0.0f, 0.0f, w);
    XMFLOAT3 wideUp = ToNdc(wideVp, 0.0f, 1.0f, 0.0f, w);
    CHECK(NearF(wideRight.x, 0.1f));
    CHECK(NearF(wideUp.y, 0.2f));

    // 나머지 필드.
    CHECK(NearF(cb.cameraPos.x, 0.0f));
    CHECK(NearF(cb.cameraPos.y, 0.0f));
    CHECK(NearF(cb.cameraPos.z, 5.0f));
    CHECK(NearF(cb.time, 0.25f));
    const float lightLen = std::sqrt(cb.lightDir.x * cb.lightDir.x +
                                      cb.lightDir.y * cb.lightDir.y +
                                      cb.lightDir.z * cb.lightDir.z);
    CHECK(NearF(lightLen, 1.0f));
    CHECK(NearF(cb.lightDir.y / cb.lightDir.x, 2.0f));   // (0.4, 0.8, 0.2).
    CHECK(NearF(cb.lightDir.z / cb.lightDir.x, 0.5f));
}

void TestInstanceModelMatrix() {
    XMFLOAT3 axis{0.0f, 0.0f, 1.0f};
    XMFLOAT3 pos{1.0f, 2.0f, 3.0f};
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, dxlab::ComputeInstanceModel(axis, XM_PIDIV2, pos));

    // Z축 +90도 회전 뒤 (1,2,3) 이동 (행벡터 규약).
    const float expected[4][4] = {
        { 0.0f, 1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f, 0.0f},
        { 0.0f, 0.0f, 1.0f, 0.0f},
        { 1.0f, 2.0f, 3.0f, 1.0f},
    };
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            CHECK(NearF(m.m[r][c], expected[r][c]));

    // 회전이 실제로 적용되는지 — (1,0,0) -> (0,1,0) + 이동.
    XMVECTOR p = XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 1.0f),
                                     dxlab::ComputeInstanceModel(axis, XM_PIDIV2, pos));
    CHECK(NearF(XMVectorGetX(p), 1.0f));
    CHECK(NearF(XMVectorGetY(p), 3.0f));
    CHECK(NearF(XMVectorGetZ(p), 3.0f));
}

// 인스턴스 업로드 경로와 Per-Draw 루트 상수 경로가 같은 model 행렬을 쓴다.
void TestInstanceUploadMatchesRootConstants() {
    const int kCount = 8;   // side=2, spacing 2.5 -> origin -1.25.
    std::vector<dxlab::Instance> storage(kCount);

    dxlab::Scene scene;
    scene.BuildObjects(kCount);
    scene.BindInstanceStorage(storage.data(), storage.size());

    FakeCmdList instancedCmd;
    dxlab::GlCounters instancedCounters{};
    scene.RecordDraw(&instancedCmd, dxlab::DrawMode::Instanced,
                     dxlab::RootSigPolicy::Global,
                     dxlab::PsoSwitchPolicy::PerPass, FakeRootSig(),
                     FakePsoA(), nullptr, instancedCounters);

    // angle 은 0 이므로 model 은 순수 이동이고, 격자 좌표는 해석적으로 안다.
    // 저장은 transpose 되어 있으므로 이동 성분이 마지막 *열* 에 온다.
    const float spacing = 2.5f;
    const float origin = -1.25f;
    for (int i = 0; i < kCount; ++i) {
        const float ex = origin + (i % 2) * spacing;
        const float ey = origin + ((i / 2) % 2) * spacing;
        const float ez = origin + (i / 4) * spacing;
        const auto& m = storage[(size_t)i].model;
        CHECK(NearF(m.m[0][3], ex));
        CHECK(NearF(m.m[1][3], ey));
        CHECK(NearF(m.m[2][3], ez));
        CHECK(NearF(m.m[0][0], 1.0f));
        CHECK(NearF(m.m[1][1], 1.0f));
        CHECK(NearF(m.m[2][2], 1.0f));
        CHECK(NearF(m.m[3][3], 1.0f));
        CHECK(NearF(m.m[0][1], 0.0f));
        CHECK(NearF(m.m[1][0], 0.0f));
        CHECK(NearF(storage[(size_t)i].color.w, 1.0f));
    }

    // 회전을 진행시킨 뒤 두 경로가 여전히 같은 행렬을 만드는지 대조한다.
    scene.Update(0.75f);
    FakeCmdList instanced2;
    dxlab::GlCounters c2{};
    scene.RecordDraw(&instanced2, dxlab::DrawMode::Instanced,
                     dxlab::RootSigPolicy::Global,
                     dxlab::PsoSwitchPolicy::PerPass, FakeRootSig(),
                     FakePsoA(), nullptr, c2);

    FakeCmdList perDrawCmd;
    dxlab::GlCounters c3{};
    scene.RecordDraw(&perDrawCmd, dxlab::DrawMode::PerDraw,
                     dxlab::RootSigPolicy::Global,
                     dxlab::PsoSwitchPolicy::PerPass, FakeRootSig(),
                     FakePsoA(), nullptr, c3);

    CHECK(perDrawCmd.rootConsts.size() == (size_t)kCount * 3);
    bool rotated = false;
    for (int i = 0; i < kCount; ++i) {
        const auto& modelConst = perDrawCmd.rootConsts[(size_t)i * 3 + 1];
        CHECK(modelConst.num == 16);
        CHECK(modelConst.destOffset == 4);
        if (modelConst.data.size() != 16) continue;
        XMFLOAT4X4 fromRootConst;
        std::memcpy(&fromRootConst, modelConst.data.data(), sizeof(XMFLOAT4X4));
        const auto& fromUpload = storage[(size_t)i].model;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                CHECK(NearF(fromRootConst.m[r][c], fromUpload.m[r][c]));
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                if (!NearF(fromUpload.m[r][c], (r == c) ? 1.0f : 0.0f, 1e-3f))
                    rotated = true;
    }
    // Update 가 실제로 각도를 바꿨다 — 회전 성분이 항등이 아니다.
    CHECK(rotated);
}

}  // namespace

int main() {
    TestDrawSubmissions();
    TestSceneRecordsPerDraw();
    TestSceneRecordsGlobalPolicies();
    TestSceneRecordsInstanced();
    TestSceneRecordsPerMaterial();
    TestRecordedSubmissionsMatchHelper();
    TestRootConstantBudget();
    TestDescriptorLifetimePlan();
    TestDescriptorRangesDisjoint();
    TestHeapManagerFollowsPlan();
    TestBarrierDescription();
    TestBarrierFlushCounts();
    TestBackBufferBarrierSequence();
    TestSlotReadyPredicate();
    TestTimestampPollWaitsForFence();
    TestTimestampBeginWaitsBeforeOverwrite();
    TestFrameCamera();
    TestInstanceModelMatrix();
    TestInstanceUploadMatchesRootConstants();
    if (failures != 0) return 1;
    std::cout << "DirectX contract tests passed\n";
    return 0;
}
