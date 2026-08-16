// 골격(창·레이아웃·그리드 좌표계)의 순수 계산부. 창을 열지 않고 돈다.
//
// 이 파일이 겨냥하는 것은 Scene 이 올라가기 전의 경계다.
//   ui/scene_chrome.cpp  ComputeLayout — 탭바·캔버스·패널·푸터의 사각형 분할
//   grid/grid.cpp        Layout / CellCenter / ScreenToCell / Resize
// 둘 다 raylib 함수를 부르지 않는 계산이라 InitWindow 없이 검증된다. 렌더
// 진입점(DrawTabBar·DrawFooter·Grid::Draw)과 마우스를 읽는 HandleMouseEdit 는
// 여기서 부르지 않는다 — 그것은 창이 있어야 판정되는 부분이다.

#include "grid/grid.h"
#include "ui/scene_chrome.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':'  \
                      << __LINE__ << ")\n";                                   \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

// 화면 크기를 훑는 검사에서 어느 크기가 깨졌는지 알 수 없으면 진단이 불가능하다.
#define CHECK_CTX(expr, ctx)                                                  \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::cerr << "CHECK failed: " #expr << " [" << ctx << "] ("       \
                      << __FILE__ << ':' << __LINE__ << ")\n";                \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

constexpr int kPanelW = 280;

ailab::LayoutRects Layout1280x720() {
    return ailab::ComputeLayout(1280, 720, kPanelW);
}

// 캔버스와 패널이 겹치지 않고, 네 사각형이 화면을 빈틈 없이 덮는가.
// 창 크기가 바뀌어도 성립해야 하므로 여러 크기를 훑는다.
void TestLayoutRectsAreDisjoint() {
    for (int screenW : {320, 641, 800, 1280, 1920}) {
        for (int screenH : {200, 480, 721, 1080}) {
            const ailab::LayoutRects L =
                ailab::ComputeLayout(screenW, screenH, kPanelW);
            const std::string ctx =
                std::to_string(screenW) + "x" + std::to_string(screenH);

            // snippet-begin: layout-disjoint
            // 캔버스 오른쪽 끝과 패널 왼쪽 끝이 정확히 맞닿는다 — 한 픽셀도
            // 겹치지 않고, 한 픽셀도 비지 않는다.
            CHECK_CTX(L.canvas.x + L.canvas.width == L.panel.x, ctx);
            CHECK_CTX(L.canvas.width + L.panel.width == (float)screenW, ctx);
            CHECK_CTX(L.panel.width == (float)kPanelW, ctx);

            // 세로도 같다: 탭바 → 콘텐츠 → 푸터가 이어 붙는다.
            CHECK_CTX(L.tabBar.y == 0.0f, ctx);
            CHECK_CTX(L.tabBar.y + L.tabBar.height == L.canvas.y, ctx);
            CHECK_CTX(L.canvas.y + L.canvas.height == L.footer.y, ctx);
            CHECK_CTX(L.footer.y + L.footer.height == (float)screenH, ctx);

            // 캔버스와 패널은 같은 띠를 나눠 쓴다.
            CHECK_CTX(L.canvas.y == L.panel.y, ctx);
            CHECK_CTX(L.canvas.height == L.panel.height, ctx);
            // snippet-end: layout-disjoint

            CHECK_CTX(L.tabBar.width == (float)screenW, ctx);
            CHECK_CTX(L.footer.width == (float)screenW, ctx);
            CHECK_CTX(L.canvas.height > 0.0f, ctx);
        }
    }
}

// 셀 중심을 다시 셀로 되돌리면 같은 셀이 나오는가. 알고리즘은 정수 셀
// 좌표만 다루고 픽셀 변환은 Grid 만 한다는 불변식이 이 왕복으로 고정된다.
void TestGridScreenRoundTrip() {
    ailab::Grid grid;
    grid.Init(32, 20);
    const ailab::LayoutRects L = Layout1280x720();
    grid.Layout((int)L.canvas.x, (int)L.canvas.y,
                (int)L.canvas.width, (int)L.canvas.height);
    CHECK(grid.CellSize() >= 2.0f);

    for (int y = 0; y < grid.Height(); ++y) {
        for (int x = 0; x < grid.Width(); ++x) {
            ailab::Cell back{-1, -1};
            const std::string ctx = std::to_string(x) + "," + std::to_string(y);
            CHECK_CTX(grid.ScreenToCell(grid.CellCenter(x, y), back), ctx);
            CHECK_CTX(back.x == x && back.y == y, ctx);
        }
    }

    // 왕복만 보면 CellCenter 가 셀 모서리를 돌려줘도 통과한다 — ScreenToCell 이
    // 같은 식을 그대로 되돌리기 때문이다(실측 확인). 반환점이 정말 *한가운데*
    // 인지는 양옆 반 칸을 찍어 셀 경계가 어디인지 물어야 드러난다.
    const float half = grid.CellSize() * 0.5f;
    for (int x = 1; x + 1 < grid.Width(); ++x) {
        const Vector2 center = grid.CellCenter(x, 3);
        const std::string ctx = "x=" + std::to_string(x);
        ailab::Cell hit{};
        CHECK_CTX(grid.ScreenToCell({center.x - half + 0.25f, center.y}, hit) &&
                      hit.x == x, ctx);
        CHECK_CTX(grid.ScreenToCell({center.x + half - 0.25f, center.y}, hit) &&
                      hit.x == x, ctx);
        CHECK_CTX(grid.ScreenToCell({center.x - half - 0.25f, center.y}, hit) &&
                      hit.x == x - 1, ctx);
        CHECK_CTX(grid.ScreenToCell({center.x + half + 0.25f, center.y}, hit) &&
                      hit.x == x + 1, ctx);
    }

    // 격자는 캔버스 안에 여백을 두고 들어간다 — 패널 위로 넘치지 않는다.
    const Vector2 last = grid.CellCenter(grid.Width() - 1, grid.Height() - 1);
    CHECK(last.x + half <= L.canvas.x + L.canvas.width);
    CHECK(last.y + half <= L.canvas.y + L.canvas.height);
    const Vector2 first = grid.CellCenter(0, 0);
    CHECK(first.x - half >= L.canvas.x);
    CHECK(first.y - half >= L.canvas.y);
}

// 화면 어디를 찍어도 격자 범위를 벗어난 셀이 나오지 않는가.
void TestEdgeClicksStayInBounds() {
    ailab::Grid grid;
    grid.Init(32, 20);
    const ailab::LayoutRects L = Layout1280x720();
    grid.Layout((int)L.canvas.x, (int)L.canvas.y,
                (int)L.canvas.width, (int)L.canvas.height);

    const std::vector<Vector2> probes = {
        {0.0f, 0.0f},                                    // 창 왼쪽 위 (탭바)
        {L.canvas.x, L.canvas.y},                        // 캔버스 왼쪽 위 여백
        {L.canvas.x + L.canvas.width, L.canvas.y},       // 캔버스 오른쪽 끝
        {L.panel.x + 10.0f, L.panel.y + 10.0f},          // 패널 위
        {L.footer.x + 10.0f, L.footer.y + 5.0f},         // 푸터 위
        {1279.0f, 719.0f},                               // 창 오른쪽 아래
        {-5000.0f, -5000.0f},
        {5000.0f, 5000.0f},
    };
    for (const Vector2& p : probes) {
        ailab::Cell hit{777, 777};
        const std::string ctx =
            std::to_string((int)p.x) + "," + std::to_string((int)p.y);
        if (grid.ScreenToCell(p, hit)) {
            CHECK_CTX(grid.InBounds(hit.x, hit.y), ctx);
        } else {
            // 실패하면 출력 인자를 건드리지 않는다. 호출자가 낡은 셀을
            // 새 셀로 착각하지 않게 하는 계약이다.
            CHECK_CTX(hit.x == 777 && hit.y == 777, ctx);
        }
    }

    // 캔버스 밖은 반드시 격자 밖이다 — 패널·푸터·탭바 클릭은 편집이 아니다.
    ailab::Cell hit{};
    CHECK(!grid.ScreenToCell({L.panel.x + 1.0f, L.panel.y + 40.0f}, hit));
    CHECK(!grid.ScreenToCell({100.0f, 1.0f}, hit));
    CHECK(!grid.ScreenToCell({-1.0f, -1.0f}, hit));
}

// 격자 크기를 바꿔도 start·goal 이 범위 밖으로 나가지 않고, 남는 영역의
// 벽은 보존되는가.
void TestResizeClampsStartAndGoal() {
    ailab::Grid grid;
    grid.Init(40, 30);
    CHECK(grid.Start() == (ailab::Cell{2, 15}));
    CHECK(grid.Goal() == (ailab::Cell{37, 15}));

    grid.SetWall(5, 5, true);
    grid.Resize(8, 6);
    CHECK(grid.Width() == 8 && grid.Height() == 6);
    CHECK(grid.InBounds(grid.Start().x, grid.Start().y));
    CHECK(grid.InBounds(grid.Goal().x, grid.Goal().y));
    CHECK(grid.Start() == (ailab::Cell{2, 5}));
    CHECK(grid.Goal() == (ailab::Cell{7, 5}));
    CHECK(!grid.Walkable(5, 5));          // 살아남은 영역의 벽은 그대로

    grid.Resize(40, 30);
    CHECK(!grid.Walkable(5, 5));
    CHECK(grid.Walkable(20, 20));         // 새로 생긴 영역은 비어 있다
    CHECK(grid.Walkable(39, 29));
    CHECK(!grid.Walkable(40, 29));        // 범위 밖은 통행 불가로 답한다
}

struct TestCase {
    const char* step;
    const char* name;
    void (*fn)();
};

// 어느 케이스가 어느 step 소속인가 — 그 step 이 무엇을 추가했는지로 정한다.
// 여기 있는 넷은 전부 골격 step 이 만든 경계다.
const TestCase kCases[] = {
    {"step01", "layout/canvas-panel-disjoint", TestLayoutRectsAreDisjoint},
    {"step01", "grid/screen-round-trip",       TestGridScreenRoundTrip},
    {"step01", "grid/edge-clicks-in-bounds",   TestEdgeClicksStayInBounds},
    {"step01", "grid/resize-clamps-endpoints", TestResizeClampsStartAndGoal},
};

}  // namespace

// 인자가 없으면 전부, `step01` 같은 step 이름이나 케이스 이름 전체를 주면
// 그것만 돈다. 아무것도 고르지 못한 필터는 조용히 통과하지 않고 실패한다.
int main(int argc, char** argv) {
    const std::string filter = (argc > 1) ? argv[1] : "";
    std::vector<const TestCase*> selected;
    for (const TestCase& c : kCases)
        if (filter.empty() || filter == c.step || filter == c.name)
            selected.push_back(&c);

    if (selected.empty()) {
        std::cerr << "no case matches filter '" << filter << "'\n";
        return 2;
    }

    std::cout << "ai_chrome_tests [" << (filter.empty() ? "all" : filter)
              << "] " << selected.size() << " case(s)\n";
    for (const TestCase* c : selected) {
        const int before = failures;
        c->fn();
        std::cout << "  " << (failures == before ? "ok" : "FAIL") << "  "
                  << c->name << "\n";
    }

    if (failures != 0) {
        std::cerr << failures << " chrome test(s) failed\n";
        return 1;
    }
    std::cout << "chrome tests passed\n";
    return 0;
}
