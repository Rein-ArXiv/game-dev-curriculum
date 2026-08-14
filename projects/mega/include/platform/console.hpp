// console — 콘솔 UTF-8 출력 설정(Windows 코드페이지 mojibake 방지).
//
// 소스는 UTF-8 이고 프로그램은 UTF-8 바이트를 출력하는데, Windows 콘솔 기본 코드페이지가
// CP949 면 한글이 깨져 보인다. 프로그램 시작에서 콘솔 출력 코드페이지를 UTF-8(65001)로
// 바꿔 더블클릭·cmd 실행에서도 한글이 제대로 나오게 한다. 비-Windows 에서는 no-op.
//
// windows.h 를 끌어오면 min/max 매크로 등이 오염되므로, 필요한 함수만 직접 선언한다.

#pragma once

namespace mega {
namespace platform {

#ifdef _WIN32
extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int wCodePageID);
inline void EnableUtf8Console() { SetConsoleOutputCP(65001u); }
#else
inline void EnableUtf8Console() {}
#endif

}  // namespace platform
}  // namespace mega
