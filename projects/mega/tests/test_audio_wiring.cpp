// 소리가 카메라를 지나는가 — 배선 검사.
//
// engine/audio.hpp 의 감쇠·패닝이 옳다는 것과, 제품이 그것을 **부른다**는 것은
// 다른 문장이다. 후자가 없으면 앞의 검사는 도서관에 꽂힌 책을 검사하는 것과 같다.
//
// 그래서 여기서는 제품의 진짜 프레임 루프(DefenseApp::Step)를 헤드리스로 돌리고,
// 그 프레임에 계산된 보이스의 **좌우 부호**가 카메라 기준으로 옳은지 본다.
// 장치는 열리지 않지만 게인 계산까지는 열려 있을 때와 같은 코드가 돈다 — 그것이
// SfxBus 를 장치 바깥에 둔 이유다.

#include "game/defense_app.hpp"
#include "game/sfx_bus.hpp"
#include "platform/paths.hpp"
#include "test_util.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace mega;
using namespace mega::game;

namespace {

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// ── 1. 버스 단독: 카메라가 좌우를 정한다 ────────────────────────────
void TestBusPansAroundTheCamera() {
    const DefenseMap map = DefenseMap::CreateDefault();
    platform::SfxTones tones;
    SfxBus bus;

    DefenseCamera flat;
    flat.Configure(CameraKind::Ortho2d, kViewWidth, kViewHeight);
    flat.Frame(map);
    const math::Vec2 focus = flat.Focus();

    const std::vector<SoundEvent> events{
        {{focus.x - 200.0f, focus.y}, SoundKind::Fire},
        {{focus.x + 200.0f, focus.y}, SoundKind::Fire},
        {{focus.x, focus.y}, SoundKind::Fire},
    };
    bus.Collect(events, flat, tones);
    CHECK_EQ(bus.Voices().size(), static_cast<size_t>(3));
    CHECK(bus.Voices()[0].gainL > bus.Voices()[0].gainR);     // 왼쪽
    CHECK(bus.Voices()[1].gainR > bus.Voices()[1].gainL);     // 오른쪽
    CHECK(Near(bus.Voices()[2].gainL, bus.Voices()[2].gainR)); // 정면
    // 정면이 옆보다 가깝다 → 더 크다.
    const float centre = bus.Voices()[2].gainL + bus.Voices()[2].gainR;
    const float side = bus.Voices()[0].gainL + bus.Voices()[0].gainR;
    CHECK(centre > side);
    std::printf("  ortho2d  좌=%.3f/%.3f  우=%.3f/%.3f  정면=%.3f/%.3f\n",
                bus.Voices()[0].gainL, bus.Voices()[0].gainR,
                bus.Voices()[1].gainL, bus.Voices()[1].gainR,
                bus.Voices()[2].gainL, bus.Voices()[2].gainR);

    // 거리 감쇠가 실제로 걸린다: 아주 먼 소리는 사실상 들리지 않는다.
    bus.Collect({{{focus.x, focus.y + 4000.0f}, SoundKind::Fire}}, flat, tones);
    CHECK_EQ(bus.Voices().size(), static_cast<size_t>(1));
    CHECK(bus.Voices()[0].gainL + bus.Voices()[0].gainR < 0.1f);

    // ── 기울인 카메라: 좌우는 카메라의 우측 기저가 정한다 ──────────
    DefenseCamera tilted;
    tilted.Configure(CameraKind::Tilted3d, kViewWidth, kViewHeight);
    tilted.Frame(map);
    bus.Collect({{{focus.x + 200.0f, focus.y}, SoundKind::Fire}}, tilted, tones);
    CHECK(bus.Voices()[0].gainR > bus.Voices()[0].gainL);

    // 카메라를 반 바퀴 돌리면 같은 소리가 반대쪽에서 난다. 이 한 줄이
    // "청취자가 카메라다"를 다른 어떤 단언보다 직접 말한다.
    tilted.SetYawPitch(180.0f, 58.0f);
    bus.Collect({{{focus.x + 200.0f, focus.y}, SoundKind::Fire}}, tilted, tones);
    CHECK(bus.Voices()[0].gainL > bus.Voices()[0].gainR);
    std::printf("  tilted3d yaw=180  좌=%.3f 우=%.3f\n",
                bus.Voices()[0].gainL, bus.Voices()[0].gainR);
}

// 보이스 상한. 시뮬이 한 프레임에 32개까지만 쌓고, 버스도 그 상한을 지킨다.
void TestVoiceBudget() {
    const DefenseMap map = DefenseMap::CreateDefault();
    platform::SfxTones tones;
    DefenseCamera camera;
    camera.Configure(CameraKind::Ortho2d, kViewWidth, kViewHeight);
    camera.Frame(map);

    std::vector<SoundEvent> flood;
    for (size_t i = 0; i < engine::kMaxVoices * 2; ++i)
        flood.push_back({{100.0f + static_cast<float>(i), 200.0f}, SoundKind::Fire});
    SfxBus bus;
    bus.Collect(flood, camera, tones);
    CHECK_EQ(bus.Voices().size(), engine::kMaxVoices);
    CHECK(bus.Dropped() > 0);
}

// 시뮬이 소리를 실제로 낸다. 상한도 지킨다.
void TestSimulationEmits() {
    Switches switches;
    GameWorld world;
    world.Configure(switches);
    const DefenseMap map = DefenseMap::CreateDefault();
    world.Init(map, {WaveDef{40, 40.0f, 40.0f, 5, 0.1f, EnemyType::Swarm}}, 4000, 1.0e9f);
    const DefenseMap::Cell plan[8] = {{6, 1}, {8, 2}, {10, 2}, {11, 4},
                                      {13, 5}, {10, 6}, {8, 8}, {5, 8}};
    for (const DefenseMap::Cell& cell : plan)
        world.PlaceTowerType(map.CellCenter(cell.x, cell.y), TowerType::Gatling);

    // 한 프레임에 몇 개가 겹치는가. 이 분포가 "프레임당 한 블록으로 합성한다"는
    // 결정의 근거다 — 전부 1이면 합성할 것이 없고 예전 방식으로 충분했다
    // (waveOut 은 버퍼 여럿을 줄 세우면 섞지 않고 차례로 재생한다).
    size_t histogram[10] = {0};
    size_t maxVoices = 0;
    int fires = 0, kills = 0, frames = 0;
    for (int tick = 0; tick < 2000; ++tick) {
        world.Step(1.0f / 60.0f);
        const size_t voices = world.Sounds().size();
        if (voices) ++frames;
        if (voices > maxVoices) maxVoices = voices;
        histogram[voices < 10 ? voices : 9] += 1;
        CHECK(voices <= GameWorld::kMaxSounds);
        for (const SoundEvent& event : world.Sounds()) {
            if (event.kind == SoundKind::Fire) ++fires;
            if (event.kind == SoundKind::Kill) ++kills;
        }
        world.ClearSounds();
    }
    CHECK(fires > 0);
    CHECK(kills > 0);
    CHECK(frames > 0);
    CHECK(maxVoices >= 3);      // 실제로 겹친다
    std::printf("  시뮬 출력: 발사 %d · 처치 %d (소리 난 프레임 %d, 최대 %zu)\n",
                fires, kills, frames, maxVoices);
    std::printf("  프레임당 소리 수: 1=%zu 2=%zu 3=%zu 4=%zu 5=%zu 6+=%zu\n",
                histogram[1], histogram[2], histogram[3], histogram[4], histogram[5],
                histogram[6] + histogram[7] + histogram[8] + histogram[9]);
}

// ── 2. 제품의 프레임 루프가 그 계산을 부르는가 ──────────────────────
void TestAppFrameRoutesSoundThroughCamera() {
    DefenseOptions options;
    options.selftest = true;
    options.profilePath = platform::ExecutableDirectory() + "/audio_probe.wal";
    DefenseApp app;
    CHECK(app.Init(options));

    const DefenseMap& map = app.Map();
    const math::Vec2 focus = app.Camera().Focus();
    // 화면 왼쪽 절반과 오른쪽 절반에 하나씩 세운다. 스크립트가 하는 것과 같은
    // 방식으로 — 화면을 클릭한다.
    const DefenseMap::Cell cells[2] = {{5, 8}, {13, 5}};
    for (const DefenseMap::Cell& cell : cells) {
        const int before = app.World().TowerCount();
        for (int attempt = 0; attempt < 600 && app.World().TowerCount() == before; ++attempt) {
            FrameInput frame;
            frame.Press('2');
            const math::Vec2 screen = app.Camera().Project(map.CellCenter(cell.x, cell.y));
            frame.mouseX = static_cast<int>(std::lround(screen.x));
            frame.mouseY = static_cast<int>(std::lround(screen.y));
            frame.mouseClicked = true;
            app.Step(frame, static_cast<double>(kDefenseFixedDt));
        }
    }
    CHECK_EQ(app.World().TowerCount(), 2);

    int left = 0, right = 0, mismatched = 0;
    uint64_t blocksAtStart = app.Sfx().Blocks();
    // 양쪽 타워가 한 번씩은 쏠 때까지 돈다. 합계로 끊으면 경로가 먼저 지나는
    // 쪽만 40번 세고 끝나 반대쪽이 한 번도 확인되지 않는다.
    for (int tick = 0; tick < 8000 && (left == 0 || right == 0); ++tick) {
        app.Step(FrameInput{}, static_cast<double>(kDefenseFixedDt));
        for (const SfxVoice& voice : app.Sfx().Voices()) {
            if (voice.at.x < focus.x - 30.0f) {
                if (voice.gainL > voice.gainR) ++left; else ++mismatched;
            } else if (voice.at.x > focus.x + 30.0f) {
                if (voice.gainR > voice.gainL) ++right; else ++mismatched;
            }
        }
    }
    CHECK(left > 0);
    CHECK(right > 0);
    CHECK_EQ(mismatched, 0);
    CHECK(app.Sfx().Blocks() > blocksAtStart);   // 실제로 블록이 제출됐다
    std::printf("  제품 프레임: 왼쪽 보이스 %d · 오른쪽 보이스 %d · 어긋남 %d · 블록 %llu\n",
                left, right, mismatched,
                static_cast<unsigned long long>(app.Sfx().Blocks()));

    // 소리는 프레임마다 비워진다 — 비우지 않으면 같은 총성이 매 프레임 다시 난다.
    CHECK(app.World().Sounds().empty());

    // 그리고 제품 프레임에서도 소리가 실제로 겹친다.
    size_t maxVoices = 0;
    for (int tick = 0; tick < 4000; ++tick) {
        app.Step(FrameInput{}, static_cast<double>(kDefenseFixedDt));
        maxVoices = app.Sfx().Voices().size() > maxVoices ? app.Sfx().Voices().size()
                                                          : maxVoices;
    }
    CHECK(maxVoices >= 2);
}

// 합성 비용. 보이스 수에 선형이어야 하고, 상한이 그 선형을 끊어야 한다.
void ReportMixCost() {
    platform::SfxTones tones;
    size_t frames = 0;
    const float* tone = tones.Samples(platform::Sfx::Death, frames);
    CHECK(tone != nullptr);
    CHECK(frames > 0);
    std::vector<float> left(frames), right(frames);
    for (size_t voices : {size_t(1), size_t(8), size_t(32), size_t(64)}) {
        std::vector<engine::Voice> mix(voices);
        for (engine::Voice& voice : mix) {
            voice.samples = tone;
            voice.count = frames;
            voice.gainL = 0.3f;
            voice.gainR = 0.7f;
        }
        const auto begin = std::chrono::steady_clock::now();
        size_t mixed = 0;
        for (int repeat = 0; repeat < 200; ++repeat)
            mixed = engine::MixVoices(mix.data(), mix.size(), left.data(), right.data(), frames);
        const double us = std::chrono::duration<double, std::micro>(
                              std::chrono::steady_clock::now() - begin).count() / 200.0;
        std::printf("  보이스 %2zu개 x %zu프레임 합성 = %.1fus (실제 섞인 보이스 %zu)\n",
                    voices, frames, us, mixed);
        CHECK(mixed == (voices < engine::kMaxVoices ? voices : engine::kMaxVoices));
    }
}

}  // namespace

static void RunTests() {
    TestBusPansAroundTheCamera();
    TestVoiceBudget();
    TestSimulationEmits();
    TestAppFrameRoutesSoundThroughCamera();
    ReportMixCost();
}

TEST_MAIN("audio_wiring")
