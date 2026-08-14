#include "scenes/measure_scene.h"
#include "core/contact_solver.h"

#include "raygui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace physlab {

namespace {

constexpr int kGraphCapacity = 600;

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
// 그리고 그 코드 경로는 *Step 6 이 가르친 그 SI* 여야 한다 — 그래야 결정성
// 측정이 교재의 솔버를 재는 것이 된다. 접촉 수집 / SI / 위치 보정은 전부
// core/contact_solver.h 한 벌이고, 이 파일은 시나리오만 정한다.

constexpr float kSimRadius = 12.0f;

// shuffle 토글용 재정렬 정책. SI 의 Gauss-Seidel 풀이가 *순서 의존* 임을
// 실증하는 실패 주입 — 호출마다 진행되는 RNG 로 매 iter 순서를 섞는다.
struct ShuffleContactOrder {
    uint32_t* rng;
    void operator()(std::vector<int>& order) const {
        for (int k = (int)order.size() - 1; k > 0; --k) {
            const int j = (int)(Lcg(*rng) % (uint32_t)(k + 1));
            std::swap(order[(size_t)k], order[(size_t)j]);
        }
    }
};

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
    const WorldBounds bounds = Bounds();

    // Step 6 의 SequentialImpulse 분기와 *같은 네 줄* 이다. 여기서 자체 구현을
    // 하는 순간 결정성 측정은 교재의 솔버가 아니라 사본을 재게 된다.

    // 1. semi-implicit Euler 적분 (중력만).
    core::IntegrateSemiImplicitEuler(sim, kDefaultGravity, dt);

    // 2. contact 수집 (body-body brute O(n²) + 벽 / 바닥).
    std::vector<Contact> contacts;
    core::GatherCircleContacts(sim, bounds, contacts);

    // 3. Sequential Impulse — velocity 수렴.
    //    shuffleContacts_ 가 true 면 *각 iter 안에서* contact 처리 순서를
    //    Lcg seed (호출마다 진행되는) 로 섞는다. SI 의 Gauss-Seidel 풀이는
    //    *순서 의존* 이라 결과가 갈라진다 → 결정성 검증에서 NON-DET 으로 표시.
    if (shuffleContacts_) {
        static thread_local uint32_t shuffleRng = 0xDEADBEEFu;
        core::SolveSequentialImpulse(sim, contacts, solver_, iterations_,
                                     ShuffleContactOrder{&shuffleRng});
    } else {
        core::SolveSequentialImpulse(sim, contacts, solver_, iterations_);
    }

    // 4. position correction (split impulse 단순화).
    core::SolvePositionCorrection(sim, bounds, solver_, iterations_ / 2);
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
