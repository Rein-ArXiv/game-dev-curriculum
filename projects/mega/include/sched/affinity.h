// 스레드 배치 — affinity, 우선순위, NUMA 힌트.
//
// Job 워커는 코어에 고정될 때 가장 빠르다. 워커가 코어 사이를 떠다니면 L1/L2
// 캐시가 매번 식고, NUMA 머신에서는 워커 로컬 메모리 풀이 cross-node 접근이
// 되어 1.5~2배 느려진다(system 시리즈 NUMA 측정). 그래서 워커 ID → 코어를
// 고정하고, 메모리 풀도 워커 로컬로 둔다.
//
// 이 헤더는 플랫폼 무관 인터페이스만 노출한다. 실제 구현(SetThreadAffinityMask /
// sched_setaffinity 등)은 affinity.cpp 에 격리해 <windows.h> 오염을 막는다.

#pragma once

#include <cstdint>

namespace mega {
namespace sched {

enum class Priority {
    Idle,      // 백그라운드(에셋 프리페치 등)
    Low,
    Normal,
    High,      // 프레임 임계 워커 (Linux 는 SCHED_FIFO 라 CAP_SYS_NICE 필요)
    Realtime,  // 오디오 콜백 급(권한 필요할 수 있음)
};

// 논리 CPU(하드웨어 스레드) 수.
unsigned HardwareThreads();

// 현재 스레드를 단일 코어에 고정. 성공 시 true.
bool PinThreadToCore(int core);

// 현재 스레드의 affinity 마스크 설정(비트 i = 코어 i 허용). 성공 시 true.
bool SetAffinityMask(uint64_t mask);

// 현재 스레드 스케줄링 우선순위 힌트. 권한 부족 시 false 일 수 있다.
bool SetThreadPriorityHint(Priority p);

// 현재 스레드가 지금 실행 중인 논리 CPU 번호. 알 수 없으면 -1.
int CurrentCore();

// 코어가 속한 NUMA 노드. 단일 노드/미지원이면 0, 오류면 -1.
int NumaNodeOfCore(int core);

// RAII: 현재 스레드를 코어에 고정하고, 소멸 시 이전 마스크로 원복.
class ScopedAffinity {
public:
    explicit ScopedAffinity(int core);
    ~ScopedAffinity();
    ScopedAffinity(const ScopedAffinity&) = delete;
    ScopedAffinity& operator=(const ScopedAffinity&) = delete;
    bool Ok() const { return ok_; }
private:
    uint64_t prevMask_ = 0;
    bool     ok_       = false;
};

}  // namespace sched
}  // namespace mega
