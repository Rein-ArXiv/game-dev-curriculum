#pragma once

#include "sync/i_sync.h"

#include <mutex>

namespace skl {

// std::mutex 기반.
//
// 일반적인 libstdc++ + glibc/NPTL 구현에서 std::mutex는 pthread mutex를
// 사용하며, uncontended fast path는 유저 공간 atomic 경로로 처리된다.
// 이는 C++ 표준 보장이 아니라 해당 표준 라이브러리 구현의 성질이다.
//
// 즉 "uncontended에서 mutex가 비싸다"는 통념은 glibc 환경에서 부정확하다.
// 직접 futex mutex와 비용이 비슷할 수 있지만 공정성, 적응형 spin, 구현 세부와
// 경합 분포가 달라 결과를 미리 단정할 수 없다.
class MutexSync final : public ISync {
public:
    void Lock()   override { mutex_.lock(); }
    void Unlock() override { mutex_.unlock(); }

    const char* Name() const override { return "mutex"; }

private:
    std::mutex mutex_;
};

}  // namespace skl
