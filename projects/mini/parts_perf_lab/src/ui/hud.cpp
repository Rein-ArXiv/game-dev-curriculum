#include "ui/hud.h"

#include "platform.h"
#include "sim/kernel_dispatch.h"
#include "ui/hud_layout.h"

#include "raylib.h"
#include "raygui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ppl {

namespace {

// raygui 콤보박스용 옵션 문자열 ("a;b;c" 형식).
constexpr const char* kAllocatorOptions = "malloc;Arena;Pool;Slab";
constexpr const char* kLayoutOptions    = "SoA;AoS;Archetype";
constexpr const char* kContainerOptions = "vector;std::list;Intrusive";
// "Scalar"는 손으로 쓴 인트린식이 없는 일반 C++ 루프라는 뜻이라 "C++ Auto"로 표시한다.
constexpr const char* kSimdOptions      = "C++ Auto;SSE;AVX2";
constexpr const char* kEntityOptions    = "1k;10k;100k";

Rectangle Rect(int x, int y, int w, int h) {
    return Rectangle{static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(w), static_cast<float>(h)};
}

}  // namespace

void Hud::Init(int windowW, int windowH, int hudHeight) {
    windowW_ = windowW;
    windowH_ = windowH;
    hudH_    = hudHeight;
    hudY_    = windowH_ - hudH_;

    // raygui 기본 스타일 약간 손본다. 어두운 배경에 맞춰 텍스트 밝기.
    GuiSetStyle(DEFAULT, TEXT_SIZE, 14);
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0xE0E0E0FF);
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR,  0x14141CFF);
}

void Hud::Draw(Toggles& toggles, const FrameTimer& timer,
               std::size_t entityCount,
               const EventBenchmark& bench,
               const IAllocator& alloc,
               const ContainerBenchmark& cbench) {
    // HUD 배경
    DrawRectangle(0, hudY_, windowW_, hudH_, Color{20, 20, 28, 255});
    DrawLine(0, hudY_, windowW_, hudY_, Color{80, 80, 100, 255});

    // 좌측 영역: 핵심 수치 (폭 약 300)
    DrawStats(toggles, timer, entityCount, bench, alloc, cbench, 16, hudY_ + 10);

    // 중앙 영역: 토글 패널. 열린 드롭다운이 창을 넘지 않도록 상단을 위로 올린다.
    const int panelY = PanelOriginY(hudY_ + 12, windowH_);
    DrawTogglePanel(toggles, 320, panelY);

    // 우측 영역: 프레임타임 그래프 (폭 약 460)
    const int graphX = 820;
    const int graphY = hudY_ + 12;
    const int graphW = windowW_ - graphX - 16;
    const int graphH = hudH_ - 24;
    DrawFrameGraph(timer, graphX, graphY, graphW, graphH);
}

void Hud::DrawStats(const Toggles& toggles, const FrameTimer& timer,
                     std::size_t entityCount,
                     const EventBenchmark& bench, const IAllocator& alloc,
                     const ContainerBenchmark& cbench,
                     int x, int y) {
    const int actualFps = GetFPS();

    // 요청한 SIMD와 실제 실행 커널을 분리해 표시한다. AoS+AVX2처럼 요청이 실제
    // 경로와 다른 경우가 HUD에서 바로 보인다.
    const ResolvedKernel kernel =
        ResolveKernel(toggles.layout, toggles.simd, toggles.branchless);

    // 8줄로 고정한다. HUD 높이 140px, 기본 line spacing 15px 기준.
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
                  "ent %zu  simd %s>%s %s\n"
                  "frame %6.3f  sim+sub %6.3f\n"
                  "sim %6.3f  draw %6.3f  fps %d\n"
                  "alloc %6.3f  %llu/%llu fail %llu\n"
                  "live %llu peak %llu\n"
                  "slot %llu back %llu\n"
                  "cont %6.3f  %zu/%zu sum %.2f\n"
                  "build %s cpu-avx2 %s fma %s",
                  entityCount,
                  SimdName(toggles.simd), SimdName(kernel.effective),
                  KernelFallbackShortName(kernel.reason),
                  static_cast<double>(timer.LastFrameWallMs()),
                  static_cast<double>(timer.LastSimDrawSubmitMs()),
                  static_cast<double>(timer.LastUpdateMs()),
                  static_cast<double>(timer.LastDrawSubmitMs()),
                  actualFps,
                  static_cast<double>(bench.LastFrameMs()),
                  static_cast<unsigned long long>(bench.LastFrameAllocCount()),
                  static_cast<unsigned long long>(bench.Target()),
                  static_cast<unsigned long long>(bench.FailedCount()),
                  static_cast<unsigned long long>(alloc.LiveBytes()),
                  static_cast<unsigned long long>(alloc.PeakBytes()),
                  static_cast<unsigned long long>(alloc.SlotBytesLive()),
                  static_cast<unsigned long long>(alloc.ReservedBacking()),
                  static_cast<double>(cbench.LastFrameMs()),
                  cbench.NodeCount(), cbench.RequestedCount(),
                  static_cast<double>(cbench.LastSum()),
#if defined(PERF_LAB_AVX2)
                  "AVX2",
#elif defined(PERF_LAB_X86_SIMD)
                  "SSE",
#else
                  "portable",
#endif
                  CpuReportsAvx2() ? "y" : "n",
                  CpuReportsFma() ? "y" : "n");
    DrawText(buf, x, y, 14, RAYWHITE);

    // 무효 표본 경고: 고갈/용량 부족으로 요청량을 못 채운 결과를 시간 비교에 쓰면 안 된다.
    // 마지막 줄(build)을 경고로 대체해 HUD 높이를 넘기지 않는다.
    if (bench.Status() != BenchStatus::Valid || cbench.Status() != BenchStatus::Valid) {
        char warn[160];
        std::snprintf(warn, sizeof(warn),
                      "WARN invalid sample alloc=%s cont=%s",
                      BenchStatusName(bench.Status()),
                      BenchStatusName(cbench.Status()));
        DrawRectangle(x, y + 7 * 15, 300, 15, Color{20, 20, 28, 255});
        DrawText(warn, x, y + 7 * 15, 14, Color{255, 120, 120, 255});
    }
}

void Hud::DrawTogglePanel(Toggles& toggles, int x, int y) {
    // 콤보가 열려 있는 동안 다른 위젯의 입력을 잠가야 하므로 GuiLock/GuiUnlock으로 둘러싼다.
    // 열린 콤보 자신은 raygui 4.0 GuiDropdownBox의 `editMode || !guiLocked` 조건 덕에
    // 잠금 중에도 자기 입력을 계속 처리한다.
    //
    // 잠금만으로는 부족하다. 펼친 목록의 높이는
    //   (itemCount + 1) * (kRowH + DROPDOWN_ITEMS_SPACING) = 4 * 24 = 96
    // 이라 같은 컬럼의 아래 두세 행을 덮는데(x와 폭이 같다), 아래 행 위젯을 *나중에*
    // 그리면 목록 위에 덮어 그려진다. 그래서 열린 콤보만 두 번째 패스로 미뤄
    // 마지막에 그린다.
    const bool comboOpen = entityEdit_ || allocatorEdit_ || layoutEdit_ ||
                           containerEdit_ || simdEdit_;

    if (comboOpen) GuiLock();
    DrawTogglePanelPass(toggles, x, y, /*openPass=*/false);
    GuiUnlock();
    if (comboOpen) DrawTogglePanelPass(toggles, x, y, /*openPass=*/true);
}

void Hud::DrawTogglePanelPass(Toggles& toggles, int x, int y, bool openPass) {
    // 현재 활성 토글: entities, layout, simd, branchless, allocator, container.
    // 측정 가능한 비교 축: entities, layout, simd, branchless, allocator, container 여섯 가지.
    // (allocator는 EventBenchmark 워크로드 ms에만 영향.
    //  container는 ContainerBenchmark 워크로드 ms에만 영향.)
    //
    // raygui의 ComboBox는 active 인덱스를 in-out 인자로 받는다.
    //
    // openPass=false 는 닫힌 위젯만, true 는 열린 콤보 하나만 그린다(PanelDrawsWidget).
    // 접힌 위젯끼리는 겹치지 않으므로(행 간격 24 > 높이 22) 한 클릭이 콤보를 둘
    // 동시에 열 수 없다. 즉 두 번째 패스에서 그려지는 위젯은 최대 하나다.

    constexpr int kRowH    = kPanelRowH;
    constexpr int kLabelW  = kPanelLabelW;
    constexpr int kFieldW  = kPanelFieldW;
    int row = 0;

    auto rowY = [&](int r) { return PanelRowY(y, r); };

    // 1행: 엔티티 수
    if (PanelDrawsWidget(entityEdit_, openPass)) {
        DrawText("entities :", x, rowY(row), 14, LIGHTGRAY);
        int entityIdx = toggles.entityPresetIdx;
        if (GuiDropdownBox(Rect(x + kLabelW, rowY(row), kFieldW, kRowH),
                           kEntityOptions, &entityIdx, entityEdit_)) {
            entityEdit_ = !entityEdit_;
        }
        if (!entityEdit_) {
            toggles.entityPresetIdx =
                std::clamp(entityIdx, 0, kEntityCountPresetCount - 1);
        }
    }
    row++;

    // 2행: 할당자 — 활성. EventBenchmark 워크로드에 적용된다.
    if (PanelDrawsWidget(allocatorEdit_, openPass)) {
        DrawText("allocator:", x, rowY(row), 14, LIGHTGRAY);
        int allocIdx = (int)toggles.allocator;
        if (GuiDropdownBox(Rect(x + kLabelW, rowY(row), kFieldW, kRowH),
                           kAllocatorOptions, &allocIdx, allocatorEdit_)) {
            allocatorEdit_ = !allocatorEdit_;
        }
        if (!allocatorEdit_) {
            toggles.allocator = (Allocator)std::clamp(allocIdx, 0, kAllocatorCount - 1);
        }
    }
    row++;

    // 3행: 레이아웃 — 활성
    if (PanelDrawsWidget(layoutEdit_, openPass)) {
        DrawText("layout   :", x, rowY(row), 14, LIGHTGRAY);
        int layoutIdx = (int)toggles.layout;
        if (GuiDropdownBox(Rect(x + kLabelW, rowY(row), kFieldW, kRowH),
                           kLayoutOptions, &layoutIdx, layoutEdit_)) {
            layoutEdit_ = !layoutEdit_;
        }
        if (!layoutEdit_) {
            toggles.layout = (Layout)std::clamp(layoutIdx, 0, kLayoutCount - 1);
        }
    }
    row++;

    // 우측 컬럼: 컨테이너 / SIMD / branchless
    const int x2 = PanelColumn2X(x);
    int row2 = 0;
    auto rowY2 = [&](int r) { return PanelRowY(y, r); };

    // container 토글 — 활성. ContainerBenchmark 워크로드에 적용된다.
    if (PanelDrawsWidget(containerEdit_, openPass)) {
        DrawText("container:", x2, rowY2(row2), 14, LIGHTGRAY);
        int containerIdx = (int)toggles.container;
        if (GuiDropdownBox(Rect(x2 + kLabelW, rowY2(row2), kFieldW, kRowH),
                           kContainerOptions, &containerIdx, containerEdit_)) {
            containerEdit_ = !containerEdit_;
        }
        if (!containerEdit_) {
            toggles.container = (Container)std::clamp(containerIdx, 0, kContainerCount - 1);
        }
    }
    row2++;

    if (PanelDrawsWidget(simdEdit_, openPass)) {
        DrawText("simd     :", x2, rowY2(row2), 14, LIGHTGRAY);
        int simdIdx = (int)toggles.simd;
        if (GuiDropdownBox(Rect(x2 + kLabelW, rowY2(row2), kFieldW, kRowH),
                           kSimdOptions, &simdIdx, simdEdit_)) {
            simdEdit_ = !simdEdit_;
        }
        if (!simdEdit_) {
            toggles.simd = (SimdWidth)std::clamp(simdIdx, 0, kSimdCount - 1);
        }
    }
    row2++;

    // branchless 체크박스 — 활성. Scalar(C++ Auto) 경로에 한해 효과를 본다.
    // 콤보가 아니므로 열린 패스에는 그리지 않는다.
    if (!openPass) {
        bool branchless = toggles.branchless;
        GuiCheckBox(Rect(x2 + kLabelW, rowY2(row2), kRowH, kRowH),
                    "branchless", &branchless);
        toggles.branchless = branchless;
    }
    row2++;
}

void Hud::DrawFrameGraph(const FrameTimer& timer, int x, int y, int w, int h) {
    DrawRectangle(x, y, w, h, Color{8, 8, 12, 255});
    DrawRectangleLines(x, y, w, h, Color{60, 60, 80, 255});

    // 그래프 스케일은 최근 N 프레임의 최대값 기준 (자동 줌).
    const auto& frameWall = timer.FrameWallMs();
    const auto& draw      = timer.DrawMs();
    const std::size_t filled = timer.Filled();
    if (filled == 0) return;

    const auto& alloc     = timer.AllocMs();
    const auto& container = timer.ContainerMs();

    // 자동 스케일: 네 라인(frame wall / alloc / container)의 max 기준.
    float maxMs = 0.0f;
    for (std::size_t i = 0; i < filled; ++i) {
        if (frameWall[i] > maxMs) maxMs = frameWall[i];
        if (alloc[i] > maxMs)     maxMs = alloc[i];
        if (container[i] > maxMs) maxMs = container[i];
    }
    // 최소 16.67ms 스케일 (60fps 기준선이 보이도록). 이 기준선은 frame budget 관점이며,
    // 작은 alloc/container 차이를 가릴 수 있다(그 해석은 숫자 요약과 함께 본다).
    if (maxMs < 16.67f) maxMs = 16.67f;

    // 60fps / 30fps 기준선. 패널 안에 들어올 때만 그린다.
    auto drawHline = [&](float ms, Color c) {
        if (ms > maxMs) return;
        const float ratio = ms / maxMs;
        const int yLine = y + h - (int)(ratio * (h - 4));
        DrawLine(x, yLine, x + w, yLine, c);
    };
    drawHline(16.67f, Color{80, 200, 80, 100});   // 60fps
    drawHline(33.33f, Color{200, 200, 80, 100});  // 30fps

    const std::size_t cap = FrameTimer::kCapacity;
    const std::size_t head = timer.Head();

    // 한 채널을 폴리라인으로 그린다. ms[idx] 값을 0..maxMs 정규화해 y 좌표로.
    // x축은 sample index다(프레임률에 따라 실제 경과 시간 길이가 달라진다).
    auto drawSeries = [&](const std::array<float, FrameTimer::kCapacity>& series,
                          Color c) {
        int prevX = -1;
        int prevY = 0;
        const float denom = (filled > 1) ? static_cast<float>(filled - 1) : 1.0f;
        for (std::size_t i = 0; i < filled; ++i) {
            const std::size_t idx = (head + cap - filled + i) % cap;
            const float v = series[idx];
            const float ratio = v / maxMs;

            const int px = x + (int)((float)i * w / denom);
            const int py = y + h - (int)(ratio * (h - 4));

            if (prevX >= 0) {
                DrawLine(prevX, prevY, px, py, c);
            }
            prevX = px;
            prevY = py;
        }
    };
    (void)draw;

    // 라인 셋. frame wall은 HUD/EndDrawing 포함, alloc/container는 벤치마크 자체 측정.
    drawSeries(frameWall, Color{200, 220, 255, 220}); // frame wall
    drawSeries(alloc,     Color{ 80, 220, 220, 200}); // alloc
    drawSeries(container, Color{255, 200,  80, 200}); // container

    // 좌측 상단: max 스케일.
    char label[64];
    std::snprintf(label, sizeof(label), "max %.2f ms", maxMs);
    DrawText(label, x + 6, y + 4, 12, GRAY);

    // 우측 상단: 라인 범례.
    const int legendX = x + w - 200;
    const int legendY = y + 4;
    DrawRectangle(legendX, legendY,     8, 8, Color{200, 220, 255, 220});
    DrawText("frame wall",  legendX + 12, legendY,     10, LIGHTGRAY);
    DrawRectangle(legendX + 74, legendY, 8, 8, Color{ 80, 220, 220, 200});
    DrawText("alloc",      legendX + 86, legendY,     10, LIGHTGRAY);
    DrawRectangle(legendX + 124, legendY, 8, 8, Color{255, 200,  80, 200});
    DrawText("container",  legendX + 136, legendY,    10, LIGHTGRAY);
}

}  // namespace ppl
