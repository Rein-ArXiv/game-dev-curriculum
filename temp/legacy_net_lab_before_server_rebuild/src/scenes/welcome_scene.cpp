#include "scenes/welcome_scene.h"

#include "raylib.h"
#include "raygui.h"

namespace netlab {

void WelcomeScene::DrawWorld(int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    int x = cx + 32, y = cy + 28;
    Color title{235, 235, 235, 255}, body{180, 184, 192, 255};

    DrawText("net_lab — 멀티플레이어 원리 측정 / 시각화", x, y, 24, title);
    y += 44;
    DrawText("multiplayer 시리즈의 트레이드오프를 인터랙티브하게 관찰한다.", x, y, 16, body);
    y += 40;

    const char* lines[] = {
        "Rollback    : Delay vs Rollback 비교. 인공 지연/지터로 예측·재시뮬 관찰.",
        "              롤백 횟수 / 평균 재시뮬 깊이 측정. (multiplayer part01)",
        "Lockstep    : 결정론 시뮬 2클라 동시 진행 + 디싱크 주입 -> 상태 해시 갈라짐.",
        "Matchmaking : MMR 큐 + Progressive Widening. 대기시간 vs 매치품질. (part02)",
        "LagSim      : 패킷 지연/손실/재정렬 + 보간 vs 외삽(예측) 비교. (part01/04)",
        "",
        "각 Scene 우측 패널의 슬라이더 / 콤보 / 버튼으로 파라미터를 토글한다.",
        "하단 푸터에 프레임 ms / fps 가 표시된다.",
    };
    for (const char* l : lines) { DrawText(l, x, y, 16, body); y += 26; }
}

void WelcomeScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 240, 18}, "net_lab");
    y += 24;
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 240, 18}, "상단 탭으로 Scene 전환");
}

}  // namespace netlab
