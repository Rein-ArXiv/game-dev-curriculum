// detect — 컴파일 타임 OS·아키텍처·엔디안 감지.
//
// 전처리기 매크로로 현재 플랫폼을 enum 으로 노출한다. 상위 코드는 #ifdef 를
// 흩뿌리는 대신 이 enum 을 질의한다. 엔디안은 C++20 std::endian 으로 정적 판별한다.

#pragma once

#include <bit>
#include <cstdint>
#include <string>

namespace mega {
namespace platform {

enum class OS   { Windows, Linux, MacOS, Android, iOS, Web, Unknown };
enum class Arch { X86_64, X86, ARM64, ARM, Wasm, Unknown };
enum class Endian { Little, Big };

constexpr OS CurrentOS() {
#if defined(__ANDROID__)
    return OS::Android;
#elif defined(__EMSCRIPTEN__)
    return OS::Web;
#elif defined(_WIN32)
    return OS::Windows;
#elif defined(__APPLE__)
    return OS::MacOS;     // iOS 는 추가 매크로로 구분(여기선 MacOS 로 묶음)
#elif defined(__linux__)
    return OS::Linux;
#else
    return OS::Unknown;
#endif
}

constexpr Arch CurrentArch() {
#if defined(__wasm__) || defined(__EMSCRIPTEN__)
    return Arch::Wasm;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return Arch::ARM64;
#elif defined(__arm__) || defined(_M_ARM)
    return Arch::ARM;
#elif defined(__x86_64__) || defined(_M_X64)
    return Arch::X86_64;
#elif defined(__i386__) || defined(_M_IX86)
    return Arch::X86;
#else
    return Arch::Unknown;
#endif
}

constexpr Endian CurrentEndian() {
    return std::endian::native == std::endian::little ? Endian::Little : Endian::Big;
}

constexpr const char* OSName(OS os) {
    switch (os) {
        case OS::Windows: return "Windows";
        case OS::Linux:   return "Linux";
        case OS::MacOS:   return "macOS";
        case OS::Android: return "Android";
        case OS::iOS:     return "iOS";
        case OS::Web:     return "Web";
        default:          return "Unknown";
    }
}
constexpr const char* ArchName(Arch a) {
    switch (a) {
        case Arch::X86_64: return "x86-64";
        case Arch::X86:    return "x86";
        case Arch::ARM64:  return "ARM64";
        case Arch::ARM:    return "ARM";
        case Arch::Wasm:   return "Wasm";
        default:           return "Unknown";
    }
}
constexpr const char* EndianName(Endian e) {
    return e == Endian::Little ? "LE" : "BE";
}

// ── 와이어 코드 ────────────────────────────────────────────────────
// 입장 핸드셰이크에 실린다. 열거 값이 곧 와이어 값이므로 순서를 바꾸면
// 프로토콜이 바뀐다 — 그 사실을 컴파일 시간에 못박는다. 새 플랫폼은 언제나
// Unknown **앞이 아니라** 그 자리를 유지한 채 뒤에 붙인다.
// snippet-begin: os-wire-code
static_assert(static_cast<uint8_t>(OS::Windows) == 0 && static_cast<uint8_t>(OS::Unknown) == 6,
              "OS 열거 값은 입장 핸드셰이크의 와이어 코드다");
static_assert(static_cast<uint8_t>(Arch::X86_64) == 0 && static_cast<uint8_t>(Arch::Unknown) == 5,
              "Arch 열거 값은 입장 핸드셰이크의 와이어 코드다");

constexpr uint8_t OSCode(OS os) { return static_cast<uint8_t>(os); }
constexpr OS OSFromCode(uint8_t code) {
    return code <= static_cast<uint8_t>(OS::Unknown) ? static_cast<OS>(code) : OS::Unknown;
}
constexpr uint8_t ArchCode(Arch a) { return static_cast<uint8_t>(a); }
constexpr Arch ArchFromCode(uint8_t code) {
    return code <= static_cast<uint8_t>(Arch::Unknown) ? static_cast<Arch>(code) : Arch::Unknown;
}
// snippet-end: os-wire-code

// 로그 한 줄. 서버 시작과 입장 승인에 이 문자열이 실려, 어느 OS 클라이언트가
// 어느 OS 서버에 붙었는지가 로그만으로 답해진다.
inline std::string PlatformLine(OS os, Arch arch, Endian endian) {
    return std::string(OSName(os)) + "/" + ArchName(arch) + "/" + EndianName(endian);
}
inline std::string CurrentPlatformLine() {
    return PlatformLine(CurrentOS(), CurrentArch(), CurrentEndian());
}

}  // namespace platform
}  // namespace mega
