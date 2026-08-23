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

// ── 해금 선행 그래프 ───────────────────────────────────────────────
//
// 파일 한 장이 "A 를 열어야 B 를 살 수 있다"를 정한다. 이 절이 묻는 것은 다섯이다.
//   (1) 순환이 있는 파일은 거부되고, 진단에 순환 구성요소의 id 가 나온다.
//   (2) 정상 파일의 위상 순서에서 모든 간선이 pos[u] < pos[v] 를 만족한다.
//   (3) 선행이 없는 상태의 해금 시도는 거절되고 **스크랩이 소각되지 않는다**.
//   (4) 선행 폐포를 어긴 지갑은 로드가 거부된다.
//   (5) 같은 파일은 같은 순서를 낸다.
static void TestUnlockGraphRejectsCycles() {
    UnlockGraph graph;
    std::string error;

    // 자기 자신을 가리키는 간선도 순환이다.
    CHECK(!graph.Parse("unlock 0 requires=0\n", &error));
    CHECK(error.find("순환") != std::string::npos);
    CHECK(error.find("[0]") != std::string::npos);
    CHECK(graph.Empty());

    // 세 항목이 서로를 문다. 진단은 "순환이 있다"가 아니라 **누구인지**를 적어야
    // 저작자가 고칠 수 있다.
    CHECK(!graph.Parse("unlock 1 requires=3\nunlock 2 requires=1\nunlock 3 requires=2\n",
                       &error));
    CHECK(error.find("[1,2,3]") != std::string::npos);
    std::printf("  순환 진단: %s\n", error.c_str());

    // 순환에 걸리지 않은 항목이 함께 있어도 파일 전체가 거부된다 — 절반만 적용된
    // 선행 그래프는 상점에 영원히 회색인 항목을 남긴다.
    CHECK(!graph.Parse("unlock 0\nunlock 1 requires=2\nunlock 2 requires=1\n", &error));
    CHECK(graph.Empty());

    // 문법·범위·중복·미선언 선행도 전부 거부.
    CHECK(!graph.Parse("lock 0\n", &error));
    CHECK(!graph.Parse("unlock 99\n", &error));
    CHECK(!graph.Parse("unlock 0\nunlock 0\n", &error));
    CHECK(!graph.Parse("unlock 0 requires=5\n", &error));
    CHECK(!graph.Parse("unlock 0 requires=\n", &error));
    CHECK(!graph.Parse("unlock 1 needs=0\n", &error));
    CHECK(!graph.Parse("# 주석뿐\n", &error));
}

static void TestUnlockOrderIsTopologicalAndStable() {
    // 줄 순서를 일부러 뒤섞는다. 위상 순서는 줄 순서가 아니라 간선이 정한다.
    const char* text =
        "# 상점 선행\n"
        "unlock 3 requires=1,2\n"
        "unlock 2 requires=0\n"
        "unlock 0\n"
        "unlock 1 requires=0\n";
    UnlockGraph graph;
    std::string error;
    CHECK(graph.Parse(text, &error));
    CHECK_EQ(graph.Size(), static_cast<size_t>(4));

    const std::vector<uint32_t>& order = graph.ShopOrder();
    CHECK_EQ(order.size(), static_cast<size_t>(4));
    auto positionOf = [&order](uint32_t type) {
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i] == type) return static_cast<int>(i);
        return -1;
    };
    // (2) 모든 간선에서 선행이 앞선다.
    CHECK(positionOf(0) < positionOf(1));
    CHECK(positionOf(0) < positionOf(2));
    CHECK(positionOf(1) < positionOf(3));
    CHECK(positionOf(2) < positionOf(3));
    // 동점(1 과 2 는 동시에 준비된다)은 종류 번호 오름차순으로 갈린다. 삽입
    // 순서로 갈리면 위 텍스트의 줄 순서 때문에 2 가 1 보다 앞에 온다.
    CHECK(positionOf(1) < positionOf(2));

    // (5) 줄 순서를 다르게 적은 같은 그래프가 같은 순서를 낸다.
    const char* shuffled =
        "unlock 0\n"
        "unlock 2 requires=0\n"
        "unlock 1 requires=0\n"
        "unlock 3 requires=2,1\n";
    UnlockGraph other;
    CHECK(other.Parse(shuffled, &error));
    CHECK(other.ShopOrder() == order);
    std::printf("  상점 순서 %u>%u>%u>%u (줄 순서와 무관)\n",
                order[0], order[1], order[2], order[3]);
}

static void TestUnlockRequiresPrerequisitesAndConservesScrap() {
    UnlockGraph graph;
    std::string error;
    CHECK(graph.Parse("unlock 0\nunlock 1 requires=0\nunlock 2 requires=0\n"
                      "unlock 3 requires=1,2\n", &error));

    Economy economy;
    economy.SetUnlockGraph(graph);
    CHECK(economy.Grant(7, 100000, Source::MatchReward));
    const int64_t start = economy.Balance(7);

    // (3) 선행이 없으면 거절이고, 스크랩은 한 푼도 타지 않는다.
    CHECK(!economy.UnlockTower(7, 3));
    CHECK_EQ(economy.Balance(7), start);
    CHECK_EQ(economy.Sunk(), static_cast<int64_t>(0));
    CheckInvariants(economy, "unlock-rejected");

    // 선행부터 순서대로 사면 전부 통과한다.
    CHECK(economy.UnlockTower(7, 0));
    CHECK(!economy.UnlockTower(7, 3));      // 아직 1·2 가 없다
    CHECK(economy.UnlockTower(7, 1));
    CHECK(!economy.UnlockTower(7, 3));      // 2 가 없다
    CHECK(economy.UnlockTower(7, 2));
    CHECK(economy.UnlockTower(7, 3));
    CHECK(!economy.UnlockTower(7, 3));      // 두 번 살 수는 없다
    CheckInvariants(economy, "unlock-ordered");

    const int64_t spent = Economy::TowerUnlockCost(0) + Economy::TowerUnlockCost(1) +
                          Economy::TowerUnlockCost(2) + Economy::TowerUnlockCost(3);
    CHECK_EQ(economy.Balance(7), start - spent);
    CHECK_EQ(economy.Sunk(), spent);

    // 저작되지 않은 종류는 상점에 없다.
    CHECK(!economy.UnlockTower(7, 9));
    CHECK_EQ(economy.Balance(7), start - spent);

    // 그래프가 비어 있으면(파일 없음) 선행이라는 개념이 없던 시절 그대로다.
    Economy plain;
    CHECK(plain.Grant(7, 100000, Source::MatchReward));
    CHECK(plain.UnlockTower(7, 3));      // 아무 순서로나 열린다
    CheckInvariants(plain, "no-graph");
}

static void TestWalletMustBePrerequisiteClosed() {
    UnlockGraph graph;
    std::string error;
    CHECK(graph.Parse("unlock 0\nunlock 1 requires=0\nunlock 3 requires=1\n", &error));

    Economy economy;
    economy.SetUnlockGraph(graph);

    // (4) 잎만 들고 있는 지갑은 이 게임에서 도달할 수 없는 상태다.
    Wallet leaf;
    leaf.account = 2;
    leaf.balance = 500; leaf.sourced = 500; leaf.sunk = 0;
    leaf.unlocks = {3u};
    CHECK(!economy.Import(leaf, &error));
    CHECK(error.find("선행") != std::string::npos);

    // 저작되지 않은 종류를 들고 있어도 거부.
    Wallet alien = leaf;
    alien.unlocks = {7u};
    CHECK(!economy.Import(alien, &error));

    // 폐포면 통과.
    Wallet closed = leaf;
    closed.unlocks = {0u, 1u, 3u};
    CHECK(economy.Import(closed, &error));
    CHECK_EQ(economy.Balance(2), static_cast<int64_t>(500));
    CheckInvariants(economy, "closure");

    // 그래프가 없으면 예전처럼 통과한다.
    Economy plain;
    CHECK(plain.Import(leaf, &error));
}

static void RunTests() {
    TestUnlockGraphRejectsCycles();
    TestUnlockOrderIsTopologicalAndStable();
    TestUnlockRequiresPrerequisitesAndConservesScrap();
    TestWalletMustBePrerequisiteClosed();
    TestConservationOverManyMatches();
    TestSpendNeverGoesNegative();
    TestInflationRate();
    TestPermanentUpgrades();
    TestWalletSurvivesReopen();
    TestCorruptWalletRejected();
}

TEST_MAIN("meta_economy")
