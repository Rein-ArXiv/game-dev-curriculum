#pragma once

namespace skl {

// 워커 스레드의 스케줄링 정책 토글.
//
// 스케줄링은 *워커 시작 시 한 번 적용* 되는 동작이라 ISync 같은 가상 함수
// 인터페이스 대신 *enum + apply 함수* 로 단순하게 둔다.
//
// 적용은 *best-effort*. 권한 부족, 환경 부적합 시 stderr에 안내 + 기본
// (SCHED_OTHER nice 0) 으로 동작 — 도구가 죽지 않는다.
enum class SchedType : int {
    Default = 0,   // SCHED_OTHER, nice 0 (시스템 기본)
    Nice    = 1,   // SCHED_OTHER, nice +10 (양보 많이)
    Fifo    = 2,   // SCHED_FIFO, prio 50 (실시간, 다른 nice 스레드 선점)
    Cgroup  = 3,   // 명시 승인한 현재 cgroup의 cpu.weight를 200으로
};

constexpr int kSchedTypeCount = 4;

inline constexpr const char* kSchedTypeLabels[kSchedTypeCount] = {
    "default", "nice +10", "SCHED_FIFO", "cgroup weight 200",
};

// 현재 스레드에 정책 적용. 실패 시 stderr 메시지 + false.
// false여도 스레드는 그대로 동작 (기본 정책 유지).
bool ApplySchedToCurrentThread(SchedType type);

// 측정 시작 전 *프로세스 단위* 환경 설정. cgroup 정책은 cpu.weight 쓰기를
// 시도하고, 다른 정책으로 돌아오면 기존 cpu.weight 복구를 시도한다.
// 워커별 ApplySchedToCurrentThread는 그 후에 호출되어 cgroup에 *이미 들어
// 있는 상태* 에서 동작한다.
//
// 호출은 멱등 — 도구 실행 중 여러 번 불러도 동작이 같다.
bool ApplySchedProcessSetup(SchedType type);

// 정책이 *컴파일에 들어갔는지*. 모든 정책이 Linux 표준이라 모두 true.
// 다만 *런타임 가용성* 은 별개 — 권한·환경에 따라 실패할 수 있다.
inline bool SchedCompiledIn(SchedType /*type*/) { return true; }

}  // namespace skl
