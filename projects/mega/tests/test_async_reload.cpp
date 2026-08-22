// asset_io=sync|async — 매치 중 에셋 다시 읽기.
//
// 시작할 때 한 번 읽는 것은 동기 IO 로 충분하다. 문제는 매치 중이다 — 스프라이트
// 열 장을 프레임 스레드에서 읽으면 그 프레임 하나가 디스크 지연만큼 통째로 멈춘다.
//
// 두 팔의 판정은 순서가 있다.
//   (a) **최종 픽셀이 같아야 한다.** 이것이 "읽기가 끝나기 전에 버퍼를 쓴다"
//       부류의 결함을 잡는다. 비동기 팔에서 완료를 기다리지 않고 디코드하면
//       스프라이트가 부분적으로 0이 되고, 그 차이가 렌더된 프레임에 남는다.
//   (b) 프레임 스레드가 붙잡히는 시간. (a) 없이 (b)만 재면 "덜 읽어서 빨라진 것"과
//       구분되지 않는다.

#include "game/asset_reload.hpp"
#include "game/defense_view.hpp"
#include "io/file.h"
#include "io/read_file.hpp"
#include "jobs/scheduler.h"
#include "platform/paths.hpp"
#include "test_util.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace mega;
using namespace mega::game;

namespace {

bool WriteWholeFile(const std::string& path, const std::string& text) {
    io::File file;
    if (!file.Open(path.c_str(), io::FileMode::Write)) return false;
    return file.WriteAt(0, text.data(), text.size()).ok;
}

// 실행 파일 옆의 진짜 에셋을 사적인 디렉터리로 복사한다. 원본을 건드리면 같은
// 디렉터리를 읽는 다른 테스트가 이 테스트에 의존하게 된다.
bool StageAssets(const std::string& source, const std::string& target) {
    for (int slot = 0; slot < DefenseSkin::kFileCount; ++slot) {
        std::string text;
        if (!io::ReadTextFile(source + "/" + DefenseSkin::FileName(slot), text, 4 * 1024 * 1024))
            return false;
        if (!WriteWholeFile(target + "/" + DefenseSkin::FileName(slot), text)) return false;
    }
    return true;
}

// 한 칸짜리 단색 PPM. 리로드가 실제로 새 바이트를 반영했는지의 표식이다.
std::string SolidPpm(int r, int g, int b) {
    char text[64];
    std::snprintf(text, sizeof(text), "P3\n1 1\n255\n%d %d %d\n", r, g, b);
    return text;
}

// 스킨으로 한 프레임을 실제로 그려 픽셀을 만든다. 스프라이트 배열을 직접
// 비교하지 않는 이유는, 반영이 화면까지 도달했는지를 묻고 싶기 때문이다.
std::vector<uint32_t> RenderWith(const DefenseSkin& skin) {
    const DefenseMap map = DefenseMap::CreateDefault();
    render::Framebuffer framebuffer;
    framebuffer.Init(kViewWidth, kViewHeight);

    wire::Snapshot view;
    view.tick = 5;
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
    for (int i = 0; i < 5; ++i) {
        wire::EnemyState enemy;
        enemy.id = static_cast<uint32_t>(100 + i);
        enemy.pos = map.CellCenter(7, i);
        enemy.hp = 10;
        enemy.maxHp = 20;
        enemy.type = static_cast<EnemyType>(i);
        view.enemies.push_back(enemy);
    }
    HudState hud;
    DrawGame(framebuffer, map, view, skin, hud, true, nullptr);

    const size_t count = static_cast<size_t>(framebuffer.Width()) * framebuffer.Height();
    return std::vector<uint32_t>(framebuffer.Data(), framebuffer.Data() + count);
}

struct ReloadRun {
    std::vector<uint32_t> pixels;
    double requestMs = 0;      // 프레임 스레드가 Request 에 붙잡힌 시간
    double totalMs = 0;        // 요청부터 반영까지
    int    pollFrames = 0;     // 반영까지 지나간 프레임 수
    int    externalAssets = 0;
    bool   busyAfterRequest = false;
};

ReloadRun Reload(AssetIoKind kind, const std::string& root, Scheduler* scheduler) {
    DefenseSkin skin;
    skin.Load(root);

    AssetReloader reloader;
    reloader.Init(scheduler, kind, root);
    CHECK(reloader.Kind() == kind);

    ReloadRun run;
    const auto begin = std::chrono::steady_clock::now();
    reloader.Request(skin);
    const auto afterRequest = std::chrono::steady_clock::now();
    run.busyAfterRequest = reloader.Busy();

    // 프레임 루프를 흉내 낸다: 매 프레임 폴링만 하고 계속 간다.
    while (reloader.Busy() && run.pollFrames < 100000) {
        reloader.Poll(skin);
        ++run.pollFrames;
    }
    reloader.Poll(skin);
    const auto end = std::chrono::steady_clock::now();

    run.requestMs = std::chrono::duration<double, std::milli>(afterRequest - begin).count();
    run.totalMs = std::chrono::duration<double, std::milli>(end - begin).count();
    run.externalAssets = skin.externalAssets;
    CHECK_EQ(reloader.Reloads(), static_cast<uint64_t>(1));
    run.pixels = RenderWith(skin);
    return run;
}

void TestBothArmsProduceTheSamePixels() {
    const std::string exeDir = platform::ExecutableDirectory();
    const std::string source = exeDir + "/assets";
    const std::string staged = exeDir + "/reload_test_assets";
    platform::EnsureDirectory(staged);
    if (!StageAssets(source, staged)) {
        // 에셋이 없는 환경에서는 이 검사가 성립하지 않는다. 조용히 통과시키지 않고
        // 그 사실을 적는다 — 아무 말 없이 지나가는 검사는 없는 검사와 같다.
        std::printf("[async_reload] SKIP: %s 에서 에셋을 읽지 못했다\n", source.c_str());
        CHECK(false);
        return;
    }

    // 리로드가 반영할 새 바이트. 원본과 확실히 다른 1x1 스프라이트로 바꾼다.
    CHECK(WriteWholeFile(staged + "/" + DefenseSkin::FileName(0), SolidPpm(17, 200, 90)));
    CHECK(WriteWholeFile(staged + "/" + DefenseSkin::FileName(9), SolidPpm(240, 30, 60)));

    Scheduler scheduler;
    Scheduler::Config config;
    config.workers = 4;
    scheduler.Start(config);

    const ReloadRun sync = Reload(AssetIoKind::Sync, staged, nullptr);
    const ReloadRun async = Reload(AssetIoKind::Async, staged, &scheduler);
    scheduler.Stop();

    std::printf("[asset_io] sync  request=%.3fms total=%.3fms poll_frames=%d assets=%d\n",
                sync.requestMs, sync.totalMs, sync.pollFrames, sync.externalAssets);
    std::printf("[asset_io] async request=%.3fms total=%.3fms poll_frames=%d assets=%d\n",
                async.requestMs, async.totalMs, async.pollFrames, async.externalAssets);

    // (a) 같은 결과. 픽셀 하나라도 다르면 두 경로가 다른 스프라이트를 만든 것이다.
    CHECK_EQ(sync.pixels.size(), async.pixels.size());
    CHECK(sync.pixels == async.pixels);
    CHECK_EQ(sync.externalAssets, DefenseSkin::kFileCount);
    CHECK_EQ(async.externalAssets, DefenseSkin::kFileCount);

    // (b) 구조적 차이: 동기 팔은 Request 안에서 전부 끝나고, 비동기 팔은 제출만
    //     하고 나온다. 시간이 아니라 이 상태 차이를 단언한다 — 타이밍은 머신
    //     부하에 흔들리지만 "프레임 스레드가 붙잡혔는가"는 흔들리지 않는다.
    CHECK(!sync.busyAfterRequest);
    CHECK(async.busyAfterRequest);
    CHECK_EQ(sync.pollFrames, 0);
    CHECK(async.pollFrames >= 1);
    // 프레임 스레드가 Request 에 붙잡힌 시간은 비동기 팔이 더 짧아야 한다.
    CHECK(async.requestMs <= sync.requestMs);
}

// 워커가 없으면 async 를 요청해도 sync 로 떨어진다 — 없는 워커에 제출하면 완료가
// 영영 오지 않고 리로드는 매달린다.
void TestAsyncFallsBackWithoutWorkers() {
    AssetReloader reloader;
    reloader.Init(nullptr, AssetIoKind::Async, platform::ExecutableDirectory() + "/assets");
    CHECK(reloader.Kind() == AssetIoKind::Sync);
    DefenseSkin skin;
    reloader.Request(skin);
    CHECK(!reloader.Busy());
    CHECK_EQ(skin.externalAssets, DefenseSkin::kFileCount);
}

// 트레이스 덤프는 io::File::WriteAt 의 제품 호출자다. 두 팔 모두 파일이 남아야 한다.
void TestTraceReportWritesThroughBothArms() {
    Scheduler scheduler;
    Scheduler::Config config;
    config.workers = 2;
    scheduler.Start(config);
    const std::string base = platform::ExecutableDirectory() + "/trace_test";
    for (AssetIoKind kind : {AssetIoKind::Sync, AssetIoKind::Async}) {
        const std::string path = base + (kind == AssetIoKind::Sync ? "_sync.tsv" : "_async.tsv");
        CHECK(WriteTraceReport(path, kind, &scheduler));
        std::string text;
        CHECK(io::ReadTextFile(path, text));
        CHECK(text.rfind("scope\tcount\ttotal_ms", 0) == 0);
    }
    scheduler.Stop();
}

}  // namespace

static void RunTests() {
    TestAsyncFallsBackWithoutWorkers();
    TestBothArmsProduceTheSamePixels();
    TestTraceReportWritesThroughBothArms();
}

TEST_MAIN("async_reload")
