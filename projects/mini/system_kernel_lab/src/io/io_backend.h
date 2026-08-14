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

    // 한 사이클 = paths의 모든 파일을 *최대* fileBytes까지 읽기.
    // 각 파일 관찰 가능 시점마다 onRead 호출.
    // data 포인터는 onRead 호출 중에만 유효하다. 호출자는 저장하지 않는다.
    // 비동기 구현도 RunCycle 안에서 모든 완료 수확과 onRead 호출을 끝내고 반환한다.
    // 반환: 이 사이클에서 처리한 총 byte.
    //
    // ── 짧은 파일(short read) 계약 ────────────────────────────────────────
    // 파일이 fileBytes보다 짧은 것은 *오류가 아니라 EOF* 다. POSIX read(2)에서
    // 요청보다 적게 돌아오는 것은 정상이고, 0이 EOF이며, 오류는 -1로 따로
    // 알린다. 이 인터페이스도 그 구분을 그대로 따른다.
    //
    //   - 모든 구현은 각 path마다 onRead를 *정확히 한 번* 부른다.
    //   - 그때 넘기는 크기 n은 실제로 읽은 바이트이고 0 <= n <= fileBytes 다.
    //     파일이 fileBytes보다 짧으면 n은 파일 크기, 비어 있으면 0이다.
    //   - 반환값은 그 n의 합이다. 따라서 짧은 파일이 섞이면 반환값이
    //     paths.size() * fileBytes 보다 작다 — 호출자가 판단할 몫이다.
    //   - *진짜* I/O 오류(syscall이 -1, aio_error != 0, cqe->res < 0,
    //     mmap/fstat 실패, mmap backend에 정규 파일이 아닌 경로)는 여전히
    //     std::runtime_error 로 throw 한다.
    //
    // 왜 "짧으면 throw"가 아닌가: backend는 fixture가 얼마나 커야 하는지 모른다.
    // "내가 요청한 것보다 파일이 작다"와 "파일이 여기서 끝난다"를 구분할 수 있는
    // 것은 호출자뿐이다. 게다가 5개 구현이 이 판정에서 일치할 수가 없다 —
    // mmap은 매핑 후에는 EOF를 관측조차 못 하고, aio/uring은 완료 단위로만 안다.
    // 바이트 수를 돌려주는 쪽이 예외보다 정보량이 많고, 다섯 구현이 같은 값으로
    // 수렴한다.
    virtual std::uint64_t RunCycle(const std::vector<std::string>& paths,
                                    std::size_t fileBytes,
                                    const OnFileRead& onRead) = 0;

    // 프로파일러·HUD 표시용.
    virtual const char* Name() const = 0;
};

}  // namespace skl
