#include "io/io_backend_factory.h"
#include "sync/sync_factory.h"
#include "workload/file_io_workload.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {

int failures = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':'  \
                      << __LINE__ << ")\n";                                   \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

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

void TestIoBackends() {
    constexpr std::size_t kFileCount = 4;
    constexpr std::size_t kFileBytes = 64 * 1024;
    TempFixture fixture(kFileCount, kFileBytes);

    for (int raw = 0; raw < skl::kBackendTypeCount; ++raw) {
        const auto requested = static_cast<skl::BackendType>(raw);
        if (!skl::BackendCompiledIn(requested)) continue;

        skl::BackendType actual = requested;
        auto backend = skl::CreateBackend(requested, &actual);
        std::uint64_t sum = 0;
        const std::uint64_t bytes = backend->RunCycle(
            fixture.Paths(), fixture.FileBytes(),
            [&](const unsigned char* data, std::size_t size) {
                for (std::size_t i = 0; i < size; ++i) sum += data[i];
            });

        CHECK(bytes == kFileCount * kFileBytes);
        CHECK(sum == fixture.ExpectedSum());
        if (actual != requested) {
            std::cout << skl::kBackendTypeLabels[raw]
                      << " unavailable at runtime; verified read fallback\n";
        }
    }
}

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
        CHECK(protectedCounter ==
              static_cast<std::uint64_t>(kThreads * kIterations));
    }
}

void TestWorkloadExecutionModels() {
    constexpr std::size_t kFileCount = 8;
    constexpr std::size_t kFileBytes = 4097;
    TempFixture owner(1, 1);

    skl::FileIoWorkload workload;
    workload.Init(skl::BackendType::Read, skl::SyncType::Mutex,
                  skl::SchedType::Default, skl::CtxType::Pthread,
                  1, kFileCount, kFileBytes, owner.Dir() / "workload");
    const std::uint64_t expectedBytes = workload.RunCycle();
    const std::uint64_t expectedChecksum = workload.LastChecksum();
    CHECK(expectedBytes == kFileCount * kFileBytes);

    for (int raw = 0; raw < skl::kSyncTypeCount; ++raw) {
        const auto sync = static_cast<skl::SyncType>(raw);
        workload.Reconfigure(skl::BackendType::Read, sync,
                             skl::SchedType::Default,
                             skl::CtxType::Pthread, 4);
        CHECK(workload.RunCycle() == expectedBytes);
        CHECK(workload.LastChecksum() == expectedChecksum);

        workload.Reconfigure(skl::BackendType::Read, sync,
                             skl::SchedType::Default,
                             skl::CtxType::Coroutine, 4);
        CHECK(workload.RunCycle() == expectedBytes);
        CHECK(workload.LastChecksum() == expectedChecksum);
    }
    workload.Shutdown();
}

}  // namespace

int main() {
    TestIoBackends();
    TestSyncPrimitives();
    TestWorkloadExecutionModels();
    if (failures != 0) {
        std::cerr << failures << " system core test(s) failed\n";
        return 1;
    }
    std::cout << "system core tests passed\n";
    return 0;
}
