// 증분 쿡: 바뀐 것과 그 의존자만, 순서는 언제나 같게, 순환은 값으로 거부.
//
// 변이 표적은 Evaluate 의 의존 결합이다. 그 한 줄을 빼면 자기 콘텐츠가 바뀐
// 에셋만 다시 구워지고 그것을 참조하는 에셋은 옛 산출물을 그대로 들고 있는다 —
// 빌드는 성공하고, 포즈만 바뀐 보스가 옛 본 정의로 그려진다.
#include "tools/asset_pipeline.hpp"
#include "test_util.h"

#include <string>
#include <vector>

using namespace mega::tools;

// 이 게임의 실제 에셋 그래프를 그대로 세운다.
//   level.txt / waves.txt / balance.script — 서로 독립
//   boss.pose  → skeleton.def              (포즈는 본 정의를 참조한다)
//   elite.bt   → actions.def               (행동 트리는 행동 이름 목록을 참조한다)
//   blit.frag  → common.glsl               (셰이더는 공용 인클루드를 참조한다)
//   pack.index → 위 전부                    (배포 목록은 모든 산출물을 참조한다)
static AssetPipeline BuildGraph() {
    AssetPipeline pipeline;
    CHECK(pipeline.Register("skeleton.def", 0x1001));
    CHECK(pipeline.Register("actions.def", 0x1002));
    CHECK(pipeline.Register("common.glsl", 0x1003));
    CHECK(pipeline.Register("level.txt", 0x2001));
    CHECK(pipeline.Register("waves.txt", 0x2002));
    CHECK(pipeline.Register("balance.script", 0x2003));
    CHECK(pipeline.Register("boss.pose", 0x3001, {"skeleton.def"}));
    CHECK(pipeline.Register("elite.bt", 0x3002, {"actions.def"}));
    CHECK(pipeline.Register("blit.frag", 0x3003, {"common.glsl"}));
    CHECK(pipeline.Register("pack.index", 0x4001,
                            {"level.txt", "waves.txt", "balance.script",
                             "boss.pose", "elite.bt", "blit.frag"}));
    return pipeline;
}

static bool Contains(const std::vector<std::string>& names, const char* wanted) {
    for (const std::string& name : names) if (name == wanted) return true;
    return false;
}

// ── 첫 쿡과 두 번째 쿡 ─────────────────────────────────────────────
static void TestFirstCookThenNothing() {
    AssetPipeline pipeline = BuildGraph();
    CHECK_EQ(pipeline.Count(), (size_t)10);

    int rebuilt = -1;
    std::vector<std::string> names;
    std::string error;
    CHECK(pipeline.Cook(&rebuilt, &names, &error) == CookStatus::Ok);
    CHECK(error.empty());
    CHECK_EQ(rebuilt, 10);
    CHECK_EQ(names.size(), (size_t)10);
    // 목록은 이름 오름차순이다 — 등록 순서가 아니다.
    for (size_t i = 1; i < names.size(); ++i) CHECK(names[i - 1] < names[i]);

    // 아무것도 안 바꾸면 0.
    CHECK(pipeline.Cook(&rebuilt, &names, &error) == CookStatus::Ok);
    CHECK_EQ(rebuilt, 0);
    CHECK(names.empty());
}

// ── 바뀐 것과 그 의존자만 ──────────────────────────────────────────
static void TestOnlyDirtyAndDependents() {
    AssetPipeline pipeline = BuildGraph();
    int rebuilt = 0;
    std::vector<std::string> names;
    CHECK(pipeline.Cook(&rebuilt, &names) == CookStatus::Ok);

    // 잎을 하나 건드린다: skeleton.def → boss.pose → pack.index 세 개만.
    CHECK(pipeline.UpdateSource("skeleton.def", 0x1001DEAD));
    CHECK(pipeline.Cook(&rebuilt, &names) == CookStatus::Ok);
    std::printf("  skeleton.def 변경 → 재빌드 %d개:", rebuilt);
    for (const std::string& name : names) std::printf(" %s", name.c_str());
    std::printf("\n");
    CHECK_EQ(rebuilt, 3);
    CHECK(Contains(names, "skeleton.def"));
    CHECK(Contains(names, "boss.pose"));
    CHECK(Contains(names, "pack.index"));
    CHECK(!Contains(names, "elite.bt"));
    CHECK(!Contains(names, "level.txt"));

    // 아무 의존자도 없는 잎: 자기 자신과 pack.index 둘.
    CHECK(pipeline.UpdateSource("waves.txt", 0x2002BEEF));
    CHECK(pipeline.Cook(&rebuilt, &names) == CookStatus::Ok);
    CHECK_EQ(rebuilt, 2);
    CHECK(Contains(names, "waves.txt"));
    CHECK(Contains(names, "pack.index"));

    // 뿌리만 건드리면 뿌리 하나.
    CHECK(pipeline.UpdateSource("pack.index", 0x4001AAAA));
    CHECK(pipeline.Cook(&rebuilt, &names) == CookStatus::Ok);
    CHECK_EQ(rebuilt, 1);
    CHECK(Contains(names, "pack.index"));

    // 등록되지 않은 이름의 갱신은 거부.
    CHECK(!pipeline.UpdateSource("nope.txt", 1));
}

// ── 콘텐츠 버전 해시 ───────────────────────────────────────────────
static void TestContentVersionHash() {
    AssetPipeline a = BuildGraph();
    int rebuilt = 0;
    CHECK(a.Cook(&rebuilt) == CookStatus::Ok);
    const uint64_t first = a.ContentVersionHash();

    // 같은 그래프를 **다른 등록 순서**로 세워도 같은 해시.
    AssetPipeline b;
    CHECK(b.Register("pack.index", 0x4001,
                     {"blit.frag", "elite.bt", "boss.pose",
                      "balance.script", "waves.txt", "level.txt"}));
    CHECK(b.Register("blit.frag", 0x3003, {"common.glsl"}));
    CHECK(b.Register("elite.bt", 0x3002, {"actions.def"}));
    CHECK(b.Register("boss.pose", 0x3001, {"skeleton.def"}));
    CHECK(b.Register("balance.script", 0x2003));
    CHECK(b.Register("waves.txt", 0x2002));
    CHECK(b.Register("level.txt", 0x2001));
    CHECK(b.Register("common.glsl", 0x1003));
    CHECK(b.Register("actions.def", 0x1002));
    CHECK(b.Register("skeleton.def", 0x1001));
    CHECK(b.Cook(&rebuilt) == CookStatus::Ok);
    CHECK_EQ(b.ContentVersionHash(), first);
    std::printf("  content version hash = 0x%016llx (등록 순서 무관)\n",
                (unsigned long long)first);

    // 원본 한 바이트가 바뀌면 버전 해시가 바뀐다 — 그것이 정체성의 전부다.
    CHECK(a.UpdateSource("balance.script", 0x2003 + 1));
    CHECK(a.Cook(&rebuilt) == CookStatus::Ok);
    CHECK(a.ContentVersionHash() != first);

    // 되돌리면 해시도 돌아온다(경로 의존이 없다).
    CHECK(a.UpdateSource("balance.script", 0x2003));
    CHECK(a.Cook(&rebuilt) == CookStatus::Ok);
    CHECK_EQ(a.ContentVersionHash(), first);
}

// ── 순환과 없는 의존 ───────────────────────────────────────────────
static void TestCycleAndMissingRejected() {
    // 자기 참조는 등록에서 걸린다.
    AssetPipeline selfRef;
    CHECK(!selfRef.Register("a", 1, {"a"}));

    // 2주기 순환.
    AssetPipeline cycle2;
    CHECK(cycle2.Register("a", 1, {"b"}));
    CHECK(cycle2.Register("b", 2, {"a"}));
    std::string error;
    CHECK(cycle2.Cook(nullptr, nullptr, &error) == CookStatus::Cycle);
    CHECK(!error.empty());
    std::printf("  순환 거부: %s\n", error.c_str());
    CHECK(!cycle2.IsCooked("a"));            // 순환이면 아무것도 굽지 않는다

    // 3주기 순환.
    AssetPipeline cycle3;
    CHECK(cycle3.Register("a", 1, {"b"}));
    CHECK(cycle3.Register("b", 2, {"c"}));
    CHECK(cycle3.Register("c", 3, {"a"}));
    CHECK(cycle3.Cook(nullptr, nullptr, &error) == CookStatus::Cycle);

    // 없는 의존.
    AssetPipeline missing;
    CHECK(missing.Register("a", 1, {"ghost"}));
    CHECK(missing.Cook(nullptr, nullptr, &error) == CookStatus::MissingDependency);
    CHECK(!error.empty());
    // 의존을 실제로 등록하면 통과한다.
    CHECK(missing.Register("ghost", 2));
    int rebuilt = 0;
    CHECK(missing.Cook(&rebuilt, nullptr, &error) == CookStatus::Ok);
    CHECK_EQ(rebuilt, 2);

    // 마름모(같은 노드를 두 경로로 도달)는 순환이 아니다.
    AssetPipeline diamond;
    CHECK(diamond.Register("leaf", 1));
    CHECK(diamond.Register("left", 2, {"leaf"}));
    CHECK(diamond.Register("right", 3, {"leaf"}));
    CHECK(diamond.Register("top", 4, {"left", "right"}));
    CHECK(diamond.Cook(&rebuilt) == CookStatus::Ok);
    CHECK_EQ(rebuilt, 4);
    CHECK(diamond.UpdateSource("leaf", 99));
    CHECK(diamond.Cook(&rebuilt) == CookStatus::Ok);
    CHECK_EQ(rebuilt, 4);                    // 잎이 바뀌면 넷 전부
}

// ── 이름과 상한 ────────────────────────────────────────────────────
static void TestNameAndLimits() {
    AssetPipeline pipeline;
    CHECK(!pipeline.Register("", 1));
    CHECK(!pipeline.Register("has space.txt", 1));
    CHECK(!pipeline.Register(std::string(AssetPipeline::kMaxNameLength + 1, 'a'), 1));
    CHECK(pipeline.Register("dir/sub-file_1.txt", 1));

    std::vector<std::string> deps;
    for (size_t i = 0; i <= AssetPipeline::kMaxDependencies; ++i)
        deps.push_back("d" + std::to_string(i));
    CHECK(!pipeline.Register("many", 1, deps));       // 의존 상한 초과

    // 같은 이름을 다시 등록하면 갱신이지 중복이 아니다.
    const size_t before = pipeline.Count();
    CHECK(pipeline.Register("dir/sub-file_1.txt", 2));
    CHECK_EQ(pipeline.Count(), before);
}

static void RunTests() {
    TestFirstCookThenNothing();
    TestOnlyDirtyAndDependents();
    TestContentVersionHash();
    TestCycleAndMissingRejected();
    TestNameAndLimits();
}

TEST_MAIN("asset_cook")
