// IoSystem — 위치 지정 IO 를 Job 스케줄러 워커에 흩뿌리는 비동기 계층.
//
// 게임 루프는 세이브·로그·에셋 스트리밍을 프레임을 막지 않고 처리해야 한다.
// 이 백엔드는 work-stealing Scheduler 워커 위에서 blocking pread/pwrite
// 를 돌리고, 완료를 sync 모듈의 futex 로 통지한다 — io ↔ sync ↔ jobs 세 모듈이
// 맞물리는 지점이다.
//
// 이식 백엔드(스레드 풀)는 어디서나 빌드·검증된다. Linux 의 io_uring 백엔드는
// 같은 IoRequest 인터페이스 뒤에서 시스템 콜을 SQ/CQ 링으로 모아 워커 스레드
// 없이도 깊은 큐를 채우는 형태로 교체된다(system 시리즈).
//
// 수명 규약(overlapped IO 와 동일): 제출한 버퍼와 File 은 Wait 가 끝날 때까지
// 살아 있어야 한다.

#pragma once

#include "io/file.h"
#include "jobs/scheduler.h"
#include "sync/futex.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace mega {
namespace io {

class IoRequest {
public:
    IoRequest() = default;

    bool Done() const { return st_ && st_->done.load(std::memory_order_acquire) != 0; }

    // 완료까지 블로킹. 완료 상태에서 futex 는 즉시 반환한다.
    IoResult Wait() {
        if (!st_) return {false, 0};
        while (st_->done.load(std::memory_order_acquire) == 0)
            sync::FutexWait(&st_->done, 0);
        return st_->result;
    }

private:
    friend class IoSystem;
    struct State {
        std::atomic<uint32_t> done{0};
        IoResult              result{false, 0};
    };
    std::shared_ptr<State> st_;
};

class IoSystem {
public:
    explicit IoSystem(Scheduler& sched) : sched_(sched) {}

    IoRequest ReadAt(const File& f, uint64_t offset, void* buf, size_t n) {
        IoRequest req;
        req.st_ = std::make_shared<IoRequest::State>();
        auto st = req.st_;
        const File* fp = &f;
        sched_.Submit([st, fp, offset, buf, n] {
            st->result = fp->ReadAt(offset, buf, n);
            st->done.store(1, std::memory_order_release);
            sync::FutexWakeAll(&st->done);
        });
        return req;
    }

    IoRequest WriteAt(File& f, uint64_t offset, const void* buf, size_t n) {
        IoRequest req;
        req.st_ = std::make_shared<IoRequest::State>();
        auto st = req.st_;
        File* fp = &f;
        sched_.Submit([st, fp, offset, buf, n] {
            st->result = fp->WriteAt(offset, buf, n);
            st->done.store(1, std::memory_order_release);
            sync::FutexWakeAll(&st->done);
        });
        return req;
    }

private:
    Scheduler& sched_;
};

}  // namespace io
}  // namespace mega
