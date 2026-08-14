#include "ui/handle.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace mathlab {

namespace {

// 모든 DragHandle 이 공유하는 capture 상태.
// activeId == 0 은 "잡혀 있는 핸들 없음" 의미.
uint32_t g_activeId          = 0;
uint32_t g_nextId            = 1;

// 현재 프레임의 클릭이 잡힐 후보 (가장 가까운 핸들).
// EndFrame 에서 g_activeId 로 승격.
uint32_t g_pendingId         = 0;
float    g_pendingDistSq     = std::numeric_limits<float>::infinity();
bool     g_clickedThisFrame  = false;

}  // namespace

DragHandle::DragHandle() : id_(g_nextId++) {}
DragHandle::DragHandle(Vector2 initial) : pos_(initial), id_(g_nextId++) {}

bool DragHandle::IsDragging() const {
    return g_activeId == id_;
}

void DragHandle::BeginFrame() {
    g_pendingId         = 0;
    g_pendingDistSq     = std::numeric_limits<float>::infinity();
    g_clickedThisFrame  = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        g_activeId = 0;
    }
}

void DragHandle::EndFrame() {
    if (g_clickedThisFrame && g_pendingId != 0) {
        g_activeId = g_pendingId;
    }
    g_clickedThisFrame = false;
}

void DragHandle::Update() {
    Vector2 m  = GetMousePosition();
    float   dx = m.x - pos_.x;
    float   dy = m.y - pos_.y;
    float   d2 = dx * dx + dy * dy;
    float   r2 = (radius_ + 4.0f) * (radius_ + 4.0f);

    // 이미 잡혀 있으면 마우스 따라옴.
    if (g_activeId == id_) {
        pos_ = m;
        return;
    }

    // 새 클릭의 후보가 될 수 있나 — 핸들 위 + 다른 핸들보다 가까움.
    if (g_clickedThisFrame && d2 <= r2 && d2 < g_pendingDistSq) {
        g_pendingId     = id_;
        g_pendingDistSq = d2;
    }
}

void DragHandle::Draw(Color fill, const char* label) const {
    Vector2 m  = GetMousePosition();
    float   dx = m.x - pos_.x;
    float   dy = m.y - pos_.y;
    bool    hover = (dx * dx + dy * dy)
                     <= (radius_ + 4.0f) * (radius_ + 4.0f);
    bool    dragging = (g_activeId == id_);
    Color   ring = (dragging || hover) ? Color{255, 255, 255, 220}
                                        : Color{60, 60, 60, 255};
    DrawCircleV(pos_, radius_ + 2.0f, ring);
    DrawCircleV(pos_, radius_, fill);

    if (label) {
        DrawText(label,
                  (int)pos_.x + (int)radius_ + 6,
                  (int)pos_.y - 8,
                  14,
                  Color{220, 220, 220, 255});
    }
}

}  // namespace mathlab
