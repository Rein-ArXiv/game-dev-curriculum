// 오류 경로 전용 테스트.
//
// system_core_tests 는 *성공 경로* 만 본다. 정규 파일 + 정상 커널에서는
// EINTR 재시도, aio_suspend 실패 후 수확, io_uring 링 재초기화, factory 의
// uring→read 런타임 폴백, 스케줄링 권한 실패 폴백이 한 번도 실행되지 않는다.
// 이 파일은 src/fault 의 주입 seam 으로 그 분기들을 결정적으로 밟는다.
//
// 공통 사후조건: 모든 오류 경로 뒤에 /proc/self/fd 개수가 그대로여야 한다
// (fd 누수 없음), 그리고 주입을 푼 다음 사이클이 정상값을 내야 한다
// (오류 처리가 backend 를 망가뜨린 채로 두지 않음).
//
// 검사들은 파일 끝의 표에서 *그것을 추가한 step* 에 묶인다. 인자 없이 부르면
// 전부 돌고, `--step=N` 은 그 step 이 추가한 검사만 돈다(tests/step_filter.h).

#include "fault/fault_inject.h"
#include "io/io_backend.h"
#include "io/io_backend_factory.h"
#include "sched/sched_policy.h"
#include "sync/sync_factory.h"
#include "workload/file_io_workload.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>

#include "step_filter.h"

namespace {

std::atomic<int> failures{0};

int Failures() { return failures.load(); }

// 이 lab 은 backend 를 한 step 에 하나씩 얹는다. 오류 경로 검사도 같은 단위로
// 갈라야 "이 step 이 추가한 것" 을 독자가 따로 돌릴 수 있다.
constexpr skl::BackendType kReadBackend[]  = {skl::BackendType::Read};
constexpr skl::BackendType kSyncBackends[] = {skl::BackendType::Pread,
                                              skl::BackendType::Mmap};
constexpr skl::BackendType kAsyncBackends[] = {skl::BackendType::Aio,
                                               skl::BackendType::Uring};

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':'     \
                      << __LINE__ << ")\n";                                   \
            failures.fetch_add(1);                                            \
        }                                                                     \
    } while (false)

// ---------------------------------------------------------------------------
// 공통 유틸
// ---------------------------------------------------------------------------

// 열린 fd 개수. 오류 경로가 fd 를 흘리는지 보는 값싼 사후조건.
// 반환값에는 디렉터리 순회 자체의 fd 도 포함되지만, 비교는 항상 같은 방식으로
// 잰 두 값 사이에서만 한다.
std::size_t OpenFdCount() {
    std::error_code ec;
    const std::filesystem::directory_iterator it("/proc/self/fd", ec);
    if (ec) return 0;
    return static_cast<std::size_t>(
        std::distance(it, std::filesystem::directory_iterator()));
}

// 테스트가 끝나면 반드시 주입을 푼다 — 다음 테스트로 새는 것을 막는다.
class ScopedFault {
public:
    explicit ScopedFault(const std::string& spec) {
        if (!skl::SetFaultSpec(spec)) {
            std::cerr << "SetFaultSpec rejected: " << spec << '\n';
            failures.fetch_add(1);
        }
    }
    ~ScopedFault() { skl::ClearFaults(); }
    ScopedFault(const ScopedFault&)            = delete;
    ScopedFault& operator=(const ScopedFault&) = delete;
};

bool Contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

class TempFixture {
public:
    TempFixture(std::size_t fileCount, std::size_t fileBytes)
        : fileBytes_(fileBytes) {
        // FIFO·임시 파일은 반드시 로컬 파일시스템에. 소스 트리에 만들지 않는다.
        dir_ = std::filesystem::temp_directory_path() /
               ("system_fault_tests_" + std::to_string(::getpid()));
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
    const std::filesystem::path&    Dir()   const { return dir_; }
    std::size_t   FileBytes()   const { return fileBytes_; }
    std::size_t   FileCount()   const { return paths_.size(); }
    std::uint64_t ExpectedSum() const { return expectedSum_; }
    std::uint64_t ExpectedBytes() const {
        return static_cast<std::uint64_t>(paths_.size()) * fileBytes_;
    }

private:
    std::filesystem::path    dir_;
    std::vector<std::string> paths_;
    std::size_t              fileBytes_   = 0;
    std::uint64_t            expectedSum_ = 0;
};

struct CycleOutcome {
    bool          threw = false;
    std::string   what;
    std::uint64_t bytes = 0;
    std::uint64_t sum   = 0;
};

CycleOutcome RunOne(skl::IIoBackend& backend, const TempFixture& fixture) {
    CycleOutcome out;
    try {
        out.bytes = backend.RunCycle(
            fixture.Paths(), fixture.FileBytes(),
            [&out](const unsigned char* data, std::size_t size) {
                for (std::size_t i = 0; i < size; ++i) out.sum += data[i];
            });
    } catch (const std::exception& e) {
        out.threw = true;
        out.what  = e.what();
    }
    return out;
}

// 주입을 푼 상태에서 한 사이클이 정상값을 내는지 — 오류 처리가 backend 를
// 사용 불가 상태로 남기지 않았다는 확인.
void CheckHealthyAfterFault(skl::IIoBackend& backend, const TempFixture& fixture,
                             const char* label) {
    const CycleOutcome ok = RunOne(backend, fixture);
    if (ok.threw) {
        std::cerr << label << ": recovery cycle threw: " << ok.what << '\n';
        failures.fetch_add(1);
        return;
    }
    CHECK(ok.bytes == fixture.ExpectedBytes());
    CHECK(ok.sum == fixture.ExpectedSum());
}

std::unique_ptr<skl::IIoBackend> MakeExact(skl::BackendType type) {
    skl::BackendType actual = type;
    auto backend = skl::CreateBackend(type, &actual);
    CHECK(actual == type);
    return backend;
}

// ---------------------------------------------------------------------------
// 0. seam 자체의 계약
//    이게 깨지면 아래 모든 테스트가 조용히 무의미해지므로 먼저 못 박는다.
// ---------------------------------------------------------------------------

void TestFaultSpecSemantics() {
    CHECK(!skl::FaultsArmed());
    CHECK(skl::SklFaultCheck("read") == 0);

    // skip=2 → 처음 두 번은 통과, 세 번째부터 주입. times=1 → 한 번만.
    CHECK(skl::SetFaultSpec("read:2:EIO:1"));
    CHECK(skl::FaultsArmed());
    CHECK(skl::SklFaultCheck("read") == 0);
    CHECK(skl::SklFaultCheck("read") == 0);
    CHECK(skl::SklFaultCheck("read") == EIO);
    CHECK(skl::SklFaultCheck("read") == 0);
    CHECK(skl::FaultCallCount("read") == 4);
    CHECK(skl::FaultInjectionCount("read") == 1);
    // 다른 op 는 영향받지 않는다.
    CHECK(skl::SklFaultCheck("pread") == 0);
    CHECK(skl::FaultCallCount("pread") == 0);

    skl::ClearFaults();
    CHECK(!skl::FaultsArmed());
    CHECK(skl::SklFaultCheck("read") == 0);
    CHECK(skl::FaultCallCount("read") == 0);

    // 기본값: skip 0, EIO, 무제한.
    CHECK(skl::SetFaultSpec("open"));
    CHECK(skl::SklFaultCheck("open") == EIO);
    CHECK(skl::SklFaultCheck("open") == EIO);
    CHECK(skl::FaultInjectionCount("open") == 2);

    // 여러 규칙 + errno 이름.
    CHECK(skl::SetFaultSpec("open:1:EACCES:1,pread:0:EINTR:2"));
    CHECK(skl::SklFaultCheck("open") == 0);
    CHECK(skl::SklFaultCheck("open") == EACCES);
    CHECK(skl::SklFaultCheck("pread") == EINTR);
    CHECK(skl::SklFaultCheck("pread") == EINTR);
    CHECK(skl::SklFaultCheck("pread") == 0);

    // 잘못된 spec 은 거부되고 *기존 설정까지 해제* 된다 — 절반만 적용된
    // 상태를 만들지 않는다.
    CHECK(!skl::SetFaultSpec("read:notanumber:EIO"));
    CHECK(!skl::FaultsArmed());
    CHECK(!skl::SetFaultSpec("read:0:ENOSUCHERRNO"));
    CHECK(!skl::SetFaultSpec(":0:EIO"));
    CHECK(!skl::SetFaultSpec("read:0:EIO:1:2"));
    CHECK(!skl::SetFaultSpec("read:18446744073709551616:EIO"));
    CHECK(!skl::SetFaultSpec("read:0:EIO:18446744073709551616"));
    CHECK(!skl::SetFaultSpec("read:0:18446744073709551621"));
    CHECK(!skl::FaultsArmed());
    skl::ClearFaults();
    std::cout << "  spec 'read:2:EIO:1' -> pass,pass,EIO,pass (calls=4 "
                 "injections=1); 7 malformed specs rejected and disarmed\n";
}

// ---------------------------------------------------------------------------
// 1. open 실패 — 모든 backend 가 이미 연 fd 를 되돌려 놓아야 한다.
// ---------------------------------------------------------------------------

// snippet-begin: open-failure-fd
void CheckOpenFailureReleasesFds(const skl::BackendType* types,
                                 std::size_t count) {
    TempFixture fixture(4, 64 * 1024);

    for (std::size_t idx = 0; idx < count; ++idx) {
        const auto type = types[idx];
        if (!skl::BackendCompiledIn(type)) continue;
        auto backend = MakeExact(type);
        const char* label = skl::kBackendTypeLabels[static_cast<int>(type)];

        const std::size_t fdBefore = OpenFdCount();
        {
            // 세 번째 파일의 open 만 실패. aio/uring 은 그 시점에 fd 두 개를
            // 이미 들고 있다.
            ScopedFault fault("open:2:EACCES:1");
            const CycleOutcome bad = RunOne(*backend, fixture);
            CHECK(bad.threw);
            CHECK(Contains(bad.what, "open failed"));
            CHECK(skl::FaultInjectionCount("open") == 1);
        }
        CHECK(OpenFdCount() == fdBefore);
        CheckHealthyAfterFault(*backend, fixture, label);
        CHECK(OpenFdCount() == fdBefore);
        std::cout << "  " << label << ": open failed at file 3, fds back to "
                  << fdBefore << ", next cycle healthy\n";
    }
}
// snippet-end: open-failure-fd

void TestReadOpenFailure() {
    CheckOpenFailureReleasesFds(kReadBackend, 1);
}

void TestSyncBackendsOpenFailure() {
    CheckOpenFailureReleasesFds(kSyncBackends, 2);
}

void TestAsyncBackendsOpenFailure() {
    CheckOpenFailureReleasesFds(kAsyncBackends, 2);
}

// ---------------------------------------------------------------------------
// 2. EINTR 재시도 — 정규 파일로는 절대 재현되지 않는 분기.
//    시그널 대신 seam 으로 강제하고, 결과가 무주입 실행과 완전히 같아야 한다.
// ---------------------------------------------------------------------------

void CheckEintrRetryIsTransparent(skl::BackendType type, const char* op) {
    TempFixture fixture(4, 64 * 1024);
    const std::string spec = std::string(op) + ":0:EINTR:3";

    auto backend = MakeExact(type);
    const std::size_t fdBefore = OpenFdCount();
    {
        ScopedFault fault(spec);
        const CycleOutcome out = RunOne(*backend, fixture);
        CHECK(!out.threw);
        CHECK(out.bytes == fixture.ExpectedBytes());
        CHECK(out.sum == fixture.ExpectedSum());
        CHECK(skl::FaultInjectionCount(op) == 3);
    }
    CHECK(OpenFdCount() == fdBefore);
    std::cout << "  " << op << ": 3 injected EINTR retried, bytes="
              << fixture.ExpectedBytes() << " unchanged\n";
}

void TestReadEintrRetry() {
    CheckEintrRetryIsTransparent(skl::BackendType::Read, "read");
}

void TestPreadEintrRetry() {
    CheckEintrRetryIsTransparent(skl::BackendType::Pread, "pread");
}

// ---------------------------------------------------------------------------
// 3. mmap backend 의 fstat 크기 가드 / mmap·munmap 실패
// ---------------------------------------------------------------------------

void TestMmapErrorPaths() {
    TempFixture fixture(4, 64 * 1024);
    auto backend = MakeExact(skl::BackendType::Mmap);
    const std::size_t fdBefore = OpenFdCount();

    {
        // fstat 실패는 여전히 오류다. (파일이 fileBytes 보다 *짧은* 것은
        // 더 이상 오류가 아니다 — io_backend.h 의 short read 계약. 그쪽은
        // system_io_path_tests 의 CheckShortReadContract 가 본다.)
        ScopedFault fault("fstat:0:EIO:1");
        const CycleOutcome bad = RunOne(*backend, fixture);
        CHECK(bad.threw);
        CHECK(Contains(bad.what, "fstat failed"));
        CHECK(skl::FaultInjectionCount("fstat") == 1);
    }
    CHECK(OpenFdCount() == fdBefore);

    {
        ScopedFault fault("mmap:0:ENOMEM:1");
        const CycleOutcome bad = RunOne(*backend, fixture);
        CHECK(bad.threw);
        CHECK(Contains(bad.what, "mmap failed"));
    }
    CHECK(OpenFdCount() == fdBefore);

    {
        // 주입이 실제 unmap을 건너뛰어도 정리 경로는 매핑과 fd를 회수한다.
        ScopedFault fault("munmap:0:EINVAL:1");
        const CycleOutcome bad = RunOne(*backend, fixture);
        CHECK(bad.threw);
        CHECK(Contains(bad.what, "munmap failed"));
    }
    CHECK(OpenFdCount() == fdBefore);

    CheckHealthyAfterFault(*backend, fixture, "mmap");
    CHECK(OpenFdCount() == fdBefore);
    std::cout << "  mmap: fstat/mmap/munmap each failed once -> throw with a "
                 "named cause, fds stay at " << fdBefore
              << ", next cycle healthy\n";
}

// ---------------------------------------------------------------------------
// 4. POSIX AIO — aio_read 제출 실패, aio_suspend EINTR, aio_suspend 실패 후
//    cancel + polling 수확.
// ---------------------------------------------------------------------------

void TestAioErrorPaths() {
    if (!skl::BackendCompiledIn(skl::BackendType::Aio)) {
        std::cout << "  aio not compiled in — skipped\n";
        return;
    }
    TempFixture fixture(4, 64 * 1024);
    auto backend = MakeExact(skl::BackendType::Aio);
    const std::size_t fdBefore = OpenFdCount();

    {
        // aio_suspend 가 EINTR 를 돌려주면 그냥 다시 기다려야 한다.
        ScopedFault fault("aio_suspend:0:EINTR:2");
        const CycleOutcome out = RunOne(*backend, fixture);
        CHECK(!out.threw);
        CHECK(out.bytes == fixture.ExpectedBytes());
        CHECK(out.sum == fixture.ExpectedSum());
        CHECK(skl::FaultInjectionCount("aio_suspend") == 2);
    }
    CHECK(OpenFdCount() == fdBefore);

    {
        // 세 번째 aio_read 제출 실패 → 앞의 두 요청을 취소하고 *전부 수확한 뒤*
        // 던져야 한다. 수확 없이 반환하면 곧 해제될 버퍼로 커널이 계속 쓴다.
        ScopedFault fault("aio_read:2:EAGAIN:1");
        const CycleOutcome bad = RunOne(*backend, fixture);
        CHECK(bad.threw);
        CHECK(Contains(bad.what, "aio_read failed"));
    }
    CHECK(OpenFdCount() == fdBefore);
    CheckHealthyAfterFault(*backend, fixture, "aio");

    {
        // aio_suspend 자체가 못 쓰게 되면 취소 요청 후 aio_error polling 으로
        // 수확을 끝내야 한다. 여기서 멈추면(수확 루프가 끝나지 않으면)
        // ctest TIMEOUT 이 잡는다.
        ScopedFault fault("aio_suspend:0:EBADF");
        const CycleOutcome bad = RunOne(*backend, fixture);
        CHECK(bad.threw);
        CHECK(Contains(bad.what, "aio_suspend failed"));
        CHECK(skl::FaultInjectionCount("aio_suspend") == 1);
    }
    CHECK(OpenFdCount() == fdBefore);
    CheckHealthyAfterFault(*backend, fixture, "aio");
    CHECK(OpenFdCount() == fdBefore);
    std::cout << "  aio: suspend EINTR retried x2, submit failure harvested "
                 "before throw, suspend EBADF drained by polling; fds stay at "
              << fdBefore << '\n';
}

// ---------------------------------------------------------------------------
// 5. io_uring — 링 재초기화가 필요한 경로 전부.
//    각 경우마다 "다음 사이클이 정상" 을 요구한다. 이게 링을 실제로 되살렸는지
//    보는 유일한 관측점이다.
// ---------------------------------------------------------------------------

void TestUringErrorPaths() {
    if (!skl::BackendCompiledIn(skl::BackendType::Uring)) {
        std::cout << "  io_uring not compiled in — skipped\n";
        return;
    }
    TempFixture fixture(4, 64 * 1024);
    auto backend = MakeExact(skl::BackendType::Uring);
    const std::size_t fdBefore = OpenFdCount();

    {
        ScopedFault fault("io_uring_wait_cqe:0:EINTR:2");
        const CycleOutcome out = RunOne(*backend, fixture);
        CHECK(!out.threw);
        CHECK(out.bytes == fixture.ExpectedBytes());
        CHECK(out.sum == fixture.ExpectedSum());
        CHECK(skl::FaultInjectionCount("io_uring_wait_cqe") == 2);
    }
    CHECK(OpenFdCount() == fdBefore);

    {
        // SQ 가 가득 차 SQE 를 못 얻는 상황. 이미 준비된 SQE 는 곧 사라질
        // 버퍼를 가리키므로 링을 버려야 한다.
        ScopedFault fault("io_uring_get_sqe:2:EIO:1");
        const CycleOutcome bad = RunOne(*backend, fixture);
        CHECK(bad.threw);
        CHECK(Contains(bad.what, "get_sqe returned null"));
    }
    CHECK(OpenFdCount() == fdBefore);
    CheckHealthyAfterFault(*backend, fixture, "uring/get_sqe");

    {
        // submit 음수 반환. 준비된 SQE 가 링에 남은 채이므로 재초기화 필수.
        ScopedFault fault("io_uring_submit:0:EAGAIN:1");
        const CycleOutcome bad = RunOne(*backend, fixture);
        CHECK(bad.threw);
        CHECK(Contains(bad.what, "io_uring_submit failed"));
    }
    CHECK(OpenFdCount() == fdBefore);
    CheckHealthyAfterFault(*backend, fixture, "uring/submit");

    {
        // 부분 제출. 실제로는 n 개가 in-flight 인데 n-1 만 보고된다 —
        // 남은 CQE 를 링과 함께 버리지 않으면 다음 사이클이 오염된다.
        ScopedFault fault("io_uring_submit_short:0:EIO:1");
        const CycleOutcome bad = RunOne(*backend, fixture);
        CHECK(bad.threw);
        CHECK(Contains(bad.what, "fewer SQEs"));
    }
    CHECK(OpenFdCount() == fdBefore);
    CheckHealthyAfterFault(*backend, fixture, "uring/short-submit");

    {
        // 손상된 user_data. 인덱스로 쓰면 범위 밖 접근이 된다.
        ScopedFault fault("uring_user_data:0:EIO");
        const CycleOutcome bad = RunOne(*backend, fixture);
        CHECK(bad.threw);
        CHECK(Contains(bad.what, "invalid user_data"));
    }
    CHECK(OpenFdCount() == fdBefore);
    CheckHealthyAfterFault(*backend, fixture, "uring/user_data");
    CHECK(OpenFdCount() == fdBefore);
    std::cout << "  io_uring: wait EINTR retried x2; get_sqe/submit/short-submit"
                 "/user_data each rebuilt the ring and the next cycle was "
                 "healthy; fds stay at " << fdBefore << '\n';
}

// ---------------------------------------------------------------------------
// 6. factory 의 uring 런타임 폴백.
//    커널이 io_uring 을 막은 환경(ENOSYS/EPERM)을 seam 으로 재현한다.
// ---------------------------------------------------------------------------

void TestFactoryUringRuntimeFallback() {
    if (!skl::BackendCompiledIn(skl::BackendType::Uring)) {
        std::cout << "  io_uring not compiled in — fallback path skipped\n";
        return;
    }
    TempFixture fixture(4, 64 * 1024);
    const std::size_t fdBefore = OpenFdCount();

    {
        ScopedFault fault("io_uring_queue_init:0:ENOSYS:1");
        skl::BackendType actual = skl::BackendType::Uring;
        auto backend = skl::CreateBackend(skl::BackendType::Uring, &actual);
        CHECK(backend != nullptr);
        CHECK(actual == skl::BackendType::Read);
        CHECK(skl::FaultInjectionCount("io_uring_queue_init") == 1);
        // 폴백 backend 는 그대로 쓸 수 있어야 한다.
        const CycleOutcome out = RunOne(*backend, fixture);
        CHECK(!out.threw);
        CHECK(out.bytes == fixture.ExpectedBytes());
        CHECK(out.sum == fixture.ExpectedSum());
    }
    CHECK(OpenFdCount() == fdBefore);

    // 주입이 없으면 폴백하지 않는다 — 위 CHECK 가 항등식이 아님을 보인다.
    {
        skl::BackendType actual = skl::BackendType::Read;
        auto backend = skl::CreateBackend(skl::BackendType::Uring, &actual);
        CHECK(actual == skl::BackendType::Uring);
        CHECK(backend != nullptr);
    }
    CHECK(OpenFdCount() == fdBefore);
    std::cout << "  factory: queue_init ENOSYS -> actual=read (usable), "
                 "no injection -> actual=io_uring\n";
}

// ---------------------------------------------------------------------------
// 7. 스케줄링 정책 — 실패 폴백과 (권한이 있으면) 성공 경로.
//    무권한 기기: "권한이 없을 때 false 를 돌려주고 스레드는 계속 동작한다"
//    를 검증하고, 성공 경로는 skip 사유를 stdout 에 남긴다.
//    유권한 기기: SCHED_FIFO 는 실제 적용을 커널에 되물어 확인하고, cgroup 은
//    SKL_CGROUP_DIR 로 승인된 일회용 cgroup 에 실제 쓰기 + 복구까지 밟는다.
//    SKL_REQUIRE_SCHED 가 성공 경로를 *요구* 로 바꾼다 — 권한을 붙였는데
//    조용히 폴백으로 새면 실패다 (SKL_REQUIRE_BACKENDS 와 같은 패턴).
// ---------------------------------------------------------------------------

// SKL_REQUIRE_SCHED — "fifo", "cgroup" 을 ',' 로 나열. 목록에 있는 정책의
// 성공 경로가 skip 되거나 폴백하면 테스트가 실패한다. 미설정 = 폴백 허용
// (이식성 모드). 오타를 조용히 넘기면 요구 모드가 아무것도 강제하지 않은 채
// 초록이 되므로, 모르는 이름은 실패로 센다.
bool SchedRequired(const char* name) {
    static const std::vector<std::string> tokens = [] {
        std::vector<std::string> out;
        const char* env = std::getenv("SKL_REQUIRE_SCHED");
        if (env == nullptr) return out;
        const std::string spec(env);
        std::size_t pos = 0;
        while (pos <= spec.size()) {
            const std::size_t comma = spec.find(',', pos);
            const std::size_t end =
                (comma == std::string::npos) ? spec.size() : comma;
            std::string token = spec.substr(pos, end - pos);
            token.erase(std::remove_if(token.begin(), token.end(),
                                       [](unsigned char c) {
                                           return std::isspace(c) != 0;
                                       }),
                        token.end());
            if (!token.empty()) {
                if (token == "fifo" || token == "cgroup") {
                    out.push_back(token);
                } else {
                    std::cerr << "CHECK failed: unknown sched name in "
                                 "SKL_REQUIRE_SCHED: '" << token << "'\n";
                    failures.fetch_add(1);
                }
            }
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        return out;
    }();
    return std::find(tokens.begin(), tokens.end(), name) != tokens.end();
}

std::string DetectCgroupV2Path() {
    std::ifstream f("/proc/self/cgroup");
    if (!f) return {};
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("0::", 0) != 0) continue;
        const std::string rel = line.substr(3);
        return (rel.empty() || rel == "/") ? std::string("/sys/fs/cgroup")
                                            : std::string("/sys/fs/cgroup") + rel;
    }
    return {};
}

void TestSchedThreadPolicyFallback() {
    const bool requireFifo = SchedRequired("fifo");
    // 이 스레드의 nice 를 바꾸므로 메인 스레드가 아니라 전용 스레드에서.
    std::thread worker([requireFifo] {
        CHECK(skl::ApplySchedToCurrentThread(skl::SchedType::Default));

        {
            ScopedFault fault("setpriority:0:EACCES");
            CHECK(!skl::ApplySchedToCurrentThread(skl::SchedType::Nice));
            CHECK(skl::FaultInjectionCount("setpriority") == 1);
        }
        // 무주입: nice 를 *올리는* 것은 권한이 필요 없다 → 성공 경로 대조군.
        CHECK(skl::ApplySchedToCurrentThread(skl::SchedType::Nice));

        {
            ScopedFault fault("pthread_setschedparam:0:EPERM");
            CHECK(!skl::ApplySchedToCurrentThread(skl::SchedType::Fifo));
            CHECK(skl::FaultInjectionCount("pthread_setschedparam") == 1);
        }
        const bool fifoOk = skl::ApplySchedToCurrentThread(skl::SchedType::Fifo);
        std::cout << "  SCHED_FIFO applied without injection: "
                  << (fifoOk ? "yes (privileged)" : "no (expected without CAP_SYS_NICE)")
                  << '\n';
        if (fifoOk) {
            // 성공을 주장했으면 커널에 되물어 실제로 걸렸는지 확인한다 —
            // true 를 돌려주고 정책을 안 바꾸는 구현이 여기서 잡힌다.
            int         policy = -1;
            sched_param param{};
            CHECK(::pthread_getschedparam(::pthread_self(), &policy, &param)
                  == 0);
            CHECK(policy == SCHED_FIFO);
            CHECK(param.sched_priority == 50);
        } else if (requireFifo) {
            // 권한을 붙인 기기에서 조용히 폴백으로 새는 것을 막는다.
            std::cerr << "CHECK failed: SKL_REQUIRE_SCHED demands fifo but "
                         "SCHED_FIFO was not applied (check CAP_SYS_NICE / "
                         "ulimit -r >= 50)\n";
            failures.fetch_add(1);
        }

        // 주입: Default 의 nice 복원이 실패하면 false 가 나와야 한다.
        // 아래 무주입 복원은 권한에 따라 둘 다 나올 수 있으므로, 모든 기기에서
        // 같은 답을 내는 실패 단언을 여기서 한 번 박아 둔다.
        {
            ScopedFault fault("setpriority:0:EACCES");
            CHECK(!skl::ApplySchedToCurrentThread(skl::SchedType::Default));
            CHECK(skl::FaultInjectionCount("setpriority") == 1);
        }

        // 실패했든 성공했든 스레드는 살아 있고 기본으로 돌아갈 수 있어야 한다.
        // 다만 nice +10 → 0 은 *우선순위를 올리는* 방향이라 CAP_SYS_NICE 나
        // RLIMIT_NICE 여유가 없는 기기에서는 EACCES 로 실패한다 — 그때는 false 가
        // 정상이다. 반환값을 그냥 통과시키면(예전 코드) 무조건 true 를 돌려주는
        // 구현도 통과하므로, getpriority 로 커널에 되물어 반환값과 실제 nice 가
        // 서로 맞는지를 단언한다 — true 면 0, false 면 +10 이 그대로 남아 있어야 한다.
        errno = 0;
        const int niceBefore = ::getpriority(PRIO_PROCESS, 0);
        CHECK(errno == 0);
        CHECK(niceBefore == 10);
        const bool defaultOk =
            skl::ApplySchedToCurrentThread(skl::SchedType::Default);
        errno = 0;
        const int niceAfter = ::getpriority(PRIO_PROCESS, 0);
        CHECK(errno == 0);
        CHECK(niceAfter == (defaultOk ? 0 : 10));
        std::cout << "  Default nice reset: " << niceBefore << " -> "
                  << niceAfter
                  << (defaultOk ? " (restored)"
                                : " (EACCES; nice +10 kept, false returned)")
                  << '\n';
        {
            // 원상 복원 확인 — FIFO 가 성공했던 경우에도 SCHED_OTHER 로
            // 돌아와야 뒤의 검사가 실시간 우선순위를 물려받지 않는다.
            int         policy = -1;
            sched_param param{};
            CHECK(::pthread_getschedparam(::pthread_self(), &policy, &param)
                  == 0);
            CHECK(policy == SCHED_OTHER);
        }
        std::cout << "  SCHED_FIFO success path: "
                  << (fifoOk ? "verified via pthread_getschedparam, then "
                               "restored to SCHED_OTHER"
                             : "skipped (no privilege; fallback verified)")
                  << (requireFifo ? " [required by SKL_REQUIRE_SCHED]" : "")
                  << '\n';
    });
    worker.join();
}

// cpu.weight 파일의 첫 줄. 제품 코드의 ReadFirstLine 과 달리 seam 을 지나지
// 않는다 — 테스트가 "파일의 진짜 값" 을 볼 때 쓴다.
std::string ReadWeightLine(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    if (f && std::getline(f, line)) return line;
    return {};
}

void TestSchedCgroupFallback() {
    const bool requireCgroup = SchedRequired("cgroup");

    // 사용자가 export 해 둔 승인 경로를 먼저 복사해 둔다 — (1)(2)가 env 를
    // 밀어 버린다. 실제 쓰기 단계 (6) 은 이 값이 검출 경로와 일치할 때만 돈다.
    // 테스트가 스스로 승인하지 않는다 — 공유 cgroup 을 실수로 바꾸지 않기 위한
    // opt-in 계약(sched_policy.cpp 의 SKL_CGROUP_DIR 게이트)을 그대로 따른다.
    const char*       userDirEnv = std::getenv("SKL_CGROUP_DIR");
    const bool        hadUserDir = userDirEnv != nullptr;
    const std::string userDir    = hadUserDir ? userDirEnv : "";

    // (1) 승인 환경변수가 없으면 현재 cgroup 을 건드리지 않고 거부한다.
    ::unsetenv("SKL_CGROUP_DIR");
    CHECK(!skl::ApplySchedProcessSetup(skl::SchedType::Cgroup));

    // (2) 승인 경로가 실제 cgroup 과 다르면 역시 거부한다.
    ::setenv("SKL_CGROUP_DIR", "/sys/fs/cgroup/skl-not-our-cgroup", 1);
    CHECK(!skl::ApplySchedProcessSetup(skl::SchedType::Cgroup));
    ::unsetenv("SKL_CGROUP_DIR");

    const std::string dir = DetectCgroupV2Path();
    const std::string weightPath = dir.empty() ? std::string()
                                                : dir + "/cpu.weight";
    if (dir.empty()) {
        std::cout << "  cgroup v2 path not detected — read/write fallback skipped\n";
        std::cout << "  cgroup success path: skipped (no cgroup v2 path)\n";
        if (requireCgroup) {
            std::cerr << "CHECK failed: SKL_REQUIRE_SCHED demands cgroup but "
                         "no cgroup v2 path was detected\n";
            failures.fetch_add(1);
        }
        return;
    }

    const std::string original = ReadWeightLine(weightPath);
    const bool        readable = !original.empty();
    const bool        writable = ::access(weightPath.c_str(), W_OK) == 0;

    // (3)(4)의 주입 케이스는 쓰기 가능한 기기에서도 안전하다 — cgroup_write
    // seam 은 주입 시 파일을 아예 열지 않으므로 실제 값이 바뀔 수 없다.
    ::setenv("SKL_CGROUP_DIR", dir.c_str(), 1);
    {
        // (3) 원래 값 읽기 실패 → 아무것도 쓰지 않고 기본 유지.
        // cgroup_write 는 관측 전용(skip 이 커서 주입하지 않음)으로 함께 무장한다.
        // 무장하지 않으면 아래 FaultCallCount 가 seam 방문과 무관하게 0 이라
        // 검사가 절대 실패할 수 없다.
        ScopedFault fault("cgroup_read:0:EACCES,cgroup_write:100000000");
        CHECK(!skl::ApplySchedProcessSetup(skl::SchedType::Cgroup));
        CHECK(skl::FaultInjectionCount("cgroup_read") >= 1);
        CHECK(skl::FaultCallCount("cgroup_write") == 0);
    }
    {
        // (4) 읽기 주입을 푼 재시도 — 이번에는 캡처가 성공해 cgroup_write
        //     seam 까지 도달해야 하고, 쓰기 주입 실패 → 기본 유지 폴백이 돈다.
        //     CaptureOriginalCgroupWeight 가 (3)의 읽기 실패를 captured 로
        //     캐시하면 재캡처를 건너뛰어 여기 절대 도달하지 못한다 — 그 회귀
        //     (정적 캐시 오염)를 아래의 reachedWrite CHECK 가 잡는다.
        ScopedFault fault("cgroup_write:0:EACCES");
        CHECK(!skl::ApplySchedProcessSetup(skl::SchedType::Cgroup));
        const bool reachedWrite = skl::FaultCallCount("cgroup_write") > 0;
        std::cout << "  cgroup write seam reached: "
                  << (reachedWrite ? "yes" : "no (cpu.weight unreadable here)")
                  << '\n';
        if (readable) {
            // cpu.weight 를 읽을 수 있는 환경에서는 도달이 *보장* 되어야 한다.
            CHECK(reachedWrite);
        }
        if (reachedWrite) CHECK(skl::FaultInjectionCount("cgroup_write") >= 1);
    }
    // (5) 쓰기가 한 번도 성공하지 않았으므로 복구는 no-op 으로 성공해야 한다.
    CHECK(skl::ApplySchedProcessSetup(skl::SchedType::Default));
    ::unsetenv("SKL_CGROUP_DIR");

    // (6) 실제 쓰기 성공 + modified==true 복구. 위임된 일회용 cgroup 을
    //     사용자가 SKL_CGROUP_DIR 로 명시 승인했을 때만 돈다
    //     (docs/LINUX_HANDOFF.md §3-1). 승인 없이 쓰기 가능한 상황에서는
    //     실제 cgroup 을 바꿀 위험이 있으므로 여기서도 건드리지 않는다.
    const bool approved = hadUserDir && userDir == dir;
    if (writable && approved && readable) {
        ::setenv("SKL_CGROUP_DIR", dir.c_str(), 1);
        {
            // 관측 모드(skip 을 아주 크게 잡은 규칙): 아무것도 주입하지 않고
            // 쓰기 seam 방문 횟수만 센다 — 원래 값이 우연히 200 이어도 실제
            // 쓰기가 일어났음을 값 비교와 독립적으로 확인한다.
            ScopedFault observe("cgroup_write:100000000");
            CHECK(skl::ApplySchedProcessSetup(skl::SchedType::Cgroup));
            CHECK(skl::FaultCallCount("cgroup_write") == 1);
            CHECK(skl::FaultInjectionCount("cgroup_write") == 0);
            const std::string during = ReadWeightLine(weightPath);
            CHECK(during == "200");

            // modified==true 복구 경로 — 원래 값이 되돌아와야 한다.
            CHECK(skl::ApplySchedProcessSetup(skl::SchedType::Default));
            CHECK(skl::FaultCallCount("cgroup_write") == 2);
            const std::string after = ReadWeightLine(weightPath);
            CHECK(after == original);
            std::cout << "  cgroup real write+restore: orig=" << original
                      << " during=" << during << " after=" << after << '\n';
        }
        // 복구 뒤 재복구는 no-op 으로 성공한다 (modified 가 내려갔다).
        CHECK(skl::ApplySchedProcessSetup(skl::SchedType::Default));
        ::unsetenv("SKL_CGROUP_DIR");
        std::cout << "  cgroup success path: ran (delegated cgroup approved "
                     "via SKL_CGROUP_DIR)"
                  << (requireCgroup ? " [required by SKL_REQUIRE_SCHED]" : "")
                  << '\n';
    } else {
        std::cout << "  cgroup success path: skipped ("
                  << (!writable
                          ? "cpu.weight not writable"
                          : !approved
                                ? "SKL_CGROUP_DIR does not approve this cgroup"
                                : "cpu.weight unreadable")
                  << ") — see docs/LINUX_HANDOFF.md §3-1\n";
        if (requireCgroup) {
            std::cerr << "CHECK failed: SKL_REQUIRE_SCHED demands cgroup but "
                         "the real write+restore path was skipped\n";
            failures.fetch_add(1);
        }
    }

    // 사용자 export 를 되돌려 놓는다 — 이 테스트가 env 를 소비한 채 끝나지
    // 않는다.
    if (hadUserDir) ::setenv("SKL_CGROUP_DIR", userDir.c_str(), 1);
}

// ---------------------------------------------------------------------------
// 8. 워커에서 난 오류가 메인으로 전달되고, 풀이 그대로 재사용 가능한가.
//    워커가 예외를 삼킨 뒤 remaining_ 회계를 어긋나게 두면 다음 RunCycle 이
//    영원히 끝나지 않는다 → ctest TIMEOUT 이 잡는다.
// ---------------------------------------------------------------------------

void CheckWorkloadRecovers(skl::CtxType ctx, const char* label) {
    constexpr std::size_t kFileCount = 8;
    constexpr std::size_t kFileBytes = 4096;

    const auto dir = std::filesystem::temp_directory_path() /
                     ("system_fault_tests_wl_" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);

    skl::FileIoWorkload workload;
    workload.Init(skl::BackendType::Read, skl::SyncType::Mutex,
                  skl::SchedType::Default, ctx,
                  4, kFileCount, kFileBytes, dir);

    const std::uint64_t expectedBytes    = workload.RunCycle();
    const std::uint64_t expectedChecksum = workload.LastChecksum();
    CHECK(expectedBytes == kFileCount * kFileBytes);

    const std::size_t fdBefore = OpenFdCount();
    {
        ScopedFault fault("open:2:EACCES:1");
        bool threw = false;
        try {
            workload.RunCycle();
        } catch (const std::exception&) {
            threw = true;
        }
        if (!threw) {
            std::cerr << label << ": worker error was not propagated\n";
            failures.fetch_add(1);
        }
    }

    // 주입을 푼 다음 사이클이 기준값과 같아야 한다.
    CHECK(workload.RunCycle() == expectedBytes);
    CHECK(workload.LastChecksum() == expectedChecksum);
    CHECK(OpenFdCount() == fdBefore);

    workload.Shutdown();
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
    std::cout << "  " << label
              << ": worker error propagated, pool reusable, bytes="
              << expectedBytes << " checksum=" << expectedChecksum << '\n';
}

void TestWorkerPoolErrorPropagation() {
    CheckWorkloadRecovers(skl::CtxType::Pthread, "pthread pool");
}

void TestCoroutinePoolErrorPropagation() {
    CheckWorkloadRecovers(skl::CtxType::Coroutine, "coroutine pool");
}

// ---------------------------------------------------------------------------
// 9. 성공 경로 fd 누수 — 오류가 없어도 사이클을 반복하면 드러난다.
// ---------------------------------------------------------------------------

void CheckRepeatedCyclesLeakNoFds(const skl::BackendType* types,
                                  std::size_t count) {
    TempFixture fixture(4, 4096);

    for (std::size_t idx = 0; idx < count; ++idx) {
        const auto type = types[idx];
        if (!skl::BackendCompiledIn(type)) continue;
        auto backend = MakeExact(type);

        // 첫 사이클에서 지연 초기화가 끝나도록 한 번 돌린 뒤 기준을 잡는다.
        CHECK(!RunOne(*backend, fixture).threw);
        const std::size_t fdBefore = OpenFdCount();
        for (int i = 0; i < 20; ++i) {
            const CycleOutcome out = RunOne(*backend, fixture);
            CHECK(!out.threw);
            CHECK(out.bytes == fixture.ExpectedBytes());
        }
        CHECK(OpenFdCount() == fdBefore);
        std::cout << "  " << skl::kBackendTypeLabels[static_cast<int>(type)]
                  << ": 20 clean cycles, open fds " << fdBefore << " -> "
                  << OpenFdCount() << '\n';
    }
}

void TestReadRepeatedCycles() {
    CheckRepeatedCyclesLeakNoFds(kReadBackend, 1);
}

void TestSyncBackendsRepeatedCycles() {
    CheckRepeatedCyclesLeakNoFds(kSyncBackends, 2);
}

void TestAsyncBackendsRepeatedCycles() {
    CheckRepeatedCyclesLeakNoFds(kAsyncBackends, 2);
}

// 검사 ↔ step 대응표. 소속은 *그 step 이 무엇을 추가했는가* 로 정한다 —
// backend 오류 경로는 그 backend 를 얹은 step, 실행 모델의 오류 전파는 그
// 실행 모델을 얹은 step, seam 자체의 계약은 seam 을 세운 step 이다.
const skl_test::StepCase kCases[] = {
    {2, "read: open failure releases fds",   &TestReadOpenFailure},
    {2, "read: EINTR retry transparency",    &TestReadEintrRetry},
    {2, "read: repeated cycles leak no fds", &TestReadRepeatedCycles},
    {3, "pread/mmap: open failure releases fds",
     &TestSyncBackendsOpenFailure},
    {3, "pread: EINTR retry transparency",   &TestPreadEintrRetry},
    {3, "mmap error paths",                  &TestMmapErrorPaths},
    {3, "pread/mmap: repeated cycles leak no fds",
     &TestSyncBackendsRepeatedCycles},
    {4, "aio/io_uring: open failure releases fds",
     &TestAsyncBackendsOpenFailure},
    {4, "aio error paths",                   &TestAioErrorPaths},
    {4, "io_uring error paths",              &TestUringErrorPaths},
    {4, "factory uring runtime fallback",    &TestFactoryUringRuntimeFallback},
    {4, "aio/io_uring: repeated cycles leak no fds",
     &TestAsyncBackendsRepeatedCycles},
    {5, "worker pool error propagation",     &TestWorkerPoolErrorPropagation},
    {6, "sched thread policy fallback",      &TestSchedThreadPolicyFallback},
    {6, "sched cgroup fallback",             &TestSchedCgroupFallback},
    {7, "coroutine pool error propagation",
     &TestCoroutinePoolErrorPropagation},
    {8, "fault spec semantics",              &TestFaultSpecSemantics},
};

}  // namespace

int main(int argc, char** argv) {
    return skl_test::RunStepCases(argc, argv, "system_fault_tests", kCases,
                                  sizeof(kCases) / sizeof(kCases[0]),
                                  &Failures, &skl::ClearFaults);
}
