#include "io/mmap_backend.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace skl {

std::uint64_t MmapBackend::RunCycle(const std::vector<std::string>& paths,
                                      std::size_t fileBytes,
                                      const OnFileRead& onRead) {
    std::uint64_t totalBytes = 0;

    for (const auto& path : paths) {
        const int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            throw std::runtime_error(std::string("open failed: ") + path
                                     + ": " + std::strerror(errno));
        }

        struct stat st {};
        if (::fstat(fd, &st) != 0 ||
            st.st_size < static_cast<off_t>(fileBytes)) {
            const std::string msg = std::string("fixture is shorter than mapping: ")
                                  + path;
            ::close(fd);
            throw std::runtime_error(msg);
        }

        void* p = ::mmap(nullptr, fileBytes, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) {
            const std::string msg = std::string("mmap failed: ") + path
                                  + ": " + std::strerror(errno);
            ::close(fd);
            throw std::runtime_error(msg);
        }

        // 매핑된 주소를 그대로 onRead에 넘긴다.
        // 워크로드의 sampled checksum이 페이지를 만지면서 *페이지 폴트가 발생*
        // 하고 커널이 페이지 캐시에서 매핑을 채운다. read와 달리 유저 공간으로의
        // 명시적 복사가 없다.
        onRead(static_cast<const unsigned char*>(p), fileBytes);

        if (::munmap(p, fileBytes) != 0) {
            const std::string msg = std::string("munmap failed: ") + path
                                  + ": " + std::strerror(errno);
            ::close(fd);
            throw std::runtime_error(msg);
        }
        ::close(fd);

        totalBytes += fileBytes;
    }

    return totalBytes;
}

}  // namespace skl
