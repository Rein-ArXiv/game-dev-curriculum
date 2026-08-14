#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace skl {

// 한 사이클 동안 N개 파일을 읽는 *모델* 의 추상.
//
// 구현체는 동기(read/pread/mmap)일 수도, 비동기(aio/io_uring)일 수도 있다.
// 인터페이스는 그 차이를 가린다: backend는 paths를 받아 자기 방식대로 읽고,
// *각 파일이 관찰 가능한 시점* 에 onRead 콜백을 호출한다.
//   - 동기 backend: read 직후 onRead.
//   - 비동기 backend: reap(완료 수확) 시점에 onRead. submit 순서와 다를 수 있다.
//
// 이 추상이 step02의 핵심 결정이다. 토글 UI가 들어오기 전에 *모듈 경계* 를
// 먼저 그어 두면 이후 회차에서 backend 추가가 인터페이스 변경 없이 끝난다.
class IIoBackend {
public:
    virtual ~IIoBackend() = default;

    using OnFileRead = std::function<void(const unsigned char* data,
                                           std::size_t bytes)>;

    // 한 사이클 = paths의 모든 파일을 fileBytes만큼 읽기.
    // 각 파일 관찰 가능 시점마다 onRead 호출.
    // data 포인터는 onRead 호출 중에만 유효하다. 호출자는 저장하지 않는다.
    // 비동기 구현도 RunCycle 안에서 모든 완료 수확과 onRead 호출을 끝내고 반환한다.
    // 반환: 이 사이클에서 처리한 총 byte.
    virtual std::uint64_t RunCycle(const std::vector<std::string>& paths,
                                    std::size_t fileBytes,
                                    const OnFileRead& onRead) = 0;

    // 프로파일러·HUD 표시용.
    virtual const char* Name() const = 0;
};

}  // namespace skl
