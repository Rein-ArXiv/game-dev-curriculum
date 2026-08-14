// defense_app — 디펜스 제품의 프레임 루프 그 자체.
//
// 재설계안 A3: "games/…/main.cpp ≤ 150줄, 게임 로직 0줄. 프레임 루프는
// mega_core 의 GameApp::Step()." 이 파일이 그 Step 이다.
//
// 왜 옮겼나. 감사는 --selftest 가 main.cpp 에서 조기 반환해 에셋 로드 · 넷 루프 ·
// 복제 · 진행도를 전부 건너뛴다고 지적했다. 원인은 "게을러서"가 아니라 구조였다 —
// 프레임 루프가 main() 안에 있고 창 생성과 얽혀 있으면, 창 없이 그 루프를 돌릴
// 방법 자체가 없다. 그래서 테스트는 루프를 흉내 낸 다른 코드를 돌릴 수밖에 없고,
// 그 순간 테스트와 제품이 다른 코드를 보게 된다.
//
// 여기서는 프레임 하나가 DefenseApp::Step(FrameInput, dt) 하나다. 창 프런트엔드는
// platform::Window 에서 FrameInput 을 채워 넣고, 헤드리스 셀프테스트는 스크립트로
// 채워 넣는다. 둘은 입력을 만드는 방식만 다르고 그 뒤로는 완전히 같은 코드다.
//
// 계층: 이 헤더는 OS API 를 직접 부르지 않는다. 소켓은 net/, 창·오디오·GPU·경로는
// platform/, 파일은 io/ 가 소유하고 여기서는 그 인터페이스만 호출한다.

#pragma once

#include "engine/input.hpp"
#include "engine/particles.hpp"
#include "game/defense_content.hpp"
#include "game/defense_progression.hpp"
#include "game/defense_protocol.hpp"
#include "game/defense_view.hpp"
#include "game/game.hpp"
#include "live/analytics.hpp"
#include "net/reliable_channel.hpp"
#include "net/session.hpp"
#include "net/udp_transport.hpp"
#include "platform/audio_device.hpp"
#include "platform/paths.hpp"
#include "platform/vfs.hpp"
#include "platform/vk_present.hpp"
#include "platform/window.hpp"
#include "render/framebuffer.hpp"

#include <algorithm>
#include <array>
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

enum class RunMode { Solo, Host, Client };

inline constexpr float kDefenseFixedDt = 1.0f / 60.0f;
inline constexpr uint16_t kDefenseDefaultPort = 27015;

// ── 검사를 "건너뜀"으로 보고하는 종료 코드 ─────────────────────────
// CTest 는 `SKIP_RETURN_CODE` 로 지정한 종료 코드를 실패가 아니라 SKIP 으로 센다.
// 77 은 그 관례값(GNU automake)이라 다른 도구도 같은 뜻으로 읽는다.
//
// 왜 필요한가. `--gpu-selftest` 는 실제 Vulkan 장치와 스왑체인을 요구한다. GPU 도
// 드라이버도 없는 CI 러너에서 이것을 실패로 보고하면 게이트가 상시 빨간불이 되고,
// 상시 빨간 게이트는 곧 무시당한다. 반대로 등록하지 않으면 감사 #7·#24 가 지적한
// 상태 — 문서는 "자동 검증한다"고 적는데 ctest 에는 없는 상태 — 로 돌아간다.
// 그래서 "돌 수 있으면 돌고, 없으면 SKIP" 이 유일하게 정직한 세 번째 선택지다.
inline constexpr int kDefenseSkipExitCode = 77;

struct DefenseOptions {
    RunMode     mode = RunMode::Solo;
    bool        selftest = false;         // 창 없이 같은 루프를 돌린다
    int         selftestTicks = 18000;    // 5분 분량(60Hz). 승패가 나면 조기 종료
    std::string address = "127.0.0.1";
    std::string admissionToken;
    std::string profilePath;              // 비면 platform::UserDataDirectory 기본값
    uint16_t    port = kDefenseDefaultPort;
    bool        vulkan = false;
    bool        gpuSelftest = false;
};

inline const char* DefenseUsage() {
    return "usage: mega_defense [--selftest [ticks] | --gpu-selftest | --host [port] | "
           "--join <ipv4> [port] [--token TOKEN]] [--profile PATH] [--vulkan]\n";
}

inline bool ParseUint16(const char* text, uint16_t& value) {
    char* end = nullptr;
    long parsed = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || parsed < 1 || parsed > 65535) return false;
    value = static_cast<uint16_t>(parsed);
    return true;
}

inline bool ParseDefenseOptions(int argc, char** argv, DefenseOptions& options) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) {
            options.selftest = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                char* end = nullptr;
                long ticks = std::strtol(argv[++i], &end, 10);
                if (!end || *end != '\0' || ticks < 1 || ticks > 1000000) return false;
                options.selftestTicks = static_cast<int>(ticks);
            }
        } else if (std::strcmp(argv[i], "--vulkan") == 0) {
            options.vulkan = true;
        } else if (std::strcmp(argv[i], "--gpu-selftest") == 0) {
            options.vulkan = true;
            options.gpuSelftest = true;
        } else if (std::strcmp(argv[i], "--host") == 0) {
            options.mode = RunMode::Host;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                if (!ParseUint16(argv[++i], options.port)) return false;
            }
        } else if (std::strcmp(argv[i], "--join") == 0) {
            if (i + 1 >= argc) return false;
            options.mode = RunMode::Client;
            options.address = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                if (!ParseUint16(argv[++i], options.port)) return false;
            }
        } else if (std::strcmp(argv[i], "--token") == 0) {
            if (i + 1 >= argc) return false;
            options.admissionToken = argv[++i];
        } else if (std::strcmp(argv[i], "--profile") == 0) {
            if (i + 1 >= argc) return false;
            options.profilePath = argv[++i];
        } else {
            return false;
        }
    }
    return true;
}

// ── 프레임 입력 ────────────────────────────────────────────────────
// 창이든 스크립트든 프레임 루프에 들어가는 것은 이 구조체 하나다. 이것이
// "헤드리스가 제품의 진짜 루프를 돌린다"를 가능하게 하는 이음매다.
inline constexpr std::array<int, 9> kDefenseActionKeys{
    '1', '2', '3', '4', 'P', 'U', 'X', 'T', 'R'};

struct FrameInput {
    std::array<bool, kDefenseActionKeys.size()> keyDown{};
    int  mouseX = -100, mouseY = -100;   // 맵 밖 = 배치 프리뷰 없음
    bool mouseClicked = false;           // 이번 프레임의 누름 엣지

    void Press(int virtualKey) {
        for (size_t i = 0; i < kDefenseActionKeys.size(); ++i)
            if (kDefenseActionKeys[i] == virtualKey) keyDown[i] = true;
    }
};

class DefenseApp {
public:
    using Clock = std::chrono::steady_clock;

    DefenseApp() = default;
    DefenseApp(const DefenseApp&) = delete;
    DefenseApp& operator=(const DefenseApp&) = delete;

    // 콘텐츠 · 네트워크 · 에셋 · 진행도 저장소를 연다. 창은 열지 않는다 —
    // 헤드리스와 창 실행이 여기까지 완전히 같은 경로를 지나게 하려는 것이다.
    bool Init(const DefenseOptions& options) {
        options_ = options;

        const std::string executableDirectory = platform::ExecutableDirectory();
        vfs_.Mount("assets", executableDirectory + "/assets");
        const std::string assetRoot = vfs_.Resolve("assets");

        map_ = defense::LoadMapFile(vfs_.Resolve("assets/level.txt"), &externalMap_);
        // 밸런스 스크립트가 거부되면 기본 구성으로 계속 진행한다(멈추지 않는다).
        // 다만 그 사실을 말한다 — 실행 예산 초과·구문 오류가 조용히 지나가면
        // 파일을 교체한 사람은 자기 값이 적용된 줄로 안다.
        std::string balanceError;
        balance_ = defense::LoadBalanceScript(vfs_.Resolve("assets/balance.script"),
                                              &externalBalance_, &balanceError);
        if (!balanceError.empty())
            std::fprintf(stderr, "[content] balance.script 거부, 기본 구성으로 진행: %s\n",
                         balanceError.c_str());
        waves_ = defense::ApplyBalance(
            defense::LoadWaveFile(vfs_.Resolve("assets/waves.txt"),
                                  defense::DefaultWaves(), &externalWaves_),
            balance_);
        world_.Init(map_, waves_, balance_.startGold, balance_.coreHp);

        if (options_.mode == RunMode::Host && !network_.OpenHost(options_.port)) {
            std::fprintf(stderr, "host socket failed: %s\n", network_.LastError().c_str());
            return false;
        }
        if (options_.mode == RunMode::Client &&
            !network_.OpenClient(options_.address, options_.port)) {
            std::fprintf(stderr, "client socket failed: %s\n", network_.LastError().c_str());
            return false;
        }

        framebuffer_.Init(kViewWidth, kViewHeight);
        skin_.Load(assetRoot);

        for (int key : kDefenseActionKeys) input_.Bind(key, ActionForKey(key));

        // 클라이언트는 권위 진행도를 갖지 않으므로 저장소를 열지 않는다(기존 동작).
        if (options_.mode != RunMode::Client) {
            std::string path = options_.profilePath;
            if (path.empty()) {
                const std::string directory = platform::UserDataDirectory("MegaDefense");
                path = (directory.empty() ? executableDirectory : directory) + "/profile.wal";
            }
            if (progression_.Open(path)) progression_.Load(profile_);
        }

        std::printf("[assets] loaded %d/10 external sprites from %s\n",
                    skin_.externalAssets, assetRoot.c_str());
        std::printf("[content] map=%s waves=%s (%zu) balance=%s\n",
                    externalMap_ ? "external" : "fallback",
                    externalWaves_ ? "external" : "fallback", waves_.size(),
                    externalBalance_ ? "script" : "fallback");
        if (options_.mode == RunMode::Host)
            std::printf("[network] hosting UDP port %u\n", network_.LocalPort());
        if (options_.mode == RunMode::Client)
            std::printf("[network] joining %s:%u\n", options_.address.c_str(), options_.port);
        // 소켓이 열렸다는 사실은 다른 프로세스가 관측할 수 있어야 한다. 표준
        // 출력이 파일로 리다이렉트되면 기본이 전량 버퍼링이라 이 줄은 프로세스가
        // 끝날 때까지 나오지 않는다 — 그러면 협동 테스트는 "호스트가 포트를 잡았는가"를
        // 고정 sleep 으로 추측할 수밖에 없고, 그 추측이 플래키의 출처가 된다.
        std::fflush(stdout);
        return true;
    }

    // 오디오 장치는 창 실행에서만 연다. 헤드리스에서 열지 않는 것은 코드 분기가
    // 아니라 장치 부재이고, Play() 는 열리지 않은 장치에서 조용한 no-op 이다.
    void OpenAudio() { sound_.Init(); }

    // ── 프레임 하나 ────────────────────────────────────────────────
    // 창 프런트엔드와 헤드리스 셀프테스트가 부르는 유일한 함수. 표시(Present)와
    // 프레임 페이싱만 바깥에 있다.
    void Step(const FrameInput& frame, double frameSeconds) {
        input_.BeginFrame();
        for (size_t i = 0; i < kDefenseActionKeys.size(); ++i)
            input_.OnKey(kDefenseActionKeys[i], frame.keyDown[i]);

        const Clock::time_point now = Clock::now();
        frameSeconds = std::clamp(frameSeconds, 0.0, 0.25);
        accumulator_ += frameSeconds;

        if (options_.mode == RunMode::Host) PumpHost();
        else if (options_.mode == RunMode::Client) PumpClient(now, frameSeconds);

        if (input_.WasPressed("tower_cannon")) selectedType_ = TowerType::Cannon;
        if (input_.WasPressed("tower_gatling")) selectedType_ = TowerType::Gatling;
        if (input_.WasPressed("tower_splash")) selectedType_ = TowerType::Splash;
        if (input_.WasPressed("tower_sniper")) selectedType_ = TowerType::Sniper;
        if (options_.mode == RunMode::Solo && input_.WasPressed("pause"))
            paused_ = !paused_;

        view_ = options_.mode == RunMode::Client ? replicas_.Render(wire::Lerp)
                                                 : CaptureView(world_, hostTick_);
        const math::Vec2 mouse{static_cast<float>(frame.mouseX),
                               static_cast<float>(frame.mouseY)};

        if (frame.mouseClicked && frame.mouseY >= map_.OriginY()) {
            if (const auto* tower = TowerAt(view_, mouse, map_.CellSize() * 0.45f)) {
                selectedTower_ = tower->id;
            } else {
                selectedTower_ = 0;
                Submit(wire::CommandType::Place, mouse, static_cast<uint8_t>(selectedType_));
            }
        }

        if (const auto* selected = wire::FindById(view_.towers, selectedTower_)) {
            if (input_.WasPressed("upgrade"))
                Submit(wire::CommandType::Upgrade, selected->pos, 0);
            if (input_.WasPressed("sell")) {
                Submit(wire::CommandType::Sell, selected->pos, 0);
                selectedTower_ = 0;
            }
            if (input_.WasPressed("target_mode")) {
                uint8_t next = (static_cast<uint8_t>(selected->targetMode) + 1) % 3;
                Submit(wire::CommandType::Target, selected->pos, next);
            }
        } else {
            selectedTower_ = 0;
        }

        if (options_.mode != RunMode::Client) {
            if ((world_.Won() || world_.Lost()) && input_.WasPressed("restart")) Restart();
            int catchUpSteps = 0;
            if (paused_) accumulator_ = 0.0;
            while (!paused_ && accumulator_ >= kDefenseFixedDt && catchUpSteps < 5) {
                if (!world_.Won() && !world_.Lost()) world_.Step(kDefenseFixedDt);
                ++hostTick_;
                accumulator_ -= kDefenseFixedDt;
                ++catchUpSteps;
                if (options_.mode == RunMode::Host && network_.PeerKnown() && hostTick_ % 3 == 0)
                    SendSnapshot();
            }
            if (catchUpSteps == 5 && accumulator_ >= kDefenseFixedDt) accumulator_ = 0.0;
            view_ = CaptureView(world_, hostTick_);
        }

        connected_ = options_.mode == RunMode::Solo ||
                     (options_.mode == RunMode::Host && network_.PeerKnown()) ||
                     (options_.mode == RunMode::Client && replicas_.Ready() &&
                      now - lastSnapshot_ < std::chrono::seconds(2));
        if (options_.mode == RunMode::Client && view_.towers.size() > previousTowerCount_)
            sound_.Play(platform::Sfx::Pickup);
        previousTowerCount_ = view_.towers.size();
        const bool ended = view_.won || view_.lost;
        if (ended && !previousEnd_)
            sound_.Play(view_.won ? platform::Sfx::Win : platform::Sfx::Death);
        previousEnd_ = ended;
        if (options_.mode != RunMode::Client && ended && !recordedMatch_) {
            const int score = ComputeScore(world_.Wave(), world_.Gold(), world_.Won());
            if (progression_.Record(static_cast<uint32_t>(world_.Wave()),
                                    static_cast<uint32_t>(std::max(score, 0)),
                                    world_.Won(), hostTick_, profile_)) {
                metrics_.GetCounter("matches_finished").Add();
                recordedMatch_ = true;
            }
        }

        UpdateImpactParticles(view_, previousProjectiles_, particles_,
                              static_cast<float>(frameSeconds));
        HudState hud;
        hud.selectedType = selectedType_;
        hud.selectedTower = selectedTower_;
        hud.mouseX = frame.mouseX;
        hud.mouseY = frame.mouseY;
        hud.modeLabel = ModeLabel();
        hud.paused = paused_;
        DrawGame(framebuffer_, map_, view_, skin_, hud, connected_, &particles_);
    }

    // 히스토그램의 이름·버킷 설정은 여기 한 번만 적는다. 기록하는 곳과 읽는 곳에
    // 같은 리터럴을 두 번 쓰면 한쪽만 바뀌었을 때 조용히 다른 히스토그램이 생긴다.
    live::Histogram& FrameHistogram() {
        return metrics_.GetHistogram("frame_ms", 50.0, 50);
    }
    live::Histogram& SnapshotBytesHistogram() {
        return metrics_.GetHistogram("snapshot_bytes",
                                     static_cast<double>(wire::kMaxPacketBytes), 24);
    }

    void RecordFrameMs(double milliseconds) { FrameHistogram().Record(milliseconds); }

    // 한 줄에 양쪽 방향이 다 들어간다. 호스트가 받은 것(remote_commands)과
    // 클라이언트가 보낸 것(commands_sent)이 같은 형식으로 찍히므로, 두 프로세스의
    // 표준 출력만으로 "어느 쪽 배선이 끊겼는가"를 답할 수 있다.
    //
    // snapshot_bytes 는 감사 #30 까지 **수집만 하고 출력하지 않았다** — 관측할 수
    // 없는 지표는 없는 지표와 같다. p95 와 최댓값을 함께 찍는 이유는 둘이 다른
    // 질문에 답하기 때문이다: p95 는 평소 대역폭, 최댓값은 1200바이트 예산까지의
    // 여유. truncated 는 그 예산이 실제로 부족했던 횟수다(감사 #31).
    void PrintMetrics() {
        live::Histogram& bytes = SnapshotBytesHistogram();
        std::printf("[metrics] matches=%llu remote_commands=%llu snapshots=%llu "
                    "commands_sent=%llu command_ack=%u frame_p95=%.1fms "
                    "snapshot_bytes_p95=%.0f snapshot_bytes_max=%.0f "
                    "snapshots_truncated=%llu\n",
                    static_cast<unsigned long long>(metrics_.GetCounter("matches_finished").Value()),
                    static_cast<unsigned long long>(metrics_.GetCounter("remote_commands").Value()),
                    static_cast<unsigned long long>(metrics_.GetCounter("snapshots_sent").Value()),
                    static_cast<unsigned long long>(commandPacketsSent_),
                    acknowledgedCommand_,
                    FrameHistogram().Percentile(0.95),
                    bytes.Percentile(0.95), bytes.Max(),
                    static_cast<unsigned long long>(
                        metrics_.GetCounter("snapshots_truncated").Value()));
        std::fflush(stdout);
    }

    // ── 조회(창 프런트엔드 · 헤드리스 검증 공용) ────────────────────
    const render::Framebuffer& Surface() const { return framebuffer_; }
    const GameWorld& World() const { return world_; }
    const DefenseMap& Map() const { return map_; }
    const wire::Snapshot& View() const { return view_; }
    const DefenseSkin& Skin() const { return skin_; }
    const defense::BalanceConfig& Balance() const { return balance_; }
    uint32_t HostTick() const { return hostTick_; }
    bool Connected() const { return connected_; }
    bool Finished() const { return view_.won || view_.lost; }
    bool ExternalMap() const { return externalMap_; }
    bool ExternalWaves() const { return externalWaves_; }
    bool ExternalBalance() const { return externalBalance_; }
    const DefenseOptions& Options() const { return options_; }
    live::Registry& Metrics() { return metrics_; }

    // ── 클라이언트 배선 관측 ────────────────────────────────────────
    // 호스트 쪽은 remote_commands · snapshots_sent 카운터로 이미 관측 가능했고,
    // 그래서 호스트 배선을 무력화하는 변이는 잡혔다. 클라이언트 쪽에는 대응하는
    // 관측점이 없어 "명령이 한 번도 나가지 않는" 상태와 정상 상태가 밖에서
    // 구분되지 않았다. 아래 넷이 그 구분을 만든다. 게임플레이는 읽지 않는다.
    uint64_t HelloPacketsSent() const { return helloPacketsSent_; }
    uint64_t CommandPacketsSent() const { return commandPacketsSent_; }
    uint64_t CommandsQueued() const { return commandsQueued_; }
    uint64_t SnapshotsReceived() const { return snapshotsReceived_; }
    // 호스트가 순서대로 반영했다고 통지한 마지막 명령 번호(0 = 아직 하나도 없음).
    uint32_t AcknowledgedCommand() const { return acknowledgedCommand_; }
    size_t   UnackedCommands() const { return localCommands_.UnackedCount(); }

private:
    static const char* ActionForKey(int key) {
        switch (key) {
            case '1': return "tower_cannon";
            case '2': return "tower_gatling";
            case '3': return "tower_splash";
            case '4': return "tower_sniper";
            case 'P': return "pause";
            case 'U': return "upgrade";
            case 'X': return "sell";
            case 'T': return "target_mode";
            default:  return "restart";
        }
    }

    const char* ModeLabel() const {
        if (options_.mode == RunMode::Solo) return "SOLO";
        if (options_.mode == RunMode::Host) return connected_ ? "HOST 2P" : "HOST WAIT";
        return connected_ ? "CLIENT 2P" : "CONNECTING";
    }

    void Restart() {
        world_.Init(map_, waves_, balance_.startGold, balance_.coreHp);
        selectedTower_ = 0;
        previousEnd_ = false;
        recordedMatch_ = false;
        paused_ = false;
        particles_.Clear();
        previousProjectiles_.clear();
    }

    void Submit(wire::CommandType type, const math::Vec2& position, uint8_t argument) {
        wire::Command command;
        command.type = type;
        command.x = wire::EncodePosition(position.x);
        command.y = wire::EncodePosition(position.y);
        command.argument = argument;
        if (options_.mode == RunMode::Client) {
            localCommands_.Queue(command);        // 시퀀스는 채널이 부여한다
            ++commandsQueued_;
        } else if (wire::ApplyCommand(world_, command)) {
            sound_.Play(platform::Sfx::Pickup);
        }
    }

    void PumpHost() {
        net::Bytes packet;
        while (network_.Receive(packet)) {
            if (wire::ReadHello(packet)) {
                network_.AcceptLastSender();
                continue;
            }
            wire::Command command;
            if (!wire::ReadCommand(packet, command)) continue;
            remoteCommands_.Accept(command.sequence, command);
        }
        wire::Command command;
        while (remoteCommands_.Poll(command)) {   // 순서대로, 정확히 한 번
            wire::ApplyCommand(world_, command);
            metrics_.GetCounter("remote_commands").Add();
        }
    }

    void PumpClient(Clock::time_point now, double frameSeconds) {
        if (now - lastHello_ >= std::chrono::milliseconds(500)) {
            network_.Send(options_.admissionToken.empty()
                ? wire::WriteHello()
                : wire::WriteAdmissionHello(options_.admissionToken));
            lastHello_ = now;
            ++helloPacketsSent_;
        }
        if (!localCommands_.Empty() &&
            now - lastResend_ >= std::chrono::milliseconds(100)) {
            localCommands_.ForEachUnacked(
                [&](uint32_t sequence, const wire::Command& queued) {
                    wire::Command outgoing = queued;
                    outgoing.sequence = sequence;
                    network_.Send(wire::WriteCommand(outgoing));
                    // 관측 지점. 여기가 클라이언트에서 명령 바이트가 나가는 유일한
                    // 곳이므로, 이 숫자가 0 이면 협동 클라이언트는 벙어리다.
                    // 게이트가 없던 시절 그 상태가 53개 테스트를 전부 통과했다.
                    ++commandPacketsSent_;
                });
            lastResend_ = now;
        }
        net::Bytes packet;
        while (network_.Receive(packet)) {
            wire::Snapshot snapshot;
            if (!wire::ReadSnapshot(packet, snapshot)) continue;
            // 와이어 flags 의 truncated 를 읽는 유일한 제품 코드다. 켜져 있으면
            // 이 클라이언트가 그리는 세계에 호스트가 버린 엔티티가 빠져 있다는
            // 뜻이므로, 화면과 지표가 그 사실을 알고 있어야 한다.
            const bool truncated = snapshot.truncated;
            if (replicas_.Add(std::move(snapshot))) {
                lastSnapshot_ = now;
                ++snapshotsReceived_;
                if (truncated) metrics_.GetCounter("snapshots_truncated").Add();
            }
        }
        const wire::Snapshot* newest = replicas_.Newest();
        if (newest && newest->acknowledgedCommand > acknowledgedCommand_)
            acknowledgedCommand_ = newest->acknowledgedCommand;
        localCommands_.AckThrough(newest ? newest->acknowledgedCommand : 0);
        replicas_.Advance(frameSeconds * 60.0);
    }

    void SendSnapshot() {
        // 잘림은 조용히 일어나면 안 되는 손실이다 — 엔티티가 클라이언트 화면에서
        // 그냥 사라지고 아무 신호도 남지 않는다. 지금은 컬렉션 상한이 게임플레이
        // 상한보다 크거나 같아 이 값이 켜지지 않지만, 그 관계가 깨지는 순간(예:
        // 웨이브당 적 상한 상향) 이 카운터가 먼저 말한다.
        bool truncated = false;
        net::Bytes snapshot = wire::WriteSnapshot(
            world_, hostTick_, remoteCommands_.LastDelivered(), &truncated);
        if (snapshot.empty()) return;
        network_.Send(snapshot);
        metrics_.GetCounter("snapshots_sent").Add();
        if (truncated) metrics_.GetCounter("snapshots_truncated").Add();
        SnapshotBytesHistogram().Record(static_cast<double>(snapshot.size()));
    }

    DefenseOptions            options_;
    platform::Vfs             vfs_;
    DefenseMap                map_;
    defense::BalanceConfig    balance_;
    std::vector<WaveDef>      waves_;
    GameWorld                 world_;
    net::UdpTransport         network_;
    render::Framebuffer       framebuffer_;
    DefenseSkin               skin_;
    platform::AudioDevice     sound_;
    engine::InputMap          input_;
    engine::ParticleSystem    particles_;
    live::Registry            metrics_;
    defense::ProgressionStore progression_;
    defense::Profile          profile_;

    // 신뢰·순서 보장은 net/reliable_channel.hpp 의 코어가 갖는다. 이 프로토콜은
    // 시퀀스를 명령 패킷에, 누적 ack 를 스냅샷의 acknowledgedCommand 에 실으므로
    // 프레이밍 없이 코어만 쓴다. 첫 명령 번호는 1.
    net::ReliableOrderedReceiver<wire::Command> remoteCommands_{1};
    net::ReliableOrderedSender<wire::Command>   localCommands_{1};
    // 복제 뷰: 스냅샷 8개 보관, 새 타임라인에서 최신보다 3틱 뒤에서 시작, 렌더
    // 시계는 최신에 2틱보다 가까이 붙지 않는다(호스트는 3틱마다 보낸다).
    wire::SnapshotView replicas_{net::InterpolationClock{3.0, 2.0, 8}};

    std::unordered_map<uint32_t, math::Vec2> previousProjectiles_;
    wire::Snapshot     view_;
    Clock::time_point  lastHello_{};
    Clock::time_point  lastResend_{};
    Clock::time_point  lastSnapshot_{};
    double             accumulator_ = 0.0;
    uint64_t           helloPacketsSent_ = 0;
    uint64_t           commandPacketsSent_ = 0;
    uint64_t           commandsQueued_ = 0;
    uint64_t           snapshotsReceived_ = 0;
    uint32_t           acknowledgedCommand_ = 0;
    uint32_t           hostTick_ = 0;
    uint32_t           selectedTower_ = 0;
    TowerType          selectedType_ = TowerType::Cannon;
    size_t             previousTowerCount_ = 0;
    bool               previousEnd_ = false;
    bool               paused_ = false;
    bool               recordedMatch_ = false;
    bool               connected_ = false;
    bool               externalMap_ = false;
    bool               externalWaves_ = false;
    bool               externalBalance_ = false;
};

// ── 창 실행 ────────────────────────────────────────────────────────
// GPU 셀프테스트의 실패와 부재를 가른다. 요구 조건(Vulkan 빌드 · 창 · 장치)이
// **없어서** 못 도는 것은 SKIP 이고, 다 갖췄는데 present 가 깨지는 것만 실패다.
// 그 구분이 없으면 이 검사는 GPU 없는 CI 에서 영구 실패로 남아 무시당한다.
inline int DefenseGpuSelftestUnavailable(const char* reason) {
    std::fprintf(stderr, "[gpu-selftest] SKIP: %s\n", reason);
    std::fflush(stderr);
    return kDefenseSkipExitCode;
}

inline int RunDefenseWindowed(DefenseApp& app, const DefenseOptions& options) {
    using Clock = DefenseApp::Clock;

#ifndef MEGA_HAS_VULKAN
    // 창을 열기 전에 답이 나오는 조건이다. 창부터 만들면 GPU 가 없는 환경에서
    // "창 생성 실패"라는 엉뚱한 이유가 먼저 보고된다.
    if (options.vulkan) {
        if (options.gpuSelftest)
            return DefenseGpuSelftestUnavailable("이 빌드에 Vulkan SDK 지원이 없다");
        std::fprintf(stderr, "this build has no Vulkan SDK support\n");
        return 1;
    }
#endif

    platform::Window window;
    const wchar_t* title = options.mode == RunMode::Solo ? L"Mega Defense" :
                           options.mode == RunMode::Host ? L"Mega Defense - Host" :
                                                           L"Mega Defense - Client";
    if (!window.Create(title, kViewWidth, kViewHeight)) {
        if (options.gpuSelftest)
            return DefenseGpuSelftestUnavailable("이 환경에 창 백엔드가 없다");
        std::fprintf(stderr,
                     "window creation failed (this platform has no window backend; use --selftest)\n");
        return 2;
    }

#ifdef MEGA_HAS_VULKAN
    platform::VkPresenter presenter;
    if (options.vulkan &&
        !presenter.Init(window.NativeHandle(), kViewWidth, kViewHeight,
                        platform::ExecutableDirectory())) {
        if (options.gpuSelftest)
            return DefenseGpuSelftestUnavailable(
                "Vulkan 장치·스왑체인·셰이더를 준비하지 못했다(드라이버 또는 SPIR-V 부재)");
        std::fprintf(stderr, "Vulkan presenter initialization failed\n");
        return 1;
    }
#endif

    app.OpenAudio();

#ifdef MEGA_HAS_VULKAN
    if (options.gpuSelftest) {
        for (int frame = 0; frame < 5; ++frame) {
            window.PumpAndContinue();
            app.Step(FrameInput{}, kDefenseFixedDt);
            presenter.Present(app.Surface().Data());
        }
        presenter.Destroy();
        std::printf("[gpu-selftest] Vulkan present 5 frames: OK\n");
        return 0;
    }
#endif

    Clock::time_point previousFrame = Clock::now();
    Clock::time_point nextFrame = previousFrame;
    while (window.PumpAndContinue()) {
        FrameInput frame;
        for (size_t i = 0; i < kDefenseActionKeys.size(); ++i)
            frame.keyDown[i] = window.Key(kDefenseActionKeys[i]);
        frame.mouseX = window.MouseX();
        frame.mouseY = window.MouseY();
        frame.mouseClicked = window.MouseClicked();

        const Clock::time_point now = Clock::now();
        const double frameSeconds =
            std::chrono::duration<double>(now - previousFrame).count();
        previousFrame = now;

        app.Step(frame, frameSeconds);

#ifdef MEGA_HAS_VULKAN
        if (options.vulkan) presenter.Present(app.Surface().Data());
        else window.Present(app.Surface().Data(), kViewWidth, kViewHeight);
#else
        window.Present(app.Surface().Data(), kViewWidth, kViewHeight);
#endif
        nextFrame += std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(kDefenseFixedDt));
        if (nextFrame > Clock::now()) std::this_thread::sleep_until(nextFrame);
        else nextFrame = Clock::now();
        app.RecordFrameMs(std::chrono::duration<double, std::milli>(Clock::now() - now).count());
    }
#ifdef MEGA_HAS_VULKAN
    if (options.vulkan) presenter.Destroy();
#endif
    app.PrintMetrics();
    return 0;
}

// ── 헤드리스 실행 ──────────────────────────────────────────────────
// 창만 없다. 위 창 루프와 **같은 DefenseApp::Step** 을 돌린다 — 에셋 로드,
// 명령 경로, 고정 스텝 시뮬레이션, 파티클, 소프트웨어 렌더, 진행도 기록이
// 전부 실제로 실행된다. 입력은 창 대신 아래 스크립트가 만든다.
//
// 스크립트: 계획된 셀을 순서대로 클릭해 타워를 세우고(골드가 모일 때까지 같은
// 셀을 계속 시도한다 — 실제 플레이와 같은 경로), 다 세우면 타워를 하나씩 선택해
// 'U'로 강화한다. 즉 배치·선택·강화 명령이 전부 wire::ApplyCommand 로 흐른다.
struct SelftestReport {
    int  ticks = 0;
    int  towers = 0;
    int  wave = 0;
    bool won = false;
    bool lost = false;
    bool protocolOk = false;
    bool renderedPixels = false;
    size_t packetBytes = 0;

    // ── 클라이언트 전용 ────────────────────────────────────────────
    // 클라이언트는 권위 월드를 소유하지 않는다. "이겼는가"로는 성공을 물을 수
    // 없고, 그래서 클라이언트 셀프테스트는 여태 판정 기준이 없어 항상 1 을
    // 반환했다 — 협동 클라이언트를 실행하는 어떤 자동 검사도 결과를 읽을 수
    // 없었다는 뜻이다. 대신 왕복이 실제로 일어났는가를 묻는다.
    bool     connected = false;          // 살아 있는 스냅샷 스트림을 본 적이 있다
    uint64_t snapshotsReceived = 0;      // 호스트 → 클라이언트
    uint64_t commandPacketsSent = 0;     // 클라이언트 → 호스트(실제로 나간 패킷)
    uint64_t commandsQueued = 0;         // 입력이 만든 명령 수
    uint32_t commandAck = 0;             // 호스트가 반영했다고 통지한 마지막 번호
};

// 클라이언트 셀프테스트의 성공 기준. 네 가지가 전부 참이어야 협동 클라이언트가
// 제 몫을 한 것이다: 붙었고(connected), 상태를 받았고(snapshots), 명령을 실제로
// 내보냈고(commandPacketsSent), 호스트가 그 명령을 순서대로 반영했다(commandAck).
inline bool ClientRoundTripProven(const DefenseApp& app) {
    return app.Connected() && app.SnapshotsReceived() > 0 &&
           app.CommandPacketsSent() > 0 && app.AcknowledgedCommand() > 0;
}

inline SelftestReport RunDefenseHeadless(DefenseApp& app, int maxTicks) {
    static constexpr std::array<DefenseMap::Cell, 12> kPlan{{
        {6, 1}, {8, 2}, {10, 2}, {11, 4}, {13, 5}, {10, 6},
        {8, 8}, {5, 8}, {3, 9}, {5, 10}, {6, 12}, {9, 13},
    }};

    SelftestReport report;
    const DefenseMap& map = app.Map();
    size_t planCursor = 0;
    int    placedTowers = 0;
    size_t upgradeCursor = 0;
    bool   selecting = true;

    for (report.ticks = 0; report.ticks < maxTicks; ++report.ticks) {
        FrameInput frame;

        if (planCursor < kPlan.size()) {
            // 계획한 종류를 고르고(숫자 키) 그 셀을 클릭한다. 골드가 모자라면
            // 호스트가 명령을 거절하고 타워 수가 늘지 않으므로 다음 틱에 다시 시도한다.
            frame.Press(static_cast<int>('1' + (planCursor % 4)));
            const math::Vec2 target = map.CellCenter(kPlan[planCursor].x, kPlan[planCursor].y);
            frame.mouseX = static_cast<int>(target.x);
            frame.mouseY = static_cast<int>(target.y);
            frame.mouseClicked = true;
        } else if (!app.View().towers.empty()) {
            // 남는 골드는 강화에 쓴다. 한 틱은 선택(클릭), 다음 틱은 'U'.
            const auto& towers = app.View().towers;
            const auto& tower = towers[upgradeCursor % towers.size()];
            frame.mouseX = static_cast<int>(tower.pos.x);
            frame.mouseY = static_cast<int>(tower.pos.y);
            if (selecting) {
                frame.mouseClicked = true;
            } else {
                frame.Press('U');
                ++upgradeCursor;
            }
            selecting = !selecting;
        }

        app.Step(frame, static_cast<double>(kDefenseFixedDt));

        const int towers = app.World().TowerCount();
        if (planCursor < kPlan.size() && towers > placedTowers) {
            placedTowers = towers;
            ++planCursor;
        }
        if (app.Connected()) report.connected = true;
        if (app.Finished()) { ++report.ticks; break; }
        // 정지 조건은 성공 조건과 같은 모양이어야 한다. 솔로·호스트는 이길 때까지
        // 돌고(Finished), 클라이언트는 왕복이 증명될 때까지 돈다.
        if (app.Options().mode == RunMode::Client && ClientRoundTripProven(app)) {
            ++report.ticks;
            break;
        }
    }

    // 실제로 돌린 월드를 그대로 전송 포맷으로 왕복시킨다.
    const net::Bytes encoded =
        wire::WriteSnapshot(app.World(), app.HostTick(), 0);
    wire::Snapshot decoded;
    report.protocolOk = !encoded.empty() && wire::ReadSnapshot(encoded, decoded);
    report.packetBytes = encoded.size();

    // 렌더가 실제로 일어났는지: 프레임버퍼에 배경 클리어 색 말고 다른 픽셀이 있는가.
    const render::Framebuffer& surface = app.Surface();
    const uint32_t background = render::Pack({14, 18, 23, 255});
    const uint32_t* pixels = surface.Data();
    const size_t pixelCount = static_cast<size_t>(surface.Width()) * surface.Height();
    for (size_t i = 0; i < pixelCount; ++i) {
        if (pixels[i] != background) { report.renderedPixels = true; break; }
    }

    report.snapshotsReceived = app.SnapshotsReceived();
    report.commandPacketsSent = app.CommandPacketsSent();
    report.commandsQueued = app.CommandsQueued();
    report.commandAck = app.AcknowledgedCommand();

    if (app.Options().mode == RunMode::Client) {
        // 클라이언트가 아는 세계는 복제된 뷰뿐이다. 스텝된 적 없는 로컬 월드를
        // 읽으면 언제나 0 이 나와 실패와 구분되지 않는다.
        report.towers = static_cast<int>(app.View().towers.size());
        report.wave = static_cast<int>(app.View().wave);
        report.won = app.View().won;
        report.lost = app.View().lost;
        return report;
    }

    report.towers = app.World().TowerCount();
    report.wave = app.World().Wave();
    report.won = app.World().Won();
    report.lost = app.World().Lost();
    return report;
}

inline int RunDefenseSelftest(DefenseApp& app, const DefenseOptions& options) {
    const SelftestReport report = RunDefenseHeadless(app, options.selftestTicks);
    const bool contentOk = app.Skin().externalAssets == 10 && app.ExternalMap() &&
                           app.ExternalWaves() && app.ExternalBalance();
    const char* content = (app.ExternalMap() && app.ExternalWaves() &&
                           app.ExternalBalance()) ? "external" : "fallback";

    if (options.mode == RunMode::Client) {
        // LINKED = 명령이 나갔고 호스트가 그것을 반영했다. NO-ACK = 상태는 받는데
        // 내 명령이 반영되지 않는다. NO-LINK = 아예 붙지 못했다. 세 상태를 구분해야
        // 실패가 어느 방향의 배선인지 밖에서 읽을 수 있다.
        const char* result = report.commandAck > 0
            ? "LINKED"
            : (report.snapshotsReceived > 0 ? "NO-ACK" : "NO-LINK");
        std::printf("[selftest] result=%s ticks=%d snapshots=%llu commands_queued=%llu "
                    "commands_sent=%llu ack=%u towers=%d wave=%d render=%s "
                    "assets=%d/10 content=%s\n",
                    result, report.ticks,
                    static_cast<unsigned long long>(report.snapshotsReceived),
                    static_cast<unsigned long long>(report.commandsQueued),
                    static_cast<unsigned long long>(report.commandPacketsSent),
                    report.commandAck, report.towers, report.wave,
                    report.renderedPixels ? "OK" : "BLANK",
                    app.Skin().externalAssets, content);
        app.PrintMetrics();

        const bool ok = report.connected && report.snapshotsReceived > 0 &&
                        report.commandPacketsSent > 0 && report.commandAck > 0 &&
                        report.renderedPixels && report.protocolOk && contentOk;
        return ok ? 0 : 1;
    }

    std::printf("[selftest] result=%s ticks=%d towers=%d wave=%d packet=%zu "
                "protocol=%s render=%s assets=%d/10 content=%s\n",
                report.won ? "CLEAR" : (report.lost ? "LOST" : "TIMEOUT"),
                report.ticks, report.towers, report.wave, report.packetBytes,
                report.protocolOk ? "OK" : "FAIL",
                report.renderedPixels ? "OK" : "BLANK",
                app.Skin().externalAssets, content);
    app.PrintMetrics();

    // 이 단언들은 전부 "제품의 진짜 프레임 루프가 돌았는가"를 묻는다.
    const bool ok = report.won && report.protocolOk && report.renderedPixels &&
                    report.towers > 0 && contentOk;
    return ok ? 0 : 1;
}

// 진입점이 부르는 단 하나의 함수. games/ 에는 이 호출과 인자 파싱만 남는다.
inline int RunDefense(const DefenseOptions& options) {
    DefenseOptions effective = options;
    if (effective.selftest && effective.profilePath.empty()) {
        // 셀프테스트도 진짜 ProgressionStore 에 진짜 파일로 커밋한다. 다만
        // 플레이어의 프로필을 덮지 않도록 실행 파일 옆의 별도 파일을 쓴다.
        // 코드 경로가 아니라 데이터 경로만 다르다.
        effective.profilePath = platform::ExecutableDirectory() + "/selftest_profile.wal";
    }

    DefenseApp app;
    if (!app.Init(effective)) return 1;
    if (effective.selftest) return RunDefenseSelftest(app, effective);
    return RunDefenseWindowed(app, effective);
}

}  // namespace game
}  // namespace mega
