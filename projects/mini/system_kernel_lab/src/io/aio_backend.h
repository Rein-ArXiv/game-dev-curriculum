#pragma once

#include "io/io_backend.h"

#include <vector>

namespace skl {

// POSIX AIO 백엔드 (aio_read / aio_suspend / aio_error / aio_return).
//
// "POSIX AIO"는 표준 인터페이스 이름이고, Linux에서의 *내부 구현* 은
// 환경 의존이다. glibc의 POSIX AIO는 일반적으로 *유저 공간 worker thread* 로
// 구현되어 있다. Linux native AIO(io_submit/io_getevents)와는 다르다.
// io_uring 결과와 비교할 때 *POSIX AIO의 구현 방식 자체* 가
// 측정 차이의 일부라는 점을 인지하고 본다.
//
// 이 구현은 *모든 파일을 한꺼번에 aio_read 제출* 후 *완료된 순서대로 onRead*
// 호출하는 batched 패턴이다. 동기 backend(read/pread)는 한 파일씩 직렬,
// AIO는 N개 동시 제출이라 *측정 차이가 모델 자체* 에서 나온다.
class AioBackend final : public IIoBackend {
public:
    std::uint64_t RunCycle(const std::vector<std::string>& paths,
                            std::size_t fileBytes,
                            const OnFileRead& onRead) override;

    const char* Name() const override { return "aio"; }
};

}  // namespace skl
