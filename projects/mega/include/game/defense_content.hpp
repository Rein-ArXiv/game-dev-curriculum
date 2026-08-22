// Runtime-authored defense content with validated fallback behavior.
//
// games/defense/content.hpp 에서 옮겨 왔다. 옮긴 이유는 두 가지다.
//  1) 계층 규칙(A3): 이 로더들은 디스크를 읽는다. games/ 에는 파일 API 를 부를
//     권한이 없다. 읽기는 이제 io::ReadTextFile 을 통해 io/file.cpp 의 진짜
//     시스템 콜로 내려간다 — <fstream> 사본이 게임 쪽에 흩어지지 않는다.
//  2) 제품과 테스트가 같은 코드를 보게 하려면 mega_core 안에 있어야 한다.
//     games/ 를 인클루드 루트로 열어 주는 방식은 그 자체가 분기의 통로였다.

#pragma once

#include "game/game.hpp"
#include "game/map_gen.hpp"
#include "io/read_file.hpp"
#include "live/event_schedule.hpp"
#include "live/feature_flag.hpp"
#include "tools/asset_pipeline.hpp"
#include "tools/script.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace mega::defense {

struct BalanceConfig {
    int startGold = 420;
    float coreHp = 260.0f;
    float enemyHpMultiplier = 1.0f;
    float enemySpeedMultiplier = 1.0f;
    float rewardMultiplier = 1.0f;
};

inline bool InRange(double value, double minimum, double maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

// assets/waves.txt 가 없거나 검증에 실패했을 때 쓰는 내장 웨이브 표.
// 클라이언트 진입점과 전용 서버가 각자 사본을 갖고 있었다 — 두 사본이 갈라지면
// 두 프로세스가 다른 게임을 돌리게 되므로 하나로 합쳤다.
inline std::vector<game::WaveDef> DefaultWaves() {
    return {
        {10, 12, 30, 5, 0.35f, game::EnemyType::Normal},
        {16, 14, 34, 5, 0.22f, game::EnemyType::Fast},
        { 6, 40, 30, 8, 0.45f, game::EnemyType::Tank},
        {26, 10, 36, 4, 0.14f, game::EnemyType::Swarm},
        { 1, 60, 30, 50, 1.0f, game::EnemyType::Boss},
    };
}

// 바이트 그대로 읽은 텍스트를 줄 단위로 자른다. CRLF 는 여기서 한 번만 벗긴다
// — 텍스트 모드 스트림의 암묵적 변환에 기대면 플랫폼마다 결과가 달라진다.
inline std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') current.pop_back();
            lines.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.empty()) {
        if (current.back() == '\r') current.pop_back();
        lines.push_back(current);
    }
    return lines;
}

// balance.script 는 재빌드 없이 교체할 수 있다고 광고된 파일이다 = 신뢰 경계 밖
// 입력이다. 그래서 세 가지 실패가 모두 폴백으로 흡수돼야 한다: 파일이 없다,
// 스크립트가 실행에 실패한다(구문 오류·실행 예산 초과), 결과가 범위 밖이다.
//
// diagnostic 은 뒤의 둘을 호출자가 **말할 수 있게** 하려고 있다. 폴백은 정상
// 동작이지만 "왜 폴백인가"까지 조용하면, 교체한 사람은 자기 파일이 적용된 줄
// 알고 그대로 둔다. 파일이 아예 없는 경우만 진단이 비는데, 그건 기본 구성으로
// 도는 정상 상태이지 거부가 아니기 때문이다.
inline BalanceConfig LoadBalanceScript(const std::string& path,
                                       bool* loadedExternal = nullptr,
                                       std::string* diagnostic = nullptr) {
    BalanceConfig fallback;
    if (diagnostic) diagnostic->clear();
    std::string source;
    if (!io::ReadTextFile(path, source, 64 * 1024)) {
        if (loadedExternal) *loadedExternal = false;
        return fallback;
    }

    tools::Script script;
    script.SetName(path);            // 진단 문자열이 어느 파일인지 말하게 한다
    if (!script.Run(source)) {
        if (loadedExternal) *loadedExternal = false;
        if (diagnostic) *diagnostic = script.Error();
        return fallback;
    }

    const double startGold = script.GetOr("start_gold", fallback.startGold);
    const double coreHp = script.GetOr("core_hp", fallback.coreHp);
    const double hp = script.GetOr("enemy_hp_multiplier", 1.0);
    const double speed = script.GetOr("enemy_speed_multiplier", 1.0);
    const double reward = script.GetOr("reward_multiplier", 1.0);
    const bool valid =
        InRange(startGold, 0.0, 5000.0) &&
        InRange(coreHp, 1.0, 10000.0) &&
        InRange(hp, 0.5, 1.25) &&
        InRange(speed, 0.5, 1.25) &&
        InRange(reward, 0.5, 2.0);
    if (!valid) {
        if (loadedExternal) *loadedExternal = false;
        if (diagnostic)
            *diagnostic = path + ": 계산된 밸런스 값이 허용 범위 밖이다";
        return fallback;
    }

    BalanceConfig loaded;
    loaded.startGold = static_cast<int>(std::lround(startGold));
    loaded.coreHp = static_cast<float>(coreHp);
    loaded.enemyHpMultiplier = static_cast<float>(hp);
    loaded.enemySpeedMultiplier = static_cast<float>(speed);
    loaded.rewardMultiplier = static_cast<float>(reward);
    if (loadedExternal) *loadedExternal = true;
    return loaded;
}

// assets/boss.pose 도 재빌드 없이 교체할 수 있는 파일 = 신뢰 경계 밖 입력이다.
// balance.script 와 같은 규칙을 그대로 따른다: 없으면 조용히 내장 클립, 거부되면
// 내장 클립 + 진단. **부분 반영은 없다** — 걷기만 파일이고 공격은 내장인 포즈는
// 아무도 저작한 적이 없는 조합이라, 그 상태로 도는 것이 오히려 진단을 어렵게 한다.
inline game::BossPoses LoadBossPoses(const std::string& path,
                                     bool* loadedExternal = nullptr,
                                     std::string* diagnostic = nullptr) {
    game::BossPoses fallback;
    if (diagnostic) diagnostic->clear();
    std::string text;
    if (!io::ReadTextFile(path, text, 64 * 1024)) {
        if (loadedExternal) *loadedExternal = false;
        return fallback;
    }
    game::BossPoses loaded;
    std::string why;
    if (!game::ParseBossPoses(text, loaded, &why)) {
        if (loadedExternal) *loadedExternal = false;
        if (diagnostic) *diagnostic = path + ": " + why;
        return fallback;
    }
    if (loadedExternal) *loadedExternal = true;
    return loaded;
}

inline std::vector<game::WaveDef> ApplyBalance(
        std::vector<game::WaveDef> waves, const BalanceConfig& balance) {
    for (game::WaveDef& wave : waves) {
        wave.hp *= balance.enemyHpMultiplier;
        wave.speed *= balance.enemySpeedMultiplier;
        wave.reward = static_cast<int>(
            std::lround(static_cast<double>(wave.reward) * balance.rewardMultiplier));
    }
    return waves;
}

// 정예 행동 트리의 저작 파일. balance.script · boss.pose 와 **같은 계약**이다 —
// 없으면 조용히 내장 트리, 있으면 그대로 쓰되 거부되면 내장 트리 + 진단.
// 여기서 검증하지 않는 이유는 검증기가 조립기 안에 있기 때문이다(알 수 없는
// 노드 이름 · 짝이 안 맞는 end). 이 함수는 바이트를 읽어 넘기기만 한다.
inline std::string LoadEliteTree(const std::string& path, bool* found = nullptr) {
    std::string text;
    const bool ok = io::ReadTextFile(path, text, 16 * 1024);
    if (found) *found = ok;
    return ok ? text : std::string{};
}

// 은신 규칙이 켜지면 Fast 웨이브가 Stalker 웨이브가 된다. 콘텐츠 파일을 두
// 벌 두지 않기 위한 변환이다 — waves.txt 는 `stalker` 를 직접 쓸 수도 있고,
// 그때 이 변환은 아무것도 바꾸지 않는다(이미 Stalker 다).
//
// 왜 스위치가 웨이브를 건드리는가. 은신은 **규칙**이지만 은신하는 적이 하나도
// 없으면 그 규칙은 관측되지 않는다. 규칙만 켜고 콘텐츠를 그대로 두면
// `stealth=1` 과 `stealth=0` 이 같은 게임이 되고, A/B 는 비교할 것을 잃는다.
// snippet-begin: content-hash
// 파일 바이트의 콘텐츠 해시. FNV-1a 64. 쿡 파이프라인의 입력이자 콘텐츠 정체성의
// 바닥이다 — 이 값이 원본에서 오지 않으면 버전 해시는 아무것도 증명하지 못한다.
inline uint64_t ContentHash(const std::string& bytes) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (char c : bytes) { h ^= static_cast<uint8_t>(c); h *= 0x100000001b3ull; }
    return h;
}
// snippet-end: content-hash

// assets/events.txt. balance.script 와 같은 원자적 폴백이다 — 없으면 조용히
// 기본(이벤트 0개), 있는데 거부되면 기본 + 진단. 절반만 켜진 시즌은 없다.
inline live::EventSchedule LoadEventSchedule(const std::string& path,
                                             bool* loadedExternal = nullptr,
                                             std::string* diagnostic = nullptr) {
    live::EventSchedule schedule;
    if (diagnostic) diagnostic->clear();
    std::string text;
    if (!io::ReadTextFile(path, text, 64 * 1024)) {
        if (loadedExternal) *loadedExternal = false;
        return schedule;
    }
    std::string error;
    if (!live::ParseEventSchedule(text, schedule, &error)) {
        if (loadedExternal) *loadedExternal = false;
        if (diagnostic) *diagnostic = path + ": " + error;
        return live::EventSchedule{};
    }
    if (loadedExternal) *loadedExternal = true;
    return schedule;
}

// assets/flags.txt. 거부되면 정의 0개 = 전원 기본 팔.
inline live::FeatureFlags LoadFeatureFlags(const std::string& path,
                                           bool* loadedExternal = nullptr,
                                           std::string* diagnostic = nullptr) {
    live::FeatureFlags flags;
    if (diagnostic) diagnostic->clear();
    std::string text;
    if (!io::ReadTextFile(path, text, 64 * 1024)) {
        if (loadedExternal) *loadedExternal = false;
        return flags;
    }
    std::string error;
    if (!live::ParseFlagConfig(text, flags, &error)) {
        if (loadedExternal) *loadedExternal = false;
        if (diagnostic) *diagnostic = path + ": " + error;
        return live::FeatureFlags{};
    }
    if (loadedExternal) *loadedExternal = true;
    return flags;
}

// snippet-begin: cook-assets
// 배포되는 원본 전부를 의존 관계와 함께 등록하고 굽는다. 반환값은 콘텐츠 버전
// 해시이고, 실패(순환·없는 의존)면 0 — 0 은 "발행할 수 없음"의 이름이다.
inline uint64_t CookAssets(const std::string& assetRoot, tools::AssetPipeline& pipeline,
                           int* cooked = nullptr, int* registered = nullptr,
                           std::string* diagnostic = nullptr) {
    struct Source { const char* name; size_t cap; };
    static const Source kSources[] = {
        {"level.txt", 256 * 1024}, {"waves.txt", 64 * 1024},
        {"balance.script", 64 * 1024}, {"events.txt", 64 * 1024},
        {"flags.txt", 64 * 1024}, {"boss.pose", 64 * 1024}, {"elite.bt", 16 * 1024},
        {"core.ppm", 256 * 1024},
        {"tower_cannon.ppm", 256 * 1024}, {"tower_gatling.ppm", 256 * 1024},
        {"tower_splash.ppm", 256 * 1024}, {"tower_sniper.ppm", 256 * 1024},
        {"enemy_normal.ppm", 256 * 1024}, {"enemy_fast.ppm", 256 * 1024},
        {"enemy_tank.ppm", 256 * 1024},   {"enemy_swarm.ppm", 256 * 1024},
        {"enemy_boss.ppm", 256 * 1024},
    };
    std::vector<std::string> shipped;
    for (const Source& source : kSources) {
        std::string bytes;
        // 없는 파일은 등록하지 않는다. 폴백으로 도는 것은 정상 경로이고,
        // 배포되지 않은 파일이 버전 해시에 들어가면 그 해시는 거짓말이 된다.
        if (!io::ReadTextFile(assetRoot + "/" + source.name, bytes, source.cap)) continue;
        if (!pipeline.Register(source.name, ContentHash(bytes), {})) continue;
        shipped.emplace_back(source.name);
    }
    // 원본이 하나도 없으면 발행할 것이 없다. 배포 목록 하나만으로 해시를 내면
    // "에셋이 통째로 빠진 배포"가 유효한 콘텐츠 버전이 되고, 그 버전에 대한
    // 입장 검사는 아무것도 지키지 않는다.
    if (shipped.empty()) {
        if (registered) *registered = 0;
        if (cooked) *cooked = 0;
        if (diagnostic) *diagnostic = assetRoot + ": 배포된 원본이 하나도 없다";
        return 0;
    }
    // 배포 목록은 실제로 배포된 것 전부에 의존한다. 어느 하나가 바뀌면 이것이
    // 다시 구워지고, 그 결과가 콘텐츠 버전이 된다.
    pipeline.Register("pack.index", 0x9A5Eull, shipped);
    if (registered) *registered = static_cast<int>(shipped.size());
    int rebuilt = 0;
    std::string error;
    const tools::CookStatus status = pipeline.Cook(&rebuilt, nullptr, &error);
    if (cooked) *cooked = rebuilt;
    if (status != tools::CookStatus::Ok) {
        if (diagnostic) *diagnostic = std::string(tools::CookStatusName(status)) + ": " + error;
        return 0;
    }
    if (diagnostic) diagnostic->clear();
    return pipeline.ContentVersionHash();
}
// snippet-end: cook-assets

// 활성 이벤트의 수정자를 밸런스에 얹는다. balance.script 가 적용된 뒤,
// ApplyBalance 가 웨이브에 곱해지기 **전**에 부른다.
inline BalanceConfig ApplyEventModifiers(BalanceConfig balance,
                                         const live::EventModifiers& mods) {
    balance.startGold += mods.bonusStartGold;
    if (balance.startGold < 0) balance.startGold = 0;
    balance.enemyHpMultiplier *= mods.enemyHpMultiplier;
    balance.rewardMultiplier  *= mods.rewardMultiplier;
    return balance;
}

// 보스 러시: 보스 웨이브의 개체 수를 늘린다. 웨이브 표를 두 벌 두지 않기 위한
// 변환이고, ApplyStealth 와 같은 자리에 선다.
inline std::vector<game::WaveDef> ApplyBossRush(std::vector<game::WaveDef> waves,
                                                bool enabled) {
    if (!enabled) return waves;
    for (game::WaveDef& wave : waves)
        if (wave.type == game::EnemyType::Boss) wave.count *= 3;
    return waves;
}

// snippet-begin: flag-split
// 배정에는 두 종류가 섞여 있다: A/B 스위치 이름과 운영 토글. 토글을 스위치 표에
// 그대로 먹이면 전부 "알 수 없는 이름"으로 거절되고, 그 거절 로그가 매 실행마다
// 나오면 진짜 오타가 그 안에 묻힌다. 규칙은 한 곳에만 있어야 한다 — 클라이언트와
// 전용 서버가 서로 다른 규칙을 쓰면 같은 배정이 두 프로세스에서 다른 팔이 된다.
struct LiveToggles {
    bool metaShop = false;      // 매치 밖 상점을 여는가
    bool scrapEvent = true;     // 이벤트의 스크랩 배율을 적용하는가
};

inline live::FlagSet SplitLiveToggles(const live::FlagSet& assigned, LiveToggles& toggles) {
    live::FlagSet forSwitches;
    for (const live::FlagAssignment& item : assigned.All()) {
        const bool on = item.arm == "on" || item.arm == "1";
        if (item.name == "meta_shop") { toggles.metaShop = on; continue; }
        if (item.name == "scrap_weekend") { toggles.scrapEvent = on; continue; }
        if (item.name == "boss_rig_preview") {
            // 표시 전용 팔이다. 꺼진 상태가 기본 팔(sprite)이므로 아무것도 배정하지
            // 않는다 — 기본값을 다시 적으면 그 이름이 "명시됨"처럼 보인다.
            if (on) forSwitches.Assign("boss_render", "rig");
            continue;
        }
        forSwitches.Assign(item.name, item.arm);
    }
    return forSwitches;
}
// snippet-end: flag-split

// 매치 단위 버킷 키. 플래그 배정이 플레이어마다 다르면 한 매치 안에서 두 사람이
// 서로 다른 **권위** 팔을 요구하게 된다. 매치가 단위여야 한다.
inline uint32_t MatchBucketId(const std::string& matchId) {
    uint32_t h = 2166136261u;
    for (char c : matchId) { h ^= static_cast<uint8_t>(c); h *= 16777619u; }
    return h;
}

inline std::vector<game::WaveDef> ApplyStealth(std::vector<game::WaveDef> waves,
                                               const game::Switches& switches) {
    if (!switches.stealth) return waves;
    for (game::WaveDef& wave : waves)
        if (wave.type == game::EnemyType::Fast) wave.type = game::EnemyType::Stalker;
    return waves;
}

inline game::DefenseMap LoadMapFile(const std::string& path,
                                    bool* loadedExternal = nullptr) {
    std::string text;
    std::vector<std::string> rows;
    if (io::ReadTextFile(path, text, 256 * 1024)) {
        for (std::string& row : SplitLines(text))
            if (!row.empty()) rows.push_back(std::move(row));
    }

    game::DefenseMap loaded;
    const bool valid = game::DefenseMap::FromAscii(rows, 30, 30, loaded);
    if (loadedExternal) *loadedExternal = valid;
    return valid ? loaded : game::DefenseMap::CreateDefault();
}

// 맵의 출처를 한 곳에서 고른다. classic 은 파일(없으면 내장 기본 맵), procgen 은
// 시드 하나에서 생성한 격자다. 생성이 검증에 실패하면 classic 으로 떨어진다 —
// 플레이할 수 없는 맵으로 매치를 시작하는 것보다 나은 실패는 없다.
// snippet-begin: choose-map
inline game::DefenseMap ChooseMap(const game::Switches& switches, const std::string& path,
                                  bool* loadedExternal = nullptr,
                                  game::mapgen::TerrainStats* statsOut = nullptr) {
    if (switches.map == game::MapSource::Procgen) {
        game::mapgen::TerrainParams params;
        params.seed = static_cast<uint32_t>(switches.mapSeed);
        game::DefenseMap generated;
        if (game::mapgen::Generate(params, generated, statsOut)) {
            if (loadedExternal) *loadedExternal = true;
            return generated;
        }
    }
    return LoadMapFile(path, loadedExternal);
}
// snippet-end: choose-map

inline bool ParseEnemyType(const std::string& text, game::EnemyType& type) {
    // 이름 순서가 곧 열거 값이다. 새 종류는 **뒤에만** 붙는다.
    static constexpr const char* names[] = {
        "normal", "fast", "tank", "swarm", "boss", "stalker",
    };
    for (size_t i = 0; i < std::size(names); ++i) {
        if (text == names[i]) {
            type = static_cast<game::EnemyType>(i);
            return true;
        }
    }
    return false;
}

inline std::vector<game::WaveDef> LoadWaveFile(
        const std::string& path,
        const std::vector<game::WaveDef>& fallback,
        bool* loadedExternal = nullptr) {
    std::string text;
    std::vector<game::WaveDef> waves;
    bool valid = io::ReadTextFile(path, text, 256 * 1024);

    if (valid) {
        for (std::string line : SplitLines(text)) {
            const size_t comment = line.find('#');
            if (comment != std::string::npos) line.erase(comment);
            if (line.find_first_not_of(" \t") == std::string::npos) continue;

            game::WaveDef wave;
            std::string typeName;
            std::string trailing;
            std::istringstream parser(line);
            if (!(parser >> wave.count >> wave.hp >> wave.speed >> wave.reward
                         >> wave.interval >> typeName) ||
                (parser >> trailing) ||
                !ParseEnemyType(typeName, wave.type) ||
                wave.count < 1 || wave.count > 64 ||
                !std::isfinite(wave.hp) || wave.hp < 1.0f || wave.hp > 1000.0f ||
                !std::isfinite(wave.speed) || wave.speed < 1.0f ||
                wave.speed > 300.0f ||
                wave.reward < 0 || wave.reward > 1000 ||
                !std::isfinite(wave.interval) || wave.interval < 0.03f ||
                wave.interval > 10.0f) {
                valid = false;
                break;
            }
            waves.push_back(wave);
            if (waves.size() > 100) { valid = false; break; }
        }
    }

    valid = valid && !waves.empty();
    if (loadedExternal) *loadedExternal = valid;
    return valid ? waves : fallback;
}

}  // namespace mega::defense
