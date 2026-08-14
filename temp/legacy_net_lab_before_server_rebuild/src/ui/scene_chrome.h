// 윈도우 chrome — 상단 탭 바 + 하단 프레임 타이머.
#pragma once

#include "raylib.h"

#include <string_view>
#include <vector>

namespace netlab {

struct LayoutRects {
    Rectangle tabBar;
    Rectangle canvas;
    Rectangle panel;
    Rectangle footer;
};

LayoutRects ComputeLayout(int screenW, int screenH, int panelW);
int  DrawTabBar(Rectangle bar, const std::vector<std::string_view>& labels, int currentIdx);
void DrawFooter(Rectangle footer, float frameMs, float fps, std::string_view sceneName);
void DrawCanvasBackground(Rectangle canvas);
void DrawPanelBackground(Rectangle panel);

}  // namespace netlab
