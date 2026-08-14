#include "io/read_backend.h"

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
        const int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            throw std::runtime_error(std::string("open failed: ") + path
                                     + ": " + std::strerror(errno));
        }

        std::size_t pos = 0;
        while (pos < fileBytes) {
            const ssize_t n = ::read(fd, buffer_.data() + pos,
                                      fileBytes - pos);
            if (n < 0) {
                if (errno == EINTR) continue;
                const std::string msg = std::string("read failed: ") + path
                                      + ": " + std::strerror(errno);
                ::close(fd);
                throw std::runtime_error(msg);
            }
            if (n == 0) break;  // EOF (파일이 fixture보다 작을 때)
            pos += static_cast<std::size_t>(n);
        }
        ::close(fd);

        if (pos != fileBytes) {
            throw std::runtime_error("short read: " + path + ": expected "
                                     + std::to_string(fileBytes) + ", got "
                                     + std::to_string(pos));
        }
        onRead(buffer_.data(), pos);
        totalBytes += pos;
    }

    return totalBytes;
}

}  // namespace skl
