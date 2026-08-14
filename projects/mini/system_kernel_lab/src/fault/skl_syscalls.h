#pragma once

#include "fault/fault_inject.h"

#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace skl {

// syscall seam — 제품 코드는 ::read 대신 SklRead 를 부른다.
// 주입이 무장돼 있지 않으면 relaxed atomic load 하나를 더 지날 뿐,
// 인자·반환값·errno 규약은 원래 syscall 과 완전히 같다.

inline int SklOpen(const char* path, int flags) {
    if (const int e = SklFaultCheck("open")) {
        errno = e;
        return -1;
    }
    return ::open(path, flags);
}

inline ssize_t SklRead(int fd, void* buf, std::size_t count) {
    if (const int e = SklFaultCheck("read")) {
        errno = e;
        return -1;
    }
    return ::read(fd, buf, count);
}

inline ssize_t SklPread(int fd, void* buf, std::size_t count, off_t offset) {
    if (const int e = SklFaultCheck("pread")) {
        errno = e;
        return -1;
    }
    return ::pread(fd, buf, count, offset);
}

inline int SklFstat(int fd, struct stat* out) {
    if (const int e = SklFaultCheck("fstat")) {
        errno = e;
        return -1;
    }
    return ::fstat(fd, out);
}

inline void* SklMmap(void* addr, std::size_t length, int prot, int flags,
                      int fd, off_t offset) {
    if (const int e = SklFaultCheck("mmap")) {
        errno = e;
        return MAP_FAILED;
    }
    return ::mmap(addr, length, prot, flags, fd, offset);
}

inline int SklMunmap(void* addr, std::size_t length) {
    if (const int e = SklFaultCheck("munmap")) {
        errno = e;
        return -1;
    }
    return ::munmap(addr, length);
}

inline int SklSetPriority(int which, id_t who, int value) {
    if (const int e = SklFaultCheck("setpriority")) {
        errno = e;
        return -1;
    }
    return ::setpriority(which, who, value);
}

// pthread_* 는 errno 가 아니라 *오류 번호를 직접 반환* 한다. 주입도 같은
// 규약을 따른다 — 0 이 아닌 값이 실패다.
inline int SklPthreadSetSchedParam(pthread_t thread, int policy,
                                    const sched_param* param) {
    if (const int e = SklFaultCheck("pthread_setschedparam")) {
        return e;
    }
    return ::pthread_setschedparam(thread, policy, param);
}

}  // namespace skl
