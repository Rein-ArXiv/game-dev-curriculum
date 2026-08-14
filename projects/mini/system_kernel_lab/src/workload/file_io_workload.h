#pragma once

#include "ctx/ctx_type.h"
#include "io/io_backend.h"
#include "io/io_backend_factory.h"
#include "sched/sched_policy.h"
#include "sync/i_sync.h"
#include "sync/sync_factory.h"   // SyncType — Init/Reconfigure 시그니처와 Sync() 가 쓴다

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <exception>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <ucontext.h>
#include <vector>

namespace skl {

// 파일 IO 워크로드 — 매 사이클 N개 파일을 읽어 sampled checksum을 누적한다.
//
// 세 모드.
//   - workerCount == 1: 기존 backend->RunCycle 한 번 호출 (단일 스레드).
//   - workerCount  > 1 && ctx == Pthread: pthread 워커 풀이 작업 큐에서
//                       fileIdx를 pop, 자기 backend로 파일 1개씩 처리.
//   - workerCount  > 1 && ctx == Coroutine: driver 스레드 하나가 ucontext
//                       coroutine들을 round-robin dispatch.
//
// sync 토글은 *작업 큐 보호 락* 의 비용을 측정한다. 단일 스레드 모드는 sync
// 토글의 영향이 없다.
class FileIoWorkload {
public:
    ~FileIoWorkload() { Shutdown(); }

    void Init(BackendType backendType,
              SyncType    syncType,
              SchedType   schedType,
              CtxType     ctxType,
              std::size_t workerCount,
              std::size_t fileCount,
              std::size_t fileBytes,
              const std::filesystem::path& fixtureDir = {});

    void Reconfigure(BackendType backendType,
                     SyncType    syncType,
                     SchedType   schedType,
                     CtxType     ctxType,
                     std::size_t workerCount);

    std::uint64_t RunCycle();
    void Shutdown();

    std::size_t   FileCount()    const { return paths_.size(); }
    std::size_t   FileBytes()    const { return fileBytes_; }
    std::uint64_t LastChecksum() const { return lastChecksum_.load(); }
    std::size_t   WorkerCount()  const { return workerCount_; }
    BackendType   Backend()      const { return backendType_; }
    SyncType      Sync()         const { return syncType_; }
    SchedType     Sched()        const { return schedType_; }
    bool          SchedApplied() const {
        return schedApplied_.load(std::memory_order_relaxed);
    }
    CtxType       Ctx()          const { return ctxType_; }
    const std::filesystem::path& FixtureDir() const { return fixtureDir_; }

    // ucontext 트램폴린 진입점. makecontext 가 부르는 함수는 자유 함수여야 하고
    // (extern "C", 인자는 int), 그 함수가 다시 이 멤버로 넘어온다. 그래서 public 이다 —
    // 호출자는 파일 안의 트램폴린 하나뿐이며 외부에서 부를 대상이 아니다.
    void CoroBody(std::size_t coroIdx);

private:
    void GenerateFixture(std::size_t fileCount, std::size_t fileBytes);

    void StartWorkers();
    void StopWorkers();
    void WorkerLoop(std::size_t workerIdx);

    void StartCoroutinePool();
    void StopCoroutinePool();

    void RunCycleSingle();
    void RunCycleMulti();
    void RunCycleCoroutine();
    std::unique_ptr<IIoBackend> CreateEffectiveBackend();
    void ClearWorkerError();
    void RecordWorkerError(std::exception_ptr error);
    void RethrowWorkerError();

    // ========================
    // 설정
    // ========================
    std::filesystem::path        fixtureDir_;
    std::vector<std::string>     paths_;
    std::size_t                  fileBytes_   = 0;
    BackendType                  backendType_ = BackendType::Read;
    SyncType                     syncType_    = SyncType::Mutex;
    SchedType                    schedType_   = SchedType::Default;
    CtxType                      ctxType_     = CtxType::Pthread;
    std::size_t                  workerCount_ = 1;
    std::atomic<bool>            schedApplied_{true};

    // ========================
    // 단일 스레드 모드
    // ========================
    std::unique_ptr<IIoBackend>  singleBackend_;

    // ========================
    // 워커 풀 모드
    // ========================
    std::vector<std::unique_ptr<IIoBackend>>  workerBackends_;
    std::vector<std::thread>                  workerThreads_;
    std::unique_ptr<ISync>                    queueSync_;

    // 작업 큐. push는 메인 (DispatchAll), pop은 워커.
    std::queue<std::size_t>      queue_;

    // 메인이 워커들에게 *지금 사이클 시작* 을 알리는 카운터.
    std::atomic<std::size_t>     cycleSeq_{0};        // 매 사이클 ++
    std::atomic<std::size_t>     workerCycleAck_{0};  // 워커가 본 cycleSeq

    std::atomic<std::size_t>     remaining_{0};       // 남은 작업 수
    std::atomic<bool>            running_{false};
    std::atomic<std::uint64_t>   cycleBytes_{0};

    // 누적 checksum. 워커가 atomic add로 누적.
    std::atomic<std::uint64_t>   lastChecksum_{0};
    std::mutex                   workerErrorMutex_;
    std::exception_ptr           workerError_;

    // ========================
    // 코루틴 모드 (단일 OS 스레드 + N개 ucontext stackful coroutine)
    // ========================
    static constexpr std::size_t kCoroStackSize = 64 * 1024;  // 64 KB

    struct Coroutine {
        ucontext_t                  ctx{};
        std::vector<unsigned char>  stack;
        bool                        done = false;
    };

    std::vector<Coroutine>       coros_;
    std::vector<std::unique_ptr<IIoBackend>> coroBackends_;
    ucontext_t                   driverCtx_{};
    std::thread                  coroDriver_;
    std::atomic<bool>            coroRunning_{false};
};

}  // namespace skl
