// 윈도우 chrome — 상단 탭 바 + 하단 프레임 타이머 표시.
// Scene 자체는 그 둘 사이의 영역에만 그린다.

#pragma once

#include "raylib.h"

#include <string_view>
#include <vector>

namespace mathlab {

struct LayoutRects {
    Rectangle tabBar;
    Rectangle canvas;
    Rectangle panel;
    Rectangle footer;
};

LayoutRects ComputeLayout(int screenW, int screenH, int panelW);

// 탭 바 그리고 클릭 처리. 반환값은 "다음에 활성화할 인덱스".
// 클릭이 없으면 currentIdx 그대로.
int DrawTabBar(Rectangle bar,
                const std::vector<std::string_view>& labels,
                int currentIdx);

// 푸터 — 프레임 타이머의 ms / fps 표시.
void DrawFooter(Rectangle footer, float frameMs, float fps,
                  std::string_view sceneName);

// 패널 / 캔버스 배경 등 공통 chrome 그리기.
void DrawCanvasBackground(Rectangle canvas);
void DrawPanelBackground(Rectangle panel);

}  // namespace mathlab
