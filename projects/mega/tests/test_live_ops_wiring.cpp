// 라이브 운영 **배선** — 배정과 이벤트가 제품 안에서 실제로 소비되는가.
//
// 왜 이 파일이 필요한가. 이 저장소의 라이브 운영 테스트는 전부 서브시스템을
// 검증한다 — `test_flag_rollout` 은 `live::FeatureFlags` 의 버킷 안정성·롤아웃
// 수렴·팔 독립성을, `test_event_window` 는 `live::EventSchedule` 의 경계 시각과
// 합성 순서를, `test_meta_economy` 는 `live::Economy` 의 보존 불변식을 잰다.
// 그 서브시스템들은 이미 잘 덮여 있다. 덮이지 않은 것은 **배선**이었다:
// 그 결과를 게임에 얹는 세 자리.
//
//   1. `Switches::ApplyFlags` — 확정된 배정을 A/B 스위치 표에 얹는다.
//   2. `defense::ApplyEventModifiers` — 활성 이벤트의 수정자를 밸런스에 얹는다.
//   3. `DefenseApp::Step` 의 `@settle-scrap` — 정산에 스크랩 배율을 곱한다.
//
// 셋을 각각 무력화해도(1은 `return 0`, 2는 본문 건너뛰기, 3은 배율을 상수 1.0)
// ctest 117개가 전부 통과했다. 제품 동작은 확실히 달라진다 — 1을 죽이면
// `[flags] applied=5` 가 `applied=0` 이 되고 경로 탐색이 flowfield 에서 poly 로
// 바뀌며, 3을 죽이면 정산 스크랩이 `+416` 에서 `+208` 로 반토막 난다. 무증상이
// 아니라 아무도 안 보고 있었던 것이다. 원인은 단순하다: 어떤 테스트도 그 셋을
// 호출한 적이 없다(`grep -rn ApplyFlags tests/` 가 0행이었다).
//
// 여기서는 `test_defense_client_loop` 와 같은 방식을 쓴다 — 흉내가 아니라
// **제품의 진짜 `DefenseApp`** 을 헤드리스로 세우고, 그 앱이 실행 파일 옆의
// 진짜 `assets/flags.txt` · `assets/events.txt` 를 읽게 한 다음 결과를 묻는다.
//
// 시각은 **주입한다**. 벽시계로 이벤트를 판정하면 이 테스트는 주말에만 초록인
// 검사가 된다(`weekend_scrap` 은 주 단위로 반복한다). `DefenseOptions::
// matchUnixOverride` 가 리플레이 재생을 위해 이미 그 주입점을 갖고 있으므로
// 새로 열 것은 없다 — 리플레이가 요구한 성질이 그대로 검증 가능성이 된다.
//
// 시각 상수도 손으로 적지 않는다. 창의 좌표를 파일에서 뽑아 쓰면 운영자가
// 시즌을 옮긴 날 이 테스트가 "배선이 죽었다"고 거짓 보고하지 않는다.

#include "game/defense_app.hpp"
#include "game/defense_content.hpp"
#include "game/switches.hpp"
#include "live/economy.hpp"
#include "live/event_schedule.hpp"
#include "live/feature_flag.hpp"
#include "live/unlock_graph.hpp"
#include "test_util.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace mega;
using namespace mega::game;

namespace {

bool Near(double a, double b) { return std::fabs(a - b) < 1e-4; }

std::string AssetRoot() { return platform::ExecutableDirectory() + "/assets"; }

// ── 이벤트 창의 시각을 일정에서 뽑는다 ─────────────────────────────
// 셋이 필요하다. (a) 아무 이벤트도 없는 시각 — 배율 비교의 대조군이고, 동시에
// "이벤트 없음이 정상 경로"라는 계약의 확인이다. (b) 스크랩 배율 **하나만**
// 걸리는 시각 — 게임플레이가 대조군과 완전히 같아야 정산 차이를 배율 탓으로
// 돌릴 수 있다. (c) 적 체력·보상 수정자가 걸리는 시각(보스 러시 창).
struct EventTimes {
    int64_t quiet     = 0;   // 활성 이벤트 0개
    int64_t scrapOnly = 0;   // scrap 배율만, 나머지는 항등
    int64_t content   = 0;   // enemy_hp · reward 가 함께 걸린다
};

EventTimes PickTimes(const live::EventSchedule& schedule) {
    EventTimes out;
    int64_t earliest = 0;
    bool haveEarliest = false;
    std::vector<int64_t> candidates;
    for (const live::Event& e : schedule.All()) {
        if (!haveEarliest || e.start < earliest) { earliest = e.start; haveEarliest = true; }
        const int64_t duration = e.end - e.start;
        if (duration <= 0) continue;
        // 첫 창과 (반복이면) 두 번째 창의 앞·가운데·끝을 후보로 둔다. 창이
        // 겹치는 구간은 이 후보들 사이에서 자연히 잡힌다.
        for (int cycle = 0; cycle < 2; ++cycle) {
            if (cycle > 0 && e.period <= 0) break;
            const int64_t base = e.start + e.period * cycle;
            candidates.push_back(base);
            candidates.push_back(base + duration / 2);
            candidates.push_back(base + duration - 1);
        }
    }
    // 가장 이른 시작보다 하루 앞. `Event::ActiveAt` 은 `t < start` 를 반복
    // 여부와 무관하게 비활성으로 답하므로 여기는 언제나 조용하다.
    if (haveEarliest) out.quiet = earliest - 86400;
    for (int64_t t : candidates) {
        const live::EventModifiers m = schedule.ModifiersAt(t);
        if (out.scrapOnly == 0 && m.scrapMultiplier != 1.0f &&
            m.rewardMultiplier == 1.0f && m.enemyHpMultiplier == 1.0f &&
            m.bonusStartGold == 0 && m.blastCostDelta == 0 && !m.bossRush)
            out.scrapOnly = t;
        if (out.content == 0 && m.enemyHpMultiplier != 1.0f && m.rewardMultiplier != 1.0f)
            out.content = t;
    }
    return out;
}

// 제품과 완전히 같은 경로로 앱 하나를 세운다. 창만 없다.
DefenseOptions HeadlessOptions(const char* profilePath, int64_t matchUnix) {
    DefenseOptions options;
    options.mode = RunMode::Solo;
    options.profilePath = profilePath;
    // 0 은 "실제 시계"의 이름이다. 그 값이 들어가면 이 테스트는 주말에만
    // 초록인 검사가 된다.
    options.matchUnixOverride = matchUnix;
    return options;
}

}  // namespace

// ══ 1. 배정 → 스위치 표 ═════════════════════════════════════════════
//
// `assets/flags.txt` 가 있는 상태로 앱을 세우고, 파일이 정한 팔이 실제로 잡혔는지
// 본다. `Switches::ApplyFlags` 가 `return 0` 이 되면 여기서 죽는다.
static void TestFlagAssignmentReachesSwitches(const EventTimes& times) {
    bool externalFlags = false;
    std::string flagError;
    const live::FeatureFlags flags =
        defense::LoadFeatureFlags(AssetRoot() + "/flags.txt", &externalFlags, &flagError);
    // 파일이 폴백이면 아래 단언은 아무것도 증명하지 않는다 — 전원 기본 팔인
    // 상태와 배선이 죽은 상태가 같은 값을 내기 때문이다.
    CHECK(externalFlags);
    CHECK(flagError.empty());

    // 제품이 쓰는 것과 **같은 버킷 키**다. 여기서 다른 키를 쓰면 두 계산이
    // 서로 다른 팔을 내고, 그 차이가 배선 결함으로 보고된다.
    const live::FlagSet assigned = flags.Resolve(defense::MatchBucketId("local"));
    defense::LiveToggles toggles;
    const live::FlagSet forSwitches = defense::SplitLiveToggles(assigned, toggles);
    CHECK(forSwitches.Count() > 0);

    // 이 검사가 성립하려면 파일이 적어도 한 팔을 기본에서 옮겨야 한다. 전부
    // 기본 팔이면 "얹혔다"와 "안 얹혔다"가 같은 표를 낸다.
    Switches expected;
    for (const live::FlagAssignment& item : forSwitches.All()) {
        std::string why;
        CHECK(expected.Set(item.name, item.arm, &why));
    }
    CHECK(expected.Assignments() != Switches{}.Assignments());
    CHECK(forSwitches.Find("pathing") != nullptr);
    CHECK(expected.pathing != Switches{}.pathing);

    const char* kProfile = "mega_live_wiring_flags.wal";
    std::remove(kProfile);
    {
        DefenseApp app;
        CHECK(app.Init(HeadlessOptions(kProfile, times.quiet)));

        // 27개 스위치 **전부**를 한 줄로 대조한다. 어느 하나가 배정대로 잡히지
        // 않으면 여기서 갈린다.
        CHECK_EQ(app.Options().switches.Assignments(), expected.Assignments());
        // 그중에서도 팔이 실제로 도는 축 하나를 이름으로 못박는다. 이 줄이
        // `[switches] pathing=flowfield` 의 자동 검증이다.
        CHECK(app.Options().switches.pathing == expected.pathing);
        CHECK(app.Options().switches.pathing != Switches{}.pathing);

        CHECK_EQ(app.FlagsApplied(), static_cast<int>(forSwitches.Count()));
        CHECK_EQ(std::string(app.FlagSource()), std::string("file"));
        CHECK_EQ(app.FlagAssignment(), assigned.Encode());
        // 운영 토글은 스위치 표가 아니라 앱의 상태로 간다. 둘을 가르는 것이
        // `SplitLiveToggles` 이고, 그 분류가 무너지면 토글이 "알 수 없는
        // 스위치 이름"으로 거절되면서 applied 가 줄어든다.
        CHECK_EQ(app.MetaShopOpen(), toggles.metaShop);
        CHECK_EQ(app.ScrapEventEnabled(), toggles.scrapEvent);

        std::printf("  flags: source=%s applied=%d/%zu pathing=%s meta_shop=%d scrap_event=%d\n",
                    app.FlagSource(), app.FlagsApplied(), forSwitches.Count(),
                    app.Options().switches.pathing == PathingKind::FlowField ? "flowfield"
                    : app.Options().switches.pathing == PathingKind::AStar    ? "astar" : "poly",
                    app.MetaShopOpen() ? 1 : 0, app.ScrapEventEnabled() ? 1 : 0);
    }
    std::remove(kProfile);

    // ── CLI 가 파일을 이긴다 ────────────────────────────────────────
    // 이 계약이 없으면 위 검사를 강화한 대가로 A/B 게이트가 약해진다 —
    // `mega_defense_selftest_poly` 를 포함해 팔을 못박는 모든 검사가 서버
    // 배정에 좌우되기 때문이다. 여기서 함께 잠근다.
    const char* kCliProfile = "mega_live_wiring_flags_cli.wal";
    std::remove(kCliProfile);
    {
        DefenseOptions cli = HeadlessOptions(kCliProfile, times.quiet);
        std::string error;
        // `--ab pathing=poly` 가 지나는 것과 **같은 함수**다.
        CHECK(cli.switches.Parse("pathing=poly", &error));
        CHECK(error.empty());

        DefenseApp app;
        CHECK(app.Init(cli));
        CHECK(app.Options().switches.pathing == PathingKind::Polyline);
        // 명시된 이름 하나만 건너뛴다 — 나머지 배정은 그대로 얹힌다.
        CHECK_EQ(app.FlagsApplied(), static_cast<int>(forSwitches.Count()) - 1);
        CHECK(app.Options().switches.WasSetExplicitly("pathing"));
        std::printf("  flags: --ab pathing=poly 가 파일 배정을 이긴다 (applied=%d)\n",
                    app.FlagsApplied());
    }
    std::remove(kCliProfile);
}

// 배선 함수 자체의 계약. 위 깔때기가 파일에 의존하는 것과 달리 여기는 파일이
// 없어도 성립한다 — 두 검사가 같은 결함을 서로 다른 높이에서 잡는다.
static void TestApplyFlagsContract() {
    live::FlagSet set;
    CHECK(set.Assign("pathing", "flowfield"));
    CHECK(set.Assign("entity_store", "ecs"));
    CHECK(set.Assign("steering", "1"));

    Switches plain;
    std::string rejected;
    CHECK_EQ(plain.ApplyFlags(set, &rejected), 3);
    CHECK(rejected.empty());
    CHECK(plain.pathing == PathingKind::FlowField);
    CHECK(plain.entityStore == EntityStoreKind::Ecs);
    CHECK(plain.steering);
    // 적용은 "명시됨"이 아니다. 서버 배정이 명시로 기록되면 그 뒤에 도착한
    // CLI 가 무시된다.
    CHECK(!plain.WasSetExplicitly("pathing"));

    // 명시된 이름은 건드리지 않고, 그 이름은 적용 수에서도 빠진다.
    Switches pinned;
    CHECK(pinned.SetExplicit("pathing", "poly"));
    CHECK_EQ(pinned.ApplyFlags(set, &rejected), 2);
    CHECK(pinned.pathing == PathingKind::Polyline);
    CHECK(pinned.entityStore == EntityStoreKind::Ecs);

    // 모르는 이름·값은 기본 팔로 남고 사유가 쌓인다. 크래시하지 않는 것이 계약이다.
    live::FlagSet noisy;
    CHECK(noisy.Assign("no_such_switch", "on"));
    CHECK(noisy.Assign("pathing", "flowfeild"));
    Switches tolerant;
    rejected.clear();
    CHECK_EQ(tolerant.ApplyFlags(noisy, &rejected), 0);
    CHECK(!rejected.empty());
    CHECK(tolerant.pathing == Switches{}.pathing);

    // 운영 토글은 스위치 표에 들어가지 않는다. 그대로 먹이면 전부 거절돼
    // applied 가 0 이 되고, 그 거절 로그에 진짜 오타가 묻힌다.
    live::FlagSet mixed;
    CHECK(mixed.Assign("meta_shop", "on"));
    CHECK(mixed.Assign("scrap_weekend", "off"));
    CHECK(mixed.Assign("boss_rig_preview", "on"));
    CHECK(mixed.Assign("camera", "tilted3d"));
    defense::LiveToggles toggles;
    const live::FlagSet forSwitches = defense::SplitLiveToggles(mixed, toggles);
    CHECK(toggles.metaShop);
    CHECK(!toggles.scrapEvent);
    CHECK_EQ(forSwitches.Count(), (size_t)2);        // camera + boss_render
    Switches split;
    rejected.clear();
    CHECK_EQ(split.ApplyFlags(forSwitches, &rejected), 2);
    CHECK(rejected.empty());
    CHECK(split.camera == CameraKind::Tilted3d);
    CHECK(split.bossRender == BossRenderKind::Rig);
}

// ══ 2. 이벤트 수정자 → 밸런스 → 콘텐츠 수치 ═════════════════════════
//
// 보스 러시 창의 시각을 주입해, `enemy_hp` · `reward` 가 실제 웨이브 표까지
// 내려가는지 본다. `ApplyEventModifiers` 본문을 건너뛰면 여기서 죽는다.
static void TestEventModifiersReachContent(const EventTimes& times) {
    CHECK(times.content != 0);
    if (times.content == 0) return;

    bool externalBalance = false, externalWaves = false;
    const defense::BalanceConfig script =
        defense::LoadBalanceScript(AssetRoot() + "/balance.script", &externalBalance);
    const std::vector<WaveDef> file = defense::LoadWaveFile(
        AssetRoot() + "/waves.txt", defense::DefaultWaves(), &externalWaves);
    CHECK(externalBalance);
    CHECK(externalWaves);

    const char* kProfile = "mega_live_wiring_event.wal";
    std::remove(kProfile);
    {
        DefenseApp app;
        CHECK(app.Init(HeadlessOptions(kProfile, times.content)));
        CHECK_EQ(app.MatchStartUnix(), times.content);

        const live::EventModifiers mods = app.EventModifiers();
        // 고정물 확인 — 이 셋이 항등이면 아래 단언은 아무것도 잡지 못한다.
        CHECK(mods.enemyHpMultiplier != 1.0f);
        CHECK(mods.rewardMultiplier != 1.0f);

        // 밸런스에 얹혔는가. 오른쪽은 두 **독립한 출처**의 곱이다 —
        // balance.script 를 직접 읽은 값과 일정이 낸 수정자. 배선이 죽으면
        // 왼쪽은 스크립트 값 그대로가 되고 둘이 갈린다.
        const float hpMul = script.enemyHpMultiplier * mods.enemyHpMultiplier;
        const float rewardMul = script.rewardMultiplier * mods.rewardMultiplier;
        CHECK(Near(app.Balance().enemyHpMultiplier, hpMul));
        CHECK(Near(app.Balance().rewardMultiplier, rewardMul));
        CHECK_EQ(app.Balance().startGold,
                 script.startGold + mods.bonusStartGold +
                     live::Economy::StartGoldBonus(app.StartGoldTier()));
        // 콘텐츠 수치까지 내려갔는가. Balance() 만 보면 "밸런스에는 얹혔는데
        // 웨이브에는 안 곱해진" 상태를 구분할 수 없다.
        CHECK_EQ(app.Waves().size(), file.size());
        for (size_t i = 0; i < file.size() && i < app.Waves().size(); ++i) {
            const WaveDef& got = app.Waves()[i];
            CHECK(Near(got.hp, file[i].hp * hpMul));
            CHECK_EQ(got.reward,
                     static_cast<int>(std::lround(static_cast<double>(file[i].reward) *
                                                  rewardMul)));
            const int expectedCount =
                (mods.bossRush && file[i].type == EnemyType::Boss) ? file[i].count * 3
                                                                   : file[i].count;
            CHECK_EQ(got.count, expectedCount);
            // 속도는 어느 이벤트도 건드리지 않는다 — 스크립트 값 그대로다.
            CHECK(Near(got.speed, file[i].speed * script.enemySpeedMultiplier));
        }
        std::printf("  events: t=%lld enemy_hp=x%.2f reward=x%.2f boss_rush=%d "
                    "wave0 hp %.1f->%.1f reward %d->%d\n",
                    static_cast<long long>(times.content),
                    static_cast<double>(mods.enemyHpMultiplier),
                    static_cast<double>(mods.rewardMultiplier), mods.bossRush ? 1 : 0,
                    static_cast<double>(file[0].hp),
                    static_cast<double>(app.Waves()[0].hp),
                    file[0].reward, app.Waves()[0].reward);
    }
    std::remove(kProfile);

    // ── 조용한 시각에서는 콘텐츠가 파일 그대로다 ────────────────────
    // 이벤트가 없는 상태는 결함이 아니라 정상 경로다. 이 대조가 없으면 위
    // 단언이 "언제나 1.2배"인 상수 배선으로도 통과한다.
    const char* kQuietProfile = "mega_live_wiring_quiet_content.wal";
    std::remove(kQuietProfile);
    {
        DefenseApp app;
        CHECK(app.Init(HeadlessOptions(kQuietProfile, times.quiet)));
        CHECK(app.EventModifiers().IsIdentity());
        CHECK(Near(app.Balance().enemyHpMultiplier, script.enemyHpMultiplier));
        CHECK(Near(app.Balance().rewardMultiplier, script.rewardMultiplier));
        CHECK_EQ(app.Waves().size(), file.size());
        for (size_t i = 0; i < file.size() && i < app.Waves().size(); ++i) {
            CHECK(Near(app.Waves()[i].hp, file[i].hp * script.enemyHpMultiplier));
            CHECK_EQ(app.Waves()[i].count, file[i].count);
        }
    }
    std::remove(kQuietProfile);
}

// ══ 3. 스크랩 배율 → 매치 정산 ══════════════════════════════════════
//
// 같은 매치를 두 번 돌린다. 다른 것은 주입한 시각뿐이고, 그 시각이 정하는 것은
// 스크랩 배율 하나다(그 창에서 다른 수정자는 전부 항등이라 게임플레이가 같다).
// `@settle-scrap` 의 배율을 상수 1.0 으로 바꾸면 두 판의 정산이 같아지고 죽는다.
struct Settled {
    int64_t scrap      = 0;
    int64_t balance    = 0;
    int     score      = 0;
    int     wave       = 0;
    int     gold       = 0;
    int     ticks      = 0;
    bool    won        = false;
    bool    finished   = false;
    float   multiplier = 1.0f;
};

static Settled PlayOneMatch(int64_t matchUnix, const char* profilePath) {
    Settled out;
    std::remove(profilePath);
    {
        DefenseApp app;
        if (!app.Init(HeadlessOptions(profilePath, matchUnix))) {
            ++megatest::g_failures;
            std::printf("  FAIL PlayOneMatch: Init 실패 (%s)\n", profilePath);
            return out;
        }
        // 제품의 헤드리스 구동 그대로다. `mega_defense --selftest` 가 부르는
        // 것과 같은 함수이고, 그래서 정산은 흉내가 아니라 진짜 매치 종료다.
        const SelftestReport report = RunDefenseHeadless(app, 20000);
        out.ticks = report.ticks;
        out.won = app.World().Won();
        out.finished = app.Finished();
        out.wave = app.World().Wave();
        out.gold = app.World().Gold();
        out.score = ComputeScore(out.wave, out.gold, out.won);
        out.scrap = app.ScrapEarned();
        out.balance = app.ScrapBalance();
        // 제품이 곱해야 하는 값. 이 두 조회는 배율을 곱하는 코드와 다른
        // 자리에 있으므로, 그 코드를 죽여도 여기 값은 그대로 남는다.
        out.multiplier = app.ScrapEventEnabled()
            ? app.EventModifiers().scrapMultiplier : 1.0f;
        std::string why;
        CHECK(app.EconomyConserved(&why));
        if (!why.empty()) std::printf("  경제 보존 위반: %s\n", why.c_str());
    }
    std::remove(profilePath);
    return out;
}

static void TestSettleAppliesScrapMultiplier(const EventTimes& times) {
    CHECK(times.scrapOnly != 0);
    CHECK(times.quiet != 0);
    if (times.scrapOnly == 0 || times.quiet == 0) return;

    const Settled plain = PlayOneMatch(times.quiet, "mega_live_wiring_settle_plain.wal");
    const Settled boosted = PlayOneMatch(times.scrapOnly, "mega_live_wiring_settle_x2.wal");

    CHECK(plain.finished);
    CHECK(boosted.finished);

    // 두 판이 **같은 매치**인가. 다르면 정산 차이를 배율 탓으로 돌릴 수 없다.
    CHECK_EQ(plain.ticks, boosted.ticks);
    CHECK_EQ(plain.score, boosted.score);
    CHECK_EQ(plain.wave, boosted.wave);
    CHECK_EQ(plain.gold, boosted.gold);
    CHECK_EQ(plain.won, boosted.won);

    // 고정물 확인 — 배율이 서로 다른 두 시각이어야 비교가 성립한다.
    CHECK(Near(plain.multiplier, 1.0));
    CHECK(boosted.multiplier > 1.0f);

    // 제품이 실제로 발행한 스크랩. 오른쪽은 `@settle-scrap` 의 산식을 그대로
    // 다시 쓴 것이고, 왼쪽은 제품이 낸 값이다.
    const int64_t base =
        static_cast<int64_t>(std::max(plain.score, 0)) / 10 + (plain.won ? 50 : 0);
    CHECK(base > 0);
    CHECK_EQ(plain.scrap, base);
    CHECK_EQ(boosted.scrap,
             static_cast<int64_t>(static_cast<double>(base) *
                                  static_cast<double>(boosted.multiplier)));
    // 그리고 두 판이 정확히 배율만큼 갈린다.
    CHECK_EQ(boosted.scrap,
             static_cast<int64_t>(static_cast<double>(plain.scrap) *
                                  static_cast<double>(boosted.multiplier)));
    CHECK(boosted.scrap > plain.scrap);
    // 발행 자리는 하나다 — 잔액이 이번 판에 발행한 만큼이어야 한다.
    CHECK_EQ(plain.balance, plain.scrap);
    CHECK_EQ(boosted.balance, boosted.scrap);

    std::printf("  settle: score=%d base=%lld  x1.00 -> +%lld  x%.2f -> +%lld  (ticks=%d wave=%d won=%d)\n",
                plain.score, static_cast<long long>(base),
                static_cast<long long>(plain.scrap),
                static_cast<double>(boosted.multiplier),
                static_cast<long long>(boosted.scrap),
                plain.ticks, plain.wave, plain.won ? 1 : 0);
}

// ══ 4. 해금 선행 그래프 → 상점과 지갑 ═══════════════════════════════
//
// `assets/unlocks.txt` 가 배포되고 있고, 제품이 그것을 실제로 읽어 상점 순서와
// 지갑 검사에 얹는가. 앞의 세 자리와 같은 종류의 배선이다 — 서브시스템 테스트는
// `test_meta_economy` 가 갖고 있고, 여기서는 **제품 안에서** 묻는다.
static void TestUnlockGraphReachesProduct(const EventTimes& times) {
    bool externalUnlocks = false;
    std::string unlockError;
    const live::UnlockGraph shipped =
        defense::LoadUnlockGraph(AssetRoot() + "/unlocks.txt", &externalUnlocks, &unlockError);
    // 파일이 폴백이면 아래 단언은 아무것도 증명하지 않는다 — 선행 0개인 상태와
    // 배선이 죽은 상태가 같은 값을 내기 때문이다.
    CHECK(externalUnlocks);
    CHECK(unlockError.empty());
    CHECK(shipped.Size() > 1);

    static const char* kUnlockProfile = "live_ops_unlock.wal";
    std::remove(kUnlockProfile);
    DefenseApp app;
    if (!app.Init(HeadlessOptions(kUnlockProfile, times.quiet))) {
        ++megatest::g_failures;
        std::printf("  FAIL TestUnlockGraphReachesProduct: Init 실패\n");
        return;
    }
    ++megatest::g_checks;

    // 앱이 파일에서 온 그래프를 들고 있다.
    CHECK(app.ExternalUnlocks());
    CHECK_EQ(app.Unlocks().Size(), shipped.Size());
    CHECK(app.Unlocks().ShopOrder() == shipped.ShopOrder());

    // 상점 순서에서 모든 간선의 선행이 앞선다.
    const std::vector<uint32_t>& order = app.Unlocks().ShopOrder();
    for (size_t i = 0; i < order.size(); ++i) {
        for (uint32_t need : app.Unlocks().Prerequisites(order[i])) {
            bool earlier = false;
            for (size_t j = 0; j < i; ++j)
                if (order[j] == need) earlier = true;
            CHECK(earlier);
        }
    }

    // 선행을 건너뛴 해금은 제품 경로에서도 거절된다. 잔액은 넉넉히 준다 —
    // 거절의 이유가 돈이 아니라 선행이라는 것.
    const uint32_t leaf = order.back();
    CHECK(!app.Unlocks().Prerequisites(leaf).empty());
    CHECK(!app.UnlockTower(leaf));
    CHECK(app.OwnedUnlocks().empty());
    std::string violation;
    CHECK(app.EconomyConserved(&violation));

    std::string text;
    for (uint32_t type : order) text += std::to_string(type) + " ";
    std::printf("  제품 상점 순서: %s(파일 %zu노드)\n", text.c_str(), shipped.Size());
    std::remove(kUnlockProfile);
}

static void RunTests() {
    bool externalEvents = false;
    std::string eventError;
    const live::EventSchedule schedule =
        defense::LoadEventSchedule(AssetRoot() + "/events.txt", &externalEvents, &eventError);
    CHECK(externalEvents);
    CHECK(eventError.empty());
    CHECK(schedule.Count() > 0);

    const EventTimes times = PickTimes(schedule);
    std::printf("  창: quiet=%lld scrap_only=%lld content=%lld (이벤트 %zu개)\n",
                static_cast<long long>(times.quiet),
                static_cast<long long>(times.scrapOnly),
                static_cast<long long>(times.content), schedule.Count());
    CHECK(times.quiet > 0);
    CHECK(schedule.ActiveAt(times.quiet).empty());

    TestApplyFlagsContract();
    TestFlagAssignmentReachesSwitches(times);
    TestEventModifiersReachContent(times);
    TestSettleAppliesScrapMultiplier(times);
    TestUnlockGraphReachesProduct(times);
}

TEST_MAIN("live_ops_wiring")
