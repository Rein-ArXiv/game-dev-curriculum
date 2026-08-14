// read_file — 파일 전체를 한 번에 읽어 오는 io 계층의 최소 진입점.
//
// 왜 io/ 에 있고 왜 <fstream> 이 아닌가: 계층 규칙(A3)상 파일 API 를 부를 수 있는
// 것은 net/ · platform/ · io/ 뿐이다. 게임·콘텐츠 로더가 각자 std::ifstream 을
// 열면 "누가 디스크를 만지는가"가 코드베이스 전체로 흩어지고, 그 지점을 교체
// (비동기 IO · 패키지 아카이브 · 인메모리 VFS)할 방법도 없어진다.
//
// 구현은 io::File 의 positioned IO 를 그대로 쓴다. 즉 이 헤더를 쓰는 모든 경로가
// io/file.cpp 의 진짜 시스템 콜(CreateFileA/ReadFile, open/pread)로 내려간다.
// 부분 읽기는 여기서 루프로 흡수하므로 호출자는 "전부 읽혔다"만 보면 된다.

#pragma once

#include "io/file.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mega {
namespace io {

// 신뢰할 수 없는 입력(에셋·설정)을 다루므로 상한을 강제한다. 상한을 넘으면
// 잘라 읽지 않고 실패시킨다 — 잘린 콘텐츠는 조용한 오작동의 원인이다.
inline bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out,
                          size_t maxBytes = 16u * 1024u * 1024u) {
    out.clear();
    File file;
    if (!file.Open(path.c_str(), FileMode::Read)) return false;
    const int64_t size = file.Size();
    if (size < 0 || static_cast<uint64_t>(size) > static_cast<uint64_t>(maxBytes)) return false;
    if (size == 0) return true;

    out.assign(static_cast<size_t>(size), 0);
    size_t done = 0;
    while (done < out.size()) {
        const IoResult result = file.ReadAt(done, out.data() + done, out.size() - done);
        if (!result.ok || result.bytes == 0) {   // 실패 또는 예상보다 이른 EOF
            out.clear();
            return false;
        }
        done += result.bytes;
    }
    return true;
}

// 텍스트로 읽는다. 줄 끝 변환은 하지 않는다(바이트 그대로) — CRLF 처리는
// 파서가 명시적으로 한다. 텍스트 모드의 암묵적 변환에 파서가 의존하면
// 플랫폼마다 다른 결과가 나오기 때문이다.
inline bool ReadTextFile(const std::string& path, std::string& out,
                         size_t maxBytes = 1024u * 1024u) {
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path, bytes, maxBytes)) return false;
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

}  // namespace io
}  // namespace mega
