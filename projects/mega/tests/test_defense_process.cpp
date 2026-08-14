// Real UDP handshake, command, and snapshot across two independent processes.
#include "game/defense_protocol.hpp"
#include "net/udp_transport.hpp"
#include "test_util.h"

#include <chrono>
#include <cstdlib>
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

bool ReceiveUntil(net::UdpTransport& transport, net::Bytes& packet,
                  Clock::time_point deadline) {
    while (Clock::now() < deadline) {
        if (transport.Receive(packet)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

int RunHost(uint16_t port) {
    net::UdpTransport host;
    if (!host.OpenHost(port)) return 10;
    const auto deadline = Clock::now() + std::chrono::seconds(4);
    net::Bytes packet;
    while (Clock::now() < deadline && !host.PeerKnown()) {
        if (!ReceiveUntil(host, packet, deadline)) break;
        if (wire::ReadHello(packet)) host.AcceptLastSender();
    }
    if (!host.PeerKnown()) return 11;

    wire::Command command;
    while (Clock::now() < deadline) {
        if (!ReceiveUntil(host, packet, deadline)) break;
        if (wire::ReadCommand(packet, command)) break;
    }
    if (command.sequence != 1) return 12;

    DefenseMap map = DefenseMap::CreateDefault();
    GameWorld world;
    world.Init(map, {WaveDef{1, 10, 20, 5, 0.1f}}, 200, 100);
    if (!wire::ApplyCommand(world, command)) return 13;
    host.Send(wire::WriteSnapshot(world, 1, command.sequence));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return 0;
}

int RunClient(uint16_t port) {
    net::UdpTransport client;
    if (!client.OpenClient("127.0.0.1", port)) return 20;

    DefenseMap map = DefenseMap::CreateDefault();
    const auto position = map.CellCenter(2, 2);
    wire::Command command;
    command.sequence = 1;
    command.type = wire::CommandType::Place;
    command.x = wire::EncodePosition(position.x);
    command.y = wire::EncodePosition(position.y);
    command.argument = static_cast<uint8_t>(TowerType::Cannon);

    const auto deadline = Clock::now() + std::chrono::seconds(4);
    net::Bytes packet;
    auto nextSend = Clock::time_point{};
    while (Clock::now() < deadline) {
        if (Clock::now() >= nextSend) {
            client.Send(wire::WriteHello());
            client.Send(wire::WriteCommand(command));
            nextSend = Clock::now() + std::chrono::milliseconds(30);
        }
        if (client.Receive(packet)) {
            wire::Snapshot snapshot;
            if (wire::ReadSnapshot(packet, snapshot) &&
                snapshot.acknowledgedCommand == 1 &&
                snapshot.towers.size() == 1) {
                return 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 21;
}

#ifdef _WIN32
bool Spawn(const std::string& executable, const char* mode, uint16_t port,
           PROCESS_INFORMATION& process) {
    std::string command = "\"" + executable + "\" " + mode + " " +
                          std::to_string(port);
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    process = {};
    return CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, nullptr,
                          &startup, &process) != FALSE;
}

int WaitFor(PROCESS_INFORMATION& process) {
    const DWORD wait = WaitForSingleObject(process.hProcess, 6000);
    DWORD code = 99;
    if (wait == WAIT_TIMEOUT) TerminateProcess(process.hProcess, code);
    else GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}
#else
pid_t Spawn(const char* executable, const char* mode, uint16_t port) {
    const pid_t pid = fork();
    if (pid == 0) {
        const std::string text = std::to_string(port);
        execl(executable, executable, mode, text.c_str(), nullptr);
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
    if (argc == 3 && std::string(argv[1]) == "--child-host")
        return RunHost(static_cast<uint16_t>(std::strtoul(argv[2], nullptr, 10)));
    if (argc == 3 && std::string(argv[1]) == "--child-client")
        return RunClient(static_cast<uint16_t>(std::strtoul(argv[2], nullptr, 10)));

    std::printf("[test] defense_process\n");
    net::UdpTransport reservation;
    CHECK(reservation.OpenHost(0));
    const uint16_t port = reservation.LocalPort();
    reservation.Close();

#ifdef _WIN32
    PROCESS_INFORMATION host{}, client{};
    CHECK(Spawn(argv[0], "--child-host", port, host));
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    CHECK(Spawn(argv[0], "--child-client", port, client));
    const int clientCode = WaitFor(client);
    const int hostCode = WaitFor(host);
#else
    const pid_t host = Spawn(argv[0], "--child-host", port);
    CHECK(host > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    const pid_t client = Spawn(argv[0], "--child-client", port);
    CHECK(client > 0);
    const int clientCode = WaitFor(client);
    const int hostCode = WaitFor(host);
#endif
    CHECK_EQ(hostCode, 0);
    CHECK_EQ(clientCode, 0);
    std::printf("[test] defense_process: %d checks, %d failures\n",
                megatest::g_checks, megatest::g_failures);
    return megatest::g_failures == 0 ? 0 : 1;
}
