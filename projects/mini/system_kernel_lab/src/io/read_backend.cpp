#include "io/read_backend.h"

#include "fault/skl_syscalls.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace skl {

std::uint64_t ReadBackend::RunCycle(const std::vector<std::string>& paths,
                                      std::size_t fileBytes,
                                      const OnFileRead& onRead) {
    if (buffer_.size() < fileBytes) {
        buffer_.resize(fileBytes);
    }

    std::uint64_t totalBytes = 0;

    for (const auto& path : paths) {
        const int fd = SklOpen(path.c_str(), O_RDONLY);
        if (fd < 0) {
            throw std::runtime_error(std::string("open failed: ") + path
                                     + ": " + std::strerror(errno));
        }

        std::size_t pos = 0;
        while (pos < fileBytes) {
            const ssize_t n = SklRead(fd, buffer_.data() + pos,
                                       fileBytes - pos);
            if (n < 0) {
                if (errno == EINTR) continue;
                const std::string msg = std::string("read failed: ") + path
                                      + ": " + std::strerror(errno);
                ::close(fd);
                throw std::runtime_error(msg);
            }
            if (n == 0) break;  // EOF (파일이 fileBytes보다 짧을 때)
            pos += static_cast<std::size_t>(n);
        }
        ::close(fd);

        // pos < fileBytes 는 오류가 아니라 EOF 다 (io_backend.h 의 short read
        // 계약). 실제로 읽은 만큼만 넘기고 그만큼만 센다.
        onRead(buffer_.data(), pos);
        totalBytes += pos;
    }

    return totalBytes;
}

}  // namespace skl
