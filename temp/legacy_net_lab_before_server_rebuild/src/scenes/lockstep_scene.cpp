#include "scenes/lockstep_scene.h"

#include "raygui.h"

#include <cmath>
#include <cstring>
#include <cstdio>

namespace netlab {

namespace {
constexpr float kSimDt = 1.0f / 60.0f;

uint32_t Lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }
float Frand(uint32_t& s) { return (float)(Lcg(s) >> 8) / (float)(1u << 24); }
}  // namespace

void LockstepScene::OnEnter() { Reset(); }

void LockstepScene::Reset() {
    simA_.clear(); simB_.clear();
    uint32_t s = seed_;
    for (int i = 0; i < count_; ++i) {
        Particle p;
        p.x = 0.1f + 0.8f * Frand(s);
        p.y = 0.1f + 0.8f * Frand(s);
        p.vx = (Frand(s) - 0.5f) * 0.8f;
        p.vy = (Frand(s) - 0.5f) * 0.8f;
        simA_.push_back(p);
        simB_.push_back(p);   // 같은 초기 상태
    }
    tick_ = 0; firstDesyncTick_ = -1; accum_ = 0.0;
    hashA_ = HashSim(simA_); hashB_ = HashSim(simB_);
}

void LockstepScene::StepSim(std::vector<Particle>& ps, bool injectDesync) {
    const float dt = kSimDt;
    for (size_t i = 0; i < ps.size(); ++i) {
        Particle& p = ps[i];
        // 디싱크 주입: 한쪽 시뮬의 입자 0 속도에 미세 오차를 매 tick 누적.
        // float 1비트 차이가 시간에 따라 카오스적으로 증폭되는 것을 모사.
        if (injectDesync && i == 0) {
            p.vx *= 1.0002f;
        }
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        if (p.x < 0.0f) { p.x = 0.0f; p.vx = -p.vx; }
        if (p.x > 1.0f) { p.x = 1.0f; p.vx = -p.vx; }
        if (p.y < 0.0f) { p.y = 0.0f; p.vy = -p.vy; }
        if (p.y > 1.0f) { p.y = 1.0f; p.vy = -p.vy; }
    }
}

uint32_t LockstepScene::HashSim(const std::vector<Particle>& ps) const {
    uint32_t h = 2166136261u;   // FNV-1a
    auto mix = [&](float f) {
        static_assert(sizeof(float) == sizeof(uint32_t));
        uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof(bits));
        for (int b = 0; b < 4; ++b) {
            h ^= (bits >> (b * 8)) & 0xFFu;
            h *= 16777619u;
        }
    };
    for (const auto& p : ps) {
        mix(p.x); mix(p.y); mix(p.vx); mix(p.vy);
    }
    return h;
}

void LockstepScene::Update(float dt) {
    if ((int)simA_.size() != count_) Reset();
    if (paused_) return;
    if (dt > 0.25f) dt = 0.25f;
    accum_ += dt;
    while (accum_ >= kSimDt) {
        accum_ -= kSimDt;
        StepSim(simA_, false);
        StepSim(simB_, injectDesync_);
        hashA_ = HashSim(simA_);
        hashB_ = HashSim(simB_);
        if (hashA_ != hashB_ && firstDesyncTick_ < 0) firstDesyncTick_ = tick_;
        ++tick_;
    }
}

void LockstepScene::DrawWorld(int cx, int cy, int cw, int ch) {
    float pad = 20.0f;
    float boxW = (cw - 3 * pad) * 0.5f;
    float boxH = ch - 2 * pad - 40.0f;
    Rectangle boxA{cx + pad, (float)cy + pad + 30, boxW, boxH};
    Rectangle boxB{cx + 2 * pad + boxW, (float)cy + pad + 30, boxW, boxH};

    bool desync = (hashA_ != hashB_);

    auto drawBox = [&](Rectangle box, const std::vector<Particle>& ps,
                       const std::vector<Particle>& other, const char* label, Color lc) {
        DrawRectangleLinesEx(box, 1.5f, Color{60, 64, 74, 255});
        DrawText(label, (int)box.x, (int)box.y - 22, 16, lc);
        for (size_t i = 0; i < ps.size(); ++i) {
            float sx = box.x + ps[i].x * box.width;
            float sy = box.y + ps[i].y * box.height;
            // A 와 B 가 다른 입자는 빨강으로.
            bool diff = i < other.size() &&
                        (std::fabs(ps[i].x - other[i].x) > 1e-4f ||
                         std::fabs(ps[i].y - other[i].y) > 1e-4f);
            Color c = diff ? Color{230, 90, 90, 255} : lc;
            DrawCircleV({sx, sy}, 4.0f, c);
        }
    };
    drawBox(boxA, simA_, simB_, "Client A", Color{120, 220, 150, 255});
    drawBox(boxB, simB_, simA_, "Client B", Color{120, 180, 235, 255});

    // 상단 상태.
    char buf[128];
    std::snprintf(buf, sizeof(buf), "hash A = %08X    hash B = %08X", hashA_, hashB_);
    DrawText(buf, cx + (int)pad, cy + 6, 16, desync ? Color{230, 90, 90, 255}
                                                    : Color{120, 220, 150, 255});
    if (desync) {
        std::snprintf(buf, sizeof(buf), "DESYNC! (tick %d 부터 갈라짐)", firstDesyncTick_);
        DrawText(buf, cx + cw / 2 - 90, (int)(cy + ch - 26), 18, Color{230, 90, 90, 255});
    } else {
        DrawText("IN SYNC (해시 일치)", cx + cw / 2 - 70, (int)(cy + ch - 26), 18,
                 Color{120, 220, 150, 255});
    }
}

void LockstepScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;
    const float W = 240.0f;
    auto row = [&](int h) { float r = (float)y; y += h; return r; };
    char buf[64];

    GuiLabel(Rectangle{(float)(px + 8), row(22), W, 18}, "Lockstep 결정론");

    float c = (float)count_;
    std::snprintf(buf, sizeof(buf), "particles = %d", count_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr, &c, 4.0f, 80.0f);
    count_ = (int)c;

    GuiCheckBox(Rectangle{(float)(px + 8), row(24), 16, 16}, " 디싱크 주입 (B)", &injectDesync_);
    GuiCheckBox(Rectangle{(float)(px + 8), row(24), 16, 16}, " 일시정지", &paused_);

    if (GuiButton(Rectangle{(float)(px + 8), row(30), W, 24}, "Reset (재동기화)")) Reset();

    y += 8;
    std::snprintf(buf, sizeof(buf), "tick = %d", tick_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16},
             (hashA_ == hashB_) ? "상태: IN SYNC" : "상태: DESYNC");
    if (firstDesyncTick_ >= 0) {
        std::snprintf(buf, sizeof(buf), "첫 디싱크 tick = %d", firstDesyncTick_);
        GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    }

    y += 6;
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "같은 시드 -> 비트 단위 동일.");
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "한 비트 차이가 매치 끝까지 누적.");
}

}  // namespace netlab
