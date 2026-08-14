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

inline BalanceConfig LoadBalanceScript(const std::string& path,
                                       bool* loadedExternal = nullptr) {
    BalanceConfig fallback;
    std::string source;
    if (!io::ReadTextFile(path, source, 64 * 1024)) {
        if (loadedExternal) *loadedExternal = false;
        return fallback;
    }

    tools::Script script;
    if (!script.Run(source)) {
        if (loadedExternal) *loadedExternal = false;
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

inline bool ParseEnemyType(const std::string& text, game::EnemyType& type) {
    static constexpr const char* names[] = {
        "normal", "fast", "tank", "swarm", "boss",
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
