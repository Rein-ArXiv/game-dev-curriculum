#include "io/aio_backend.h"

#if defined(SKL_HAS_POSIX_AIO)

#include "fault/skl_syscalls.h"

#include <aio.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace skl {

namespace {

// AIO 전용 seam. <aio.h> 를 공통 헤더로 끌어오지 않으려고 여기 둔다.
int SklAioRead(aiocb* cb) {
    if (const int e = SklFaultCheck("aio_read")) {
        errno = e;
        return -1;
    }
    return ::aio_read(cb);
}

int SklAioSuspend(const aiocb* const list[], int nent,
                   const timespec* timeout) {
    if (const int e = SklFaultCheck("aio_suspend")) {
        errno = e;
        return -1;
    }
    return ::aio_suspend(list, nent, timeout);
}

// aio_cancel / aio_return 은 *취소 후 수확* 규약의 관측점이다. 주입 없이
// 무장만 해 두면 FaultCallCount 로 "요청 n 개를 전부 취소하고 전부
// aio_return 까지 수확했는가" 를 세어 확인할 수 있다.
int SklAioCancel(int fd, aiocb* cb) {
    if (SklFaultTriggered("aio_cancel")) return AIO_NOTCANCELED;
    return ::aio_cancel(fd, cb);
}

ssize_t SklAioReturn(aiocb* cb) {
    if (const int e = SklFaultCheck("aio_return")) {
        errno = e;
        return -1;
    }
    return ::aio_return(cb);
}

}  // namespace

std::uint64_t AioBackend::RunCycle(const std::vector<std::string>& paths,
                                     std::size_t fileBytes,
                                     const OnFileRead& onRead) {
    const std::size_t n = paths.size();
    if (n == 0) return 0;

    // 사이클마다 fresh aiocb 배열·버퍼·fd 배열을 잡는다. backend 멤버에 들고
    // 재사용하면 alloc 비용이 줄지만, 이번 회차는 *모델의 비용* 을 측정에 그대로
    // 드러내기 위해 재사용을 미룬다. retain 토글이 들어가면 비교한다.
    std::vector<aiocb>                       cbs(n);
    std::vector<aiocb*>                      cbsList(n);
    std::vector<std::vector<unsigned char>>  bufs(n);
    std::vector<int>                         fds(n, -1);
    std::vector<bool>                        done(n, false);
    auto closeAll = [&fds]() {
        for (int& fd : fds) {
            if (fd >= 0) {
                ::close(fd);
                fd = -1;
            }
        }
    };

    // open + aiocb 준비. 제출 전 실패가 나면 pending AIO가 없다.
    for (std::size_t i = 0; i < n; ++i) {
        fds[i] = SklOpen(paths[i].c_str(), O_RDONLY);
        if (fds[i] < 0) {
            closeAll();
            throw std::runtime_error(std::string("open failed: ") + paths[i]
                                     + ": " + std::strerror(errno));
        }
        bufs[i].resize(fileBytes);

        std::memset(&cbs[i], 0, sizeof(aiocb));
        cbs[i].aio_fildes = fds[i];
        cbs[i].aio_buf    = bufs[i].data();
        cbs[i].aio_nbytes = fileBytes;
        cbs[i].aio_offset = 0;
        cbsList[i] = &cbs[i];
    }

    std::uint64_t totalBytes = 0;
    std::string firstError;
    auto finishCompleted = [&](std::size_t i, bool invokeRead) {
        if (done[i]) return false;

        const int err = ::aio_error(&cbs[i]);
        if (err == EINPROGRESS) return false;
        // 완료된 요청은 성공·실패와 무관하게 정확히 한 번 aio_return 해야
        // 구현이 보유한 요청 자원이 회수된다.
        const ssize_t bytes = SklAioReturn(&cbs[i]);
        if (err != 0) {
            if (firstError.empty()) {
                firstError = std::string("aio_error: ") + paths[i]
                           + ": " + std::strerror(err);
            }
        } else {
            if (bytes < 0) {
                if (firstError.empty()) {
                    firstError = std::string("aio_return failed: ") + paths[i]
                               + ": " + std::strerror(errno);
                }
            } else if (invokeRead) {
                // bytes < fileBytes 는 EOF 이지 오류가 아니다
                // (io_backend.h 의 short read 계약).
                const std::size_t got = static_cast<std::size_t>(bytes);
                onRead(bufs[i].data(), got);
                totalBytes += got;
            }
        }

        if (fds[i] >= 0) {
            ::close(fds[i]);
            fds[i] = -1;
        }
        cbsList[i] = nullptr;  // 완료된 요청은 다음 aio_suspend 대기 목록에서 제외.
        done[i] = true;
        return true;
    };

    auto drainSubmitted = [&](std::size_t submitted, bool invokeRead) {
        std::size_t doneCount = 0;
        bool suspendUsable = true;
        while (doneCount < submitted) {
            if (suspendUsable) {
                const int rc = SklAioSuspend(cbsList.data(),
                                             static_cast<int>(submitted),
                                             nullptr);
                if (rc < 0) {
                    if (errno == EINTR) continue;
                    if (firstError.empty()) {
                        firstError = std::string("aio_suspend failed: ")
                                   + std::strerror(errno);
                    }
                    // 버퍼와 aiocb를 해제하기 전에 모든 요청을 반드시
                    // aio_return까지 수확해야 한다. 대기 API가 실패하면
                    // 취소를 요청하고 aio_error를 polling한다.
                    suspendUsable = false;
                    for (std::size_t i = 0; i < submitted; ++i) {
                        if (!done[i]) SklAioCancel(fds[i], &cbs[i]);
                    }
                }
            }

            bool progressed = false;
            for (std::size_t i = 0; i < submitted; ++i) {
                if (finishCompleted(i, invokeRead)) {
                    ++doneCount;
                    progressed = true;
                }
            }
            if (!suspendUsable && !progressed) {
                std::this_thread::yield();
            }
        }
    };

    // aio_read 제출 단계.
    std::size_t submitted = 0;
    for (; submitted < n; ++submitted) {
        if (SklAioRead(&cbs[submitted]) < 0) {
            const std::string msg = std::string("aio_read failed: ")
                                  + paths[submitted]
                                  + ": " + std::strerror(errno);
            for (std::size_t j = 0; j < submitted; ++j) {
                SklAioCancel(fds[j], &cbs[j]);
            }
            drainSubmitted(submitted, /*invokeRead=*/false);
            closeAll();
            throw std::runtime_error(msg);
        }
    }

    // 수확 단계. aio_suspend로 *어느 하나라도 완료* 까지 대기, 완료된 것만
    // 처리하고 다시 대기.
    drainSubmitted(n, /*invokeRead=*/true);
    if (!firstError.empty()) {
        closeAll();
        throw std::runtime_error(firstError);
    }

    return totalBytes;
}

}  // namespace skl

#else  // SKL_HAS_POSIX_AIO

#include <stdexcept>

namespace skl {

std::uint64_t AioBackend::RunCycle(const std::vector<std::string>&,
                                     std::size_t,
                                     const OnFileRead&) {
    throw std::runtime_error(
        "AioBackend: POSIX AIO not compiled in (librt missing at build time)");
}

}  // namespace skl

#endif  // SKL_HAS_POSIX_AIO
