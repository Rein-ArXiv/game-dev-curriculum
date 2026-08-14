#include "scenes/welcome_scene.h"

#include "raylib.h"
#include "raygui.h"

namespace ailab {

void WelcomeScene::DrawWorld(int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    int x = cx + 32;
    int y = cy + 28;
    Color title{235, 235, 235, 255};
    Color body{180, 184, 192, 255};

    DrawText("ai_lab — 게임 AI 원리 측정 / 시각화", x, y, 24, title);
    y += 44;
    DrawText("ai 시리즈의 알고리즘을 같은 그리드 위에서 실행해 비교한다.", x, y, 16, body);
    y += 40;

    const char* lines[] = {
        "Pathfind  : A* / Dijkstra / Greedy / JPS 를 같은 맵에서 비교.",
        "            open(파랑) / closed(회색) / path(노랑) + expand 노드 수 측정.",
        "FlowField : cost -> integration(Dijkstra) -> flow 벡터장. 대량 유닛 이동.",
        "Steering  : Seek / Arrive / Wander / Pursue + 궤적(trail).",
        "Boids     : Separation / Alignment / Cohesion 가중치 슬라이더.",
        "Influence : stamp / spread / decay 영향력 맵 히트맵.",
        "",
        "조작: 좌클릭 드래그 = 벽 그리기,  우클릭 드래그 = 벽 지우기.",
        "      각 Scene 패널의 슬라이더 / 콤보 / 버튼으로 알고리즘을 토글한다.",
    };
    for (const char* l : lines) {
        DrawText(l, x, y, 16, body);
        y += 26;
    }
}

void WelcomeScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 240, 18}, "ai_lab");
    y += 24;
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 240, 18}, "상단 탭으로 Scene 전환");
}

}  // namespace ailab
