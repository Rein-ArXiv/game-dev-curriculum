#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace {

void CloseSocket(SocketHandle socket) {
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

std::uint16_t ReservePort(int type, int protocol) {
    SocketHandle socket = ::socket(AF_INET, type, protocol);
    if (socket == kInvalidSocket) return 0;
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = 0;
    if (bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        CloseSocket(socket);
        return 0;
    }
#if defined(_WIN32)
    int length = sizeof(local);
#else
    socklen_t length = sizeof(local);
#endif
    if (getsockname(socket, reinterpret_cast<sockaddr*>(&local), &length) != 0) {
        CloseSocket(socket);
        return 0;
    }
    const std::uint16_t port = ntohs(local.sin_port);
    CloseSocket(socket);
    return port;
}

bool SendOversizedDatagrams(std::uint16_t port) {
    SocketHandle socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == kInvalidSocket) return false;
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server.sin_port = htons(port);
    std::vector<char> payload(4096, 'x');
    for (int i = 0; i < 64; ++i) {
        const int sent = sendto(socket, payload.data(),
            static_cast<int>(payload.size()), 0,
            reinterpret_cast<sockaddr*>(&server), sizeof(server));
        if (sent != static_cast<int>(payload.size())) {
            CloseSocket(socket);
            return false;
        }
    }
    CloseSocket(socket);
    return true;
}

#if defined(_WIN32)
bool Spawn(const std::string& command, PROCESS_INFORMATION& process) {
    std::string mutableCommand = command;
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    process = {};
    return CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, nullptr,
                          &startup, &process) != FALSE;
}

int WaitFor(PROCESS_INFORMATION& process, DWORD timeoutMs) {
    const DWORD wait = WaitForSingleObject(process.hProcess, timeoutMs);
    DWORD code = 99;
    if (wait == WAIT_TIMEOUT) TerminateProcess(process.hProcess, code);
    else GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}
#else
pid_t Spawn(const std::vector<std::string>& arguments) {
    const pid_t pid = fork();
    if (pid == 0) {
        std::vector<char*> raw;
        raw.reserve(arguments.size() + 1);
        for (const std::string& argument : arguments)
            raw.push_back(const_cast<char*>(argument.c_str()));
        raw.push_back(nullptr);
        execv(raw.front(), raw.data());
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

} // namespace

int main(int argc, char** argv) {
    (void)argc;
#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
#endif
    const std::uint16_t tcpPort = ReservePort(SOCK_STREAM, IPPROTO_TCP);
    const std::uint16_t udpPort = ReservePort(SOCK_DGRAM, IPPROTO_UDP);
    if (tcpPort == 0 || udpPort == 0) return 2;

    const std::filesystem::path directory =
        std::filesystem::absolute(argv[0]).parent_path();
#if defined(_WIN32)
    const std::string server = (directory / "game_server.exe").string();
    const std::string client = (directory / "game_client.exe").string();
    PROCESS_INFORMATION serverProcess{};
    const std::string serverCommand = "\"" + server + "\" --tcp-port " +
        std::to_string(tcpPort) + " --udp-port " + std::to_string(udpPort) +
        " --duration 4";
    if (!Spawn(serverCommand, serverProcess)) return 3;
#else
    const std::string server = (directory / "game_server").string();
    const std::string client = (directory / "game_client").string();
    const pid_t serverProcess = Spawn({server, "--tcp-port", std::to_string(tcpPort),
        "--udp-port", std::to_string(udpPort), "--duration", "4"});
    if (serverProcess <= 0) return 3;
#endif

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!SendOversizedDatagrams(udpPort)) return 4;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

#if defined(_WIN32)
    PROCESS_INFORMATION clientProcess{};
    const std::string clientCommand = "\"" + client + "\" --tcp-port " +
        std::to_string(tcpPort) + " --udp-port " + std::to_string(udpPort) +
        " --ticks 60";
    if (!Spawn(clientCommand, clientProcess)) return 5;
    const int clientCode = WaitFor(clientProcess, 7000);
    const int serverCode = WaitFor(serverProcess, 7000);
    WSACleanup();
#else
    const pid_t clientProcess = Spawn({client, "--tcp-port", std::to_string(tcpPort),
        "--udp-port", std::to_string(udpPort), "--ticks", "60"});
    if (clientProcess <= 0) return 5;
    const int clientCode = WaitFor(clientProcess);
    const int serverCode = WaitFor(serverProcess);
#endif
    if (clientCode != 0 || serverCode != 0) {
        std::cerr << "process smoke failed: client=" << clientCode
                  << " server=" << serverCode << '\n';
        return 6;
    }
    std::cout << "game_network_process_tests: oversized UDP recovery passed\n";
    return 0;
}
