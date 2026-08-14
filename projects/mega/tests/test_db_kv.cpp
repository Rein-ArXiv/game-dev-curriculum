// WAL KV 스토어 검증: 기본 CRUD·트랜잭션·내구성·크래시 원자성.
#include "db/kv_store.hpp"
#include "test_util.h"

#include <cstdint>
#include <cstdio>
#include <string>

using namespace mega::db;

static const char* kPath = "mega_db_test.wal";
static void Cleanup() { std::remove(kPath); }

// ── WAL 바이트를 손으로 짓는 도구 ────────────────────────────────────
//
// kv_store 의 레코드 형식(구현과 1:1):
//   PUT     [1][keyLen u32le][key][valLen u32le][val]
//   DEL     [2][keyLen u32le][key]
//   COMMIT  [3]
//
// 이 도구가 필요한 이유: 크래시는 "토막난 레코드"로만 오지 않는다. fwrite 가
// 레코드 경계에서 끊기면 **완전한 레코드인데 COMMIT 마커가 없는** 로그가 남고,
// 그것이 원자성이 실제로 시험받는 지점이다. 토막 레코드는 파서의 경계 검사에서
// 걸러지므로 커밋 마커 검사를 한 번도 밟지 않는다 — 그 경로를 직접 짓는다.
static void WalU32(std::FILE* f, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF),
                    (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 24) & 0xFF)};
    std::fwrite(b, 1, 4, f);
}
static void WalPut(std::FILE* f, const std::string& k, const std::string& v) {
    uint8_t t = 1;
    std::fwrite(&t, 1, 1, f);
    WalU32(f, (uint32_t)k.size()); std::fwrite(k.data(), 1, k.size(), f);
    WalU32(f, (uint32_t)v.size()); std::fwrite(v.data(), 1, v.size(), f);
}
static void WalDel(std::FILE* f, const std::string& k) {
    uint8_t t = 2;
    std::fwrite(&t, 1, 1, f);
    WalU32(f, (uint32_t)k.size()); std::fwrite(k.data(), 1, k.size(), f);
}
static void WalCommit(std::FILE* f) { uint8_t t = 3; std::fwrite(&t, 1, 1, f); }

static long FileSize(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return -1;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fclose(f);
    return n;
}

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

// 완전한 레코드 + COMMIT 마커 없음 = 진짜 크래시 원자성 시험.
//
// 위 TestCrashAtomicity 는 valLen 이 파일 끝을 넘는 토막 레코드를 넣는다. 그건
// 파서의 경계 검사(`pos + vl > buf.size()`)에서 걸러지므로, 커밋 마커가 있든
// 없든 결과가 같다 — 즉 COMMIT 검사를 무력화해도 그 테스트는 통과한다.
// 여기서는 바이트가 완전한 레코드를 넣어 재생이 마커까지 도달하게 만든다.
//
// 계약(구현 Replay()): 레코드는 pending 에 쌓이고 kCommit 을 만나야 Apply 된다.
// 루프가 끝날 때 pending 에 남은 것은 그대로 버려진다 → 미커밋 레코드는 무시.
static void TestReplayDropsCompleteUncommittedTail() {
    // 커밋된 로그 뒤에 "완전한 PUT + 완전한 DEL"을 덧붙인다. tailCommit 이 true 면
    // 그 뒤에 COMMIT 마커 한 바이트를 더 붙인다. 두 로그의 차이는 그 1바이트뿐이다.
    auto Build = [](bool tailCommit) {
        Cleanup();
        {
            KvStore db; db.Open(kPath);
            db.Put("committed", "ok");           // 정상 커밋 트랜잭션
            db.Close();
        }
        long before = FileSize(kPath);
        {
            std::FILE* f = std::fopen(kPath, "ab");
            CHECK(f != nullptr);
            WalPut(f, "dirty", "payload");       // 완전한 레코드(토막 아님)
            WalDel(f, "committed");              // 완전한 삭제 레코드
            if (tailCommit) WalCommit(f);
            std::fclose(f);
        }
        // 꼬리가 실제로 기록됐는지 — 0바이트를 붙여 놓고 "무시됐다"고 우기지 않는다.
        CHECK(FileSize(kPath) > before);
    };

    // (1) COMMIT 마커 없음 → 꼬리는 통째로 버려진다.
    Build(false);
    {
        KvStore db; CHECK(db.Open(kPath));
        std::string v;
        CHECK(db.Has("committed"));                          // 미커밋 DEL 이 안 먹었다
        CHECK(db.Get("committed", v)); CHECK_EQ(v, std::string("ok"));
        CHECK(!db.Has("dirty"));                             // 미커밋 PUT 이 안 먹었다
        CHECK_EQ(db.Size(), (size_t)1);
        db.Close();
    }

    // (2) 대조군 — 같은 바이트열 + COMMIT 마커 1바이트 → 꼬리가 적용된다.
    //     이게 (1)의 통과가 "레코드가 사실은 깨져 있어서"가 아님을 증명한다.
    //     차이가 마커 한 바이트뿐이므로, 결과 차이의 원인도 그 한 바이트뿐이다.
    Build(true);
    {
        KvStore db; CHECK(db.Open(kPath));
        std::string v;
        CHECK(!db.Has("committed"));                         // 커밋된 DEL 이 먹었다
        CHECK(db.Has("dirty"));
        CHECK(db.Get("dirty", v)); CHECK_EQ(v, std::string("payload"));
        CHECK_EQ(db.Size(), (size_t)1);
        db.Close();
    }
    Cleanup();
}

// 트랜잭션 여러 개가 섞인 로그: 커밋된 것만 살아남는가.
// 미커밋 꼬리가 앞선 커밋 값을 덮어쓰거나 지우려 드는 배치를 골랐다 —
// 마커 검사가 없으면 b 는 "BAD" 가 되고 c 는 사라진다.
static void TestReplayMixedCommittedAndUncommitted() {
    Cleanup();
    {
        std::FILE* f = std::fopen(kPath, "wb");
        CHECK(f != nullptr);
        WalPut(f, "a", "1"); WalPut(f, "b", "2"); WalCommit(f);   // txn1 커밋
        WalPut(f, "c", "3"); WalDel(f, "a");     WalCommit(f);   // txn2 커밋
        WalPut(f, "d", "4");                                      // txn3 — 미커밋
        WalPut(f, "b", "BAD");                                    //   (완전한 레코드,
        WalDel(f, "c");                                           //    COMMIT 마커 없음)
        std::fclose(f);
    }
    {
        KvStore db; CHECK(db.Open(kPath));
        std::string v;
        CHECK(!db.Has("a"));                                  // txn2 의 커밋된 삭제
        CHECK(db.Get("b", v)); CHECK_EQ(v, std::string("2")); // txn3 덮어쓰기는 버려짐
        CHECK(db.Get("c", v)); CHECK_EQ(v, std::string("3")); // txn3 삭제도 버려짐
        CHECK(!db.Has("d"));                                  // txn3 삽입도 버려짐
        CHECK_EQ(db.Size(), (size_t)2);
        db.Close();
    }
    Cleanup();
}

// 미커밋 꼬리는 재기동 뒤에도 되살아나지 않아야 한다.
//
// WAL 은 append-only 다. 크래시가 남긴 미커밋 꼬리를 파일에 그대로 둔 채 새 쓰기를
// 이어 붙이면, 새 트랜잭션이 적는 COMMIT 마커 하나가 **그 꼬리까지 커밋해 버린다** —
// 재생기는 pending 에 무엇이 언제 쌓였는지 구분하지 않기 때문이다. 원자성이 재기동
// 한 번을 못 넘긴다. 그래서 Open 은 마지막 COMMIT 지점까지로 로그를 잘라낸다.
static void TestUncommittedTailNeverResurfaces() {
    Cleanup();
    {
        std::FILE* f = std::fopen(kPath, "wb");
        CHECK(f != nullptr);
        WalPut(f, "real", "1"); WalCommit(f);   // 커밋된 트랜잭션
        WalPut(f, "ghost", "boo");              // 미커밋 꼬리(완전한 레코드)
        std::fclose(f);
    }
    {
        KvStore db; CHECK(db.Open(kPath));      // 1차 재기동 — 꼬리 무시
        CHECK(db.Has("real"));
        CHECK(!db.Has("ghost"));
        db.Put("later", "yes");                 // 크래시 후 정상 운영 재개
        db.Close();
    }
    {
        KvStore db; CHECK(db.Open(kPath));      // 2차 재기동 — 꼬리가 되살아나면 안 된다
        std::string v;
        CHECK(db.Has("real"));
        CHECK(db.Get("later", v)); CHECK_EQ(v, std::string("yes"));
        CHECK(!db.Has("ghost"));
        CHECK_EQ(db.Size(), (size_t)2);
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
    TestReplayDropsCompleteUncommittedTail();
    TestReplayMixedCommittedAndUncommitted();
    TestUncommittedTailNeverResurfaces();
    TestManyKeys();
}

TEST_MAIN("db_kv")
