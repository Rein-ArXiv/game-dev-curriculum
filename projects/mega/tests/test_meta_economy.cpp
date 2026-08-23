// 매치를 넘어 사는 통화의 보존 불변식과 영속 왕복.
//
// 100매치를 시뮬레이션하면서 매 거래 뒤에 세 불변식을 전부 확인한다.
//   A. supply == sourced - sunk
//   B. supply == 모든 계정 잔액의 합
//   C. 모든 잔액 >= 0
//
// C 가 이 파일의 변이 표적이다. Spend 의 잔액 검사를 지우면 A 와 B 는 산수라
// 그대로 참인 채로 잔액만 음수가 되고, InflationRate 는 계속 그럴듯한 수를 낸다.
//
// 이 파일이 **묻지 않는 것**: 매치 종료 정산이 **얼마를** 발행하는가. 여기
// 발행되는 금액은 이 파일이 지어낸 수이고, 제품의 `@settle-scrap` — 점수에서
// 기본액을 내고 활성 이벤트의 스크랩 배율을 곱하는 그 열 줄 — 은 지나지 않는다.
// 배율을 상수 1.0 으로 바꿔도 세 불변식은 그대로 참이다(반토막 난 발행도 보존은
// 지킨다). 그 자리는 `tests/test_live_ops_wiring.cpp` 가 같은 매치를 두 번 돌려
// 잰다.
#include "db/kv_store.hpp"
#include "db/repository.hpp"
#include "live/economy.hpp"
#include "test_util.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace mega::live;

static const char* kWalletPath = "mega_meta_economy_test.wal";
static void Cleanup() { std::remove(kWalletPath); }

static void CheckInvariants(const Economy& economy, const char* where) {
    std::string violation;
    if (!economy.Conserved(&violation)) {
        ++megatest::g_failures;
        std::printf("  FAIL %s: 보존 불변식 위반 — %s\n", where, violation.c_str());
    }
    ++megatest::g_checks;
}

// ── 100매치 시뮬 ───────────────────────────────────────────────────
static void TestConservationOverManyMatches() {
    Economy economy;
    // 결정적 의사 난수. 시계도 rand() 도 쓰지 않는다.
    uint32_t rng = 0x1234567u;
    auto next = [&rng]() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return rng;
    };

    const uint32_t players[] = {11u, 22u, 33u};
    int64_t granted = 0, spent = 0;
    int rejectedSpends = 0;

    for (int match = 0; match < 100; ++match) {
        for (uint32_t player : players) {
            // 매치 보상 — 웨이브에 비례한다고 치자.
            const int64_t reward = 40 + (next() % 120);
            CHECK(economy.Grant(player, reward, Source::MatchReward));
            granted += reward;
            if (match % 5 == 0) {
                CHECK(economy.Grant(player, 25, Source::ClearBonus));
                granted += 25;
            }
            CheckInvariants(economy, "grant");

            // 매치 밖 소비 — 잔액을 넘는 요구가 섞인다.
            const int64_t want = 30 + (next() % 400);
            if (economy.Spend(player, want, Sink::BlastCharge)) spent += want;
            else ++rejectedSpends;
            CheckInvariants(economy, "spend");
        }
    }

    CHECK_EQ(economy.Sourced(), granted);
    CHECK_EQ(economy.Sunk(), spent);
    CHECK_EQ(economy.Supply(), granted - spent);
    CHECK(rejectedSpends > 0);          // 거부 경로가 실제로 돌았다
    for (uint32_t player : players) CHECK(economy.Balance(player) >= 0);

    std::printf("  100매치 x %zu명: sourced=%lld sunk=%lld supply=%lld "
                "inflation=%.3f 거부된 지출=%d\n",
                sizeof(players) / sizeof(players[0]),
                (long long)economy.Sourced(), (long long)economy.Sunk(),
                (long long)economy.Supply(), economy.InflationRate(), rejectedSpends);
    std::printf("  source 내역: match_reward=%lld clear_bonus=%lld | "
                "sink 내역: blast_charge=%lld\n",
                (long long)economy.SourcedBy(Source::MatchReward),
                (long long)economy.SourcedBy(Source::ClearBonus),
                (long long)economy.SunkBy(Sink::BlastCharge));
    CHECK_EQ(economy.SourcedBy(Source::MatchReward) + economy.SourcedBy(Source::ClearBonus),
             economy.Sourced());
    CHECK_EQ(economy.SunkBy(Sink::BlastCharge), economy.Sunk());
}

// ── 잔액 부족은 거부되고 잔액은 음수가 되지 않는다 ─────────────────
static void TestSpendNeverGoesNegative() {
    Economy economy;
    CHECK(economy.Grant(1, 100, Source::MatchReward));
    CHECK(!economy.Spend(1, 101, Sink::Respec));       // 1 모자란다
    CHECK_EQ(economy.Balance(1), (int64_t)100);
    CHECK(economy.Spend(1, 100, Sink::Respec));        // 정확히 전부
    CHECK_EQ(economy.Balance(1), (int64_t)0);
    CHECK(!economy.Spend(1, 1, Sink::Respec));         // 빈 지갑
    CHECK(!economy.Spend(999, 1, Sink::Respec));       // 없는 계정
    CHECK(!economy.Spend(1, 0, Sink::Respec));         // 0 과 음수는 거래가 아니다
    CHECK(!economy.Spend(1, -50, Sink::Respec));
    CHECK(!economy.Grant(1, 0));
    CHECK(!economy.Grant(1, -50));
    CHECK(!economy.Grant(1, kMaxTransaction + 1));
    CheckInvariants(economy, "boundary");
    CHECK_EQ(economy.Balance(1), (int64_t)0);
}

// ── 인플레이션 지표 ────────────────────────────────────────────────
static void TestInflationRate() {
    Economy nothing;
    CHECK(std::fabs(nothing.InflationRate() - 0.0) < 1e-9);   // 발행 없음

    Economy hoard;
    hoard.Grant(1, 1000);
    CHECK(std::fabs(hoard.InflationRate() - 1.0) < 1e-9);     // sink 없음

    Economy balanced;
    balanced.Grant(1, 1000);
    balanced.Spend(1, 1000);
    CHECK(std::fabs(balanced.InflationRate() - 0.0) < 1e-9);  // 전량 회수

    Economy half;
    half.Grant(1, 1000);
    half.Spend(1, 500);
    CHECK(std::fabs(half.InflationRate() - 0.5) < 1e-9);
}

// ── 영구 강화가 다음 매치의 시작 조건을 바꾼다 ─────────────────────
static void TestPermanentUpgrades() {
    Economy economy;
    CHECK(economy.Grant(5, 5000, Source::MatchReward));

    CHECK_EQ(Economy::StartGoldBonus(0), 0);
    const int64_t firstCost = Economy::StartGoldTierCost(0);
    CHECK(economy.BuyStartGoldTier(5));
    CHECK_EQ(economy.StartGoldTier(5), 1u);
    CHECK_EQ(economy.Balance(5), (int64_t)5000 - firstCost);
    CHECK_EQ(Economy::StartGoldBonus(economy.StartGoldTier(5)), 40);
    // 가격은 티어마다 오른다 — sink 를 키우지 않으면 후반 잔액이 단조 증가한다.
    CHECK(Economy::StartGoldTierCost(1) > Economy::StartGoldTierCost(0));

    while (economy.StartGoldTier(5) < kMaxStartGoldTier) CHECK(economy.BuyStartGoldTier(5));
    CHECK(!economy.BuyStartGoldTier(5));           // 상한
    CHECK_EQ(economy.StartGoldTier(5), kMaxStartGoldTier);

    CHECK(!economy.HasUnlock(5, 4));
    CHECK(economy.UnlockTower(5, 4));
    CHECK(economy.HasUnlock(5, 4));
    CHECK(!economy.UnlockTower(5, 4));             // 두 번 사지 않는다
    CHECK(economy.UnlockTower(5, 2));
    CheckInvariants(economy, "upgrades");
    CHECK_EQ(economy.SunkBy(Sink::StartGold) + economy.SunkBy(Sink::TowerUnlock),
             economy.Sunk());

    // 잔액이 부족하면 강화도 거부된다(그리고 티어는 오르지 않는다).
    Economy poor;
    poor.Grant(9, 10, Source::MatchReward);
    CHECK(!poor.BuyStartGoldTier(9));
    CHECK_EQ(poor.StartGoldTier(9), 0u);
    CHECK(!poor.UnlockTower(9, 1));
    CHECK(!poor.HasUnlock(9, 1));
    CheckInvariants(poor, "poor");
}

// ── 저장·재오픈 후 잔액 보존 ───────────────────────────────────────
static void TestWalletSurvivesReopen() {
    Cleanup();
    Wallet saved;
    {
        Economy economy;
        CHECK(economy.Grant(7, 2400, Source::MatchReward));
        CHECK(economy.Spend(7, 300, Sink::BlastCharge));
        CHECK(economy.BuyStartGoldTier(7));
        CHECK(economy.UnlockTower(7, 3));
        saved = economy.Export(7);

        mega::db::KvStore store;
        CHECK(store.Open(kWalletPath));
        mega::db::Repository<Wallet> wallets(store, "wallet:");
        store.Begin();
        wallets.Save(saved.account, saved);
        CHECK(store.Commit());
        store.Close();
    }
    {
        mega::db::KvStore store;
        CHECK(store.Open(kWalletPath));      // WAL 재생
        mega::db::Repository<Wallet> wallets(store, "wallet:");
        Wallet loaded;
        CHECK(wallets.Load(7, loaded));
        store.Close();

        CHECK_EQ(loaded.balance, saved.balance);
        CHECK_EQ(loaded.sourced, saved.sourced);
        CHECK_EQ(loaded.sunk, saved.sunk);
        CHECK_EQ(loaded.startGoldTier, saved.startGoldTier);
        CHECK_EQ(loaded.unlocks.size(), saved.unlocks.size());

        Economy restored;
        std::string error;
        CHECK(restored.Import(loaded, &error));
        CHECK(error.empty());
        CHECK_EQ(restored.Balance(7), saved.balance);
        CHECK(restored.HasUnlock(7, 3));
        CHECK_EQ(restored.StartGoldTier(7), saved.startGoldTier);
        CheckInvariants(restored, "restored");
        std::printf("  왕복: balance=%lld sourced=%lld sunk=%lld tier=%u unlocks=%zu\n",
                    (long long)loaded.balance, (long long)loaded.sourced,
                    (long long)loaded.sunk, loaded.startGoldTier, loaded.unlocks.size());
    }
    Cleanup();
}

// ── 손상된 지갑은 통째로 거부된다 ──────────────────────────────────
static void TestCorruptWalletRejected() {
    Economy economy;
    CHECK(economy.Grant(1, 500, Source::MatchReward));
    const int64_t before = economy.Balance(1);
    std::string error;

    Wallet bad;
    bad.account = 1;
    bad.balance = -10; bad.sourced = 0; bad.sunk = 10;
    CHECK(!economy.Import(bad, &error));
    CHECK(!error.empty());

    bad = Wallet{};
    bad.account = 1; bad.balance = 100; bad.sourced = 100; bad.sunk = 50;   // 100 != 50
    CHECK(!economy.Import(bad, &error));

    bad = Wallet{};
    bad.account = 1; bad.startGoldTier = kMaxStartGoldTier + 1;
    CHECK(!economy.Import(bad, &error));

    bad = Wallet{};
    bad.account = 1; bad.unlocks = {3u, 3u};      // 오름차순 아님
    CHECK(!economy.Import(bad, &error));

    bad = Wallet{};
    bad.account = 1; bad.schemaVersion = 99;
    CHECK(!economy.Import(bad, &error));

    CHECK_EQ(economy.Balance(1), before);
    CheckInvariants(economy, "reject");
}

static void RunTests() {
    TestConservationOverManyMatches();
    TestSpendNeverGoesNegative();
    TestInflationRate();
    TestPermanentUpgrades();
    TestWalletSurvivesReopen();
    TestCorruptWalletRejected();
}

TEST_MAIN("meta_economy")
