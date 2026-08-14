#pragma once

#include "scenes/iscene.h"

namespace netlab {

class WelcomeScene : public IScene {
public:
    void Update(float dt) override { (void)dt; }
    void DrawWorld(int cx, int cy, int cw, int ch) override;
    void DrawPanel(int px, int py, int pw, int ph) override;
    std::string_view Name() const override { return "Welcome"; }
};

}  // namespace netlab
