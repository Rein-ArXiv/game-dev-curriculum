#include "scenes/measure_scene.h"
#include "core/physics_core.h"

#include "raygui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace physlab {

namespace {

constexpr int kGraphCapacity = 600;

inline float Dot(Vector2 a, Vector2 b) { return a.x*b.x + a.y*b.y; }
inline Vector2 Sub(Vector2 a, Vector2 b) { return {a.x-b.x, a.y-b.y}; }

uint32_t Lcg(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s;
}
float Rand01(uint32_t& s) {
    return (float)(Lcg(s) >> 8) / (float)(1u << 24);
}

// Live 시뮬과 결정성 테스트 시뮬 모두 *같은 코드 경로* 를 써야 한다 — 그래야
// "Live 와 동일한 시뮬을 두 번 재현했을 때 일치하는가" 를 답할 수 있다.

constexpr float kSimGravity     = 600.0f;
constexpr float kSimRestitution = 0.1f;
constexpr float kSimSlop        = 0.5f;
constexpr float kSimPosCorrect  = 0.2f;
constexpr float kSimRadius      = 12.0f;

}  // namespace

MeasureScene::MeasureScene() {
    energyHist_.Reset(kGraphCapacity);
    penHist_.Reset(kGraphCapacity);
}

void MeasureScene::OnEnter() {
    initialized_ = false;
    energyHist_.Clear();
    penHist_.Clear();
    testRan_     = false;
}

void MeasureScene::InitBodies(std::vector<MeasureBody>& out, int n,
                                  unsigned seed) const {
    out.assign((size_t)n, MeasureBody{});
    unsigned s = seed;
    int   perRow = std::max(1, (lastCanvasW_ - 40) / (int)(kSimRadius * 2.4f));
    for (int k = 0; k < n; ++k) {
        int row = k / perRow;
        int col = k % perRow;
        out[(size_t)k].radius = kSimRadius;
        out[(size_t)k].pos = {
            (float)(lastCanvasX_ + 24 + col * (int)(kSimRadius * 2.4f)
                     + (int)(Rand01(s) * 4.0f)),
            (float)(lastCanvasY_ + 40 + row * (int)(kSimRadius * 2.4f)),
        };
        out[(size_t)k].vel = {0, 0};
    }
}

void MeasureScene::StepSim(std::vector<MeasureBody>& sim, float dt) const {
    int n = (int)sim.size();

    // 1. semi-implicit Euler 적분 (중력만).
    for (auto& b : sim) {
        b.vel.y += kSimGravity * dt;
        b.pos.x += b.vel.x * dt;
        b.pos.y += b.vel.y * dt;
    }

    // 2. contact 수집.
    struct Cn {
        int i, j;
        Vector2 normal;
        float depth;
        float lambda;
        float targetVelocity;
    };
    std::vector<Cn> contacts;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            Vector2 d   = Sub(sim[(size_t)j].pos, sim[(size_t)i].pos);
            float   r   = sim[(size_t)i].radius + sim[(size_t)j].radius;
            float   dist = std::sqrt(d.x*d.x + d.y*d.y);
            if (dist < r) {
                const auto nrm =
                    core::SafeUnitNormal(d.x, d.y, i ^ j);
                contacts.push_back({i, j,
                                      {nrm.x, nrm.y}, r - dist, 0, 0});
            }
        }
    }
    float floorY = (float)(lastCanvasY_ + lastCanvasH_) - 4.0f;
    float wallL  = (float)lastCanvasX_ + 4.0f;
    float wallR  = (float)(lastCanvasX_ + lastCanvasW_) - 4.0f;
    for (int i = 0; i < n; ++i) {
        const auto& b = sim[(size_t)i];
        if (b.pos.y + b.radius > floorY)
            contacts.push_back({i, -1, {0, -1},
                                 (b.pos.y + b.radius) - floorY, 0, 0});
        if (b.pos.x - b.radius < wallL)
            contacts.push_back({i, -1, {1, 0},
                                 wallL - (b.pos.x - b.radius), 0, 0});
        if (b.pos.x + b.radius > wallR)
            contacts.push_back({i, -1, {-1, 0},
                                 (b.pos.x + b.radius) - wallR, 0, 0});
    }

    constexpr float kRestitutionThreshold = 50.0f;
    for (auto& c : contacts) {
        const auto& bi = sim[(size_t)c.i];
        const float initialVn = c.j >= 0
            ? Dot(Sub(sim[(size_t)c.j].vel, bi.vel), c.normal)
            : Dot(bi.vel, c.normal);
        c.targetVelocity = core::RestitutionTarget(
            initialVn, kSimRestitution, kRestitutionThreshold);
    }

    // 3. Sequential Impulse — velocity 수렴.
    //    shuffleContacts_ 가 true 면 *각 iter 안에서* contact 처리 순서를
    //    Lcg seed (호출마다 다른) 로 섞는다. SI 의 Gauss-Seidel 풀이는 *순서
    //    의존* 이라 결과가 갈라진다 → 결정성 검증에서 NON-DET 으로 표시.
    static thread_local uint32_t shuffleRng = 0xDEADBEEFu;
    std::vector<int> order((size_t)contacts.size());
    for (size_t k = 0; k < order.size(); ++k) order[k] = (int)k;
    for (int iter = 0; iter < iterations_; ++iter) {
        if (shuffleContacts_) {
            // Fisher-Yates with non-fixed seed → 매번 다른 순서.
            for (int k = (int)order.size() - 1; k > 0; --k) {
                int j = (int)(Lcg(shuffleRng) % (uint32_t)(k + 1));
                std::swap(order[(size_t)k], order[(size_t)j]);
            }
        }
        for (int kIdx = 0; kIdx < (int)order.size(); ++kIdx) {
            auto& c = contacts[(size_t)order[(size_t)kIdx]];
            MeasureBody& bi = sim[(size_t)c.i];
            if (c.j >= 0) {
                MeasureBody& bj = sim[(size_t)c.j];
                Vector2 vrel = Sub(bj.vel, bi.vel);
                float   vn   = Dot(vrel, c.normal);
                float ap = core::AccumulateNormalImpulse(
                    vn, c.targetVelocity, 2.0f, c.lambda);
                bi.vel.x -= ap * c.normal.x;
                bi.vel.y -= ap * c.normal.y;
                bj.vel.x += ap * c.normal.x;
                bj.vel.y += ap * c.normal.y;
            } else {
                float vn   = Dot(bi.vel, c.normal);
                float ap = core::AccumulateNormalImpulse(
                    vn, c.targetVelocity, 1.0f, c.lambda);
                bi.vel.x += ap * c.normal.x;
                bi.vel.y += ap * c.normal.y;
            }
        }
    }

    // 4. position correction (split impulse 단순화).
    for (int iter = 0; iter < std::max(1, iterations_ / 2); ++iter) {
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                Vector2 d  = Sub(sim[(size_t)j].pos, sim[(size_t)i].pos);
                float r    = sim[(size_t)i].radius + sim[(size_t)j].radius;
                float dist = std::sqrt(d.x*d.x + d.y*d.y);
                if (dist < r) {
                    float eff = (r - dist - kSimSlop);
                    if (eff <= 0) continue;
                    eff *= kSimPosCorrect;
                    const auto safe =
                        core::SafeUnitNormal(d.x, d.y, i ^ j);
                    Vector2 nrm = {safe.x, safe.y};
                    float k = eff / 2.0f;
                    sim[(size_t)i].pos.x -= k * nrm.x;
                    sim[(size_t)i].pos.y -= k * nrm.y;
                    sim[(size_t)j].pos.x += k * nrm.x;
                    sim[(size_t)j].pos.y += k * nrm.y;
                }
            }
        }
        for (int i = 0; i < n; ++i) {
            auto& b = sim[(size_t)i];
            if (b.pos.y + b.radius > floorY) {
                float eff = ((b.pos.y + b.radius) - floorY - kSimSlop)
                              * kSimPosCorrect;
                if (eff > 0) b.pos.y -= eff;
            }
            if (b.pos.x - b.radius < wallL) {
                float eff = (wallL - (b.pos.x - b.radius) - kSimSlop)
                              * kSimPosCorrect;
                if (eff > 0) b.pos.x += eff;
            }
            if (b.pos.x + b.radius > wallR) {
                float eff = ((b.pos.x + b.radius) - wallR - kSimSlop)
                              * kSimPosCorrect;
                if (eff > 0) b.pos.x -= eff;
            }
        }
    }
}

void MeasureScene::ComputeMetrics(const std::vector<MeasureBody>& sim,
                                       float& outAvgPen, float& outKE) const {
    int n = (int)sim.size();
    float sumPen = 0, cnt = 0;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            Vector2 d = Sub(sim[(size_t)j].pos, sim[(size_t)i].pos);
            float r   = sim[(size_t)i].radius + sim[(size_t)j].radius;
            float dist = std::sqrt(d.x*d.x + d.y*d.y);
            if (dist < r) { sumPen += (r - dist); ++cnt; }
        }
    }
    outAvgPen = (cnt > 0) ? (sumPen / cnt) : 0.0f;

    float ke = 0;
    for (const auto& b : sim) {
        ke += 0.5f * (b.vel.x*b.vel.x + b.vel.y*b.vel.y)
               / (100.0f * 100.0f);
    }
    outKE = ke;
}

void MeasureScene::RunDeterminismTest() {
    if (lastCanvasW_ <= 0) return;

    auto runOnce = [&]() {
        std::vector<MeasureBody> sim;
        InitBodies(sim, N_, seed_);
        for (int s = 0; s < testFrames_; ++s) {
            StepSim(sim, 1.0f / 120.0f);
        }
        return sim;
    };

    auto a = runOnce();
    auto b = runOnce();

    float maxD = 0, sumD = 0;
    int n = std::min((int)a.size(), (int)b.size());
    for (int i = 0; i < n; ++i) {
        float dx = a[(size_t)i].pos.x - b[(size_t)i].pos.x;
        float dy = a[(size_t)i].pos.y - b[(size_t)i].pos.y;
        float d  = std::sqrt(dx*dx + dy*dy);
        maxD = std::max(maxD, d);
        sumD += d;
    }
    lastTestMaxDelta_      = maxD;
    lastTestAvgDelta_      = (n > 0) ? (sumD / (float)n) : 0.0f;
    lastTestDeterministic_ = (maxD == 0.0f);   // bit-exact
    lastTestShuffle_       = shuffleContacts_;
    testRan_               = true;
}

void MeasureScene::HandleInput() {
    if (IsKeyPressed(KEY_R)) {
        InitBodies(live_, N_, seed_);
        energyHist_.Clear();
        penHist_.Clear();
    }
    if (IsKeyPressed(KEY_T)) {
        RunDeterminismTest();
    }
}

void MeasureScene::Step(float dt) {
    if (lastCanvasW_ <= 0) return;
    if (!initialized_) {
        InitBodies(live_, N_, seed_);
        initialized_ = true;
    }
    if ((int)live_.size() != N_) InitBodies(live_, N_, seed_);

    StepSim(live_, dt);
    ComputeMetrics(live_, lastAvgPen_, lastKE_);
    energyHist_.Push(lastKE_);
    penHist_.Push(lastAvgPen_);
}

void MeasureScene::DrawWorld(int cx, int cy, int cw, int ch) {
    lastCanvasX_ = cx; lastCanvasY_ = cy;
    lastCanvasW_ = cw; lastCanvasH_ = ch;

    DrawRectangle(cx, cy, cw, ch, Color{20, 22, 28, 255});
    DrawRectangleLines(cx, cy, cw, ch, Color{60, 62, 70, 255});

    int floorY = cy + ch - 4;
    DrawLine(cx + 4, floorY, cx + cw - 4, floorY, Color{120, 130, 140, 255});

    Color col = {120, 200, 255, 255};
    for (const auto& b : live_) {
        DrawCircleV(b.pos, b.radius, Color{col.r, col.g, col.b, 150});
        DrawCircleLines((int)b.pos.x, (int)b.pos.y, b.radius, col);
    }

    DrawText(TextFormat("Step 8: Measure   N=%d   seed=%u   iter=%d",
                          N_, seed_, iterations_),
              cx + 10, cy + 10, 14, Color{200, 200, 200, 255});

    Rectangle gr1 = {(float)(cx + cw - 320), (float)(cy + 40), 300.0f, 110.0f};
    Rectangle gr2 = {(float)(cx + cw - 320), (float)(cy + 160), 300.0f, 110.0f};
    GraphInput in1[1] = {{&energyHist_, col, "total KE"}};
    DrawLineGraph(gr1, in1, 1, "Live total KE", 0, 1, true);
    GraphInput in2[1] = {{&penHist_, Color{255, 200, 100, 255}, "avg pen"}};
    DrawLineGraph(gr2, in2, 1, "Live avg penetration", 0, 1, true);
}

void MeasureScene::DrawPanel(int panelX, int panelY, int panelW, int panelH) {
    (void)panelW; (void)panelH;
    int  y = panelY + 8;
    char buf[160];

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "Measure (baseline determinism)");
    y += 24;

    std::snprintf(buf, sizeof(buf), "N: %d", N_);
    float fn = (float)N_;
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &fn, 4.0f, 64.0f);
    int nn = (int)fn;
    if (nn != N_) {
        N_ = nn;
        InitBodies(live_, N_, seed_);
        energyHist_.Clear(); penHist_.Clear();
    }
    y += 22;

    std::snprintf(buf, sizeof(buf), "seed: %u", seed_);
    float fs = (float)seed_;
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &fs, 1.0f, 64.0f);
    unsigned nseed = (unsigned)fs;
    if (nseed != seed_) {
        seed_ = nseed;
        InitBodies(live_, N_, seed_);
        energyHist_.Clear(); penHist_.Clear();
    }
    y += 22;

    std::snprintf(buf, sizeof(buf), "iterations: %d", iterations_);
    float fi = (float)iterations_;
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &fi, 1.0f, 32.0f);
    iterations_ = (int)fi;
    y += 22;

    std::snprintf(buf, sizeof(buf), "test frames: %d", testFrames_);
    float ft = (float)testFrames_;
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &ft, 60.0f, 2400.0f);
    testFrames_ = (int)ft;
    y += 24;

    GuiCheckBox(Rectangle{(float)(panelX + 8), (float)y, 18, 18},
                 "shuffle contacts (breaks det.)", &shuffleContacts_);
    y += 26;

    if (GuiButton(Rectangle{(float)(panelX + 8), (float)y, 120, 22},
                   "Run determinism (T)")) {
        RunDeterminismTest();
    }
    if (GuiButton(Rectangle{(float)(panelX + 136), (float)y, 100, 22},
                   "Reset (R)")) {
        InitBodies(live_, N_, seed_);
        energyHist_.Clear(); penHist_.Clear();
    }
    y += 28;

    std::snprintf(buf, sizeof(buf), "live avg pen: %.2f", lastAvgPen_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 18;
    std::snprintf(buf, sizeof(buf), "live KE:      %.3f", lastKE_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 24;

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "Determinism sanity check:");
    y += 18;
    if (!testRan_) {
        GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  "(not run yet)");
        y += 18;
    } else {
        std::snprintf(buf, sizeof(buf), "max delta: %.6f px",
                       lastTestMaxDelta_);
        GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf);
        y += 18;
        std::snprintf(buf, sizeof(buf), "avg delta: %.6f px",
                       lastTestAvgDelta_);
        GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf);
        y += 18;
        // shuffle 상태를 같이 보여야 "RUNS DIVERGED" 가 의도된 실패 주입인지
        // 진짜 결정성 위반인지 UI 만으로 구분된다.
        std::snprintf(buf, sizeof(buf), "(shuffle: %s)",
                       lastTestShuffle_ ? "on" : "off");
        GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf);
        y += 18;
        if (lastTestDeterministic_) {
            DrawRectangle(panelX + 6, y, 248, 18, Color{30, 90, 30, 200});
            GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                      "BIT-EXACT REPEATABLE (THIS RUN)");
        } else if (lastTestMaxDelta_ < 1e-3f) {
            DrawRectangle(panelX + 6, y, 248, 18, Color{90, 90, 30, 200});
            GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                      "NEAR-REPEATABLE (THIS RUN)");
        } else {
            DrawRectangle(panelX + 6, y, 248, 18, Color{120, 30, 30, 200});
            GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                      "RUNS DIVERGED");
        }
        y += 22;
    }

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "Same seed twice -> max delta.");
    y += 16;
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "== 0 is necessary, not sufficient, for lockstep.");
}

}  // namespace physlab
