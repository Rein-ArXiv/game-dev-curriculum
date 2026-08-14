#include "io/uring_backend.h"

#if defined(SKL_HAS_LIBURING)

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <liburing.h>
#include <stdexcept>
#include <string>
#include <limits>
#include <unistd.h>
#include <vector>

namespace skl {

struct UringBackend::Impl {
    io_uring ring{};
    bool     ringInited = false;
};

UringBackend::UringBackend() : impl_(new Impl) {
    const int rc = io_uring_queue_init(kQueueDepth, &impl_->ring, /*flags=*/0);
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

    // 사이클마다 fresh 자원. AioBackend와 같은 정책.
    std::vector<int>                         fds(n, -1);
    std::vector<std::vector<unsigned char>>  bufs(n);
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
        const int rc = io_uring_queue_init(kQueueDepth, &impl_->ring, /*flags=*/0);
        if (rc < 0) {
            throw std::runtime_error(std::string("io_uring reset failed: ")
                                     + std::strerror(-rc));
        }
        impl_->ringInited = true;
    };

    // open + 버퍼 준비. submit 전 실패가 나도 링에 미제출 SQE가 남지 않는다.
    for (std::size_t i = 0; i < n; ++i) {
        fds[i] = ::open(paths[i].c_str(), O_RDONLY);
        if (fds[i] < 0) {
            closeAll();
            throw std::runtime_error(std::string("open failed: ") + paths[i]
                                     + ": " + std::strerror(errno));
        }
        bufs[i].resize(fileBytes);
    }

    // SQE 채움.
    for (std::size_t i = 0; i < n; ++i) {
        io_uring_sqe* sqe = io_uring_get_sqe(&impl_->ring);
        if (!sqe) {
            closeAll();
            resetRing();
            throw std::runtime_error("io_uring_get_sqe returned null");
        }
        io_uring_prep_read(sqe, fds[i], bufs[i].data(),
                            static_cast<unsigned>(fileBytes), 0);
        // user_data로 fileIdx를 박아 CQE 수확 시 매핑.
        io_uring_sqe_set_data64(sqe, static_cast<__u64>(i));
    }

    // 일괄 제출. 여러 read 요청을 하나의 submit 경계로 밀어 넣는다.
    const int submitted = io_uring_submit(&impl_->ring);
    if (submitted < 0) {
        closeAll();
        // 제출 실패 시 준비된 SQE가 링에 남을 수 있다. SQE는 곧 소멸할
        // 버퍼와 닫힌 fd를 가리키므로 다음 사이클 전에 반드시 버린다.
        resetRing();
        throw std::runtime_error(std::string("io_uring_submit failed: ")
                                 + std::strerror(-submitted));
    }
    if (submitted != static_cast<int>(n)) {
        const int drainCount = submitted < static_cast<int>(n)
                             ? submitted
                             : static_cast<int>(n);
        for (int i = 0; i < drainCount; ++i) {
            io_uring_cqe* cqe = nullptr;
            if (io_uring_wait_cqe(&impl_->ring, &cqe) < 0) break;
            io_uring_cqe_seen(&impl_->ring, cqe);
        }
        closeAll();
        resetRing();
        throw std::runtime_error("io_uring_submit submitted fewer SQEs than expected");
    }

    // CQE 수확. 완료 순서대로 onRead.
    std::uint64_t totalBytes = 0;
    std::size_t doneCount = 0;
    std::string firstError;
    while (doneCount < n) {
        io_uring_cqe* cqe = nullptr;
        const int waitRc = io_uring_wait_cqe(&impl_->ring, &cqe);
        if (waitRc < 0) {
            if (waitRc == -EINTR) continue;
            closeAll();
            resetRing();
            throw std::runtime_error(std::string("io_uring_wait_cqe failed: ")
                                     + std::strerror(-waitRc));
        }

        const std::uint64_t idx = io_uring_cqe_get_data64(cqe);
        if (idx >= n) {
            if (firstError.empty()) {
                firstError = "io_uring returned invalid user_data";
            }
            io_uring_cqe_seen(&impl_->ring, cqe);
            ++doneCount;
            continue;
        }
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

        if (bytes != fileBytes) {
            if (firstError.empty()) {
                firstError = std::string("short io_uring read: ") + paths[idx]
                           + ": expected " + std::to_string(fileBytes)
                           + ", got " + std::to_string(bytes);
            }
        } else {
            onRead(bufs[idx].data(), bytes);
            totalBytes += bytes;
        }
        if (fds[idx] >= 0) {
            ::close(fds[idx]);
            fds[idx] = -1;
        }
        io_uring_cqe_seen(&impl_->ring, cqe);
        ++doneCount;
    }
    if (!firstError.empty()) {
        closeAll();
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
