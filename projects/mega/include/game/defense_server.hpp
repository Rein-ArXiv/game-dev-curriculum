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
#include "live/event_schedule.hpp"
#include "live/feature_flag.hpp"
#include "live/patch.hpp"
#include "mp/matchmaking.hpp"
#include "mp/rollback.hpp"
#include "net/reliable_channel.hpp"
#include "net/udp_server.hpp"
#include "platform/capability.hpp"
#include "platform/console.hpp"
#include "platform/detect.hpp"
#include "platform/paths.hpp"
#include "platform/process.hpp"
#include "tools/asset_pipeline.hpp"

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
    // 서버도 같은 A/B 레지스트리를 읽는다. 스냅샷을 전체로 보낼지 차분으로
    // 보낼지는 **보내는 쪽**이 정하는 것이고, 그 쪽이 여기다.
    Switches    switches;
};

inline const char* DefenseServerUsage() {
    return "usage: mega_defense_server [--port N] [--server-id ID] "
           "[--assets PATH] [--secret-base64 BASE64] [--run-seconds N]\n"
           "                           [--ab name=value]...\n";
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
    // 순서 규약은 클라이언트와 같다: 기본값 → 환경 변수 → 명령줄.
    std::string switchError;
    if (!options.switches.LoadEnvironment(&switchError)) {
        std::fprintf(stderr, "MEGA_AB: %s\n", switchError.c_str());
        return false;
    }
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ab") == 0 && i + 1 < argc) {
            if (!options.switches.Parse(argv[++i], &switchError)) {
                std::fprintf(stderr, "--ab %s\n", switchError.c_str());
                return false;
            }
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
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
        // 합류 헤더는 **링크마다** 하나의 시퀀스 공간을 요구한다. 서버가 하나를
        // 공유하면 두 클라이언트의 ack 비트필드가 서로의 패킷을 확인한 것으로
        // 읽혀, 기준선 선택과 RTT 표본이 동시에 거짓이 된다.
        net::PacketSequencer seq;
        net::AckWindow       window;
        // 기준선도 링크마다 하나다. 두 클라이언트가 하나를 공유하면 한쪽이
        // ack 한 장을 다른 쪽의 차분 기준선으로 쓰게 된다.
        net::BaselineStore   baselines{wire::kSnapshotBaselineSlots};
        // 아바타 서브시뮬에서 이 클라이언트가 맡은 자리. 입장 순서로 정해지고,
        // 그 뒤로 바뀌지 않는다 — 자리가 흔들리면 같은 입력이 다른 사람의
        // 아바타를 움직인다.
        int                  avatarSlot = -1;
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

        // ── 콘텐츠 정체성 ──────────────────────────────────────────
        // 서버가 발행한 해시가 입장 검사의 기준이다. 0(미발행)이면 검사할 것이
        // 없고, 그 상태는 조용히 지나가면 안 되므로 로그로 남긴다.
        std::string cookError;
        contentHash_ = defense::CookAssets(options_.assets, pipeline_, &cookedCount_,
                                           &registeredAssets_, &cookError);
        if (!cookError.empty())
            std::fprintf(stderr, "[content] 쿡 실패, 콘텐츠 버전 없이 진행: %s\n",
                         cookError.c_str());
        else
            patch_.Publish(1, contentHash_);

        std::string flagError, eventError;
        flags_ = defense::LoadFeatureFlags(options_.assets + "/flags.txt", &externalFlags_,
                                           &flagError);
        if (!flagError.empty())
            std::fprintf(stderr, "[content] flags.txt 거부, 전원 기본 팔로 진행: %s\n",
                         flagError.c_str());
        events_ = defense::LoadEventSchedule(options_.assets + "/events.txt", &externalEvents_,
                                             &eventError);
        if (!eventError.empty())
            std::fprintf(stderr, "[content] events.txt 거부, 이벤트 없이 진행: %s\n",
                         eventError.c_str());
        matchStartUnix_ = UnixNow();
        eventMods_ = events_.ModifiersAt(matchStartUnix_);

        bool externalMap = false, externalWaves = false, externalBalance = false;
        map_ = defense::LoadMapFile(options_.assets + "/level.txt", &externalMap);
        const DefenseMap& map = map_;
        // 클라이언트와 같은 규칙: 거부된 스크립트는 프로세스를 세우지 않고
        // 기본 구성으로 흡수되며, 거부 사유는 표준 오류로 나간다.
        std::string balanceError;
        const defense::BalanceConfig balance = defense::LoadBalanceScript(
            options_.assets + "/balance.script", &externalBalance, &balanceError);
        if (!balanceError.empty())
            std::fprintf(stderr, "[content] balance.script 거부, 기본 구성으로 진행: %s\n",
                         balanceError.c_str());
        const defense::BalanceConfig tuned = defense::ApplyEventModifiers(balance, eventMods_);
        const std::vector<WaveDef> waves = defense::ApplyBossRush(
            defense::ApplyBalance(
                defense::LoadWaveFile(options_.assets + "/waves.txt",
                                      defense::DefaultWaves(), &externalWaves),
                tuned),
            eventMods_.bossRush);
        world_.Init(map, waves, tuned.startGold, tuned.coreHp);
        world_.SetBlastCost(kBlastCost + eventMods_.blastCostDelta);
        // 정원은 매치가 정하고, kServerMaximumPlayers 는 서버의 상한이다. 둘은
        // 지금 같은 수지만 같은 것이 아니다 — 하나는 이 프로세스가 감당하는 수,
        // 다른 하나는 이 판이 요구하는 수다.
        matchmaker_.Init(static_cast<int>(kServerMaximumPlayers), 100);
        avatars_.Init(static_cast<int>(kServerMaximumPlayers), 8);
        avatars_.SetPrediction(options_.switches.avatarPredict);

        if (!socket_.Open(options_.port)) {
            std::fprintf(stderr, "server socket failed: %s\n", socket_.LastError().c_str());
            return false;
        }
        std::printf("[server] id=%s udp=0.0.0.0:%u capacity=%zu\n",
                    options_.serverId.c_str(), socket_.LocalPort(), kServerMaximumPlayers);
        std::printf("[platform] %s profile=%s\n", platform::CurrentPlatformLine().c_str(),
                    platform::ProfileFor(platform::CurrentOS()).name);
        std::printf("[patch] version=%u hash=0x%016llX assets=%d cooked=%d\n",
                    patch_.Current(), static_cast<unsigned long long>(contentHash_),
                    registeredAssets_ + 1, cookedCount_);
        std::printf("[flags] source=%s variants=%zu toggles=%zu\n",
                    externalFlags_ ? "file" : "default",
                    flags_.DefinitionCount(), flags_.ToggleCount());
        std::printf("[events] source=%s active=%zu scrap=x%.2f reward=x%.2f enemy_hp=x%.2f "
                    "start_gold=%+d blast_cost=%+d boss_rush=%d\n",
                    externalEvents_ ? "file" : "none",
                    events_.ActiveAt(matchStartUnix_).size(),
                    static_cast<double>(eventMods_.scrapMultiplier),
                    static_cast<double>(eventMods_.rewardMultiplier),
                    static_cast<double>(eventMods_.enemyHpMultiplier),
                    eventMods_.bonusStartGold, eventMods_.blastCostDelta,
                    eventMods_.bossRush ? 1 : 0);
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
            std::printf("[match] start id=%s capacity=%zu\n", activeMatch_.c_str(),
                        matches_.Capacity(activeMatch_));
        }

        if (simulationStarted_ && now >= nextTick_) {
            int catchUp = 0;
            while (now >= nextTick_ && catchUp < 5) {
                if (!world_.Won() && !world_.Lost()) world_.Step(kServerFixedDt);
                // 서버의 아바타는 전원이 원격이다. 비예측 팔(기본)에서는 두
                // 클라이언트의 입력이 다 모인 프레임까지만 전진한다.
                if (avatars_.Tick()) ++avatarFrame_;
                ++tick_;
                // 매치가 끝나면 그 이름은 장부에서 사라진다. 남겨 두면 같은
                // 이름으로 다시 들어올 수 있고, 그러면 형성이라는 단계가 무의미해진다.
                if ((world_.Won() || world_.Lost()) && !activeMatch_.empty()) {
                    std::printf("[match] end id=%s result=%s version=%u\n",
                                activeMatch_.c_str(), world_.Won() ? "win" : "loss",
                                patch_.Current());
                    std::fflush(stdout);
                    matches_.Forget(activeMatch_);
                    activeMatch_.clear();
                }
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
        std::printf("[server] queue requests=%llu responses=%llu duplicates=%llu "
                    "matches=%llu\n",
                    static_cast<unsigned long long>(queueRequests_),
                    static_cast<unsigned long long>(queueResponses_),
                    static_cast<unsigned long long>(queueDuplicates_),
                    static_cast<unsigned long long>(matchesFormed_));
        std::printf("[server] stop tick=%u players=%zu commands=%llu rejected=%llu "
                    "snapshots=%llu snapshot_bytes=%llu avg=%.1f result=%s\n",
                    tick_, clients_.size(),
                    static_cast<unsigned long long>(acceptedCommands_),
                    static_cast<unsigned long long>(rejectedPackets_),
                    static_cast<unsigned long long>(snapshotsSent_),
                    static_cast<unsigned long long>(snapshotBytes_),
                    snapshotsSent_ ? static_cast<double>(snapshotBytes_) /
                                         static_cast<double>(snapshotsSent_) : 0.0,
                    world_.Won() ? "win" : (world_.Lost() ? "loss" : "incomplete"));
    }

    const GameWorld& World() const { return world_; }
    uint32_t Tick() const { return tick_; }
    size_t ClientCount() const { return clients_.size(); }
    uint64_t AcceptedCommands() const { return acceptedCommands_; }
    uint64_t RejectedPackets() const { return rejectedPackets_; }
    uint64_t SnapshotsSent() const { return snapshotsSent_; }
    uint64_t SnapshotBytes() const { return snapshotBytes_; }
    uint32_t AvatarFrame() const { return avatarFrame_; }
    uint32_t AvatarChecksum32() const {
        return static_cast<uint32_t>(avatars_.Checksum() & 0xFFFFFFFFu);
    }
    uint64_t AvatarInputPackets() const { return avatarInputPackets_; }
    uint64_t AvatarRelayPackets() const { return avatarRelayPackets_; }
    uint64_t ContentRejects() const { return contentRejects_; }
    uint64_t BuildRejectedOutOfReach() const { return buildRejectedOutOfReach_; }
    uint64_t GrantsSent() const { return grantsSent_; }
    uint64_t ContentHash() const { return contentHash_; }
    uint32_t ContentVersion() const { return patch_.Current(); }
    const live::EventModifiers& EventModifiers() const { return eventMods_; }
    uint64_t QueueRequests() const { return queueRequests_; }
    uint64_t QueueResponses() const { return queueResponses_; }
    uint64_t QueueDuplicates() const { return queueDuplicates_; }
    uint64_t MatchesFormed() const { return matchesFormed_; }

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

        // 큐 요청이 입장보다 먼저다. 형성되지 않은 매치에는 아무도 못 들어온다.
        net::PacketHeader queueFraming;
        std::string queuePlayer;
        int queueRating = 0;
        if (wire::ReadQueueRequest(datagram.bytes, queuePlayer, queueRating, &queueFraming)) {
            HandleQueueRequest(datagram, queuePlayer, queueRating);
            return;
        }

        std::string token;
        net::PacketHeader framing;
        wire::AdmissionHelloDetail detail;
        if (wire::ReadAdmissionHello(datagram.bytes, token, &framing, &detail)) {
            AdmitClient(datagram, endpointKey, token, now, framing, detail);
            return;
        }

        auto client = clients_.find(endpointKey);
        if (client == clients_.end()) {
            ++rejectedPackets_;
            return;
        }
        client->second.lastSeen = now;

        // 아바타 프레임 입력. 신뢰 채널을 타지 않으므로 여기서 순서를 따지지
        // 않는다 — 같은 프레임이 여러 번 와도 SetInput 은 멱등이다.
        uint32_t baseFrame = 0;
        std::vector<mp::Input> inputs;
        if (wire::ReadInputPacket(datagram.bytes, baseFrame, inputs, &framing)) {
            client->second.window.OnReceived(framing.seq);
            client->second.baselines.Acknowledge(framing);
            if (client->second.avatarSlot >= 0)
                for (size_t i = 0; i < inputs.size(); ++i)
                    avatars_.SetInput(baseFrame + static_cast<uint32_t>(i),
                                      client->second.avatarSlot, inputs[i], true);
            ++avatarInputPackets_;
            RelayAvatarInput(endpointKey, baseFrame, inputs);
            return;
        }

        wire::Command command;
        if (!wire::ReadCommand(datagram.bytes, command, &framing) ||
            !client->second.commands.Accept(command.sequence, command)) {
            ++rejectedPackets_;
            return;
        }
        client->second.window.OnReceived(framing.seq);
        client->second.baselines.Acknowledge(framing);
        while (client->second.commands.Poll(command)) {
            // 사거리 규칙은 **명령을 낸 사람의 아바타**로 판정한다. 전용 서버는
            // 자기 아바타가 없으므로 그 자리가 곧 클라이언트의 슬롯이다.
            if (!BuildAllowed(command, client->second.avatarSlot)) {
                ++buildRejectedOutOfReach_;
                continue;
            }
            if (wire::ApplyCommand(world_, command)) ++acceptedCommands_;
        }
    }

    // 줄 세우기 → 형성 → 이름 통지. 세 단계가 전부 여기 있다.
    void HandleQueueRequest(const net::UdpDatagram& datagram, const std::string& playerId,
                            int rating) {
        const uint32_t key = wire::PlayerKey(playerId);
        queueEndpoints_[key] = datagram.sender;
        mp::Player player;
        player.id = key;
        player.rating = rating;
        if (!matchmaker_.Enqueue(player, static_cast<uint64_t>(UnixNow()))) {
            ++queueDuplicates_;      // 이미 줄에 있다. 두 자리를 주면 정원이 거짓이 된다.
            return;
        }
        ++queueRequests_;
        for (const mp::Match& match : matchmaker_.FormMatches(static_cast<uint64_t>(UnixNow()))) {
            if (!matches_.Register(match)) continue;
            ++matchesFormed_;
            std::printf("[queue] formed id=%s players=%zu rating=%d..%d\n",
                        match.id.c_str(), match.players.size(), match.minRating,
                        match.maxRating);
            std::fflush(stdout);
            for (uint32_t member : match.players) {
                auto endpoint = queueEndpoints_.find(member);
                if (endpoint == queueEndpoints_.end()) continue;
                socket_.SendTo(endpoint->second, wire::WriteQueueResponse(match.id));
                ++queueResponses_;
            }
        }
    }

    void AdmitClient(const net::UdpDatagram& datagram, const std::string& endpointKey,
                     const std::string& token, Clock::time_point now,
                     const net::PacketHeader& framing,
                     const wire::AdmissionHelloDetail& detail) {
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
                existingClient->second.window.OnReceived(framing.seq);
                existingClient->second.baselines.Acknowledge(framing);
            }
            return;
        }
        const auto tokenOwner = tokenEndpoints_.find(admission.tokenId);
        const auto playerOwner = playerEndpoints_.find(admission.playerId);
        const bool tokenConflict = tokenOwner != tokenEndpoints_.end() &&
                                   tokenOwner->second != endpointKey;
        const bool playerConflict = playerOwner != playerEndpoints_.end() &&
                                    playerOwner->second != endpointKey;
        // 형성된 매치의 명단에 이 사람이 있는가. 이전에는 "먼저 온 토큰이 정한
        // 이름과 같은가"만 물었고, 그 질문에는 서명만 유효하면 아무 이름의 매치나
        // 여는 답이 있었다.
        if (!matches_.Allows(admission.matchId, wire::PlayerKey(admission.playerId))) {
            ++rejectedPackets_;
            return;
        }
        // 콘텐츠 정체성. 발행된 서버에서 다른 해시를 든 클라이언트는 같은 게임을
        // 돌고 있지 않다 — 밸런스 한 줄을 고친 클라이언트가 조용히 접속할 수
        // 있던 상태가 정확히 이 검사가 없던 상태다.
        if (patch_.Published() && !patch_.Accepts(detail.contentHash)) {
            ++rejectedPackets_;
            ++contentRejects_;
            std::printf("[admission] reject reason=content_mismatch player=%s "
                        "client=0x%016llX server=0x%016llX\n",
                        admission.playerId.c_str(),
                        static_cast<unsigned long long>(detail.contentHash),
                        static_cast<unsigned long long>(patch_.CurrentHash()));
            std::fflush(stdout);
            return;
        }
        // 정원은 둘 다 검사한다: 이 판의 정원과 이 프로세스의 상한.
        const size_t capacity = matches_.Capacity(admission.matchId);
        const bool full = clients_.size() >= capacity ||
                          clients_.size() >= kServerMaximumPlayers;
        if (tokenConflict || playerConflict || full) {
            ++rejectedPackets_;
            return;
        }
        activeMatch_ = admission.matchId;
        tokenEndpoints_[admission.tokenId] = endpointKey;
        playerEndpoints_[admission.playerId] = endpointKey;
        ClientSession& session = clients_[endpointKey];
        session.endpoint = datagram.sender;
        session.admission = std::move(admission);
        session.lastSeen = now;
        session.window.OnReceived(framing.seq);
        session.avatarSlot = static_cast<int>(clients_.size()) - 1;
        // 매치 단위 플래그 배정. 플레이어마다 다르면 한 매치 안에서 두 사람이
        // 서로 다른 **권위** 팔을 요구하게 된다.
        const live::FlagSet assigned =
            flags_.Resolve(defense::MatchBucketId(session.admission.matchId));
        if (!flagsApplied_) {
            defense::LiveToggles toggles;
            const live::FlagSet forSwitches = defense::SplitLiveToggles(assigned, toggles);
            (void)toggles;   // 운영 토글의 소비자는 프로필을 가진 클라이언트다
            std::string rejected;
            options_.switches.ApplyFlags(forSwitches, &rejected);
            if (!rejected.empty())
                std::fprintf(stderr, "[flags] 일부 배정을 무시했다: %s\n", rejected.c_str());
            flagsApplied_ = true;
        }
        wire::AdmissionGrant grant;
        grant.contentVersion = patch_.Current();
        grant.contentHash = patch_.CurrentHash();
        grant.serverOs = platform::OSCode(platform::CurrentOS());
        grant.serverArch = platform::ArchCode(platform::CurrentArch());
        grant.matchStartUnix = matchStartUnix_;
        grant.flags = assigned.Encode();
        const net::Bytes grantBytes =
            wire::WriteAdmissionGrant(grant, session.window.Header(session.seq.Next()));
        socket_.SendTo(session.endpoint, grantBytes);
        ++grantsSent_;
        std::printf("[admission] player=%s endpoint=%s players=%zu/%zu content=accept "
                    "grant=%zuB flags=%s event_mods=scrap x%.2f\n",
                    session.admission.playerId.c_str(), endpointKey.c_str(),
                    clients_.size(), kServerMaximumPlayers, grantBytes.size(),
                    grant.flags.empty() ? "(none)" : grant.flags.c_str(),
                    static_cast<double>(eventMods_.scrapMultiplier));
    }

    // 두 클라이언트가 **같은 2인 서브시뮬**을 굴려야 체크섬 대조가 성립한다.
    // 서버는 아바타를 하나도 조작하지 않으므로, 한쪽에서 온 입력을 다른 쪽에
    // 그대로 넘기는 것이 유일한 경로다.
    bool BuildAllowed(const wire::Command& command, int player) const {
        if (!options_.switches.avatarGate) return true;
        if (command.type != wire::CommandType::Place) return true;
        if (player < 0) return false;
        const math::Vec2 position{wire::DecodePosition(command.x),
                                  wire::DecodePosition(command.y)};
        return wire::BuildWithinReach(map_, avatars_.State().Cell(player), position);
    }

    void RelayAvatarInput(const std::string& fromKey, uint32_t baseFrame,
                          const std::vector<mp::Input>& inputs) {
        if (inputs.empty()) return;
        for (auto& [key, other] : clients_) {
            if (key == fromKey) continue;
            const net::Bytes packet = wire::WriteInputPacket(
                baseFrame, inputs.data(), inputs.size(),
                other.window.Header(other.seq.Next()));
            socket_.SendTo(other.endpoint, packet);
            ++avatarRelayPackets_;
        }
    }

    void BroadcastSnapshot() {
        for (auto& [key, client] : clients_) {
            (void)key;
            const uint16_t seq = client.seq.Next();
            const net::PacketHeader header = client.window.Header(seq);
            const net::Bytes snapshot =
                options_.switches.snapshotMode == SnapshotMode::Delta
                    ? wire::WriteSnapshotPacket(world_, tick_, client.commands.LastDelivered(),
                                                header, client.baselines, seq, nullptr,
                                                avatarFrame_, AvatarChecksum32())
                    : wire::WriteSnapshot(world_, tick_, client.commands.LastDelivered(),
                                          nullptr, header, avatarFrame_, AvatarChecksum32());
            if (!snapshot.empty()) {
                socket_.SendTo(client.endpoint, snapshot);
                snapshotBytes_ += snapshot.size();
                ++snapshotsSent_;
            }
        }
    }

    DefenseServerOptions options_;
    std::vector<uint8_t> secret_;
    GameWorld            world_;
    DefenseMap           map_;
    net::UdpServer       socket_;

    std::unordered_map<std::string, ClientSession> clients_;
    std::unordered_map<std::string, std::string>   tokenEndpoints_;
    std::unordered_map<std::string, std::string>   playerEndpoints_;
    std::string       activeMatch_;
    Clock::time_point started_{};
    Clock::time_point nextTick_{};
    uint64_t          rejectedPackets_ = 0;
    uint64_t          acceptedCommands_ = 0;
    uint64_t          snapshotsSent_ = 0;
    uint64_t          snapshotBytes_ = 0;
    uint64_t          avatarInputPackets_ = 0;
    uint64_t          avatarRelayPackets_ = 0;
    uint32_t          avatarFrame_ = 0;
    uint64_t          queueRequests_ = 0;
    uint64_t          queueResponses_ = 0;
    uint64_t          queueDuplicates_ = 0;
    uint64_t          matchesFormed_ = 0;
    uint64_t          buildRejectedOutOfReach_ = 0;
    uint64_t          contentRejects_ = 0;
    uint64_t          grantsSent_ = 0;
    uint64_t          contentHash_ = 0;
    int               cookedCount_ = 0;
    int               registeredAssets_ = 0;
    int64_t           matchStartUnix_ = 0;
    bool              externalFlags_ = false;
    bool              externalEvents_ = false;
    bool              flagsApplied_ = false;
    live::FeatureFlags   flags_;
    live::EventSchedule  events_;
    live::EventModifiers eventMods_;
    live::PatchManifest  patch_;
    tools::AssetPipeline pipeline_;
    mp::Rollback      avatars_;
    mp::Matchmaker    matchmaker_;
    mp::MatchRegistry matches_;
    std::unordered_map<uint32_t, net::UdpEndpoint> queueEndpoints_;
    uint32_t          tick_ = 0;
    bool              simulationStarted_ = false;
};

// 진입점이 부르는 단 하나의 함수.
inline int RunDefenseServer(const DefenseServerOptions& options) {
    // 이 서버의 진단은 전부 한국어다. 리다이렉트된 CI 로그를 한 인코딩으로
    // 모으는 문제이기도 하다.
    platform::EnableUtf8Console();
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
