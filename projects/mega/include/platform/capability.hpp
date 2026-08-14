// capability — 플랫폼 능력 플래그와 타깃 프로파일. 능력을 데이터로 질의한다.
//
// 상위 코드는 #ifdef 대신 "터치 되나?", "메모리 예산은?" 을 런타임으로 묻는다.
// 능력을 비트 플래그로, 타깃을 프로파일(능력 집합 + 메모리/스레드 예산)로 둔다.
// 새 플랫폼은 프로파일 하나를 더하면 끝이다(platform 능력 챕터).

#pragma once

#include "platform/detect.hpp"

#include <cstdint>

namespace mega {
namespace platform {

enum class Capability : uint32_t {
    Keyboard   = 1u << 0,
    Mouse      = 1u << 1,
    Touch      = 1u << 2,
    Gamepad    = 1u << 3,
    Filesystem = 1u << 4,   // 임의 파일 접근(웹은 제한)
    Network    = 1u << 5,
    Threads    = 1u << 6,   // 다중 스레드(웹은 제한)
    GPU        = 1u << 7,
};

inline uint32_t operator|(Capability a, Capability b) { return (uint32_t)a | (uint32_t)b; }
inline uint32_t operator|(uint32_t a, Capability b) { return a | (uint32_t)b; }

struct PlatformProfile {
    const char* name            = "Unknown";
    uint32_t    caps            = 0;
    int         memoryBudgetMB  = 0;
    int         maxThreads      = 1;

    bool Has(Capability c) const { return (caps & (uint32_t)c) != 0; }
};

namespace profiles {

inline PlatformProfile PC() {
    return {"PC",
            Capability::Keyboard | Capability::Mouse | Capability::Gamepad | Capability::Filesystem
                | Capability::Network | Capability::Threads | Capability::GPU,
            8192, 16};
}
inline PlatformProfile Mobile() {
    return {"Mobile",
            Capability::Touch | Capability::Gamepad | Capability::Filesystem
                | Capability::Network | Capability::Threads | Capability::GPU,
            2048, 8};
}
inline PlatformProfile Console() {
    return {"Console",
            Capability::Gamepad | Capability::Filesystem | Capability::Network
                | Capability::Threads | Capability::GPU,
            12288, 8};
}
inline PlatformProfile Web() {
    // 웹: 임의 파일시스템·자유 스레드 제한.
    return {"Web",
            Capability::Keyboard | Capability::Mouse | Capability::Touch | Capability::Gamepad
                | Capability::Network | Capability::GPU,
            512, 4};
}

}  // namespace profiles

// 감지된 OS 로부터 기본 프로파일.
inline PlatformProfile ProfileFor(OS os) {
    switch (os) {
        case OS::Android:
        case OS::iOS:     return profiles::Mobile();
        case OS::Web:     return profiles::Web();
        case OS::Windows:
        case OS::Linux:
        case OS::MacOS:   return profiles::PC();
        default:          return profiles::PC();
    }
}

}  // namespace platform
}  // namespace mega
