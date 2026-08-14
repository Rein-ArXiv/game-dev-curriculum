#include "io/io_backend_factory.h"

#include "io/aio_backend.h"
#include "io/mmap_backend.h"
#include "io/pread_backend.h"
#include "io/read_backend.h"
#include "io/uring_backend.h"

#include <cstdio>
#include <memory>
#include <stdexcept>

namespace skl {

bool BackendCompiledIn(BackendType type) {
    switch (type) {
        case BackendType::Read:
        case BackendType::Pread:
        case BackendType::Mmap:
            return true;
        case BackendType::Aio:
#if defined(SKL_HAS_POSIX_AIO)
            return true;
#else
            return false;
#endif
        case BackendType::Uring:
#if defined(SKL_HAS_LIBURING)
            return true;
#else
            return false;
#endif
    }
    return false;
}

std::unique_ptr<IIoBackend> CreateBackend(BackendType type,
                                           BackendType* actualType) {
    auto setActual = [actualType](BackendType actual) {
        if (actualType) *actualType = actual;
    };
    switch (type) {
        case BackendType::Read:
            setActual(BackendType::Read);
            return std::make_unique<ReadBackend>();
        case BackendType::Pread:
            setActual(BackendType::Pread);
            return std::make_unique<PreadBackend>();
        case BackendType::Mmap:
            setActual(BackendType::Mmap);
            return std::make_unique<MmapBackend>();

        case BackendType::Aio:
#if defined(SKL_HAS_POSIX_AIO)
            setActual(BackendType::Aio);
            return std::make_unique<AioBackend>();
#else
            std::fprintf(stderr,
                "skl: aio not compiled in, falling back to read\n");
            setActual(BackendType::Read);
            return std::make_unique<ReadBackend>();
#endif

        case BackendType::Uring:
#if defined(SKL_HAS_LIBURING)
            // 런타임 unavailable(예: 커널 < 5.1, 권한 차단)이면
            // UringBackend 생성자가 throw — 폴백.
            try {
                setActual(BackendType::Uring);
                return std::make_unique<UringBackend>();
            } catch (const std::exception& e) {
                std::fprintf(stderr,
                    "skl: io_uring runtime init failed (%s), "
                    "falling back to read\n", e.what());
                setActual(BackendType::Read);
                return std::make_unique<ReadBackend>();
            }
#else
            std::fprintf(stderr,
                "skl: io_uring not compiled in, falling back to read\n");
            setActual(BackendType::Read);
            return std::make_unique<ReadBackend>();
#endif
    }
    setActual(BackendType::Read);
    return std::make_unique<ReadBackend>();
}

}  // namespace skl
