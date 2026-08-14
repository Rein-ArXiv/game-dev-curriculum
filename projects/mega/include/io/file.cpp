// file.h 의 플랫폼별 구현. positioned IO 만 사용한다.
//
// Linux 1차 타깃에서는 이 동기 pread/pwrite 가 IoSystem 의 스레드 풀 백엔드에
// 실린다. io_uring 백엔드(SQ/CQ 링으로 시스템 콜을 모으는 진짜 비동기)는 같은
// File 인터페이스 뒤에서 교체되는 지점으로 남긴다(system 시리즈 io_uring 챕터).

#include "io/file.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace mega {
namespace io {

#if defined(_WIN32)

bool File::Open(const char* path, FileMode mode) {
    Close();
    DWORD access = 0, disp = 0;
    switch (mode) {
        case FileMode::Read:      access = GENERIC_READ;                 disp = OPEN_EXISTING; break;
        case FileMode::Write:     access = GENERIC_WRITE;                disp = CREATE_ALWAYS; break;
        case FileMode::ReadWrite: access = GENERIC_READ | GENERIC_WRITE; disp = OPEN_ALWAYS;   break;
    }
    HANDLE h = CreateFileA(path, access, FILE_SHARE_READ, nullptr, disp, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    handle_ = reinterpret_cast<intptr_t>(h);
    return true;
}

void File::Close() {
    if (handle_ != -1) { CloseHandle(reinterpret_cast<HANDLE>(handle_)); handle_ = -1; }
}

int64_t File::Size() const {
    if (handle_ == -1) return -1;
    LARGE_INTEGER li;
    if (!GetFileSizeEx(reinterpret_cast<HANDLE>(handle_), &li)) return -1;
    return (int64_t)li.QuadPart;
}

IoResult File::ReadAt(uint64_t offset, void* buf, size_t n) const {
    if (handle_ == -1) return {false, 0};
    OVERLAPPED ov{};
    ov.Offset     = (DWORD)(offset & 0xFFFFFFFFu);
    ov.OffsetHigh = (DWORD)(offset >> 32);
    DWORD got = 0;
    BOOL ok = ReadFile(reinterpret_cast<HANDLE>(handle_), buf, (DWORD)n, &got, &ov);
    if (!ok && GetLastError() == ERROR_HANDLE_EOF) return {true, got};   // EOF 는 정상
    return {ok != 0, got};
}

IoResult File::WriteAt(uint64_t offset, const void* buf, size_t n) {
    if (handle_ == -1) return {false, 0};
    OVERLAPPED ov{};
    ov.Offset     = (DWORD)(offset & 0xFFFFFFFFu);
    ov.OffsetHigh = (DWORD)(offset >> 32);
    DWORD put = 0;
    BOOL ok = WriteFile(reinterpret_cast<HANDLE>(handle_), buf, (DWORD)n, &put, &ov);
    return {ok != 0, put};
}

#else  // POSIX

bool File::Open(const char* path, FileMode mode) {
    Close();
    int flags = 0;
    switch (mode) {
        case FileMode::Read:      flags = O_RDONLY;                   break;
        case FileMode::Write:     flags = O_WRONLY | O_CREAT | O_TRUNC; break;
        case FileMode::ReadWrite: flags = O_RDWR  | O_CREAT;         break;
    }
    int fd = ::open(path, flags, 0644);
    if (fd < 0) return false;
    handle_ = fd;
    return true;
}

void File::Close() {
    if (handle_ != -1) { ::close((int)handle_); handle_ = -1; }
}

int64_t File::Size() const {
    if (handle_ == -1) return -1;
    struct stat st;
    if (::fstat((int)handle_, &st) != 0) return -1;
    return (int64_t)st.st_size;
}

IoResult File::ReadAt(uint64_t offset, void* buf, size_t n) const {
    if (handle_ == -1) return {false, 0};
    ssize_t r = ::pread((int)handle_, buf, n, (off_t)offset);
    if (r < 0) return {false, 0};
    return {true, (size_t)r};
}

IoResult File::WriteAt(uint64_t offset, const void* buf, size_t n) {
    if (handle_ == -1) return {false, 0};
    ssize_t r = ::pwrite((int)handle_, buf, n, (off_t)offset);
    if (r < 0) return {false, 0};
    return {true, (size_t)r};
}

#endif

// 플랫폼 공통: 이동/소멸.
File::~File() { Close(); }

File::File(File&& o) noexcept : handle_(o.handle_) { o.handle_ = -1; }

File& File::operator=(File&& o) noexcept {
    if (this != &o) { Close(); handle_ = o.handle_; o.handle_ = -1; }
    return *this;
}

}  // namespace io
}  // namespace mega
