// Step 1 (Skeleton) 헤드리스 검증.
//
// 이 step 이 추가한 것 중 창 없이 부를 수 있는 것은 chrome 의 레이아웃 계산이다.
// `ComputeLayout` 은 화면 크기와 패널 폭만으로 탭바 / 캔버스 / 패널 / 푸터 네
// 사각형을 나누는 순수 함수다 — 모든 Scene 이 받는 캔버스 좌표가 여기서 나온다.
//
// Scene 생명주기(OnEnter/OnExit), DragHandle 의 capture 정책, Welcome 강체의
// 튕김은 마우스 입력과 창이 있어야 도는 코드라 여기서 다루지 않는다. step01
// 문서가 그 경계를 명시한다.

#include "ui/scene_chrome.h"

#include "step_cases.h"

#include <cmath>
#include <iostream>

namespace {

int failures = 0;

#define CHECK(expr)                                                          \
    do {                                                                     \
        if (!(expr)) {                                                       \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':' \
                      << __LINE__ << ")\n";                                  \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

// 네 사각형이 화면을 *빈틈없이, 겹치지 않게* 나누는가.
void CheckPartition(const char* label, int screenW, int screenH, int panelW) {
    const physlab::LayoutRects L =
        physlab::ComputeLayout(screenW, screenH, panelW);

    // snippet-begin: layout-partition
    // 가로 — 캔버스와 패널이 화면 폭을 나눠 갖고, 패널은 캔버스 바로 오른쪽이다.
    CHECK(L.canvas.x == 0.0f);
    CHECK(std::fabs(L.canvas.width + L.panel.width - (float)screenW) < 1e-4f);
    CHECK(std::fabs(L.panel.x - (L.canvas.x + L.canvas.width)) < 1e-4f);
    CHECK(std::fabs(L.panel.width - (float)panelW) < 1e-4f);

    // 세로 — 탭바 / 본문 / 푸터가 위에서 아래로 정확히 맞물린다. 한 픽셀이라도
    // 벌어지면 Scene 이 받는 캔버스 높이가 화면과 어긋난다.
    CHECK(L.tabBar.y == 0.0f);
    CHECK(std::fabs(L.canvas.y - (L.tabBar.y + L.tabBar.height)) < 1e-4f);
    CHECK(std::fabs(L.footer.y - (L.canvas.y + L.canvas.height)) < 1e-4f);
    CHECK(std::fabs(L.footer.y + L.footer.height - (float)screenH) < 1e-4f);
    CHECK(L.canvas.y == L.panel.y);
    CHECK(L.canvas.height == L.panel.height);
    // snippet-end: layout-partition

    // 탭바와 푸터는 화면 폭 전체를 쓴다.
    CHECK(L.tabBar.x == 0.0f);
    CHECK(std::fabs(L.tabBar.width - (float)screenW) < 1e-4f);
    CHECK(L.footer.x == 0.0f);
    CHECK(std::fabs(L.footer.width - (float)screenW) < 1e-4f);

    std::cout << "  " << label << ": canvas " << L.canvas.width << 'x'
              << L.canvas.height << " at (" << L.canvas.x << ',' << L.canvas.y
              << ")  panel x=" << L.panel.x << " w=" << L.panel.width << '\n';
}

// (1) 여러 창 크기에서 분할 규약이 유지되는가.
void TestPartition() {
    CheckPartition("1280x720/280", 1280, 720, 280);
    CheckPartition("800x600/200", 800, 600, 200);
    CheckPartition("1920x1080/320", 1920, 1080, 320);
}

// (2) main 이 실제로 쓰는 창(1280x720, 패널 280)의 값을 리터럴로 못 박는다.
//     탭바 32 / 푸터 24 라는 상수가 바뀌면 여기서 걸린다.
void TestShippedWindow() {
    const physlab::LayoutRects L = physlab::ComputeLayout(1280, 720, 280);
    CHECK(L.tabBar.height == 32.0f);
    CHECK(L.footer.height == 24.0f);
    CHECK(L.canvas.x == 0.0f);
    CHECK(L.canvas.y == 32.0f);
    CHECK(L.canvas.width == 1000.0f);
    CHECK(L.canvas.height == 664.0f);   // 720 - 32 - 24
    CHECK(L.panel.x == 1000.0f);
    CHECK(L.panel.width == 280.0f);
    CHECK(L.footer.y == 696.0f);
    std::cout << "  shipped window: tabBar h=" << L.tabBar.height
              << " canvas " << L.canvas.width << 'x' << L.canvas.height
              << " footer y=" << L.footer.y << '\n';
}

// (3) 퇴화 입력 — 패널이 없거나 화면을 통째로 차지해도 분할 규약은 그대로다.
void TestDegeneratePanel() {
    CheckPartition("no panel", 1024, 768, 0);
    CheckPartition("panel == screen", 1024, 768, 1024);

    const physlab::LayoutRects none = physlab::ComputeLayout(1024, 768, 0);
    CHECK(none.panel.width == 0.0f);
    CHECK(none.canvas.width == 1024.0f);
    const physlab::LayoutRects all = physlab::ComputeLayout(1024, 768, 1024);
    CHECK(all.canvas.width == 0.0f);
    CHECK(all.panel.x == 0.0f);
}

const physlab::testkit::StepCase kCases[] = {
    {"step01", "layout_partition", TestPartition},
    {"step01", "shipped_window",   TestShippedWindow},
    {"step01", "degenerate_panel", TestDegeneratePanel},
};

}  // namespace

int main(int argc, char** argv) {
    return physlab::testkit::RunStepCases(
        argc, argv, kCases, (int)(sizeof(kCases) / sizeof(kCases[0])),
        "physics chrome", &failures);
}
