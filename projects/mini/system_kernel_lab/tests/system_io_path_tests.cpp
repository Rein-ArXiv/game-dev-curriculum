// PROJECTS_PLAN 이 이 lab 에 지정한 세 I/O 검증 항목 전용 테스트:
// 부분 읽기(short read) · EINTR · 취소.
//
//   - system_core_tests  : "요청한 크기와 정확히 같은 정규 파일" 성공 경로만 본다.
//   - system_fault_tests : seam 주입으로 오류 분기를 결정적으로 밟는다.
//   - 이 파일           : 그 사이를 메운다.
//       (a) 파일이 요청보다 짧을 때 5개 backend 가 *같은 값* 을 낸다는 계약
//           (io_backend.h 의 short read 계약)
//       (b) 주입이 아니라 *실제 시그널* 로 발생한 EINTR 가 결과에 보이지
//           않는다는 투명성 — 그리고 재시도 분기가 정말 실행됐다는 증거
//       (c) 취소 후 모든 요청을 수확한다는 drain 규약
//
// (b) 는 정규 파일로 재현되지 않는다. FIFO 를 쓰고, FIFO 는 반드시 로컬
// 파일시스템(/tmp)에 만든다 — 9p(/mnt/c) 는 FIFO 를 지원하지 않는다.
//
// "분기가 정말 실행됐는가" 는 fault seam 을 *주입 없이* 무장해서 센다.
// skip 을 아주 크게 준 규칙("read:100000000")은 아무것도 주입하지 않고
// 호출 횟수만 세는 순수 관측 모드다.
//
// 검사들은 파일 끝의 표에서 *그것을 추가한 step* 에 묶인다. 인자 없이 부르면
// 전부 돌고, `--step=N` 은 그 step 이 추가한 검사만 돈다(tests/step_filter.h).

#include "fault/fault_inject.h"
#include "io/io_backend.h"
#include "io/io_backend_factory.h"
#include "io/uring_backend.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include "step_filter.h"

namespace {

std::atomic<int> failures{0};

int Failures() { return failures.load(); }

// backend 를 한 step 에 하나씩 얹으므로, 계약 검사도 같은 단위로 가른다.
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

bool Contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

std::size_t OpenFdCount() {
    std::error_code ec;
    const std::filesystem::directory_iterator it("/proc/self/fd", ec);
    if (ec) return 0;
    return static_cast<std::size_t>(
        std::distance(it, std::filesystem::directory_iterator()));
}

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

// ---------------------------------------------------------------------------
// 사이클 관측
//
// 총 바이트뿐 아니라 *onRead 호출 하나하나* 를 (크기, 체크섬) 으로 기록한다.
// short read 계약이 요구하는 "path 당 정확히 한 번, 실제로 읽은 크기로" 를
// 보려면 합계만으로는 부족하다. 비동기 backend 는 완료 순서가 제출 순서와
// 다르므로 비교 전에 정렬한다.
// ---------------------------------------------------------------------------

using ReadRecord  = std::pair<std::size_t, std::uint64_t>;
using ReadRecords = std::vector<ReadRecord>;

struct CycleResult {
    bool          threw = false;
    std::string   what;
    std::uint64_t bytes = 0;
    ReadRecords   reads;
};

CycleResult RunOneCycle(skl::IIoBackend& backend,
                        const std::vector<std::string>& paths,
                        std::size_t requestBytes) {
    CycleResult out;
    try {
        out.bytes = backend.RunCycle(
            paths, requestBytes,
            [&out](const unsigned char* data, std::size_t size) {
                std::uint64_t sum = 0;
                for (std::size_t i = 0; i < size; ++i) sum += data[i];
                out.reads.emplace_back(size, sum);
            });
    } catch (const std::exception& e) {
        out.threw = true;
        out.what  = e.what();
    }
    std::sort(out.reads.begin(), out.reads.end());
    return out;
}

std::string DescribeReads(const ReadRecords& reads) {
    std::string s = "{";
    for (std::size_t i = 0; i < reads.size(); ++i) {
        if (i) s += ", ";
        s += "(" + std::to_string(reads[i].first) + ","
           + std::to_string(reads[i].second) + ")";
    }
    return s + "}";
}

void CheckReadsEqual(const ReadRecords& actual, const ReadRecords& expected,
                     const char* label) {
    if (actual == expected) return;
    std::cerr << label << ": onRead records differ\n  actual   "
              << DescribeReads(actual) << "\n  expected "
              << DescribeReads(expected) << '\n';
    failures.fetch_add(1);
}

std::unique_ptr<skl::IIoBackend> MakeExact(skl::BackendType type) {
    skl::BackendType actual = type;
    auto backend = skl::CreateBackend(type, &actual);
    CHECK(actual == type);
    return backend;
}

// ---------------------------------------------------------------------------
// 파일 크기를 파일마다 다르게 잡을 수 있는 fixture.
// 요청 크기(fileBytes)보다 짧은 파일 · 빈 파일 · 요청보다 긴 파일을 한 사이클에
// 섞어 둘 수 있어야 계약이 실제로 통일됐는지 보인다.
// ---------------------------------------------------------------------------

unsigned char FixtureByte(std::size_t fileIdx, std::size_t offset) {
    return static_cast<unsigned char>((fileIdx * 31 + offset * 17) & 0xff);
}

class SizedFixture {
public:
    SizedFixture(std::vector<std::size_t> sizes, const char* tag)
        : sizes_(std::move(sizes)) {
        dir_ = std::filesystem::temp_directory_path() /
               ("skl_io_path_" + std::string(tag) + "_"
                + std::to_string(::getpid()));
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);

        for (std::size_t f = 0; f < sizes_.size(); ++f) {
            const auto path = dir_ / ("f_" + std::to_string(f) + ".bin");
            std::vector<unsigned char> bytes(sizes_[f]);
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = FixtureByte(f, i);
            }
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            out.close();
            CHECK(std::filesystem::file_size(path) == sizes_[f]);
            paths_.push_back(path.string());
        }
    }

    ~SizedFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(dir_, ignored);
    }

    SizedFixture(const SizedFixture&)            = delete;
    SizedFixture& operator=(const SizedFixture&) = delete;

    const std::vector<std::string>& Paths() const { return paths_; }

    // 계약이 요구하는 onRead 기록: path 당 한 번, 크기는 min(파일 크기, 요청).
    ReadRecords ExpectedReads(std::size_t requestBytes) const {
        ReadRecords out;
        for (std::size_t f = 0; f < sizes_.size(); ++f) {
            const std::size_t delivered = std::min(sizes_[f], requestBytes);
            std::uint64_t sum = 0;
            for (std::size_t i = 0; i < delivered; ++i) sum += FixtureByte(f, i);
            out.emplace_back(delivered, sum);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::uint64_t ExpectedBytes(std::size_t requestBytes) const {
        std::uint64_t total = 0;
        for (const std::size_t s : sizes_) {
            total += std::min(s, requestBytes);
        }
        return total;
    }

private:
    std::filesystem::path    dir_;
    std::vector<std::size_t> sizes_;
    std::vector<std::string> paths_;
};

// ---------------------------------------------------------------------------
// (a) short read 계약
// ---------------------------------------------------------------------------

// snippet-begin: short-read-contract
void CheckContractOnFixture(const SizedFixture& fixture, std::size_t requestBytes,
                            const char* label, const skl::BackendType* types,
                            std::size_t count) {
    for (std::size_t idx = 0; idx < count; ++idx) {
        const auto type = types[idx];
        if (!skl::BackendCompiledIn(type)) continue;
        const char* name = skl::kBackendTypeLabels[static_cast<int>(type)];
        auto backend = MakeExact(type);

        const std::size_t fdBefore = OpenFdCount();
        const CycleResult out = RunOneCycle(*backend, fixture.Paths(),
                                             requestBytes);
        if (out.threw) {
            std::cerr << label << '/' << name << ": threw: " << out.what << '\n';
            failures.fetch_add(1);
            continue;
        }
        // path 당 정확히 한 번.
        CHECK(out.reads.size() == fixture.Paths().size());
        CheckReadsEqual(out.reads, fixture.ExpectedReads(requestBytes), name);
        CHECK(out.bytes == fixture.ExpectedBytes(requestBytes));
        CHECK(OpenFdCount() == fdBefore);
    }
}
// snippet-end: short-read-contract

void CheckShortReadContract(const skl::BackendType* types, std::size_t count,
                            const char* group) {
    constexpr std::size_t kRequest = 16 * 1024;

    // 대조군: 파일이 요청과 정확히 같으면 요청 전량. 아래 short 케이스가
    // "그냥 아무 값이나 통과" 하는 게 아님을 보인다.
    {
        SizedFixture exact({kRequest, kRequest, kRequest}, "exact");
        CheckContractOnFixture(exact, kRequest, "exact", types, count);
        CHECK(exact.ExpectedBytes(kRequest) == 3 * kRequest);
    }

    // 짧은 파일 · 빈 파일 · 요청보다 긴 파일을 한 사이클에 섞는다.
    //   kRequest      — 딱 맞음
    //   kRequest/2    — 요청의 절반 (EOF)
    //   4096+1        — 페이지 경계와 어긋난 짧은 파일
    //   1             — 1 바이트
    //   0             — 빈 파일 (mmap 은 length 0 매핑이 EINVAL 이라 별도 경로)
    //   kRequest*2+7  — 요청보다 김 (초과 읽기 금지)
    SizedFixture mixed({kRequest, kRequest / 2, 4096 + 1, 1, 0,
                        kRequest * 2 + 7}, "mixed");
    CheckContractOnFixture(mixed, kRequest, "mixed", types, count);
    CHECK(mixed.ExpectedBytes(kRequest)
          == kRequest + kRequest / 2 + 4097 + 1 + 0 + kRequest);

    // 전부 빈 파일: 총 0 바이트를 돌려주고 아무도 throw 하지 않는다.
    {
        SizedFixture empties({0, 0, 0}, "empty");
        CheckContractOnFixture(empties, kRequest, "empty", types, count);
        CHECK(empties.ExpectedBytes(kRequest) == 0);
    }

    std::cout << "  " << group << ": mixed fixture {16384, 8192, 4097, 1, 0, "
              << kRequest * 2 + 7 << "} request=" << kRequest
              << " -> total=" << mixed.ExpectedBytes(kRequest)
              << " bytes, one onRead per path\n";
}

void TestShortReadContractRead() {
    CheckShortReadContract(kReadBackend, 1, "read");
}

void TestShortReadContractSyncBackends() {
    CheckShortReadContract(kSyncBackends, 2, "pread/mmap");
}

void TestShortReadContractAsyncBackends() {
    CheckShortReadContract(kAsyncBackends, 2, "aio/io_uring");
}

// ---------------------------------------------------------------------------
// (b) 실제 시그널로 만드는 EINTR
// ---------------------------------------------------------------------------

std::atomic<int> g_signalsDelivered{0};

extern "C" void SklTestNoopHandler(int) {
    g_signalsDelivered.fetch_add(1, std::memory_order_relaxed);
}

void InstallInterruptingHandler() {
    struct sigaction sa {};
    sa.sa_handler = &SklTestNoopHandler;
    sa.sa_flags   = 0;  // SA_RESTART 없음 — 이게 EINTR 를 만드는 조건이다.
    ::sigemptyset(&sa.sa_mask);
    CHECK(::sigaction(SIGUSR1, &sa, nullptr) == 0);
}

// 읽기 스레드에게만 1ms 간격으로 SIGUSR1 을 보낸다 (프로세스 전체가 아니라
// pthread_kill 로 스레드 지정 — glibc AIO 의 헬퍼 스레드는 건드리지 않는다).
class SignalStorm {
public:
    void Start(pthread_t target) {
        target_ = target;
        stop_.store(false, std::memory_order_relaxed);
        th_ = std::thread([this] {
            while (!stop_.load(std::memory_order_relaxed)) {
                ::pthread_kill(target_, SIGUSR1);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    // 반드시 대상 스레드를 join 하기 *전에* 부른다 — 종료된 스레드 id 로
    // pthread_kill 하는 것은 정의되지 않은 동작이다.
    void Stop() {
        stop_.store(true, std::memory_order_relaxed);
        if (th_.joinable()) th_.join();
    }
    ~SignalStorm() { Stop(); }

private:
    pthread_t         target_{};
    std::atomic<bool> stop_{true};
    std::thread       th_;
};

class Fifo {
public:
    explicit Fifo(const char* tag) {
        path_ = (std::filesystem::temp_directory_path() /
                 ("skl_io_fifo_" + std::string(tag) + "_"
                  + std::to_string(::getpid()))).string();
        ::unlink(path_.c_str());
        if (::mkfifo(path_.c_str(), 0600) != 0) {
            std::cerr << "mkfifo failed: " << path_ << ": "
                      << std::strerror(errno) << '\n';
            failures.fetch_add(1);
            return;
        }
        // 양쪽 끝을 잡아 두는 홀더. 이게 없으면 O_RDONLY / O_WRONLY open 이
        // 상대편이 열 때까지 블록하고, 그 블록이 시그널로 EINTR 이 되어
        // 측정 대상인 read 대신 open 이 실패한다.
        holder_ = ::open(path_.c_str(), O_RDWR);
        CHECK(holder_ >= 0);
    }
    ~Fifo() {
        if (holder_ >= 0) ::close(holder_);
        ::unlink(path_.c_str());
    }
    Fifo(const Fifo&)            = delete;
    Fifo& operator=(const Fifo&) = delete;

    const std::string& Path() const { return path_; }

    // 파이프 버퍼 용량. payload 를 이보다 크게 잡으면 "한 번의 read 로는
    // 끝날 수 없다" 가 보장된다.
    std::size_t Capacity() const {
        const int c = ::fcntl(holder_, F_GETPIPE_SZ);
        return c > 0 ? static_cast<std::size_t>(c) : std::size_t{65536};
    }

private:
    std::string path_;
    int         holder_ = -1;
};

std::vector<unsigned char> MakePayload(std::size_t bytes) {
    std::vector<unsigned char> out(bytes);
    for (std::size_t i = 0; i < bytes; ++i) {
        out[i] = static_cast<unsigned char>((i * 17 + 5) & 0xff);
    }
    return out;
}

std::uint64_t SumOf(const std::vector<unsigned char>& v) {
    std::uint64_t sum = 0;
    for (const unsigned char b : v) sum += b;
    return sum;
}

std::thread StartFifoWriter(std::string path, std::vector<unsigned char> payload,
                            std::size_t chunkBytes, int firstDelayMs,
                            int gapMs) {
    return std::thread([path = std::move(path), payload = std::move(payload),
                        chunkBytes, firstDelayMs, gapMs] {
        const int fd = ::open(path.c_str(), O_WRONLY);
        if (fd < 0) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(firstDelayMs));
        std::size_t pos = 0;
        while (pos < payload.size()) {
            const std::size_t chunk = std::min(chunkBytes, payload.size() - pos);
            std::size_t written = 0;
            while (written < chunk) {
                const ssize_t w = ::write(fd, payload.data() + pos + written,
                                           chunk - written);
                if (w < 0) {
                    if (errno == EINTR) continue;
                    break;
                }
                if (w == 0) break;
                written += static_cast<std::size_t>(w);
            }
            pos += chunk;
            if (pos < payload.size() && gapMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(gapMs));
            }
        }
        ::close(fd);
    });
}

struct FifoRun {
    CycleResult result;
    std::size_t seamCalls = 0;  // 주입 없이 관측만 한 seam 호출 횟수
};

// backend 를 전용 스레드에서 돌린다. withSignals 이면 그 스레드에만 시그널을
// 퍼붓는다. observeOp 는 "재시도 분기가 실행됐는가" 를 셀 seam 이름이다.
FifoRun RunOnFifoThread(skl::IIoBackend& backend, const std::string& fifoPath,
                        std::size_t requestBytes, const char* observeOp,
                        bool withSignals) {
    FifoRun out;
    // skip 을 아주 크게 — 절대 주입하지 않고 호출 횟수만 센다.
    const ScopedFault observe(std::string(observeOp) + ":100000000:EIO");

    std::promise<pthread_t> tidPromise;
    auto tidFuture = tidPromise.get_future();
    std::atomic<bool> readerFinished{false};

    std::thread reader([&] {
        tidPromise.set_value(::pthread_self());
        out.result = RunOneCycle(backend, {fifoPath}, requestBytes);
        readerFinished.store(true, std::memory_order_release);
    });

    SignalStorm storm;
    const pthread_t tid = tidFuture.get();
    if (withSignals) storm.Start(tid);
    while (!readerFinished.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    storm.Stop();
    reader.join();

    out.seamCalls = skl::FaultCallCount(observeOp);
    return out;
}

// (b-1) 부분 읽기 루프 + EINTR.
// FIFO 에 PIPE_BUF 단위로 끊어 write 하면 read backend 의 부분 읽기 루프가
// 여러 번 돈다. 여기에 시그널을 얹어 EINTR 재시도까지 강제한다.
void TestFifoPartialReadLoopAndEintr() {
    constexpr std::size_t kChunk = 4096;
    static_assert(kChunk <= PIPE_BUF,
                  "PIPE_BUF 이하의 write 만 원자적이다 — 그래야 read 반환값이 "
                  "청크 크기의 배수임을 보장할 수 있다");

    std::size_t capacity = 0;
    {
        Fifo probe("cap");
        capacity = probe.Capacity();
    }
    // payload 가 파이프 용량보다 크면 한 번의 read 로는 절대 끝나지 않는다.
    const std::size_t chunks = capacity / kChunk + 8;
    const std::size_t total  = chunks * kChunk;
    const auto payload = MakePayload(total);
    const ReadRecords expected{{total, SumOf(payload)}};

    auto backend = MakeExact(skl::BackendType::Read);

    FifoRun base;
    {
        Fifo fifo("chunk_base");
        auto writer = StartFifoWriter(fifo.Path(), payload, kChunk,
                                      /*firstDelayMs=*/0, /*gapMs=*/3);
        base = RunOnFifoThread(*backend, fifo.Path(), total, "read",
                               /*withSignals=*/false);
        writer.join();
    }
    CHECK(!base.result.threw);
    if (base.result.threw) std::cerr << "  base threw: " << base.result.what << '\n';
    CHECK(base.result.bytes == total);
    CheckReadsEqual(base.result.reads, expected, "fifo/read/base");
    // payload > 파이프 용량 → 한 번의 read 로 끝날 수 없다. 부분 읽기 루프가
    // 최소 2회 돈다.
    CHECK(base.seamCalls >= 2);
    // PIPE_BUF 이하 write 는 원자적이므로 파이프에는 항상 kChunk 의 배수가
    // 들어 있다 → 시그널이 없으면 read 호출 수는 청크 수를 넘을 수 없다.
    CHECK(base.seamCalls <= chunks);

    const int signalsBefore = g_signalsDelivered.load();
    FifoRun sig;
    {
        Fifo fifo("chunk_sig");
        auto writer = StartFifoWriter(fifo.Path(), payload, kChunk,
                                      /*firstDelayMs=*/0, /*gapMs=*/3);
        sig = RunOnFifoThread(*backend, fifo.Path(), total, "read",
                              /*withSignals=*/true);
        writer.join();
    }
    CHECK(!sig.result.threw);
    if (sig.result.threw) std::cerr << "  signalled threw: " << sig.result.what << '\n';
    CHECK(g_signalsDelivered.load() > signalsBefore);
    // 핵심: 시그널이 와도 바이트 수와 체크섬이 무시그널 실행과 완전히 같다.
    CHECK(sig.result.bytes == base.result.bytes);
    CheckReadsEqual(sig.result.reads, base.result.reads, "fifo/read/signalled");
    // 무시그널이면 청크 수를 넘을 수 없다. 넘었다는 것은 -1/EINTR 를 받고
    // 재시도한 read 호출이 실제로 있었다는 뜻이다.
    CHECK(sig.seamCalls > chunks);

    std::cout << "  fifo chunked read: chunks=" << chunks
              << " calls(base)=" << base.seamCalls
              << " calls(signalled)=" << sig.seamCalls << '\n';
}

// (b-2) 블로킹 대기 지점의 EINTR.
// payload 를 한 번에(PIPE_BUF 원자 write) 늦게 보내면 backend 는 각자의 대기
// 지점에서 오래 블록한다: read backend 는 read, aio 는 aio_suspend,
// io_uring 은 io_uring_wait_cqe. 그 지점의 재시도 분기를 시그널로 밟는다.
struct WaitCase {
    skl::BackendType type;
    const char*      observeOp;
};

void CheckFifoBlockingWaitEintr(const WaitCase* cases, std::size_t caseCount) {
    constexpr std::size_t kBytes    = 4096;  // = PIPE_BUF, 원자적 write
    constexpr int         kDelayMs  = 80;

    const auto payload = MakePayload(kBytes);
    const ReadRecords expected{{kBytes, SumOf(payload)}};

    for (std::size_t idx = 0; idx < caseCount; ++idx) {
        const WaitCase& c = cases[idx];
        const int raw = static_cast<int>(c.type);
        if (!skl::BackendCompiledIn(c.type)) {
            std::cout << "  " << skl::kBackendTypeLabels[raw]
                      << " not compiled in — skipped\n";
            continue;
        }
        auto backend = MakeExact(c.type);

        FifoRun base;
        {
            Fifo fifo("wait_base");
            auto writer = StartFifoWriter(fifo.Path(), payload, kBytes,
                                          kDelayMs, /*gapMs=*/0);
            base = RunOnFifoThread(*backend, fifo.Path(), kBytes, c.observeOp,
                                   /*withSignals=*/false);
            writer.join();
        }
        if (base.result.threw) {
            std::cerr << "  " << skl::kBackendTypeLabels[raw]
                      << " base threw: " << base.result.what << '\n';
            failures.fetch_add(1);
            continue;
        }
        CHECK(base.result.bytes == kBytes);
        CheckReadsEqual(base.result.reads, expected,
                        skl::kBackendTypeLabels[raw]);
        // 데이터가 한 번에 도착하므로 대기 지점은 정확히 한 번만 통과한다.
        CHECK(base.seamCalls == 1);

        const int signalsBefore = g_signalsDelivered.load();
        FifoRun sig;
        {
            Fifo fifo("wait_sig");
            auto writer = StartFifoWriter(fifo.Path(), payload, kBytes,
                                          kDelayMs, /*gapMs=*/0);
            sig = RunOnFifoThread(*backend, fifo.Path(), kBytes, c.observeOp,
                                  /*withSignals=*/true);
            writer.join();
        }
        if (sig.result.threw) {
            std::cerr << "  " << skl::kBackendTypeLabels[raw]
                      << " signalled threw: " << sig.result.what << '\n';
            failures.fetch_add(1);
            continue;
        }
        CHECK(g_signalsDelivered.load() > signalsBefore);
        CHECK(sig.result.bytes == base.result.bytes);
        CheckReadsEqual(sig.result.reads, base.result.reads,
                        skl::kBackendTypeLabels[raw]);
        // 무시그널이면 1 회. 그보다 많다는 것은 대기가 EINTR 로 끊겨 재시도
        // 분기를 실제로 지났다는 뜻이다.
        CHECK(sig.seamCalls > 1);

        std::cout << "  " << skl::kBackendTypeLabels[raw] << " wait via "
                  << c.observeOp << ": calls(base)=" << base.seamCalls
                  << " calls(signalled)=" << sig.seamCalls << '\n';
    }
}

// read backend 의 대기 지점은 read(2) 자신이다.
void TestFifoBlockingReadEintr() {
    const WaitCase cases[] = {{skl::BackendType::Read, "read"}};
    CheckFifoBlockingWaitEintr(cases, 1);
}

// 비동기 backend 의 대기 지점은 완료를 기다리는 API 다.
void TestFifoBlockingAsyncWaitEintr() {
    const WaitCase cases[] = {
        {skl::BackendType::Aio,   "aio_suspend"},
        {skl::BackendType::Uring, "io_uring_wait_cqe"},
    };
    CheckFifoBlockingWaitEintr(cases, 2);
}

// (b-3) 스트림을 읽을 수 없는 backend 는 조용히 0 바이트를 돌려주는 대신
// 명시적으로 거부한다. 위 두 테스트가 pread·mmap 을 제외하는 근거를 못 박는다.
void TestFifoRejectedByNonStreamBackends() {
    Fifo fifo("reject");

    {
        auto backend = MakeExact(skl::BackendType::Pread);
        const CycleResult out = RunOneCycle(*backend, {fifo.Path()}, 4096);
        CHECK(out.threw);  // FIFO 는 seek 불가 → ESPIPE
        CHECK(Contains(out.what, "pread failed"));
    }
    {
        auto backend = MakeExact(skl::BackendType::Mmap);
        const CycleResult out = RunOneCycle(*backend, {fifo.Path()}, 4096);
        CHECK(out.threw);
        CHECK(Contains(out.what, "requires a regular file"));
    }
    std::cout << "  fifo on a seekless path: pread throws 'pread failed', "
                 "mmap throws 'requires a regular file' (neither returns 0 "
                 "bytes silently)\n";
}

// ---------------------------------------------------------------------------
// (c) 취소
// ---------------------------------------------------------------------------

// POSIX AIO: aio_suspend 가 못 쓰게 되면 남은 요청 전부에 aio_cancel 을 걸고
// *모두 aio_return 까지 수확한 뒤* 에야 버퍼와 aiocb 를 놓아야 한다.
// (articles/step04_aio_uring.md 의 수확 규약)
void TestAioCancelDrainsEveryRequest() {
    if (!skl::BackendCompiledIn(skl::BackendType::Aio)) {
        std::cout << "  aio not compiled in — skipped\n";
        return;
    }
    constexpr std::size_t kFiles   = 4;
    constexpr std::size_t kRequest = 64 * 1024;
    SizedFixture fixture(std::vector<std::size_t>(kFiles, kRequest), "aio_cancel");
    auto backend = MakeExact(skl::BackendType::Aio);
    const std::size_t fdBefore = OpenFdCount();

    // 대조군: 정상 사이클에서는 취소가 한 번도 일어나지 않는다. 그래도
    // 완료된 요청은 전부 aio_return 된다.
    {
        ScopedFault observe(
            "aio_cancel:100000000:EIO,aio_return:100000000:EIO");
        const CycleResult ok = RunOneCycle(*backend, fixture.Paths(), kRequest);
        CHECK(!ok.threw);
        CHECK(ok.bytes == fixture.ExpectedBytes(kRequest));
        CHECK(skl::FaultCallCount("aio_cancel") == 0);
        CHECK(skl::FaultCallCount("aio_return") == kFiles);
    }

    // 취소 경로.
    {
        ScopedFault fault("aio_suspend:0:EBADF,"
                          "aio_cancel:100000000:EIO,"
                          "aio_return:100000000:EIO");
        const CycleResult bad = RunOneCycle(*backend, fixture.Paths(), kRequest);
        CHECK(bad.threw);
        CHECK(Contains(bad.what, "aio_suspend failed"));
        // 남아 있던 요청 전부에 취소를 걸었는가.
        CHECK(skl::FaultCallCount("aio_cancel") == kFiles);
        // 그리고 전부 aio_return 까지 수확했는가 — 이게 규약의 핵심이다.
        CHECK(skl::FaultCallCount("aio_return") == kFiles);
    }
    CHECK(OpenFdCount() == fdBefore);

    // 취소 뒤에도 backend 는 그대로 쓸 수 있어야 한다.
    const CycleResult after = RunOneCycle(*backend, fixture.Paths(), kRequest);
    CHECK(!after.threw);
    CHECK(after.bytes == fixture.ExpectedBytes(kRequest));
    CHECK(OpenFdCount() == fdBefore);
    std::cout << "  aio drain: healthy cycle aio_cancel=0 aio_return=" << kFiles
              << "; suspend EBADF aio_cancel=" << kFiles << " aio_return="
              << kFiles << " (every request harvested before the buffers go)\n";
}

void PrintDrainStats(const char* label,
                     const skl::UringBackend::DrainStats& st) {
    std::cout << "  " << label << ": requestCqes=" << st.requestCqes
              << " cancelSqes=" << st.cancelSqes
              << " cancelCqes=" << st.cancelCqes
              << " leftoverCqes=" << st.leftoverCqes << '\n';
}

// io_uring: 대기 API 가 죽어 사이클을 포기할 때 IORING_OP_ASYNC_CANCEL 을 걸고
// *원 요청 CQE 와 취소 요청 CQE 를 모두* 수확해야 한다. CQE 개수로 센다.
//
// 변이 주입으로 확인한 검출 범위:
//   잡는다 — 취소+배수 전체 제거(두 경로 모두), 일부 요청만 취소,
//            원 요청 CQE 를 기다리지 않음.
//   못 잡는다 — 취소 CQE 를 *명시적으로* 기다리지 않게 바꾸는 변이
//            (pendingCancels 를 0 으로). 이 환경에서는 취소 CQE 가 원 요청
//            CQE 보다 먼저 도착해 배수 루프가 어차피 전부 수확하고 링도 비어
//            끝난다 — 관측 가능한 차이가 없다. 도착 순서가 반대가 되면
//            leftoverCqes 가 0 이 아니게 되어 그때는 잡힌다.
void TestUringCancelDrainsBothCqes() {
    if (!skl::BackendCompiledIn(skl::BackendType::Uring)) {
        std::cout << "  io_uring not compiled in — skipped\n";
        return;
    }
    constexpr std::size_t kFiles   = 4;
    constexpr std::size_t kRequest = 64 * 1024;
    SizedFixture fixture(std::vector<std::size_t>(kFiles, kRequest), "uring_cancel");

    // DrainStats 를 보려면 구상 타입이 필요하다.
    std::unique_ptr<skl::UringBackend> backend;
    try {
        backend = std::make_unique<skl::UringBackend>();
    } catch (const std::exception& e) {
        std::cout << "  io_uring unavailable at runtime (" << e.what()
                  << ") — skipped\n";
        return;
    }

    // 대조군: 정상 사이클에는 취소가 없고 원 요청 CQE 만 파일 수만큼 나온다.
    {
        const CycleResult ok = RunOneCycle(*backend, fixture.Paths(), kRequest);
        CHECK(!ok.threw);
        CHECK(ok.bytes == fixture.ExpectedBytes(kRequest));
        const auto& st = backend->LastDrainStats();
        CHECK(st.requestCqes == kFiles);
        CHECK(st.cancelSqes == 0);
        CHECK(st.cancelCqes == 0);
        CHECK(st.leftoverCqes == 0);
    }

    const std::size_t fdBefore = OpenFdCount();

    // (1) 첫 대기에서 대기 API 가 죽는다 → 4개 요청이 전부 in-flight 인 채로
    //     사이클을 포기해야 하는 상황.
    // snippet-begin: uring-drain-accounting
    {
        ScopedFault fault("io_uring_wait_cqe:0:EIO:1");
        const CycleResult bad = RunOneCycle(*backend, fixture.Paths(), kRequest);
        CHECK(bad.threw);
        CHECK(Contains(bad.what, "io_uring_wait_cqe failed"));
    }
    {
        const auto& st = backend->LastDrainStats();
        PrintDrainStats("wait_cqe failure", st);
        CHECK(st.cancelSqes   == kFiles);  // 남은 요청마다 취소 요청 하나
        CHECK(st.cancelCqes   == kFiles);  // 취소 요청 CQE 전부 수확
        CHECK(st.requestCqes  == kFiles);  // 원 요청 CQE 도 전부 수확
        CHECK(st.leftoverCqes == 0);       // 링에 남긴 것 없음
    }
    // snippet-end: uring-drain-accounting
    CHECK(OpenFdCount() == fdBefore);
    {
        const CycleResult ok = RunOneCycle(*backend, fixture.Paths(), kRequest);
        CHECK(!ok.threw);
        CHECK(ok.bytes == fixture.ExpectedBytes(kRequest));
    }

    // (2) 부분 제출 보고. 실제로는 4개가 in-flight 인데 3개만 제출됐다고
    //     보고된다 — 남은 CQE 를 세지 않고 링만 버리면 다음 사이클이 오염된다.
    {
        ScopedFault fault("io_uring_submit_short:0:EIO:1");
        const CycleResult bad = RunOneCycle(*backend, fixture.Paths(), kRequest);
        CHECK(bad.threw);
        CHECK(Contains(bad.what, "fewer SQEs"));
    }
    {
        const auto& st = backend->LastDrainStats();
        PrintDrainStats("short submit", st);
        CHECK(st.cancelSqes   == kFiles);
        CHECK(st.cancelCqes   == kFiles);
        CHECK(st.requestCqes  == kFiles);
        CHECK(st.leftoverCqes == 0);
    }
    CHECK(OpenFdCount() == fdBefore);

    const CycleResult after = RunOneCycle(*backend, fixture.Paths(), kRequest);
    CHECK(!after.threw);
    CHECK(after.bytes == fixture.ExpectedBytes(kRequest));
    CHECK(OpenFdCount() == fdBefore);
}

// 검사 ↔ step 대응표. 소속은 *그 step 이 무엇을 추가했는가* 로 정한다 —
// 계약은 하나지만 그 계약을 지켜야 하는 구현은 step 마다 늘어난다.
const skl_test::StepCase kCases[] = {
    {2, "short read contract (read)",       &TestShortReadContractRead},
    {2, "fifo partial read loop + EINTR",   &TestFifoPartialReadLoopAndEintr},
    {2, "fifo blocking read EINTR",         &TestFifoBlockingReadEintr},
    {3, "short read contract (pread/mmap)",
     &TestShortReadContractSyncBackends},
    {3, "fifo rejected by pread/mmap",
     &TestFifoRejectedByNonStreamBackends},
    {4, "short read contract (aio/io_uring)",
     &TestShortReadContractAsyncBackends},
    {4, "fifo blocking async wait EINTR",   &TestFifoBlockingAsyncWaitEintr},
    {4, "aio cancel drains every request",  &TestAioCancelDrainsEveryRequest},
    {4, "io_uring cancel drains both cqes", &TestUringCancelDrainsBothCqes},
};

}  // namespace

int main(int argc, char** argv) {
    InstallInterruptingHandler();
    return skl_test::RunStepCases(argc, argv, "system_io_path_tests", kCases,
                                  sizeof(kCases) / sizeof(kCases[0]),
                                  &Failures, &skl::ClearFaults);
}
