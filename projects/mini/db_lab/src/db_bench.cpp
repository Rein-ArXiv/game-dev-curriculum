// db_bench — WAL KV 의 내구성-성능 트레이드오프 측정 (db_lab)
//
// 측정 1: 커밋 지연 — fsync ON/OFF × 트랜잭션 크기(op 수) 스윕.
// 측정 2: 복구(재생) 시간 — 로그 크기(커밋 수)별 Replay 소요.
// 출력은 표 형태의 텍스트 — 글(db 시리즈)의 주장에 수치 근거를 댄다.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "wal_kv.hpp"

using Clock = std::chrono::steady_clock;
static double Ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static void RemoveFile(const std::string& p) { std::remove(p.c_str()); }

// 벤치는 실패한 커밋의 지연을 성공으로 집계하면 안 된다 — 즉시 중단한다.
static void Die(const char* what) {
    std::fprintf(stderr, "db_bench: %s\n", what);
    std::exit(1);
}

// 커밋 지연: txn_ops 개의 PUT 을 가진 트랜잭션 n_txns 회 — 평균/최대 커밋 ms
static void BenchCommit(bool fsync_on, int txn_ops, int n_txns) {
    const std::string path = "bench_commit.wal";
    RemoveFile(path);
    walkv::WalKv db;
    if (!db.Open(path, {.fsync_on_commit = fsync_on})) Die("open failed");

    double total = 0, worst = 0;
    for (int t = 0; t < n_txns; ++t) {
        for (int i = 0; i < txn_ops; ++i)
            db.TxnPut("acc:" + std::to_string((t * txn_ops + i) % 1000),
                      std::string(32, 'v'));
        auto a = Clock::now();
        const bool committed = db.Commit();
        auto b = Clock::now();
        if (!committed) Die("commit failed");
        double ms = Ms(a, b);
        total += ms;
        if (ms > worst) worst = ms;
    }
    db.Close();
    std::printf("| %-5s | %7d | %8d | %10.4f | %9.3f | %12.0f |\n",
                fsync_on ? "ON" : "OFF", txn_ops, n_txns,
                total / n_txns, worst, n_txns / (total / 1000.0));
    RemoveFile(path);
}

// 복구 시간: n_txns 커밋이 쌓인 로그의 Replay ms
static void BenchReplay(int n_txns) {
    const std::string path = "bench_replay.wal";
    RemoveFile(path);
    {
        walkv::WalKv db;
        if (!db.Open(path, {.fsync_on_commit = false})) Die("open failed");  // 로그 생성은 빠르게
        for (int t = 0; t < n_txns; ++t) {
            db.TxnPut("k:" + std::to_string(t % 5000), std::string(32, 'v'));
            if (!db.Commit()) Die("commit failed");
        }
        db.Close();
    }
    std::unordered_map<std::string, std::string> idx;
    walkv::WalKv::RecoverStats rs;
    auto a = Clock::now();
    walkv::WalKv::Replay(path, idx, &rs);
    auto b = Clock::now();
    // 재생이 커밋을 흘리면 아래 ms 는 '무엇을 재생한 시간'인지 알 수 없는 수치가 된다.
    if (rs.committed_txns != uint64_t(n_txns)) Die("replay lost commits");
    std::printf("| %8d | %10.2f | %8llu | %8zu |\n", n_txns, Ms(a, b),
                (unsigned long long)rs.committed_txns, idx.size());
    RemoveFile(path);
}

// `--smoke` 는 표본을 줄인 축소 실행이다. 측정값으로는 쓸 수 없지만,
// fsync ON 커밋 경로와 Die() 검사를 ctest 안에서 매번 지나가게 하는 것이 목적이다.
int main(int argc, char** argv) {
    const bool smoke = (argc > 1 && std::string(argv[1]) == "--smoke");
    const std::vector<int> op_sweep =
        smoke ? std::vector<int>{1, 4} : std::vector<int>{1, 4, 16, 64};
    const std::vector<int> replay_sweep =
        smoke ? std::vector<int>{200, 1000} : std::vector<int>{1000, 10000, 50000};

    if (smoke) std::printf("(smoke 모드 — 표본 축소, 측정값 아님)\n");
    std::printf("== db_lab bench 1: 커밋 지연 — fsync x 트랜잭션 크기 ==\n");
    std::printf("| fsync | txn_ops | txns     | avg ms     | max ms    | commits/sec  |\n");
    std::printf("|-------|---------|----------|------------|-----------|--------------|\n");
    for (bool fs : {false, true})
        for (int ops : op_sweep)
            BenchCommit(fs, ops, fs ? (smoke ? 20 : 200) : (smoke ? 200 : 2000));

    std::printf("\n== db_lab bench 2: 복구(재생) 시간 — 로그 크기별 ==\n");
    std::printf("| txns     | replay ms  | committed| keys   |\n");
    std::printf("|----------|------------|----------|--------|\n");
    for (int n : replay_sweep) BenchReplay(n);

    std::printf("\n해석 가이드: fsync ON 의 avg ms 가 곧 '내구성의 가격'이고, txn_ops 를\n"
                "키워도 커밋당 fsync 는 1회라 ops 당 비용은 떨어진다(그룹화의 원리).\n"
                "replay 는 로그 길이에 선형 — 체크포인트 없는 설계의 재시작 비용이다.\n");
    return 0;
}
