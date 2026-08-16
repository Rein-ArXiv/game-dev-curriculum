#include "workload/file_io_workload.h"

#include "sync/sync_factory.h"
#include "testhook/test_hook.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string>

namespace skl {

namespace {

constexpr std::size_t kChecksumStride = 4096;

void FillDeterministicBytes(std::vector<unsigned char>& buf,
                             std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::size_t i = 0;
    while (i + sizeof(std::uint64_t) <= buf.size()) {
        const std::uint64_t v = rng();
        std::memcpy(buf.data() + i, &v, sizeof(v));
        i += sizeof(std::uint64_t);
    }
    while (i < buf.size()) {
        buf[i++] = static_cast<unsigned char>(rng() & 0xFF);
    }
}

// onRead 람다 — 파일 한 개의 데이터를 받아 sampled checksum을 누적한다.
// atomic 누적이라 워커 동시 호출도 안전.
inline void AccumulateChecksum(std::atomic<std::uint64_t>& target,
                                const unsigned char* data,
                                std::size_t bytes) {
    std::uint64_t local = 0;
    for (std::size_t i = 0; i < bytes; i += kChecksumStride) {
        local += data[i];
    }
    if (bytes > 0 && ((bytes - 1) % kChecksumStride) != 0) {
        local += data[bytes - 1];
    }
    target.fetch_add(local, std::memory_order_relaxed);
}

}  // namespace

void FileIoWorkload::Init(BackendType backendType,
                          SyncType    syncType,
                          SchedType   schedType,
                          CtxType     ctxType,
                          std::size_t workerCount,
                          std::size_t fileCount,
                          std::size_t fileBytes,
                          const std::filesystem::path& fixtureDir) {
    Shutdown();

    backendType_ = backendType;
    syncType_    = syncType;
    schedType_   = schedType;
    ctxType_     = ctxType;
    workerCount_ = (workerCount == 0) ? 1 : workerCount;
    fileBytes_   = fileBytes;
    fixtureDir_  = fixtureDir.empty()
                   ? std::filesystem::path("/tmp/system_kernel_lab_data")
                   : fixtureDir;

    GenerateFixture(fileCount, fileBytes);
    schedApplied_.store(ApplySchedProcessSetup(schedType_),
                        std::memory_order_relaxed);
    if (workerCount_ <= 1 &&
        (schedType_ == SchedType::Nice || schedType_ == SchedType::Fifo)) {
        // 단일 모드는 GUI/main thread에서 직접 실행한다. UI thread의 정책을
        // 바꾸지 않으므로 worker 전용 정책은 이 조합에서 적용되지 않는다.
        schedApplied_.store(false, std::memory_order_relaxed);
    }

    if (workerCount_ <= 1) {
        singleBackend_ = CreateEffectiveBackend();
    } else if (ctxType_ == CtxType::Coroutine) {
        StartCoroutinePool();
    } else {
        StartWorkers();
    }
}

void FileIoWorkload::Reconfigure(BackendType backendType,
                                  SyncType    syncType,
                                  SchedType   schedType,
                                  CtxType     ctxType,
                                  std::size_t workerCount) {
    StopWorkers();
    StopCoroutinePool();
    singleBackend_.reset();

    backendType_ = backendType;
    syncType_    = syncType;
    schedType_   = schedType;
    ctxType_     = ctxType;
    workerCount_ = (workerCount == 0) ? 1 : workerCount;

    schedApplied_.store(ApplySchedProcessSetup(schedType_),
                        std::memory_order_relaxed);
    if (workerCount_ <= 1 &&
        (schedType_ == SchedType::Nice || schedType_ == SchedType::Fifo)) {
        schedApplied_.store(false, std::memory_order_relaxed);
    }

    if (workerCount_ <= 1) {
        singleBackend_ = CreateEffectiveBackend();
    } else if (ctxType_ == CtxType::Coroutine) {
        StartCoroutinePool();
    } else {
        StartWorkers();
    }
}

std::unique_ptr<IIoBackend> FileIoWorkload::CreateEffectiveBackend() {
    BackendType actual = backendType_;
    auto backend = CreateBackend(backendType_, &actual);
    backendType_ = actual;
    return backend;
}

// snippet-begin: worker-error
void FileIoWorkload::ClearWorkerError() {
    std::lock_guard<std::mutex> lock(workerErrorMutex_);
    workerError_ = nullptr;
}

void FileIoWorkload::RecordWorkerError(std::exception_ptr error) {
    std::lock_guard<std::mutex> lock(workerErrorMutex_);
    if (!workerError_) workerError_ = std::move(error);
}

void FileIoWorkload::RethrowWorkerError() {
    std::exception_ptr error;
    {
        std::lock_guard<std::mutex> lock(workerErrorMutex_);
        error = workerError_;
    }
    if (error) std::rethrow_exception(error);
}
// snippet-end: worker-error

void FileIoWorkload::GenerateFixture(std::size_t fileCount,
                                     std::size_t fileBytes) {
    std::error_code ec;
    std::filesystem::create_directories(fixtureDir_, ec);
    if (ec) {
        throw std::runtime_error("fixture dir create failed: " + ec.message());
    }

    paths_.reserve(fileCount);
    std::vector<unsigned char> tmp(fileBytes);

    for (std::size_t i = 0; i < fileCount; ++i) {
        char name[64];
        std::snprintf(name, sizeof(name), "f_%04zu.bin", i);
        std::filesystem::path p = fixtureDir_ / name;
        paths_.push_back(p.string());

        std::error_code statEc;
        const auto sz = std::filesystem::file_size(p, statEc);
        if (!statEc && sz == fileBytes) continue;

        FILE* f = std::fopen(p.c_str(), "wb");
        if (!f) {
            throw std::runtime_error("fixture file open failed: " + p.string());
        }
        FillDeterministicBytes(tmp, /*seed=*/0xC0FFEE'0001ULL + i);
        const std::size_t written = std::fwrite(tmp.data(), 1, tmp.size(), f);
        if (written != tmp.size()) {
            std::fclose(f);
            throw std::runtime_error("fixture file write failed: " + p.string());
        }
        if (std::fclose(f) != 0) {
            throw std::runtime_error("fixture file close failed: " + p.string());
        }
    }
}

void FileIoWorkload::StartWorkers() {
    queueSync_ = CreateSync(syncType_);

    workerBackends_.clear();
    workerBackends_.reserve(workerCount_);
    for (std::size_t i = 0; i < workerCount_; ++i) {
        workerBackends_.push_back(CreateEffectiveBackend());
    }

    cycleSeq_.store(0);
    workerCycleAck_.store(0);
    remaining_.store(0);
    running_.store(true);

    workerThreads_.reserve(workerCount_);
    for (std::size_t i = 0; i < workerCount_; ++i) {
        workerThreads_.emplace_back(&FileIoWorkload::WorkerLoop, this, i);
    }
}

void FileIoWorkload::StopWorkers() {
    if (workerThreads_.empty()) return;

    running_.store(false);
    // 워커가 폴링 중이면 cycleSeq 증가로 깨어나게.
    cycleSeq_.fetch_add(1);

    for (auto& t : workerThreads_) {
        if (t.joinable()) t.join();
    }
    workerThreads_.clear();
    workerBackends_.clear();
    queueSync_.reset();
}

void FileIoWorkload::WorkerLoop(std::size_t workerIdx) {
    auto* backend = workerBackends_[workerIdx].get();

    // 워커 스레드 자체에 스케줄링 정책 적용. 권한 부족 시 stderr 메시지 +
    // 기본으로 폴백. 실패해도 워커는 계속 동작한다.
    if (!ApplySchedToCurrentThread(schedType_))
        schedApplied_.store(false, std::memory_order_relaxed);

    std::size_t lastSeen = 0;

    while (running_.load(std::memory_order_acquire)) {
        const std::size_t curSeq = cycleSeq_.load(std::memory_order_acquire);
        if (curSeq == lastSeen) {
            if (schedType_ == SchedType::Fifo) {
                // SCHED_FIFO 워커가 idle polling 상태에서 계속 runnable이면
                // CFS 메인/UI 스레드를 굶길 수 있다. 짧게 block해 양보한다.
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            } else {
                std::this_thread::yield();
            }
            continue;
        }
        lastSeen = curSeq;
        if (!running_.load(std::memory_order_acquire)) break;

        // 사이클 시작 ack — 모든 워커가 ack해야 메인이 작업을 push 했음을 보장.
        // (사실은 메인이 push 후 cycleSeq 증가, 워커가 cycleSeq 감지 후 pop.
        //  ack 카운터는 *모든 워커가 사이클 진입했음* 을 메인에 알리는 용도.)
        workerCycleAck_.fetch_add(1, std::memory_order_release);

        // 작업 처리 루프.
        for (;;) {
            std::size_t fileIdx = static_cast<std::size_t>(-1);
            bool got = false;

            queueSync_->Lock();
            if (!queue_.empty()) {
                fileIdx = queue_.front();
                queue_.pop();
                got = true;
            }
            queueSync_->Unlock();

            if (!got) break;

            // backend의 RunCycle을 *부분 paths(파일 1개)* 로 호출.
            try {
                const std::vector<std::string> sub = { paths_[fileIdx] };
                const std::uint64_t bytes = backend->RunCycle(
                    sub, fileBytes_,
                    [this](const unsigned char* data, std::size_t n) {
                        AccumulateChecksum(lastChecksum_, data, n);
                    });
                cycleBytes_.fetch_add(bytes, std::memory_order_relaxed);
            } catch (...) {
                RecordWorkerError(std::current_exception());
            }
            // 감산 *직전* 값을 훅에 넘긴다. 1 이면 이 워커가 사이클의 마지막
            // 작업을 끝낸 것, 0 이면 아래 RunCycleMulti 주석이 말하는 언더플로다.
            // 무장되지 않은 빌드에서는 호출 자체가 사라진다(testhook/test_hook.h).
            const std::size_t before =
                remaining_.fetch_sub(1, std::memory_order_acq_rel);
            RunTestHook(TestHookId::WorkerAfterDecrement, before);
        }
    }
}

std::uint64_t FileIoWorkload::RunCycle() {
    if (workerCount_ <= 1) {
        RunCycleSingle();
    } else if (ctxType_ == CtxType::Coroutine) {
        RunCycleCoroutine();
    } else {
        RunCycleMulti();
    }
    return cycleBytes_.load(std::memory_order_relaxed);
}

void FileIoWorkload::RunCycleSingle() {
    if (!singleBackend_) {
        throw std::runtime_error("FileIoWorkload: single backend missing");
    }
    cycleBytes_.store(0);
    lastChecksum_.store(0, std::memory_order_relaxed);

    const std::uint64_t total = singleBackend_->RunCycle(
        paths_, fileBytes_,
        [this](const unsigned char* data, std::size_t n) {
            AccumulateChecksum(lastChecksum_, data, n);
        });

    cycleBytes_.store(total, std::memory_order_relaxed);
}

void FileIoWorkload::RunCycleMulti() {
    cycleBytes_.store(0);
    lastChecksum_.store(0, std::memory_order_relaxed);
    ClearWorkerError();

    // 1) 작업 push. remaining_ 설정을 *같은 락 구간 안* 에서 끝낸다.
    //    직전 사이클의 마지막 작업을 막 끝낸 워커는 내부 pop 루프로 곧장
    //    돌아오므로, unlock 후 store 전에 깨어나면 이 사이클의 항목을 pop해
    //    0에서 fetch_sub(1) → SIZE_MAX 언더플로를 만든다. 그 뒤 store가
    //    카운터를 덮어써 완료 대기가 영원히 끝나지 않는다.
    queueSync_->Lock();
    while (!queue_.empty()) queue_.pop();
    for (std::size_t i = 0; i < paths_.size(); ++i) queue_.push(i);
    remaining_.store(paths_.size(), std::memory_order_release);
    queueSync_->Unlock();
    // 위 store 를 이 지점 *뒤* 로 옮기면 언더플로가 열린다. 그 회귀를 확률이
    // 아니라 결정적으로 잡기 위한 랑데부 지점 — 테스트 빌드에서만 존재한다.
    RunTestHook(TestHookId::AfterQueueUnlock);

    // 2) 모든 워커가 새 사이클을 보도록 시퀀스 증가.
    workerCycleAck_.store(0, std::memory_order_release);
    cycleSeq_.fetch_add(1, std::memory_order_release);

    // 3) 모든 워커가 사이클 진입을 ack할 때까지 대기.
    while (workerCycleAck_.load(std::memory_order_acquire) < workerCount_) {
        std::this_thread::yield();
    }

    // 4) 작업 모두 완료 대기.
    while (remaining_.load(std::memory_order_acquire) > 0) {
        std::this_thread::yield();
    }
    RethrowWorkerError();
}

void FileIoWorkload::Shutdown() {
    StopWorkers();
    StopCoroutinePool();
    singleBackend_.reset();
    ApplySchedProcessSetup(SchedType::Default);
    paths_.clear();
    fileBytes_   = 0;
    workerCount_ = 1;
}

// ============================================================================
// 코루틴 모드 — ucontext 기반 stackful coroutine, round-robin driver.
// ============================================================================

// snippet-begin: coro-trampoline
namespace {

// makecontext의 trampoline 진입에서 *현재 어느 인스턴스의 어느 idx인가* 를
// 알아내기 위한 정적 pass-through. 단일 FileIoWorkload 인스턴스 가정.
FileIoWorkload* g_coroSelf = nullptr;

}  // namespace

extern "C" {

// ucontext entry trampoline. makecontext의 정수 인자로 코루틴 idx를 받는다.
// self는 단일 FileIoWorkload 인스턴스 가정의 pass-through로만 사용한다.
static void SklCoroTrampoline(int idx) {
    g_coroSelf->CoroBody(static_cast<std::size_t>(idx));
}

}  // extern "C"
// snippet-end: coro-trampoline

bool FileIoWorkload::SetupCoroContext(Coroutine& c, int idx) {
    if (::getcontext(&c.ctx) != 0) {
        std::fprintf(stderr, "skl: getcontext failed\n");
        RecordWorkerError(std::make_exception_ptr(
            std::runtime_error("getcontext failed")));
        c.done = true;
        return false;
    }
    c.ctx.uc_stack.ss_sp   = c.stack.data();
    c.ctx.uc_stack.ss_size = c.stack.size();
    c.ctx.uc_link          = &driverCtx_;
    c.done                 = false;
    ::makecontext(&c.ctx,
                  reinterpret_cast<void (*)()>(SklCoroTrampoline),
                  1,
                  idx);
    return true;
}

void FileIoWorkload::StartCoroutinePool() {
    queueSync_ = CreateSync(syncType_);

    coroBackends_.clear();
    coroBackends_.reserve(workerCount_);
    for (std::size_t i = 0; i < workerCount_; ++i) {
        coroBackends_.push_back(CreateEffectiveBackend());
    }

    coros_.clear();
    coros_.resize(workerCount_);
    for (std::size_t i = 0; i < workerCount_; ++i) {
        coros_[i].stack.assign(kCoroStackSize, 0);
        coros_[i].done = true;  // 사이클 시작 전엔 idle 상태
    }
    g_coroSelf = this;

    coroRunning_.store(true);
    cycleBytes_.store(0);

    // 코루틴 driver 자체도 별도 스레드로 둔다 — 메인 GUI 스레드를 blocking
    // 하지 않게. driver 스레드가 *한 사이클 dispatch + 모든 코루틴 종료 대기*
    // 패턴으로 동작. RunCycleCoroutine이 driver 스레드를 깨운다.
    cycleSeq_.store(0);
    workerCycleAck_.store(0);
    coroDriver_ = std::thread([this]() {
        if (!ApplySchedToCurrentThread(schedType_))
            schedApplied_.store(false, std::memory_order_relaxed);

        std::size_t lastSeen = 0;
        while (coroRunning_.load(std::memory_order_acquire)) {
            const std::size_t curSeq = cycleSeq_.load(std::memory_order_acquire);
            if (curSeq == lastSeen) {
                if (schedType_ == SchedType::Fifo) {
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                } else {
                    std::this_thread::yield();
                }
                continue;
            }
            lastSeen = curSeq;
            if (!coroRunning_.load(std::memory_order_acquire)) break;

            // ucontext는 normal return 후 재진입할 수 없으므로 사이클마다
            // 같은 stack 위에 context entry를 새로 만든다.
            //
            // 셋업 한 건을 별도 함수(SetupCoroContext)로 뺀 이유가 있다.
            // getcontext 는 setjmp 처럼 "두 번 반환할 수 있는" 함수라, 같은
            // 프레임에서 그 호출을 넘어 살아 있는 지역 변수는 컴파일러가
            // clobber 후보로 본다(-Wclobbered). 실제로는 getcontext 가 저장한
            // 복귀 지점을 바로 다음 makecontext 가 트램폴린 진입점으로 덮어써
            // 이 지점으로 되돌아오지 않지만, 컴파일러는 그것을 증명할 수 없다.
            // 함수를 분리하면 루프 변수와 setupOk 가 그 프레임에 없어 경고가
            // 사라지고, 누군가 나중에 setcontext(&c.ctx) 경로를 추가해도
            // 이 루프 상태가 깨지지 않는다.
            bool setupOk = true;
            for (std::size_t i = 0; i < coros_.size(); ++i) {
                if (!SetupCoroContext(coros_[i], static_cast<int>(i))) {
                    setupOk = false;
                }
            }
            if (!setupOk) {
                workerCycleAck_.fetch_add(1, std::memory_order_release);
                continue;
            }

            // round-robin dispatch. 한 코루틴이 yield (= driver로 swap-back)
            // 할 때마다 다음 코루틴으로. CoroBody는 *작업이 없으면 normal
            // return* 으로 종료, 그러면 uc_link(driverCtx_)로 복귀.
            std::size_t cur = 0;
            std::size_t allDoneCheck = 0;
            while (true) {
                if (coros_[cur].done) {
                    ++allDoneCheck;
                    if (allDoneCheck >= workerCount_) break;
                    cur = (cur + 1) % workerCount_;
                    continue;
                }
                allDoneCheck = 0;
                if (::swapcontext(&driverCtx_, &coros_[cur].ctx) != 0) {
                    RecordWorkerError(std::make_exception_ptr(
                        std::runtime_error("driver swapcontext failed")));
                    coros_[cur].done = true;
                }
                cur = (cur + 1) % workerCount_;
            }

            // 사이클 ack.
            workerCycleAck_.fetch_add(1, std::memory_order_release);
        }
    });
}

void FileIoWorkload::StopCoroutinePool() {
    if (!coroDriver_.joinable()) return;

    coroRunning_.store(false);
    cycleSeq_.fetch_add(1);  // driver 깨움.
    coroDriver_.join();

    coros_.clear();
    coroBackends_.clear();
    queueSync_.reset();
    g_coroSelf = nullptr;
}

void FileIoWorkload::CoroBody(std::size_t coroIdx) {
    auto* backend = coroBackends_[coroIdx].get();

    for (;;) {
        // 작업 큐에서 한 개 pop. 단일 스레드라 sync는 의미 없지만 토글된
        // sync 비용이 측정에 그대로 남도록 동일 락을 통과시킨다.
        std::size_t fileIdx = static_cast<std::size_t>(-1);
        bool got = false;
        queueSync_->Lock();
        if (!queue_.empty()) {
            fileIdx = queue_.front();
            queue_.pop();
            got = true;
        }
        queueSync_->Unlock();

        if (!got) break;

        try {
            const std::vector<std::string> sub = { paths_[fileIdx] };
            const std::uint64_t bytes = backend->RunCycle(
                sub, fileBytes_,
                [this](const unsigned char* data, std::size_t n) {
                    AccumulateChecksum(lastChecksum_, data, n);
                });
            cycleBytes_.fetch_add(bytes, std::memory_order_relaxed);
        } catch (...) {
            RecordWorkerError(std::current_exception());
            break;
        }

        // 한 작업 처리 후 driver로 양보 — 이게 *컨텍스트 스위치 측정 지점*.
        if (::swapcontext(&coros_[coroIdx].ctx, &driverCtx_) != 0) {
            RecordWorkerError(std::make_exception_ptr(
                std::runtime_error("coroutine swapcontext failed")));
            break;
        }
    }

    // 작업 다 떨어지면 done 표시 후 normal return. uc_link가 driverCtx_로
    // 복귀시킨다.
    coros_[coroIdx].done = true;
}

void FileIoWorkload::RunCycleCoroutine() {
    cycleBytes_.store(0);
    lastChecksum_.store(0, std::memory_order_relaxed);
    ClearWorkerError();

    queueSync_->Lock();
    while (!queue_.empty()) queue_.pop();
    for (std::size_t i = 0; i < paths_.size(); ++i) queue_.push(i);
    queueSync_->Unlock();

    // 새 사이클 시그널 — coroDriver 스레드가 dispatch 시작.
    workerCycleAck_.store(0, std::memory_order_release);
    cycleSeq_.fetch_add(1, std::memory_order_release);

    // dispatch 완료 대기.
    while (workerCycleAck_.load(std::memory_order_acquire) < 1) {
        std::this_thread::yield();
    }
    RethrowWorkerError();
}

}  // namespace skl
