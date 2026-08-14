#pragma once

#include "io/io_backend.h"

namespace skl {

// mmap(2) 백엔드.
//
// 파일 전체를 가상 메모리 영역으로 매핑한다. read 시스템 콜이 *없고*, 데이터를
// 처음 만지는 순간 페이지 폴트가 발생해 커널이 파일 페이지를 현재 프로세스의
// 주소 공간에 연결한다. read와의 가장 큰 차이는 *명시적 데이터 복사가 없다는 점* —
// read는 커널 페이지 캐시 → 유저 버퍼로 memcpy가 매번 일어나지만, mmap은 파일
// 페이지가 프로세스의 가상 주소에 매핑된다.
//
// 임시 버퍼가 필요 없으므로 backend는 상태가 거의 없다.
class MmapBackend final : public IIoBackend {
public:
    std::uint64_t RunCycle(const std::vector<std::string>& paths,
                            std::size_t fileBytes,
                            const OnFileRead& onRead) override;

    const char* Name() const override { return "mmap"; }
};

}  // namespace skl
