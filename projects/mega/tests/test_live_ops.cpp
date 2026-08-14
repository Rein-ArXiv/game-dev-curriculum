// 경제·패치·피처플래그 검증.
#include "live/economy.hpp"
#include "live/feature_flag.hpp"
#include "live/patch.hpp"
#include "test_util.h"

#include <cmath>

using namespace mega::live;

static void TestEconomyBasic() {
    Economy e;
    e.Grant(1, 100);                 // source
    e.Grant(1, 50);
    CHECK_EQ(e.Balance(1), (int64_t)150);
    CHECK_EQ(e.Supply(), (int64_t)150);

    CHECK(e.Spend(1, 60));           // sink
    CHECK_EQ(e.Balance(1), (int64_t)90);
    CHECK_EQ(e.Supply(), (int64_t)90);
    CHECK(!e.Spend(1, 1000));        // 잔고 부족
    CHECK_EQ(e.Balance(1), (int64_t)90);

    CHECK_EQ(e.Sourced(), (int64_t)150);
    CHECK_EQ(e.Sunk(), (int64_t)60);
}

static void TestInflation() {
    // sink 가 없으면 인플레율 1(전부 쌓임).
    Economy e1;
    e1.Grant(1, 1000);
    CHECK(std::fabs(e1.InflationRate() - 1.0) < 1e-9);

    // sink 가 source 를 전부 흡수하면 0.
    Economy e2;
    e2.Grant(1, 1000);
    e2.Spend(1, 1000);
    CHECK(std::fabs(e2.InflationRate() - 0.0) < 1e-9);

    // 절반 흡수면 0.5.
    Economy e3;
    e3.Grant(1, 1000);
    e3.Spend(1, 500);
    CHECK(std::fabs(e3.InflationRate() - 0.5) < 1e-9);

    // 가격은 티어마다 2배.
    CHECK_EQ(Economy::UpgradeCost(0), (int64_t)50);
    CHECK_EQ(Economy::UpgradeCost(1), (int64_t)100);
    CHECK_EQ(Economy::UpgradeCost(3), (int64_t)400);
}

static void TestPatch() {
    PatchManifest pm;
    CHECK(pm.Publish(100, 0xAAAA));
    CHECK(pm.Publish(101, 0xBBBB));
    CHECK(pm.Publish(102, 0xCCCC));
    CHECK_EQ(pm.Current(), 102u);
    CHECK_EQ(pm.CurrentHash(), (uint64_t)0xCCCC);
    CHECK_EQ(pm.VersionCount(), (size_t)3);

    // 역행 거부(단조 증가).
    CHECK(!pm.Publish(101, 0xDDDD));

    // 롤백.
    CHECK(pm.Rollback());
    CHECK_EQ(pm.Current(), 101u);
    CHECK_EQ(pm.CurrentHash(), (uint64_t)0xBBBB);
    CHECK(pm.Rollback());
    CHECK_EQ(pm.Current(), 100u);
    CHECK(!pm.Rollback());           // 더 못 되돌림

    // 롤포워드.
    CHECK(pm.RollForward());
    CHECK_EQ(pm.Current(), 101u);
}

static void TestFeatureFlags() {
    FeatureFlags ff;
    ff.Set("new_ui", true);
    ff.Set("beta", false);
    CHECK(ff.IsEnabled("new_ui"));
    CHECK(!ff.IsEnabled("beta"));
    CHECK(!ff.IsEnabled("unknown"));

    // sticky: 같은 사용자는 항상 같은 결과.
    ff.SetRollout("gradual", 50);
    bool first = ff.IsEnabledForUser("gradual", 12345);
    for (int i = 0; i < 10; ++i) CHECK_EQ(ff.IsEnabledForUser("gradual", 12345), first);

    // 0% / 100% 경계.
    ff.SetRollout("none", 0);
    ff.SetRollout("all", 100);
    bool anyNone = false, allOn = true;
    for (uint32_t u = 0; u < 200; ++u) {
        if (ff.IsEnabledForUser("none", u)) anyNone = true;
        if (!ff.IsEnabledForUser("all", u)) allOn = false;
    }
    CHECK(!anyNone);                 // 0% → 아무도
    CHECK(allOn);                    // 100% → 전원

    // 50% 롤아웃은 대략 절반(±15%).
    int on = 0;
    for (uint32_t u = 0; u < 1000; ++u) if (ff.IsEnabledForUser("gradual", u)) ++on;
    CHECK(on > 350 && on < 650);

    // A/B 변형 결정성.
    int v = ff.Variant("exp", 777, 3);
    CHECK(v >= 0 && v < 3);
    CHECK_EQ(ff.Variant("exp", 777, 3), v);   // 같은 사용자 같은 변형
}

static void RunTests() {
    TestEconomyBasic();
    TestInflation();
    TestPatch();
    TestFeatureFlags();
}

TEST_MAIN("live_ops")
