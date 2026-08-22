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
    // 웹: 임의 파일시스템·자유 스레드 제한. maxThreads 가 1인 것은 축약이 아니라
    // 능력과의 정합이다 — Threads 능력이 없는데 스레드 예산이 4라고 적어 두면
    // 그 둘 중 어느 쪽을 믿어야 하는지가 호출부마다 달라진다.
    return {"Web",
            Capability::Keyboard | Capability::Mouse | Capability::Touch | Capability::Gamepad
                | Capability::Network | Capability::GPU,
            512, 1};
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

// ── 능력이 정하는 것과 정하지 않는 것 ──────────────────────────────
//
// 여기가 이 헤더에서 가장 중요한 문장이다. 프로파일이 바꾸는 것은 **워커 수 ·
// 메모리 예산 · 파티클 수 · 오디오 보이스 · 렌더 LOD** 뿐이다. 적 상한 · 웨이브
// 구성 · 타워 사거리 같은 권위 시뮬 규모는 **절대** 건드리지 않는다.
//
// 이유는 성능이 아니라 정의다. 능력이 권위 규모를 바꾸면 모바일 클라이언트와
// PC 호스트가 서로 다른 게임을 돌게 되고, 그 순간 "같은 시드 + 같은 입력 =
// 같은 상태 해시"가 거짓이 된다. 그러면 desync 탐지도 리플레이 검증도 성립하지
// 않는다. 티어가 달라도 권위 해시는 같다 — 그것이 이 경계의 검사식이다.
struct RuntimeBudget {
    int workerCap     = 0;      // 잡 스케줄러 워커 상한(Threads 없으면 0 = 직렬)
    int frameArenaKB  = 256;    // memory/arena 프레임 스크래치
    int blobSlabKB    = 1024;   // memory/slab 매치 수명 버퍼
    int particleCap   = 512;    // 표시 전용
    int audioVoiceCap = 32;     // engine/audio 동시 보이스
    int renderLod     = 0;      // 0 = 최고 품질
};

// snippet-begin: capability-budget
inline RuntimeBudget BudgetFor(const PlatformProfile& profile) {
    RuntimeBudget budget;
    // 스레드 능력이 없으면 워커는 0이다. maxThreads 값이 무엇이든 상관없다 —
    // 능력이 예산을 이긴다.
    budget.workerCap = profile.Has(Capability::Threads) ? profile.maxThreads : 0;
    if (budget.workerCap < 0) budget.workerCap = 0;

    // 나머지는 메모리 예산에서 비례로 끌어온다. 절대값을 프로파일마다 손으로
    // 적으면 새 플랫폼이 생길 때마다 다섯 줄을 더 적게 된다.
    const int budgetMB = profile.memoryBudgetMB > 0 ? profile.memoryBudgetMB : 512;
    budget.frameArenaKB  = budgetMB >= 4096 ? 1024 : (budgetMB >= 1024 ? 512 : 256);
    budget.blobSlabKB    = budgetMB >= 4096 ? 4096 : (budgetMB >= 1024 ? 2048 : 512);
    budget.particleCap   = budgetMB >= 4096 ? 2048 : (budgetMB >= 1024 ? 1024 : 256);
    budget.audioVoiceCap = budgetMB >= 1024 ? 32 : 8;
    budget.renderLod     = budgetMB >= 4096 ? 0 : (budgetMB >= 1024 ? 1 : 2);
    if (!profile.Has(Capability::GPU)) budget.renderLod = 2;
    return budget;
}
// snippet-end: capability-budget

// 요청한 워커 수를 능력 안으로 자른다. 자르기만 하고 올리지는 않는다 —
// `--ab jobs=8` 로 명시한 사람이 8보다 많은 워커를 얻는 일은 없어야 한다.
inline int ClampWorkers(int requested, const PlatformProfile& profile) {
    const int cap = BudgetFor(profile).workerCap;
    if (requested < 0) return 0;
    return requested < cap ? requested : cap;
}

}  // namespace platform
}  // namespace mega
