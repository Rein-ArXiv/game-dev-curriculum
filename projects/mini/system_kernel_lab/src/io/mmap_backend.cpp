#include "io/mmap_backend.h"

#include "fault/skl_syscalls.h"

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
        const int fd = SklOpen(path.c_str(), O_RDONLY);
        if (fd < 0) {
            throw std::runtime_error(std::string("open failed: ") + path
                                     + ": " + std::strerror(errno));
        }

        struct stat st {};
        if (SklFstat(fd, &st) != 0) {
            const std::string msg = std::string("fstat failed: ") + path
                                  + ": " + std::strerror(errno);
            ::close(fd);
            throw std::runtime_error(msg);
        }
        // mmap 기반 읽기는 *정규 파일에만* 정의된다. FIFO·소켓·문자 장치는
        // st_size 가 0 이라 "빈 파일" 과 구분되지 않으므로 조용히 0 바이트를
        // 돌려주는 대신 명시적으로 거부한다.
        if (!S_ISREG(st.st_mode)) {
            const std::string msg =
                std::string("mmap backend requires a regular file: ") + path;
            ::close(fd);
            throw std::runtime_error(msg);
        }

        // 파일이 fileBytes 보다 짧은 것은 EOF 이지 오류가 아니다
        // (io_backend.h 의 short read 계약). 파일 끝을 넘겨 매핑하면 그 영역
        // 접근이 SIGBUS 이므로 매핑 길이 자체를 파일 크기로 자른다.
        const std::size_t available = static_cast<std::size_t>(st.st_size);
        const std::size_t mapBytes  = fileBytes < available ? fileBytes : available;

        if (mapBytes == 0) {
            // mmap(length=0) 은 EINVAL 이다. 빈 파일은 매핑 없이 0 바이트로
            // 관측시킨다 — 다른 backend 와 같은 결과.
            static const unsigned char kEmpty = 0;
            onRead(&kEmpty, 0);
            ::close(fd);
            continue;
        }

        void* p = SklMmap(nullptr, mapBytes, PROT_READ, MAP_PRIVATE, fd, 0);
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
        onRead(static_cast<const unsigned char*>(p), mapBytes);

        if (SklMunmap(p, mapBytes) != 0) {
            const std::string msg = std::string("munmap failed: ") + path
                                  + ": " + std::strerror(errno);
            ::close(fd);
            throw std::runtime_error(msg);
        }
        ::close(fd);

        totalBytes += mapBytes;
    }

    return totalBytes;
}

}  // namespace skl
