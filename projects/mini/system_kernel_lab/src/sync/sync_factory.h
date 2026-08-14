#pragma once

#include "sync/i_sync.h"

#include <memory>

namespace skl {

enum class SyncType : int {
    Mutex    = 0,
    Spin     = 1,
    Futex    = 2,
    Cas      = 3,
};

constexpr int kSyncTypeCount = 4;

inline constexpr const char* kSyncTypeLabels[kSyncTypeCount] = {
    "mutex", "spinlock", "futex", "atomic CAS",
};

std::unique_ptr<ISync> CreateSync(SyncType type);

}  // namespace skl
