#include "ui/scene_chrome.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>

namespace mathlab {

namespace {
constexpr int kTabBarH = 32;
constexpr int kFooterH = 24;
}  // namespace

LayoutRects ComputeLayout(int screenW, int screenH, int panelW) {
    LayoutRects L{};
    L.tabBar = {0.0f, 0.0f, (float)screenW, (float)kTabBarH};

    int contentY = kTabBarH;
    int contentH = screenH - kTabBarH - kFooterH;
    int canvasW  = screenW - panelW;

    L.canvas = {0.0f, (float)contentY,
                (float)canvasW, (float)contentH};
    L.panel  = {(float)canvasW, (float)contentY,
                (float)panelW, (float)contentH};
    L.footer = {0.0f, (float)(screenH - kFooterH),
                (float)screenW, (float)kFooterH};
    return L;
}

int DrawTabBar(Rectangle bar,
                const std::vector<std::string_view>& labels,
                int currentIdx) {
    DrawRectangleRec(bar, Color{32, 34, 40, 255});
    int  next = currentIdx;
    int  n    = (int)labels.size();
    if (n <= 0) return next;

    float tabW = bar.width / (float)n;
    Vector2 m  = GetMousePosition();
    bool   click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    for (int i = 0; i < n; ++i) {
        Rectangle r = {bar.x + tabW * (float)i, bar.y,
                       tabW, bar.height};
        bool hover = CheckCollisionPointRec(m, r);
        bool active = (i == currentIdx);

        Color bg = active ? Color{55, 60, 72, 255}
                          : (hover ? Color{40, 44, 52, 255}
                                   : Color{32, 34, 40, 255});
        DrawRectangleRec(r, bg);
        DrawLine((int)(r.x + r.width), (int)r.y,
                  (int)(r.x + r.width), (int)(r.y + r.height),
                  Color{60, 62, 70, 255});

        Color  fg = active ? Color{240, 240, 240, 255}
                            : Color{170, 170, 170, 255};
        std::string_view label = labels[i];
        char buf[64];
        int  copy = (int)std::min<size_t>(label.size(), sizeof(buf) - 1);
        for (int k = 0; k < copy; ++k) buf[k] = label[k];
        buf[copy] = 0;
        int tw = MeasureText(buf, 14);
        DrawText(buf,
                  (int)(r.x + r.width / 2 - tw / 2),
                  (int)(r.y + r.height / 2 - 7),
                  14,
                  fg);

        if (hover && click) next = i;
    }
    DrawLine((int)bar.x, (int)(bar.y + bar.height),
              (int)(bar.x + bar.width), (int)(bar.y + bar.height),
              Color{60, 62, 70, 255});
    return next;
}

void DrawFooter(Rectangle footer, float frameMs, float fps,
                  std::string_view sceneName) {
    DrawRectangleRec(footer, Color{24, 26, 32, 255});
    DrawLine((int)footer.x, (int)footer.y,
              (int)(footer.x + footer.width), (int)footer.y,
              Color{60, 62, 70, 255});

    char buf[160];
    int n = (int)std::min<size_t>(sceneName.size(), 32u);
    char nameBuf[40] = {0};
    for (int k = 0; k < n; ++k) nameBuf[k] = sceneName[k];

    std::snprintf(buf, sizeof(buf),
                   "[%s]   frame %.2f ms   %.0f fps",
                   nameBuf, frameMs, fps);
    DrawText(buf,
              (int)footer.x + 8,
              (int)footer.y + 4,
              14,
              Color{200, 200, 200, 255});
}

void DrawCanvasBackground(Rectangle canvas) {
    DrawRectangleRec(canvas, Color{16, 17, 22, 255});
}

void DrawPanelBackground(Rectangle panel) {
    DrawRectangleRec(panel, Color{28, 30, 36, 255});
    DrawLine((int)panel.x, (int)panel.y,
              (int)panel.x, (int)(panel.y + panel.height),
              Color{60, 62, 70, 255});
}

}  // namespace mathlab
