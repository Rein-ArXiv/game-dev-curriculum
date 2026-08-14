#pragma once

#include "io/io_backend.h"

#include <vector>

namespace skl {

// io_uring 백엔드 (liburing 래퍼 사용).
//
// io_uring은 커널 5.1부터 도입된 비동기 IO 인터페이스다. 두 개의 링버퍼(SQ, CQ)
// 를 유저-커널 공유 메모리로 두고, 시스템 콜 *경계 자체* 를 줄인다. read 시스템
// 콜이 파일당 한 번씩 일어나는 모델과 달리, io_uring은 *모든 SQE를 큐에 채운 뒤
// 한 번의 io_uring_submit* 으로 일괄 제출한다.
//
// 생성자에서 io_uring_queue_init이 실패하면 throw — factory가 catch해
// ReadBackend로 폴백한다. 런타임에 커널이 io_uring을 지원하지 않거나 권한 부족
// 환경에서 안전한 동작을 보장.
class UringBackend final : public IIoBackend {
public:
    UringBackend();
    ~UringBackend() override;

    UringBackend(const UringBackend&) = delete;
    UringBackend& operator=(const UringBackend&) = delete;

    std::uint64_t RunCycle(const std::vector<std::string>& paths,
                            std::size_t fileBytes,
                            const OnFileRead& onRead) override;

    const char* Name() const override { return "io_uring"; }

private:
    // 링 큐 깊이. 한 사이클 파일 수 + 여유. 생성 시 고정.
    static constexpr unsigned kQueueDepth = 64;

    // io_uring 구조체는 PIMPL 형태로 숨긴다. 헤더에 <liburing.h> 노출 안 함.
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace skl
