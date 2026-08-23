// 프로필 상태의 누적 — 제품에서는 기능, 검사에서는 결함.
//
// ── 무엇이 문제였나 ───────────────────────────────────────────────
// `--selftest` 는 진행도를 진짜 파일에 진짜로 커밋한다. 그것이 이 검사의 값이다
// (커밋 경로가 죽어도 "돌기는 하는" 셀프테스트는 아무것도 증명하지 않는다).
// 그런데 경로를 주지 않으면 전부 실행 파일 옆의 `selftest_profile.wal` **하나**를
// 쓰고, 그 파일은 지워지지 않는다. 그래서 지갑이 실행마다 데워지고, 데워진
// 지갑은 다음 판의 시작 골드를 바꾼다. 같은 명령이 몇 번째 실행이냐에 따라
// 다른 결과를 낸다 — 냉각 프로필에서 두 번 돌린 실측:
//
//   1회차  [meta] scrap=0 tier=0 start_gold=420+0
//          [selftest] result=CLEAR ticks=1377 towers=10 wave=6 packet=181
//   2회차  [meta] buy start_gold_tier=1 cost=200 balance=8
//          [selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=192
//
// 아티클들의 "실행과 관측"은 1회차 수치를 인용한다. 그 상태에서는 ctest 를 한 번
// 돌린 독자가 인용된 수를 두 번 다시 보지 못하고, 값을 단언하는 검사를 세우는
// 순간 그것은 플래키가 된다.
//
// ── 이 파일이 묻는 것 셋 ──────────────────────────────────────────
//   1. 누적은 **기능**인가. 같은 프로필로 두 판을 돌리면 둘째 판의 시작 조건이
//      실제로 달라지는가(지갑이 남고, 티어가 오르고, 시작 골드가 오르는가).
//      이것이 참이어야 28편의 메타 경제가 성립한다.
//   2. `--profile PATH --fresh-profile` 을 준 실행은 **몇 번째든 같은가**.
//      제품 실행 파일을 두 번 띄워 결과 줄을 바이트로 대조한다.
//   3. 그 인자를 빼면 **실제로 갈리는가**(음성 대조). 이것이 없으면 2번은
//      "무엇을 해도 통과"가 될 수 있다 — 두 실행이 우연히 같은 것과 격리가
//      작동한 것을 구분하지 못한다.
//
// 그리고 넷째로, 경로 없이 `--fresh-profile` 만 준 실행이 **거절되는가**.
// 비우기는 되돌릴 수 없고, 경로가 없다는 것은 플레이어의 진짜 프로필을 연다는
// 뜻이다. 조용히 지우는 것보다 종료 코드 2 가 낫다.
//
// ── 결정성 ────────────────────────────────────────────────────────
//   시각    전부 주입한다(`--match-unix`). 이벤트 창은 주 단위로 반복하므로
//           벽시계로 돌리면 이 검사는 요일에 좌우된다.
//   프로필  검사가 쓰는 파일은 전부 이 프로세스 id 로 이름 짓고, 시작과 끝에
//           지운다. 고정 이름을 쓰면 앞선 실행의 잔해가 이 검사의 전제를 깬다 —
//           그것이 정확히 이 파일이 고치려는 결함이다.
//   재현    실패하면 실제로 돌린 명령줄을 그대로 찍는다. 붙여 넣으면 같은
//           상태가 다시 만들어진다.

#include "game/defense_app.hpp"
#include "live/economy.hpp"
#include "test_util.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace mega;
using namespace mega::game;

namespace {

// CMakeLists 의 MEGA_SELFTEST_UNIX_QUIET 과 같은 값이다. 활성 이벤트 0개 —
// 이벤트가 없는 상태가 이 검사의 기준선이다.
constexpr int64_t kQuietUnix = 1767312000;   // 2026-01-02 00:00 UTC
constexpr int     kTicks     = 25000;

std::string ProcessTag() {
#ifdef _WIN32
    return std::to_string(static_cast<unsigned long>(GetCurrentProcessId()));
#else
    return std::to_string(static_cast<long>(getpid()));
#endif
}

std::filesystem::path BinaryDirectory() {
    return std::filesystem::path(platform::ExecutableDirectory());
}

std::filesystem::path ProductPath() {
#ifdef _WIN32
    return BinaryDirectory() / "mega_defense.exe";
#else
    return BinaryDirectory() / "mega_defense";
#endif
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void RemoveQuietly(const std::filesystem::path& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

// ── 제품 실행 파일 한 번 띄우기 ───────────────────────────────────
// 폴링도 재시도도 없다. 끝까지 기다렸다가 종료 코드와 표준 출력을 돌려준다.
// (동시성이 필요한 프로세스 검사는 `test_defense_coop_process` 가 따로 있다.)
struct Run {
    int         exitCode = 99;
    std::string stdoutText;
    std::string commandLine;   // 실패 진단 = 재현 명령
};

#ifdef _WIN32
std::string JoinCommandLine(const std::vector<std::string>& arguments) {
    std::string line;
    for (const std::string& argument : arguments) {
        if (!line.empty()) line.push_back(' ');
        line += "\"" + argument + "\"";
    }
    return line;
}

bool SpawnAndWait(const std::vector<std::string>& arguments,
                  const std::filesystem::path& logPath, int milliseconds, int& codeOut) {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE log = CreateFileA(logPath.string().c_str(), GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) return false;
    HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &attributes, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nul;
    startup.hStdOutput = log;
    startup.hStdError = log;

    PROCESS_INFORMATION info{};
    std::string line = JoinCommandLine(arguments);
    const BOOL created =
        CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &startup, &info);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!created) {
        CloseHandle(log);
        return false;
    }
    DWORD code = 99;
    if (WaitForSingleObject(info.hProcess, static_cast<DWORD>(milliseconds)) == WAIT_TIMEOUT) {
        TerminateProcess(info.hProcess, code);
        WaitForSingleObject(info.hProcess, 2000);
    } else {
        GetExitCodeProcess(info.hProcess, &code);
    }
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    CloseHandle(log);
    codeOut = static_cast<int>(code);
    return true;
}
#else
std::string JoinCommandLine(const std::vector<std::string>& arguments) {
    std::string line;
    for (const std::string& argument : arguments) {
        if (!line.empty()) line.push_back(' ');
        line += argument;
    }
    return line;
}

bool SpawnAndWait(const std::vector<std::string>& arguments,
                  const std::filesystem::path& logPath, int milliseconds, int& codeOut) {
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        if (!std::freopen(logPath.c_str(), "w", stdout)) _exit(97);
        dup2(fileno(stdout), fileno(stderr));
        std::vector<char*> raw;
        raw.reserve(arguments.size() + 1);
        for (const std::string& argument : arguments)
            raw.push_back(const_cast<char*>(argument.c_str()));
        raw.push_back(nullptr);
        execv(raw[0], raw.data());
        _exit(98);
    }
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    for (;;) {
        int status = 0;
        const pid_t seen = waitpid(pid, &status, WNOHANG);
        if (seen == pid) {
            codeOut = WIFEXITED(status) ? WEXITSTATUS(status) : 99;
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            int drained = 0;
            waitpid(pid, &drained, 0);
            codeOut = 99;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
#endif

Run RunProduct(const std::vector<std::string>& arguments, const std::string& logName) {
    Run out;
    std::vector<std::string> full;
    full.push_back(ProductPath().string());
    for (const std::string& argument : arguments) full.push_back(argument);
    out.commandLine = JoinCommandLine(full);

    const std::filesystem::path log = BinaryDirectory() / logName;
    RemoveQuietly(log);
    if (!SpawnAndWait(full, log, 240000, out.exitCode)) {
        out.exitCode = 96;
        return out;
    }
    out.stdoutText = ReadText(log);
    RemoveQuietly(log);
    return out;
}

// 표준 출력에서 접두사로 시작하는 첫 줄을 통째로 돌려준다(개행 제외).
// 없으면 빈 문자열 — 비교 자리에서 두 빈 문자열이 "같다"로 통과하지 않도록
// 호출부가 비어 있지 않음을 따로 단언한다.
std::string LineStartingWith(const std::string& text, const std::string& prefix) {
    size_t begin = 0;
    while (begin <= text.size()) {
        size_t end = text.find('\n', begin);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind(prefix, 0) == 0) return line;
        if (end == text.size()) break;
        begin = end + 1;
    }
    return {};
}

void ReportRun(const char* label, const Run& run) {
    std::printf("  %s: exit=%d\n    $ %s\n", label, run.exitCode, run.commandLine.c_str());
}

// ══ 1. 누적은 기능이다 ══════════════════════════════════════════════
//
// 제품 `DefenseApp` 을 같은 파일 위에서 두 번 세운다. 둘째 판의 **시작 조건**이
// 첫 판의 정산 때문에 달라지는지를 묻는다. 이 단언이 죽으면 28편의 메타 경제가
// 성립하지 않는다 — 그래서 아래 2번이 요구하는 격리는 이 성질을 없애는 것이
// 아니라 검사 쪽에서만 끄는 것이어야 한다.
//
// 값을 손으로 적지 않는다. 티어 비용도 시작 골드 보너스도 `live::Economy` 의
// 정적 함수에서 그대로 가져와 관계식으로 단언한다. 상수를 베껴 적으면 밸런스가
// 바뀐 날 이 검사가 "배선이 죽었다"고 거짓 보고한다.
static void TestWarmProfileAdvancesStartConditions() {
    const std::filesystem::path profile =
        BinaryDirectory() / ("profile_isolation_warm_" + ProcessTag() + ".wal");
    RemoveQuietly(profile);

    DefenseOptions options;
    options.mode = RunMode::Solo;
    options.selftest = true;
    options.matchUnixOverride = kQuietUnix;
    options.profilePath = profile.string();
    options.buyStartGoldTier = static_cast<int>(live::kMaxStartGoldTier);

    // ── 첫 판: 냉각 프로필 ────────────────────────────────────────
    uint32_t coldTier = 99;
    int      coldGold = -1;
    int64_t  earned   = -1;
    bool     shopOpen = false;
    {
        DefenseApp app;
        CHECK(app.Init(options));
        // Init 이 상점을 이미 지났다. 냉각 프로필은 잔액 0이므로 아무것도 사지
        // 못한다 — 그것이 이 판이 티어 0으로 도는 이유다.
        coldTier = app.StartGoldTier();
        coldGold = app.World().Gold();
        shopOpen = app.MetaShopOpen();
        const SelftestReport report = RunDefenseHeadless(app, kTicks);
        CHECK(report.ticks > 0);
        CHECK(app.Finished());
        earned = app.ScrapBalance();
        std::string why;
        CHECK(app.EconomyConserved(&why));
    }
    CHECK_EQ(coldTier, 0u);
    CHECK(coldGold > 0);
    // 이 판이 스크랩을 한 푼도 벌지 않았다면 아래 전부가 공회전한다.
    CHECK(earned > 0);
    // 운영 토글이 상점을 닫아 두면 `--buy-tier` 는 아무 일도 하지 않는다.
    // 그 상태에서 아래 단언이 실패하면 원인은 배선이 아니라 flags.txt 다.
    CHECK(shopOpen);

    // ── 둘째 판: 같은 파일, 데워진 프로필 ─────────────────────────
    uint32_t warmTier = 99;
    int      warmGold = -1;
    int64_t  warmBalance = -1;
    {
        DefenseApp app;
        CHECK(app.Init(options));
        warmTier = app.StartGoldTier();
        warmGold = app.World().Gold();
        warmBalance = app.ScrapBalance();
    }
    // 지갑이 판을 넘어 남았고, 그 잔액이 티어를 샀다.
    CHECK(warmTier > coldTier);
    // 산 만큼 정확히 빠졌다. 비용표는 `live::Economy` 에서 읽는다.
    int64_t spent = 0;
    for (uint32_t tier = coldTier; tier < warmTier; ++tier)
        spent += live::Economy::StartGoldTierCost(tier);
    CHECK_EQ(warmBalance, earned - spent);
    // 그리고 그 티어가 **이번 판의 시작 골드**를 올렸다. 여기가 누적이
    // 시뮬레이션을 바꾸는 정확한 자리다.
    CHECK_EQ(warmGold, coldGold + live::Economy::StartGoldBonus(warmTier));
    CHECK(warmGold > coldGold);

    // ── 셋째 판: 같은 옵션, 비운 파일 ─────────────────────────────
    // 격리가 실제로 시작 조건을 되돌리는지. 이 셋째 판이 첫 판과 같아야
    // "--fresh-profile 을 준 실행은 몇 번째든 같다"가 성립한다.
    CHECK(TruncateFile(options.profilePath));
    uint32_t freshTier = 99;
    int      freshGold = -1;
    {
        DefenseApp app;
        CHECK(app.Init(options));
        freshTier = app.StartGoldTier();
        freshGold = app.World().Gold();
    }
    CHECK_EQ(freshTier, coldTier);
    CHECK_EQ(freshGold, coldGold);

    std::printf("  누적: 냉각 tier=%u gold=%d → 정산 %lld → 데움 tier=%u gold=%d "
                "balance=%lld(=%lld-%lld) → 비움 tier=%u gold=%d\n",
                coldTier, coldGold, static_cast<long long>(earned), warmTier, warmGold,
                static_cast<long long>(warmBalance), static_cast<long long>(earned),
                static_cast<long long>(spent), freshTier, freshGold);

    RemoveQuietly(profile);
}

// ══ 2. 격리한 실행은 몇 번째든 같다 ═════════════════════════════════
//
// 여기서만 프로세스 경계를 넘는다. 같은 명령을 두 번 띄우고 결과 줄을 **바이트로**
// 대조한다. `--buy-tier` 를 켜는 이유는 그것이 누적이 시뮬레이션에 닿는 유일한
// 제품 경로이기 때문이다 — 그 인자가 없으면 누적은 출력만 바꾸고 게임은 같아서,
// 이 검사가 잡으려는 드리프트가 결과 줄에 나타나지 않는다.
static void TestIsolatedSelftestRepeatsExactly() {
    const std::filesystem::path profile =
        BinaryDirectory() / ("profile_isolation_repeat_" + ProcessTag() + ".wal");
    RemoveQuietly(profile);

    const std::vector<std::string> arguments{
        "--selftest", std::to_string(kTicks),
        "--buy-tier", std::to_string(live::kMaxStartGoldTier),
        "--match-unix", std::to_string(kQuietUnix),
        "--profile", profile.string(),
        "--fresh-profile"};

    const Run first = RunProduct(arguments, "profile_isolation_a_" + ProcessTag() + ".log");
    const Run second = RunProduct(arguments, "profile_isolation_b_" + ProcessTag() + ".log");

    CHECK_EQ(first.exitCode, 0);
    CHECK_EQ(second.exitCode, 0);

    const std::string resultA = LineStartingWith(first.stdoutText, "[selftest] result=");
    const std::string resultB = LineStartingWith(second.stdoutText, "[selftest] result=");
    const std::string metaA = LineStartingWith(first.stdoutText, "[meta] scrap=");
    const std::string metaB = LineStartingWith(second.stdoutText, "[meta] scrap=");

    // 빈 문자열끼리 같다고 통과하지 않게 존재부터 단언한다.
    CHECK(!resultA.empty());
    CHECK(!metaA.empty());
    // 비웠다는 사실이 출력에 남는다 — 이 줄이 없으면 인자가 먹지 않은 것이다.
    CHECK(first.stdoutText.find("[meta] fresh profile=") != std::string::npos);

    CHECK_EQ(resultA, resultB);
    CHECK_EQ(metaA, metaB);
    // 냉각 프로필의 지갑은 0에서 출발한다. 이것이 아티클들이 인용하는 상태다.
    CHECK(metaA.rfind("[meta] scrap=0 tier=0", 0) == 0);

    if (megatest::g_failures != 0) {
        ReportRun("1회차", first);
        ReportRun("2회차", second);
    }
    std::printf("  격리 2회: %s\n         %s\n", resultA.c_str(), metaA.c_str());
    RemoveQuietly(profile);
}

// ══ 3. 음성 대조 — 격리를 빼면 실제로 갈린다 ════════════════════════
//
// 2번만 있으면 이 파일은 "무엇을 해도 통과"가 될 수 있다. 두 실행이 우연히
// 같았는지, 격리가 작동해서 같은지 구분하지 못하기 때문이다. 그래서 같은 명령을
// `--fresh-profile` **없이** 두 번 돌려 갈리는 것을 확인한다. 여기서 두 줄이
// 같아지면 그때는 이 파일 전체가 아무것도 재지 않고 있다는 뜻이다.
static void TestWithoutIsolationTheRunsDrift() {
    const std::filesystem::path profile =
        BinaryDirectory() / ("profile_isolation_drift_" + ProcessTag() + ".wal");
    RemoveQuietly(profile);

    const std::vector<std::string> arguments{
        "--selftest", std::to_string(kTicks),
        "--buy-tier", std::to_string(live::kMaxStartGoldTier),
        "--match-unix", std::to_string(kQuietUnix),
        "--profile", profile.string()};

    const Run first = RunProduct(arguments, "profile_drift_a_" + ProcessTag() + ".log");
    const Run second = RunProduct(arguments, "profile_drift_b_" + ProcessTag() + ".log");

    CHECK_EQ(first.exitCode, 0);
    CHECK_EQ(second.exitCode, 0);
    CHECK(first.stdoutText.find("[meta] fresh profile=") == std::string::npos);

    const std::string metaA = LineStartingWith(first.stdoutText, "[meta] scrap=");
    const std::string metaB = LineStartingWith(second.stdoutText, "[meta] scrap=");
    CHECK(!metaA.empty());
    CHECK(!metaB.empty());
    // 지갑이 남았다 — 둘째 판은 0에서 출발하지 않는다.
    CHECK(metaA != metaB);

    // 그리고 그 잔액이 실제로 티어를 샀다. 한 판의 정산이 티어 하나 값에
    // 못 미치게 밸런스가 바뀌면 여기서 죽는다 — 그때는 이 검사와
    // `mega_defense_selftest_meta` 의 warm 단계를 함께 다시 봐야 한다.
    CHECK(second.stdoutText.find("[meta] buy start_gold_tier=") != std::string::npos);

    // 여기가 감사가 지적한 그 드리프트다: 같은 명령, 다른 결과 줄.
    const std::string resultA = LineStartingWith(first.stdoutText, "[selftest] result=");
    const std::string resultB = LineStartingWith(second.stdoutText, "[selftest] result=");
    CHECK(!resultA.empty());
    CHECK(resultA != resultB);

    if (megatest::g_failures != 0) {
        ReportRun("1회차", first);
        ReportRun("2회차", second);
    }
    std::printf("  누적 2회: %s\n         %s\n", resultA.c_str(), resultB.c_str());
    RemoveQuietly(profile);
}

// ══ 4. 경로 없는 비우기는 거절된다 ══════════════════════════════════
//
// `--fresh-profile` 만 주면 비울 대상은 플레이어의 진짜 프로필이다. 되돌릴 수
// 없는 일을 인자 하나의 오타로 하게 두지 않는다.
static void TestFreshWithoutPathIsRefused() {
    const Run run = RunProduct({"--selftest", "100", "--fresh-profile"},
                               "profile_isolation_refuse_" + ProcessTag() + ".log");
    // 2 = 인자 오류. 1(실행 실패)도 0(통과)도 아니어야 한다.
    CHECK_EQ(run.exitCode, 2);
    if (run.exitCode != 2) ReportRun("거절", run);
    std::printf("  거절: exit=%d\n", run.exitCode);
}

}  // namespace

static void RunTests() {
    if (!std::filesystem::exists(ProductPath())) {
        ++megatest::g_failures;
        std::printf("  FAIL 제품 실행 파일이 없다: %s\n", ProductPath().string().c_str());
        return;
    }
    TestWarmProfileAdvancesStartConditions();
    TestIsolatedSelftestRepeatsExactly();
    TestWithoutIsolationTheRunsDrift();
    TestFreshWithoutPathIsRefused();
}

TEST_MAIN("profile_isolation")
