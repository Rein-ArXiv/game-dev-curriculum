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

#include "fault/fault_inject.h"
#include "io/io_backend.h"
#include "io/io_backend_factory.h"
#include "sched/sched_policy.h"
#include "sync/sync_factory.h"
#include "workload/file_io_workload.h"

#include <atomic>
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

#include <unistd.h>

namespace {

std::atomic<int> failures{0};

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
    CHECK(!skl::FaultsArmed());
    skl::ClearFaults();
}

// ---------------------------------------------------------------------------
// 1. open 실패 — 모든 backend 가 이미 연 fd 를 되돌려 놓아야 한다.
// ---------------------------------------------------------------------------

void TestOpenFailureReleasesFds() {
    TempFixture fixture(4, 64 * 1024);

    for (int raw = 0; raw < skl::kBackendTypeCount; ++raw) {
        const auto type = static_cast<skl::BackendType>(raw);
        if (!skl::BackendCompiledIn(type)) continue;
        auto backend = MakeExact(type);

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
        CheckHealthyAfterFault(*backend, fixture, skl::kBackendTypeLabels[raw]);
        CHECK(OpenFdCount() == fdBefore);
    }
}

// ---------------------------------------------------------------------------
// 2. EINTR 재시도 — 정규 파일로는 절대 재현되지 않는 분기.
//    시그널 대신 seam 으로 강제하고, 결과가 무주입 실행과 완전히 같아야 한다.
// ---------------------------------------------------------------------------

void TestEintrRetryIsTransparent() {
    TempFixture fixture(4, 64 * 1024);

    struct Case {
        skl::BackendType type;
        const char*      spec;
        const char*      op;
    };
    const Case cases[] = {
        {skl::BackendType::Read,  "read:0:EINTR:3",  "read"},
        {skl::BackendType::Pread, "pread:0:EINTR:3", "pread"},
    };

    for (const Case& c : cases) {
        auto backend = MakeExact(c.type);
        const std::size_t fdBefore = OpenFdCount();
        {
            ScopedFault fault(c.spec);
            const CycleOutcome out = RunOne(*backend, fixture);
            CHECK(!out.threw);
            CHECK(out.bytes == fixture.ExpectedBytes());
            CHECK(out.sum == fixture.ExpectedSum());
            CHECK(skl::FaultInjectionCount(c.op) == 3);
        }
        CHECK(OpenFdCount() == fdBefore);
    }
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
        // system_io_path_tests 의 TestShortReadContract 가 본다.)
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
        // munmap 실패는 매핑을 회수하지 못한 채 던진다. fd 는 그래도 닫혀야 한다.
        ScopedFault fault("munmap:0:EINVAL:1");
        const CycleOutcome bad = RunOne(*backend, fixture);
        CHECK(bad.threw);
        CHECK(Contains(bad.what, "munmap failed"));
    }
    CHECK(OpenFdCount() == fdBefore);

    CheckHealthyAfterFault(*backend, fixture, "mmap");
    CHECK(OpenFdCount() == fdBefore);
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
}

// ---------------------------------------------------------------------------
// 7. 스케줄링 정책의 실패 폴백.
//    성공 경로(SCHED_FIFO, cgroup 쓰기)는 CAP_SYS_NICE / 위임된 cgroup 이
//    필요해 여기서 강제하지 않는다. 검증 대상은 "권한이 없을 때 false 를
//    돌려주고 스레드는 계속 동작한다" 쪽이다.
// ---------------------------------------------------------------------------

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
    // 이 스레드의 nice 를 바꾸므로 메인 스레드가 아니라 전용 스레드에서.
    std::thread worker([] {
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

        // 실패했든 성공했든 스레드는 살아 있고 기본으로 돌아갈 수 있어야 한다.
        CHECK(skl::ApplySchedToCurrentThread(skl::SchedType::Default));
    });
    worker.join();
}

void TestSchedCgroupFallback() {
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
        return;
    }
    if (::access(weightPath.c_str(), W_OK) == 0) {
        // 쓰기 권한이 있으면 이 테스트가 실제 cgroup 을 바꿀 위험이 있다.
        std::cout << "  " << weightPath
                  << " is writable — skipping (would mutate a real cgroup)\n";
        return;
    }

    // 여기부터는 cpu.weight 가 쓰기 불가임을 확인했으므로 승인 경로를 넣어도
    // 실제 값이 바뀔 수 없다.
    ::setenv("SKL_CGROUP_DIR", dir.c_str(), 1);
    {
        // (3) 원래 값 읽기 실패 → 아무것도 쓰지 않고 기본 유지.
        ScopedFault fault("cgroup_read:0:EACCES");
        CHECK(!skl::ApplySchedProcessSetup(skl::SchedType::Cgroup));
        CHECK(skl::FaultInjectionCount("cgroup_read") >= 1);
        CHECK(skl::FaultCallCount("cgroup_write") == 0);
    }
    {
        // (4) 쓰기 실패 → 기본 유지. cpu.weight 를 읽을 수 있는 환경에서만
        //     쓰기 seam 까지 도달한다.
        ScopedFault fault("cgroup_write:0:EACCES");
        CHECK(!skl::ApplySchedProcessSetup(skl::SchedType::Cgroup));
        const bool reachedWrite = skl::FaultCallCount("cgroup_write") > 0;
        std::cout << "  cgroup write seam reached: "
                  << (reachedWrite ? "yes" : "no (cpu.weight unreadable here)")
                  << '\n';
        if (reachedWrite) CHECK(skl::FaultInjectionCount("cgroup_write") >= 1);
    }
    // (5) 쓰기가 한 번도 성공하지 않았으므로 복구는 no-op 으로 성공해야 한다.
    CHECK(skl::ApplySchedProcessSetup(skl::SchedType::Default));
    ::unsetenv("SKL_CGROUP_DIR");
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
}

void TestWorkloadErrorPropagation() {
    CheckWorkloadRecovers(skl::CtxType::Pthread, "pthread pool");
    CheckWorkloadRecovers(skl::CtxType::Coroutine, "coroutine pool");
}

// ---------------------------------------------------------------------------
// 9. 성공 경로 fd 누수 — 오류가 없어도 사이클을 반복하면 드러난다.
// ---------------------------------------------------------------------------

void TestRepeatedCyclesLeakNoFds() {
    TempFixture fixture(4, 4096);

    for (int raw = 0; raw < skl::kBackendTypeCount; ++raw) {
        const auto type = static_cast<skl::BackendType>(raw);
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
    }
}

struct NamedTest {
    const char* name;
    void (*fn)();
};

}  // namespace

int main() {
    const NamedTest tests[] = {
        {"fault spec semantics",       &TestFaultSpecSemantics},
        {"open failure fd cleanup",    &TestOpenFailureReleasesFds},
        {"EINTR retry transparency",   &TestEintrRetryIsTransparent},
        {"mmap error paths",           &TestMmapErrorPaths},
        {"aio error paths",            &TestAioErrorPaths},
        {"io_uring error paths",       &TestUringErrorPaths},
        {"factory uring fallback",     &TestFactoryUringRuntimeFallback},
        {"sched thread fallback",      &TestSchedThreadPolicyFallback},
        {"sched cgroup fallback",      &TestSchedCgroupFallback},
        {"workload error propagation", &TestWorkloadErrorPropagation},
        {"repeated cycles fd leak",    &TestRepeatedCyclesLeakNoFds},
    };

    for (const NamedTest& test : tests) {
        const int before = failures.load();
        std::cout << "[ run  ] " << test.name << '\n';
        test.fn();
        skl::ClearFaults();  // 어떤 경우에도 다음 테스트로 새지 않게.
        std::cout << (failures.load() == before ? "[  ok  ] " : "[ FAIL ] ")
                  << test.name << '\n';
    }

    if (failures.load() != 0) {
        std::cerr << failures.load() << " system fault test(s) failed\n";
        return 1;
    }
    std::cout << "system fault tests passed ("
              << (sizeof(tests) / sizeof(tests[0])) << " groups)\n";
    return 0;
}
