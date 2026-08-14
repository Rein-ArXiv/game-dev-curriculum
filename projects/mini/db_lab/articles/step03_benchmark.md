## 들어가며

WAL 설계의 옳고 그름만 확인하면 내구성 비용을 체감하기 어렵다. 세 번째 단계는 fsync ON/OFF, 트랜잭션 크기, 로그 길이를 바꾸며 커밋과 복구 비용을 측정한다.

---

## 목차
1. [측정 1: 커밋 지연](#1-측정-1-커밋-지연)
2. [그룹화 효과](#2-그룹화-효과)
3. [측정 2: 복구 시간](#3-측정-2-복구-시간)
4. [벤치마크 함정](#4-벤치마크-함정)
5. [결과 해석](#5-결과-해석)

---

## 1. 측정 1: 커밋 지연

`BenchCommit`은 fsync 설정과 트랜잭션당 op 수를 바꾼다. 출력은 평균·최대 커밋 ms와 commits/sec를 포함한다.

실패한 커밋의 지연을 성공으로 집계하면 표 전체가 거짓이 되므로, `Commit()`의 반환값을 확인하고 실패 시 즉시 중단한다.

```cpp
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
```

fsync ON과 OFF의 차이는 저장 장치와 OS에 따라 크게 다르다. 절대값보다 같은 머신에서 설정을 바꾼 상대 차이가 중요하다.

## 2. 그룹화 효과

트랜잭션당 op가 늘어도 커밋당 fsync는 한 번이다.

$$
\text{ops/sec}=\text{commits/sec}\times\text{ops/txn}
$$

큰 트랜잭션은 op당 내구성 비용을 낮추지만, 한 번 실패할 때 잃는 작업 단위와 지연이 커진다.

## 3. 측정 2: 복구 시간

`BenchReplay`는 커밋 수가 다른 WAL을 만들고 Replay 시간을 잰다. 체크포인트가 없으므로 복구 시간은 로그 길이에 대체로 선형이다.

```cpp
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
```

재생 결과를 확인하지 않으면 벤치는 '아무것도 재생하지 못한 시간'도 빠른 복구로 보고할 수 있다. 커밋 수가 기대와 다르면 표를 찍기 전에 중단한다 — 커밋 쪽의 `Die("commit failed")`와 같은 이유다.

로그 생성에는 키가 5000개 순환하므로 커밋 수를 늘려도 인덱스 크기는 5000에서 멈춘다. 재생 시간이 늘어나는 원인이 인덱스가 아니라 로그 길이임을 분리해 보기 위한 설계다.

이 결과가 체크포인트와 로그 절단의 필요성을 보여 준다. 정상 실행 비용을 조금 지불해 재시작 비용을 제한하는 거래다.

## 4. 벤치마크 함정

- 첫 실행과 반복 실행의 페이지 캐시 상태가 다르다.
- 바이러스 검사와 백그라운드 I/O가 최대 지연에 영향을 준다.
- `fsync`와 디스크 내부 캐시의 실제 보장은 하드웨어·설정에 따라 다르다.
- 평균만 보면 긴 tail latency를 놓친다.

## 5. 결과 해석

1. fsync ON/OFF의 커밋 지연 비율을 본다.
2. txn_ops를 늘려 ops/sec가 어떻게 변하는지 계산한다.
3. 평균과 최대 커밋 지연의 차이를 본다.
4. 로그 크기와 replay ms를 그래프로 그린다.
5. 결과를 저장 장치 종류와 함께 기록한다.

`main`은 두 측정의 스윕 범위와 표 머리글을 정한다. `--smoke` 인자는 표본을 줄인 축소 실행이다 — 측정값으로는 쓸 수 없지만, fsync ON 커밋 경로와 위의 `Die` 검사가 ctest에서 매번 실행되도록 `db_bench_smoke`라는 이름으로 등록해 두었다.

```cpp
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
```

등록은 CMake 쪽 한 줄이다.

```cmake
add_test(NAME db_bench_smoke COMMAND db_bench --smoke)
```

```bash
cmake --build build --config Release
./build/Release/db_bench
```

관측 결과 — Windows 11, NVMe SSD, Release 빌드에서 측정한 값이다. 절대값은 장비마다 다르므로 자기 장비의 수치를 다시 기록해야 한다.

```text
== db_lab bench 1: 커밋 지연 — fsync x 트랜잭션 크기 ==
| fsync | txn_ops | txns     | avg ms     | max ms    | commits/sec  |
|-------|---------|----------|------------|-----------|--------------|
| OFF   |       1 |     2000 |     0.0138 |     0.492 |        72260 |
| OFF   |       4 |     2000 |     0.0183 |     0.233 |        54575 |
| OFF   |      16 |     2000 |     0.0257 |     0.378 |        38926 |
| OFF   |      64 |     2000 |     0.0761 |     0.333 |        13144 |
| ON    |       1 |      200 |     0.6465 |     1.785 |         1547 |
| ON    |       4 |      200 |     0.6752 |     1.334 |         1481 |
| ON    |      16 |      200 |     0.6442 |     1.208 |         1552 |
| ON    |      64 |      200 |     0.5981 |     5.816 |         1672 |

== db_lab bench 2: 복구(재생) 시간 — 로그 크기별 ==
| txns     | replay ms  | committed| keys   |
|----------|------------|----------|--------|
|     1000 |      23.20 |     1000 |     1000 |
|    10000 |      38.51 |    10000 |     5000 |
|    50000 |      85.39 |    50000 |     5000 |
```

이 실측에서 fsync ON의 커밋 지연은 OFF의 약 47배(0.6465 ms 대 0.0138 ms)다. txn_ops를 1에서 64로 키워도 ON의 avg ms는 거의 변하지 않으므로 ops/sec는 약 1547에서 약 107,000으로 오른다 — 커밋당 fsync 1회를 상각한 결과다. 반면 ON의 max ms는 txn_ops = 64에서 5.816 ms로 튀어, 평균만 보면 놓치는 꼬리 지연이 함께 커진다.

---

## 결론

트랜잭션당 연산 수와 `fsync` 정책을 독립 변수로 두고 커밋 지연, 처리량, 로그 크기, 재생 시간을 측정하도록 구성했다. 평균과 최대 지연을 함께 기록해 그룹 커밋의 처리량 이득이 긴 꼬리 지연을 숨기지 않게 했다.

이 수치는 특정 운영체제, 파일 시스템, 저장 장치, 캐시 상태에 종속된 미시 벤치마크다. 실제 게임 경제 워크로드의 키 분포, 동시성, 체크포인트 정지는 포함하지 않으므로 절대 성능표가 아니라 설계 비교 근거로 사용해야 한다.
