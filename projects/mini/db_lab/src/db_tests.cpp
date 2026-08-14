// db_tests — WalKv 단위 테스트 (ctest 대상)
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "wal_kv.hpp"

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); ++g_failed; } } while (0)

static const std::string kPath = "test_walkv.wal";
static void Reset() { std::remove(kPath.c_str()); }

// ---------- 로그 파일을 바이트 단위로 다루는 도우미 ----------
// 손상 주입 테스트는 '어느 프레임의 어느 바이트'인지를 정확히 겨냥해야 한다.
// 파일 앞에서부터 프레임을 파싱해 오프셋을 계산한다 (하드코딩 금지).

static std::vector<uint8_t> ReadAll(const std::string& path) {
    std::vector<uint8_t> buf;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return buf;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        buf.resize(size_t(n));
        if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) buf.clear();
    }
    std::fclose(f);
    return buf;
}

struct FrameSpan {
    size_t start = 0;      // 프레임(헤더 포함) 시작 오프셋
    size_t payload = 0;    // payload 시작 오프셋 (= start + 8)
    uint32_t len = 0;      // payload 길이
    walkv::Op op = walkv::Op::Commit;
};

// 온전한 로그를 전제로 프레임 경계를 훑는다. 깨진 지점에서 멈춘다.
static std::vector<FrameSpan> WalkFrames(const std::vector<uint8_t>& buf) {
    std::vector<FrameSpan> out;
    size_t off = 0;
    while (off + 8 <= buf.size()) {
        const uint32_t len = walkv::ReadU32Le(buf.data() + off);
        if (len == 0 || off + 8 + size_t(len) > buf.size()) break;
        FrameSpan fs;
        fs.start = off;
        fs.payload = off + 8;
        fs.len = len;
        fs.op = walkv::Op(buf[off + 8]);
        out.push_back(fs);
        off += 8 + size_t(len);
    }
    return out;
}

static void FlipByteAt(const std::string& path, long offset) {
    FILE* f = std::fopen(path.c_str(), "rb+");
    if (!f) { std::printf("FlipByteAt: cannot open %s\n", path.c_str()); ++g_failed; return; }
    std::fseek(f, offset, SEEK_SET);
    const int c = std::fgetc(f);
    std::fseek(f, offset, SEEK_SET);
    std::fputc(c ^ 0xFF, f);
    std::fclose(f);
}

static void AppendRaw(const std::string& path, const std::vector<uint8_t>& bytes) {
    FILE* f = std::fopen(path.c_str(), "ab");
    if (!f) { std::printf("AppendRaw: cannot open %s\n", path.c_str()); ++g_failed; return; }
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
}

// 프레임 하나를 [len][crc][payload] 로 직렬화한다 (WriteFrame 과 같은 형식).
static std::vector<uint8_t> FrameBytes(const walkv::Frame& fr) {
    std::vector<uint8_t> payload = walkv::EncodePayload(fr);
    std::vector<uint8_t> out;
    walkv::AppendU32(out, uint32_t(payload.size()));
    walkv::AppendU32(out, walkv::Crc32(payload.data(), payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

// 기본: put/get/del, 커밋 전 비가시
static void TestBasic() {
    Reset();
    walkv::WalKv db;
    CHECK(db.Open(kPath, {.fsync_on_commit = false}));
    db.TxnPut("a", "1");
    CHECK(!db.Get("a").has_value());          // 커밋 전에는 보이지 않는다
    CHECK(db.Commit());
    CHECK(db.Get("a").value_or("") == "1");
    db.TxnPut("a", "2");
    db.TxnDel("a");
    CHECK(db.Commit());
    CHECK(!db.Get("a").has_value());          // 같은 txn 의 put 후 del
    db.Close();
}

// 재시작: 커밋분은 살아 있고, 롤백분은 없다
static void TestReopen() {
    Reset();
    {
        walkv::WalKv db;
        db.Open(kPath, {.fsync_on_commit = false});
        db.TxnPut("x", "10"); db.Commit();
        db.TxnPut("y", "20"); db.Rollback();   // 기록 자체가 안 됨
        db.Close();
    }
    {
        walkv::WalKv db;
        CHECK(db.Open(kPath, {.fsync_on_commit = false}));
        CHECK(db.RecoveredTxns() == 1);
        CHECK(db.Get("x").value_or("") == "10");
        CHECK(!db.Get("y").has_value());
        db.Close();
    }
}

// 미완 트랜잭션: COMMIT 없는 꼬리 op 는 복구가 버린다
static void TestUncommittedTailDiscarded() {
    Reset();
    {
        walkv::WalKv db;
        db.Open(kPath, {.fsync_on_commit = false});
        db.TxnPut("k", "committed"); db.Commit();
        db.Close();
    }
    {   // COMMIT 프레임 없이 PUT 프레임만 손으로 덧붙인다 (크래시 재현)
        walkv::Frame fr{walkv::Op::Put, "k", "torn"};
        auto payload = walkv::EncodePayload(fr);
        std::vector<uint8_t> header;
        walkv::AppendU32(header, uint32_t(payload.size()));
        walkv::AppendU32(header, walkv::Crc32(payload.data(), payload.size()));
        FILE* f = std::fopen(kPath.c_str(), "ab");
        std::fwrite(header.data(), 1, header.size(), f);
        std::fwrite(payload.data(), 1, payload.size(), f);
        std::fclose(f);
    }
    {
        walkv::WalKv db;
        CHECK(db.Open(kPath, {.fsync_on_commit = false}));
        CHECK(db.Get("k").value_or("") == "committed");   // torn 값이 아니어야 한다
        CHECK(db.DiscardedTailBytes() > 0);
        db.TxnPut("after-recovery", "visible");
        CHECK(db.Commit());
        db.Close();
    }
    {
        walkv::WalKv db;
        CHECK(db.Open(kPath, {.fsync_on_commit = false}));
        CHECK(db.Get("k").value_or("") == "committed");
        CHECK(db.Get("after-recovery").value_or("") == "visible");
        db.Close();
    }
}

// CRC 손상: 깨진 프레임부터 뒤를 전부 무시한다
static void TestCorruptionDetected() {
    Reset();
    {
        walkv::WalKv db;
        db.Open(kPath, {.fsync_on_commit = false});
        db.TxnPut("a", "1"); db.Commit();
        db.TxnPut("b", "2"); db.Commit();
        db.Close();
    }
    {   // 두 번째 트랜잭션 영역의 바이트 하나를 뒤집는다
        FILE* f = std::fopen(kPath.c_str(), "rb+");
        std::fseek(f, -3, SEEK_END);
        int c = std::fgetc(f);
        std::fseek(f, -3, SEEK_END);
        std::fputc(c ^ 0xFF, f);
        std::fclose(f);
    }
    {
        std::unordered_map<std::string, std::string> idx;
        walkv::WalKv::RecoverStats rs;
        CHECK(walkv::WalKv::Replay(kPath, idx, &rs));
        CHECK(rs.committed_txns == 1);                    // 첫 txn 만 살아남는다
        CHECK(idx.count("a") == 1);
        CHECK(idx.count("b") == 0);
        CHECK(rs.discarded_bytes > 0);
    }
}

// 로그 '중간'의 손상: 손상 프레임 뒤에 유효한 COMMIT 이 더 있어도 꼬리 전체를 버린다.
// (TestCorruptionDetected 는 마지막 프레임만 건드리므로 '재동기화 금지' 규칙을 못 건드린다.)
static void TestCorruptionMidLogDiscardsTail() {
    Reset();
    {
        walkv::WalKv db;
        CHECK(db.Open(kPath, {.fsync_on_commit = false}));
        db.TxnPut("a", "1"); CHECK(db.Commit());
        db.TxnPut("b", "2"); CHECK(db.Commit());
        db.TxnPut("c", "3"); CHECK(db.Commit());
        db.Close();
    }
    // 두 번째 PUT 프레임의 payload 안쪽 바이트 하나를 뒤집는다.
    // 손상 뒤에는 (COMMIT#2, PUT c, COMMIT#3) 이 멀쩡히 남아 있다 — 재동기화하면 살아난다.
    {
        const std::vector<FrameSpan> frames = WalkFrames(ReadAll(kPath));
        CHECK(frames.size() == 6);                       // put/commit × 3
        int puts = 0;
        long target = -1;
        for (const FrameSpan& fs : frames) {
            if (fs.op != walkv::Op::Put) continue;
            if (++puts == 2) { target = long(fs.payload + fs.len - 1); break; }  // value 의 마지막 바이트
        }
        CHECK(target > 0);
        if (target > 0) FlipByteAt(kPath, target);
    }
    {
        std::unordered_map<std::string, std::string> idx;
        walkv::WalKv::RecoverStats rs;
        CHECK(walkv::WalKv::Replay(kPath, idx, &rs));
        CHECK(rs.committed_txns == 1);       // 손상 프레임 이후는 세지 않는다
        CHECK(idx.count("a") == 1);
        CHECK(idx.count("b") == 0);
        CHECK(idx.count("c") == 0);          // ← 재동기화(break→continue) 를 잡는 단정
        CHECK(idx.size() == 1);
        CHECK(rs.discarded_bytes > 0);
    }
}

// Del 프레임의 디스크 왕복 — 커밋된 삭제는 재시작 후에도 삭제된 상태여야 한다.
static void TestDelSurvivesRestart() {
    Reset();
    {
        walkv::WalKv db;
        CHECK(db.Open(kPath, {.fsync_on_commit = false}));
        db.TxnPut("d", "1"); CHECK(db.Commit());
        db.TxnDel("d");      CHECK(db.Commit());
        db.Close();
    }
    {   // Del 디코드가 죽으면 Replay 가 그 프레임에서 멈춰 committed==1, "d"=="1" 이 된다
        walkv::WalKv db;
        CHECK(db.Open(kPath, {.fsync_on_commit = false}));
        CHECK(db.RecoveredTxns() == 2);
        CHECK(!db.Get("d").has_value());
        CHECK(db.KeyCount() == 0);
        db.TxnPut("d", "2"); CHECK(db.Commit());   // Del 뒤의 재Put
        db.Close();
    }
    {
        walkv::WalKv db;
        CHECK(db.Open(kPath, {.fsync_on_commit = false}));
        CHECK(db.RecoveredTxns() == 3);
        CHECK(db.Get("d").value_or("") == "2");
        db.Close();
    }
}

// fsync ON 경로: 커밋이 성공을 보고해야 하고, 그 커밋은 재시작 후에도 남아야 한다.
// (ctest 시간을 감안해 커밋은 3회로 제한한다.)
static void TestFsyncOnCommitDurable() {
    Reset();
    {
        walkv::WalKv db;
        CHECK(db.Open(kPath, {.fsync_on_commit = true}));
        db.TxnPut("f", "1"); CHECK(db.Commit());   // fsync 상시 실패 변이는 여기서 잡힌다
        db.TxnPut("g", "2"); CHECK(db.Commit());
        db.TxnDel("g");      CHECK(db.Commit());
        CHECK(db.Get("f").value_or("") == "1");    // 실패한 커밋은 인덱스에 반영되지 않는다
        CHECK(!db.Get("g").has_value());
        db.Close();
    }
    {
        walkv::WalKv db;
        CHECK(db.Open(kPath, {.fsync_on_commit = true}));
        CHECK(db.RecoveredTxns() == 3);
        CHECK(db.Get("f").value_or("") == "1");
        CHECK(!db.Get("g").has_value());
        db.Close();
    }
}

// 쓰레기 길이 필드: 헤더만 있고 본문이 없는 거대 len 은 '잘림'으로 취급되어야 하며,
// 그 길이만큼 버퍼를 잡아 보려 해서도 안 된다.
static void TestGarbageLengthRejected() {
    Reset();
    {
        walkv::WalKv db;
        CHECK(db.Open(kPath, {.fsync_on_commit = false}));
        db.TxnPut("g1", "v"); CHECK(db.Commit());
        db.Close();
    }
    {   // payload 없이 헤더 8바이트만 덧붙인다 — len = 0xFFFFFFFF
        std::vector<uint8_t> header;
        walkv::AppendU32(header, 0xFFFFFFFFu);
        walkv::AppendU32(header, 0u);
        AppendRaw(kPath, header);
    }
    {
        std::unordered_map<std::string, std::string> idx;
        walkv::WalKv::RecoverStats rs;
        const auto t0 = std::chrono::steady_clock::now();
        CHECK(walkv::WalKv::Replay(kPath, idx, &rs));
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0).count();
        CHECK(rs.committed_txns == 1);
        CHECK(rs.discarded_bytes == 8);
        CHECK(idx.size() == 1);
        // 40바이트짜리 로그의 재생은 마이크로초 규모다. 2초를 넘겼다면 len 가드가
        // 없어 4GB 버퍼를 잡으려 한 것이다.
        CHECK(ms < 2000.0);
    }
}

// 길이 상한 자체의 검증: CRC 도 맞고 본문도 실제로 존재하지만 kMaxFrame 을 넘는
// 프레임은 거부되어야 한다 — 뒤따르는 COMMIT 이 있어도 살아나면 안 된다.
static void TestOversizedFrameRejected() {
    Reset();
    {
        walkv::WalKv db;
        CHECK(db.Open(kPath, {.fsync_on_commit = false}));
        db.TxnPut("small", "1"); CHECK(db.Commit());
        db.Close();
    }
    const size_t kOverPayload = size_t(walkv::WalKv::kMaxFrame) + 1;
    {   // payload 크기가 정확히 kMaxFrame+1 이 되도록 value 길이를 맞춘다
        //   payload = op(1) + [len(4)+"big"(3)] + [len(4)+value]
        const size_t value_len = kOverPayload - (1 + 4 + 3 + 4);
        const std::vector<uint8_t> big =
            FrameBytes({walkv::Op::Put, "big", std::string(value_len, 'x')});
        CHECK(big.size() == 8 + kOverPayload);
        AppendRaw(kPath, big);
        AppendRaw(kPath, FrameBytes({walkv::Op::Commit, {}, {}}));  // 유효한 COMMIT 이 뒤따른다
    }
    {
        std::unordered_map<std::string, std::string> idx;
        walkv::WalKv::RecoverStats rs;
        CHECK(walkv::WalKv::Replay(kPath, idx, &rs));
        CHECK(rs.committed_txns == 1);           // 거대 프레임 뒤의 COMMIT 은 세지 않는다
        CHECK(idx.count("big") == 0);
        CHECK(idx.size() == 1);
        CHECK(rs.discarded_bytes == (8 + kOverPayload) + 9);
    }
    Reset();
}

int main() {
    TestBasic();
    TestReopen();
    TestUncommittedTailDiscarded();
    TestCorruptionDetected();
    TestCorruptionMidLogDiscardsTail();
    TestDelSurvivesRestart();
    TestFsyncOnCommitDurable();
    TestGarbageLengthRejected();
    TestOversizedFrameRejected();
    std::remove(kPath.c_str());
    if (g_failed == 0) { std::printf("db_tests: all passed\n"); return 0; }
    std::printf("db_tests: %d check(s) failed\n", g_failed);
    return 1;
}
