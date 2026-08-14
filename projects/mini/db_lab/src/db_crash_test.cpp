// db_crash_test — 크래시 주입 일관성 검사 (db_lab)
//
// 시나리오: 계좌 N개 사이의 이체 트랜잭션(차감+지급 PUT 2개)을 로그에 쌓고,
// 주입 축 두 가지로 저장 장치 장애를 재현한다.
//   축 A — 절단: "랜덤 시점 중단"을 로그 파일의 임의 바이트 절단으로 재현한다.
//   축 B — 바이트 손상: 로그 *중간* 임의 오프셋 1바이트를 뒤집는다. 손상 프레임 뒤에
//          멀쩡한 COMMIT 이 남아 있으므로, 재동기화하는 구현이면 여기서 드러난다.
// 복구 후 불변식을 검사한다:
//   (1) 보존: 모든 계좌 잔액의 합 == 초기 총액 (이체는 합을 바꾸지 않는다)
//   (2) 원자성: 적용된 트랜잭션 수 <= 기록 시도 수, 부분 적용 없음
//   (3) 접두사: 복구된 트랜잭션 수 == 유효 경계 이하로 끝나는 COMMIT 프레임 수
//       (축 A 는 절단 길이, 축 B 는 손상 프레임의 시작 오프셋이 그 경계다)
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "wal_kv.hpp"

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

// 절단 길이 cutAt 에서 복구가 되살려야 하는 트랜잭션 수 — 완전히 포함된 COMMIT 프레임 수.
static uint64_t ExpectedCommits(const std::vector<long long>& commit_ends, long long cutAt) {
    uint64_t n = 0;
    for (long long end : commit_ends) {
        if (end > cutAt) break;   // commit_ends 는 오름차순
        ++n;
    }
    return n;
}

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

// 온전한 로그의 프레임 시작 오프셋 목록. 손상 바이트가 '어느 프레임에 속하는지'를
// 알아야 복구가 멈춰야 할 지점을 계산할 수 있다.
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

// 복구본의 불변식 검사. 실패 시 false.
static bool CheckInvariants(const std::unordered_map<std::string, std::string>& idx,
                            uint64_t committed, uint64_t expected, std::string& why) {
    // (3) 복구된 트랜잭션 수는 절단 길이가 허용하는 접두사와 정확히 같아야 한다.
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
