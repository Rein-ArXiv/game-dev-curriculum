#pragma once

#include "alloc/iallocator.h"
#include "measure/container_benchmark.h"
#include "measure/event_benchmark.h"
#include "measure/frame_timer.h"
#include "ui/toggles.h"

#include <cstddef>

namespace ppl {

// HUD: 화면 하단 패널.
//   - 좌측: 핵심 수치 (entities, update/draw ms, fps, alloc count/bytes/ms)
//   - 중앙: 토글 패널 (raygui 콤보박스 + 체크박스)
//   - 우측: frame/alloc/container 그래프 (최근 600프레임)
//
// Draw는 raylib BeginDrawing/EndDrawing *사이에* 호출되어야 한다.
// 토글이 변경되면 Toggles 구조체의 필드가 갱신된다. 호출 측은 RequiresRestart로
// 시뮬 재초기화 필요 여부를 판단해 처리한다.
class Hud {
public:
    void Init(int windowW, int windowH, int hudHeight);

    // 매 프레임 호출. 토글 상태와 측정값을 받아 HUD를 그리고,
    // 사용자가 토글을 변경했으면 toggles에 반영한다.
    void Draw(Toggles& toggles, const FrameTimer& timer,
              std::size_t entityCount,
              const EventBenchmark& bench,
              const IAllocator& alloc,
              const ContainerBenchmark& cbench);

private:
    void DrawStats(const FrameTimer& timer, std::size_t entityCount,
                   const EventBenchmark& bench, const IAllocator& alloc,
                   const ContainerBenchmark& cbench,
                   int x, int y);
    void DrawTogglePanel(Toggles& toggles, int x, int y);
    void DrawFrameGraph(const FrameTimer& timer, int x, int y, int w, int h);

    int windowW_ = 0;
    int windowH_ = 0;
    int hudY_    = 0;
    int hudH_    = 0;

    // raygui 콤보박스 편집 모드 상태.
    // raygui는 콤보박스가 펼쳐졌을 때 상호작용을 잠그는 모달 패턴을 쓴다.
    bool allocatorEdit_ = false;
    bool layoutEdit_    = false;
    bool containerEdit_ = false;
    bool simdEdit_      = false;
    bool entityEdit_    = false;
};

}  // namespace ppl
