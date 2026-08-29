## 들어가며

마지막 단계는 “대충 잘 복구된다”를 넘어 파일 전체 범위의 여러 장애 지점을 체계적으로 검사한다. 정상 WAL을 여러 바이트 위치에서 자르거나 한 바이트를 뒤집어 각각 복구하고, 결과가 커밋된 트랜잭션의 완전한 접두사인지 확인한다.

---

## 목차
1. [크래시 주입 모델](#1-크래시-주입-모델)
2. [경제 불변식](#2-경제-불변식)
3. [전 구간 스윕](#3-전-구간-스윕)
4. [단위 테스트 묶음](#4-단위-테스트-묶음)
5. [완료 기준](#5-완료-기준)

---

## 1. 크래시 주입 모델

실제 전원을 수백 번 끄는 대신 두 가지 축으로 장애를 모사한다. 축 A는 정상 WAL의 파일 전체 범위에서 절단 위치를 샘플링해 torn tail을 만든다.

```text
full.wal[0:sample_0]
full.wal[0:sample_1]
...
full.wal[0:N]
```

축 B는 절단하지 않은 완전한 복사본에서 임의 오프셋 한 바이트를 뒤집는다.

```text
full.wal 사본, 오프셋 sample_0 의 바이트 ^= 0xFF
full.wal 사본, 오프셋 sample_1 의 바이트 ^= 0xFF
...
```

두 축이 건드리는 코드 경로가 다르다. 절단만 주입하면 잘린 프레임은 길이·본문 부족 검사에서 먼저 걸러져 CRC 불일치가 아예 일어나지 않는다. 반대로 바이트를 뒤집으면 손상 프레임 뒤에 멀쩡한 COMMIT 프레임이 그대로 남는다 — 손상 지점에서 멈추지 않고 뒤를 재동기화하는 구현은 축 B에서만 드러난다. 두 축을 합쳐 Replay하면 파서·CRC·Commit 마커의 조합이 다양한 경계를 견디는지 확인할 수 있다. 모든 바이트를 검사하려면 보폭을 1로 바꿀 수 있지만, 로그 길이에 따라 실행 시간이 크게 늘어난다.

주입 도구는 원본 로그 생성과 앞부분 복사 두 가지다. 로그를 만들면서 각 COMMIT 프레임이 끝나는 파일 오프셋을 함께 기록해 둔다 — 절단 길이에서 복구가 되살려야 할 트랜잭션 수를 계산하기 위해서다.

<!-- snippet: tests/db_crash_test.cpp -->
```cpp
static constexpr int kAccounts = 16;
static constexpr long long kInitBalance = 1000;
static constexpr int kTxns = 200;

static std::string Key(int i) { return "acc:" + std::to_string(i); }

static long long FileSize(const std::string& path);

// 이체 로그 생성 — 각 트랜잭션은 (from 차감, to 지급) PUT 2개 + COMMIT
// commit_ends 에는 각 COMMIT 프레임이 끝나는 파일 오프셋을 순서대로 남긴다.
// (Commit() 이 fflush 하므로 커밋 직후의 파일 크기가 곧 그 오프셋이다.)
static void BuildLog(const std::string& path, uint32_t seed,
                     std::vector<long long>& commit_ends) {
    std::remove(path.c_str());
    commit_ends.clear();
    walkv::WalKv db;
    db.Open(path, {.fsync_on_commit = false});
    std::vector<long long> bal(kAccounts, kInitBalance);
    // 초기 잔액도 트랜잭션으로 기록한다
    for (int i = 0; i < kAccounts; ++i) db.TxnPut(Key(i), std::to_string(bal[i]));
    db.Commit();
    commit_ends.push_back(FileSize(path));

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> acc(0, kAccounts - 1);
    std::uniform_int_distribution<long long> amt(1, 50);
    for (int t = 0; t < kTxns; ++t) {
        int from = acc(rng), to = acc(rng);
        if (from == to) to = (to + 1) % kAccounts;
        long long m = amt(rng);
        // 음수 잔액은 허용한다. 이 실험은 보존과 트랜잭션 원자성만 검사한다.
        bal[from] -= m; bal[to] += m;
        db.TxnPut(Key(from), std::to_string(bal[from]));
        db.TxnPut(Key(to),   std::to_string(bal[to]));
        db.Commit();
        commit_ends.push_back(FileSize(path));
    }
    db.Close();
}

// ... ExpectedCommits — 2절

static long long FileSize(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return -1;
    std::fseek(f, 0, SEEK_END);
    long long n = std::ftell(f);
    std::fclose(f);
    return n;
}

static void TruncateTo(const std::string& src, const std::string& dst, long long n) {
    FILE* in = std::fopen(src.c_str(), "rb");
    FILE* out = std::fopen(dst.c_str(), "wb");
    std::vector<char> buf(static_cast<size_t>(n));
    size_t rd = std::fread(buf.data(), 1, size_t(n), in);
    std::fwrite(buf.data(), 1, rd, out);
    std::fclose(in); std::fclose(out);
}
```

축 B의 도구는 세 개다. 손상 바이트가 어느 프레임에 속하는지 알아야 복구가 멈춰야 할 지점을 계산할 수 있으므로, 온전한 로그를 한 번 훑어 프레임 시작 오프셋을 모아 둔다. 그리고 사본을 매번 다시 만들지 않도록 한 바이트만 뒤집었다 되돌린다.

<!-- snippet: tests/db_crash_test.cpp -->
```cpp
// 온전한 로그의 프레임 시작 오프셋 목록.
static std::vector<long long> FrameStarts(const std::string& path) {
    std::vector<long long> starts;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return starts;
    std::fseek(f, 0, SEEK_END);
    const long long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(size_t(size < 0 ? 0 : size));
    const size_t rd = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    buf.resize(rd);
    size_t off = 0;
    while (off + 8 <= buf.size()) {
        const uint32_t len = walkv::ReadU32Le(buf.data() + off);
        if (len == 0 || off + 8 + size_t(len) > buf.size()) break;
        starts.push_back((long long)off);
        off += 8 + size_t(len);
    }
    return starts;
}

// 오프셋 p 를 담고 있는 프레임의 시작. 없으면 -1.
static long long FrameStartContaining(const std::vector<long long>& starts, long long p) {
    long long found = -1;
    for (long long s : starts) {          // starts 는 오름차순
        if (s > p) break;
        found = s;
    }
    return found;
}

// path 의 offset 바이트를 ^0xFF 한다. 되돌릴 수 있도록 원래 값을 반환한다.
static int FlipByte(const std::string& path, long long offset) {
    FILE* f = std::fopen(path.c_str(), "rb+");
    if (!f) return -1;
    std::fseek(f, (long)offset, SEEK_SET);
    const int c = std::fgetc(f);
    std::fseek(f, (long)offset, SEEK_SET);
    std::fputc(c ^ 0xFF, f);
    std::fclose(f);
    return c;
}

static void RestoreByte(const std::string& path, long long offset, int original) {
    FILE* f = std::fopen(path.c_str(), "rb+");
    if (!f) return;
    std::fseek(f, (long)offset, SEEK_SET);
    std::fputc(original, f);
    std::fclose(f);
}
```

## 2. 경제 불변식

테스트 워크로드는 계정 사이 이체다. 한 트랜잭션에서 출금 계정과 입금 계정을 함께 갱신한다.

검사할 불변식:

- 전체 잔액 합은 초기 합과 같다.
- 한 이체의 차감만 또는 입금만 적용되지 않는다.
- 복구된 트랜잭션 수가 가능한 접두사와 맞는다.

세 번째 불변식은 `BuildLog`가 남긴 COMMIT 오프셋 배열로 계산한다. 유효 경계 이하에서 끝나는 COMMIT 프레임 수가 곧 복구가 되살려야 할 트랜잭션 수이고, 합 보존만으로는 잡히지 않는 '한 트랜잭션 더 살거나 덜 사는' 오류를 직접 잡는다. 유효 경계는 축 A에서는 절단 길이이고, 축 B에서는 손상된 바이트를 담은 프레임의 시작 오프셋이다 — 복구는 그 프레임에 닿는 순간 멈춰야 하므로 그 앞까지의 COMMIT만 살아야 한다.

<!-- snippet: tests/db_crash_test.cpp#ExpectedCommits -->
```cpp
// 유효 경계 cutAt 이하에서 끝나는 COMMIT 프레임 수 — 복구가 되살려야 하는 트랜잭션 수.
static uint64_t ExpectedCommits(const std::vector<long long>& commit_ends, long long cutAt) {
    uint64_t n = 0;
    for (long long end : commit_ends) {
        if (end > cutAt) break;   // commit_ends 는 오름차순
        ++n;
    }
    return n;
}
```

불변식 셋을 실제로 단정하는 것은 이 함수다. 스윕의 모든 시행이 여기를 지나가고, 어긋난 값은 `why`에 담겨 실패 줄에 그대로 찍힌다.

<!-- snippet: tests/db_crash_test.cpp#CheckInvariants -->
```cpp
// 복구본의 불변식 검사. 실패 시 false.
static bool CheckInvariants(const std::unordered_map<std::string, std::string>& idx,
                            uint64_t committed, uint64_t expected, std::string& why) {
    // (3) 복구된 트랜잭션 수는 유효 경계가 허용하는 접두사와 정확히 같아야 한다.
    if (committed != expected) {
        why = "committed=" + std::to_string(committed) +
              " expected=" + std::to_string(expected);
        return false;
    }
    if (committed == 0) return true;  // 초기화 커밋조차 못 미침 — 빈 스토어, 일관
    // 초기화 커밋 이후라면 모든 계좌가 존재해야 한다 (부분 적용 검출)
    long long sum = 0;
    for (int i = 0; i < kAccounts; ++i) {
        auto it = idx.find(Key(i));
        if (it == idx.end()) { why = "missing " + Key(i); return false; }
        sum += std::stoll(it->second);
    }
    if (sum != (long long)kAccounts * kInitBalance) {
        why = "sum=" + std::to_string(sum);
        return false;
    }
    return true;
}
```

## 3. 전 구간 스윕

몇 개 임의 위치만 자르면 Commit 경계 바로 앞뒤 같은 중요한 경우를 놓칠 수 있다. 이 구현은 파일 전체 범위를 일정 보폭으로 샘플링하고 완전한 파일 끝도 별도로 검사한다. 축 B는 같은 방식으로 손상 오프셋을 샘플링한다. 더 강한 검사가 필요하면 프레임 시작·끝과 Commit 전후를 명시적으로 추가하거나 보폭을 1로 줄인다. 여러 난수 시드로 이체 순서를 바꾸면 데이터 패턴 의존 버그도 줄인다.

<!-- snippet: tests/db_crash_test.cpp#main -->
```cpp
int main() {
    const std::string full = "crash_full.wal", cut = "crash_cut.wal";
    int failures = 0, trials = 0;
    int flip_failures = 0, flip_trials = 0;
    std::vector<long long> commit_ends;
    for (uint32_t seed : {1u, 7u, 42u}) {
        BuildLog(full, seed, commit_ends);
        long long size = FileSize(full);
        // 전체 파일 범위에서 약 211개 지점을 샘플링한다.
        for (long long cutAt = 0; cutAt <= size; cutAt += (size / 211) + 1) {
            TruncateTo(full, cut, cutAt);
            std::unordered_map<std::string, std::string> idx;
            walkv::WalKv::RecoverStats rs;
            walkv::WalKv::Replay(cut, idx, &rs);
            std::string why;
            ++trials;
            if (!CheckInvariants(idx, rs.committed_txns,
                                 ExpectedCommits(commit_ends, cutAt), why)) {
                ++failures;
                std::printf("FAIL seed=%u cut=%lld committed=%llu (%s)\n",
                            seed, cutAt, (unsigned long long)rs.committed_txns, why.c_str());
            }
        }
        // 보폭이 정확히 파일 끝에 닿지 않을 수 있으므로 완전한 로그도 확인한다.
        TruncateTo(full, cut, size);
        std::unordered_map<std::string, std::string> idx;
        walkv::WalKv::RecoverStats rs;
        walkv::WalKv::Replay(cut, idx, &rs);
        std::string why;
        ++trials;
        if (!CheckInvariants(idx, rs.committed_txns,
                             ExpectedCommits(commit_ends, size), why)) {
            ++failures;
            std::printf("FAIL seed=%u cut=%lld committed=%llu (%s)\n",
                        seed, size, (unsigned long long)rs.committed_txns, why.c_str());
        }

        // ---- 축 B: 바이트 손상 주입 ----
        // 온전한 로그를 복사해 두고, 매번 한 바이트만 뒤집었다 되돌린다.
        // 손상 프레임을 만나면 복구는 그 자리에서 멈춰야 한다 — 뒤에 유효한 COMMIT 이
        // 남아 있어도 재동기화하면 안 된다. 그래서 기대값의 경계는 '손상 프레임의 시작'이다.
        const std::vector<long long> starts = FrameStarts(full);
        TruncateTo(full, cut, size);                  // 절단 없는 완전 복사본
        for (long long p = 0; p < size; p += (size / 97) + 1) {
            const long long fstart = FrameStartContaining(starts, p);
            if (fstart < 0) continue;                 // 프레임 밖(있을 수 없음) — 건너뛴다
            const int original = FlipByte(cut, p);
            if (original < 0) continue;
            std::unordered_map<std::string, std::string> fidx;
            walkv::WalKv::RecoverStats frs;
            walkv::WalKv::Replay(cut, fidx, &frs);
            RestoreByte(cut, p, original);
            std::string fwhy;
            ++flip_trials;
            if (!CheckInvariants(fidx, frs.committed_txns,
                                 ExpectedCommits(commit_ends, fstart), fwhy)) {
                ++flip_failures;
                std::printf("FAIL(flip) seed=%u at=%lld frame=%lld committed=%llu (%s)\n",
                            seed, p, fstart, (unsigned long long)frs.committed_txns,
                            fwhy.c_str());
            }
        }
    }
    std::remove(full.c_str()); std::remove(cut.c_str());
    std::printf("crash sweep: %d trials, %d failures\n", trials, failures);
    std::printf("flip sweep : %d trials, %d failures\n", flip_trials, flip_failures);
    if (failures == 0 && flip_failures == 0)
        std::printf("OK — 샘플링한 절단·손상 지점에서 '완전한 접두사'로만 복구된다 (원자성+보존 불변식 유지)\n");
    return (failures == 0 && flip_failures == 0) ? 0 : 1;
}
```

축 B가 실제로 무엇을 잡는지는 헤더에 결함을 넣어 보면 즉시 드러난다. CRC 불일치에서 `break` 대신 `continue` 하도록 바꾸면 절단 스윕은 633 trial 모두 통과하지만 손상 스윕은 291 trial 중 225개가 실패한다.

```text
FAIL(flip) seed=1 at=390 frame=368 committed=201 (committed=201 expected=0)
FAIL(flip) seed=42 at=12480 frame=12465 committed=201 (committed=201 expected=199)
crash sweep: 633 trials, 0 failures
flip sweep : 291 trials, 225 failures
```

## 4. 단위 테스트 묶음

`db_tests`는 더 작은 실패를 빠르게 진단한다. 검사 항목과 각 항목이 겨냥하는 구현 결함, 그리고 그 검사를 추가한 step은 다음과 같이 짝을 이룬다.

| 검사 | 겨냥하는 결함 | 추가한 step |
|---|---|---|
| `frame_codec_bytes` | 엔디언·payload 배치·CRC 다항식이 바뀌는 것 | step01 |
| `crc_mismatch_stops_replay` | 마지막 프레임의 찢긴 쓰기를 넘기는 것 | step01 |
| `garbage_length_rejected` | 길이 필드를 믿고 그만큼 버퍼를 잡는 것 | step01 |
| `oversized_frame_rejected` | 프레임 길이 상한 가드의 소실 | step01 |
| `commit_visibility` | 커밋 전 변경이 인덱스에 새는 것 | step02 |
| `reopen_keeps_committed` | 커밋분 유실, 롤백분 부활 | step02 |
| `uncommitted_tail_discarded` | COMMIT 없는 op를 적용하는 것 | step02 |
| `midlog_corruption_drops_tail` | 손상 지점에서 멈추지 않고 재동기화하는 것 | step02 |
| `del_survives_restart` | Del 디코드 실패, 삭제된 키의 부활 | step02 |
| `fsync_on_commit_durable` | fsync 실패를 성공으로 보고하는 것 | step02 |

소속은 파일 맨 아래 `kCases` 표가 정하고, 그 표를 `--step=NN` 필터가 읽는다. 덕분에 step01·step02 독자는 자기 step이 추가한 것만 이름과 함께 돌려 볼 수 있고, 이 step의 독자는 `ctest`로 전부 돌린다.

`db_crash_test`가 표본 장애에서 통합 불변식을 검사하고, 단위 테스트가 실패 원인을 좁힌다.

손상 주입 테스트는 '어느 프레임의 어느 바이트'인지를 정확히 겨냥해야 하므로, 오프셋을 하드코딩하지 않고 파일 앞에서부터 프레임을 파싱해 계산한다.

<!-- snippet: tests/db_tests.cpp -->
```cpp
// db_tests — WalKv 단위 테스트 (ctest 대상)
//
// 인자 없이 돌리면 전부 실행한다. `--step=NN` 을 주면 그 step 이 추가한 검사만
// 골라 이름과 함께 찍는다 — 파일 맨 아래 kCases 표가 소속을 정한다.
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

// ... step01 의 frame_codec_bytes — step01 문서에서 인용한다

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
```

위 `TestCorruptionDetected`는 파일 *마지막* 프레임의 CRC 필드만 뒤집는다. 손상 뒤에 유효한 COMMIT이 하나도 없으므로 '재동기화 금지' 규칙 자체는 건드리지 못한다. 그 규칙을 겨냥하는 것이 아래 첫 번째 테스트다. 나머지 넷은 Del 프레임의 디스크 왕복, fsync ON 커밋, 그리고 길이 필드 가드를 각각 맡는다.

<!-- snippet: tests/db_tests.cpp -->
```cpp
// 로그 '중간'의 손상: 손상 프레임 뒤에 유효한 COMMIT 이 더 있어도 꼬리 전체를 버린다.
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
        // 37바이트짜리 로그의 재생은 마이크로초 규모다. 2초를 넘겼다면 len 가드가
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

// ...

struct Case {
    const char* step;
    const char* name;
    void (*fn)();
};

static const Case kCases[] = {
    {"01", "frame_codec_bytes",           TestFrameCodecBytes},
    {"01", "crc_mismatch_stops_replay",   TestCorruptionDetected},
    {"01", "garbage_length_rejected",     TestGarbageLengthRejected},
    {"01", "oversized_frame_rejected",    TestOversizedFrameRejected},
    {"02", "commit_visibility",           TestBasic},
    {"02", "reopen_keeps_committed",      TestReopen},
    {"02", "uncommitted_tail_discarded",  TestUncommittedTailDiscarded},
    {"02", "midlog_corruption_drops_tail",TestCorruptionMidLogDiscardsTail},
    {"02", "del_survives_restart",        TestDelSurvivesRestart},
    {"02", "fsync_on_commit_durable",     TestFsyncOnCommitDurable},
};

int main(int argc, char** argv) {
    std::string step;
    if (argc > 1) {
        const std::string arg = argv[1];
        if (arg.rfind("--step=", 0) != 0) {
            std::printf("usage: db_tests [--step=NN]\n");
            return 2;
        }
        step = arg.substr(7);
    }

    int selected = 0;
    for (const Case& c : kCases) {
        if (!step.empty() && step != c.step) continue;
        ++selected;
        const int before = g_failed;
        c.fn();
        if (!step.empty())
            std::printf("  %-4s %s\n", (g_failed == before) ? "ok" : "FAIL", c.name);
    }
    std::remove(kPath.c_str());

    if (!step.empty() && selected == 0) {
        std::printf("db_tests: no case belongs to step%s\n", step.c_str());
        return 2;
    }
    if (g_failed != 0) {
        std::printf("db_tests: %d check(s) failed\n", g_failed);
        return 1;
    }
    if (step.empty()) std::printf("db_tests: all passed\n");
    else std::printf("db_tests[step%s]: %d/%d passed\n", step.c_str(), selected, selected);
    return 0;
}
```

`TestGarbageLengthRejected`의 길이 필드는 0xFFFFFFFF다. 길이 상한 가드가 없으면 4GB 버퍼 할당이 일어나는데, 메모리가 넉넉한 기계에서는 그 할당이 그냥 성공해 버려 통계값(`committed_txns`, `discarded_bytes`)만으로는 차이가 나지 않는다. 그래서 시간 상한을 함께 단정하고, 결정적인 판정은 `TestOversizedFrameRejected`가 맡는다 — 이쪽은 CRC도 맞고 본문도 실제로 파일에 있는 상한 초과 프레임이라, 가드가 사라지면 뒤따르는 COMMIT까지 살아나 `committed_txns`가 2가 된다.

`kMaxFrame`은 이 경계를 테스트가 정확히 겨냥할 수 있도록 `WalKv`의 public 상수로 노출되어 있다.

## 5. 완료 기준

이 step이 추가한 것은 크래시 주입 스윕이므로, 이 step의 실행 단위는 `db_crash_test`다. 직접 돌리면 두 축의 시행 횟수가 그대로 나온다.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/Release/db_crash_test
```

관측 결과 — 시행 수(633·291)는 로그 길이와 보폭에서 정해지므로 결정적이다. 실패한 지점이 있으면 `FAIL`/`FAIL(flip)` 줄이 시드·오프셋·기대값과 함께 먼저 찍히고 종료 코드가 1이 된다.

```text
crash sweep: 633 trials, 0 failures
flip sweep : 291 trials, 0 failures
OK — 샘플링한 절단·손상 지점에서 '완전한 접두사'로만 복구된다 (원자성+보존 불변식 유지)
```

lab 전체의 완료 기준은 ctest 다섯 대상이다.

```bash
ctest --test-dir build -C Release --output-on-failure
```

```text
1/5 Test #1: db_tests .........................   Passed    0.18 sec
2/5 Test #2: db_crash_test ....................   Passed   18.33 sec
3/5 Test #3: db_bench_smoke ...................   Passed    0.21 sec
4/5 Test #4: db_tests_step01 ..................   Passed    0.10 sec
5/5 Test #5: db_tests_step02 ..................   Passed    0.08 sec

100% tests passed out of 5
```

| ctest 이름 | 무엇을 돌리나 | 어느 step의 실행 단위인가 |
|---|---|---|
| `db_tests` | 단위 테스트 열 건 전부 | — (전체 실행) |
| `db_tests_step01` | `db_tests --step=01` — 프레임 형식·CRC·파싱 경계 | step01 |
| `db_tests_step02` | `db_tests --step=02` — 트랜잭션 커밋·복구 | step02 |
| `db_bench_smoke` | `db_bench --smoke` — 벤치의 `Die` 검사 | step03 |
| `db_crash_test` | 절단·손상 스윕과 경제 불변식 | step04 |

`db_bench_smoke`는 표본을 줄인 축소 실행이라 측정값으로는 못 쓰지만, fsync ON 커밋 경로와 벤치의 실패 검사가 매 ctest 실행에서 지나가게 하는 값이 있다 — 커밋이나 재생이 실패를 보고하면 벤치는 지연 표를 찍지 않고 즉시 비정상 종료한다. 수치를 볼 때는 `./build/Release/db_bench`를 인자 없이 직접 돌린다.

완료 기준은 `0 failures` 한 줄만이 아니다. 어떤 불변식을 어떤 장애 모델에서 검사했는지 설명할 수 있어야 한다. 이 lab이 검증하는 범위는 단일 프로세스·단일 WAL의 잘린 꼬리 복구이며, 실제 전원 손실의 저장 장치 동작·동시성 제어·복제 합의까지 검증하지는 않는다.

---

## 결론

로그의 파일 전체 범위에서 표본 절단 지점과 표본 손상 지점을 순회하는 크래시 주입과 단위 테스트를 결합해, 복구 상태가 완전히 커밋된 트랜잭션의 접두사라는 불변식을 검사한다. 잔액 보존 같은 도메인 불변식도 저장 형식 검증과 분리해 확인한다.

테스트가 실제로 이빨을 가졌는지는 결함을 일부러 넣어 확인했다. CRC 불일치 후 재동기화, Del 프레임 디코드 실패, fsync 상시 실패, 프레임 길이 상한 가드 제거 — 넷 모두 위 검사에서 빨갛게 죽는다.

검증 범위는 프로세스 종료로 모사한 잘린 꼬리와 단일 바이트 손상까지다. 디스크가 쓰기 순서를 위반하거나 섹터 일부만 기록하는 상황, 다중 writer, 복제 노드 간 합의는 이 테스트의 보장에 포함되지 않는다.
