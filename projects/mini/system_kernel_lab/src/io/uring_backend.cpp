#include "io/uring_backend.h"

#if defined(SKL_HAS_LIBURING)

#include "fault/skl_syscalls.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <exception>
#include <liburing.h>
#include <stdexcept>
#include <string>
#include <limits>
#include <thread>
#include <unistd.h>
#include <vector>

namespace skl {

namespace {

// io_uring 전용 seam. liburing 함수들은 errno 가 아니라 *음수 오류 번호* 를
// 반환하므로 주입도 같은 규약을 따른다.
int SklIoUringQueueInit(unsigned depth, io_uring* ring, unsigned flags) {
    if (const int e = SklFaultCheck("io_uring_queue_init")) return -e;
    return io_uring_queue_init(depth, ring, flags);
}

io_uring_sqe* SklIoUringGetSqe(io_uring* ring) {
    // SQ 가 가득 찬 상황 — liburing 은 nullptr 로만 알린다.
    if (SklFaultTriggered("io_uring_get_sqe")) return nullptr;
    return io_uring_get_sqe(ring);
}

int SklIoUringSubmit(io_uring* ring) {
    if (const int e = SklFaultCheck("io_uring_submit")) return -e;
    const int rc = io_uring_submit(ring);
    // *부분 제출* 재현: 실제로는 전부 제출됐지만 반환값만 하나 줄인다.
    // 링에는 아직 in-flight 요청이 남아 있으므로, 제품 코드가 링을 재초기화
    // 하지 않고 다음 사이클로 넘어가면 그 흔적이 반드시 드러난다.
    if (rc > 0 && SklFaultTriggered("io_uring_submit_short")) return rc - 1;
    return rc;
}

int SklIoUringWaitCqe(io_uring* ring, io_uring_cqe** cqe) {
    if (const int e = SklFaultCheck("io_uring_wait_cqe")) return -e;
    return io_uring_wait_cqe(ring, cqe);
}

// 취소 요청 CQE 를 원 요청 CQE 와 구분하는 태그. 원 요청의 user_data 는
// 파일 인덱스(< kQueueDepth)라 최상위 비트를 절대 쓰지 않는다.
constexpr __u64 kCancelTag = static_cast<__u64>(1) << 63;

// 커널이 손상된 user_data 를 돌려주는 상황 재현.
__u64 SklUringUserData(std::size_t fileIdx) {
    if (SklFaultTriggered("uring_user_data")) {
        return ~static_cast<__u64>(0);
    }
    return static_cast<__u64>(fileIdx);
}

}  // namespace

struct UringBackend::Impl {
    io_uring ring{};
    bool     ringInited = false;
};

UringBackend::UringBackend() : impl_(new Impl) {
    const int rc = SklIoUringQueueInit(kQueueDepth, &impl_->ring, /*flags=*/0);
    if (rc < 0) {
        // ENOSYS: 커널이 io_uring 미지원. EPERM: seccomp 등 권한 차단.
        delete impl_;
        impl_ = nullptr;
        throw std::runtime_error(std::string("io_uring_queue_init failed: ")
                                 + std::strerror(-rc));
    }
    impl_->ringInited = true;
}

UringBackend::~UringBackend() {
    if (impl_) {
        if (impl_->ringInited) {
            io_uring_queue_exit(&impl_->ring);
        }
        delete impl_;
    }
}

std::uint64_t UringBackend::RunCycle(const std::vector<std::string>& paths,
                                       std::size_t fileBytes,
                                       const OnFileRead& onRead) {
    const std::size_t n = paths.size();
    if (n == 0) return 0;
    if (n > kQueueDepth) {
        throw std::runtime_error("UringBackend: paths exceed kQueueDepth");
    }
    if (fileBytes > std::numeric_limits<unsigned>::max()) {
        throw std::runtime_error("UringBackend: fileBytes exceeds read length");
    }

    drain_ = DrainStats{};

    // 사이클마다 fresh 자원. AioBackend와 같은 정책.
    std::vector<int>                         fds(n, -1);
    std::vector<std::vector<unsigned char>>  bufs(n);
    // 어느 요청의 CQE 를 이미 수확했는가. 취소 대상 특정과 배수 회계에 쓴다.
    std::vector<char>                        reaped(n, 0);
    auto closeAll = [&fds]() {
        for (int& fd : fds) {
            if (fd >= 0) {
                ::close(fd);
                fd = -1;
            }
        }
    };
    auto resetRing = [this]() {
        if (impl_->ringInited) {
            io_uring_queue_exit(&impl_->ring);
            impl_->ringInited = false;
        }
        const int rc = SklIoUringQueueInit(kQueueDepth, &impl_->ring, /*flags=*/0);
        if (rc < 0) {
            throw std::runtime_error(std::string("io_uring reset failed: ")
                                     + std::strerror(-rc));
        }
        impl_->ringInited = true;
    };

    if (!impl_->ringInited) resetRing();

    // flags == 0: consume all prepared SQEs before adding cancellation requests.
    // A permanent cleanup failure cannot justify releasing live I/O buffers.
    auto submitPending = [&]() {
        while (io_uring_sq_ready(&impl_->ring) != 0) {
            const int rc = io_uring_submit(&impl_->ring);
            if (rc < 0 && rc != -EINTR && rc != -EAGAIN)
                std::terminate();
            if (rc <= 0) std::this_thread::yield();
        }
    };

    // 대기 API 가 실패해 사이클을 포기해야 할 때, in-flight 요청은 곧 사라질
    // bufs[] 를 가리킨 채 남는다. 링을 재초기화(io_uring_queue_exit)하는 것만
    // 으로는 커널이 그 요청을 정리하는 시점이 우리 반환 시점과 동기화되지
    // 않는다. 그래서 남은 요청마다 IORING_OP_ASYNC_CANCEL 을 걸고
    // *원 요청 CQE 와 취소 요청 CQE 를 모두 수확한 뒤* 에야 반환한다.
    // (system/part14_io_uring.md 의 drain 규약)
    auto cancelAndDrain = [&]() {
        submitPending();
        // Each read is one-shot. Even an invalid identity still retires one read.
        const std::size_t outstanding = n - drain_.requestCqes;
        if (outstanding == 0) return;

        std::size_t queued = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (reaped[i]) continue;
            io_uring_sqe* sqe = io_uring_get_sqe(&impl_->ring);
            if (!sqe) std::terminate();
            io_uring_prep_cancel64(sqe, static_cast<__u64>(i), /*flags=*/0);
            // 취소 요청 자신의 CQE 는 최상위 비트로 구분한다. 원 요청의
            // user_data 는 파일 인덱스(< n)라 절대 겹치지 않는다.
            io_uring_sqe_set_data64(sqe, kCancelTag | static_cast<__u64>(i));
            ++queued;
        }
        submitPending();
        drain_.cancelSqes += queued;

        std::size_t pendingOriginals = outstanding;
        std::size_t pendingCancels   = queued;
        while (pendingOriginals > 0 || pendingCancels > 0) {
            io_uring_cqe* cqe = nullptr;
            // Timeout is not completion. Keep ownership until the read CQE arrives.
            __kernel_timespec ts{};
            ts.tv_sec = 1;
            const int injected = SklFaultCheck("io_uring_drain_wait");
            const int rc = injected ? -injected
                : io_uring_wait_cqe_timeout(&impl_->ring, &cqe, &ts);
            if (rc < 0) {
                if (rc == -EINTR || rc == -ETIME || rc == -EAGAIN) continue;
                std::terminate();
            }
            const __u64 data = io_uring_cqe_get_data64(cqe);
            if ((data & kCancelTag) != 0 && (data & ~kCancelTag) < n) {
                if (pendingCancels > 0) --pendingCancels;
                ++drain_.cancelCqes;
            } else if (data < n) {
                reaped[data] = 1;
                if (pendingOriginals > 0) --pendingOriginals;
                ++drain_.requestCqes;
            } else {
                // 취소 CQE 의 user_data 는 우리가 직접 박은 kCancelTag|i 뿐이므로
                // 어느 쪽 형태도 아닌 값은 user_data 가 손상된 원 요청 CQE 다.
                // 최상위 비트만 보고 취소로 세면 pendingOriginals 가 영영 안 준다.
                if (pendingOriginals > 0) --pendingOriginals;
                ++drain_.requestCqes;
            }
            io_uring_cqe_seen(&impl_->ring, cqe);
        }

        // 사후조건: 배수가 끝났으면 CQ 링이 비어 있어야 한다. 남은 CQE 는
        // 다음 사이클이 자기 요청의 완료로 오인할 쓰레기다. 세면서 치운다.
        for (;;) {
            io_uring_cqe* leftover = nullptr;
            if (io_uring_peek_cqe(&impl_->ring, &leftover) != 0 || !leftover) {
                break;
            }
            ++drain_.leftoverCqes;
            io_uring_cqe_seen(&impl_->ring, leftover);
        }
    };

    for (auto& buf : bufs) buf.resize(fileBytes);

    // open + 버퍼 준비. submit 전 실패가 나도 링에 미제출 SQE가 남지 않는다.
    for (std::size_t i = 0; i < n; ++i) {
        fds[i] = SklOpen(paths[i].c_str(), O_RDONLY);
        if (fds[i] < 0) {
            closeAll();
            throw std::runtime_error(std::string("open failed: ") + paths[i]
                                     + ": " + std::strerror(errno));
        }
    }

    // SQE 채움.
    for (std::size_t i = 0; i < n; ++i) {
        io_uring_sqe* sqe = SklIoUringGetSqe(&impl_->ring);
        if (!sqe) {
            closeAll();
            resetRing();
            throw std::runtime_error("io_uring_get_sqe returned null");
        }
        io_uring_prep_read(sqe, fds[i], bufs[i].data(),
                            static_cast<unsigned>(fileBytes), 0);
        // user_data로 fileIdx를 박아 CQE 수확 시 매핑.
        io_uring_sqe_set_data64(sqe, SklUringUserData(i));
    }

    // 일괄 제출. 여러 read 요청을 하나의 submit 경계로 밀어 넣는다.
    const int submitted = SklIoUringSubmit(&impl_->ring);
    if (submitted < 0) {
        // Even an interrupted submit must not leave published reads using locals.
        cancelAndDrain();
        closeAll();
        // 제출 실패 시 준비된 SQE가 링에 남을 수 있다. SQE는 곧 소멸할
        // 버퍼와 닫힌 fd를 가리키므로 다음 사이클 전에 반드시 버린다.
        resetRing();
        throw std::runtime_error(std::string("io_uring_submit failed: ")
                                 + std::strerror(-submitted));
    }
    if (submitted != static_cast<int>(n)) {
        // 보고된 수보다 실제로 더 많이 in-flight 일 수 있고(부분 제출 보고),
        // 남은 SQE 가 링에 그대로일 수도 있다. 어느 쪽이든 취소를 걸고 CQE 를
        // 전부 수확한 뒤에야 버퍼를 놓는다. 취소 SQE 를 제출하는 submit 이
        // 링에 남아 있던 read SQE 도 함께 밀어내므로 양쪽 다 CQE 로 끝난다.
        cancelAndDrain();
        closeAll();
        resetRing();
        throw std::runtime_error("io_uring_submit submitted fewer SQEs than expected");
    }

    // CQE 수확. 완료 순서대로 onRead.
    std::uint64_t totalBytes = 0;
    std::size_t doneCount = 0;
    std::string firstError;
    std::exception_ptr callbackError;
    while (doneCount < n) {
        io_uring_cqe* cqe = nullptr;
        const int waitRc = SklIoUringWaitCqe(&impl_->ring, &cqe);
        if (waitRc < 0) {
            if (waitRc == -EINTR) continue;
            // 아직 수확하지 못한 요청이 bufs[] 를 가리킨 채 남아 있다.
            // 취소를 걸고 원 요청·취소 요청 CQE 를 모두 수확한 뒤 반환한다.
            cancelAndDrain();
            closeAll();
            resetRing();
            throw std::runtime_error(std::string("io_uring_wait_cqe failed: ")
                                     + std::strerror(-waitRc));
        }

        const std::uint64_t idx = io_uring_cqe_get_data64(cqe);
        ++drain_.requestCqes;
        if (idx >= n) {
            if (firstError.empty()) {
                firstError = "io_uring returned invalid user_data";
            }
            io_uring_cqe_seen(&impl_->ring, cqe);
            ++doneCount;
            continue;
        }
        reaped[idx] = 1;
        if (cqe->res < 0) {
            if (firstError.empty()) {
                firstError = std::string("io_uring read failed: ")
                           + paths[idx] + ": "
                           + std::strerror(-cqe->res);
            }
            if (fds[idx] >= 0) {
                ::close(fds[idx]);
                fds[idx] = -1;
            }
            io_uring_cqe_seen(&impl_->ring, cqe);
            ++doneCount;
            continue;
        }
        const std::size_t bytes = static_cast<std::size_t>(cqe->res);
        if (!callbackError) {
            try {
                onRead(bufs[idx].data(), bytes);
                totalBytes += bytes;
            } catch (...) {
                callbackError = std::current_exception();
            }
        }
        if (fds[idx] >= 0) {
            ::close(fds[idx]);
            fds[idx] = -1;
        }
        io_uring_cqe_seen(&impl_->ring, cqe);
        ++doneCount;
    }
    closeAll();
    if (callbackError) std::rethrow_exception(callbackError);
    if (!firstError.empty()) {
        throw std::runtime_error(firstError);
    }

    return totalBytes;
}

}  // namespace skl

#else  // SKL_HAS_LIBURING

#include <stdexcept>

namespace skl {

struct UringBackend::Impl {};

UringBackend::UringBackend() {
    throw std::runtime_error(
        "UringBackend: liburing not compiled in (liburing missing at build time)");
}

UringBackend::~UringBackend() = default;

std::uint64_t UringBackend::RunCycle(const std::vector<std::string>&,
                                       std::size_t,
                                       const OnFileRead&) {
    throw std::runtime_error("UringBackend: not compiled in");
}

}  // namespace skl

#endif  // SKL_HAS_LIBURING
