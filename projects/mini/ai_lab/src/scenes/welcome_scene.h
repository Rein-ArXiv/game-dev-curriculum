#pragma once

#include "scenes/iscene.h"

namespace ailab {

// 인트로 Scene — lab 의 범위와 조작법을 안내한다.
class WelcomeScene : public IScene {
public:
    void Update(float dt) override { (void)dt; }
    void DrawWorld(int cx, int cy, int cw, int ch) override;
    void DrawPanel(int px, int py, int pw, int ph) override;
    std::string_view Name() const override { return "Welcome"; }
};

}  // namespace ailab
