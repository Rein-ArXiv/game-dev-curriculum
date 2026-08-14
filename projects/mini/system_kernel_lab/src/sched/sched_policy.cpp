#include "sched/sched_policy.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <pthread.h>
#include <sched.h>
#include <string>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace skl {

namespace {

// gettid 시스템 콜 헬퍼 (glibc 2.30+ 의 gettid 함수가 없을 때 대비).
inline pid_t SklGettid() {
    return static_cast<pid_t>(::syscall(SYS_gettid));
}

std::string ReadFirstLine(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    if (f && std::getline(f, line)) return line;
    return {};
}

struct CgroupWeightState {
    bool        captured = false;
    bool        modified = false;
    std::string dir;
    std::string originalWeight;
};

CgroupWeightState& CgroupState() {
    static CgroupWeightState state;
    return state;
}

// /proc/self/cgroup 첫 줄의 v2 cgroup 경로를 얻는다.
//   "0::/user.slice/.../skl.scope" 형태. /sys/fs/cgroup 와 합쳐 절대 경로.
//   cgroup v2가 아니면 빈 문자열.
std::string CurrentCgroupV2Path() {
    std::ifstream f("/proc/self/cgroup");
    if (!f) return {};
    std::string line;
    while (std::getline(f, line)) {
        // v2 single hierarchy 라인은 "0::..."
        if (line.size() >= 4 && line[0] == '0' && line[1] == ':'
            && line[2] == ':') {
            const std::string rel = line.substr(3);
            const std::string detected =
                (rel.empty() || rel == "/")
                ? std::string("/sys/fs/cgroup")
                : std::string("/sys/fs/cgroup") + rel;
            const char* approved = std::getenv("SKL_CGROUP_DIR");
            if (!approved || detected != approved) {
                std::fprintf(stderr,
                    "skl: refusing to modify the current cgroup. Set "
                    "SKL_CGROUP_DIR exactly to '%s' only for a delegated, "
                    "disposable benchmark cgroup.\n", detected.c_str());
                return {};
            }
            return detected;
        }
    }
    return {};
}

bool ApplyNice(int niceValue) {
    // Linux는 nice를 task_struct 단위로 들고 있으므로 TID를 명시한다.
    // 이렇게 해야 워커 스레드별 nice 적용 의도가 코드에 분명히 드러난다.
    const pid_t tid = SklGettid();
    if (::setpriority(PRIO_PROCESS, tid, niceValue) != 0) {
        std::fprintf(stderr,
            "skl: setpriority(nice=%d) failed: %s — keeping default\n",
            niceValue, std::strerror(errno));
        return false;
    }
    return true;
}

bool ApplyFifo(int rtPriority) {
    sched_param param{};
    param.sched_priority = rtPriority;
    const int rc = ::pthread_setschedparam(::pthread_self(),
                                            SCHED_FIFO, &param);
    if (rc != 0) {
        std::fprintf(stderr,
            "skl: pthread_setschedparam(SCHED_FIFO, prio=%d) failed: %s — "
            "keeping default. Check ulimit -r and CAP_SYS_NICE.\n",
            rtPriority, std::strerror(rc));
        return false;
    }
    return true;
}

bool WriteCgroupFile(const std::string& dir, const char* file,
                      const char* content,
                      int* outErr = nullptr) {
    const std::string path = dir + "/" + file;
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        if (outErr) *outErr = errno;
        return false;
    }
    const std::size_t len = std::strlen(content);
    const std::size_t written = std::fwrite(content, 1, len, f);
    if (written != len) {
        if (outErr) *outErr = std::ferror(f) ? errno : EIO;
        std::fclose(f);
        return false;
    }
    const int closeRc = std::fclose(f);
    if (closeRc != 0) {
        if (outErr) *outErr = errno;
        return false;
    }
    return true;
}

void CaptureOriginalCgroupWeight(const std::string& dir) {
    auto& state = CgroupState();
    if (state.captured && state.dir == dir) return;

    state.captured = true;
    state.modified = false;
    state.dir = dir;
    state.originalWeight = ReadFirstLine(dir + "/cpu.weight");
}

bool ApplyCgroupCpuWeight(int weight) {
    const std::string dir = CurrentCgroupV2Path();
    if (dir.empty()) {
        std::fprintf(stderr,
            "skl: cgroup v2 not detected — keeping default\n");
        return false;
    }
    CaptureOriginalCgroupWeight(dir);
    if (CgroupState().originalWeight.empty()) {
        std::fprintf(stderr,
            "skl: reading %s/cpu.weight failed — keeping default\n",
            dir.c_str());
        return false;
    }

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d\n", weight);
    int writeErr = 0;
    if (!WriteCgroupFile(dir, "cpu.weight", buf, &writeErr)) {
        std::fprintf(stderr,
            "skl: writing %s/cpu.weight failed: %s — keeping default\n",
            dir.c_str(), std::strerror(writeErr));
        return false;
    }
    CgroupState().modified = true;
    return true;
}

bool RestoreCgroupCpuWeightIfNeeded() {
    auto& state = CgroupState();
    if (!state.modified || state.dir.empty() || state.originalWeight.empty()) {
        return true;
    }

    std::string content = state.originalWeight;
    content.push_back('\n');
    int writeErr = 0;
    if (!WriteCgroupFile(state.dir, "cpu.weight", content.c_str(), &writeErr)) {
        std::fprintf(stderr,
            "skl: restoring %s/cpu.weight failed: %s — keeping current value\n",
            state.dir.c_str(), std::strerror(writeErr));
        return false;
    }
    state.modified = false;
    return true;
}

}  // namespace

bool ApplySchedToCurrentThread(SchedType type) {
    switch (type) {
        case SchedType::Default:
            // 명시적 reset. nice 0 + SCHED_OTHER.
            if (::setpriority(PRIO_PROCESS, SklGettid(), 0) != 0) {
                // 거의 실패하지 않지만 silent fallback.
            }
            {
                sched_param p{};
                p.sched_priority = 0;
                ::pthread_setschedparam(::pthread_self(), SCHED_OTHER, &p);
            }
            return true;

        case SchedType::Nice:
            return ApplyNice(10);

        case SchedType::Fifo:
            return ApplyFifo(50);

        case SchedType::Cgroup:
            // cgroup은 *프로세스 단위* 정책. 워커 스레드 별도 처리는 없다.
            // ApplySchedProcessSetup에서 이미 처리됨.
            return true;
    }
    return true;
}

bool ApplySchedProcessSetup(SchedType type) {
    switch (type) {
        case SchedType::Default:
        case SchedType::Nice:
        case SchedType::Fifo:
            return RestoreCgroupCpuWeightIfNeeded();

        case SchedType::Cgroup:
            // 명시 승인된 현재 cgroup의 cpu.weight를 200으로 설정.
            return ApplyCgroupCpuWeight(200);
    }
    return true;
}

}  // namespace skl
