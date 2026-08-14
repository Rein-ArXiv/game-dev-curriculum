// 레코드 레포지토리 검증: 직렬화 저장/복원·영속·크로스 레포 트랜잭션 원자성.
#include "db/kv_store.hpp"
#include "db/records.hpp"
#include "db/repository.hpp"
#include "test_util.h"

#include <cstdio>
#include <string>

using namespace mega::db;

static const char* kPath = "mega_db_repo_test.wal";
static void Cleanup() { std::remove(kPath); }

static void TestAccountRoundtrip() {
    Cleanup();
    KvStore store; store.Open(kPath);
    Repository<Account> accounts(store, "acc:");

    Account a; a.id = 7; a.name = "lunacat"; a.rating = 1234; a.createdAt = 1000;
    accounts.Save(a.id, a);

    Account loaded;
    CHECK(accounts.Load(7, loaded));
    CHECK_EQ(loaded.id, 7u);
    CHECK_EQ(loaded.name, std::string("lunacat"));
    CHECK_EQ(loaded.rating, 1234u);
    CHECK_EQ(loaded.createdAt, (uint64_t)1000);
    CHECK(!accounts.Load(99, loaded));      // 없는 id
    store.Close();
    Cleanup();
}

static void TestPersistAcrossReopen() {
    Cleanup();
    {
        KvStore store; store.Open(kPath);
        Repository<MatchResult> matches(store, "match:");
        MatchResult m; m.id = 42; m.hostAccount = 7; m.waveReached = 5; m.score = 9001; m.cleared = 1; m.endedAt = 2000;
        matches.Save(m.id, m);
        store.Close();
    }
    {
        KvStore store; store.Open(kPath);   // WAL 재생
        Repository<MatchResult> matches(store, "match:");
        MatchResult m;
        CHECK(matches.Load(42, m));
        CHECK_EQ(m.score, 9001u);
        CHECK_EQ(m.cleared, (uint8_t)1);
        CHECK_EQ(m.waveReached, 5u);
        store.Close();
    }
    Cleanup();
}

// 한 트랜잭션으로 여러 레포(계정·매치·강화)를 원자적으로 갱신.
static void TestCrossRepoTransaction() {
    Cleanup();
    KvStore store; store.Open(kPath);
    Repository<Account>     accounts(store, "acc:");
    Repository<MatchResult> matches(store, "match:");
    Repository<Upgrade>     upgrades(store, "up:");

    Account a; a.id = 1; a.name = "p1"; a.rating = 1000;
    accounts.Save(a.id, a);

    // 매치 종료 정산: 매치 기록 + 레이팅 갱신 + 강화 지급을 한 트랜잭션으로.
    store.Begin();
    MatchResult m; m.id = 100; m.hostAccount = 1; m.waveReached = 10; m.cleared = 1; m.score = 5000;
    matches.Save(m.id, m);
    a.rating = 1050;
    accounts.Save(a.id, a);
    Upgrade up; up.accountId = 1; up.towerType = 2; up.tier = 1;
    upgrades.Save(1, up);
    CHECK(store.Commit());

    // 전부 반영됐는가.
    Account a2; CHECK(accounts.Load(1, a2)); CHECK_EQ(a2.rating, 1050u);
    MatchResult m2; CHECK(matches.Load(100, m2)); CHECK_EQ(m2.score, 5000u);
    Upgrade up2; CHECK(upgrades.Load(1, up2)); CHECK_EQ(up2.tier, 1u);
    store.Close();

    // 재시작 후에도 트랜잭션 결과 영속.
    KvStore store2; store2.Open(kPath);
    Repository<Account> acc2(store2, "acc:");
    Account a3; CHECK(acc2.Load(1, a3)); CHECK_EQ(a3.rating, 1050u);
    store2.Close();
    Cleanup();
}

static void TestRemove() {
    Cleanup();
    KvStore store; store.Open(kPath);
    Repository<Upgrade> upgrades(store, "up:");
    Upgrade up; up.accountId = 5; up.tier = 3;
    upgrades.Save(5, up);
    CHECK(upgrades.Exists(5));
    upgrades.Remove(5);
    CHECK(!upgrades.Exists(5));
    store.Close();
    Cleanup();
}

static void RunTests() {
    TestAccountRoundtrip();
    TestPersistAcrossReopen();
    TestCrossRepoTransaction();
    TestRemove();
}

TEST_MAIN("db_repo")
