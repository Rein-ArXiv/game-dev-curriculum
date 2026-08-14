#include "io/pread_backend.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <unistd.h>

namespace skl {

std::uint64_t PreadBackend::RunCycle(const std::vector<std::string>& paths,
                                       std::size_t fileBytes,
                                       const OnFileRead& onRead) {
    if (buffer_.size() < fileBytes) {
        buffer_.resize(fileBytes);
    }

    std::uint64_t totalBytes = 0;

    for (const auto& path : paths) {
        const int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            throw std::runtime_error(std::string("open failed: ") + path
                                     + ": " + std::strerror(errno));
        }

        std::size_t pos = 0;
        while (pos < fileBytes) {
            // pread: 시작 offset을 인자로. fd 내부 offset은 안 바뀐다.
            const ssize_t n = ::pread(fd, buffer_.data() + pos,
                                       fileBytes - pos,
                                       static_cast<off_t>(pos));
            if (n < 0) {
                if (errno == EINTR) continue;
                const std::string msg = std::string("pread failed: ") + path
                                      + ": " + std::strerror(errno);
                ::close(fd);
                throw std::runtime_error(msg);
            }
            if (n == 0) break;
            pos += static_cast<std::size_t>(n);
        }
        ::close(fd);

        if (pos != fileBytes) {
            throw std::runtime_error("short pread: " + path + ": expected "
                                     + std::to_string(fileBytes) + ", got "
                                     + std::to_string(pos));
        }
        onRead(buffer_.data(), pos);
        totalBytes += pos;
    }

    return totalBytes;
}

}  // namespace skl
