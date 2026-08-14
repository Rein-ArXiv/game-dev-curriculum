// File — 위치 지정(positioned) 동기 IO 파일 핸들.
//
// seek + read 는 두 연산이라 같은 핸들을 여러 스레드가 쓰면 위치가 꼬인다. 그래서
// 위치를 인자로 받는 원자적 positioned IO 만 노출한다(POSIX pread/pwrite, Windows
// 는 OVERLAPPED.Offset). 이러면 한 파일에 여러 비동기 읽기를 동시에 걸어도 안전
// 하다 — IoSystem 이 이 위에서 워커별로 읽기를 흩뿌린다.
//
// 플랫폼 핸들은 intptr_t 하나로 추상화(-1 = 무효)해 헤더에서 <windows.h> 를
// 들이지 않는다. 실제 시스템 콜은 file.cpp 에 격리된다.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mega {
namespace io {

enum class FileMode {
    Read,       // 기존 파일 읽기
    Write,      // 새로 만들거나 0으로 자르고 쓰기
    ReadWrite,  // 만들거나 열어 읽기·쓰기(자르지 않음)
};

struct IoResult {
    bool   ok    = false;
    size_t bytes = 0;
};

class File {
public:
    File() = default;
    ~File();
    File(File&& o) noexcept;
    File& operator=(File&& o) noexcept;
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    bool    Open(const char* path, FileMode mode);
    void    Close();
    bool    IsOpen() const { return handle_ != -1; }
    int64_t Size() const;                                   // 바이트, 실패 시 -1

    // 위치 지정 읽기/쓰기(스레드 안전: 핸들 위치를 바꾸지 않음).
    IoResult ReadAt(uint64_t offset, void* buf, size_t n) const;
    IoResult WriteAt(uint64_t offset, const void* buf, size_t n);

private:
    intptr_t handle_ = -1;   // POSIX fd 또는 Windows HANDLE, -1 = 무효
};

}  // namespace io
}  // namespace mega
