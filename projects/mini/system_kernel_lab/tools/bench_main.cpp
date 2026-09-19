// system_kernel_bench — 창 없는(headless) 측정 러너.
//
// GUI 도구는 토글을 손으로 바꾸고 그래프를 눈으로 읽는다. 그 경로는 재현이
// 불가능하다 — 같은 조합을 두 번 돌린 결과를 나란히 놓을 수도, 머신 간에
// 대조할 수도, 스크립트로 회귀를 판정할 수도 없다.
//
// 이 러너는 같은 워크로드(FileIoWorkload)를 같은 계측기(FrameTimer +
// ProcCounters)로 돌리되, 조합을 명령줄로 받고 사이클마다 4채널을 CSV 한 줄로
// 뱉는다. SDL2/OpenGL/ImGui 는 링크하지 않는다.
//
//   ./system_kernel_bench --backend=io_uring --workers=4 --cycles=200
//                         --warmup=20 --out=uring_w4.csv
//
// CSV 는 조합 정보를 매 행에 함께 싣는다. 여러 실행의 파일을 그대로 이어 붙여
// 하나의 표로 읽을 수 있게 하기 위해서다(헤더 한 줄만 지우면 된다).
//
// 측정 정직성에 대한 두 가지 계약.
//   1. backend 는 *요청* 과 *실제* 를 따로 적는다. io_uring/aio 는 런타임 초기화
//      실패 시 조용히 read 로 폴백하므로, 요청 값만 기록하면 read 로 돈 수치를
//      io_uring 수치로 읽게 된다.
//   2. 러너는 성능 판정을 하지 않는다. 대신 *정합성* 은 판정한다 — 사이클마다
//      총 바이트가 files*fileBytes 인지, 체크섬이 사이클 간 동일한지 확인하고
//      어긋나면 0 이 아닌 코드로 종료한다. 그래서 이 러너 자체를 ctest 의
//      스모크 케이스로 걸 수 있다.

#include "ctx/ctx_type.h"
#include "io/io_backend_factory.h"
#include "measure/frame_timer.h"
#include "measure/proc_counters.h"
#include "sched/sched_policy.h"
#include "sync/sync_factory.h"
#include "workload/file_io_workload.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

namespace {

// ============================================================================
// 이름 파싱
//
// UI 라벨("atomic CAS", "nice +10", "cgroup weight 200")은 공백이 있어 명령줄에
// 그대로 쓰기 나쁘다. 입력은 [a-z0-9] 만 남기고 정규화한 뒤 별칭표와 맞춘다 —
// "io_uring", "IO_URING", "uring" 이 모두 같은 값이 된다. CSV 에는 아래 짧은
// 이름을 쓴다(공백 없음, 열 파싱이 단순해진다).
// ============================================================================

struct Alias {
    const char* name;   // 정규화된 형태로 적는다.
    int         value;
};

const Alias kBackendAliases[] = {
    {"read", 0}, {"pread", 1}, {"mmap", 2}, {"aio", 3},
    {"iouring", 4}, {"uring", 4},
};
const char* const kBackendShort[] = {"read", "pread", "mmap", "aio", "io_uring"};

const Alias kSyncAliases[] = {
    {"mutex", 0}, {"spin", 1}, {"spinlock", 1}, {"futex", 2},
    {"cas", 3}, {"atomiccas", 3},
};
const char* const kSyncShort[] = {"mutex", "spin", "futex", "cas"};

const Alias kSchedAliases[] = {
    {"default", 0}, {"nice", 1}, {"nice10", 1},
    {"fifo", 2}, {"schedfifo", 2}, {"cgroup", 3}, {"cgroupweight200", 3},
};
const char* const kSchedShort[] = {"default", "nice", "fifo", "cgroup"};

const Alias kCtxAliases[] = {
    {"pthread", 0}, {"coroutine", 1}, {"coro", 1},
};
const char* const kCtxShort[] = {"pthread", "coroutine"};

std::string Normalize(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

template <std::size_t N>
bool ParseEnum(const std::string& text, const Alias (&table)[N], int* out) {
    const std::string key = Normalize(text);
    for (const Alias& alias : table) {
        if (key == alias.name) {
            *out = alias.value;
            return true;
        }
    }
    return false;
}

bool ParseSize(const std::string& text, std::size_t* out) {
    // strtoull 은 선행 공백과 부호를 받아들여 "-1" 을 2^64-1 로 wrap 한다.
    // 첫 글자가 숫자가 아니면 거부한다.
    if (text.empty() || !std::isdigit(static_cast<unsigned char>(text[0]))) {
        return false;
    }
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') return false;
    *out = static_cast<std::size_t>(value);
    return true;
}

bool TakeOption(const std::string& arg, const char* name, std::string* value) {
    const std::string prefix = std::string("--") + name + "=";
    if (arg.compare(0, prefix.size(), prefix) != 0) return false;
    *value = arg.substr(prefix.size());
    return true;
}

// ============================================================================
// 요약 통계
//
// p50/p99 는 nearest-rank. 사이클 수가 적을 때(스모크 3사이클) 보간 정의는
// 원본에 없는 값을 만들어 내므로 실제 관측값 하나를 고르는 쪽을 택했다.
// ============================================================================

struct Stats {
    double mean = 0.0;
    double p50  = 0.0;
    double p99  = 0.0;
    double min  = 0.0;
    double max  = 0.0;
};

double NearestRank(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    const double rank = std::ceil(p * static_cast<double>(sorted.size()));
    std::size_t index = (rank < 1.0) ? 0 : static_cast<std::size_t>(rank) - 1;
    if (index >= sorted.size()) index = sorted.size() - 1;
    return sorted[index];
}

Stats Summarize(std::vector<double> values) {
    Stats stats;
    if (values.empty()) return stats;
    double sum = 0.0;
    for (const double v : values) sum += v;
    stats.mean = sum / static_cast<double>(values.size());
    std::sort(values.begin(), values.end());
    stats.p50 = NearestRank(values, 0.50);
    stats.p99 = NearestRank(values, 0.99);
    stats.min = values.front();
    stats.max = values.back();
    return stats;
}

void PrintStatsLine(std::ostream& os, const char* label, const Stats& stats,
                    int precision) {
    char line[512];
    std::snprintf(line, sizeof(line),
                  "[bench] %-9s mean=%.*f  p50=%.*f  p99=%.*f  "
                  "min=%.*f  max=%.*f",
                  label,
                  precision, stats.mean, precision, stats.p50,
                  precision, stats.p99, precision, stats.min,
                  precision, stats.max);
    os << line << '\n';
}

void PrintUsage(std::ostream& os) {
    os <<
        "system_kernel_bench — headless measurement runner\n"
        "\n"
        "usage: system_kernel_bench [options]\n"
        "\n"
        "  --backend=read|pread|mmap|aio|io_uring   (default read)\n"
        "  --sync=mutex|spin|futex|cas              (default mutex)\n"
        "  --sched=default|nice|fifo|cgroup         (default default)\n"
        "  --ctx=pthread|coroutine                  (default pthread)\n"
        "  --workers=N                              (default 1)\n"
        "  --cycles=N       measured cycles         (default 30)\n"
        "  --warmup=N       discarded cycles        (default 2)\n"
        "  --files=N        fixture file count      (default 16)\n"
        "  --bytes=N        bytes per fixture file  (default 1048576)\n"
        "  --fixture-dir=P  fixture directory       (default /tmp/system_kernel_bench_data)\n"
        "  --out=P          CSV path, '-' for stdout (default run.csv)\n"
        "  --strict-backend treat a runtime backend fallback as failure\n"
        "  --min-faults=N   fail unless mean page faults per cycle >= N\n"
        "  --help\n"
        "\n"
        "CSV columns:\n"
        "  backend_req,backend,sync,sched,sched_applied,ctx,workers,files,\n"
        "  file_bytes,cycle,cycle_ms,mb_per_s,ctx_sw,page_faults\n"
        "\n"
        "'backend' is the backend that actually ran; it differs from\n"
        "'backend_req' when io_uring/aio fell back to read at runtime.\n";
}

struct Options {
    int         backend   = 0;
    int         sync      = 0;
    int         sched     = 0;
    int         ctx       = 0;
    std::size_t workers   = 1;
    std::size_t cycles    = 30;
    std::size_t warmup    = 2;
    std::size_t files     = 16;
    std::size_t fileBytes = 1024u * 1024u;
    std::string fixtureDir = "/tmp/system_kernel_bench_data";
    std::string out        = "run.csv";
    bool        strictBackend = false;
    // 채널이 살아 있는지 확인하는 하한. 0 이면 검사하지 않는다.
    //
    // 왜 필요한가: getrusage → DiffCounters → FrameTimer → CSV 로 이어지는
    // 배선이 끊어져도 러너는 여전히 "돈다". 4채널 중 두 개가 통째로 0 이 되면
    // 이 도구는 아무것도 재지 않는 것이고, 그 상태를 CSV 만 보고 알아채기
    // 어렵다. mmap backend 는 사이클마다 새 매핑을 만들어 반드시 minor fault 를
    // 낸다 — 그 성질을 ctest 케이스 하나로 못 박는 데 쓴다.
    std::size_t minFaults  = 0;
};

}  // namespace

int main(int argc, char** argv) {
    Options options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        std::string value;

        if (arg == "--help" || arg == "-h") {
            PrintUsage(std::cout);
            return 0;
        }
        if (arg == "--strict-backend") {
            options.strictBackend = true;
            continue;
        }

        bool ok = true;
        if (TakeOption(arg, "backend", &value)) {
            ok = ParseEnum(value, kBackendAliases, &options.backend);
        } else if (TakeOption(arg, "sync", &value)) {
            ok = ParseEnum(value, kSyncAliases, &options.sync);
        } else if (TakeOption(arg, "sched", &value)) {
            ok = ParseEnum(value, kSchedAliases, &options.sched);
        } else if (TakeOption(arg, "ctx", &value)) {
            ok = ParseEnum(value, kCtxAliases, &options.ctx);
        } else if (TakeOption(arg, "workers", &value)) {
            ok = ParseSize(value, &options.workers) && options.workers >= 1;
        } else if (TakeOption(arg, "cycles", &value)) {
            ok = ParseSize(value, &options.cycles) && options.cycles >= 1;
        } else if (TakeOption(arg, "warmup", &value)) {
            ok = ParseSize(value, &options.warmup);
        } else if (TakeOption(arg, "files", &value)) {
            ok = ParseSize(value, &options.files) && options.files >= 1;
        } else if (TakeOption(arg, "bytes", &value)) {
            ok = ParseSize(value, &options.fileBytes) && options.fileBytes >= 1;
        } else if (TakeOption(arg, "fixture-dir", &value)) {
            options.fixtureDir = value;
            ok = !value.empty();
        } else if (TakeOption(arg, "out", &value)) {
            options.out = value;
            ok = !value.empty();
        } else if (TakeOption(arg, "min-faults", &value)) {
            ok = ParseSize(value, &options.minFaults);
        } else {
            std::cerr << "system_kernel_bench: unknown argument '" << arg
                      << "'\n\n";
            PrintUsage(std::cerr);
            return 2;
        }

        if (!ok) {
            std::cerr << "system_kernel_bench: bad value for '" << arg
                      << "'\n\n";
            PrintUsage(std::cerr);
            return 2;
        }
    }

    // CSV 를 stdout 으로 보내는 모드에서는 사람이 읽는 줄이 CSV 를 오염시키면
    // 안 되므로 요약을 stderr 로 내린다.
    const bool csvToStdout = (options.out == "-");
    std::ostream& note = csvToStdout ? std::cerr : std::cout;

    std::ofstream file;
    if (!csvToStdout) {
        file.open(options.out, std::ios::out | std::ios::trunc);
        if (!file) {
            std::cerr << "system_kernel_bench: cannot open --out='"
                      << options.out << "'\n";
            return 1;
        }
    }
    std::ostream& csv = csvToStdout ? std::cout : file;

    const auto requestedBackend = static_cast<skl::BackendType>(options.backend);

    skl::FileIoWorkload workload;
    try {
        workload.Init(requestedBackend,
                      static_cast<skl::SyncType>(options.sync),
                      static_cast<skl::SchedType>(options.sched),
                      static_cast<skl::CtxType>(options.ctx),
                      options.workers, options.files, options.fileBytes,
                      std::filesystem::path(options.fixtureDir));
    } catch (const std::exception& e) {
        std::cerr << "system_kernel_bench: workload init failed: " << e.what()
                  << '\n';
        return 1;
    }

    const skl::BackendType actualBackend = workload.Backend();
    const char* backendReqName = kBackendShort[options.backend];
    const char* backendName    = kBackendShort[static_cast<int>(actualBackend)];
    const char* syncName       = kSyncShort[options.sync];
    const char* schedName      = kSchedShort[options.sched];
    const char* ctxName        = kCtxShort[options.ctx];

    // sched 적용 여부는 여기서 읽으면 안 된다. Init() 은 워커를 띄우기만 하고,
    // nice/SCHED_FIFO 는 각 워커가 *자기 루프 시작 시점* 에 자신에게 건다.
    // 그래서 Init() 직후 읽으면 아직 아무도 시도하지 않은 값을 보게 된다 —
    // 실측: 워커 4개가 전부 EPERM 으로 실패했는데도 1 이 기록된 실행이
    // --cycles=3 12회 중 5회. 워밍업이 끝난 뒤(= 모든 워커가 최소 한 사이클을
    // 돌아 적용을 시도한 뒤)에 읽어야 이 열이 실제 상태를 뜻한다.
    int schedApplied = 0;

    if (actualBackend != requestedBackend) {
        std::cerr << "system_kernel_bench: backend '" << backendReqName
                  << "' unavailable at runtime, running '" << backendName
                  << "' instead\n";
        if (options.strictBackend) {
            workload.Shutdown();
            return 1;
        }
    }

    csv << "backend_req,backend,sync,sched,sched_applied,ctx,workers,files,"
           "file_bytes,cycle,cycle_ms,mb_per_s,ctx_sw,page_faults\n";

    const std::uint64_t expectedBytes =
        static_cast<std::uint64_t>(options.files) *
        static_cast<std::uint64_t>(options.fileBytes);

    skl::FrameTimer timer;
    std::vector<double> cycleMs;
    std::vector<double> mbPerSec;
    std::vector<double> ctxSw;
    std::vector<double> faults;
    cycleMs.reserve(options.cycles);
    mbPerSec.reserve(options.cycles);
    ctxSw.reserve(options.cycles);
    faults.reserve(options.cycles);

    std::uint64_t firstChecksum = 0;
    bool          haveChecksum  = false;
    int           mismatches    = 0;

    const std::size_t totalCycles = options.warmup + options.cycles;
    for (std::size_t i = 0; i < totalCycles; ++i) {
        // GUI 루프와 완전히 같은 계측 패턴: 사이클 직전·직후에 getrusage 를
        // 읽고 delta 를 FrameTimer 로 넘긴다. 여기서 읽는 값이 HUD 그래프가
        // 그리는 값과 같은 채널이라는 것이 이 러너의 전제다.
        // snippet-begin: bench-cycle
        timer.BeginCycle();
        const skl::ProcCounters before = skl::ReadProcCounters();
        std::uint64_t bytes = 0;
        try {
            bytes = workload.RunCycle();
        } catch (const std::exception& e) {
            std::cerr << "system_kernel_bench: cycle " << i << " failed: "
                      << e.what() << '\n';
            workload.Shutdown();
            return 1;
        }
        const skl::ProcCounters after = skl::ReadProcCounters();
        const skl::ProcCounters delta = skl::DiffCounters(before, after);
        timer.EndCycle(bytes,
                       static_cast<std::uint64_t>(delta.voluntaryCtxSw +
                                                  delta.involuntaryCtxSw),
                       static_cast<std::uint64_t>(delta.minorFaults +
                                                  delta.majorFaults));
        // snippet-end: bench-cycle

        // 정합성 판정. 성능은 환경마다 다르지만 이 둘은 조합과 무관하게 같아야
        // 한다 — 러너가 워크로드를 잘못 몰고 있으면 여기서 걸린다.
        if (bytes != expectedBytes) {
            std::cerr << "system_kernel_bench: cycle " << i << " read " << bytes
                      << " bytes, expected " << expectedBytes << '\n';
            ++mismatches;
        }
        const std::uint64_t checksum = workload.LastChecksum();
        if (!haveChecksum) {
            firstChecksum = checksum;
            haveChecksum  = true;
        } else if (checksum != firstChecksum) {
            std::cerr << "system_kernel_bench: cycle " << i << " checksum "
                      << checksum << " differs from " << firstChecksum << '\n';
            ++mismatches;
        }

        if (i < options.warmup) continue;  // warmup 은 버린다.

        // 첫 기록 사이클 직전에 한 번 확정한다. 이 시점이면 모든 워커가
        // 최소 한 사이클을 돌아 sched 적용을 이미 시도했다.
        if (i == options.warmup) schedApplied = workload.SchedApplied() ? 1 : 0;

        const std::size_t index = i - options.warmup;
        char row[512];
        std::snprintf(row, sizeof(row),
                      "%s,%s,%s,%s,%d,%s,%zu,%zu,%zu,%zu,%.6f,%.3f,%.0f,%.0f\n",
                      backendReqName, backendName, syncName, schedName,
                      schedApplied, ctxName, options.workers, options.files,
                      options.fileBytes, index,
                      static_cast<double>(timer.LastCycleMs()),
                      static_cast<double>(timer.LastThroughputMBs()),
                      static_cast<double>(timer.LastCtxSwitches()),
                      static_cast<double>(timer.LastPageFaults()));
        csv << row;

        cycleMs.push_back(static_cast<double>(timer.LastCycleMs()));
        mbPerSec.push_back(static_cast<double>(timer.LastThroughputMBs()));
        ctxSw.push_back(static_cast<double>(timer.LastCtxSwitches()));
        faults.push_back(static_cast<double>(timer.LastPageFaults()));
    }

    csv.flush();
    if (!csvToStdout) {
        file.close();
        if (!file) {
            std::cerr << "system_kernel_bench: writing '" << options.out
                      << "' failed\n";
            workload.Shutdown();
            return 1;
        }
    }

    workload.Shutdown();

    note << "[bench] backend=" << backendReqName << "(actual " << backendName
         << ") sync=" << syncName << " sched=" << schedName
         << (schedApplied ? "(applied)" : "(not applied)")
         << " ctx=" << ctxName << " workers=" << options.workers << '\n';
    note << "[bench] files=" << options.files << " x " << options.fileBytes
         << " B  warmup=" << options.warmup << "  cycles=" << options.cycles
         << "  out=" << options.out << '\n';
    const Stats faultStats = Summarize(faults);
    PrintStatsLine(note, "cycle_ms", Summarize(cycleMs),  3);
    PrintStatsLine(note, "mb_per_s", Summarize(mbPerSec), 1);
    PrintStatsLine(note, "ctx_sw",   Summarize(ctxSw),    0);
    PrintStatsLine(note, "faults",   faultStats,          0);
    note << "[bench] bytes/cycle=" << expectedBytes
         << "  checksum=" << firstChecksum << '\n';

    if (options.minFaults > 0 &&
        faultStats.mean < static_cast<double>(options.minFaults)) {
        std::cerr << "system_kernel_bench: mean page faults per cycle "
                  << faultStats.mean << " < required " << options.minFaults
                  << " — the page-fault channel is not measuring anything\n";
        return 1;
    }

    if (mismatches != 0) {
        std::cerr << "system_kernel_bench: " << mismatches
                  << " cycle consistency failure(s)\n";
        return 1;
    }
    return 0;
}
