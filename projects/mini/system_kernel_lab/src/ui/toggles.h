#pragma once

#include "ctx/ctx_type.h"
#include "io/io_backend_factory.h"
#include "sched/sched_policy.h"
#include "sync/sync_factory.h"

#include <cstddef>

namespace skl {

// 모든 토글 상태를 한 구조체에 모은다. ImGui 패널이 in-out으로 갱신하고,
// main 루프가 prev/curr 비교로 변경 감지를 한다.
//
// 활성화된 토글: backend / sync / sched / ctx / workerCount.
// 확장 지점: ctx(컨텍스트), file size 등.
struct Toggles {
    BackendType backend     = BackendType::Read;
    SyncType    sync        = SyncType::Mutex;
    SchedType   sched       = SchedType::Default;
    CtxType     ctx         = CtxType::Pthread;
    int         workerIdx   = 0;       // workerCountPresets[0] = 1
};

// 워커 수 프리셋. 콤보박스 인덱스 → 실제 워커 수.
struct WorkerCountPreset {
    const char* label;
    std::size_t count;
};
inline constexpr WorkerCountPreset kWorkerCountPresets[] = {
    {"1",  1}, {"2", 2}, {"4", 4}, {"8", 8},
};
constexpr int kWorkerCountPresetCount =
    static_cast<int>(sizeof(kWorkerCountPresets)
                      / sizeof(kWorkerCountPresets[0]));

inline std::size_t WorkerCountFromIdx(int idx) {
    if (idx < 0 || idx >= kWorkerCountPresetCount) return 1;
    return kWorkerCountPresets[idx].count;
}

inline bool RequiresRestart(const Toggles& a, const Toggles& b) {
    return a.backend     != b.backend
        || a.sync        != b.sync
        || a.sched       != b.sched
        || a.ctx         != b.ctx
        || a.workerIdx   != b.workerIdx;
}

}  // namespace skl
