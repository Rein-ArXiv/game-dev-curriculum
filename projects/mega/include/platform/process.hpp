// process — 프로세스 수준 OS 신호(Ctrl+C · SIGTERM)의 유일한 소유자.
//
// 계층 규칙(A3): games/ 는 <csignal> 을 직접 부를 수 없다. 신호 처리기는
// async-signal-safe 한 것만 만질 수 있다는 제약이 있어(atomic 플래그 하나),
// 그 규칙을 아는 코드가 한 곳에 있어야 한다. 헤드리스 서버는 "종료 요청이
// 왔는가"만 물으면 된다.

#pragma once

namespace mega {
namespace platform {

// SIGINT/SIGTERM 을 잡아 종료 플래그를 세우도록 설치한다. 여러 번 불러도 안전.
void InstallShutdownSignals();

// 종료 요청이 있었는가. 신호 처리기와 루프 사이의 유일한 통신 수단이다.
bool ShutdownRequested();

// 플래그를 되돌린다(테스트·재시작용).
void ClearShutdownRequest();

// 신호 없이 프로그램적으로 종료를 요청한다(런타임 상한 도달 등).
void RequestShutdown();

}  // namespace platform
}  // namespace mega
