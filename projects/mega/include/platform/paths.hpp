// paths — 실행 파일 위치 · 사용자 데이터 위치 · 환경 변수.
//
// 계층 규칙(A3): games/ 는 OS 를 직접 묻지 않는다. "내 exe 가 어디 있나"
// (GetModuleFileNameW / /proc/self/exe), "저장 파일을 어디에 두나"
// (%LOCALAPPDATA% / $HOME), "환경 변수" 는 전부 OS 질의이므로 platform/ 이
// 소유한다. 헤더는 표준 타입만 노출하고 시스템 콜은 paths.cpp 에 격리한다 —
// 이 헤더를 인클루드해도 <windows.h> 가 딸려 오지 않는다.

#pragma once

#include <string>

namespace mega {
namespace platform {

// 실행 중인 바이너리가 있는 디렉토리(뒤에 구분자 없음). 알 수 없으면 ".".
// 에셋·셰이더는 exe 옆에 복사되므로 이것이 콘텐츠 루트의 기준점이다.
std::string ExecutableDirectory();

// 쓰기 가능한 사용자별 데이터 디렉토리. 없으면 만든다. 실패하면 빈 문자열.
//   Windows: %LOCALAPPDATA%/<application>
//   기타:    $HOME/.local/share/<application>
std::string UserDataDirectory(const std::string& application);

// 환경 변수. 없으면 false 이고 out 은 건드리지 않는다.
bool EnvironmentVariable(const char* name, std::string& out);

// 중간 경로를 포함해 디렉토리를 만든다. 이미 있으면 true.
bool EnsureDirectory(const std::string& path);

}  // namespace platform
}  // namespace mega
