// detect — 컴파일 타임 OS·아키텍처·엔디안 감지.
//
// 전처리기 매크로로 현재 플랫폼을 enum 으로 노출한다. 상위 코드는 #ifdef 를
// 흩뿌리는 대신 이 enum 을 질의한다. 엔디안은 C++20 std::endian 으로 정적 판별한다.

#pragma once

#include <bit>

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

}  // namespace platform
}  // namespace mega
