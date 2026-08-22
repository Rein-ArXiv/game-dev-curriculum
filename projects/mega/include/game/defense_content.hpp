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
#include "tools/script.hpp"

#include <algorithm>
#include <cmath>
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
