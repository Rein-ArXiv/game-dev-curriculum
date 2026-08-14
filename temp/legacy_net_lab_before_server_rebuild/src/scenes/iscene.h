// Scene 인터페이스 — 각 멀티플레이어 토픽이 한 Scene 으로 구현된다.
#pragma once

#include <string_view>

namespace netlab {

class IScene {
public:
    virtual ~IScene() = default;
    virtual void OnEnter() {}
    virtual void OnExit() {}
    virtual void Update(float dt) = 0;
    virtual void DrawWorld(int cx, int cy, int cw, int ch) = 0;
    virtual void DrawPanel(int px, int py, int pw, int ph) = 0;
    virtual std::string_view Name() const = 0;
};

}  // namespace netlab
