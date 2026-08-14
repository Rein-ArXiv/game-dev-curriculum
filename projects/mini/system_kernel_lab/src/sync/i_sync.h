#pragma once

namespace skl {

// 동기화 프리미티브 추상.
//
// 워커 풀의 *작업 큐 보호용* 으로 쓴다. 워크로드의 메인 스레드가 작업 N개를
// 큐에 push하고, 워커들이 push/pop을 sync로 보호하며 작업을 받는다.
//
// 모든 구현이 uncontended fast path를 가진다. mutex/futex의 시스템 콜은
// 보통 contended slow path에서만 발생한다. 이 lab은 구현 우열의 보편 법칙이
// 아니라 현재 작업 큐의 경합 분포에서 생기는 비용을 측정한다.
class ISync {
public:
    virtual ~ISync() = default;

    virtual void Lock()   = 0;
    virtual void Unlock() = 0;

    virtual const char* Name() const = 0;
};

}  // namespace skl
