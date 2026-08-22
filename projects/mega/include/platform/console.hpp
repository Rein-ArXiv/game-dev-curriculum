// console — 콘솔 UTF-8 출력 설정(Windows 코드페이지 mojibake 방지).
//
// 이 저장소의 진단은 전부 한국어다.
//
//     [content] balance.script 거부, 기본 구성으로 진행: …
//     [admission] 콘텐츠 해시 불일치 …
//
// 소스도 UTF-8 이고 프로그램이 내보내는 바이트도 UTF-8 인데, Windows 콘솔의
// 기본 출력 코드페이지가 CP949 면 이 줄들이 깨져 나온다. 읽을 수 없는 실패
// 메시지는 무시당하고, 무시당하는 진단은 없는 것과 같다 — 그래서 이것은 표시
// 취향이 아니라 진단의 도달성 문제다. 3타깃 CI 로그를 한 인코딩으로 모으는
// 문제이기도 하다.
//
// windows.h 를 끌어오면 min/max 매크로 등이 오염되므로, 필요한 함수만 직접
// 선언한다.
//
// 콘솔이 아예 없는 프로세스(파이프로 리다이렉트된 CI 러너, GUI 서브시스템)에서는
// 설정할 대상이 없다. 그 상태는 실패가 아니라 **정상**이고, 그래서
// EnableUtf8Console 은 "콘솔이 없거나 65001 이 됐다"를 참으로 돌려준다.

#pragma once

namespace mega {
namespace platform {

inline constexpr unsigned kUtf8CodePage = 65001u;

#ifdef _WIN32
extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int wCodePageID);
extern "C" __declspec(dllimport) unsigned int __stdcall GetConsoleOutputCP(void);

// 0 = 이 프로세스에 콘솔이 없다.
inline unsigned ConsoleOutputCodePage() { return GetConsoleOutputCP(); }

// snippet-begin: utf8-console
inline bool EnableUtf8Console() {
    if (GetConsoleOutputCP() == 0u) return true;     // 콘솔 없음 = 할 일 없음
    SetConsoleOutputCP(kUtf8CodePage);
    return GetConsoleOutputCP() == kUtf8CodePage;
}
// snippet-end: utf8-console
#else
// 비-Windows 의 콘솔은 이미 UTF-8 로케일이다. 조회는 0(콘솔 코드페이지라는
// 개념이 없다)을 돌려주고 설정은 no-op 이다.
inline unsigned ConsoleOutputCodePage() { return 0u; }
inline bool EnableUtf8Console() { return true; }
#endif

}  // namespace platform
}  // namespace mega
