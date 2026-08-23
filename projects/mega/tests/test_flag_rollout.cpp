// 피처 플래그의 네 가지 성질을 잠근다.
//
//   1. 안정 — 같은 (플래그, 플레이어)는 언제나 같은 팔.
//   2. 독립 — 서로 다른 플래그는 서로 다른 사용자 집합을 켠다.
//   3. 수렴 — 롤아웃 퍼센트가 대규모 표본에서 목표 비율에 수렴한다.
//   4. 폴백 — 알 수 없는 팔·정의 없는 이름은 기본 팔로 떨어진다.
//
// 2번이 이 파일의 존재 이유다. 버킷 해시에서 플래그 이름 결합을 빼면 1·3·4는
// 그대로 통과하고 2만 죽는다 — 그 상태에서는 두 실험이 정확히 같은 사용자
// 집합을 켜므로, 한 실험의 효과가 다른 실험의 결과로 보고된다.
//
// 이 파일이 **묻지 않는 것**: 확정된 배정이 게임에 실제로 얹히는가. 여기 있는
// 것은 `live::FeatureFlags` 하나이고, 그 결과를 `Switches` 표에 옮기는
// `Switches::ApplyFlags` 는 한 줄도 지나지 않는다. 그 배선을 무력화해도 이
// 파일은 전부 통과한다 — 그래서 `tests/test_live_ops_wiring.cpp` 가 제품
// `DefenseApp` 을 세워 같은 질문을 한 계단 위에서 다시 묻는다.
#include "live/feature_flag.hpp"
#include "test_util.h"

#include <string>
#include <vector>

using namespace mega::live;

static FeatureFlags BuildFlags() {
    FeatureFlags flags;
    // 팔 목록의 첫 번째가 기본 팔이고, 그것은 스위치가 생기기 전의 동작이다.
    CHECK(flags.Define("entity_store", {"aos", "soa", "ecs"}, 60));
    CHECK(flags.Define("pathing", {"poly", "astar", "flowfield"}, 100));
    CHECK(flags.Define("steering", {"0", "1"}, 30));
    return flags;
}

// ── 1. 안정 ────────────────────────────────────────────────────────
static void TestStickyBucket() {
    const FeatureFlags flags = BuildFlags();
    for (uint32_t player = 1; player <= 64; ++player) {
        const std::string first = flags.ArmFor("entity_store", player);
        CHECK(!first.empty());
        for (int repeat = 0; repeat < 16; ++repeat)
            CHECK_EQ(flags.ArmFor("entity_store", player), first);
    }
    // 같은 정의를 다른 순서로 세워도 같은 답이 나온다 — 버킷은 삽입 순서를
    // 보지 않는다.
    FeatureFlags reordered;
    CHECK(reordered.Define("steering", {"0", "1"}, 30));
    CHECK(reordered.Define("pathing", {"poly", "astar", "flowfield"}, 100));
    CHECK(reordered.Define("entity_store", {"aos", "soa", "ecs"}, 60));
    for (uint32_t player = 1; player <= 200; ++player)
        CHECK_EQ(reordered.ArmFor("entity_store", player),
                 flags.ArmFor("entity_store", player));
}

// ── 2. 독립 ────────────────────────────────────────────────────────
static void TestFlagsAreIndependent() {
    FeatureFlags flags;
    // 두 플래그의 팔 개수도 롤아웃도 같다. 다른 것은 이름뿐이다.
    CHECK(flags.Define("alpha", {"off", "on"}, 50));
    CHECK(flags.Define("beta", {"off", "on"}, 50));

    int alphaOn = 0, betaOn = 0, agree = 0;
    const int sample = 4000;
    for (uint32_t player = 0; player < (uint32_t)sample; ++player) {
        const bool a = flags.ArmFor("alpha", player) == "on";
        const bool b = flags.ArmFor("beta", player) == "on";
        alphaOn += a ? 1 : 0;
        betaOn += b ? 1 : 0;
        agree += (a == b) ? 1 : 0;
    }
    // 둘 다 대략 절반씩 켜지되,
    CHECK(alphaOn > sample * 45 / 100 && alphaOn < sample * 55 / 100);
    CHECK(betaOn > sample * 45 / 100 && betaOn < sample * 55 / 100);
    // **같은 사용자 집합은 아니다**. 이름이 섞이지 않으면 이 값이 정확히 sample 이 된다.
    std::printf("  alpha_on=%d beta_on=%d 같은 답=%d/%d (%.1f%%)\n", alphaOn, betaOn,
                agree, sample, 100.0 * agree / sample);
    CHECK(agree > sample * 40 / 100 && agree < sample * 60 / 100);

    // 롤아웃에 드는 사용자를 고르는 버킷과 팔을 고르는 버킷도 독립이어야 한다.
    // 같은 salt 를 쓰면 롤아웃 하위 구간에 팔이 몰린다.
    CHECK(FlagBucket("alpha", 12345u, 0u) != FlagBucket("alpha", 12345u, 0x9E3779B9u));
    CHECK(FlagBucket("alpha", 12345u, 0u) != FlagBucket("beta", 12345u, 0u));
}

// ── 3. 수렴 ────────────────────────────────────────────────────────
static void TestRolloutConverges() {
    struct Case { int percent; };
    const Case cases[] = {{0}, {10}, {30}, {75}, {100}};
    const int sample = 20000;
    for (const Case& c : cases) {
        FeatureFlags flags;
        CHECK(flags.Define("gradual", {"legacy", "next"}, c.percent));
        int next = 0;
        for (uint32_t player = 0; player < (uint32_t)sample; ++player)
            if (flags.ArmFor("gradual", player) == "next") ++next;
        const double ratio = 100.0 * next / sample;
        std::printf("  rollout=%3d%% -> %6.2f%% (%d/%d)\n", c.percent, ratio, next, sample);
        // 0 과 100 은 경계이므로 정확히 맞아야 한다.
        if (c.percent == 0) CHECK_EQ(next, 0);
        else if (c.percent == 100) CHECK_EQ(next, sample);
        else CHECK(ratio > c.percent - 2.0 && ratio < c.percent + 2.0);
    }

    // 팔이 셋이면 롤아웃에 든 사용자가 두 대안에 고르게 나뉜다.
    FeatureFlags three;
    CHECK(three.Define("split", {"base", "left", "right"}, 100));
    int left = 0, right = 0, base = 0;
    for (uint32_t player = 0; player < (uint32_t)sample; ++player) {
        const std::string arm = three.ArmFor("split", player);
        if (arm == "left") ++left;
        else if (arm == "right") ++right;
        else ++base;
    }
    CHECK_EQ(base, 0);
    std::printf("  3-arm split: left=%d right=%d\n", left, right);
    CHECK(left > sample * 45 / 100 && left < sample * 55 / 100);
    CHECK(right > sample * 45 / 100 && right < sample * 55 / 100);
}

// ── 4. 폴백 ────────────────────────────────────────────────────────
static void TestUnknownFallsBackToDefaultArm() {
    FeatureFlags flags = BuildFlags();

    // 정의되지 않은 이름은 빈 문자열 — 호출자가 자기 기본을 유지한다.
    CHECK(flags.ArmFor("no_such_flag", 7).empty());
    CHECK(!flags.Defined("no_such_flag"));

    // 운영자가 오타 난 팔을 강제하면 Force 가 false 를 돌려주고(진단),
    // 판정은 기본 팔로 떨어진다(안전).
    std::string reason;
    CHECK(!flags.Force("pathing", "flowfeild", &reason));
    CHECK(!reason.empty());
    for (uint32_t player = 0; player < 500; ++player)
        CHECK_EQ(flags.ArmFor("pathing", player), std::string("poly"));

    // 목록에 있는 팔을 강제하면 롤아웃과 무관하게 전원이 그 팔이다.
    CHECK(flags.Force("pathing", "flowfield", &reason));
    for (uint32_t player = 0; player < 500; ++player)
        CHECK_EQ(flags.ArmFor("pathing", player), std::string("flowfield"));
    flags.ClearForce("pathing");

    // 팔이 하나뿐이면 롤아웃과 무관하게 언제나 그 팔이다.
    CHECK(flags.Define("solo", {"only"}, 100));
    for (uint32_t player = 0; player < 100; ++player)
        CHECK_EQ(flags.ArmFor("solo", player), std::string("only"));

    // 문법 위반 정의는 거부된다.
    CHECK(!flags.Define("Bad Name", {"a"}));
    CHECK(!flags.Define("ok_name", {}));
    CHECK(!flags.Define("ok_name", {"a", "a"}));
    CHECK(!flags.Define("ok_name", {"UPPER"}));
}

// ── 배정의 인코딩 ──────────────────────────────────────────────────
static void TestFlagSetWire() {
    const FeatureFlags flags = BuildFlags();
    const FlagSet resolved = flags.Resolve(4242);
    CHECK_EQ(resolved.Count(), (size_t)3);

    const std::string encoded = resolved.Encode();
    std::printf("  resolve(4242) = %s\n", encoded.c_str());
    // 이름 오름차순이라 삽입 순서와 무관하게 같은 바이트다.
    CHECK(encoded.rfind("entity_store=", 0) == 0);

    FlagSet decoded;
    std::string error;
    CHECK(FlagSet::Decode(encoded, decoded, &error));
    CHECK(error.empty());
    CHECK_EQ(decoded.Count(), resolved.Count());
    for (const FlagAssignment& item : resolved.All())
        CHECK_EQ(decoded.Get(item.name, "?"), item.arm);
    CHECK_EQ(decoded.Encode(), encoded);

    // 배정이 없는 이름은 호출자 기본값을 그대로 돌려준다.
    CHECK_EQ(decoded.Get("camera", "ortho2d"), std::string("ortho2d"));

    // 신뢰 경계 밖 입력의 거부 — 실패하면 out 은 손대지 않는다.
    FlagSet keep;
    CHECK(keep.Assign("scale", "large"));
    const std::string before = keep.Encode();
    CHECK(!FlagSet::Decode("no_equals_here", keep, &error));
    CHECK(!FlagSet::Decode("a=b;a=c", keep, &error));
    CHECK(!FlagSet::Decode("A=b", keep, &error));
    CHECK(!FlagSet::Decode("a=B", keep, &error));
    CHECK(!FlagSet::Decode(std::string(kFlagWireMax + 1, 'a'), keep, &error));
    CHECK_EQ(keep.Encode(), before);
}

// ── 운영자 파일 ────────────────────────────────────────────────────
static void TestFlagConfigFile() {
    const char* good =
        "# 운영자가 손으로 적는 파일\n"
        "variant entity_store aos soa ecs rollout=50\n"
        "variant pathing poly astar flowfield rollout=100 force=astar\n"
        "toggle scrap_weekend on\n"
        "toggle beta off rollout=25\n";
    FeatureFlags flags;
    std::string error;
    CHECK(ParseFlagConfig(good, flags, &error));
    CHECK(error.empty());
    CHECK_EQ(flags.DefinitionCount(), (size_t)2);
    CHECK_EQ(flags.ToggleCount(), (size_t)2);
    for (uint32_t player = 0; player < 50; ++player)
        CHECK_EQ(flags.ArmFor("pathing", player), std::string("astar"));
    CHECK(flags.IsEnabled("scrap_weekend"));

    // 원자적 폴백: 마지막 줄 하나가 틀리면 앞의 정의도 채택되지 않는다.
    const char* broken =
        "variant entity_store aos soa ecs rollout=50\n"
        "variant pathing poly astar flowfield rollout=250\n";
    FeatureFlags rejected;
    CHECK(rejected.Define("sentinel", {"kept"}));
    const size_t beforeCount = rejected.DefinitionCount();
    CHECK(!ParseFlagConfig(broken, rejected, &error));
    CHECK(!error.empty());
    CHECK_EQ(rejected.DefinitionCount(), beforeCount);
    CHECK(rejected.Defined("sentinel"));

    // force 오타는 파일 단계에서 거부한다 — 런타임 폴백은 서버가 내려보낸
    // 값을 위한 것이지 운영자의 오타를 감춰 주는 장치가 아니다.
    FeatureFlags typo;
    CHECK(!ParseFlagConfig("variant pathing poly astar force=flowfeild\n", typo, &error));
    CHECK(!ParseFlagConfig("variant\n", typo, &error));
    CHECK(!ParseFlagConfig("nonsense a b\n", typo, &error));
    CHECK(!ParseFlagConfig("toggle beta maybe\n", typo, &error));
    CHECK(!ParseFlagConfig("variant p a b unknown_key=1\n", typo, &error));

    // 빈 파일과 주석만 있는 파일은 정상(정의 0개).
    FeatureFlags empty;
    CHECK(ParseFlagConfig("", empty, &error));
    CHECK(ParseFlagConfig("# 아무것도 없다\n", empty, &error));
    CHECK_EQ(empty.DefinitionCount(), (size_t)0);
}

// ── on/off 롤아웃(옛 API)이 그대로 도는지 ──────────────────────────
static void TestToggleRolloutStillWorks() {
    FeatureFlags flags;
    flags.SetRollout("gradual", 50);
    const bool first = flags.IsEnabledForUser("gradual", 12345);
    for (int i = 0; i < 10; ++i) CHECK_EQ(flags.IsEnabledForUser("gradual", 12345), first);
    flags.SetRollout("none", 0);
    flags.SetRollout("all", 100);
    for (uint32_t u = 0; u < 200; ++u) {
        CHECK(!flags.IsEnabledForUser("none", u));
        CHECK(flags.IsEnabledForUser("all", u));
    }
}

static void RunTests() {
    TestStickyBucket();
    TestFlagsAreIndependent();
    TestRolloutConverges();
    TestUnknownFallsBackToDefaultArm();
    TestFlagSetWire();
    TestFlagConfigFile();
    TestToggleRolloutStillWorks();
}

TEST_MAIN("flag_rollout")
