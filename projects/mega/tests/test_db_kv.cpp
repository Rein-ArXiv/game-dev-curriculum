// WAL KV 스토어 검증: 기본 CRUD·트랜잭션·내구성·크래시 원자성.
#include "db/kv_store.hpp"
#include "test_util.h"

#include <cstdio>
#include <string>

using namespace mega::db;

static const char* kPath = "mega_db_test.wal";
static void Cleanup() { std::remove(kPath); }

static void TestBasicCrud() {
    Cleanup();
    KvStore db;
    CHECK(db.Open(kPath));
    CHECK(db.Put("a", "1"));
    CHECK(db.Put("b", "2"));
    std::string v;
    CHECK(db.Get("a", v)); CHECK_EQ(v, std::string("1"));
    CHECK(db.Get("b", v)); CHECK_EQ(v, std::string("2"));
    CHECK(!db.Get("z", v));
    CHECK_EQ(db.Size(), (size_t)2);

    db.Put("a", "99");                       // 덮어쓰기
    CHECK(db.Get("a", v)); CHECK_EQ(v, std::string("99"));
    db.Delete("b");
    CHECK(!db.Has("b"));
    CHECK_EQ(db.Size(), (size_t)1);
    db.Close();
    Cleanup();
}

static void TestDurability() {
    Cleanup();
    {
        KvStore db;
        db.Open(kPath);
        db.Put("name", "lunacat");
        db.Put("score", "12345");
        db.Close();                          // 파일에 영속
    }
    {
        KvStore db2;                          // 새 인스턴스 — WAL 재생
        CHECK(db2.Open(kPath));
        std::string v;
        CHECK(db2.Get("name", v)); CHECK_EQ(v, std::string("lunacat"));
        CHECK(db2.Get("score", v)); CHECK_EQ(v, std::string("12345"));
        CHECK_EQ(db2.Size(), (size_t)2);
        db2.Close();
    }
    Cleanup();
}

static void TestTransactionCommit() {
    Cleanup();
    KvStore db; db.Open(kPath);
    db.Begin();
    db.Put("x", "10");
    db.Put("y", "20");
    CHECK(db.Commit());
    std::string v;
    CHECK(db.Get("x", v)); CHECK_EQ(v, std::string("10"));
    CHECK(db.Get("y", v)); CHECK_EQ(v, std::string("20"));
    db.Close();

    // 재시작 후에도 커밋된 트랜잭션 존재.
    KvStore db2; db2.Open(kPath);
    CHECK(db2.Has("x")); CHECK(db2.Has("y"));
    db2.Close();
    Cleanup();
}

static void TestTransactionRollback() {
    Cleanup();
    KvStore db; db.Open(kPath);
    db.Put("keep", "yes");
    db.Begin();
    db.Put("temp", "no");
    db.Rollback();                           // 버림
    CHECK(!db.Has("temp"));
    CHECK(db.Has("keep"));
    db.Close();

    KvStore db2; db2.Open(kPath);            // 롤백분은 로그에도 없음
    CHECK(!db2.Has("temp"));
    CHECK(db2.Has("keep"));
    db2.Close();
    Cleanup();
}

// 크래시 원자성: 커밋 마커 전에 끊긴 트랜잭션은 재생이 통째로 버린다.
static void TestCrashAtomicity() {
    Cleanup();
    {
        KvStore db; db.Open(kPath);
        db.Put("committed", "ok");           // 정상 커밋
        db.Close();
    }
    // WAL 끝에 "미완 트랜잭션"을 흉내내 토막난 PUT 레코드를 덧붙인다(커밋 마커 없음).
    {
        std::FILE* f = std::fopen(kPath, "ab");
        uint8_t put = 1;                     // kPut
        std::fwrite(&put, 1, 1, f);
        uint8_t kl[4] = {5, 0, 0, 0};        // keyLen=5
        std::fwrite(kl, 1, 4, f);
        std::fwrite("dirty", 1, 5, f);
        uint8_t vl[4] = {100, 0, 0, 0};      // valLen=100 인데 실제 바이트는 부족(토막)
        std::fwrite(vl, 1, 4, f);
        std::fwrite("xx", 1, 2, f);          // 100바이트 약속했지만 2바이트만 — 크래시 흉내
        std::fclose(f);
    }
    {
        KvStore db; CHECK(db.Open(kPath));    // 재생: 토막 레코드 무시, 크래시 없음
        CHECK(db.Has("committed"));           // 커밋된 것은 살아 있음
        std::string v; CHECK(db.Get("committed", v)); CHECK_EQ(v, std::string("ok"));
        CHECK(!db.Has("dirty"));              // 미완 트랜잭션은 적용 안 됨(원자성)
        db.Close();
    }
    Cleanup();
}

static void TestManyKeys() {
    Cleanup();
    KvStore db; db.Open(kPath);
    db.Begin();
    for (int i = 0; i < 1000; ++i) db.Put("k" + std::to_string(i), std::to_string(i * 7));
    db.Commit();
    db.Close();

    KvStore db2; db2.Open(kPath);
    CHECK_EQ(db2.Size(), (size_t)1000);
    std::string v;
    CHECK(db2.Get("k500", v)); CHECK_EQ(v, std::string("3500"));
    db2.Close();
    Cleanup();
}

static void RunTests() {
    TestBasicCrud();
    TestDurability();
    TestTransactionCommit();
    TestTransactionRollback();
    TestCrashAtomicity();
    TestManyKeys();
}

TEST_MAIN("db_kv")
