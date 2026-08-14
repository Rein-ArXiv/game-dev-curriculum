#pragma once

#include <cstdint>

namespace skl {

// /proc 기반 프로세스 카운터의 한 스냅샷.
//
// getrusage(RUSAGE_SELF) 로 읽는 카운터들. 권한 부담 없이 *현재 프로세스의
// 누적 통계* 를 가져온다. 사이클 시작·끝에서 각각 한 번 읽어 delta를 구하면
// "이 사이클에서 발생한 컨텍스트 스위치 수·페이지 폴트 수" 가 된다.
//
// perf_event_open과 비교하면:
//   - getrusage: 모든 사용자, 모든 환경에서 동작. 카운터 종류 제한적.
//   - perf_event_open: 더 풍부한 카운터(IPC, 캐시 미스 등). perf_event_paranoid
//     설정에 따라 권한 부담.
// 이 lab은 도구 단순성과 환경 호환성을 우선해 getrusage를 쓴다.
struct ProcCounters {
    long voluntaryCtxSw    = 0;  // ru_nvcsw  — 워커가 wait/yield로 양보한 횟수
    long involuntaryCtxSw  = 0;  // ru_nivcsw — 스케줄러가 시간 다 돼서 빼앗은 횟수
    long minorFaults       = 0;  // ru_minflt — 디스크 IO 없이 처리된 페이지 폴트
    long majorFaults       = 0;  // ru_majflt — 디스크 IO가 필요했던 페이지 폴트
};

// 현재 프로세스의 누적 카운터를 읽는다. 모든 환경에서 거의 항상 성공.
ProcCounters ReadProcCounters();

// b - a. 음수가 나오면 0으로 clamp (rusage는 monotonic이라 음수가 거의 없지만 안전).
inline ProcCounters DiffCounters(const ProcCounters& a, const ProcCounters& b) {
    auto d = [](long x, long y) -> long { return (y > x) ? (y - x) : 0; };
    return ProcCounters{
        d(a.voluntaryCtxSw,    b.voluntaryCtxSw),
        d(a.involuntaryCtxSw,  b.involuntaryCtxSw),
        d(a.minorFaults,       b.minorFaults),
        d(a.majorFaults,       b.majorFaults),
    };
}

}  // namespace skl
