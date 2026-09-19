#include "sched/sched_policy.h"

#include "fault/skl_syscalls.h"

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
    // cgroup 파일 읽기 실패 seam. 컨트롤러가 없거나 권한이 없는 환경을
    // 결정적으로 재현한다.
    if (SklFaultTriggered("cgroup_read")) return {};
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

// /proc/self/cgroup 의 v2 라인("0::...")에서 cgroup 경로를 얻는다.
//   "0::/user.slice/.../skl.scope" 형태. /sys/fs/cgroup 와 합쳐 절대 경로.
//   cgroup v2가 아니거나, SKL_CGROUP_DIR 이 그 경로와 정확히 일치하지 않으면
//   빈 문자열.
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
    if (SklSetPriority(PRIO_PROCESS, static_cast<id_t>(tid), niceValue) != 0) {
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
    const int rc = SklPthreadSetSchedParam(::pthread_self(),
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
    // cgroup 파일 쓰기 실패 seam. 위임되지 않은 cgroup(= 쓰기 권한 없음)을
    // 결정적으로 재현한다. 주입 시 파일을 아예 열지 않는다.
    if (const int e = SklFaultCheck("cgroup_write")) {
        if (outErr) *outErr = e;
        return false;
    }
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

    state.modified = false;
    state.dir = dir;
    state.originalWeight = ReadFirstLine(dir + "/cpu.weight");
    // 읽기에 *성공했을 때만* captured 로 굳힌다. 실패를 캐시로 굳히면 일시적
    // 실패(또는 테스트의 cgroup_read 주입) 한 번이 같은 dir 의 이후 모든 쓰기
    // 경로를 영구히 막는다 — 다음 호출이 다시 읽도록 남겨 둔다.
    state.captured = !state.originalWeight.empty();
}

bool ApplyCgroupCpuWeight(int weight) {
    const std::string dir = CurrentCgroupV2Path();
    if (dir.empty()) {
        std::fprintf(stderr,
            "skl: no usable cgroup v2 path (not detected, or SKL_CGROUP_DIR does "
            "not approve it) — keeping default\n");
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
        case SchedType::Default: {
            // 명시적 reset. nice 0 + SCHED_OTHER.
            //
            // nice 를 *내리는* 것은 권한이 필요한 방향이다 — nice +10 에서 0
            // 으로 되돌리려면 CAP_SYS_NICE 거나 RLIMIT_NICE 여유가 있어야 하고,
            // soft 가 0 인 보통 머신에서는 EACCES 로 실패한다. 그걸 삼키고 true 를
            // 돌려주면 호출 측의 schedApplied_ 메타데이터가 거짓이 된다.
            // 헤더의 계약대로 실패를 stderr 로 알리고 false 를 돌려준다.
            //
            // 한쪽이 실패해도 다른 한쪽은 그대로 시도한다 — 되돌릴 수 있는 만큼은
            // 되돌리는 것이 best-effort 폴백의 의미다. 둘 다 seam 을 지나므로
            // 이 분기도 주입으로 결정적으로 밟을 수 있다.
            bool ok = true;
            if (SklSetPriority(PRIO_PROCESS, static_cast<id_t>(SklGettid()), 0)
                != 0) {
                std::fprintf(stderr,
                    "skl: setpriority(nice=0) failed: %s — keeping current "
                    "nice. Check ulimit -e and CAP_SYS_NICE.\n",
                    std::strerror(errno));
                ok = false;
            }
            sched_param p{};
            p.sched_priority = 0;
            const int rc =
                SklPthreadSetSchedParam(::pthread_self(), SCHED_OTHER, &p);
            if (rc != 0) {
                std::fprintf(stderr,
                    "skl: pthread_setschedparam(SCHED_OTHER) failed: %s — "
                    "keeping current policy\n", std::strerror(rc));
                ok = false;
            }
            return ok;
        }

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
