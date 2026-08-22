// 스프라이트 소유 — 값에서 세대 핸들로.
//
// 리로드가 생기기 전까지 DefenseSkin 은 스프라이트 열 장을 값으로 들었고, 그것으로
// 충분했다. 리소스 수명이 프로세스 수명과 같았기 때문이다. 매치 중 리로드는 그
// 전제를 깬다 — 열 장이 통째로 교체되고, 교체 전의 픽셀을 가리키던 참조가 있다면
// 그것은 이미 사라진 메모리다. 값 소유는 그 사고를 **말할 수단조차 주지 않는다**.
//
// 그래서 이 파일이 묻는 것은 셋이다.
//
//   1. 리로드 뒤 옛 핸들이 거절되는가. 슬롯 인덱스가 재사용된 뒤에도 거절되는가
//      (= 세대가 실제로 일을 하는가).
//   2. 참조를 잡고 있으면 교체 중에도 옛 픽셀이 살아 있는가. 마지막 참조가
//      사라질 때만 슬롯이 회수되는가.
//   3. 커밋 전에는 화면이 보는 것이 하나도 바뀌지 않는가(대기 목록의 계약).

#include "game/defense_view.hpp"
#include "io/file.h"
#include "io/read_file.hpp"
#include "platform/paths.hpp"
#include "test_util.h"

#include <cstdint>
#include <cstdio>
#include <string>

using namespace mega;
using namespace mega::game;

namespace {

bool WriteWholeFile(const std::string& path, const std::string& text) {
    io::File file;
    if (!file.Open(path.c_str(), io::FileMode::Write)) return false;
    return file.WriteAt(0, text.data(), text.size()).ok;
}

std::string SolidPpm(int r, int g, int b) {
    char text[64];
    std::snprintf(text, sizeof(text), "P3\n1 1\n255\n%d %d %d\n", r, g, b);
    return text;
}

// 열 슬롯 전부를 한 칸짜리 단색으로 채운 디렉터리. 슬롯마다 색이 달라야 어느
// 슬롯이 어느 세대를 보는지 픽셀만으로 구분된다.
bool StageSolid(const std::string& dir, int base) {
    platform::EnsureDirectory(dir);
    for (int slot = 0; slot < DefenseSkin::kFileCount; ++slot)
        if (!WriteWholeFile(dir + "/" + DefenseSkin::FileName(slot),
                            SolidPpm(base + slot * 3, 40 + slot * 5, 200 - slot * 7)))
            return false;
    return true;
}

uint32_t FirstPixel(const render::Sprite& sprite) {
    return sprite.px.empty() ? 0u : sprite.px[0];
}

void RunSkinTests() {
    const std::string exeDir = platform::ExecutableDirectory();
    const std::string red = exeDir + "/handle_test_red";
    const std::string blue = exeDir + "/handle_test_blue";
    CHECK(StageSolid(red, 210));
    CHECK(StageSolid(blue, 20));

    DefenseSkin skin;
    skin.Load(red);
    CHECK_EQ(skin.externalAssets, DefenseSkin::kFileCount);
    CHECK_EQ(skin.ResidentCount(), static_cast<size_t>(DefenseSkin::kFileCount));
    CHECK_EQ(skin.PendingCount(), static_cast<size_t>(0));
    CHECK_EQ(skin.AliveResources(), static_cast<size_t>(DefenseSkin::kFileCount));

    const uint32_t redTower0 = FirstPixel(skin.Tower(0));
    const uint32_t redTower1 = FirstPixel(skin.Tower(1));
    CHECK(redTower0 != 0);
    CHECK(redTower0 != redTower1);

    // 참조를 하나 잡는다(슬롯 0). 그리고 슬롯 1 은 아무도 잡지 않는다.
    const engine::Handle held = skin.Acquire(0);
    const engine::Handle loose = skin.SlotHandle(1);
    CHECK(skin.IsValid(held));
    CHECK_EQ(skin.RefCount(held), 2);          // 슬롯이 1, 여기서 잡은 것이 1
    CHECK_EQ(skin.RefCount(loose), 1);

    // ── 리로드 1회 ─────────────────────────────────────────────────
    skin.Load(blue);
    CHECK(FirstPixel(skin.Tower(0)) != redTower0);
    CHECK(FirstPixel(skin.Tower(1)) != redTower1);

    // 잡아 둔 핸들은 살아 있고 **옛 픽셀**을 본다. 이것이 레퍼런스 카운트다.
    CHECK(skin.IsValid(held));
    CHECK(skin.Resolve(held) != nullptr);
    if (skin.Resolve(held)) CHECK_EQ(FirstPixel(*skin.Resolve(held)), redTower0);
    CHECK_EQ(skin.RefCount(held), 1);
    // 아무도 잡지 않은 핸들은 커밋 순간에 회수됐다.
    CHECK(!skin.IsValid(loose));
    CHECK(skin.Resolve(loose) == nullptr);
    // 살아 있는 리소스는 새 열 장 + 붙잡힌 옛 한 장.
    CHECK_EQ(skin.AliveResources(), static_cast<size_t>(DefenseSkin::kFileCount + 1));
    CHECK_EQ(skin.ResidentCount(), static_cast<size_t>(DefenseSkin::kFileCount));

    // ── 리로드 2회: 슬롯 인덱스가 재사용된다 ───────────────────────
    // 여기서부터가 **세대**의 몫이다. 1회 리로드만으로는 옛 핸들이 거절되는 이유가
    // "슬롯이 죽었다"인지 "세대가 다르다"인지 구분되지 않는다. 인덱스가 다시
    // 살아난 뒤에도 거절되어야 세대가 일을 한 것이다.
    skin.Load(red);
    bool indexReused = false;
    uint32_t reusedGeneration = loose.generation;
    for (int slot = 0; slot < DefenseSkin::kFileCount; ++slot) {
        const engine::Handle current = skin.SlotHandle(slot);
        if (current.index != loose.index) continue;
        indexReused = true;
        reusedGeneration = current.generation;
    }
    CHECK(indexReused);                        // 그 자리가 실제로 다시 쓰였다
    CHECK(reusedGeneration != loose.generation);
    CHECK(!skin.IsValid(loose));               // 그래도 옛 핸들은 거절된다
    CHECK(skin.Resolve(loose) == nullptr);
    std::printf("  재사용된 슬롯 index=%u  옛 세대=%u  새 세대=%u\n",
                loose.index, loose.generation, reusedGeneration);

    // ── 마지막 참조를 놓으면 그때 회수된다 ─────────────────────────
    const size_t before = skin.AliveResources();
    skin.Release(held);
    CHECK(!skin.IsValid(held));
    CHECK_EQ(skin.AliveResources(), before - 1);
    CHECK_EQ(skin.AliveResources(), static_cast<size_t>(DefenseSkin::kFileCount));

    // ── 커밋 전에는 아무것도 바뀌지 않는다 ─────────────────────────
    const uint32_t visible = FirstPixel(skin.Tower(0));
    render::Sprite staged;
    CHECK(render::DecodePpmP3(SolidPpm(1, 2, 3), staged));
    skin.BeginReload();
    skin.Stage(0, std::move(staged));
    CHECK_EQ(skin.PendingCount(), static_cast<size_t>(1));
    CHECK_EQ(FirstPixel(skin.Tower(0)), visible);     // 화면은 그대로다
    // 중단(다음 BeginReload)이 대기 목록을 비우고, 스테이징한 리소스를 반납한다.
    skin.BeginReload();
    CHECK_EQ(skin.PendingCount(), static_cast<size_t>(0));
    CHECK_EQ(skin.AliveResources(), static_cast<size_t>(DefenseSkin::kFileCount));
    CHECK_EQ(FirstPixel(skin.Tower(0)), visible);

    // 커밋하면 그때 바뀐다.
    render::Sprite again;
    CHECK(render::DecodePpmP3(SolidPpm(9, 9, 9), again));
    skin.Stage(0, std::move(again));
    skin.CommitReload();
    CHECK(FirstPixel(skin.Tower(0)) != visible);
    CHECK_EQ(skin.PendingCount(), static_cast<size_t>(0));
    CHECK_EQ(skin.ResidentCount(), static_cast<size_t>(DefenseSkin::kFileCount));
    CHECK_EQ(skin.AliveResources(), static_cast<size_t>(DefenseSkin::kFileCount));
}

// 핸들 뒤에 있어도 실제로 그려지는가. 소유만 바뀌고 그림은 같아야 한다.
void TestSkinStillDraws() {
    DefenseSkin skin;
    skin.Load(platform::ExecutableDirectory() + "/assets");
    const DefenseMap map = DefenseMap::CreateDefault();
    render::Framebuffer framebuffer;
    framebuffer.Init(kViewWidth, kViewHeight);
    wire::Snapshot view;
    view.gold = 100;
    view.coreHp = 80;
    view.coreMaxHp = 100;
    view.wave = 2;
    for (int i = 0; i < 4; ++i) {
        wire::TowerState tower;
        tower.id = static_cast<uint32_t>(i + 1);
        tower.pos = map.CellCenter(6 + i, 1 + i);
        tower.type = static_cast<TowerType>(i);
        tower.tier = 1;
        view.towers.push_back(tower);
    }
    view.enemies.push_back({99, map.CellCenter(4, 4), 10.0f, 20.0f, EnemyType::Boss});
    HudState hud;
    DrawGame(framebuffer, map, view, skin, hud, true);

    const uint32_t background = render::Pack({14, 18, 23, 255});
    size_t painted = 0;
    const size_t pixels = static_cast<size_t>(framebuffer.Width()) * framebuffer.Height();
    for (size_t i = 0; i < pixels; ++i)
        if (framebuffer.Data()[i] != background) ++painted;
    CHECK(painted > 1000);
}

}  // namespace

static void RunTests() {
    RunSkinTests();
    TestSkinStillDraws();
}

TEST_MAIN("asset_handles")
