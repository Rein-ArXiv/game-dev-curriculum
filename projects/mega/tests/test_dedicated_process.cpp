// Spawn the product server and join it through two independent UDP endpoints.
#include "game/defense_protocol.hpp"
#include "game/game.hpp"
#include "live/admission_token.hpp"
#include "net/udp_transport.hpp"
#include "test_util.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace mega;
using namespace mega::game;
namespace wire = mega::game::wire;

namespace {

using Clock = std::chrono::steady_clock;

std::string Base64Url(std::span<const uint8_t> bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string output;
    uint32_t accumulator = 0;
    int bits = 0;
    for (uint8_t byte : bytes) {
        accumulator = (accumulator << 8) | byte;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            output.push_back(alphabet[(accumulator >> bits) & 63]);
        }
    }
    if (bits > 0) output.push_back(alphabet[(accumulator << (6 - bits)) & 63]);
    return output;
}

std::string Token(std::string player, std::string tokenId) {
    const std::string json =
        "{\"Kind\":\"game\",\"PlayerId\":\"" + player +
        "\",\"MatchId\":\"process-match\",\"ServerId\":\"process-server\"," 
        "\"TokenId\":\"" + tokenId + "\",\"ExpiresAt\":4102444800}";
    const auto secret = live::LocalDevelopmentSecret();
    const std::string body = Base64Url(std::span(
        reinterpret_cast<const uint8_t*>(json.data()), json.size()));
    const auto signature = live::detail::Hmac(
        secret,
        std::span(reinterpret_cast<const uint8_t*>(body.data()), body.size()));
    return body + "." + Base64Url(signature);
}

#ifdef _WIN32
bool SpawnServer(const std::string& executable, uint16_t port,
                 PROCESS_INFORMATION& process) {
    std::string command = "\"" + executable + "\" --port " +
        std::to_string(port) +
        " --server-id process-server --run-seconds 3";
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    process = {};
    return CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, nullptr,
                          &startup, &process) != FALSE;
}

int WaitFor(PROCESS_INFORMATION& process) {
    const DWORD wait = WaitForSingleObject(process.hProcess, 5000);
    DWORD code = 99;
    if (wait == WAIT_TIMEOUT) TerminateProcess(process.hProcess, code);
    else GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}
#else
pid_t SpawnServer(const std::string& executable, uint16_t port) {
    const pid_t pid = fork();
    if (pid == 0) {
        const std::string text = std::to_string(port);
        execl(executable.c_str(), executable.c_str(), "--port", text.c_str(),
              "--server-id", "process-server", "--run-seconds", "3", nullptr);
        _exit(98);
    }
    return pid;
}

int WaitFor(pid_t process) {
    int status = 0;
    if (waitpid(process, &status, 0) < 0 || !WIFEXITED(status)) return 99;
    return WEXITSTATUS(status);
}
#endif

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    std::printf("[test] dedicated_process\n");
    net::UdpTransport reservation;
    CHECK(reservation.OpenHost(0));
    const uint16_t port = reservation.LocalPort();
    reservation.Close();

    std::filesystem::path server = std::filesystem::path(argv[0]).parent_path();
#ifdef _WIN32
    server /= "mega_defense_server.exe";
    PROCESS_INFORMATION process{};
    CHECK(SpawnServer(server.string(), port, process));
#else
    server /= "mega_defense_server";
    const pid_t process = SpawnServer(server.string(), port);
    CHECK(process > 0);
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    net::UdpTransport first;
    net::UdpTransport second;
    CHECK(first.OpenClient("127.0.0.1", port));
    CHECK(second.OpenClient("127.0.0.1", port));
    const net::Bytes firstHello = wire::WriteAdmissionHello(Token("alice", "token-a"));
    const net::Bytes secondHello = wire::WriteAdmissionHello(Token("bob", "token-b"));

    DefenseMap map = DefenseMap::CreateDefault();
    wire::Command command;
    command.sequence = 1;
    command.type = wire::CommandType::Place;
    const auto position = map.CellCenter(2, 2);
    command.x = wire::EncodePosition(position.x);
    command.y = wire::EncodePosition(position.y);
    command.argument = static_cast<uint8_t>(TowerType::Cannon);

    bool firstReady = false;
    bool secondReady = false;
    auto nextSend = Clock::time_point{};
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    while (Clock::now() < deadline && (!firstReady || !secondReady)) {
        if (Clock::now() >= nextSend) {
            first.Send(firstHello);
            second.Send(secondHello);
            first.Send(wire::WriteCommand(command));
            nextSend = Clock::now() + std::chrono::milliseconds(40);
        }
        net::Bytes packet;
        while (first.Receive(packet)) {
            wire::Snapshot snapshot;
            if (wire::ReadSnapshot(packet, snapshot) &&
                snapshot.acknowledgedCommand == 1 && snapshot.towers.size() == 1)
                firstReady = true;
        }
        while (second.Receive(packet)) {
            wire::Snapshot snapshot;
            if (wire::ReadSnapshot(packet, snapshot) && snapshot.towers.size() == 1)
                secondReady = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(firstReady);
    CHECK(secondReady);

#ifdef _WIN32
    const int serverCode = WaitFor(process);
#else
    const int serverCode = WaitFor(process);
#endif
    CHECK_EQ(serverCode, 0);
    std::printf("[test] dedicated_process: %d checks, %d failures\n",
                megatest::g_checks, megatest::g_failures);
    return megatest::g_failures == 0 ? 0 : 1;
}
