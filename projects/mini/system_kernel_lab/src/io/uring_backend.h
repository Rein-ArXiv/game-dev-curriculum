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

    // 마지막 RunCycle 의 CQE 회계. 취소(IORING_OP_ASYNC_CANCEL) 경로가
    // *원 요청 CQE와 취소 요청 CQE를 모두 수확했는지* 를 밖에서 셀 수 있게
    // 노출한다. 관측 전용이며 RunCycle 의 동작에 영향을 주지 않는다.
    //
    // 정상 사이클: requestCqes == paths.size(), cancelSqes == cancelCqes == 0.
    // 대기 실패로 사이클을 포기한 경우: 남아 있던 요청 수 k 에 대해
    //   cancelSqes == k, cancelCqes == k, requestCqes == paths.size()
    // 즉 총 수확 CQE 는 paths.size() + k 다. leftoverCqes 는 언제나 0 이어야
    // 한다 — 배수가 끝난 뒤 CQ 링에 남은 CQE 는 다음 사이클이 자기 요청의
    // 완료로 오인할 쓰레기다.
    struct DrainStats {
        std::uint64_t requestCqes  = 0;  // 원 read 요청의 CQE (취소 배수 포함)
        std::uint64_t cancelSqes   = 0;  // 제출한 취소 요청 수
        std::uint64_t cancelCqes   = 0;  // 수확한 취소 요청 CQE 수
        std::uint64_t leftoverCqes = 0;  // 배수 후에도 링에 남아 있던 CQE 수
    };
    const DrainStats& LastDrainStats() const { return drain_; }

private:
    // 링 큐 깊이. 한 사이클 파일 수 + 여유. 생성 시 고정.
    static constexpr unsigned kQueueDepth = 64;

    // io_uring 구조체는 PIMPL 형태로 숨긴다. 헤더에 <liburing.h> 노출 안 함.
    struct Impl;
    Impl* impl_ = nullptr;

    DrainStats drain_{};
};

}  // namespace skl
