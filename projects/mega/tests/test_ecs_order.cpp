// archetype 순회 순서가 **도달 경로에 의존하지 않는가**.
//
// archetype 저장의 배열은 "어느 조합이 먼저 나타났는가" 순으로 늘어난다. 같은
// 엔티티 집합이라도 스폰 순서가 다르면 그 배열 순서가 달라지고, 그 순서로 무엇을
// 누적하면 결과가 실행마다 갈린다. 시뮬 갱신 경로는 그런 자료구조를 쓸 수 없다.
//
// 그래서 World 는 순회를 두 벌 갖는다.
//
//   ForEachChunk         생성 순서. 순서를 묻지 않는 자리(집계·디버그)용.
//   ForEachChunkOrdered  조합 마스크 오름차순. 시뮬이 쓰는 유일한 쪽.
//
// 이 파일의 본 단언은 하나다: **같은 최종 집합에 서로 다른 경로로 도달한 두 월드가
// 같은 순서를 낸다.** 그리고 그 단언이 공짜가 아니라는 것 — 정렬을 벗긴 순회는
// 실제로 다른 순서를 낸다는 것 — 을 같은 파일에서 함께 보인다.

#include "ecs/world.h"
#include "test_util.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace mega;

namespace {

// 컴포넌트 넷. Core 는 모두가 갖고, 나머지 셋이 조합을 만든다 —
// 제품에서 보스만 리그를 갖는 것과 같은 모양이다.
struct Core  { float value = 0.0f; };
struct Brain { int   tier = 0; };
struct Cloak { int   tier = 0; };
struct Rig   { int   tier = 0; };

// 어느 조합을 만들 것인가. 비트 0=Brain, 1=Cloak, 2=Rig.
struct Recipe { int extras; float value; };

// 조합 하나에 엔티티 하나씩 넣는다. archetype 마다 행이 하나뿐이라, 방문 순서가
// 곧 값의 순서다 — archetype 안의 자리 순서가 결과에 섞이지 않는다.
void Build(World& world, const std::vector<Recipe>& recipes) {
    for (const Recipe& recipe : recipes) {
        const EntityId entity = world.Create();
        world.Add(entity, Core{recipe.value});
        if (recipe.extras & 1) world.Add(entity, Brain{1});
        if (recipe.extras & 2) world.Add(entity, Cloak{1});
        if (recipe.extras & 4) world.Add(entity, Rig{1});
    }
}

std::vector<float> OrderedValues(World& world) {
    std::vector<float> out;
    world.ForEachChunkOrdered<Core>([&](size_t count, Core* core) {
        for (size_t i = 0; i < count; ++i) out.push_back(core[i].value);
    });
    return out;
}

std::vector<float> RawValues(World& world) {
    std::vector<float> out;
    world.ForEachChunk<Core>([&](size_t count, Core* core) {
        for (size_t i = 0; i < count; ++i) out.push_back(core[i].value);
    });
    return out;
}

// 방문 순서에 민감한 누적. 부동소수 곱셈-덧셈은 결합법칙을 지키지 않으므로,
// 순서가 다르면 비트가 다르다 — 시뮬이 순서에 의존한다는 사실의 축소판이다.
uint32_t FoldBits(const std::vector<float>& values) {
    float accumulator = 1.0f;
    for (float v : values) accumulator = accumulator * 0.5f + v;
    uint32_t bits = 0;
    std::memcpy(&bits, &accumulator, sizeof(bits));
    return bits;
}

const std::vector<Recipe> kForward{
    {0, 1.5f}, {1, 2.25f}, {2, 3.125f}, {4, 4.0625f},
    {3, 5.5f}, {5, 6.75f}, {6, 7.875f}, {7, 8.125f},
};

std::vector<Recipe> Reversed() {
    return std::vector<Recipe>(kForward.rbegin(), kForward.rend());
}

// ── 1. 도달 경로가 달라도 순서가 같다 ──────────────────────────────
void TestOrderIsIndependentOfHistory() {
    World forward, backward;
    Build(forward, kForward);
    Build(backward, Reversed());

    const std::vector<float> a = OrderedValues(forward);
    const std::vector<float> b = OrderedValues(backward);
    CHECK_EQ(a.size(), kForward.size());
    CHECK_EQ(b.size(), kForward.size());
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) CHECK(a[i] == b[i]);
    CHECK_EQ(FoldBits(a), FoldBits(b));

    // 그리고 이 일치가 공짜가 아니라는 것. 정렬을 지나지 않는 순회는 두 월드에서
    // 실제로 다른 순서를 낸다 — 정렬 래퍼를 벗기면 위 단언이 이 상태가 된다.
    const std::vector<float> rawForward = RawValues(forward);
    const std::vector<float> rawBackward = RawValues(backward);
    CHECK(FoldBits(rawForward) != FoldBits(rawBackward));
    std::printf("  ordered fold=%08x/%08x  raw fold=%08x/%08x\n",
                FoldBits(a), FoldBits(b), FoldBits(rawForward), FoldBits(rawBackward));
}

// ── 2. archetype 이동을 겪어도 같은 순서 ────────────────────────────
// 컴포넌트를 붙였다 떼면 행이 archetype 사이를 옮겨 다닌다. 최종 조합만 같으면
// 그 여정이 어떻든 순회 순서는 같아야 한다.
void TestArchetypeMovesKeepOrder() {
    World direct;
    Build(direct, kForward);

    World churned;
    // 전부 Core 만 가진 상태로 만든 뒤, 필요한 컴포넌트를 뒤에서부터 붙인다.
    std::vector<EntityId> entities;
    for (const Recipe& recipe : kForward) {
        const EntityId entity = churned.Create();
        churned.Add(entity, Core{recipe.value});
        // 한 번 붙였다 떼는 여정을 강제로 넣어 archetype 이동을 유발한다.
        churned.Add(entity, Rig{9});
        churned.Add(entity, Cloak{9});
        churned.Remove<Rig>(entity);
        churned.Remove<Cloak>(entity);
        entities.push_back(entity);
    }
    for (size_t i = entities.size(); i-- > 0;) {
        const int extras = kForward[i].extras;
        if (extras & 1) churned.Add(entities[i], Brain{1});
        if (extras & 2) churned.Add(entities[i], Cloak{1});
        if (extras & 4) churned.Add(entities[i], Rig{1});
    }

    const std::vector<float> a = OrderedValues(direct);
    const std::vector<float> b = OrderedValues(churned);
    CHECK_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) CHECK(a[i] == b[i]);
    CHECK_EQ(FoldBits(a), FoldBits(b));
}

// ── 3. 비어 버린 조합은 방문하지 않는다 ────────────────────────────
// 이동으로 텅 빈 archetype 은 배열에 그대로 남는다. 그것을 방문하면 청크 하나에
// 원소 0개가 실려 오고, 그 순간 "청크는 비어 있지 않다"는 전제로 쓴 루프가
// 나눗셈이나 첫 원소 접근에서 깨진다.
void TestOrderedSkipsEmptyArchetypes() {
    World world;
    const EntityId lone = world.Create();
    world.Add(lone, Core{1.0f});
    world.Add(lone, Rig{1});          // (Core) archetype 이 비게 된다
    size_t chunks = 0;
    size_t rows = 0;
    world.ForEachChunkOrdered<Core>([&](size_t count, Core*) {
        ++chunks;
        rows += count;
        CHECK(count > 0);
    });
    CHECK_EQ(chunks, static_cast<size_t>(1));
    CHECK_EQ(rows, static_cast<size_t>(1));

    world.Destroy(lone);
    chunks = 0;
    world.ForEachChunkOrdered<Core>([&](size_t, Core*) { ++chunks; });
    CHECK_EQ(chunks, static_cast<size_t>(0));
}

// ── 4. 질의 마스크가 조합을 실제로 거른다 ──────────────────────────
void TestQueryMaskFilters() {
    World world;
    Build(world, kForward);
    size_t rigRows = 0;
    world.ForEachChunkOrdered<Core, Rig>([&](size_t count, Core*, Rig*) { rigRows += count; });
    // extras 비트 2(=4)가 켜진 조합은 넷이다: 4, 5, 6, 7.
    CHECK_EQ(rigRows, static_cast<size_t>(4));

    size_t allThree = 0;
    world.ForEachChunkOrdered<Core, Brain, Cloak, Rig>(
        [&](size_t count, Core*, Brain*, Cloak*, Rig*) { allThree += count; });
    CHECK_EQ(allThree, static_cast<size_t>(1));
}

// ── 5. 두 축의 비용(단언하지 않고 출력만) ──────────────────────────
// archetype 저장의 값은 조합이 갈릴 때 나오고, 비용은 조합이 갈릴 때 든다.
// 같은 개체 수를 몇 개의 조합으로 쪼개는가를 바꿔 가며 그 둘을 함께 잰다.
// 벽시계 수치는 게이트로 쓰지 않는다 — 판정은 위 네 검사가 한다.
void ReportChunkCostVersusDiversity() {
    const size_t kEntities = 8192;
    std::printf("  [조합 다양성 축] 엔티티 %zu 고정\n", kEntities);
    for (int combos : {1, 2, 4, 8}) {
        World world;
        std::vector<EntityId> entities;
        entities.reserve(kEntities);
        const auto buildBegin = std::chrono::steady_clock::now();
        for (size_t i = 0; i < kEntities; ++i) {
            const EntityId entity = world.Create();
            world.Add(entity, Core{static_cast<float>(i)});
            const int extras = static_cast<int>(i) % combos;
            if (extras & 1) world.Add(entity, Brain{1});
            if (extras & 2) world.Add(entity, Cloak{1});
            if (extras & 4) world.Add(entity, Rig{1});
            entities.push_back(entity);
        }
        const double buildMs = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - buildBegin).count();

        double sink = 0.0;
        const auto sweepBegin = std::chrono::steady_clock::now();
        for (int repeat = 0; repeat < 400; ++repeat) {
            world.ForEachChunkOrdered<Core>([&](size_t count, Core* core) {
                for (size_t i = 0; i < count; ++i) sink += core[i].value;
            });
        }
        const double sweepMs = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - sweepBegin).count();

        // 컴포넌트 하나를 붙였다 떼며 archetype 이동을 강제한다. 이 비용이
        // archetype 저장이 치르는 값이고, 고정 열 SoA 에는 없는 항목이다.
        const auto moveBegin = std::chrono::steady_clock::now();
        for (EntityId entity : entities) world.Add(entity, Rig{7});
        for (EntityId entity : entities) world.Remove<Rig>(entity);
        const double moveMs = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - moveBegin).count();

        std::printf("    조합 %2d개(archetype %2zu) 구축=%.2fms 순회x400=%.2fms "
                    "이동(추가+제거 %zu회)=%.2fms  sink=%.0f\n",
                    combos, world.ArchetypeCount(), buildMs, sweepMs,
                    kEntities * 2, moveMs, sink);
    }
}

}  // namespace

static void RunTests() {
    TestOrderIsIndependentOfHistory();
    TestArchetypeMovesKeepOrder();
    TestOrderedSkipsEmptyArchetypes();
    TestQueryMaskFilters();
    ReportChunkCostVersusDiversity();
}

TEST_MAIN("ecs_order")
