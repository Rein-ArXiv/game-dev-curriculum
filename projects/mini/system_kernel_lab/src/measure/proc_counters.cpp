#include "measure/proc_counters.h"

#include <sys/resource.h>

namespace skl {

ProcCounters ReadProcCounters() {
    struct rusage r{};
    if (::getrusage(RUSAGE_SELF, &r) != 0) {
        return ProcCounters{};
    }
    return ProcCounters{
        r.ru_nvcsw,
        r.ru_nivcsw,
        r.ru_minflt,
        r.ru_majflt,
    };
}

}  // namespace skl
