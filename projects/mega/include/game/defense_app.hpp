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

#include "containers/hash_map.h"
#include "engine/input.hpp"
#include "engine/particles.hpp"
#include "game/asset_reload.hpp"
#include "game/defense_content.hpp"
#include "game/defense_progression.hpp"
#include "game/defense_protocol.hpp"
#include "game/defense_view.hpp"
#include "game/game.hpp"
#include "game/replay.hpp"
#include "game/sfx_bus.hpp"
#include "live/analytics.hpp"
#include "live/economy.hpp"
#include "live/event_schedule.hpp"
#include "live/feature_flag.hpp"
#include "live/patch.hpp"
#include "mp/matchmaking.hpp"
#include "mp/rollback.hpp"
#include "net/reliable_channel.hpp"
#include "net/rtt.hpp"
#include "net/session.hpp"
#include "net/udp_transport.hpp"
#include "platform/audio_device.hpp"
#include "platform/capability.hpp"
#include "platform/console.hpp"
#include "platform/detect.hpp"
#include "platform/paths.hpp"
#include "platform/vfs.hpp"
#include "platform/vk_present.hpp"
#include "platform/window.hpp"
#include "profiler/trace.h"
#include "render/framebuffer.hpp"
#include "tools/asset_pipeline.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
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

// 로컬 메타 통화 계정. 이 제품은 한 머신에 한 프로필이므로 계정도 하나다 —
// 계정 개념 자체를 지우지 않는 이유는 db 스키마와 보존 불변식이 계정 단위이기
// 때문이고, 그 단위를 잃으면 supply == sourced - sunk 를 계정별로 물을 수 없다.
inline constexpr uint32_t kMetaAccount = 1;

struct DefenseOptions {
    RunMode     mode = RunMode::Solo;
    bool        selftest = false;         // 창 없이 같은 루프를 돌린다
    int         selftestTicks = 18000;    // 5분 분량(60Hz). 승패가 나면 조기 종료
    std::string address = "127.0.0.1";
    std::string admissionToken;
    // 비면 큐를 쓰지 않는다(기존 동작). 채우면 클라이언트가 입장 전에 줄을 선다.
    std::string queuePlayerId;
    // 0 이면 상점을 거치지 않는다(기존 동작). 양수면 시작 골드 티어를 그 값까지
    // 살 수 있을 만큼 산다 — 매치 밖 소비(sink)의 유일한 제품 경로다.
    int         buyStartGoldTier = 0;
    std::string profilePath;              // 비면 platform::UserDataDirectory 기본값
    std::string traceOut;                 // 비면 트레이스를 덤프하지 않는다
    // 비면 기록하지 않는다. 채우면 헤드리스 한 판의 입력 열과 골든 체인을 적는다.
    std::string recordPath;
    // 비면 검증하지 않는다. 채우면 그 파일의 입력 열로 같은 체인을 다시 만든다.
    std::string verifyPath;
    // 0 이면 실제 시계. 리플레이 재생은 기록된 값으로 못박는다 — 기간 한정
    // 이벤트가 시작 골드·적 체력·발파 비용을 바꾸므로, 벽시계로 재생하면
    // 이벤트 창을 넘는 순간 결정성 결함이 아닌 것이 결정성 결함으로 보고된다.
    int64_t     matchUnixOverride = 0;
    // 골든 체인의 구간 길이. 1 이면 갈린 **틱**을, N 이면 갈린 **구간**을 짚는다.
    uint32_t    chainStride = 1;
    Switches    switches;                 // A/B 스위치(game/switches.hpp)
    uint16_t    port = kDefenseDefaultPort;
    bool        vulkan = false;
    bool        gpuSelftest = false;
};

inline const char* DefenseUsage() {
    return "usage: mega_defense [--selftest [ticks] | --gpu-selftest | --host [port] | "
           "--join <ipv4> [port] [--token TOKEN] [--queue PLAYER_ID]] [--profile PATH] [--buy-tier N] [--vulkan]\n"
           "                    [--ab name=value]... [--trace-out PATH]\n"
           "                    [--record PATH [stride]] [--verify-replay PATH]\n"
           "  --record         헤드리스 한 판의 입력 열과 골든 해시 체인을 PATH 에 적는다\n"
           "  --verify-replay  그 파일의 입력만으로 같은 체인을 다시 만들어 대조한다\n"
           "  --ab   entity_store=aos|soa|ecs  id_map=linear|hash  frame_alloc=malloc|arena\n"
           "         blob_alloc=malloc|slab  body_alloc=vector|pool  worker_lock=std|futex\n"
           "         asset_io=sync|async  scale=small|large  jobs=0|1|2|4|8  affinity=0|1\n"
           "         map=classic|procgen  map_seed=1..8  camera=ortho2d|tilted3d\n"
           "         target_index=linear|grid|quadtree  static_query=naive|bvh\n"
           "         broadphase=naive|grid  integrator=symplectic|explicit  solver_iters=4|8|16\n"
           "         boss_render=sprite|rig\n"
           "         (환경 변수 MEGA_AB=\"name=value,...\" 도 같은 이름을 받는다)\n";
}

inline bool ParseUint16(const char* text, uint16_t& value) {
    char* end = nullptr;
    long parsed = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || parsed < 1 || parsed > 65535) return false;
    value = static_cast<uint16_t>(parsed);
    return true;
}

inline bool ParseDefenseOptions(int argc, char** argv, DefenseOptions& options) {
    // 순서가 규약이다: 기본값 → 환경 변수 → 명령줄. 뒤가 앞을 덮는다.
    std::string switchError;
    if (!options.switches.LoadEnvironment(&switchError)) {
        std::fprintf(stderr, "MEGA_AB: %s\n", switchError.c_str());
        return false;
    }
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ab") == 0) {
            if (i + 1 >= argc) return false;
            if (!options.switches.Parse(argv[++i], &switchError)) {
                std::fprintf(stderr, "--ab %s\n", switchError.c_str());
                return false;
            }
            continue;
        }
        if (std::strcmp(argv[i], "--trace-out") == 0) {
            if (i + 1 >= argc) return false;
            options.traceOut = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--record") == 0) {
            if (i + 1 >= argc) return false;
            options.recordPath = argv[++i];
            // 뒤에 숫자가 오면 구간 길이다. 긴 판을 기록할 때 마크를 줄이는
            // 손잡이이고, 그 대가는 진단이 틱이 아니라 구간으로 나오는 것이다.
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
                char* end = nullptr;
                const long stride = std::strtol(argv[++i], &end, 10);
                if (!end || *end != '\0' || stride < 1 || stride > 65536) return false;
                options.chainStride = static_cast<uint32_t>(stride);
            }
            continue;
        }
        if (std::strcmp(argv[i], "--verify-replay") == 0) {
            if (i + 1 >= argc) return false;
            options.verifyPath = argv[++i];
            continue;
        }
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
        } else if (std::strcmp(argv[i], "--buy-tier") == 0) {
            if (i + 1 >= argc) return false;
            char* end = nullptr;
            const long tier = std::strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || tier < 0 || tier > 5) return false;
            options.buyStartGoldTier = static_cast<int>(tier);
        } else if (std::strcmp(argv[i], "--queue") == 0) {
            if (i + 1 >= argc) return false;
            options.queuePlayerId = argv[++i];
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
// 뒤 넷이 아바타 이동이다. FrameInput::Press 가 이 배열을 그대로 훑으므로
// 창 프런트엔드도 헤드리스 스크립트도 진입점을 고칠 필요가 없다.
inline constexpr std::array<int, 17> kDefenseActionKeys{
    '1', '2', '3', '4', '5', '6', 'P', 'U', 'X', 'T', 'R', 'L', 'B',
    'W', 'A', 'S', 'D'};

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

        // ── 능력 프로파일 ──────────────────────────────────────────
        // 여기서 정하는 것은 워커 수·버퍼 크기·표시 예산뿐이고 권위 규모는 한 줄도
        // 건드리지 않는다. 능력이 적 상한을 바꾸면 모바일과 PC 가 서로 다른 게임을
        // 돌게 되고, 그 순간 "같은 입력 → 같은 상태 해시"가 거짓이 된다.
        platformProfile_ = platform::ProfileFor(platform::CurrentOS());
        budget_ = platform::BudgetFor(platformProfile_);

        const std::string executableDirectory0 = platform::ExecutableDirectory();
        const std::string assetRoot0 = executableDirectory0 + "/assets";

        // ── 진행도와 지갑 ──────────────────────────────────────────
        // 밸런스보다 **먼저** 연다. 지갑의 시작 골드 티어가 balance_.startGold 에
        // 더해지므로, 나중에 열면 첫 판은 언제나 티어 0으로 돈다.
        OpenProfileStore(executableDirectory0);

        // ── 콘텐츠 쿡 → 발행 ───────────────────────────────────────
        // 해시가 0이면 발행하지 않는다. 0 은 "발행할 수 없음"의 이름이고, 그
        // 상태에서 버전을 붙이면 그 버전은 아무것도 증명하지 않는다.
        std::string cookError;
        contentHash_ = defense::CookAssets(assetRoot0, pipeline_, &cookedCount_,
                                           &registeredAssets_, &cookError);
        if (!cookError.empty())
            std::fprintf(stderr, "[content] 쿡 실패, 콘텐츠 버전 없이 진행: %s\n",
                         cookError.c_str());
        else
            patch_.Publish(1, contentHash_);

        // ── 플래그와 이벤트 ────────────────────────────────────────
        // 둘 다 balance.script 와 같은 원자적 폴백이다. 절반만 켜진 시즌보다
        // 이벤트 없는 판이 언제나 낫다.
        std::string flagError, eventError;
        flags_ = defense::LoadFeatureFlags(assetRoot0 + "/flags.txt", &externalFlags_,
                                           &flagError);
        if (!flagError.empty())
            std::fprintf(stderr, "[content] flags.txt 거부, 전원 기본 팔로 진행: %s\n",
                         flagError.c_str());
        events_ = defense::LoadEventSchedule(assetRoot0 + "/events.txt", &externalEvents_,
                                             &eventError);
        if (!eventError.empty())
            std::fprintf(stderr, "[content] events.txt 거부, 이벤트 없이 진행: %s\n",
                         eventError.c_str());

        // 스위치의 출처 교체. 클라이언트는 여기서 적용하지 않는다 — 서버가
        // 내려보낸 배정을 입장 승인 패킷에서 받는다(그래야 한 매치의 두 사람이
        // 같은 권위 팔을 돈다).
        if (options_.mode != RunMode::Client) {
            // 리플레이 재생은 시계를 못박는다. 이벤트 창은 주 단위로 반복하므로
            // 기록과 재생 사이에 창 하나만 열려도 시작 골드·적 체력이 달라진다.
            matchStartUnix_ = options_.matchUnixOverride != 0 ? options_.matchUnixOverride
                                                              : UnixSeconds();
            const live::FlagSet assigned = flags_.Resolve(defense::MatchBucketId("local"));
            ApplyFlagAssignment(assigned, externalFlags_ ? "file" : "default");
            eventMods_ = events_.ModifiersAt(matchStartUnix_);
        }

        // 영구 강화 구매. 밸런스가 읽히기 **전**에 끝나야 이번 판의 시작 골드에
        // 반영된다. 잔액이 모자라면 조용히 멈춘다 — 거절은 결함이 아니다.
        while (options_.buyStartGoldTier > 0 &&
               static_cast<int>(wallet_.startGoldTier) < options_.buyStartGoldTier &&
               BuyStartGoldTier()) {
            std::printf("[meta] buy start_gold_tier=%u cost=%lld balance=%lld\n",
                        wallet_.startGoldTier,
                        static_cast<long long>(
                            live::Economy::StartGoldTierCost(wallet_.startGoldTier - 1)),
                        static_cast<long long>(economy_.Balance(kMetaAccount)));
        }


        // 워커 풀은 스위치가 요구할 때만 존재한다. jobs=0 이면 스레드가 하나도
        // 생기지 않고 시뮬은 예전과 똑같이 직렬로 돈다 — 기본값이 스위치 이전의
        // 동작이어야 한다는 규약이 여기서도 그대로다.
        const int requestedWorkers = platform::ClampWorkers(options_.switches.jobs,
                                                            platformProfile_);
        if (requestedWorkers > 0) {
            Scheduler::Config config;
            config.workers = requestedWorkers;
            config.lock = options_.switches.workerLock == WorkerLockKind::Futex
                ? WorkerLock::Futex : WorkerLock::Std;
            config.pinToCores = options_.switches.affinity;
            scheduler_.Start(config);
        }
        world_.Configure(options_.switches, requestedWorkers > 0 ? &scheduler_ : nullptr);
        interpolator_.SetMode(options_.switches.idMap);
        // 아레나 용량도 능력이 정한다. 표시·스크래치 예산이라 권위 결과에
        // 영향을 주지 않는다 — test_capability_invariant 가 그 사실을 검사한다.
        frameScratch_.Init(options_.switches.frameAlloc,
                           static_cast<size_t>(budget_.frameArenaKB) * 1024);

        const std::string executableDirectory = platform::ExecutableDirectory();
        vfs_.Mount("assets", executableDirectory + "/assets");
        const std::string assetRoot = vfs_.Resolve("assets");

        map_ = defense::ChooseMap(options_.switches, vfs_.Resolve("assets/level.txt"),
                                  &externalMap_, &terrain_);
        // 밸런스 스크립트가 거부되면 기본 구성으로 계속 진행한다(멈추지 않는다).
        // 다만 그 사실을 말한다 — 실행 예산 초과·구문 오류가 조용히 지나가면
        // 파일을 교체한 사람은 자기 값이 적용된 줄로 안다.
        std::string balanceError;
        balance_ = defense::LoadBalanceScript(vfs_.Resolve("assets/balance.script"),
                                              &externalBalance_, &balanceError);
        if (!balanceError.empty())
            std::fprintf(stderr, "[content] balance.script 거부, 기본 구성으로 진행: %s\n",
                         balanceError.c_str());
        // 이벤트 수정자는 밸런스가 정해진 **뒤**, 웨이브에 곱해지기 **전**에
        // 얹는다. 순서가 뒤집히면 "적 체력 1.2배"가 웨이브 표가 아니라 이미
        // 곱해진 값에 다시 곱해진다.
        balance_ = defense::ApplyEventModifiers(balance_, eventMods_);
        balance_.startGold += live::Economy::StartGoldBonus(wallet_.startGoldTier);
        waves_ = defense::ApplyBossRush(
            defense::ApplyStealth(
                defense::ApplyBalance(
                    defense::LoadWaveFile(vfs_.Resolve("assets/waves.txt"),
                                          defense::DefaultWaves(), &externalWaves_),
                    balance_),
                options_.switches),
            eventMods_.bossRush);
        // 보스의 저작 포즈. 이것은 표시 데이터가 아니라 **시뮬 입력**이다 —
        // 클립이 정한 고관절 회전이 IK 목표를 돌리고, 그 IK 가 푼 발이 스톰프의
        // 원점이 된다. 그래서 월드가 사본을 들고, 표시도 같은 사본을 본다.
        std::string poseError;
        poses_ = defense::LoadBossPoses(vfs_.Resolve("assets/boss.pose"),
                                        &externalPoses_, &poseError);
        if (!poseError.empty())
            std::fprintf(stderr, "[content] boss.pose 거부, 내장 클립으로 진행: %s\n",
                         poseError.c_str());
        world_.SetPoses(poses_);
        // 정예 행동 트리도 재빌드 없이 갈아 끼우는 파일이다. Configure 가 트리를
        // 조립하므로 Init 보다 먼저 넣어야 하고, 거부되면 내장 트리로 계속 간다.
        bool eliteFileFound = false;
        world_.SetEliteTree(
            defense::LoadEliteTree(vfs_.Resolve("assets/elite.bt"), &eliteFileFound));
        world_.Configure(options_.switches, requestedWorkers > 0 ? &scheduler_ : nullptr);
        externalEliteTree_ = world_.ExternalEliteTree();
        if (eliteFileFound && !externalEliteTree_)
            std::fprintf(stderr, "[content] elite.bt 거부, 내장 트리로 진행\n");
        world_.Init(map_, waves_, balance_.startGold, balance_.coreHp);
        // 발파 비용도 이벤트가 정한다. 기본 수정자는 0 이라 예전 값 그대로다.
        world_.SetBlastCost(kBlastCost + eventMods_.blastCostDelta);

        // 솔로는 아바타가 하나뿐이라 원격 입력을 기다릴 상대가 없다. 2인으로
        // 두면 비예측 팔(기본)이 영원히 멈춘다 — 없는 사람의 확정을 기다린다.
        avatarPlayers_ = options_.mode == RunMode::Solo ? 1 : 2;
        localAvatar_ = options_.mode == RunMode::Client ? 1 : 0;
        remoteAvatar_ = options_.mode == RunMode::Client ? 0 : 1;
        avatars_.Init(avatarPlayers_, 8);
        avatars_.SetPrediction(options_.switches.avatarPredict);

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
        // 카메라는 맵을 알아야 프레이밍을 잡는다. ortho2d 팔에서는 이 두 줄이
        // 아무 행렬도 만들지 않는다 — 항등이 곧 기본값이다.
        camera_.Configure(options_.switches.camera, kViewWidth, kViewHeight);
        camera_.Frame(map_);
        skin_.Load(assetRoot);
        reloader_.Init(requestedWorkers > 0 ? &scheduler_ : nullptr,
                       options_.switches.assetIo, assetRoot);

        for (int key : kDefenseActionKeys) input_.Bind(key, ActionForKey(key));


        std::printf("[platform] %s profile=%s workers=%d arena=%dKB voices=%d lod=%d\n",
                    platform::CurrentPlatformLine().c_str(), platformProfile_.name,
                    budget_.workerCap, budget_.frameArenaKB, budget_.audioVoiceCap,
                    budget_.renderLod);
        std::printf("[content] version=%u hash=0x%016llX cooked=%d/%d\n",
                    patch_.Current(),
                    static_cast<unsigned long long>(contentHash_),
                    cookedCount_, registeredAssets_ + 1);
        std::printf("[events] source=%s active=%zu scrap=x%.2f reward=x%.2f enemy_hp=x%.2f "
                    "start_gold=%+d blast_cost=%+d boss_rush=%d\n",
                    externalEvents_ ? "file" : "none",
                    events_.ActiveAt(matchStartUnix_).size(),
                    static_cast<double>(eventMods_.scrapMultiplier),
                    static_cast<double>(eventMods_.rewardMultiplier),
                    static_cast<double>(eventMods_.enemyHpMultiplier),
                    eventMods_.bonusStartGold, eventMods_.blastCostDelta,
                    eventMods_.bossRush ? 1 : 0);
        std::printf("[meta] scrap=%lld tier=%u start_gold=%d+%d\n",
                    static_cast<long long>(economy_.Balance(kMetaAccount)),
                    wallet_.startGoldTier,
                    balance_.startGold - live::Economy::StartGoldBonus(wallet_.startGoldTier),
                    live::Economy::StartGoldBonus(wallet_.startGoldTier));
        std::printf("[flags] source=%s applied=%d meta_shop=%d scrap_event=%d assignment=%s\n",
                    flagSource_, flagsApplied_, metaShop_ ? 1 : 0, scrapEventEnabled_ ? 1 : 0,
                    flagAssignment_.empty() ? "(none)" : flagAssignment_.c_str());
        std::printf("[assets] loaded %d/10 external sprites from %s\n",
                    skin_.externalAssets, assetRoot.c_str());
        std::printf("[content] elite_tree=%s\n", externalEliteTree_ ? "external" : "builtin");
        std::printf("[content] map=%s waves=%s (%zu) balance=%s pose=%s(%zu키)\n",
                    externalMap_ ? "external" : "fallback",
                    externalWaves_ ? "external" : "fallback", waves_.size(),
                    externalBalance_ ? "script" : "fallback",
                    externalPoses_ ? "external" : "fallback", poses_.walk.KeyCount());
        if (map_.Generated())
            std::printf("[terrain] seed=%d %dx%d maze=%d carved=%d rock=%d veins=%d route=%d "
                        "bvh_prims=%zu/nodes=%zu\n",
                        options_.switches.mapSeed, map_.Columns(), map_.Rows(),
                        terrain_.maze, terrain_.carved, terrain_.rock, terrain_.veins,
                        terrain_.route, world_.Geometry().PrimCount(),
                        world_.Geometry().NodeCount());
        // 어느 팔로 돌았는지가 로그에 남지 않으면 뒤에 찍히는 모든 수치가 출처
        // 없는 숫자가 된다.
        std::printf("[switches] %s workers=%d lock=%s\n",
                    options_.switches.Describe().c_str(), scheduler_.WorkerCount(),
                    scheduler_.LockKind() == WorkerLock::Futex ? "futex" : "std");
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

    // ── 리플레이 채집 ──────────────────────────────────────────────
    // 켜면 프레임마다 조작을, 시뮬 틱마다 골든 체인 링크와 아바타 자취를 모은다.
    // 끄면 이 셋은 한 줄도 돌지 않는다 — 기록하지 않는 실행의 프레임에 검사
    // 비용을 얹지 않는 것이 기본값 규약이다.
    void EnableReplayCapture(uint32_t stride) {
        capture_ = true;
        chain_.Reset(stride);
        capturedFrames_.clear();
        capturedAvatar_.clear();
    }
    bool ReplayCapturing() const { return capture_; }
    const mp::GoldenChain& Chain() const { return chain_; }
    const std::vector<ReplayFrame>& CapturedFrames() const { return capturedFrames_; }
    const std::vector<ReplayAvatarTick>& CapturedAvatar() const { return capturedAvatar_; }
    int AvatarPlayers() const { return avatarPlayers_; }
    int LocalAvatarIndex() const { return localAvatar_; }

    // FrameInput 과 그 와이어 형태 사이의 변환. 소유자가 둘 다 아는 유일한
    // 자리라 여기 있다(replay.hpp 는 FrameInput 을 볼 수 없다 — 순환이다).
    // snippet-begin: replay-frame-codec
    static ReplayFrame ToReplayFrame(const FrameInput& frame) {
        ReplayFrame out;
        for (size_t i = 0; i < kDefenseActionKeys.size(); ++i)
            if (frame.keyDown[i]) out.keys |= (1u << i);
        out.mouseX = static_cast<int16_t>(std::clamp(frame.mouseX, -32768, 32767));
        out.mouseY = static_cast<int16_t>(std::clamp(frame.mouseY, -32768, 32767));
        out.flags = frame.mouseClicked ? kReplayClicked : 0;
        return out;
    }
    static FrameInput FromReplayFrame(const ReplayFrame& record) {
        FrameInput frame;
        for (size_t i = 0; i < kDefenseActionKeys.size(); ++i)
            frame.keyDown[i] = (record.keys & (1u << i)) != 0;
        frame.mouseX = record.mouseX;
        frame.mouseY = record.mouseY;
        frame.mouseClicked = (record.flags & kReplayClicked) != 0;
        return frame;
    }
    // snippet-end: replay-frame-codec

    // ── 프레임 하나 ────────────────────────────────────────────────
    // 창 프런트엔드와 헤드리스 셀프테스트가 부르는 유일한 함수. 표시(Present)와
    // 프레임 페이싱만 바깥에 있다.
    void Step(const FrameInput& frame, double frameSeconds) {
        MEGA_PROFILE_SCOPE("frame");
        // 프레임의 조작은 **쓰이기 전에** 적는다. 소비된 뒤에 적으면 이 프레임이
        // 무엇을 눌렀는가가 아니라 그 뒤에 무엇이 남았는가를 적게 된다.
        if (capture_) capturedFrames_.push_back(ToReplayFrame(frame));
        frameScratch_.Begin();
        // snippet-begin: frame-seam
        input_.BeginFrame();
        for (size_t i = 0; i < kDefenseActionKeys.size(); ++i)
            input_.OnKey(kDefenseActionKeys[i], frame.keyDown[i]);

        const Clock::time_point now = Clock::now();
        frameSeconds = std::clamp(frameSeconds, 0.0, 0.25);
        accumulator_ += frameSeconds;

        {
            MEGA_PROFILE_SCOPE("net.pump");
            if (options_.mode == RunMode::Host) PumpHost();
            else if (options_.mode == RunMode::Client) PumpClient(now, frameSeconds);
        }
        // snippet-end: frame-seam

        // 에셋 리로드. 'L' 이 요청을 걸고, 완료 확인은 매 프레임 폴링이다 —
        // 동기 팔에서는 Request 안에서 이미 끝나 있고 Poll 이 할 일이 없다.
        if (input_.WasPressed("reload")) reloader_.Request(skin_);
        reloader_.Poll(skin_);

        if (input_.WasPressed("tower_cannon")) selectedType_ = TowerType::Cannon;
        if (input_.WasPressed("tower_gatling")) selectedType_ = TowerType::Gatling;
        if (input_.WasPressed("tower_splash")) selectedType_ = TowerType::Splash;
        if (input_.WasPressed("tower_sniper")) selectedType_ = TowerType::Sniper;
        if (input_.WasPressed("tower_frost")) selectedType_ = TowerType::Frost;
        if (input_.WasPressed("tower_radar")) selectedType_ = TowerType::Radar;
        if (options_.mode == RunMode::Solo && input_.WasPressed("pause"))
            paused_ = !paused_;

        view_ = options_.mode == RunMode::Client
            ? replicas_.Render([this](const wire::Snapshot& a, const wire::Snapshot& b, float t) {
                  return interpolator_.Blend(a, b, t);
              })
            : CaptureView(world_, hostTick_);

        // 화면 좌표를 지면 좌표로 되돌린다. ortho2d 팔에서 Pick 은 입력을 그대로
        // 돌려주므로 이 줄이 생기기 전과 같은 값이 나온다.
        // snippet-begin: mouse-pick
        math::Vec2 mouse{-100.0f, -100.0f};
        const bool onGround =
            frame.mouseY >= map_.OriginY() &&
            camera_.Pick(static_cast<float>(frame.mouseX),
                         static_cast<float>(frame.mouseY), mouse);
        // snippet-end: mouse-pick

        if (onGround && input_.WasPressed("blast"))
            Submit(wire::CommandType::Blast, mouse, 0);

        // snippet-begin: click-routing
        if (frame.mouseClicked && onGround) {
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
        // snippet-end: click-routing

        // snippet-begin: fixed-step
        if (options_.mode != RunMode::Client) {
            if ((world_.Won() || world_.Lost()) && input_.WasPressed("restart")) Restart();
            int catchUpSteps = 0;
            if (paused_) accumulator_ = 0.0;
            while (!paused_ && accumulator_ >= kDefenseFixedDt && catchUpSteps < 5) {
                if (!world_.Won() && !world_.Lost()) world_.Step(kDefenseFixedDt);
                StepAvatars(frame);
                RecordTick();
                ++hostTick_;
                accumulator_ -= kDefenseFixedDt;
                ++catchUpSteps;
                if (options_.mode == RunMode::Host && network_.PeerKnown()) {
                    // 아바타는 양쪽이 같은 2인 서브시뮬을 굴린다. 호스트 입력이
                    // 클라이언트에 가지 않으면 클라이언트의 player 0 은 언제나
                    // "직전 반복"으로 예측돼, 체크섬 대조가 상시 불일치가 된다.
                    SendAvatarInput();
                    if (hostTick_ % 3 == 0) {
                        SendSnapshot();
                        MaybeSendTerrain();
                    }
                }
            }
            if (catchUpSteps == 5 && accumulator_ >= kDefenseFixedDt) accumulator_ = 0.0;
            view_ = CaptureView(world_, hostTick_);
        } else {
            // 클라이언트는 권위 시뮬을 돌리지 않지만 **자기 아바타는 예측한다**.
            // 같은 고정 스텝이어야 호스트의 프레임 번호와 맞춰 볼 수 있다.
            int avatarSteps = 0;
            while (accumulator_ >= kDefenseFixedDt && avatarSteps < 5) {
                StepAvatars(frame);
                RecordTick();
                accumulator_ -= kDefenseFixedDt;
                ++avatarSteps;
            }
            if (avatarSteps == 5 && accumulator_ >= kDefenseFixedDt) accumulator_ = 0.0;
        }
        // snippet-end: fixed-step

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
            // snippet-begin: settle-scrap
            // 매치 안 골드는 매치와 함께 사라진다. 스크랩은 그 위에 얹는 메타
            // 통화이고, 발행 시점은 정확히 여기 한 곳이다 — 발행 자리가 둘이면
            // supply == sourced - sunk 를 어디서 깨뜨렸는지 말할 수 없다.
            const int64_t base = static_cast<int64_t>(std::max(score, 0)) / 10 +
                                 (world_.Won() ? 50 : 0);
            const double multiplier =
                scrapEventEnabled_ ? static_cast<double>(eventMods_.scrapMultiplier) : 1.0;
            const int64_t scrap = static_cast<int64_t>(static_cast<double>(base) * multiplier);
            if (scrap > 0 && economy_.Grant(kMetaAccount, scrap,
                                            world_.Won() ? live::Source::ClearBonus
                                                         : live::Source::MatchReward)) {
                scrapEarned_ += scrap;
                wallet_ = economy_.Export(kMetaAccount);
            }
            // snippet-end: settle-scrap
            if (progression_.Record(static_cast<uint32_t>(world_.Wave()),
                                    static_cast<uint32_t>(std::max(score, 0)),
                                    world_.Won(), hostTick_, profile_, &wallet_)) {
                metrics_.GetCounter("matches_finished").Add();
                recordedMatch_ = true;
                std::printf("[meta] settle scrap=+%lld balance=%lld supply=%lld "
                            "sourced=%lld sunk=%lld tier=%u\n",
                            static_cast<long long>(scrap),
                            static_cast<long long>(economy_.Balance(kMetaAccount)),
                            static_cast<long long>(economy_.Supply()),
                            static_cast<long long>(economy_.Sourced()),
                            static_cast<long long>(economy_.Sunk()),
                            wallet_.startGoldTier);
                std::fflush(stdout);
            }
        }

        UpdateImpactParticles(view_, previousProjectiles_, particles_,
                              static_cast<float>(frameSeconds), frameScratch_);

        // 웨이브가 바뀌면 카메라가 경로를 훑고 돌아온다. 표시 전용이라 시뮬은
        // 이 코드가 있는지도 모른다.
        // snippet-begin: wave-rail
        if (camera_.Tilted() && view_.wave != previousWave_) {
            previousWave_ = view_.wave;
            camera_.BeginRail(RailControls(), 1.5f);
        }
        camera_.UpdateRail(static_cast<float>(frameSeconds));
        // snippet-end: wave-rail

        // 시뮬이 낸 소리를 지금 카메라 기준으로 옮긴다. 카메라가 먼저 갱신돼야
        // 하는 이유는 레일이 도는 프레임에서 청취자가 한 프레임 뒤처지기 때문이다.
        // snippet-begin: frame-audio
        sfx_.Collect(world_.Sounds(), camera_, sound_.Tones());
        sfx_.Submit(sound_);
        world_.ClearSounds();
        // snippet-end: frame-audio

        HudState hud;
        hud.selectedType = selectedType_;
        hud.selectedTower = selectedTower_;
        hud.mouseX = frame.mouseX;
        hud.mouseY = frame.mouseY;
        hud.mouseWorld = mouse;
        hud.mouseOnGround = onGround;
        hud.avatarGate = options_.switches.avatarGate;
        hud.withinReach =
            !options_.switches.avatarGate ||
            wire::BuildWithinReach(map_, avatars_.State().Cell(localAvatar_), mouse);
        hud.modeLabel = ModeLabel();
        hud.paused = paused_;
        hud.bossRender = options_.switches.bossRender;
        hud.poses = &poses_;
        {
            MEGA_PROFILE_SCOPE("render.draw");
            DrawGame(framebuffer_, map_, view_, skin_, hud, connected_, &particles_, camera_);
        }
        frameScratch_.End();
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
                    "snapshots_truncated=%llu baseline_miss=%llu "
                    "rtt_ms=%.1f resend_ms=%.0f delay_ticks=%.2f\n",
                    static_cast<unsigned long long>(metrics_.GetCounter("matches_finished").Value()),
                    static_cast<unsigned long long>(metrics_.GetCounter("remote_commands").Value()),
                    static_cast<unsigned long long>(metrics_.GetCounter("snapshots_sent").Value()),
                    static_cast<unsigned long long>(commandPacketsSent_),
                    acknowledgedCommand_,
                    FrameHistogram().Percentile(0.95),
                    bytes.Percentile(0.95), bytes.Max(),
                    static_cast<unsigned long long>(
                        metrics_.GetCounter("snapshots_truncated").Value()),
                    static_cast<unsigned long long>(
                        metrics_.GetCounter("snapshot_baseline_miss").Value()),
                    static_cast<double>(rtt_.Rtt()),
                    static_cast<double>(AppliedResendMs()),
                    AppliedDelayTicks());
        std::fflush(stdout);
    }

    // ── 조회(창 프런트엔드 · 헤드리스 검증 공용) ────────────────────
    const render::Framebuffer& Surface() const { return framebuffer_; }
    const GameWorld& World() const { return world_; }
    const DefenseMap& Map() const { return map_; }
    const wire::Snapshot& View() const { return view_; }
    uint16_t LocalPort() const { return network_.LocalPort(); }
    // 지금 팔이 **실제로 쓰는** 값. 측정값 그 자체를 찍으면 fixed 팔의 로그가
    // "링크를 봤다"고 말하게 되고, 두 팔의 로그가 구분되지 않는다.
    // snippet-begin: applied-timing
    int AppliedResendMs() const {
        return options_.switches.interpDelay == InterpDelayKind::Rtt
            ? static_cast<int>(timing_.ResendMs(rtt_.Estimator()))
            : static_cast<int>(timing_.fallbackResendMs);
    }
    double AppliedDelayTicks() const {
        return options_.switches.interpDelay == InterpDelayKind::Rtt
            ? timing_.DelayTicks(rtt_.Estimator())
            : timing_.fallbackDelayTicks;
    }
    // snippet-end: applied-timing
    float MeasuredRttMs() const { return rtt_.Rtt(); }
    bool  RttReady() const { return rtt_.Ready(); }
    const DefenseSkin& Skin() const { return skin_; }
    const DefenseCamera& Camera() const { return camera_; }
    const mapgen::TerrainStats& Terrain() const { return terrain_; }
    uint64_t TerrainPacketsSent() const { return terrainPacketsSent_; }
    uint64_t TerrainPacketsReceived() const { return terrainPacketsReceived_; }
    const defense::BalanceConfig& Balance() const { return balance_; }
    uint32_t HostTick() const { return hostTick_; }
    bool Connected() const { return connected_; }
    bool Finished() const { return view_.won || view_.lost; }
    bool ExternalMap() const { return externalMap_; }
    bool ExternalWaves() const { return externalWaves_; }
    bool ExternalBalance() const { return externalBalance_; }
    bool ExternalPoses() const { return externalPoses_; }
    bool ExternalEliteTree() const { return externalEliteTree_; }
    const BossPoses& Poses() const { return poses_; }
    // 이번 프레임에 카메라 기준으로 계산된 보이스. 장치가 열려 있든 아니든
    // 같은 값이라, 헤드리스에서 배선을 관측할 수 있다.
    const SfxBus& Sfx() const { return sfx_; }
    const DefenseOptions& Options() const { return options_; }
    live::Registry& Metrics() { return metrics_; }
    Scheduler& Jobs() { return scheduler_; }
    const FrameScratch& RenderScratch() const { return frameScratch_; }
    AssetReloader& Reloader() { return reloader_; }
    // 창 없는 호출자(테스트·셀프테스트)가 리로드를 거는 입구. 창 실행에서는
    // 'L' 키가 같은 자리로 들어간다.
    void RequestAssetReload() { reloader_.Request(skin_); }

    // ── 계측 출력 ──────────────────────────────────────────────────
    // live::Histogram 의 frame_ms 는 "프레임이 몇 ms 였나"까지만 답한다. 느려진
    // 프레임 안에서 **무엇이** 오래 걸렸는지는 답하지 못한다. 아래 줄이 그 질문의
    // 답이고, 출처는 프레임 루프와 시뮬 시스템에 박은 ScopeTimer 다.
    void PrintProfile() {
        std::vector<profiler::AggEntry> aggregate =
            profiler::AggregateByName(profiler::Profiler::Get().Snapshot());
        std::sort(aggregate.begin(), aggregate.end(),
                  [](const profiler::AggEntry& a, const profiler::AggEntry& b) {
                      return a.totalMs > b.totalMs;
                  });
        std::printf("[profile] recorded=%llu",
                    static_cast<unsigned long long>(profiler::Profiler::Get().TotalRecorded()));
        for (size_t i = 0; i < aggregate.size() && i < 8; ++i)
            std::printf(" %s=%.2f/%.3fms(n=%llu)", aggregate[i].name.c_str(),
                        aggregate[i].totalMs, aggregate[i].maxMs,
                        static_cast<unsigned long long>(aggregate[i].count));
        std::printf("\n");
        std::fflush(stdout);
    }

    // --trace-out 이 있으면 집계를 파일로 남긴다. asset_io=async 면 이 쓰기도
    // 워커가 한다(io::File::WriteAt 의 제품 호출자는 이 경로 하나뿐이다).
    bool DumpTrace() {
        if (options_.traceOut.empty()) return false;
        const bool ok = WriteTraceReport(options_.traceOut, options_.switches.assetIo,
                                         scheduler_.WorkerCount() > 0 ? &scheduler_ : nullptr);
        std::printf("[trace] %s %s\n", ok ? "wrote" : "FAILED", options_.traceOut.c_str());
        std::fflush(stdout);
        return ok;
    }

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

    // ── 아바타 관측 ────────────────────────────────────────────────
    uint32_t AvatarFrame() const { return avatarFrame_; }
    uint32_t AvatarChecksum32() const {
        return static_cast<uint32_t>(avatars_.Checksum() & 0xFFFFFFFFu);
    }
    int32_t  AvatarCell() const { return avatars_.State().Cell(localAvatar_); }
    // 셀프테스트가 목표 칸을 묻는 입구. 제품의 승인 판정과 **같은 함수**를 쓰지
    // 않으면 스크립트가 사거리 안이라고 믿는 자리와 호스트가 받아 주는 자리가
    // 갈라져, 계획된 타워가 영원히 안 선다.
    int32_t  AvatarCellOf(const math::Vec2& position) const {
        return AvatarCellOfWorld(position);
    }
    uint64_t AvatarStalls() const { return avatars_.Stalls(); }
    uint64_t AvatarRollbacks() const { return avatars_.Rollbacks(); }
    uint64_t AvatarComparisons() const { return avatarComparisons_; }
    uint64_t AvatarDesyncs() const { return avatarDesyncs_; }
    uint64_t AvatarInputPacketsSent() const { return avatarInputPacketsSent_; }
    uint64_t AvatarInputPacketsReceived() const { return avatarInputPacketsReceived_; }
    uint64_t AvatarInputBytesSent() const { return avatarInputBytesSent_; }
    uint64_t BuildRejectedOutOfReach() const { return buildRejectedOutOfReach_; }
    const std::string& QueueMatchId() const { return queueMatchId_; }
    uint64_t ContentHash() const { return contentHash_; }
    uint32_t ContentVersion() const { return patch_.Current(); }
    int      CookedAssets() const { return cookedCount_; }
    const live::EventModifiers& EventModifiers() const { return eventMods_; }
    int64_t  MatchStartUnix() const { return matchStartUnix_; }
    int64_t  ScrapBalance() const { return economy_.Balance(kMetaAccount); }
    int64_t  ScrapEarned() const { return scrapEarned_; }
    uint32_t StartGoldTier() const { return wallet_.startGoldTier; }
    bool     EconomyConserved(std::string* why = nullptr) const {
        return economy_.Conserved(why);
    }
    int      FlagsApplied() const { return flagsApplied_; }
    bool     MetaShopOpen() const { return metaShop_; }
    bool     ScrapEventEnabled() const { return scrapEventEnabled_; }
    const char* FlagSource() const { return flagSource_; }
    const std::string& FlagAssignment() const { return flagAssignment_; }
    bool     GrantReceived() const { return grantReceived_; }
    const platform::PlatformProfile& Platform() const { return platformProfile_; }
    const platform::RuntimeBudget& Budget() const { return budget_; }
    // 영구 강화 한 단계. 매치 밖 소비(sink)의 유일한 제품 경로다.
    bool BuyStartGoldTier() {
        if (!metaShop_) return false;      // 운영 토글이 닫아 둔 상점이다
        if (!economy_.BuyStartGoldTier(kMetaAccount)) return false;
        wallet_ = economy_.Export(kMetaAccount);
        defense::Profile ignored = profile_;
        progression_.Record(profile_.bestWave, profile_.bestScore, false, 0, ignored, &wallet_);
        profile_ = ignored;
        return true;
    }
    uint64_t QueuePacketsSent() const { return queuePacketsSent_; }
    int      QueueRatingSent() const { return QueueRating(); }

private:
    static const char* ActionForKey(int key) {
        switch (key) {
            case '1': return "tower_cannon";
            case '2': return "tower_gatling";
            case '3': return "tower_splash";
            case '4': return "tower_sniper";
            case '5': return "tower_frost";
            case '6': return "tower_radar";
            case 'P': return "pause";
            case 'U': return "upgrade";
            case 'X': return "sell";
            case 'T': return "target_mode";
            case 'L': return "reload";
            case 'B': return "blast";
            case 'W': return "avatar_up";
            case 'A': return "avatar_left";
            case 'S': return "avatar_down";
            case 'D': return "avatar_right";
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
        previousProjectiles_.Clear();
    }

    void Submit(wire::CommandType type, const math::Vec2& position, uint8_t argument) {
        wire::Command command;
        command.type = type;
        command.x = wire::EncodePosition(position.x);
        command.y = wire::EncodePosition(position.y);
        command.argument = argument;
        if (options_.mode != RunMode::Client && !BuildAllowed(command, localAvatar_)) return;
        if (options_.mode == RunMode::Client) {
            localCommands_.Queue(command);        // 시퀀스는 채널이 부여한다
            ++commandsQueued_;
        } else if (wire::ApplyCommand(world_, command)) {
            sound_.Play(platform::Sfx::Pickup);
        }
    }

    // 한 프레임의 아바타 입력. 창이든 스크립트든 같은 FrameInput 에서 나온다.
    // snippet-begin: avatar-input
    mp::Input LocalAvatarInput(const FrameInput& frame) const {
        mp::Input input;
        const int right = frame.keyDown[KeyIndex('D')] ? 1 : 0;
        const int left  = frame.keyDown[KeyIndex('A')] ? 1 : 0;
        const int down  = frame.keyDown[KeyIndex('S')] ? 1 : 0;
        const int up    = frame.keyDown[KeyIndex('W')] ? 1 : 0;
        input.moveX = static_cast<int8_t>(right - left);
        input.moveY = static_cast<int8_t>(down - up);
        // 클릭이 곧 건설 요청이다. 아바타 사거리 규칙은 그 요청을 **호스트가**
        // 판정하고, 여기서는 "요청했다"만 기록한다.
        input.SetBuild(frame.mouseClicked);
        input.SetFire(frame.keyDown[KeyIndex('B')]);
        return input;
    }

    void StepAvatars(const FrameInput& frame) {
        MEGA_PROFILE_SCOPE("sim.avatar");
        const mp::Input input = LocalAvatarInput(frame);
        lastAvatarInput_ = input;
        lastAvatarAdvanced_ = false;
        avatars_.SetInput(avatarFrame_, localAvatar_, input, true);
        // 창에 **먼저** 넣는다. 이 프레임의 로컬 입력은 틱이 성공했든 아니든
        // 이미 확정된 사실이고, 상대는 그것을 받아야 자기 프레임을 열 수 있다.
        // 틱 뒤로 미루면 비예측 팔에서 양쪽이 서로를 기다리며 영원히 굳는다 —
        // 내 입력을 안 보내니 상대가 못 돌고, 상대가 못 도니 내 입력이 안 온다.
        if (avatarFrame_ == windowEnd_) {
            if (recentCount_ < mp::kMaxInputFrames) {
                recentInputs_[recentCount_++] = input;
            } else {
                for (size_t i = 1; i < mp::kMaxInputFrames; ++i)
                    recentInputs_[i - 1] = recentInputs_[i];
                recentInputs_[mp::kMaxInputFrames - 1] = input;
            }
            ++windowEnd_;
        }
        if (!avatars_.Tick()) return;   // 비예측 팔: 원격 확정이 없으면 멈춘다
        ++avatarFrame_;
        lastAvatarAdvanced_ = true;
        // 슬롯 번호는 **틱을 마친 뒤의 프레임 수**다. 호스트가 스냅샷에 싣는
        // avatarFrame 도 같은 수(자기 서브시뮬의 frame)이므로 둘이 같은 좌표를
        // 가리킨다. 여기서 한 칸 어긋나면 두 쪽이 다른 프레임을 맞춰 보게 되고,
        // 정상 동작이 전량 desync 로 보고된다.
        const uint32_t checksum = static_cast<uint32_t>(avatars_.Checksum() & 0xFFFFFFFFu);
        const size_t slot = avatarFrame_ % kAvatarChecksumWindow;
        avatarChecksums_[slot] = checksum;
        avatarChecksumValid_[slot] = true;
        // 이 값이 예측 위에서 계산됐다면, 나중에 되감기가 그것을 고쳐 쓴다.
        // 되감기 횟수를 함께 적어 두면 "그 뒤로 되감긴 적이 없다"를 물을 수 있고,
        // 그때만 대조가 성립한다. 되감겼다면 조용히 건너뛴다 — 되감힌 프레임의
        // 옛 체크섬으로 불일치를 세면 정상 동작이 desync 로 보고된다.
        avatarRollbacksAt_[slot] = avatars_.Rollbacks();
    }
    // snippet-end: avatar-input

    // 시뮬 틱 하나를 체인에 접는다. 고정 스텝 루프 안, `world_.Step` 과
    // `StepAvatars` **둘 다 끝난 뒤**에 부른다 — 링크가 가리키는 것은 "이 틱을
    // 마친 뒤의 세계"이고, 그 좌표를 아바타 체크섬 슬롯과 같게 두어야 두 수가
    // 같은 순간을 말한다.
    // snippet-begin: chain-append
    void RecordTick() {
        if (!capture_) return;
        chain_.Append(world_.StateHash(), avatars_.Checksum(), avatarFrame_);
        ReplayAvatarTick tick;
        tick.local = lastAvatarInput_;
        tick.advanced = lastAvatarAdvanced_ ? 1 : 0;
        tick.checksum = avatars_.Checksum();
        capturedAvatar_.push_back(tick);
    }
    // snippet-end: chain-append

    int32_t AvatarCellOfWorld(const math::Vec2& position) const {
        return wire::AvatarCellOfMap(map_, position);
    }

    static constexpr size_t KeyIndex(int key) {
        for (size_t i = 0; i < kDefenseActionKeys.size(); ++i)
            if (kDefenseActionKeys[i] == key) return i;
        return 0;
    }

    // 레이팅의 출처. 협동 PvE 라 승패가 상대적이지 않으므로 지금 저장되는
    // 유일한 실력 대리 지표가 최고 점수다. 그 사실을 이름으로 적어 둔다.
    int QueueRating() const {
        return mp::RatingFromBestScore(profile_.bestScore);
    }

    void SendAvatarInput() {
        if (recentCount_ == 0) return;
        const uint32_t base = windowEnd_ - static_cast<uint32_t>(recentCount_);
        const net::PacketHeader header = ackWindow_.Header(outgoingSeq_.Next());
        rtt_.OnSent(header.seq, NowMs(Clock::now()));
        const net::Bytes packet =
            wire::WriteInputPacket(base, recentInputs_.data(), recentCount_, header);
        network_.Send(packet);
        ++avatarInputPacketsSent_;
        avatarInputBytesSent_ += packet.size();
    }

    // 원격 아바타의 입력 스트림을 먹인다. 같은 프레임이 여러 번 도착하는 것이
    // 정상이다(중복 전송이 재전송을 대신한다) — SetInput 은 멱등이다.
    bool FeedAvatarInput(const net::Bytes& packet, net::PacketHeader* framing) {
        uint32_t base = 0;
        std::vector<mp::Input> inputs;
        if (!wire::ReadInputPacket(packet, base, inputs, framing)) return false;
        for (size_t i = 0; i < inputs.size(); ++i)
            avatars_.SetInput(base + static_cast<uint32_t>(i), remoteAvatar_, inputs[i], true);
        ++avatarInputPacketsReceived_;
        return true;
    }

    // 호스트가 실은 (프레임, 체크섬)을 내 같은 프레임과 맞춘다. 창 밖이거나
    // 아직 그 프레임을 굴리지 않았으면 **모른다**이지 불일치가 아니다.
    void CompareAvatarChecksum(const wire::Snapshot& snapshot) {
        if (snapshot.avatarFrame == 0) return;
        if (snapshot.avatarFrame > avatarFrame_) return;
        if (avatarFrame_ - snapshot.avatarFrame >= kAvatarChecksumWindow) return;
        const size_t slot = snapshot.avatarFrame % kAvatarChecksumWindow;
        if (!avatarChecksumValid_[slot]) return;
        if (avatarRollbacksAt_[slot] != avatars_.Rollbacks()) return;   // 고쳐 쓰였다
        ++avatarComparisons_;
        if (avatarChecksums_[slot] != snapshot.avatarChecksum) {
            ++avatarDesyncs_;
            metrics_.GetCounter("avatar_desync").Add();
        }
    }

    static int64_t UnixSeconds() {
        return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    void OpenProfileStore(const std::string& executableDirectory) {
        std::string path = options_.profilePath;
        if (path.empty()) {
            // 클라이언트는 권위 진행도를 갖지 않으므로 기본 저장소를 열지 않는다
            // (기존 동작). 명시된 프로필이 있으면 읽는다 — 큐에 실어 보낼 레이팅의
            // 출처가 실제 저장된 최고 점수여야 하기 때문이다.
            if (options_.mode == RunMode::Client) return;
            const std::string directory = platform::UserDataDirectory("MegaDefense");
            path = (directory.empty() ? executableDirectory : directory) + "/profile.wal";
        }
        if (!progression_.Open(path)) return;
        progression_.Load(profile_);
        if (progression_.LoadWallet(wallet_)) {
            std::string importError;
            if (!economy_.Import(wallet_, &importError))
                std::fprintf(stderr, "[meta] 지갑을 거부했다, 잔액 0으로 진행: %s\n",
                             importError.c_str());
        }
        wallet_ = economy_.Export(kMetaAccount);
    }

    // 서버가 내려보낸 승인을 적용한다. 클라이언트가 버킷을 **다시 계산하지
    // 않는** 것이 핵심이다 — 같은 해시를 두 곳에서 돌리면 한쪽만 바뀌었을 때
    // 두 프로세스가 다른 게임을 돌게 된다.
    void ApplyAdmissionGrant(const wire::AdmissionGrant& grant) {
        grantReceived_ = true;
        matchStartUnix_ = grant.matchStartUnix;
        eventMods_ = events_.ModifiersAt(matchStartUnix_);
        live::FlagSet assigned;
        if (!grant.flags.empty()) live::FlagSet::Decode(grant.flags, assigned);
        ApplyFlagAssignment(assigned, "server");
        const bool sameContent = grant.contentHash == contentHash_;
        std::printf("[admission] grant version=%u hash=0x%016llX server=%s/%s content=%s "
                    "match_start=%lld flags=%s\n",
                    grant.contentVersion,
                    static_cast<unsigned long long>(grant.contentHash),
                    platform::OSName(platform::OSFromCode(grant.serverOs)),
                    platform::ArchName(platform::ArchFromCode(grant.serverArch)),
                    sameContent ? "accept" : "mismatch",
                    static_cast<long long>(grant.matchStartUnix),
                    grant.flags.empty() ? "(none)" : grant.flags.c_str());
        std::fflush(stdout);
        if (!sameContent) metrics_.GetCounter("content_mismatch").Add();
    }

    // 배정을 스위치 표에 얹고, 무엇이 어디서 왔는지를 기록한다. CLI·환경 변수로
    // 명시된 이름은 건드리지 않는다 — 그래서 테스트는 언제나 팔을 이긴다.
    // snippet-begin: flag-consume
    void ApplyFlagAssignment(const live::FlagSet& assigned, const char* source) {
        defense::LiveToggles toggles;
        const live::FlagSet forSwitches = defense::SplitLiveToggles(assigned, toggles);
        metaShop_ = toggles.metaShop;
        scrapEventEnabled_ = toggles.scrapEvent;
        std::string rejected;
        flagsApplied_ = options_.switches.ApplyFlags(forSwitches, &rejected);
        flagAssignment_ = assigned.Encode();
        flagSource_ = flagsApplied_ > 0 ? source : "default";
        if (!rejected.empty())
            std::fprintf(stderr, "[flags] 일부 배정을 무시했다: %s\n", rejected.c_str());
    }
    // snippet-end: flag-consume

    // 표본의 시각축. steady_clock 한 곳에서만 읽는다 — 보내는 쪽과 받는 쪽이
    // 다른 시계를 쓰면 왕복 시간이 그 두 시계의 차이만큼 편향된다.
    double NowMs(Clock::time_point now) const {
        return std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();
    }

    // snippet-begin: avatar-gate
    // 건설이 아바타 사거리 안으로 묶이는 팔. GameWorld::PlaceTowerType 자체는
    // 건드리지 않는다 — 그 함수를 직접 부르는 기존 검사들은 아바타를 모른다.
    // 로컬 제출과 원격 명령이 **같은 함수**를 지나야 규칙이 하나로 남는다.
    bool BuildAllowed(const wire::Command& command, int player) {
        if (!options_.switches.avatarGate) return true;
        if (command.type != wire::CommandType::Place) return true;
        const math::Vec2 position{wire::DecodePosition(command.x),
                                  wire::DecodePosition(command.y)};
        if (wire::BuildWithinReach(map_, avatars_.State().Cell(player), position))
            return true;
        ++buildRejectedOutOfReach_;
        return false;
    }
    // snippet-end: avatar-gate

    void PumpHost() {
        net::Bytes packet;
        net::PacketHeader framing;
        while (network_.Receive(packet)) {
            if (wire::ReadHello(packet, &framing)) {
                ackWindow_.OnReceived(framing.seq);
                rtt_.OnRemoteHeader(framing, NowMs(Clock::now()));
                baselines_.Acknowledge(framing);
                network_.AcceptLastSender();
                continue;
            }
            if (FeedAvatarInput(packet, &framing)) {
                ackWindow_.OnReceived(framing.seq);
                rtt_.OnRemoteHeader(framing, NowMs(Clock::now()));
                baselines_.Acknowledge(framing);
                continue;
            }
            wire::Command command;
            if (!wire::ReadCommand(packet, command, &framing)) continue;
            ackWindow_.OnReceived(framing.seq);
            rtt_.OnRemoteHeader(framing, NowMs(Clock::now()));
            baselines_.Acknowledge(framing);
            remoteCommands_.Accept(command.sequence, command);
        }
        wire::Command command;
        while (remoteCommands_.Poll(command)) {   // 순서대로, 정확히 한 번
            // 원격 명령도 같은 문을 지난다. 로컬 제출만 검사하면 사거리 규칙은
            // 자기 자신에게만 적용되고, 클라이언트는 맵 어디에나 지을 수 있다.
            if (!BuildAllowed(command, remoteAvatar_)) continue;
            wire::ApplyCommand(world_, command);
            metrics_.GetCounter("remote_commands").Add();
        }
    }

    void PumpClient(Clock::time_point now, double frameSeconds) {
        // 큐를 쓰는 팔에서는 매치 이름을 받기 전까지 입장 인사를 보내지 않는다.
        // 형성되지 않은 이름으로 두드려 봐야 rejected 만 쌓인다.
        if (!options_.queuePlayerId.empty() && queueMatchId_.empty()) {
            if (now - lastQueue_ >= std::chrono::milliseconds(250)) {
                const net::PacketHeader header = ackWindow_.Header(outgoingSeq_.Next());
                network_.Send(wire::WriteQueueRequest(options_.queuePlayerId,
                                                      QueueRating(), header));
                lastQueue_ = now;
                ++queuePacketsSent_;
            }
        }
        if (now - lastHello_ >= std::chrono::milliseconds(500) &&
            (options_.queuePlayerId.empty() || !queueMatchId_.empty())) {
            const net::PacketHeader header = ackWindow_.Header(outgoingSeq_.Next());
            rtt_.OnSent(header.seq, NowMs(now));
            network_.Send(options_.admissionToken.empty()
                ? wire::WriteHello(header)
                : wire::WriteAdmissionHello(
                      options_.admissionToken, header, contentHash_,
                      platform::OSCode(platform::CurrentOS()),
                      platform::ArchCode(platform::CurrentArch())));
            lastHello_ = now;
            ++helloPacketsSent_;
        }
        // 재전송 주기의 출처. fixed 팔은 100ms 그대로이고, rtt 팔은
        // rtt + 4·jitter 다 — 왕복이 끝나기 전에 다시 보내면 그것은 복구가
        // 아니라 중복이므로 왕복 시간 그 자체가 하한이다.
        const int resendMs = AppliedResendMs();
        metrics_.GetGauge("resend_ms").Set(static_cast<double>(resendMs));
        metrics_.GetGauge("rtt_ms").Set(static_cast<double>(rtt_.Rtt()));
        if (!localCommands_.Empty() &&
            now - lastResend_ >= std::chrono::milliseconds(resendMs)) {
            localCommands_.ForEachUnacked(
                [&](uint32_t sequence, const wire::Command& queued) {
                    wire::Command outgoing = queued;
                    outgoing.sequence = sequence;
                    const net::PacketHeader header = ackWindow_.Header(outgoingSeq_.Next());
                    rtt_.OnSent(header.seq, NowMs(now));
                    network_.Send(wire::WriteCommand(outgoing, header));
                    // 관측 지점. 여기가 클라이언트에서 명령 바이트가 나가는 유일한
                    // 곳이므로, 이 숫자가 0 이면 협동 클라이언트는 벙어리다.
                    // 게이트가 없던 시절 그 상태가 53개 테스트를 전부 통과했다.
                    ++commandPacketsSent_;
                });
            lastResend_ = now;
        }
        // 프레임 입력은 매 프레임 나간다. 신뢰 채널을 쓰지 않고 중복으로 메운다.
        SendAvatarInput();
        net::Bytes packet;
        std::vector<uint16_t> terrainCells;
        net::PacketHeader framing;
        while (network_.Receive(packet)) {
            if (FeedAvatarInput(packet, &framing)) {
                ackWindow_.OnReceived(framing.seq);
                rtt_.OnRemoteHeader(framing, NowMs(now));
                continue;
            }
            // 입장 승인. 여기서 받는 넷이 이 클라이언트의 매치를 정의한다 —
            // 콘텐츠 정체성 · 서버 플랫폼 · 매치 시작 시각 · 확정된 플래그 배정.
            wire::AdmissionGrant grant;
            if (wire::ReadAdmissionGrant(packet, grant, &framing)) {
                ackWindow_.OnReceived(framing.seq);
                if (!grantReceived_) ApplyAdmissionGrant(grant);
                continue;
            }
            std::string matchId;
            if (wire::ReadQueueResponse(packet, matchId, &framing)) {
                ackWindow_.OnReceived(framing.seq);
                if (queueMatchId_.empty()) {
                    queueMatchId_ = matchId;
                    std::printf("[queue] player=%s rating=%d match=%s\n",
                                options_.queuePlayerId.c_str(), QueueRating(),
                                queueMatchId_.c_str());
                    std::fflush(stdout);
                }
                continue;
            }
            if (wire::ReadTerrain(packet, terrainCells, &framing)) {
                ackWindow_.OnReceived(framing.seq);
                rtt_.OnRemoteHeader(framing, NowMs(now));
                ApplyTerrain(terrainCells);
                continue;
            }
            wire::Snapshot snapshot;
            net::Bytes restored;
            bool baselineMiss = false;
            if (!wire::ReadSnapshotPacket(packet, heldSnapshots_, snapshot, &framing,
                                          &restored, &baselineMiss)) {
                if (baselineMiss) metrics_.GetCounter("snapshot_baseline_miss").Add();
                continue;
            }
            ackWindow_.OnReceived(framing.seq);
            rtt_.OnRemoteHeader(framing, NowMs(now));
            baselines_.Acknowledge(framing);
            // 복원된 전체 바이트가 다음 차분의 기준선이다. 여기서 넣지 않으면
            // 호스트가 가리키는 seq 를 클라이언트가 갖지 못해 델타 팔이
            // 첫 장 이후로 전부 기준선 손실이 된다.
            heldSnapshots_.Insert(framing.seq, std::move(restored));
            CompareAvatarChecksum(snapshot);
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
        // 보간 지연의 출처. fixed 팔은 3.0/2.0 그대로다 — 스위치 이전의 동작이
        // 기본이어야 한다는 규약이 여기서도 그대로다.
        if (options_.switches.interpDelay == InterpDelayKind::Rtt) {
            net::InterpolationClock clock;
            clock.delayTicks = timing_.DelayTicks(rtt_.Estimator());
            clock.leadTicks = timing_.LeadTicks(rtt_.Estimator());
            replicas_.SetClock(clock);
        }
        replicas_.Advance(frameSeconds * 60.0);
    }

    // 발파로 열린 칸 목록은 **누적 전체**라 멱등이다. 목록이 자란 순간 한 번,
    // 그 뒤로는 3초마다 한 번 다시 보낸다 — 유실된 패킷을 신뢰 채널 없이
    // 메우는 가장 싼 방법이 반복 전송이다.
    // snippet-begin: send-terrain
    void MaybeSendTerrain() {
        const std::vector<uint16_t>& cells = world_.OpenedCells();
        if (cells.empty()) return;
        const bool grew = cells.size() != sentTerrainCells_;
        if (!grew && hostTick_ - lastTerrainTick_ < 180) return;
        network_.Send(wire::WriteTerrain(cells, ackWindow_.Header(outgoingSeq_.Next())));
        sentTerrainCells_ = cells.size();
        lastTerrainTick_ = hostTick_;
        ++terrainPacketsSent_;
    }
    // snippet-end: send-terrain

    // 클라이언트 쪽 적용. 맵 사본을 같은 상태로 만들고 경로를 다시 푼다 —
    // 클라이언트는 권위 시뮬을 돌리지 않지만 **그리기 위해** 경로가 필요하다.
    void ApplyTerrain(const std::vector<uint16_t>& cells) {
        ++terrainPacketsReceived_;
        bool changed = false;
        for (uint16_t packed : cells) {
            const int x = packed % map_.Columns();
            const int y = packed / map_.Columns();
            if (map_.OpenCell(x, y)) changed = true;
        }
        if (changed) map_.RecomputeRoute();
    }

    // 웨이브 시작 레일의 제어점. 경로를 성기게 뽑아 스플라인 마디로 쓴다.
    std::vector<math::Vec2> RailControls() const {
        std::vector<math::Vec2> controls;
        const size_t length = map_.RouteLength();
        if (length < 2) return controls;
        const size_t stride = length > 12 ? length / 6 : 1;
        for (size_t i = 0; i < length; i += stride) {
            const DefenseMap::Cell cell = map_.RouteCell(i);
            controls.push_back(map_.CellCenter(cell.x, cell.y));
        }
        const DefenseMap::Cell last = map_.RouteCell(length - 1);
        controls.push_back(map_.CellCenter(last.x, last.y));
        return controls;
    }

    void SendSnapshot() {
        // 잘림은 조용히 일어나면 안 되는 손실이다 — 엔티티가 클라이언트 화면에서
        // 그냥 사라지고 아무 신호도 남지 않는다. 지금은 컬렉션 상한이 게임플레이
        // 상한보다 크거나 같아 이 값이 켜지지 않지만, 그 관계가 깨지는 순간(예:
        // 웨이브당 적 상한 상향) 이 카운터가 먼저 말한다.
        bool truncated = false;
        // 프레이밍 헤더의 seq 와 기준선 보관소의 키는 **같은 수여야 한다**.
        // 갈라지면 클라이언트가 ack 한 seq 와 호스트가 보관한 seq 가 다른 장을
        // 가리키고, 그 차분은 복원되지 않는다.
        const uint16_t seq = outgoingSeq_.Next();
        const net::PacketHeader header = ackWindow_.Header(seq);
        rtt_.OnSent(seq, NowMs(Clock::now()));
        net::Bytes snapshot =
            options_.switches.snapshotMode == SnapshotMode::Delta
                ? wire::WriteSnapshotPacket(world_, hostTick_,
                                            remoteCommands_.LastDelivered(), header,
                                            baselines_, seq, &truncated, avatarFrame_,
                                            AvatarChecksum32())
                : wire::WriteSnapshot(world_, hostTick_,
                                      remoteCommands_.LastDelivered(), &truncated, header,
                                      avatarFrame_, AvatarChecksum32());
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
    // 합류 헤더의 두 끝. 시퀀스 공간은 링크마다 하나여야 한다 — 여러 곳에서
    // 각자 세면 ack 비트필드가 무엇을 확인한 것인지 말할 수 없게 된다.
    net::PacketSequencer outgoingSeq_;
    net::AckWindow       ackWindow_;
    // 델타 스냅샷의 두 끝. 호스트는 보낸 전체 스냅샷을 seq 로 보관하고(기준선),
    // 클라이언트는 복원한 전체 스냅샷을 seq 로 보관한다(다음 차분의 기준선).
    net::BaselineStore              baselines_{wire::kSnapshotBaselineSlots};
    net::SequenceBuffer<net::Bytes> heldSnapshots_{wire::kSnapshotHeldSlots};
    // 표본은 게임 트래픽에서 나온다(별도 ping 없음). 나갈 때 seq 를 적고,
    // 상대 헤더가 그 seq 를 덮으면 그 자리에서 왕복 시간이 된다.
    net::RttSampler    rtt_{64, 0.1f};
    net::AdaptiveTiming timing_;
    // 아바타 서브시뮬. 권위 시뮬(적·타워·발사체)은 여전히 호스트 하나가 소유하고
    // float 로 돈다 — 되감기는 **예측하는 부분에만** 적용된다.
    mp::Rollback avatars_;
    std::array<mp::Input, mp::kMaxInputFrames> recentInputs_{};
    size_t       recentCount_ = 0;
    uint32_t     avatarFrame_ = 0;
    int          avatarPlayers_ = 1;
    int          localAvatar_ = 0;
    int          remoteAvatar_ = 1;

    // ── 리플레이 채집 ──────────────────────────────────────────────
    bool                          capture_ = false;
    mp::GoldenChain               chain_;
    std::vector<ReplayFrame>      capturedFrames_;
    std::vector<ReplayAvatarTick> capturedAvatar_;
    mp::Input                     lastAvatarInput_;
    bool                          lastAvatarAdvanced_ = false;
    // 클라이언트가 자기 프레임의 체크섬을 기억해 두는 창. 호스트 스냅샷이
    // (프레임, 체크섬)을 실어 오면 같은 프레임끼리 맞춰 본다.
    static constexpr size_t kAvatarChecksumWindow = 256;
    std::array<uint32_t, kAvatarChecksumWindow> avatarChecksums_{};
    std::array<uint64_t, kAvatarChecksumWindow> avatarRollbacksAt_{};
    std::array<bool, kAvatarChecksumWindow>     avatarChecksumValid_{};
    uint32_t     windowEnd_ = 0;
    uint64_t     avatarComparisons_ = 0;
    uint64_t     avatarDesyncs_ = 0;
    uint64_t     avatarInputPacketsSent_ = 0;
    uint64_t     avatarInputPacketsReceived_ = 0;
    uint64_t     avatarInputBytesSent_ = 0;
    uint64_t     buildRejectedOutOfReach_ = 0;
    // 복제 뷰: 스냅샷 8개 보관, 새 타임라인에서 최신보다 3틱 뒤에서 시작, 렌더
    // 시계는 최신에 2틱보다 가까이 붙지 않는다(호스트는 3틱마다 보낸다).
    wire::SnapshotView replicas_{net::InterpolationClock{3.0, 2.0, 8}};

    DefenseCamera             camera_;
    mapgen::TerrainStats      terrain_;
    OpenAddressingHashMap<uint32_t, math::Vec2> previousProjectiles_;
    wire::SnapshotInterpolator interpolator_;
    Scheduler          scheduler_;
    FrameScratch       frameScratch_;
    AssetReloader      reloader_;
    wire::Snapshot     view_;
    Clock::time_point  lastHello_{};
    Clock::time_point  lastQueue_{};
    // ── 라이브 운영과 플랫폼 ────────────────────────────────────────
    live::FeatureFlags        flags_;
    live::EventSchedule       events_;
    live::EventModifiers      eventMods_;
    live::PatchManifest       patch_;
    tools::AssetPipeline      pipeline_;
    live::Economy             economy_;
    live::Wallet              wallet_;
    platform::PlatformProfile platformProfile_{};
    platform::RuntimeBudget   budget_{};
    uint64_t                  contentHash_ = 0;
    int                       cookedCount_ = 0;
    int                       registeredAssets_ = 0;
    int64_t                   matchStartUnix_ = 0;
    bool                      externalEvents_ = false;
    bool                      externalFlags_ = false;
    int                       flagsApplied_ = 0;
    std::string               flagAssignment_;
    const char*               flagSource_ = "default";
    int64_t                   scrapEarned_ = 0;
    bool                      grantReceived_ = false;
    // 운영 토글이 소비되는 자리. 이벤트 일정을 고치지 않고도 시즌 보너스를
    // 끄고(scrap_weekend) 상점을 닫을 수 있어야(meta_shop) 운영이 성립한다.
    bool                      metaShop_ = false;
    bool                      scrapEventEnabled_ = true;
    std::string        queueMatchId_;
    uint64_t           queuePacketsSent_ = 0;
    Clock::time_point  lastResend_{};
    Clock::time_point  lastSnapshot_{};
    double             accumulator_ = 0.0;
    uint64_t           helloPacketsSent_ = 0;
    uint64_t           commandPacketsSent_ = 0;
    uint64_t           commandsQueued_ = 0;
    uint64_t           snapshotsReceived_ = 0;
    uint64_t           terrainPacketsSent_ = 0;
    uint64_t           terrainPacketsReceived_ = 0;
    size_t             sentTerrainCells_ = 0;
    uint32_t           lastTerrainTick_ = 0;
    uint16_t           previousWave_ = 0;
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
    bool               externalPoses_ = false;
    bool               externalEliteTree_ = false;
    BossPoses          poses_;
    SfxBus             sfx_;
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
    app.PrintProfile();
    app.DumpTrace();
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
    int  walkTicks = 0;
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

// 셀프테스트가 클릭할 셀 목록. Classic 맵은 손으로 고른 열두 자리를 쓰고,
// 절차 맵은 시드마다 지형이 다르므로 **경로 옆의 건설 가능한 칸**을 훑어 만든다.
// 하드코딩한 좌표를 절차 맵에 쓰면 그 좌표가 바위일 때 스크립트가 영원히 헛돈다.
// snippet-begin: selftest-plan
inline std::vector<DefenseMap::Cell> SelftestPlan(const DefenseMap& map) {
    static constexpr std::array<DefenseMap::Cell, 12> kClassicPlan{{
        {6, 1}, {8, 2}, {10, 2}, {11, 4}, {13, 5}, {10, 6},
        {8, 8}, {5, 8}, {3, 9}, {5, 10}, {6, 12}, {9, 13},
    }};
    if (!map.Generated())
        return std::vector<DefenseMap::Cell>(kClassicPlan.begin(), kClassicPlan.end());

    std::vector<DefenseMap::Cell> plan;
    const size_t length = map.RouteLength();
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    for (size_t i = 1; i + 1 < length && plan.size() < GameWorld::kMaxTowers; i += 2) {
        const DefenseMap::Cell on = map.RouteCell(i);
        for (int d = 0; d < 4; ++d) {
            const int nx = on.x + dx[d], ny = on.y + dy[d];
            if (map.Tile(nx, ny) != TileType::Buildable || map.Occupied(nx, ny)) continue;
            bool taken = false;
            for (const DefenseMap::Cell& c : plan)
                if (c.x == nx && c.y == ny) { taken = true; break; }
            if (taken) continue;
            plan.push_back({nx, ny});
            break;
        }
    }
    return plan;
}
// 발파할 바위. 경로 옆에 붙은 벽만 고른다 — 아레나 구석의 바위를 뚫어 봐야
// 통행 격자에도 사격선에도 아무 영향이 없어 배선을 확인하지 못한다.
inline std::vector<DefenseMap::Cell> SelftestRockPlan(const DefenseMap& map) {
    std::vector<DefenseMap::Cell> rocks;
    if (!map.Generated()) return rocks;
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    for (size_t i = 2; i + 2 < map.RouteLength() && rocks.size() < 6; i += 5) {
        const DefenseMap::Cell on = map.RouteCell(i);
        for (int d = 0; d < 4; ++d) {
            const int nx = on.x + dx[d], ny = on.y + dy[d];
            if (!map.Rock(nx, ny)) continue;
            if (nx <= 0 || ny <= 0 || nx >= map.Columns() - 1 || ny >= map.Rows() - 1) continue;
            rocks.push_back({nx, ny});
            break;
        }
    }
    return rocks;
}
// snippet-end: selftest-plan

// 스크립트가 이 자리에 세울 타워의 숫자 키. 기본은 넷을 돌려 쓰는 예전 규칙
// 그대로이고, AI 팔이 켜졌을 때만 지원 타워가 끼어든다.
//
// 레이더를 **셋** 세우는 이유는 은신 규칙의 구조 때문이다. 은신 개체만 나오는
// 웨이브에서는 아무 타워도 쏘지 않으므로 청각이 발동하지 않고, 오직 시야만
// 남는다. 레이더 하나로는 79칸짜리 경로의 한 토막만 덮어 나머지가 그대로 샌다.
// snippet-begin: selftest-tower-key
inline int SelftestTowerKey(size_t cursor, const Switches& switches) {
    if (switches.stealth && (cursor == 3 || cursor == 7 || cursor == 11)) return '6';
    if (switches.pathing != PathingKind::Polyline && cursor == 5) return '5';
    return static_cast<int>('1' + (cursor % 4));
}
// snippet-end: selftest-tower-key

inline SelftestReport RunDefenseHeadless(DefenseApp& app, int maxTicks) {
    SelftestReport report;
    const DefenseMap& map = app.Map();
    const std::vector<DefenseMap::Cell> plan = SelftestPlan(map);
    const std::vector<DefenseMap::Cell> rocks = SelftestRockPlan(map);
    size_t blastCursor = 0;
    size_t planCursor = 0;
    int    placedTowers = 0;
    int    stallTicks = 0;
    size_t upgradeCursor = 0;
    bool   selecting = true;

    // 스크립트는 사람 대신 **화면을** 클릭한다. 월드 좌표를 그대로 마우스에
    // 넣으면 기울인 카메라에서 엉뚱한 칸이 눌린다 — 카메라가 생겼다는 사실이
    // 여기서 드러난다. ortho2d 팔에서 Project 는 항등이라 값이 바뀌지 않는다.
    // snippet-begin: selftest-click
    auto clickAt = [&app](FrameInput& input, const math::Vec2& world) {
        const math::Vec2 screen = app.Camera().Project(world);
        input.mouseX = static_cast<int>(std::lround(screen.x));
        input.mouseY = static_cast<int>(std::lround(screen.y));
    };
    // snippet-end: selftest-click

    // 아바타를 목표 칸 쪽으로 한 걸음. 행·열 차이의 부호가 그대로 이동 축이다.
    // snippet-begin: selftest-walk
    auto stepToward = [](FrameInput& input, int32_t here, int32_t target) {
        const int32_t hx = here % mp::DetState::kCells, hy = here / mp::DetState::kCells;
        const int32_t tx = target % mp::DetState::kCells, ty = target / mp::DetState::kCells;
        if (tx > hx) input.Press('D');
        else if (tx < hx) input.Press('A');
        if (ty > hy) input.Press('S');
        else if (ty < hy) input.Press('W');
    };
    // snippet-end: selftest-walk
    const bool avatarGate = app.Options().switches.avatarGate;
    int walkTicks = 0;

    for (report.ticks = 0; report.ticks < maxTicks; ++report.ticks) {
        FrameInput frame;

        if (planCursor < plan.size()) {
            const math::Vec2 spot = map.CellCenter(plan[planCursor].x, plan[planCursor].y);
            // 사거리 팔에서는 먼저 걸어가야 한다. 걷는 동안은 클릭하지 않는다 —
            // 사거리 밖 클릭은 거부되고, 그 거부가 계획 커서를 헛돌게 한다.
            const bool reachable =
                !avatarGate ||
                mp::DetState::CellsWithinReach(app.AvatarCell(), app.AvatarCellOf(spot),
                                               wire::kBuildReach);
            if (!reachable) {
                stepToward(frame, app.AvatarCell(), app.AvatarCellOf(spot));
                ++walkTicks;
            } else {
                // 계획한 종류를 고르고(숫자 키) 그 셀을 클릭한다. 골드가 모자라면
                // 호스트가 명령을 거절하고 타워 수가 늘지 않으므로 다음 틱에 다시 시도한다.
                frame.Press(SelftestTowerKey(planCursor, app.Options().switches));
                clickAt(frame, spot);
                frame.mouseClicked = true;
            }
        } else if (!app.View().towers.empty()) {
            // 남는 골드는 강화에 쓴다. 한 틱은 선택(클릭), 다음 틱은 'U'.
            const auto& towers = app.View().towers;
            const auto& tower = towers[upgradeCursor % towers.size()];
            clickAt(frame, tower.pos);
            if (selecting) {
                frame.mouseClicked = true;
            } else {
                frame.Press('U');
                ++upgradeCursor;
            }
            selecting = !selecting;
        }

        // 절차 지형에서는 발파와 철거도 스크립트가 실제로 돌린다. 돌지 않는
        // 경로는 배선이 끊겨도 아무 검사가 알아채지 못한다.
        // snippet-begin: selftest-terrain-script
        if (map.Generated() && report.ticks > 600) {
            if (blastCursor < rocks.size() && report.ticks % 300 == 150) {
                const math::Vec2 target = map.CellCenter(rocks[blastCursor].x, rocks[blastCursor].y);
                ++blastCursor;
                frame = FrameInput{};
                clickAt(frame, target);
                frame.Press('B');
            } else if (report.ticks == 1801 && !app.View().towers.empty()) {
                frame = FrameInput{};                       // 한 틱: 선택
                clickAt(frame, app.View().towers.front().pos);
                frame.mouseClicked = true;
            } else if (report.ticks == 1802) {
                frame = FrameInput{};                       // 다음 틱: 철거 → 잔해
                frame.Press('X');
            }
        }
        // snippet-end: selftest-terrain-script

        app.Step(frame, static_cast<double>(kDefenseFixedDt));

        const int towers = app.World().TowerCount();
        if (planCursor < plan.size()) {
            if (towers > placedTowers) {
                placedTowers = towers;
                ++planCursor;
                stallTicks = 0;
            } else if (++stallTicks > 900) {
                // 15초를 눌러도 서지 않는 칸은 골드 부족이 아니라 규칙이 막은
                // 자리다(잔해가 눌러앉았거나 연결성 검사가 거절한다). 다음 칸으로.
                ++planCursor;
                stallTicks = 0;
            }
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

    report.walkTicks = walkTicks;
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
    // 절차 맵은 파일에서 오지 않는다. "외부 콘텐츠를 읽었다"는 판정은 맵을 빼고
    // 웨이브·밸런스·스프라이트로만 묻는다 — 그러지 않으면 map=procgen 은 언제나
    // 콘텐츠 실패로 보고된다.
    const bool contentOk = app.Skin().externalAssets == 10 && app.ExternalWaves() &&
                           app.ExternalBalance() && app.ExternalPoses() &&
                           (app.ExternalMap() || app.Map().Generated());
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
        std::printf("[selftest] avatar frame=%u cell=%d checksum=0x%08X stalls=%llu "
                    "rollbacks=%llu input_packets=%llu input_bytes=%llu desync=%llu/%llu\n",
                    app.AvatarFrame(), app.AvatarCell(), app.AvatarChecksum32(),
                    static_cast<unsigned long long>(app.AvatarStalls()),
                    static_cast<unsigned long long>(app.AvatarRollbacks()),
                    static_cast<unsigned long long>(app.AvatarInputPacketsSent()),
                    static_cast<unsigned long long>(app.AvatarInputBytesSent()),
                    static_cast<unsigned long long>(app.AvatarDesyncs()),
                    static_cast<unsigned long long>(app.AvatarComparisons()));
        app.PrintMetrics();
        app.PrintProfile();
        app.DumpTrace();

        const bool ok = report.connected && report.snapshotsReceived > 0 &&
                        report.commandPacketsSent > 0 && report.commandAck > 0 &&
                        report.renderedPixels && report.protocolOk && contentOk;
        return ok ? 0 : 1;
    }

    std::printf("[selftest] result=%s ticks=%d towers=%d wave=%d packet=%zu "
                "protocol=%s render=%s assets=%d/10 content=%s stomps=%u\n",
                report.won ? "CLEAR" : (report.lost ? "LOST" : "TIMEOUT"),
                report.ticks, report.towers, report.wave, report.packetBytes,
                report.protocolOk ? "OK" : "FAIL",
                report.renderedPixels ? "OK" : "BLANK",
                app.Skin().externalAssets, content, app.World().Stomps());
    // AI 팔이 하나라도 켜져 있으면 그 팔들이 **실제로 돌았는지**를 수로 남긴다.
    // 도달성 게이트는 헤더가 제품에서 도달 가능한지만 묻고, 도달 가능한 코드가
    // 한 줄도 실행되지 않는 상태는 잡지 못한다. 여기 다섯 수가 전부 0 이면
    // 배선은 있는데 게임은 예전 그대로라는 뜻이다.
    const Switches& ab = app.Options().switches;
    // 아바타 서브시뮬은 어느 팔에서도 돈다. 그 수가 없으면 avatar_gate 두 팔의
    // 차이도, avatar_predict 두 팔의 차이도 밖에서 읽을 수 없다.
    std::printf("[selftest] avatar frame=%u cell=%d checksum=0x%08X stalls=%llu "
                "rollbacks=%llu walk_ticks=%d gate_rejected=%llu input_packets=%llu "
                "input_bytes=%llu desync=%llu/%llu\n",
                app.AvatarFrame(), app.AvatarCell(), app.AvatarChecksum32(),
                static_cast<unsigned long long>(app.AvatarStalls()),
                static_cast<unsigned long long>(app.AvatarRollbacks()),
                report.walkTicks,
                static_cast<unsigned long long>(app.BuildRejectedOutOfReach()),
                static_cast<unsigned long long>(app.AvatarInputPacketsSent()),
                static_cast<unsigned long long>(app.AvatarInputBytesSent()),
                static_cast<unsigned long long>(app.AvatarDesyncs()),
                static_cast<unsigned long long>(app.AvatarComparisons()));
    if (app.World().GridNav() || ab.steering || ab.stealth ||
        ab.eliteBrain != EliteBrainKind::Plain)
        std::printf("[selftest] ai field_builds=%zu searches=%zu melee=%u slams=%u reveal=%u\n",
                    app.World().Field().Builds(), app.World().Field().Searches(),
                    app.World().MeleeTicks(), app.World().Slams(),
                    app.World().RevealTicks());
    if (app.Map().Generated())
        std::printf("[selftest] terrain route=%zu opened=%zu debris_live=%zu debris_settled=%zu "
                    "target_rebuilds=%zu\n",
                    app.World().RouteLength(), app.World().OpenedCells().size(),
                    app.World().Debris().Live(),
                    static_cast<size_t>(app.World().DebrisSettled()),
                    app.World().Targets().Rebuilds());
    app.PrintMetrics();
    app.PrintProfile();
    app.DumpTrace();

    // 이 단언들은 전부 "제품의 진짜 프레임 루프가 돌았는가"를 묻는다.
    const bool ok = report.won && report.protocolOk && report.renderedPixels &&
                    report.towers > 0 && contentOk;
    return ok ? 0 : 1;
}

// ── 리플레이 ───────────────────────────────────────────────────────
// 기록과 재생이 **같은 빈 프로필 저장소**에서 출발하게 하는 경로. 실행 파일 옆의
// selftest_profile.wal 을 그대로 쓰면 앞선 기록이 커밋한 지갑을 재생이 물려받아
// 시작 골드가 달라지고, 첫 틱부터 갈린다. 리플레이 파일 옆에 두는 이유는 두
// 실행이 같은 하나를 가리키게 하는 가장 단순한 규칙이기 때문이다.
inline std::string ReplayProfilePath(const std::string& replayPath) {
    return replayPath + ".wal";
}

inline int RunDefenseRecord(DefenseApp& app, const DefenseOptions& options) {
    app.EnableReplayCapture(options.chainStride);
    const SelftestReport report = RunDefenseHeadless(app, options.selftestTicks);

    Replay replay;
    replay.mode = static_cast<uint8_t>(options.mode);
    replay.players = static_cast<uint8_t>(app.AvatarPlayers());
    replay.localPlayer = static_cast<uint8_t>(app.LocalAvatarIndex());
    replay.prediction = app.Options().switches.avatarPredict ? 1 : 0;
    replay.stride = options.chainStride;
    replay.matchUnix = app.MatchStartUnix();
    replay.contentHash = app.ContentHash();
    // **배정이 끝난 뒤의** 27개 전부. flags.txt 가 정한 팔까지 여기 들어간다.
    replay.switches = app.Options().switches.Assignments();
    replay.frames = app.CapturedFrames();
    replay.avatar = app.CapturedAvatar();
    replay.chain = app.Chain();
    replay.chain.Seal();

    if (replay.frames.empty() || replay.chain.Ticks() == 0) {
        std::fprintf(stderr, "[record] 기록할 프레임이 없다\n");
        return 1;
    }
    size_t fileBytes = 0;
    if (!WriteReplayFile(options.recordPath, replay, &fileBytes)) {
        std::fprintf(stderr, "[record] 쓸 수 없다: %s\n", options.recordPath.c_str());
        return 1;
    }
    std::printf("[record] path=%s frames=%zu ticks=%u marks=%zu stride=%u "
                "chain=0x%016llx avatar_frames=%u mode=%u players=%u predict=%u\n",
                options.recordPath.c_str(), replay.frames.size(), replay.chain.Ticks(),
                replay.chain.Marks().size(), replay.stride,
                static_cast<unsigned long long>(replay.chain.Chain()), app.AvatarFrame(),
                static_cast<unsigned>(replay.mode), static_cast<unsigned>(replay.players),
                static_cast<unsigned>(replay.prediction));
    std::printf("[record] wave=%d towers=%d bytes=%zu switches=%s\n", report.wave,
                report.towers, fileBytes, replay.switches.c_str());
    app.PrintMetrics();
    return 0;
}

// 기록된 입력만으로 같은 체인을 다시 만든다. 이 함수가 이 그룹의 게이트다.
// snippet-begin: verify-replay
inline int RunDefenseVerify(const DefenseOptions& options) {
    // 재생 중에는 팔을 명령줄에서 고를 수 없다. 파일이 27개 전부를 들고 있고,
    // 여기서 하나를 덮으면 기록과 다른 게임을 돌린 뒤 그 차이를 결정성 결함으로
    // 보고하게 된다. 조용히 무시하지 않는 이유는 스위치 규약 4번과 같다 —
    // 무시된 스위치는 사람이 검사했다고 믿는 것을 검사하지 않게 만든다.
    if (!options.switches.explicitNames.empty()) {
        std::fprintf(stderr,
                     "[replay] 재생은 팔을 파일에서 읽는다 — --ab / MEGA_AB 를 함께 "
                     "줄 수 없다(명시된 것: %s)\n",
                     options.switches.explicitNames.c_str());
        return 2;
    }
    Replay replay;
    std::string error;
    if (!ReadReplayFile(options.verifyPath, replay, &error)) {
        std::fprintf(stderr, "[replay] %s: %s\n", options.verifyPath.c_str(), error.c_str());
        return 1;
    }
    if (replay.mode == static_cast<uint8_t>(RunMode::Client)) {
        std::fprintf(stderr, "[replay] 클라이언트 기록은 재생하지 않는다 — 권위 월드가 "
                             "없어 재구성할 상태가 없다\n");
        return 1;
    }

    DefenseOptions fresh;
    fresh.mode = static_cast<RunMode>(replay.mode);
    fresh.port = options.port;
    fresh.selftest = true;
    fresh.selftestTicks = static_cast<int>(replay.frames.size());
    fresh.matchUnixOverride = replay.matchUnix;
    fresh.chainStride = replay.stride;
    fresh.profilePath = ReplayProfilePath(options.verifyPath);
    std::string switchError;
    if (!fresh.switches.ParseList(replay.switches, &switchError)) {
        std::fprintf(stderr, "[replay] 스위치를 되살릴 수 없다: %s\n", switchError.c_str());
        return 1;
    }
    if (!TruncateFile(fresh.profilePath)) {
        std::fprintf(stderr, "[replay] 프로필 저장소를 비울 수 없다: %s\n",
                     fresh.profilePath.c_str());
        return 1;
    }

    DefenseApp app;
    if (!app.Init(fresh)) return 1;
    app.EnableReplayCapture(replay.stride);
    for (const ReplayFrame& frame : replay.frames)
        app.Step(DefenseApp::FromReplayFrame(frame), static_cast<double>(kDefenseFixedDt));

    mp::GoldenChain rebuilt = app.Chain();
    rebuilt.Seal();
    const mp::ChainDiff diff = mp::CompareChains(replay.chain, rebuilt);

    // 아바타 자취는 체인 안에도 이미 들어 있다. 따로 대조하는 이유는 **진단**이다 —
    // 체인만 있으면 "몇 번째 틱에서 갈렸다"까지고, 여기까지 오면 "정수 서브시뮬이
    // 갈렸는가, 아니면 부동소수 권위 시뮬이 갈렸는가"가 함께 나온다.
    const std::vector<ReplayAvatarTick>& track = app.CapturedAvatar();
    uint32_t avatarBadTick = 0;
    if (track.size() != replay.avatar.size()) {
        avatarBadTick = static_cast<uint32_t>(
            (track.size() < replay.avatar.size() ? track.size() : replay.avatar.size()) + 1);
    } else {
        for (size_t i = 0; i < track.size(); ++i) {
            if (track[i].checksum == replay.avatar[i].checksum &&
                track[i].advanced == replay.avatar[i].advanced &&
                track[i].local == replay.avatar[i].local)
                continue;
            avatarBadTick = static_cast<uint32_t>(i) + 1;
            break;
        }
    }

    const LockstepReport lockstep = VerifyWithLockstep(replay);

    const bool ok = !diff.diverged && avatarBadTick == 0 && lockstep.ok;
    std::printf("[replay] result=%s ticks=%u/%u frames=%zu chain=0x%016llx/0x%016llx "
                "stride=%u\n",
                ok ? "MATCH" : "DIVERGED", rebuilt.Ticks(), replay.chain.Ticks(),
                replay.frames.size(),
                static_cast<unsigned long long>(replay.chain.Chain()),
                static_cast<unsigned long long>(rebuilt.Chain()), replay.stride);
    if (diff.diverged)
        std::printf("[replay] chain diverged: last_agree=%u suspect=%u exact=%s\n",
                    diff.lastAgreeTick, diff.suspectTick, diff.exact ? "yes" : "no(구간)");
    if (avatarBadTick != 0)
        std::printf("[replay] avatar track diverged at tick %u\n", avatarBadTick);
    std::printf("[replay] lockstep=%s frames=%u stalls=%u fault=%s tick=%u\n",
                lockstep.ok ? "OK" : "FAIL", lockstep.frames, lockstep.stalls,
                LockstepFaultName(lockstep.fault), lockstep.faultTick);
    if (lockstep.fault == LockstepFault::Checksum)
        std::printf("[replay] lockstep checksum expected=0x%016llx actual=0x%016llx\n",
                    static_cast<unsigned long long>(lockstep.expected),
                    static_cast<unsigned long long>(lockstep.actual));
    return ok ? 0 : 1;
}
// snippet-end: verify-replay

// 진입점이 부르는 단 하나의 함수. games/ 에는 이 호출과 인자 파싱만 남는다.
inline int RunDefense(const DefenseOptions& options) {
    // 이 저장소의 진단은 전부 한국어다. 기본 코드페이지 콘솔에서 깨져 나오는
    // 실패 메시지는 무시당하고, 무시당하는 진단은 없는 것과 같다.
    platform::EnableUtf8Console();
    DefenseOptions effective = options;
    // 재생은 자기 옵션을 파일에서 만든다 — 여기서 만든 것을 쓰면 재생이 기록과
    // 다른 팔·다른 시계로 돌고, 그 차이가 결정성 결함으로 보고된다.
    if (!effective.verifyPath.empty() && !effective.recordPath.empty()) {
        std::fprintf(stderr, "--record 와 --verify-replay 는 함께 쓸 수 없다\n");
        return 2;
    }
    if (!effective.verifyPath.empty()) return RunDefenseVerify(effective);
    if (!effective.recordPath.empty()) {
        effective.selftest = true;
        effective.profilePath = ReplayProfilePath(effective.recordPath);
        if (!TruncateFile(effective.profilePath)) {
            std::fprintf(stderr, "[record] 프로필 저장소를 비울 수 없다: %s\n",
                         effective.profilePath.c_str());
            return 1;
        }
        DefenseApp recorder;
        if (!recorder.Init(effective)) return 1;
        return RunDefenseRecord(recorder, effective);
    }
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
