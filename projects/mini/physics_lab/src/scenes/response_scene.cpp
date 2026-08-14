#include "scenes/response_scene.h"
#include "core/physics_core.h"

#include "raygui.h"

#include <algorithm>
#include <chrono>
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

}  // namespace

ResponseScene::ResponseScene() {
    penHist_.Reset(kGraphCapacity);
    msHist_.Reset(kGraphCapacity);
    keHist_.Reset(kGraphCapacity);
}

void ResponseScene::OnEnter() {
    initialized_ = false;
    penHist_.Clear();
    msHist_.Clear();
    keHist_.Clear();
}

void ResponseScene::ResetBodies(int n) {
    bodies_.assign((size_t)n, CircleRb{});
    rng_ = 1u;
    if (lastCanvasW_ <= 0) return;
    // 좌상단부터 격자에 가깝게 배치 → 떨어지면서 stacking.
    float r        = 12.0f;
    int   perRow   = std::max(1, (lastCanvasW_ - 40) / (int)(r * 2.4f));
    for (int k = 0; k < n; ++k) {
        int row = k / perRow;
        int col = k % perRow;
        bodies_[(size_t)k].radius  = r;
        bodies_[(size_t)k].mass    = 1.0f;
        bodies_[(size_t)k].invMass = 1.0f;
        bodies_[(size_t)k].pos = {
            (float)(lastCanvasX_ + 24 + col * (int)(r * 2.4f)
                     + (int)(Rand01(rng_) * 4.0f)),
            (float)(lastCanvasY_ + 40 + row * (int)(r * 2.4f)),
        };
        bodies_[(size_t)k].prevPos = bodies_[(size_t)k].pos;
        bodies_[(size_t)k].vel     = {0, 0};
    }
}

void ResponseScene::HandleInput() {
    if (IsKeyPressed(KEY_R)) ResetBodies(N_);
    if (IsKeyPressed(KEY_SPACE)) {
        // 모든 body 에 위 방향 임펄스 — stacking 깨기.
        for (auto& b : bodies_) {
            b.vel.y -= 300.0f;
        }
    }
}

void ResponseScene::IntegrateGravity(float dt) {
    for (auto& b : bodies_) {
        if (b.invMass <= 0) continue;
        b.vel.y += gravity_ * dt;
        b.pos.x += b.vel.x * dt;
        b.pos.y += b.vel.y * dt;
    }
}

void ResponseScene::GatherContacts(std::vector<Contact>& out) const {
    out.clear();
    int n = (int)bodies_.size();

    // body-body O(n²) — Broad Phase 가 아니라 *응답* 비교가 주제이므로 단순.
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            Vector2 d   = Sub(bodies_[(size_t)j].pos, bodies_[(size_t)i].pos);
            float   r   = bodies_[(size_t)i].radius + bodies_[(size_t)j].radius;
            float   dist = std::sqrt(d.x*d.x + d.y*d.y);
            if (dist < r) {
                const auto nrm =
                    core::SafeUnitNormal(d.x, d.y, i ^ j);
                Contact c;
                c.i = i; c.j = j;
                c.normal = {nrm.x, nrm.y};             // i → j
                c.depth  = r - dist;
                c.lambda = 0.0f;
                out.push_back(c);
            }
        }
    }

    // 벽 / 바닥.
    float floorY = (float)(lastCanvasY_ + lastCanvasH_) - 4.0f;
    float wallL  = (float)lastCanvasX_ + 4.0f;
    float wallR  = (float)(lastCanvasX_ + lastCanvasW_) - 4.0f;
    for (int i = 0; i < n; ++i) {
        const auto& b = bodies_[(size_t)i];
        if (b.pos.y + b.radius > floorY) {
            Contact c;
            c.i = i; c.j = -1;
            c.normal = {0, -1};                       // body 가 위로 밀려남
            c.depth  = (b.pos.y + b.radius) - floorY;
            c.lambda = 0.0f;
            out.push_back(c);
        }
        if (b.pos.x - b.radius < wallL) {
            Contact c;
            c.i = i; c.j = -1;
            c.normal = {1, 0};
            c.depth  = wallL - (b.pos.x - b.radius);
            c.lambda = 0.0f;
            out.push_back(c);
        }
        if (b.pos.x + b.radius > wallR) {
            Contact c;
            c.i = i; c.j = -1;
            c.normal = {-1, 0};
            c.depth  = (b.pos.x + b.radius) - wallR;
            c.lambda = 0.0f;
            out.push_back(c);
        }
    }
}

void ResponseScene::ApplyPenalty(const std::vector<Contact>& contacts, float dt) {
    // 매 step 의 force 누적. 각 contact 에 대해 spring force = k * depth * normal.
    // contacts 는 호출자 (Step) 가 이미 수집 — 알고리즘 간 *공정 비교* 를 위해
    // GatherContacts 는 step 당 한 번만.
    for (const auto& c : contacts) {
        Vector2 fOn_i = {-penaltyK_ * c.depth * c.normal.x,
                          -penaltyK_ * c.depth * c.normal.y};
        if (c.j >= 0) {
            bodies_[(size_t)c.i].vel.x += fOn_i.x * bodies_[(size_t)c.i].invMass * dt;
            bodies_[(size_t)c.i].vel.y += fOn_i.y * bodies_[(size_t)c.i].invMass * dt;
            bodies_[(size_t)c.j].vel.x += -fOn_i.x * bodies_[(size_t)c.j].invMass * dt;
            bodies_[(size_t)c.j].vel.y += -fOn_i.y * bodies_[(size_t)c.j].invMass * dt;
        } else {
            // wall: normal 이 body 가 밀려나는 방향 → force on body = +k*depth*normal
            bodies_[(size_t)c.i].vel.x += -fOn_i.x * bodies_[(size_t)c.i].invMass * dt;
            bodies_[(size_t)c.i].vel.y += -fOn_i.y * bodies_[(size_t)c.i].invMass * dt;
        }
    }
}

void ResponseScene::ApplyImpulseOnce(std::vector<Contact>& contacts) {
    for (auto& c : contacts) {
        CircleRb& bi = bodies_[(size_t)c.i];
        if (c.j >= 0) {
            CircleRb& bj = bodies_[(size_t)c.j];
            Vector2 vrel = Sub(bj.vel, bi.vel);
            float   vn   = Dot(vrel, c.normal);       // i → j 방향이 +
            if (vn < 0) {
                float jLambda = -(1.0f + restitution_) * vn
                                  / (bi.invMass + bj.invMass);
                bi.vel.x -= jLambda * bi.invMass * c.normal.x;
                bi.vel.y -= jLambda * bi.invMass * c.normal.y;
                bj.vel.x += jLambda * bj.invMass * c.normal.x;
                bj.vel.y += jLambda * bj.invMass * c.normal.y;
            }
        } else {
            // wall — c.normal 은 body 가 밀려나는 방향.
            float vn = Dot(bi.vel, c.normal);
            if (vn < 0) {
                float jLambda = -(1.0f + restitution_) * vn / bi.invMass;
                bi.vel.x += jLambda * bi.invMass * c.normal.x;
                bi.vel.y += jLambda * bi.invMass * c.normal.y;
            }
        }
    }
}

void ResponseScene::ApplySequentialImpulse(std::vector<Contact>& contacts) {
    // *target velocity* 를 첫 iter 전에 한 번 계산. iterations 안에서는
    // restitution 을 재주입 안 한다 (재주입 시 에너지 over-injection — 정지한
    // 박스가 다시 튀어오르거나 시뮬이 떨림).
    //
    // restitution slop: 접근 속도가 임계값 이하면 *resting contact* 로 보고
    // restitution = 0. 게임 엔진의 표준 처리 (Box2D 의 b2_velocityThreshold).
    constexpr float kRestThreshold = 50.0f;  // px/s
    std::vector<float> vTarget(contacts.size(), 0.0f);
    for (size_t k = 0; k < contacts.size(); ++k) {
        const Contact& c = contacts[k];
        const CircleRb& bi = bodies_[(size_t)c.i];
        float vnInit;
        if (c.j >= 0) {
            const CircleRb& bj = bodies_[(size_t)c.j];
            vnInit = Dot(Sub(bj.vel, bi.vel), c.normal);
        } else {
            vnInit = Dot(bi.vel, c.normal);
        }
        // vnInit < 0 (접근) + 임계 이상이면 분리 방향으로 -e * vnInit 만큼 가속.
        if (vnInit < -kRestThreshold) {
            vTarget[k] = core::RestitutionTarget(
                vnInit, restitution_, kRestThreshold);
        }
    }

    // accumulated lambda (≥ 0 clamp). 같은 contact 의 누적 임펄스가 음수가
    // 되지 않도록 — body 가 *밀려나는 방향만* 허용.
    for (int iter = 0; iter < iterations_; ++iter) {
        for (size_t k = 0; k < contacts.size(); ++k) {
            Contact& c = contacts[k];
            CircleRb& bi = bodies_[(size_t)c.i];
            if (c.j >= 0) {
                CircleRb& bj = bodies_[(size_t)c.j];
                Vector2 vrel = Sub(bj.vel, bi.vel);
                float   vn   = Dot(vrel, c.normal);
                float appliedL = core::AccumulateNormalImpulse(
                    vn, vTarget[k], bi.invMass + bj.invMass, c.lambda);
                bi.vel.x -= appliedL * bi.invMass * c.normal.x;
                bi.vel.y -= appliedL * bi.invMass * c.normal.y;
                bj.vel.x += appliedL * bj.invMass * c.normal.x;
                bj.vel.y += appliedL * bj.invMass * c.normal.y;
            } else {
                float vn       = Dot(bi.vel, c.normal);
                float appliedL = core::AccumulateNormalImpulse(
                    vn, vTarget[k], bi.invMass, c.lambda);
                bi.vel.x += appliedL * bi.invMass * c.normal.x;
                bi.vel.y += appliedL * bi.invMass * c.normal.y;
            }
        }
    }
}

void ResponseScene::ApplyPositionCorrection(const std::vector<Contact>& contacts) {
    for (const auto& c : contacts) {
        if (c.depth <= slop_) continue;
        float effDepth = (c.depth - slop_) * posCorrectFactor_;
        CircleRb& bi = bodies_[(size_t)c.i];
        if (c.j >= 0) {
            CircleRb& bj = bodies_[(size_t)c.j];
            float wsum = bi.invMass + bj.invMass;
            if (wsum < 1e-9f) continue;
            float k = effDepth / wsum;
            bi.pos.x -= k * bi.invMass * c.normal.x;
            bi.pos.y -= k * bi.invMass * c.normal.y;
            bj.pos.x += k * bj.invMass * c.normal.x;
            bj.pos.y += k * bj.invMass * c.normal.y;
        } else {
            // wall — c.normal 은 body 가 밀려나는 방향.
            bi.pos.x += effDepth * c.normal.x;
            bi.pos.y += effDepth * c.normal.y;
        }
    }
}

void ResponseScene::ApplyPBD(float dt) {
    // PBD 패턴:
    //   1. predicted pos: prevPos = pos; vel += g*dt; pos += vel*dt;  (이미
    //      IntegrateGravity 에서 했지만 prevPos 가 적분 전 위치여야 한다.)
    //   2. iterations 반복: contact 마다 position 직접 보정.
    //   3. vel = (pos - prevPos) / dt
    //
    // 호출 흐름이 *Step* 안의 IntegrateGravity 다음 → 여기서 prevPos 는
    // *integrate 전 위치* 이어야 한다. main loop 에서 미리 저장.
    for (int iter = 0; iter < iterations_; ++iter) {
        std::vector<Contact> contacts;
        GatherContacts(contacts);
        for (const auto& c : contacts) {
            if (c.depth <= 0) continue;
            CircleRb& bi = bodies_[(size_t)c.i];
            if (c.j >= 0) {
                CircleRb& bj = bodies_[(size_t)c.j];
                float wsum = bi.invMass + bj.invMass;
                if (wsum < 1e-9f) continue;
                float k = c.depth / wsum;
                bi.pos.x -= k * bi.invMass * c.normal.x;
                bi.pos.y -= k * bi.invMass * c.normal.y;
                bj.pos.x += k * bj.invMass * c.normal.x;
                bj.pos.y += k * bj.invMass * c.normal.y;
            } else {
                bi.pos.x += c.depth * c.normal.x;
                bi.pos.y += c.depth * c.normal.y;
            }
        }
    }

    // velocity = (pos - prevPos) / dt
    float invDt = 1.0f / dt;
    for (auto& b : bodies_) {
        b.vel.x = (b.pos.x - b.prevPos.x) * invDt;
        b.vel.y = (b.pos.y - b.prevPos.y) * invDt;
    }
}

void ResponseScene::Step(float dt) {
    if (lastCanvasW_ <= 0) return;
    if (!initialized_) {
        ResetBodies(N_);
        initialized_ = true;
    }
    if ((int)bodies_.size() != N_) ResetBodies(N_);

    // PBD 를 위해 prevPos 저장 (integrate 전).
    for (auto& b : bodies_) b.prevPos = b.pos;

    auto t0 = std::chrono::high_resolution_clock::now();

    if (algo_ == ResponseAlgo::PBD) {
        // PBD: vel += g*dt; pos += vel*dt; iterations 반복 position projection.
        for (auto& b : bodies_) {
            b.vel.y += gravity_ * dt;
            b.pos.x += b.vel.x * dt;
            b.pos.y += b.vel.y * dt;
        }
        ApplyPBD(dt);
    } else {
        IntegrateGravity(dt);
        std::vector<Contact> contacts;
        GatherContacts(contacts);
        lastContacts_ = (int)contacts.size();
        if (algo_ == ResponseAlgo::Penalty) {
            ApplyPenalty(contacts, dt);
        } else if (algo_ == ResponseAlgo::Impulse) {
            ApplyImpulseOnce(contacts);
            ApplyPositionCorrection(contacts);
        } else {
            // SequentialImpulse
            ApplySequentialImpulse(contacts);
            // SI 도 position correction 별도 (split impulse 의 단순 버전).
            for (int it = 0; it < std::max(1, iterations_ / 2); ++it) {
                std::vector<Contact> cc;
                GatherContacts(cc);
                ApplyPositionCorrection(cc);
            }
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    lastMs_ = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // 측정 — 응답 후 남은 평균 / 최대 침투 깊이.
    std::vector<Contact> after;
    GatherContacts(after);
    float sum = 0, mx = 0;
    for (const auto& c : after) { sum += c.depth; mx = std::max(mx, c.depth); }
    lastAvgPen_ = after.empty() ? 0.0f : sum / (float)after.size();
    lastMaxPen_ = mx;
    lastContacts_ = (int)after.size();

    float ke = 0;
    for (const auto& b : bodies_) {
        ke += 0.5f * b.mass * (b.vel.x * b.vel.x + b.vel.y * b.vel.y)
                / (100.0f * 100.0f);  // m²/s² scale
    }

    penHist_.Push(lastAvgPen_);
    msHist_.Push((float)lastMs_);
    keHist_.Push(ke);
}

void ResponseScene::DrawWorld(int cx, int cy, int cw, int ch) {
    lastCanvasX_ = cx; lastCanvasY_ = cy;
    lastCanvasW_ = cw; lastCanvasH_ = ch;

    DrawRectangle(cx, cy, cw, ch, Color{20, 22, 28, 255});
    DrawRectangleLines(cx, cy, cw, ch, Color{60, 62, 70, 255});

    int floorY = cy + ch - 4;
    DrawLine(cx + 4, floorY, cx + cw - 4, floorY, Color{120, 130, 140, 255});

    Color algoCol{255, 255, 255, 255};
    switch (algo_) {
    case ResponseAlgo::Penalty:           algoCol = {255, 120, 120, 255}; break;
    case ResponseAlgo::Impulse:           algoCol = {255, 200, 100, 255}; break;
    case ResponseAlgo::SequentialImpulse: algoCol = {120, 200, 255, 255}; break;
    case ResponseAlgo::PBD:               algoCol = {120, 230, 130, 255}; break;
    }

    for (const auto& b : bodies_) {
        DrawCircleV(b.pos, b.radius,
                     Color{algoCol.r, algoCol.g, algoCol.b, 150});
        DrawCircleLines((int)b.pos.x, (int)b.pos.y, b.radius, algoCol);
    }

    const char* names[4] = {"Penalty", "Impulse (1-shot)",
                              "Sequential Impulse", "PBD"};
    DrawText(TextFormat("Step 6: Response = %s   N=%d   contacts=%d",
                          names[(int)algo_], N_, lastContacts_),
              cx + 10, cy + 10, 14, Color{200, 200, 200, 255});

    Rectangle gr1 = {(float)(cx + cw - 320), (float)(cy + 40), 300.0f, 100.0f};
    Rectangle gr2 = {(float)(cx + cw - 320), (float)(cy + 150), 300.0f, 100.0f};
    Rectangle gr3 = {(float)(cx + cw - 320), (float)(cy + 260), 300.0f, 100.0f};
    GraphInput in1[1] = {{&penHist_, algoCol, "avg penetration (px)"}};
    DrawLineGraph(gr1, in1, 1, "Avg penetration", 0, 1, true);
    GraphInput in2[1] = {{&msHist_, algoCol, "response ms"}};
    DrawLineGraph(gr2, in2, 1, "Response time (ms)", 0, 1, true);
    GraphInput in3[1] = {{&keHist_, algoCol, "total KE"}};
    DrawLineGraph(gr3, in3, 1, "Total kinetic energy", 0, 1, true);
}

void ResponseScene::DrawPanel(int panelX, int panelY, int panelW, int panelH) {
    (void)panelW; (void)panelH;
    int  y = panelY + 8;
    char buf[160];

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, "Response");
    y += 24;

    const char* names[4] = {"Penalty", "Impulse (1-shot)",
                              "Sequential Impulse", "PBD"};
    for (int i = 0; i < 4; ++i) {
        Rectangle r = {(float)(panelX + 8), (float)y, 240, 22};
        bool selected = ((int)algo_ == i);
        if (selected) DrawRectangleRec(r, Color{60, 100, 160, 255});
        if (GuiButton(r, names[i])) {
            algo_ = (ResponseAlgo)i;
            ResetBodies(N_);
            penHist_.Clear();
            msHist_.Clear();
            keHist_.Clear();
        }
        y += 26;
    }
    y += 6;

    std::snprintf(buf, sizeof(buf), "N: %d", N_);
    float fn = (float)N_;
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &fn, 4.0f, 100.0f);
    int nn = (int)fn;
    if (nn != N_) {
        N_ = nn;
        ResetBodies(N_);
        penHist_.Clear(); msHist_.Clear(); keHist_.Clear();
    }
    y += 22;

    std::snprintf(buf, sizeof(buf), "iterations: %d", iterations_);
    float fi = (float)iterations_;
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &fi, 1.0f, 32.0f);
    iterations_ = (int)fi;
    y += 22;

    std::snprintf(buf, sizeof(buf), "restitution: %.2f", restitution_);
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &restitution_, 0.0f, 1.0f);
    y += 22;

    std::snprintf(buf, sizeof(buf), "penalty k: %.0f", penaltyK_);
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &penaltyK_, 100.0f, 80000.0f);
    y += 28;

    if (GuiButton(Rectangle{(float)(panelX + 8), (float)y, 100, 22},
                   "Reset (R)")) {
        ResetBodies(N_);
        penHist_.Clear(); msHist_.Clear(); keHist_.Clear();
    }
    if (GuiButton(Rectangle{(float)(panelX + 116), (float)y, 100, 22},
                   "Pulse (Space)")) {
        for (auto& b : bodies_) b.vel.y -= 300.0f;
    }
    y += 28;

    std::snprintf(buf, sizeof(buf), "avg pen: %.2f px", lastAvgPen_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 18;
    std::snprintf(buf, sizeof(buf), "max pen: %.2f px", lastMaxPen_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 18;
    std::snprintf(buf, sizeof(buf), "contacts: %d", lastContacts_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 18;
    std::snprintf(buf, sizeof(buf), "response: %.3f ms", lastMs_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 24;

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "SI / PBD: increase iterations");
    y += 16;
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "to see avg pen drop.");
    y += 16;
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "Impulse (1-shot) jitters.");
}

}  // namespace physlab
