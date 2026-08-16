// 성공 경로 테스트. 오류 경로는 system_fault_tests, I/O 3항목(부분 읽기 ·
// EINTR · 취소)은 system_io_path_tests 가 본다.
//
// 이 파일이 답해야 하는 질문 셋.
//   1. 어느 backend 가 *실제로* 돌았는가. 런타임 폴백(io_uring → read)은
//      조용히 일어나므로, 요약 라인을 항상 찍고 SKL_REQUIRE_BACKENDS 가
//      설정된 환경에서는 폴백을 실패로 만든다. ctest 는 stdout 을 감추므로
//      "찍었다" 만으로는 검증이 아니다 — CHECK 이 있어야 검증이다.
//   2. 실행 모델(단일 · 워커풀 · 코루틴)과 sync 구현이 backend 를 바꿔도
//      같은 바이트·체크섬을 내는가. 이전에는 BackendType::Read 만 하드코딩돼
//      aio/uring/mmap 은 워커풀·코루틴 경로를 한 번도 밟지 않았다.
//   3. 작업 큐 인계(push → remaining_ store → unlock)의 경계가 안전한가.
//      반복 스트레스와 *결정적* 랑데부 두 가지로 본다.
//
// 검사들은 파일 끝의 표에서 *그것을 추가한 step* 에 묶인다. 인자 없이 부르면
// 전부 돌고, `--step=N` 은 그 step 이 추가한 검사만 돈다(tests/step_filter.h).

#include "fault/fault_inject.h"
#include "io/io_backend_factory.h"
#include "measure/frame_timer.h"
#include "sync/sync_factory.h"
#include "testhook/test_hook.h"
#include "workload/file_io_workload.h"

#include "step_filter.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {

int failures = 0;

int Failures() { return failures; }

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':'  \
                      << __LINE__ << ")\n";                                   \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

// 실패 사유에 조합 정보를 함께 남긴다. backend × ctx × sync 순회에서는
// 어떤 조합이 깨졌는지가 표현식보다 중요하다.
#define CHECK_IN(expr, where)                                                 \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::cerr << "CHECK failed: " #expr << " [" << (where) << "] ("   \
                      << __FILE__ << ':' << __LINE__ << ")\n";                \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

const char* Label(skl::BackendType type) {
    return skl::kBackendTypeLabels[static_cast<int>(type)];
}

std::string Combo(skl::BackendType backend, skl::CtxType ctx,
                  skl::SyncType sync, std::size_t workers) {
    return std::string(Label(backend)) + '/' +
           skl::kCtxTypeLabels[static_cast<int>(ctx)] + '/' +
           skl::kSyncTypeLabels[static_cast<int>(sync)] + "/w" +
           std::to_string(workers);
}

// ============================================================================
// SKL_REQUIRE_BACKENDS — 폴백을 실패로 만들지 결정한다.
//
// liburing 도 POSIX AIO 도 없는 머신에서 무조건 실패시키면 테스트가 이식성을
// 잃는다. 반대로 아무 때나 통과시키면 "io_uring 을 검사했다" 는 말이 거짓이
// 된다. 그래서 요구 목록을 환경변수로 받고, ctest 는 그 변수를 세팅한
// system_core_tests_strict 케이스를 따로 둔다(LABELS strict).
// ============================================================================

std::vector<skl::BackendType> g_required;
bool g_requireEnvSet = false;

bool ParseBackendName(const std::string& name, skl::BackendType* out) {
    for (int raw = 0; raw < skl::kBackendTypeCount; ++raw) {
        if (name == skl::kBackendTypeLabels[raw]) {
            *out = static_cast<skl::BackendType>(raw);
            return true;
        }
    }
    if (name == "uring") {  // "io_uring" 의 짧은 별칭
        *out = skl::BackendType::Uring;
        return true;
    }
    return false;
}

void LoadRequiredBackends() {
    const char* env = std::getenv("SKL_REQUIRE_BACKENDS");
    if (env == nullptr) return;
    g_requireEnvSet = true;

    const std::string spec(env);
    std::size_t pos = 0;
    while (pos <= spec.size()) {
        const std::size_t comma = spec.find(',', pos);
        const std::size_t end = (comma == std::string::npos) ? spec.size() : comma;
        std::string token = spec.substr(pos, end - pos);
        // 공백 제거.
        token.erase(std::remove_if(token.begin(), token.end(),
                                   [](unsigned char c) { return std::isspace(c); }),
                    token.end());
        if (!token.empty()) {
            skl::BackendType type{};
            if (ParseBackendName(token, &type)) {
                g_required.push_back(type);
            } else {
                // 오타를 조용히 넘기면 strict 케이스가 아무것도 강제하지 않는
                // 채로 초록이 된다. 그건 이 테스트가 고치려는 병이다.
                std::cerr << "CHECK failed: unknown backend name in "
                             "SKL_REQUIRE_BACKENDS: '" << token << "'\n";
                ++failures;
            }
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
}

bool Required(skl::BackendType type) {
    return std::find(g_required.begin(), g_required.end(), type) !=
           g_required.end();
}

// 요청한 backend 가 실제로 그 backend 였는지. 요구 목록에 있으면 CHECK,
// 아니면 폴백을 관측 사실로만 기록한다.
void CheckNoSilentFallback(skl::BackendType requested, skl::BackendType actual,
                           const std::string& where) {
    if (actual == requested) return;
    if (Required(requested)) {
        std::cerr << "CHECK failed: backend '" << Label(requested)
                  << "' fell back to '" << Label(actual) << "' at " << where
                  << " but SKL_REQUIRE_BACKENDS demands it\n";
        ++failures;
    } else {
        std::cout << "  note: " << Label(requested)
                  << " unavailable at runtime -> " << Label(actual)
                  << " fallback exercised (" << where << ")\n";
    }
}

class TempFixture {
public:
    TempFixture(std::size_t fileCount, std::size_t fileBytes)
        : fileBytes_(fileBytes) {
        dir_ = std::filesystem::temp_directory_path() /
               ("system_core_tests_" + std::to_string(::getpid()));
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);

        for (std::size_t file = 0; file < fileCount; ++file) {
            const auto path = dir_ / ("fixture_" + std::to_string(file) + ".bin");
            std::vector<unsigned char> bytes(fileBytes_);
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = static_cast<unsigned char>((file * 31 + i * 17) & 0xff);
                expectedSum_ += bytes[i];
            }
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            CHECK(out.good());
            paths_.push_back(path.string());
        }
    }

    ~TempFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(dir_, ignored);
    }

    const std::vector<std::string>& Paths() const { return paths_; }
    const std::filesystem::path& Dir() const { return dir_; }
    std::size_t FileBytes() const { return fileBytes_; }
    std::uint64_t ExpectedSum() const { return expectedSum_; }

private:
    std::filesystem::path dir_;
    std::vector<std::string> paths_;
    std::size_t fileBytes_ = 0;
    std::uint64_t expectedSum_ = 0;
};

// backend 를 추가한 step 단위로 부르기 위해 대상 목록을 인자로 받는다. 전체
// 순회는 아래 세 그룹을 차례로 부르는 것과 같다.
// snippet-begin: backend-equivalence
void CheckIoBackends(const skl::BackendType* types, std::size_t count,
                     const char* group) {
    constexpr std::size_t kFileCount = 4;
    constexpr std::size_t kFileBytes = 64 * 1024;
    TempFixture fixture(kFileCount, kFileBytes);

    std::string summary;
    for (std::size_t idx = 0; idx < count; ++idx) {
        const auto requested = types[idx];
        if (!summary.empty()) summary += ' ';
        summary += Label(requested);
        summary += '=';

        if (!skl::BackendCompiledIn(requested)) {
            summary += "<not compiled>";
            // 컴파일조차 안 됐는데 요구 목록에 있으면 그건 빌드 구성 문제다.
            if (Required(requested)) {
                std::cerr << "CHECK failed: backend '" << Label(requested)
                          << "' is required but was not compiled in\n";
                ++failures;
            }
            continue;
        }

        skl::BackendType actual = requested;
        auto backend = skl::CreateBackend(requested, &actual);
        std::uint64_t sum = 0;
        const std::uint64_t bytes = backend->RunCycle(
            fixture.Paths(), fixture.FileBytes(),
            [&](const unsigned char* data, std::size_t size) {
                for (std::size_t i = 0; i < size; ++i) sum += data[i];
            });

        CHECK_IN(bytes == kFileCount * kFileBytes, Label(requested));
        CHECK_IN(sum == fixture.ExpectedSum(), Label(requested));
        CheckNoSilentFallback(requested, actual, "CreateBackend");
        summary += Label(actual);
    }

    // ctest 가 stdout 을 감추더라도 --output-on-failure 나 -V 에서 어느
    // backend 가 실제로 돌았는지 한 줄로 읽힌다.
    std::cout << "[backends] " << group << ": " << summary << '\n';
}
// snippet-end: backend-equivalence

// read backend 하나. 인터페이스와 factory 를 처음 세운 step 의 몫이다.
constexpr skl::BackendType kReadBackend[] = {skl::BackendType::Read};
// 동기 backend 두 개를 더 얹은 step.
constexpr skl::BackendType kSyncBackends[] = {skl::BackendType::Pread,
                                              skl::BackendType::Mmap};
// 비동기 backend 두 개를 더 얹은 step.
constexpr skl::BackendType kAsyncBackends[] = {skl::BackendType::Aio,
                                               skl::BackendType::Uring};

template <std::size_t N>
void CheckGroup(const skl::BackendType (&types)[N], const char* group) {
    CheckIoBackends(types, N, group);
}

void TestReadBackendEquivalence()  { CheckGroup(kReadBackend,  "read"); }
void TestSyncBackendEquivalence()  { CheckGroup(kSyncBackends, "pread/mmap"); }
void TestAsyncBackendEquivalence() { CheckGroup(kAsyncBackends, "aio/io_uring"); }

// snippet-begin: sync-mutual-exclusion
void TestSyncPrimitives() {
    constexpr int kThreads = 4;
    constexpr int kIterations = 20000;

    for (int raw = 0; raw < skl::kSyncTypeCount; ++raw) {
        auto sync = skl::CreateSync(static_cast<skl::SyncType>(raw));
        std::uint64_t protectedCounter = 0;
        std::vector<std::thread> workers;
        for (int t = 0; t < kThreads; ++t) {
            workers.emplace_back([&] {
                for (int i = 0; i < kIterations; ++i) {
                    sync->Lock();
                    ++protectedCounter;
                    sync->Unlock();
                }
            });
        }
        for (auto& worker : workers) worker.join();
        CHECK_IN(protectedCounter ==
                     static_cast<std::uint64_t>(kThreads * kIterations),
                 skl::kSyncTypeLabels[raw]);
    }
    std::cout << "[sync] " << skl::kSyncTypeCount << " impls x " << kThreads
              << " threads x " << kIterations << " increments; each ended at "
              << kThreads * kIterations << '\n';
}
// snippet-end: sync-mutual-exclusion

// 그 backend 만 부르는 syscall seam 이름. fault seam 을 *주입 없이* (skip 을
// 아주 크게) 무장하면 호출 횟수만 세는 순수 관측 모드가 된다. 순회가 정말
// 그 backend 를 워커풀·코루틴 경로에서 돌렸는지를 print 가 아니라 CHECK 으로
// 확인하는 데 쓴다 — 바이트·체크섬 비교만으로는 폴백한 read 와 구별되지 않는다.
const char* ProbeOp(skl::BackendType type) {
    switch (type) {
        case skl::BackendType::Read:  return "read";
        case skl::BackendType::Pread: return "pread";
        case skl::BackendType::Mmap:  return "mmap";
        case skl::BackendType::Aio:   return "aio_read";
        case skl::BackendType::Uring: return "io_uring_submit";
    }
    return "read";
}

// ============================================================================
// 실행 모델 × backend × sync 순회.
//
// 기준값은 "단일 스레드 / read backend" 사이클 하나다. 나머지 조합은 실행
// 단위도 backend 도 다르지만 *같은 파일 집합* 을 읽으므로 총 바이트와 체크섬이
// 같아야 한다. 체크섬은 파일 단위 표본이라 처리 순서에 무관하다.
//
// 실행 단위(ctx)는 인자로 받는다. 워커 풀과 코루틴 풀은 서로 다른 step 이
// 추가한 것이라 각자의 step 에서 따로 불려야 한다. 두 ctx 를 차례로 부르면
// 예전의 전수 순회와 같은 조합을 밟는다.
// ============================================================================
void CheckWorkloadExecutionModel(skl::CtxType ctx) {
    constexpr std::size_t kFileCount = 8;
    constexpr std::size_t kFileBytes = 4097;
    constexpr std::size_t kWorkers = 4;   // 워커 4개 = uring ring 4개 동시 생성
    constexpr int kCyclesPerCombo = 4;
    const char* ctxLabel = skl::kCtxTypeLabels[static_cast<int>(ctx)];
    TempFixture owner(1, 1);

    skl::FileIoWorkload workload;
    workload.Init(skl::BackendType::Read, skl::SyncType::Mutex,
                  skl::SchedType::Default, skl::CtxType::Pthread,
                  1, kFileCount, kFileBytes, owner.Dir() / "workload");
    const std::uint64_t expectedBytes = workload.RunCycle();
    const std::uint64_t expectedChecksum = workload.LastChecksum();
    CHECK(expectedBytes == kFileCount * kFileBytes);
    CHECK(expectedChecksum != 0);

    int combos = 0;
    std::string exercised;
    for (int rawBackend = 0; rawBackend < skl::kBackendTypeCount; ++rawBackend) {
        const auto backend = static_cast<skl::BackendType>(rawBackend);
        if (!skl::BackendCompiledIn(backend)) continue;

        // 관측 전용 무장 — skip 이 커서 아무것도 주입하지 않고 세기만 한다.
        const char* probe = ProbeOp(backend);
        CHECK_IN(skl::SetFaultSpec(std::string(probe) + ":100000000"),
                 Label(backend));

        bool downgraded = false;
        // snippet-begin: execution-model-equivalence
        {
            for (int rawSync = 0; rawSync < skl::kSyncTypeCount; ++rawSync) {
                const auto sync = static_cast<skl::SyncType>(rawSync);
                const std::string where = Combo(backend, ctx, sync, kWorkers);

                workload.Reconfigure(backend, sync, skl::SchedType::Default,
                                     ctx, kWorkers);
                CheckNoSilentFallback(backend, workload.Backend(), where);
                if (workload.Backend() != backend) downgraded = true;

                for (int cycle = 0; cycle < kCyclesPerCombo; ++cycle) {
                    CHECK_IN(workload.RunCycle() == expectedBytes, where);
                    CHECK_IN(workload.LastChecksum() == expectedChecksum, where);
                }
                ++combos;
            }
        }
        // snippet-end: execution-model-equivalence

        const std::size_t probeCalls = skl::FaultCallCount(probe);
        const std::size_t probeInjections = skl::FaultInjectionCount(probe);
        skl::ClearFaults();
        // 관측 모드가 정말 관측만 했는가 (주입했다면 위 비교가 무의미해진다).
        CHECK_IN(probeInjections == 0, Label(backend));
        // 폴백하지 않은 backend 는 자기 syscall 을 실제로 불렀어야 한다.
        // 이 CHECK 이 "순회가 read 만 돌리고 있다" 를 잡는다.
        if (!downgraded) {
            CHECK_IN(probeCalls > 0, Label(backend));
        }

        if (!exercised.empty()) exercised += ' ';
        exercised += Label(backend);
        exercised += '(';
        exercised += probe;
        exercised += 'x';
        exercised += std::to_string(probeCalls);
        exercised += ')';
    }
    workload.Shutdown();

    // 순회가 조용히 비어 버리는(= BackendCompiledIn 이 전부 false) 상황을
    // 초록으로 넘기지 않는다.
    CHECK(combos >= skl::kSyncTypeCount);
    std::cout << "[models] ctx=" << ctxLabel << ": " << combos << " combos x "
              << kCyclesPerCombo << " cycles; backends exercised: " << exercised
              << '\n';
}

void TestWorkerPoolExecutionModel() {
    CheckWorkloadExecutionModel(skl::CtxType::Pthread);
}

void TestCoroutinePoolExecutionModel() {
    CheckWorkloadExecutionModel(skl::CtxType::Coroutine);
}

// ============================================================================
// 작업 큐 인계 스트레스.
//
// RunCycleMulti 는 사이클마다 push → remaining_ 설정 → unlock → 워커 깨우기
// 를 반복한다. 사이클 경계는 "직전 사이클의 마지막 작업을 막 끝낸 워커가
// 아직 pop 루프 안에 있는" 짧은 순간을 만든다. 한 사이클만 돌려서는 그 순간을
// 밟을 수 없으므로 사이클 수를 늘린다.
//
// 판정은 세 가지를 겹쳐 쓴다.
//   (a) 바이트·체크섬 불일치 — 인계가 작업을 잃거나 중복 처리했는가.
//   (b) remaining_ 언더플로 관측 — 아래 queue_stress 훅이 센다.
//   (c) 진행 정지(hang) — 감시 스레드의 데드라인.
//
// (b)/(c) 를 왜 둘 다 두는가. 예전에는 (a) 와 "매달리면 ctest TIMEOUT" 뿐이라
// 인계 회귀가 60초 뒤 아무 설명 없는 TIMEOUT 으로만 나타났다. 문제는 이
// 워크로드에서 언더플로와 hang 이 *같은 사건* 이라는 점이다 —
// 0 에서의 fetch_sub 는 SIZE_MAX 를 만들고, 곧이어 오는 store(N) 이 그 감산을
// 통째로 지워 remaining_ 이 영영 0 이 되지 못한다. 즉 언더플로를 일으킨 바로
// 그 RunCycle 이 그대로 매달리므로, 루프로 돌아와 카운터를 읽는 코드는
// 실행될 기회가 없다. 그래서 카운터는 *다른 스레드* 가 읽어야 한다.
//
// 감시 스레드가 1ms 주기로 카운터를 폴링하고, 0 이 아닌 순간 즉시 진단을
// 찍고 종료한다. hang 이 TIMEOUT 으로 익을 때까지 기다리지 않으므로 실패
// 지연이 60초 → 밀리초가 된다. 데드라인은 "언더플로 없이 멈추는" 다른 실패
// 양상을 위한 뒷받침으로 남긴다.
// ============================================================================
namespace queue_stress {

// 세기 전용 훅. 회귀 테스트의 랑데부 훅(queue_race)과 달리 락도 대기도 없이
// relaxed fetch_add 하나뿐이다 — 스트레스가 찾는 것은 확률적 인터리빙이므로
// 훅이 타이밍을 바꾸면 찾으려던 창 자체가 사라진다.
std::atomic<std::size_t> underflowSeen{0};
std::atomic<bool> loopDone{false};

void OnWorkerAfterDecrement(std::size_t before) {
    // before == 0 → 0 에서 fetch_sub(1) = SIZE_MAX 언더플로.
    // store 가 unlock *앞* 에 있는 정상 코드에서는 일어날 수 없다.
    if (before == 0) underflowSeen.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace queue_stress

void TestQueueHandoffStress() {
    constexpr std::size_t kFileCount = 8;
    constexpr std::size_t kFileBytes = 4096;
    constexpr std::size_t kWorkers = 8;

    // 사이클 수. 이 회귀는 메인이 (Unlock, store) 사이에서 *선점당해야* 발현하고
    // 그 창은 수십 ns 라, 측정한 사이클당 적중 확률은 ~2e-4 다. 예전 값 400
    // (= sync 4종 합쳐 1600)으로는 회귀를 20회 중 4회밖에 못 잡았다. 8000
    // (합쳐 32000)에서 40/40. 워커 수를 코어보다 늘려 선점을 유도하는 쪽도
    // 재봤지만 검출률은 그대로고 시간만 5배가 됐다 — 사이클 수가 유일하게
    // 듣는 손잡이였다. 정상 코드 소요는 0.2s → 0.9s (ctest TIMEOUT 은 60s).
    //
    // SKL_STRESS_CYCLES 로 *늘릴* 수는 있어도 줄일 수는 없다. soak 용 손잡이지
    // 테스트를 약화시키는 손잡이가 아니다.
    constexpr int kCyclesFloor = 8000;
    int kCycles = kCyclesFloor;
    if (const char* env = std::getenv("SKL_STRESS_CYCLES")) {
        kCycles = std::max(kCyclesFloor, std::atoi(env));
    }
    TempFixture owner(1, 1);

    skl::FileIoWorkload workload;
    workload.Init(skl::BackendType::Read, skl::SyncType::Mutex,
                  skl::SchedType::Default, skl::CtxType::Pthread,
                  1, kFileCount, kFileBytes, owner.Dir() / "stress");
    const std::uint64_t expectedBytes = workload.RunCycle();
    const std::uint64_t expectedChecksum = workload.LastChecksum();
    CHECK(expectedBytes == kFileCount * kFileBytes);

    queue_stress::underflowSeen.store(0, std::memory_order_relaxed);
    queue_stress::loopDone.store(false, std::memory_order_release);
    skl::SetTestHook(skl::TestHookId::WorkerAfterDecrement,
                     &queue_stress::OnWorkerAfterDecrement);

    // 감시 스레드. 아래 루프가 매달리면 이 스레드만이 카운터를 읽을 수 있다.
    // 정상 경로에서는 1ms 마다 원자 load 한 번이라 워커 타이밍에 영향이 없다.
    // 데드라인은 정상 소요(수백 ms)의 100배 이상으로 잡아 부하 걸린 머신에서
    // 오탐이 나지 않게 한다. ctest TIMEOUT(60s)보다는 확실히 앞선다.
    constexpr auto kStressBudget = std::chrono::seconds(30);
    std::thread monitor([&] {
        const auto deadline = std::chrono::steady_clock::now() + kStressBudget;
        while (!queue_stress::loopDone.load(std::memory_order_acquire)) {
            const std::size_t uf =
                queue_stress::underflowSeen.load(std::memory_order_relaxed);
            const bool late = std::chrono::steady_clock::now() > deadline;
            if (uf != 0 || late) {
                std::cerr
                    << "CHECK failed: "
                    << (uf != 0 ? "remaining_ underflow observed"
                                : "stress loop made no progress within budget")
                    << " — work-queue handoff regression (remaining_ store "
                       "must happen before the queue unlock). underflow "
                       "observations="
                    << uf << "\n  " << __FILE__ << ':' << __LINE__ << '\n'
                    << std::flush;
                // 언더플로가 났다면 remaining_ 은 이미 복구 불가능하게
                // 덮어써졌고 워커/메인이 매달려 join 도 정상 종료도 안 된다.
                std::_Exit(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::size_t underflow = 0;
    for (int rawSync = 0; rawSync < skl::kSyncTypeCount; ++rawSync) {
        const auto sync = static_cast<skl::SyncType>(rawSync);
        const std::string where =
            Combo(skl::BackendType::Read, skl::CtxType::Pthread, sync, kWorkers);
        workload.Reconfigure(skl::BackendType::Read, sync,
                             skl::SchedType::Default, skl::CtxType::Pthread,
                             kWorkers);
        int mismatches = 0;
        for (int cycle = 0; cycle < kCycles; ++cycle) {
            if (workload.RunCycle() != expectedBytes) ++mismatches;
            if (workload.LastChecksum() != expectedChecksum) ++mismatches;
            // 매달리지 *않은* 언더플로를 위한 보조 경로. 위 감시 스레드가 먼저
            // 잡는 것이 보통이지만, 이 검사는 프로세스를 죽이지 않고 조합
            // 정보까지 남기므로 둘 다 둔다.
            underflow =
                queue_stress::underflowSeen.load(std::memory_order_relaxed);
            if (underflow != 0) break;
        }
        CHECK_IN(mismatches == 0, where);
        CHECK_IN(underflow == 0, where);
        if (underflow != 0) break;
    }

    queue_stress::loopDone.store(true, std::memory_order_release);
    monitor.join();
    skl::SetTestHook(skl::TestHookId::WorkerAfterDecrement, nullptr);
    workload.Shutdown();
    std::cout << "[handoff] " << skl::kSyncTypeCount << " sync types x "
              << kCycles << " cycles with " << kWorkers
              << " workers; remaining_ underflow observations=" << underflow
              << '\n';
}

// ============================================================================
// 작업 큐 인계 회귀 — 결정적 재현.
//
// 스트레스 반복은 확률에 기댄다. 여기서는 test_hook 두 개로 문제의 인터리빙을
// 직접 만든다.
//
//   [워커] 사이클의 마지막 작업을 끝내 remaining_ 을 0 으로 만든 워커를
//          WorkerAfterDecrement 훅이 붙잡는다. 그 워커는 *pop 루프 안에* 있다.
//   [메인] 다음 사이클에서 push 를 끝내고 락을 푼 직후 AfterQueueUnlock 훅이
//          그 워커를 풀어 주고, 그 워커가 항목 하나를 소비해 감산까지 마칠
//          때까지 기다린다.
//
// remaining_.store 가 unlock *앞* 에 있으면(현재 코드) 그 감산은 8 → 7 이다.
// unlock *뒤* 로 내리면 0 → SIZE_MAX 언더플로가 되고, 뒤이은 store(8) 이
// 카운터를 덮어써 완료 대기가 끝나지 않는다. 그래서 이 테스트는 그 변이에서
// 반드시 매달린다 — 매달린 스레드는 join 할 수 없으므로 워치독이 진단을 찍고
// 즉시 종료한다(ctest 의 TIMEOUT 은 그다음 안전망이다).
// ============================================================================
namespace queue_race {

enum Phase { kArmed = 0, kStragglerParked = 1, kStragglerConsumed = 2 };

std::mutex mtx;
std::condition_variable cv;
int phase = kArmed;
bool released = false;
std::thread::id stragglerId{};
std::size_t underflowSeen = 0;

void OnWorkerAfterDecrement(std::size_t before) {
    std::unique_lock<std::mutex> lock(mtx);
    if (before == 0) ++underflowSeen;  // 0 에서 감산 = 언더플로 목격

    if (phase == kArmed && before == 1) {
        phase = kStragglerParked;
        stragglerId = std::this_thread::get_id();
        cv.notify_all();
        cv.wait_for(lock, std::chrono::seconds(10), [] { return released; });
        return;
    }
    if (phase == kStragglerParked &&
        std::this_thread::get_id() == stragglerId) {
        phase = kStragglerConsumed;
        cv.notify_all();
    }
}

void OnAfterQueueUnlock(std::size_t) {
    std::unique_lock<std::mutex> lock(mtx);
    if (phase != kStragglerParked) return;  // 붙잡아 둔 워커가 없는 사이클
    released = true;
    cv.notify_all();
    cv.wait_for(lock, std::chrono::seconds(10),
                [] { return phase == kStragglerConsumed; });
}

}  // namespace queue_race

void TestQueueHandoffRegression() {
    constexpr std::size_t kFileCount = 8;
    constexpr std::size_t kFileBytes = 4096;
    constexpr std::size_t kWorkers = 4;
    TempFixture owner(1, 1);

    skl::FileIoWorkload workload;
    workload.Init(skl::BackendType::Read, skl::SyncType::Mutex,
                  skl::SchedType::Default, skl::CtxType::Pthread,
                  1, kFileCount, kFileBytes, owner.Dir() / "race");
    const std::uint64_t expectedBytes = workload.RunCycle();
    const std::uint64_t expectedChecksum = workload.LastChecksum();
    CHECK(expectedBytes == kFileCount * kFileBytes);

    workload.Reconfigure(skl::BackendType::Read, skl::SyncType::Mutex,
                         skl::SchedType::Default, skl::CtxType::Pthread,
                         kWorkers);

    skl::SetTestHook(skl::TestHookId::WorkerAfterDecrement,
                     &queue_race::OnWorkerAfterDecrement);
    skl::SetTestHook(skl::TestHookId::AfterQueueUnlock,
                     &queue_race::OnAfterQueueUnlock);

    // snippet-begin: queue-handoff-rendezvous
    // 1) 이 사이클의 마지막 워커가 훅에 붙잡힌다.
    CHECK(workload.RunCycle() == expectedBytes);

    // 2) 정말 붙잡혔는지 확인한 뒤에 다음 push 를 낸다. 이 대기가 있어야
    //    "워커가 pop 루프 안에 있는 상태에서의 push" 가 보장된다.
    {
        std::unique_lock<std::mutex> lock(queue_race::mtx);
        const bool parked = queue_race::cv.wait_for(
            lock, std::chrono::seconds(10),
            [] { return queue_race::phase >= queue_race::kStragglerParked; });
        CHECK(parked);
    }

    // 3) 문제의 사이클. 워치독을 걸고 별도 스레드에서 돌린다.
    std::promise<std::uint64_t> promise;
    auto future = promise.get_future();
    std::thread runner([&] {
        try {
            promise.set_value(workload.RunCycle());
        } catch (...) {
            promise.set_exception(std::current_exception());
        }
    });

    if (future.wait_for(std::chrono::seconds(10)) !=
        std::future_status::ready) {
        std::size_t underflow = 0;
        {
            std::lock_guard<std::mutex> lock(queue_race::mtx);
            underflow = queue_race::underflowSeen;
        }
        std::cerr << "CHECK failed: RunCycle did not return within 10s — "
                     "work-queue handoff regression (remaining_ store must "
                     "happen before the queue unlock). underflow observations="
                  << underflow << "\n"
                  << "  " << __FILE__ << ':' << __LINE__ << '\n'
                  << std::flush;
        // 워커가 매달려 있어 join 도 정상 종료도 불가능하다.
        std::_Exit(1);
    }
    runner.join();

    std::uint64_t bytes = 0;
    try {
        bytes = future.get();
    } catch (const std::exception& e) {
        std::cerr << "CHECK failed: RunCycle threw: " << e.what() << '\n';
        ++failures;
    }
    CHECK(bytes == expectedBytes);
    CHECK(workload.LastChecksum() == expectedChecksum);

    {
        std::lock_guard<std::mutex> lock(queue_race::mtx);
        // 랑데부가 실제로 성립했는가. 성립하지 않았다면 이 테스트는 아무것도
        // 재현하지 않은 것이고, 그건 통과가 아니라 실패다.
        CHECK(queue_race::phase == queue_race::kStragglerConsumed);
        CHECK(queue_race::underflowSeen == 0);
    }
    // snippet-end: queue-handoff-rendezvous

    skl::SetTestHook(skl::TestHookId::WorkerAfterDecrement, nullptr);
    skl::SetTestHook(skl::TestHookId::AfterQueueUnlock, nullptr);

    // 훅을 푼 뒤에도 정상 동작하는지 — 훅 자체가 상태를 망가뜨리지 않았음.
    for (int cycle = 0; cycle < 8; ++cycle) {
        CHECK(workload.RunCycle() == expectedBytes);
        CHECK(workload.LastChecksum() == expectedChecksum);
    }
    workload.Shutdown();
    std::cout << "[handoff] deterministic straggler rendezvous reproduced\n";
}

// ============================================================================
// 워크로드 골격 — 결정적 fixture + 단일 스레드 사이클.
//
// 토글도 워커도 없는 상태에서 이 도구가 재는 것이 무엇인지를 못 박는다.
// fixture 가 요청한 개수·크기로 실제 디스크에 있고, 사이클이 그 전량을 읽고,
// *반복해도 같은 값* 이 나와야 한다. 사이클마다 값이 달라지면 그 뒤의 어떤
// 비교도 성립하지 않는다.
// ============================================================================
// snippet-begin: workload-skeleton
void TestWorkloadSkeleton() {
    constexpr std::size_t kFileCount = 6;
    constexpr std::size_t kFileBytes = 4096;
    TempFixture owner(1, 1);
    const auto dir = owner.Dir() / "skeleton";

    skl::FileIoWorkload workload;
    workload.Init(skl::BackendType::Read, skl::SyncType::Mutex,
                  skl::SchedType::Default, skl::CtxType::Pthread,
                  1, kFileCount, kFileBytes, dir);

    CHECK(workload.FileCount() == kFileCount);
    CHECK(workload.FileBytes() == kFileBytes);
    CHECK(workload.WorkerCount() == 1);
    CHECK(workload.FixtureDir() == dir);

    // fixture 가 *디스크에* 요청한 개수·크기로 있는가. 개수만 세면 크기가
    // 어긋난 파일을 그대로 지나친다.
    std::size_t onDisk = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        ++onDisk;
        CHECK(std::filesystem::file_size(entry.path()) == kFileBytes);
    }
    CHECK(onDisk == kFileCount);

    const std::uint64_t bytes = workload.RunCycle();
    const std::uint64_t checksum = workload.LastChecksum();
    CHECK(bytes == kFileCount * kFileBytes);
    CHECK(checksum != 0);
    for (int cycle = 0; cycle < 4; ++cycle) {
        CHECK(workload.RunCycle() == bytes);
        CHECK(workload.LastChecksum() == checksum);
    }
    workload.Shutdown();

    // fixture 를 지우고 다시 만들어도 같은 체크섬 — 같은 시드로 재생성된다.
    // 이게 깨지면 "어제 잰 값" 과 "오늘 잰 값" 을 비교할 수 없다.
    std::filesystem::remove_all(dir);
    skl::FileIoWorkload regenerated;
    regenerated.Init(skl::BackendType::Read, skl::SyncType::Mutex,
                     skl::SchedType::Default, skl::CtxType::Pthread,
                     1, kFileCount, kFileBytes, dir);
    CHECK(regenerated.RunCycle() == bytes);
    CHECK(regenerated.LastChecksum() == checksum);
    regenerated.Shutdown();

    std::cout << "[skeleton] " << kFileCount << " files x " << kFileBytes
              << " B; bytes/cycle=" << bytes << " checksum=" << checksum
              << " (stable across 5 cycles and one regeneration)\n";
}
// snippet-end: workload-skeleton

// ============================================================================
// FrameTimer — 슬라이딩 윈도우와 누계.
//
// 시간은 머신마다 다르지만 *링 버퍼의 회계* 는 아니다. head 가 용량에서
// 되감기고, filled 가 용량에서 멈추고, 누계가 사이클 수·바이트 수를 그대로
// 따라가야 한다. 이게 어긋나면 그래프는 그럴듯하게 그려지면서 엉뚱한 구간을
// 보여 준다.
// ============================================================================
void TestFrameTimerWindow() {
    skl::FrameTimer timer;
    CHECK(timer.Head() == 0);
    CHECK(timer.Filled() == 0);
    CHECK(timer.TotalCycles() == 0);
    CHECK(timer.TotalBytes() == 0);

    constexpr std::uint64_t kBytes = 1024;
    const std::size_t cap = skl::FrameTimer::kCapacity;
    for (std::size_t i = 0; i < cap; ++i) {
        timer.BeginCycle();
        timer.EndCycle(kBytes);
    }
    CHECK(timer.Head() == 0);            // 정확히 한 바퀴 → 되감김
    CHECK(timer.Filled() == cap);        // 가득 참
    CHECK(timer.TotalCycles() == cap);
    CHECK(timer.TotalBytes() == cap * kBytes);
    CHECK(timer.LastBytes() == kBytes);

    // 한 바퀴를 넘겨도 filled 는 용량에서 멈추고 head 만 전진한다.
    timer.BeginCycle();
    timer.EndCycle(2 * kBytes);
    CHECK(timer.Head() == 1);
    CHECK(timer.Filled() == cap);
    CHECK(timer.TotalCycles() == cap + 1);
    CHECK(timer.TotalBytes() == cap * kBytes + 2 * kBytes);
    CHECK(timer.LastBytes() == 2 * kBytes);
    // 링에 마지막 값이 실제로 들어갔는가 (head 는 다음 칸을 가리킨다).
    CHECK(timer.CycleMs()[0] == timer.LastCycleMs());
    CHECK(timer.ThroughputMBs()[0] == timer.LastThroughputMBs());
    // 처리량은 ms 로 나눈 값이라 시간에 의존하지만 부호와 유한성은 아니다.
    CHECK(timer.LastCycleMs() >= 0.0f);
    CHECK(timer.LastThroughputMBs() >= 0.0f);

    std::cout << "[timer] capacity=" << cap << " head=" << timer.Head()
              << " filled=" << timer.Filled()
              << " totalCycles=" << timer.TotalCycles()
              << " totalBytes=" << timer.TotalBytes() << '\n';
}

// ============================================================================
// FrameTimer 의 카운터 두 채널.
//
// cycle ms 와 처리량만으로는 "왜 느려졌는가" 를 못 나눈다. 컨텍스트 스위치와
// page fault 를 사이클 delta 로 받아 같은 윈도우에 싣는 채널이 그 자리다.
// 값을 *가공하지 않고 그대로* 실어야 외부 도구의 수치와 대조할 수 있다.
// ============================================================================
void TestFrameTimerCounterChannels() {
    skl::FrameTimer timer;

    timer.BeginCycle();
    timer.EndCycle(4096, /*ctxSwitchesDelta=*/37, /*pageFaultsDelta=*/11);
    CHECK(timer.LastCtxSwitches() == 37.0f);
    CHECK(timer.LastPageFaults() == 11.0f);
    CHECK(timer.CtxSwitches()[0] == 37.0f);
    CHECK(timer.PageFaults()[0] == 11.0f);

    // 기본 인자 = 0. 카운터를 못 읽는 환경에서도 채널이 조용히 이전 값을
    // 물고 있으면 안 된다.
    timer.BeginCycle();
    timer.EndCycle(4096);
    CHECK(timer.LastCtxSwitches() == 0.0f);
    CHECK(timer.LastPageFaults() == 0.0f);
    CHECK(timer.CtxSwitches()[1] == 0.0f);
    CHECK(timer.PageFaults()[1] == 0.0f);

    // 네 채널이 같은 인덱스를 공유한다 — 그래프 네 개가 같은 사이클을 가리킨다.
    CHECK(timer.Head() == 2);
    CHECK(timer.Filled() == 2);
    CHECK(timer.CycleMs()[1] == timer.LastCycleMs());

    std::cout << "[channels] ctx_sw=" << timer.CtxSwitches()[0] << ','
              << timer.CtxSwitches()[1] << " faults=" << timer.PageFaults()[0]
              << ',' << timer.PageFaults()[1] << " (delta as given, no smoothing)"
              << '\n';
}

// 검사 ↔ step 대응표. 소속은 *그 step 이 무엇을 추가했는가* 로 정한다.
const skl_test::StepCase kCases[] = {
    {1, "workload skeleton (deterministic fixture, single thread)",
     &TestWorkloadSkeleton},
    {1, "frame timer sliding window",        &TestFrameTimerWindow},
    {2, "read backend equivalence",          &TestReadBackendEquivalence},
    {3, "pread/mmap backend equivalence",    &TestSyncBackendEquivalence},
    {4, "aio/io_uring backend equivalence",  &TestAsyncBackendEquivalence},
    {5, "sync primitives mutual exclusion",  &TestSyncPrimitives},
    {5, "worker pool execution model",       &TestWorkerPoolExecutionModel},
    {5, "queue handoff stress",              &TestQueueHandoffStress},
    {5, "queue handoff rendezvous",          &TestQueueHandoffRegression},
    {7, "coroutine pool execution model",    &TestCoroutinePoolExecutionModel},
    {8, "frame timer counter channels",      &TestFrameTimerCounterChannels},
};

}  // namespace

int main(int argc, char** argv) {
    LoadRequiredBackends();
    // 폴백을 실패로 만드는 모드인지 한 번만 알린다. 아래 backend 검사들이
    // 이 설정을 공유한다.
    std::cout << "[strict] SKL_REQUIRE_BACKENDS="
              << (g_requireEnvSet ? std::getenv("SKL_REQUIRE_BACKENDS")
                                  : "<unset> (fallback tolerated)")
              << '\n';
    return skl_test::RunStepCases(argc, argv, "system_core_tests", kCases,
                                  sizeof(kCases) / sizeof(kCases[0]),
                                  &Failures, &skl::ClearFaults);
}
