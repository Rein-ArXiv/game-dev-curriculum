// 콘텐츠 로더는 games/defense/content.hpp 에서 include/game/defense_content.hpp
// 로 올라갔다(P0-3 계층 규칙). 테스트가 게임 폴더를 인클루드 루트로 여는 통로가
// 사라졌으므로, 여기서 검증하는 코드는 제품이 링크하는 바로 그 코드다.
#include "game/defense_content.hpp"
#include "test_util.h"

#include <filesystem>
#include <fstream>

using namespace mega;
using namespace mega::game;

namespace {

class TempContent {
public:
    TempContent() {
        path_ = std::filesystem::temp_directory_path() /
                "mega_defense_content_test.txt";
    }
    ~TempContent() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    void Write(const char* text) {
        std::ofstream output(path_, std::ios::trunc);
        output << text;
    }
    std::string Path() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

void TestWaveFileValidationAndFallback() {
    const std::vector<WaveDef> fallback = {
        {2, 10, 20, 3, 0.5f, EnemyType::Normal},
    };
    TempContent file;
    bool external = false;

    file.Write("# authored waves\n"
               "4 20 30 7 0.25 fast\n"
               "1 80 15 30 1.0 boss\n");
    auto waves = defense::LoadWaveFile(file.Path(), fallback, &external);
    CHECK(external);
    CHECK_EQ(waves.size(), 2u);
    CHECK(waves[0].type == EnemyType::Fast);
    CHECK(waves[1].type == EnemyType::Boss);

    file.Write("0 20 30 7 0.25 fast\n");
    waves = defense::LoadWaveFile(file.Path(), fallback, &external);
    CHECK(!external);
    CHECK_EQ(waves.size(), fallback.size());
    CHECK_EQ(waves[0].count, fallback[0].count);

    file.Write("4 20 30 7 0.25 unknown\n");
    waves = defense::LoadWaveFile(file.Path(), fallback, &external);
    CHECK(!external);
    CHECK_EQ(waves.size(), fallback.size());
}

void TestBalanceScriptValidationAndApplication() {
    TempContent file;
    bool external = false;
    file.Write(
        "start_gold = 500\n"
        "core_hp = 300\n"
        "enemy_hp_multiplier = 1.2\n"
        "enemy_speed_multiplier = 0.8\n"
        "reward_multiplier = 1.5\n");
    const auto balance = defense::LoadBalanceScript(file.Path(), &external);
    CHECK(external);
    CHECK_EQ(balance.startGold, 500);
    CHECK(balance.coreHp == 300.0f);

    std::vector<WaveDef> waves{
        {2, 100, 20, 10, 0.5f, EnemyType::Normal},
    };
    waves = defense::ApplyBalance(std::move(waves), balance);
    CHECK(waves[0].hp > 119.9f && waves[0].hp < 120.1f);
    CHECK(waves[0].speed > 15.9f && waves[0].speed < 16.1f);
    CHECK_EQ(waves[0].reward, 15);

    file.Write("enemy_hp_multiplier = 9\n");
    const auto fallback = defense::LoadBalanceScript(file.Path(), &external);
    CHECK(!external);
    CHECK(fallback.enemyHpMultiplier == 1.0f);

    file.Write("while 1 { x = 1 }\n");
    defense::LoadBalanceScript(file.Path(), &external);
    CHECK(!external);
}

}  // namespace

static void RunTests() {
    TestWaveFileValidationAndFallback();
    TestBalanceScriptValidationAndApplication();
}

TEST_MAIN("defense_content")
