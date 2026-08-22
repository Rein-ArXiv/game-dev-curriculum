// 콘텐츠에 정체성이 생긴다 — 발행·입장 검사·롤백.
//
// 지금까지 서버는 "네가 들고 있는 콘텐츠가 무엇인가"를 물을 어휘가 없었다.
// 쿡 파이프라인이 낸 산출 해시를 매니페스트에 발행하면 그 (버전, 해시) 쌍이
// 콘텐츠의 이름이 되고, 입장 승인이 그 이름을 대조한다.
//
// 세 상태를 **구분**하는 것이 이 파일의 요점이다.
//   Unpublished — 강제할 기준이 없다. 아무도 거르지 않는다.
//   Accept      — 기준이 있고 같다.
//   Mismatch    — 기준이 있고 다르다. 이때만 거절이다.
// 셋을 bool 하나로 줄이면 "왜 통과했는가"를 로그에서 답할 수 없다.
#include "live/patch.hpp"
#include "tools/asset_pipeline.hpp"
#include "test_util.h"

#include <cstdint>
#include <string>

using namespace mega::live;
using mega::tools::AssetPipeline;
using mega::tools::CookStatus;

// 쿡 파이프라인에서 콘텐츠 해시를 얻는다 — 발행되는 값의 출처가 실제로 여기다.
static uint64_t CookAndHash(AssetPipeline& pipeline) {
    int rebuilt = 0;
    std::string error;
    CHECK(pipeline.Cook(&rebuilt, nullptr, &error) == CookStatus::Ok);
    CHECK(error.empty());
    return pipeline.ContentVersionHash();
}

static AssetPipeline ShippedAssets() {
    AssetPipeline pipeline;
    CHECK(pipeline.Register("level.txt", 0xA001));
    CHECK(pipeline.Register("waves.txt", 0xA002));
    CHECK(pipeline.Register("balance.script", 0xA003));
    CHECK(pipeline.Register("skeleton.def", 0xA004));
    CHECK(pipeline.Register("boss.pose", 0xA005, {"skeleton.def"}));
    return pipeline;
}

// ── 미발행 상태의 안전 동작 ────────────────────────────────────────
static void TestUnpublishedAcceptsEveryone() {
    PatchManifest manifest;
    CHECK_EQ(manifest.Current(), 0u);
    CHECK_EQ(manifest.CurrentHash(), (uint64_t)0);
    CHECK_EQ(manifest.Latest(), 0u);
    CHECK(!manifest.Published());
    CHECK(!manifest.RolledBack());
    CHECK_EQ(manifest.VersionCount(), (size_t)0);

    // 기준이 없으므로 아무도 거르지 않는다. 매니페스트를 세우는 그 순간 모든
    // 클라이언트가 못 붙는 상태가 되면 안 된다.
    CHECK(manifest.Check(0) == ContentVerdict::Unpublished);
    CHECK(manifest.Check(0xDEADBEEF) == ContentVerdict::Unpublished);
    CHECK(manifest.Accepts(0xDEADBEEF));

    // 되돌릴 것도 없다.
    CHECK(!manifest.Rollback());
    CHECK(!manifest.RollForward());
    CHECK(manifest.Entry(1) == nullptr);

    // 버전 0 은 "미발행"의 이름이라 발행할 수 없다.
    CHECK(!manifest.Publish(0, 0x1234));
    CHECK(!manifest.Published());
}

// ── 해시가 다른 클라이언트는 거절 ──────────────────────────────────
static void TestMismatchRejected() {
    AssetPipeline server = ShippedAssets();
    const uint64_t shipped = CookAndHash(server);

    PatchManifest manifest;
    CHECK(manifest.Publish(100, shipped));
    CHECK_EQ(manifest.Current(), 100u);
    CHECK_EQ(manifest.CurrentHash(), shipped);
    CHECK(manifest.Published());

    // 같은 원본을 가진 클라이언트 — 통과.
    AssetPipeline honest = ShippedAssets();
    CHECK_EQ(CookAndHash(honest), shipped);
    CHECK(manifest.Check(shipped) == ContentVerdict::Accept);

    // 밸런스를 한 줄 고친 클라이언트 — 거절.
    AssetPipeline tampered = ShippedAssets();
    CHECK(tampered.Register("balance.script", 0xA003 ^ 0x1));
    const uint64_t altered = CookAndHash(tampered);
    CHECK(altered != shipped);
    CHECK(manifest.Check(altered) == ContentVerdict::Mismatch);
    CHECK(!manifest.Accepts(altered));
    std::printf("  shipped=0x%016llx  tampered=0x%016llx  verdict=%s\n",
                (unsigned long long)shipped, (unsigned long long)altered,
                ContentVerdictName(manifest.Check(altered)));

    // 본 정의만 고쳐도 그것을 참조하는 포즈까지 해시가 바뀐다 — 정체성이
    // 의존 그래프 전체를 덮는다는 뜻이다.
    AssetPipeline deep = ShippedAssets();
    CHECK(deep.Register("skeleton.def", 0xA004 + 1, {}));
    CHECK(manifest.Check(CookAndHash(deep)) == ContentVerdict::Mismatch);
}

// ── 단조 버전과 롤백 ───────────────────────────────────────────────
static void TestRollbackAndRollForward() {
    PatchManifest manifest;
    CHECK(manifest.Publish(100, 0xAAAA));
    CHECK(manifest.Publish(101, 0xBBBB));
    CHECK(manifest.Publish(102, 0xCCCC));
    CHECK_EQ(manifest.Current(), 102u);
    CHECK_EQ(manifest.Latest(), 102u);
    CHECK(!manifest.RolledBack());

    // 역행 발행은 거부된다 — 이 검사가 없으면 과거 버전을 다시 발행하는 것으로
    // 현재가 뒤로 갈 수 있고, Rollback 이 "직전"을 가리키지 못한다.
    CHECK(!manifest.Publish(101, 0xDDDD));
    CHECK(!manifest.Publish(102, 0xDDDD));
    CHECK_EQ(manifest.VersionCount(), (size_t)3);

    // 잘못 배포된 밸런스를 되돌린다.
    CHECK(manifest.Rollback());
    CHECK_EQ(manifest.Current(), 101u);
    CHECK_EQ(manifest.CurrentHash(), (uint64_t)0xBBBB);
    CHECK_EQ(manifest.Latest(), 102u);        // 발행 이력은 그대로다
    CHECK(manifest.RolledBack());

    // 롤백 중에는 **현재 버전의 해시**가 입장 기준이다. 이미 붙어 있던 세션이
    // 들고 있는 102 의 해시는 다음 매치부터 거절된다.
    CHECK(manifest.Check(0xBBBB) == ContentVerdict::Accept);
    CHECK(manifest.Check(0xCCCC) == ContentVerdict::Mismatch);

    CHECK(manifest.Rollback());
    CHECK_EQ(manifest.Current(), 100u);
    CHECK(!manifest.Rollback());              // 더 못 되돌린다

    CHECK(manifest.RollForward());
    CHECK_EQ(manifest.Current(), 101u);
    CHECK(manifest.RollForward());
    CHECK_EQ(manifest.Current(), 102u);
    CHECK(!manifest.RollForward());
    CHECK(!manifest.RolledBack());

    // 롤백 중에 새 패치를 발행하면 기준은 **마지막 발행분**이다(현재가 아니라).
    PatchManifest hotfix;
    CHECK(hotfix.Publish(10, 1));
    CHECK(hotfix.Publish(20, 2));
    CHECK(hotfix.Rollback());
    CHECK_EQ(hotfix.Current(), 10u);
    CHECK(!hotfix.Publish(15, 3));            // 20 보다 작다 → 거부
    CHECK(hotfix.Publish(21, 3));
    CHECK_EQ(hotfix.Current(), 21u);
    CHECK_EQ(hotfix.VersionCount(), (size_t)3);

    // 이력 조회.
    const PatchEntry* entry = hotfix.Entry(20);
    CHECK(entry != nullptr);
    if (entry) {
        CHECK_EQ(entry->version, 20u);
        CHECK_EQ(entry->prevVersion, 10u);
        CHECK_EQ(entry->contentHash, (uint64_t)2);
    }
    CHECK(hotfix.Entry(999) == nullptr);
}

// ── 발행되는 값이 실제로 쿡 결과인가 ───────────────────────────────
static void TestPublishTracksCookOutput() {
    AssetPipeline pipeline = ShippedAssets();
    PatchManifest manifest;
    uint32_t version = 1;
    uint64_t previous = 0;

    for (int change = 0; change < 4; ++change) {
        CHECK(pipeline.Register("waves.txt", 0xA002 + change));
        const uint64_t hash = CookAndHash(pipeline);
        CHECK(hash != previous);
        CHECK(manifest.Publish(version, hash));
        CHECK_EQ(manifest.CurrentHash(), hash);
        CHECK(manifest.Check(hash) == ContentVerdict::Accept);
        if (previous != 0) CHECK(manifest.Check(previous) == ContentVerdict::Mismatch);
        previous = hash;
        ++version;
    }
    CHECK_EQ(manifest.VersionCount(), (size_t)4);

    // 아무것도 안 바꾸면 쿡도 0건이고 해시도 그대로다 — 같은 원본이 같은
    // 버전 해시를 낸다는 것이 발행의 전제다.
    int rebuilt = -1;
    CHECK(pipeline.Cook(&rebuilt) == CookStatus::Ok);
    CHECK_EQ(rebuilt, 0);
    CHECK_EQ(pipeline.ContentVersionHash(), previous);
}

static void RunTests() {
    TestUnpublishedAcceptsEveryone();
    TestMismatchRejected();
    TestRollbackAndRollForward();
    TestPublishTracksCookOutput();
}

TEST_MAIN("content_version")
