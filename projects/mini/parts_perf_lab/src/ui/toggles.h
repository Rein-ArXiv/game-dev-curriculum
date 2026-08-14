#pragma once

#include <cstddef>

namespace ppl {

// 토글 가능한 항목들의 enum.
// 여섯 축(엔티티 수·레이아웃·SIMD·branchless·할당자·컨테이너)이 모두 활성이며,
// HUD는 여섯 위젯을 전부 활성 상태로 그린다. 비활성 표시(grayed out) 경로는 없다.
// 어떤 토글이 프레임 끝에서 재구성을 요구하는지는 아래 RequiresRestart가 구분한다.

enum class Allocator : int {
    Malloc = 0,
    Arena,
    Pool,
    Slab,
};
constexpr int kAllocatorCount = 4;
inline const char* AllocatorName(Allocator a) {
    switch (a) {
        case Allocator::Malloc: return "malloc";
        case Allocator::Arena:  return "Arena";
        case Allocator::Pool:   return "Pool";
        case Allocator::Slab:   return "Slab";
    }
    return "?";
}

enum class Layout : int {
    SoA = 0,
    AoS,
    Archetype,
};
constexpr int kLayoutCount = 3;
inline const char* LayoutName(Layout l) {
    switch (l) {
        case Layout::SoA:       return "SoA";
        case Layout::AoS:       return "AoS";
        case Layout::Archetype: return "Archetype";
    }
    return "?";
}

enum class Container : int {
    Vector = 0,
    StdList,
    IntrusiveList,
};
constexpr int kContainerCount = 3;
inline const char* ContainerName(Container c) {
    switch (c) {
        case Container::Vector:        return "vector";
        case Container::StdList:       return "std::list";
        case Container::IntrusiveList: return "Intrusive";
    }
    return "?";
}

enum class SimdWidth : int {
    Scalar = 0,
    SSE,
    AVX2,
};
constexpr int kSimdCount = 3;
inline const char* SimdName(SimdWidth s) {
    switch (s) {
        case SimdWidth::Scalar: return "Scalar";
        case SimdWidth::SSE:    return "SSE";
        case SimdWidth::AVX2:   return "AVX2";
    }
    return "?";
}

// 엔티티 수 프리셋. UI에서 셋 중 하나를 고른다.
struct EntityCountPreset {
    const char* label;
    std::size_t count;
};
inline constexpr EntityCountPreset kEntityCountPresets[] = {
    {"1k",   1'000},
    {"10k",  10'000},
    {"100k", 100'000},
};
constexpr int kEntityCountPresetCount = 3;

// 모든 토글 상태를 한 구조체에 모은다.
struct Toggles {
    Allocator allocator = Allocator::Malloc;
    Layout    layout    = Layout::SoA;
    Container container = Container::Vector;
    SimdWidth simd      = SimdWidth::Scalar;
    bool      branchless = false;
    int       entityPresetIdx = 1;  // 10k

    std::size_t EntityCount() const {
        return kEntityCountPresets[entityPresetIdx].count;
    }
};

// 어떤 토글이 프레임 끝에서 재구성 처리를 요구하는지 나타낸다.
// 엔티티 수/레이아웃은 시뮬 재초기화, allocator/container는 각 벤치마크 재구성을 요구한다.
// 단순 토글(예: branchless predication)은 재구성 없이 다음 프레임부터 적용 가능.
inline bool RequiresRestart(const Toggles& a, const Toggles& b) {
    return a.entityPresetIdx != b.entityPresetIdx
        || a.layout    != b.layout
        || a.allocator != b.allocator
        || a.container != b.container;
}

}  // namespace ppl
