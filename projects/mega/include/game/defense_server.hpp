// defense_server — 헤드리스 권위 서버의 루프 그 자체.
//
// games/defense/server_main.cpp 안에 있던 코드다. 클라이언트 쪽과 같은 이유로
// 옮겼다: games/ 는 OS API(소켓·파일·신호·환경 변수)를 직접 부를 수 없고,
// 진입점에 루프가 들어 있으면 그 루프를 테스트가 구동할 방법이 없다.
//
// 신호는 platform::InstallShutdownSignals, 경로·환경 변수는 platform::,
// 소켓은 net::UdpServer, 콘텐츠 로드는 defense:: 가 담당한다. 여기에는
// "게임 규칙 + 접속 승인 + 스냅샷 방송"만 남는다.

#pragma once

#include "game/defense_content.hpp"
#include "game/defense_protocol.hpp"
#include "game/game.hpp"
#include "live/admission_token.hpp"
#include "net/reliable_channel.hpp"
#include "net/udp_server.hpp"
#include "platform/paths.hpp"
#include "platform/process.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mega {
namespace game {

inline constexpr float kServerFixedDt = 1.0f / 60.0f;
inline constexpr uint16_t kServerDefaultPort = 27016;
inline constexpr size_t kServerMaximumPlayers = 2;

struct DefenseServerOptions {
    uint16_t    port = kServerDefaultPort;
    std::string serverId = "mega-local-1";
    std::string assets;              // 비면 실행 파일 옆의 assets/
    std::string secretBase64;        // 비면 LIVE_SERVICE_TOKEN_SECRET, 그것도 없으면 개발용
    int         runSeconds = 0;      // 0 = 무제한(신호로 종료)
};

inline const char* DefenseServerUsage() {
    return "usage: mega_defense_server [--port N] [--server-id ID] "
           "[--assets PATH] [--secret-base64 BASE64] [--run-seconds N]\n";
}

inline bool ParseServerPort(const char* text, uint16_t& port) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || value < 1 || value > 65535) return false;
    port = static_cast<uint16_t>(value);
    return true;
}

inline bool ParseServerPositive(const char* text, int& value) {
    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || parsed < 1 || parsed > 86400) return false;
    value = static_cast<int>(parsed);
    return true;
}

inline bool ParseDefenseServerOptions(int argc, char** argv, DefenseServerOptions& options) {
    options.assets = platform::ExecutableDirectory() + "/assets";
    platform::EnvironmentVariable("LIVE_SERVICE_TOKEN_SECRET", options.secretBase64);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            if (!ParseServerPort(argv[++i], options.port)) return false;
        } else if (std::strcmp(argv[i], "--server-id") == 0 && i + 1 < argc) {
            options.serverId = argv[++i];
        } else if (std::strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
            options.assets = argv[++i];
        } else if (std::strcmp(argv[i], "--secret-base64") == 0 && i + 1 < argc) {
            options.secretBase64 = argv[++i];
        } else if (std::strcmp(argv[i], "--run-seconds") == 0 && i + 1 < argc) {
            if (!ParseServerPositive(argv[++i], options.runSeconds)) return false;
        } else {
            return false;
        }
    }
    return !options.serverId.empty();
}

class DefenseServer {
public:
    using Clock = std::chrono::steady_clock;

    struct ClientSession {
        net::UdpEndpoint     endpoint;
        live::GameAdmission  admission;
        // 클라이언트당 신뢰·순서 수신. 클라이언트가 1번부터 센다.
        net::ReliableOrderedReceiver<wire::Command> commands{1};
        Clock::time_point    lastSeen{};
    };

    DefenseServer() = default;
    DefenseServer(const DefenseServer&) = delete;
    DefenseServer& operator=(const DefenseServer&) = delete;

    bool Init(const DefenseServerOptions& options) {
        options_ = options;

        if (options_.secretBase64.empty()) {
            secret_ = live::LocalDevelopmentSecret();
            std::fprintf(stderr,
                "[security] using the local development token secret; do not expose this server\n");
        } else {
            auto decoded = live::DecodeSecret(options_.secretBase64);
            if (!decoded) {
                std::fprintf(stderr, "token secret must be base64 for at least 32 bytes\n");
                return false;
            }
            secret_ = std::move(*decoded);
        }

        bool externalMap = false, externalWaves = false, externalBalance = false;
        const DefenseMap map =
            defense::LoadMapFile(options_.assets + "/level.txt", &externalMap);
        const defense::BalanceConfig balance =
            defense::LoadBalanceScript(options_.assets + "/balance.script", &externalBalance);
        const std::vector<WaveDef> waves = defense::ApplyBalance(
            defense::LoadWaveFile(options_.assets + "/waves.txt",
                                  defense::DefaultWaves(), &externalWaves),
            balance);
        world_.Init(map, waves, balance.startGold, balance.coreHp);

        if (!socket_.Open(options_.port)) {
            std::fprintf(stderr, "server socket failed: %s\n", socket_.LastError().c_str());
            return false;
        }
        std::printf("[server] id=%s udp=0.0.0.0:%u capacity=%zu\n",
                    options_.serverId.c_str(), socket_.LocalPort(), kServerMaximumPlayers);
        std::printf("[content] map=%s waves=%s balance=%s\n",
                    externalMap ? "external" : "fallback",
                    externalWaves ? "external" : "fallback",
                    externalBalance ? "external" : "fallback");
        started_ = Clock::now();
        nextTick_ = started_;
        return true;
    }

    // 한 번의 서비스 사이클: 수신 처리 → 시뮬레이션 → 스냅샷 방송.
    void Step(Clock::time_point now) {
        net::UdpDatagram datagram;
        while (socket_.Receive(datagram)) HandleDatagram(datagram, now);

        if (!simulationStarted_ && clients_.size() == kServerMaximumPlayers) {
            simulationStarted_ = true;
            nextTick_ = now;
            std::printf("[match] start id=%s\n", activeMatch_.c_str());
        }

        if (simulationStarted_ && now >= nextTick_) {
            int catchUp = 0;
            while (now >= nextTick_ && catchUp < 5) {
                if (!world_.Won() && !world_.Lost()) world_.Step(kServerFixedDt);
                ++tick_;
                ++catchUp;
                nextTick_ += std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<double>(kServerFixedDt));
            }
            if (catchUp == 5 && now >= nextTick_) nextTick_ = now;
            if (tick_ % 3 == 0) BroadcastSnapshot();
        }
    }

    // 종료 조건: 신호 또는 --run-seconds 상한.
    bool ShouldContinue(Clock::time_point now) const {
        if (platform::ShutdownRequested()) return false;
        if (options_.runSeconds > 0 &&
            now - started_ >= std::chrono::seconds(options_.runSeconds)) return false;
        return true;
    }

    void PrintSummary() const {
        std::printf("[server] stop tick=%u players=%zu commands=%llu rejected=%llu result=%s\n",
                    tick_, clients_.size(),
                    static_cast<unsigned long long>(acceptedCommands_),
                    static_cast<unsigned long long>(rejectedPackets_),
                    world_.Won() ? "win" : (world_.Lost() ? "loss" : "incomplete"));
    }

    const GameWorld& World() const { return world_; }
    uint32_t Tick() const { return tick_; }
    size_t ClientCount() const { return clients_.size(); }
    uint64_t AcceptedCommands() const { return acceptedCommands_; }
    uint64_t RejectedPackets() const { return rejectedPackets_; }

private:
    static int64_t UnixNow() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void HandleDatagram(const net::UdpDatagram& datagram, Clock::time_point now) {
        const std::string endpointKey = datagram.sender.Key();
        if (endpointKey.empty()) {
            ++rejectedPackets_;
            return;
        }

        std::string token;
        if (wire::ReadAdmissionHello(datagram.bytes, token)) {
            AdmitClient(datagram, endpointKey, token, now);
            return;
        }

        auto client = clients_.find(endpointKey);
        if (client == clients_.end()) {
            ++rejectedPackets_;
            return;
        }
        client->second.lastSeen = now;
        wire::Command command;
        if (!wire::ReadCommand(datagram.bytes, command) ||
            !client->second.commands.Accept(command.sequence, command)) {
            ++rejectedPackets_;
            return;
        }
        while (client->second.commands.Poll(command))
            if (wire::ApplyCommand(world_, command)) ++acceptedCommands_;
    }

    void AdmitClient(const net::UdpDatagram& datagram, const std::string& endpointKey,
                     const std::string& token, Clock::time_point now) {
        live::GameAdmission admission;
        if (!live::VerifyGameAdmission(token, secret_, options_.serverId, UnixNow(), admission)) {
            ++rejectedPackets_;
            return;
        }
        auto existingClient = clients_.find(endpointKey);
        if (existingClient != clients_.end()) {
            if (existingClient->second.admission.tokenId != admission.tokenId) {
                ++rejectedPackets_;
            } else {
                existingClient->second.endpoint = datagram.sender;
                existingClient->second.lastSeen = now;
            }
            return;
        }
        const auto tokenOwner = tokenEndpoints_.find(admission.tokenId);
        const auto playerOwner = playerEndpoints_.find(admission.playerId);
        const bool tokenConflict = tokenOwner != tokenEndpoints_.end() &&
                                   tokenOwner->second != endpointKey;
        const bool playerConflict = playerOwner != playerEndpoints_.end() &&
                                    playerOwner->second != endpointKey;
        const bool matchConflict = !activeMatch_.empty() && activeMatch_ != admission.matchId;
        const bool full = clients_.size() >= kServerMaximumPlayers;
        if (tokenConflict || playerConflict || matchConflict || full) {
            ++rejectedPackets_;
            return;
        }
        if (activeMatch_.empty()) activeMatch_ = admission.matchId;
        tokenEndpoints_[admission.tokenId] = endpointKey;
        playerEndpoints_[admission.playerId] = endpointKey;
        ClientSession& session = clients_[endpointKey];
        session.endpoint = datagram.sender;
        session.admission = std::move(admission);
        session.lastSeen = now;
        std::printf("[admission] player=%s endpoint=%s players=%zu/%zu\n",
                    session.admission.playerId.c_str(), endpointKey.c_str(),
                    clients_.size(), kServerMaximumPlayers);
    }

    void BroadcastSnapshot() {
        for (const auto& [key, client] : clients_) {
            (void)key;
            const net::Bytes snapshot =
                wire::WriteSnapshot(world_, tick_, client.commands.LastDelivered());
            if (!snapshot.empty()) socket_.SendTo(client.endpoint, snapshot);
        }
    }

    DefenseServerOptions options_;
    std::vector<uint8_t> secret_;
    GameWorld            world_;
    net::UdpServer       socket_;

    std::unordered_map<std::string, ClientSession> clients_;
    std::unordered_map<std::string, std::string>   tokenEndpoints_;
    std::unordered_map<std::string, std::string>   playerEndpoints_;
    std::string       activeMatch_;
    Clock::time_point started_{};
    Clock::time_point nextTick_{};
    uint64_t          rejectedPackets_ = 0;
    uint64_t          acceptedCommands_ = 0;
    uint32_t          tick_ = 0;
    bool              simulationStarted_ = false;
};

// 진입점이 부르는 단 하나의 함수.
inline int RunDefenseServer(const DefenseServerOptions& options) {
    DefenseServer server;
    if (!server.Init(options)) return 1;
    platform::InstallShutdownSignals();

    while (true) {
        const DefenseServer::Clock::time_point now = DefenseServer::Clock::now();
        if (!server.ShouldContinue(now)) break;
        server.Step(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    server.PrintSummary();
    return 0;
}

}  // namespace game
}  // namespace mega
