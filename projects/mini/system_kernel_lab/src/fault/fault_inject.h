#pragma once

#include <atomic>
#include <cstddef>
#include <string>

namespace skl {

// ============================================================================
// 실패 주입 seam
//
// backend 들은 syscall 을 직접 부르지 않고 fault/skl_syscalls.h 의 얇은
// wrapper 를 통해 부른다. wrapper 는 실제 호출 직전에 SklFaultCheck(op) 를
// 묻고, 0 이 아닌 errno 가 돌아오면 *syscall 을 부르지 않고* 그 errno 로
// 실패한 것처럼 반환한다.
//
// 이 seam 이 없으면 정규 파일 + 정상 커널에서는 절대 재현되지 않는 경로가
// 통째로 미검증으로 남는다:
//   - read/pread 의 EINTR 재시도 루프
//   - aio_suspend 실패 후 aio_cancel + polling 수확 (버퍼 해제 전 전부
//     aio_return 까지 수확해야 한다는 규약)
//   - io_uring submit 음수/부분 제출 시 링 재초기화, 잘못된 user_data 방어
//   - io_backend_factory 의 uring → read 런타임 폴백
//   - mmap 의 fstat 크기 가드
//   - sched_policy 의 권한 실패 폴백, cgroup 읽기/쓰기 실패
//
// 무장(armed)되지 않은 상태의 비용은 relaxed atomic load 하나다. 주입 설정이
// 없으면 어떤 동작도 바뀌지 않는다.
// ============================================================================

namespace detail {

// 무장 여부 fast path 플래그. 정의는 fault_inject.cpp.
extern std::atomic<bool> g_faultArmed;

// 무장된 경우에만 불린다. 규칙 조회는 mutex 로 보호된다.
int FaultCheckSlow(const char* op);

}  // namespace detail

// spec 문법: "op[:skip[:ERRNO[:times]]]" 를 ',' 로 나열.
//   op    — seam 이름 ("open", "read", "pread", "fstat", "mmap", "munmap",
//           "aio_read", "aio_suspend", "aio_cancel", "aio_return",
//           "io_uring_queue_init", "io_uring_get_sqe", "io_uring_submit",
//           "io_uring_submit_short", "io_uring_wait_cqe", "uring_user_data",
//           "setpriority", "pthread_setschedparam", "cgroup_read",
//           "cgroup_write").
//   skip  — 이 횟수만큼은 그대로 통과시킨다 (기본 0).
//   ERRNO — 주입할 errno 이름(EIO, EINTR, ...) 또는 양의 정수 (기본 EIO).
//   times — 주입 횟수 상한, 0 이면 무제한 (기본 0).
//
// 예) "open:2:EACCES:1" — open 3번째 호출 한 번만 EACCES.
//     "read:0:EINTR:3"  — read 처음 3번을 EINTR (재시도 루프를 강제).
//
// 파싱에 실패하면 false 를 반환하고 *기존 설정을 포함해 모든 주입을 해제*
// 한다 — 잘못 쓴 spec 이 절반만 적용되는 상태를 만들지 않는다.
bool SetFaultSpec(const std::string& spec);

// 모든 주입 해제 + 카운터 초기화.
void ClearFaults();

// 현재 주입 규칙이 하나라도 있는가.
bool FaultsArmed();

// seam 방문 횟수 / 실제 주입 횟수. 무장된 op 에 대해서만 센다 (무장되지 않은
// 상태에서는 fast path 가 즉시 반환하므로 세지 않는다).
// 테스트가 "그 오류 분기가 실제로 실행됐는가" 를 확인하는 데 쓴다.
//
// skip 을 아주 크게 잡은 규칙("read:100000000")은 *아무것도 주입하지 않으면서*
// 호출 횟수만 세는 순수 관측 모드가 된다. 실제 시그널로 EINTR 재시도가
// 일어났는지, 취소 후 모든 요청을 aio_return 까지 수확했는지 같은 "분기가 정말
// 실행됐나" 를 동작을 바꾸지 않고 확인하는 데 쓴다.
std::size_t FaultCallCount(const char* op);
std::size_t FaultInjectionCount(const char* op);

// seam 본체. 주입하지 않으면 0, 주입하면 errno(>0) 를 반환한다.
inline int SklFaultCheck(const char* op) {
    if (!detail::g_faultArmed.load(std::memory_order_relaxed)) return 0;
    return detail::FaultCheckSlow(op);
}

// errno 가 의미 없는 seam (반환값 변조 등) 용 — 주입 여부만 본다.
inline bool SklFaultTriggered(const char* op) {
    return SklFaultCheck(op) != 0;
}

}  // namespace skl
