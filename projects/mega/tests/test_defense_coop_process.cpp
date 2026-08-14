// 협동 한 판을 제품 실행 파일 **둘**로 실제로 붙인다.
//
// 이 저장소에는 이미 프로세스를 띄우는 테스트가 둘 있다. 그러나 둘 다 반쪽이다:
//   test_defense_process   — 호스트도 클라이언트도 테스트가 직접 구현한다.
//   test_dedicated_process — 서버만 제품이고 클라이언트는 테스트가 구현한다.
// 그래서 "제품 클라이언트가 실제로 명령을 내보내는가"는 어느 쪽도 묻지 않았고,
// 그 배선을 통째로 죽인 변이가 53개 테스트를 전부 통과했다.
//
// 여기서는 양쪽이 모두 mega_defense 다. 테스트가 대신 구현하는 코드는 한 줄도
// 없다 — argv 파싱부터 프레임 루프까지 전부 제품이 돈다. 이 테스트가 하는 일은
// 프로세스를 띄우고, 두 표준 출력을 읽고, 왕복이 일어났는지 세는 것뿐이다.
//
// ── 결정성 ────────────────────────────────────────────────────────
//   포트    OS 가 고르게 하고(bind 0), 호스트가 그 포트를 못 잡으면 새 포트로 재시도.
//   대기    고정 sleep 없음. 호스트가 "[network] hosting UDP port N" 을 실제로
//           찍을 때까지 로그를 폴링한 뒤에야 클라이언트를 띄운다. 제품은 그
//           줄에서 stdout 을 flush 한다 — 안 그러면 리다이렉트된 파일에서는
//           전량 버퍼링 탓에 프로세스가 끝날 때까지 그 줄이 보이지 않는다.
//   재시도  "둘이 아예 만나지 못했다"(호스트 스냅샷 0)일 때만 다시 돈다. 그것은
//           환경 문제다. "만났는데 명령이 오지 않았다"는 재시도하지 않는다 —
//           그것이 정확히 이 테스트가 잡으려는 결함이기 때문이다.

#include "net/udp_transport.hpp"
#include "test_util.h"

#include <chrono>
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

namespace {

using Clock = std::chrono::steady_clock;

// ── 프로세스 제어(플랫폼별로 이 셋만 다르다) ──────────────────────────
#ifdef _WIN32
struct Process {
    PROCESS_INFORMATION info{};
    HANDLE              log = INVALID_HANDLE_VALUE;
    bool                valid = false;
};

std::string JoinCommandLine(const std::vector<std::string>& arguments) {
    std::string line;
    for (const std::string& argument : arguments) {
        if (!line.empty()) line.push_back(' ');
        line += "\"" + argument + "\"";
    }
    return line;
}

bool Spawn(const std::vector<std::string>& arguments, const std::string& logPath,
           Process& out) {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    // 자식이 쓰는 동안 이 프로세스가 읽어야 하므로 공유 읽기를 연다.
    HANDLE log = CreateFileA(logPath.c_str(), GENERIC_WRITE,
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

    std::string line = JoinCommandLine(arguments);
    const BOOL created =
        CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &startup, &out.info);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!created) {
        CloseHandle(log);
        return false;
    }
    out.log = log;
    out.valid = true;
    return true;
}

bool Exited(Process& process, int& code) {
    if (!process.valid) return true;
    DWORD status = 0;
    if (!GetExitCodeProcess(process.info.hProcess, &status)) return false;
    if (status == STILL_ACTIVE) return false;
    code = static_cast<int>(status);
    return true;
}

int Wait(Process& process, int milliseconds) {
    if (!process.valid) return 99;
    DWORD code = 99;
    if (WaitForSingleObject(process.info.hProcess, static_cast<DWORD>(milliseconds)) ==
        WAIT_TIMEOUT) {
        TerminateProcess(process.info.hProcess, code);
        WaitForSingleObject(process.info.hProcess, 2000);
    } else {
        GetExitCodeProcess(process.info.hProcess, &code);
    }
    CloseHandle(process.info.hThread);
    CloseHandle(process.info.hProcess);
    CloseHandle(process.log);
    process.valid = false;
    return static_cast<int>(code);
}
#else
struct Process {
    pid_t pid = -1;
    bool  valid = false;
    bool  reaped = false;
    int   code = 99;
};

bool Spawn(const std::vector<std::string>& arguments, const std::string& logPath,
           Process& out) {
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
    out.pid = pid;
    out.valid = true;
    return true;
}

bool Exited(Process& process, int& code) {
    if (!process.valid) return true;
    if (process.reaped) {
        code = process.code;
        return true;
    }
    int status = 0;
    if (waitpid(process.pid, &status, WNOHANG) != process.pid) return false;
    process.reaped = true;
    process.code = WIFEXITED(status) ? WEXITSTATUS(status) : 99;
    code = process.code;
    return true;
}

int Wait(Process& process, int milliseconds) {
    if (!process.valid) return 99;
    const auto deadline = Clock::now() + std::chrono::milliseconds(milliseconds);
    int code = 99;
    while (Clock::now() < deadline) {
        if (Exited(process, code)) {
            process.valid = false;
            return code;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    kill(process.pid, SIGKILL);
    int status = 0;
    waitpid(process.pid, &status, 0);
    process.valid = false;
    return 99;
}
#endif

// 로그·프로필 이름을 이 프로세스에 묶는다. 고정 이름을 쓰면 같은 디렉터리에서
// 두 인스턴스가 돌 때(또는 앞선 실행의 잔해가 남았을 때) 서로의 출력을 읽고
// 엉뚱한 판정을 내린다 — 그렇게 생긴 실패는 진짜 결함과 구분되지 않는다.
std::string ProcessTag() {
#ifdef _WIN32
    return std::to_string(static_cast<unsigned long>(GetCurrentProcessId()));
#else
    return std::to_string(static_cast<long>(getpid()));
#endif
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// 조건 폴링: 토큰이 로그에 나타나거나, 프로세스가 죽거나, 데드라인이 지날 때까지.
bool WaitForToken(const std::filesystem::path& log, const std::string& token,
                  Process& process, Clock::time_point deadline) {
    for (;;) {
        if (ReadText(log).find(token) != std::string::npos) return true;
        int code = 0;
        if (Exited(process, code)) return ReadText(log).find(token) != std::string::npos;
        if (Clock::now() >= deadline) return ReadText(log).find(token) != std::string::npos;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

// "key=<정수>" 를 찾아 값을 돌려준다. 없으면 -1.
long long MetricValue(const std::string& text, const std::string& key) {
    const size_t at = text.find(key);
    if (at == std::string::npos) return -1;
    return std::strtoll(text.c_str() + at + key.size(), nullptr, 10);
}

void DumpLog(const char* label, const std::string& text) {
    std::printf("  ---- %s ----\n", label);
    size_t begin = 0;
    while (begin < text.size()) {
        size_t end = text.find('\n', begin);
        if (end == std::string::npos) end = text.size();
        std::printf("  | %.*s\n", static_cast<int>(end - begin), text.c_str() + begin);
        begin = end + 1;
    }
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    std::printf("[test] defense_coop_process\n");

    const std::filesystem::path binaryDirectory =
        std::filesystem::path(argv[0]).parent_path();
#ifdef _WIN32
    const std::filesystem::path product = binaryDirectory / "mega_defense.exe";
#else
    const std::filesystem::path product = binaryDirectory / "mega_defense";
#endif
    CHECK(std::filesystem::exists(product));
    if (!std::filesystem::exists(product)) {
        std::printf("[test] defense_coop_process: %d checks, %d failures\n",
                    megatest::g_checks, megatest::g_failures);
        return 1;
    }

    const std::string tag = ProcessTag();
    const std::filesystem::path hostLog = binaryDirectory / ("coop_host_" + tag + ".log");
    const std::filesystem::path clientLog =
        binaryDirectory / ("coop_client_" + tag + ".log");
    // 플레이어 프로필도, 기존 mega_defense_selftest 의 프로필도 건드리지 않는다.
    const std::filesystem::path hostProfile =
        binaryDirectory / ("coop_host_profile_" + tag + ".wal");
    std::error_code ignored;
    std::filesystem::remove(hostProfile, ignored);
    std::filesystem::remove(hostLog, ignored);
    std::filesystem::remove(clientLog, ignored);

    std::string hostText;
    std::string clientText;
    int hostCode = 99;
    int clientCode = 99;
    bool met = false;      // 두 프로세스가 서로를 본 적이 있는가
    int attempts = 0;

    for (; attempts < 3 && !met; ++attempts) {
        net::UdpTransport reservation;
        if (!reservation.OpenHost(0)) continue;
        const uint16_t port = reservation.LocalPort();
        reservation.Close();

        Process host;
        if (!Spawn({product.string(), "--host", std::to_string(port), "--selftest",
                    "6000", "--profile", hostProfile.string()},
                   hostLog.string(), host))
            continue;

        // 예약과 실제 bind 사이에 포트를 뺏길 수 있다. 그때 호스트는 이 줄을
        // 찍지 못하고 종료 코드 1 로 죽는다 — 새 포트로 다시 돈다.
        const std::string ready = "[network] hosting UDP port " + std::to_string(port);
        if (!WaitForToken(hostLog, ready, host, Clock::now() + std::chrono::seconds(20))) {
            Wait(host, 5000);
            continue;
        }

        Process client;
        if (!Spawn({product.string(), "--join", "127.0.0.1", std::to_string(port),
                    "--selftest", "4000"},
                   clientLog.string(), client)) {
            Wait(host, 30000);
            continue;
        }

        clientCode = Wait(client, 120000);
        hostCode = Wait(host, 120000);
        hostText = ReadText(hostLog);
        clientText = ReadText(clientLog);
        met = MetricValue(hostText, "snapshots=") > 0;
    }

    // 여기서 met 이 거짓이면 둘이 만나지도 못한 것이다. 그것도 실패다 — 다만
    // 위 재시도를 이미 다 쓴 뒤라는 뜻이다.
    CHECK(met);

    const long long remoteCommands = MetricValue(hostText, "remote_commands=");
    const long long hostSnapshots = MetricValue(hostText, "snapshots=");
    const long long clientCommandsSent = MetricValue(clientText, "commands_sent=");
    // 앞의 공백이 있어야 [metrics] 줄의 "command_ack=" 와 섞이지 않는다.
    const long long clientAck = MetricValue(clientText, " ack=");

    std::printf("[test] defense_coop_process: attempts=%d host_exit=%d client_exit=%d "
                "remote_commands=%lld snapshots=%lld client_sent=%lld client_ack=%lld\n",
                attempts, hostCode, clientCode, remoteCommands, hostSnapshots,
                clientCommandsSent, clientAck);

    // ── 호스트가 본 것 ────────────────────────────────────────────────
    // 제품 클라이언트가 명령을 실제로 내보냈고, 호스트가 그것을 순서대로 반영했다.
    // 클라이언트 명령 경로가 죽으면 이 값은 0 이 된다.
    CHECK(remoteCommands > 0);
    CHECK(hostSnapshots > 0);
    CHECK_EQ(hostCode, 0);

    // ── 클라이언트가 본 것 ────────────────────────────────────────────
    // 클라이언트 셀프테스트가 성공/실패를 판정할 수 있어야 이 줄이 성립한다.
    CHECK_EQ(clientCode, 0);
    CHECK(clientText.find("result=LINKED") != std::string::npos);
    CHECK(clientCommandsSent > 0);
    CHECK(clientAck > 0);

    if (megatest::g_failures != 0) {
        // 실패했을 때만 남긴다. 두 프로세스의 표준 출력이 곧 진단이다.
        DumpLog("host stdout", hostText);
        DumpLog("client stdout", clientText);
    } else {
        std::filesystem::remove(hostLog, ignored);
        std::filesystem::remove(clientLog, ignored);
        std::filesystem::remove(hostProfile, ignored);
    }

    std::printf("[test] defense_coop_process: %d checks, %d failures\n",
                megatest::g_checks, megatest::g_failures);
    return megatest::g_failures == 0 ? 0 : 1;
}
