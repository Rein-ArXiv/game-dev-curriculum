#pragma once

#include "io/io_backend.h"

#include <vector>

namespace skl {

// pread(2) 동기 백엔드.
//
// read와의 차이는 *시작 위치를 인자로 받는다는 점*. fd 내부 offset을 갱신하지
// 않으므로 다중 스레드가 같은 fd에 동시 pread를 호출해도 안전하다. 단일 스레드
// 워크로드에서는 read와 거의 같은 시스템 콜 비용을 보이지만, 인터페이스 차이가
// *측정 가능한 노이즈* 안에서 어떻게 드러나는지가 비교의 의미다.
class PreadBackend final : public IIoBackend {
public:
    std::uint64_t RunCycle(const std::vector<std::string>& paths,
                            std::size_t fileBytes,
                            const OnFileRead& onRead) override;

    const char* Name() const override { return "pread"; }

private:
    std::vector<unsigned char> buffer_;
};

}  // namespace skl
