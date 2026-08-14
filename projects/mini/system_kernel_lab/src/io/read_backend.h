#pragma once

#include "io/io_backend.h"

#include <vector>

namespace skl {

// read(2) 동기 백엔드. baseline.
// 매 파일 open → read 루프 → close. 한 파일 읽기가 끝난 직후 onRead 호출.
class ReadBackend final : public IIoBackend {
public:
    std::uint64_t RunCycle(const std::vector<std::string>& paths,
                            std::size_t fileBytes,
                            const OnFileRead& onRead) override;

    const char* Name() const override { return "read"; }

private:
    // 파일당 임시 버퍼. 사이클 간 재사용.
    std::vector<unsigned char> buffer_;
};

}  // namespace skl
