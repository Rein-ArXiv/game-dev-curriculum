// Spawn the product server and join it through two independent UDP endpoints.
#include "game/defense_content.hpp"
#include "game/defense_protocol.hpp"
#include "game/game.hpp"
#include "platform/detect.hpp"
#include "tools/asset_pipeline.hpp"
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

std::string Token(std::string player, std::string tokenId, std::string matchId) {
    const std::string json =
        "{\"Kind\":\"game\",\"PlayerId\":\"" + player +
        "\",\"MatchId\":\"" + matchId + "\",\"ServerId\":\"process-server\"," 
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

    // 서버가 발행할 콘텐츠 해시를 **같은 바이트에서 같은 방식으로** 다시 만든다.
    // 서버 로그를 읽어 베끼면 그 값이 재현 가능한지는 검사되지 않는다.
    const std::filesystem::path assetRoot =
        std::filesystem::path(argv[0]).parent_path() / "assets";
    tools::AssetPipeline pipeline;
    const uint64_t contentHash = defense::CookAssets(assetRoot.string(), pipeline);
    CHECK(contentHash != 0);
    const uint8_t osCode = mega::platform::OSCode(mega::platform::CurrentOS());
    const uint8_t archCode = mega::platform::ArchCode(mega::platform::CurrentArch());

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

    // ── 0단계: 형성되지 않은 MatchId 는 서명이 유효해도 들어오지 못한다 ──
    // 이 단계가 없으면 아래 큐 왕복은 "이름을 받아 왔다"만 보이고, 그 이름이
    // **필요했다**는 것은 보이지 않는다.
    const net::Bytes forgedHello = wire::WriteAdmissionHello(
        Token("alice", "token-forged", "m-0000-00000000"), {}, contentHash, osCode, archCode);
    bool forgedAdmitted = false;
    auto forgedDeadline = Clock::now() + std::chrono::milliseconds(400);
    while (Clock::now() < forgedDeadline) {
        first.Send(forgedHello);
        net::Bytes packet;
        while (first.Receive(packet)) {
            wire::Snapshot snapshot;
            if (wire::ReadSnapshot(packet, snapshot)) forgedAdmitted = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(!forgedAdmitted);

    // ── 1단계: 큐 요청 → 형성 → MatchId 통지 ──
    const net::Bytes aliceQueue = wire::WriteQueueRequest("alice", 1200);
    const net::Bytes bobQueue = wire::WriteQueueRequest("bob", 1250);
    CHECK(!aliceQueue.empty());
    std::string aliceMatch, bobMatch;
    const auto queueDeadline = Clock::now() + std::chrono::seconds(2);
    while (Clock::now() < queueDeadline && (aliceMatch.empty() || bobMatch.empty())) {
        first.Send(aliceQueue);
        second.Send(bobQueue);
        net::Bytes packet;
        std::string id;
        while (first.Receive(packet))
            if (wire::ReadQueueResponse(packet, id)) aliceMatch = id;
        while (second.Receive(packet))
            if (wire::ReadQueueResponse(packet, id)) bobMatch = id;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(!aliceMatch.empty());
    CHECK(!bobMatch.empty());
    CHECK_EQ(aliceMatch, bobMatch);                 // 같은 매치에 배정됐다
    CHECK_EQ(aliceMatch.size(), (size_t)15);        // FormatMatchId 의 폭
    std::printf("[test] dedicated_process: queue -> match id=%s\n", aliceMatch.c_str());

    // ── 1.5단계: 콘텐츠 해시가 다르면 이름이 맞아도 못 들어온다 ──
    // 밸런스 한 줄을 고친 클라이언트가 조용히 접속할 수 있던 상태가 정확히
    // 이 검사가 없던 상태다.
    const net::Bytes staleHello = wire::WriteAdmissionHello(
        Token("alice", "token-stale", aliceMatch), {}, contentHash ^ 0x1ull, osCode, archCode);
    bool staleAdmitted = false;
    const auto staleDeadline = Clock::now() + std::chrono::milliseconds(400);
    while (Clock::now() < staleDeadline) {
        first.Send(staleHello);
        net::Bytes packet;
        while (first.Receive(packet)) {
            wire::Snapshot snapshot;
            wire::AdmissionGrant grant;
            if (wire::ReadSnapshot(packet, snapshot)) staleAdmitted = true;
            if (wire::ReadAdmissionGrant(packet, grant)) staleAdmitted = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(!staleAdmitted);

    // ── 2단계: 그 이름과 그 콘텐츠로만 입장 ──
    const net::Bytes firstHello = wire::WriteAdmissionHello(
        Token("alice", "token-a", aliceMatch), {}, contentHash, osCode, archCode);
    const net::Bytes secondHello = wire::WriteAdmissionHello(
        Token("bob", "token-b", bobMatch), {}, contentHash, osCode, archCode);

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
    bool grantSeen = false;
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
            wire::AdmissionGrant grant;
            if (wire::ReadAdmissionGrant(packet, grant)) {
                // 승인은 콘텐츠 정체성과 서버 플랫폼과 확정된 배정을 실어 온다.
                CHECK_EQ(grant.contentHash, contentHash);
                CHECK(grant.contentVersion > 0);
                CHECK_EQ(grant.serverOs, osCode);
                grantSeen = true;
                continue;
            }
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
    CHECK(grantSeen);          // ← 승인 패킷을 안 보내면 여기서 죽는다

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
