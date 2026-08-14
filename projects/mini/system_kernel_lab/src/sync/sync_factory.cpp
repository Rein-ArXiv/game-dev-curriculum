#include "sync/sync_factory.h"

#include "sync/cas_sync.h"
#include "sync/futex_sync.h"
#include "sync/mutex_sync.h"
#include "sync/spin_sync.h"

namespace skl {

std::unique_ptr<ISync> CreateSync(SyncType type) {
    switch (type) {
        case SyncType::Mutex: return std::make_unique<MutexSync>();
        case SyncType::Spin:  return std::make_unique<SpinSync>();
        case SyncType::Futex: return std::make_unique<FutexSync>();
        case SyncType::Cas:   return std::make_unique<CasSync>();
    }
    return std::make_unique<MutexSync>();
}

}  // namespace skl
